/// LLVM Code-Generator für moo — erzeugt nativen Maschinencode über LLVM IR.
/// Alle Werte sind MooValues. Alle Operationen gehen über die C-Runtime.

use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::passes::PassBuilderOptions;
use inkwell::targets::{
    CodeModel, FileType, InitializationConfig, RelocMode, Target, TargetMachine,
};
use inkwell::types::{BasicMetadataTypeEnum, BasicType, BasicTypeEnum, StructType};
use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, FunctionValue, PointerValue, StructValue};
use inkwell::OptimizationLevel;
use inkwell::AddressSpace;

use std::collections::HashMap;
use std::path::Path;

use crate::ast::*;
use crate::runtime_bindings::RuntimeBindings;

#[path = "codegen_defer.rs"]
mod defer_catalog;

/// MooTag-Werte — EINZIGE Stelle im Rust-Code mit Tag-Zahlen.
/// MUSS mit runtime/moo_runtime.h (typedef enum MooTag) synchron sein:
/// Drift heisst der Codegen erzeugt Werte mit falschem Tag -> die Runtime
/// interpretiert Pointer/Doubles falsch = die bekannte SEGV-Klasse.
/// Synchronitaet wird von tests/ir_gates/run_tag_sync_gate.sh erzwungen
/// (parst das C-enum und vergleicht; drittes Pruefziel: moo_wasm.c-#defines).
/// Neue Tags hier NUR ergaenzen wenn der Codegen sie wirklich braucht —
/// alles andere laeuft ueber Runtime-Konstruktoren (moo_none() etc.).
pub mod moo_tag {
    pub const STRING: u64 = 1;  // MOO_STRING
    pub const NONE: u64 = 3;    // MOO_NONE
    pub const OBJECT: u64 = 7;  // MOO_OBJECT
}
use moo_tag as tag;

/// P7-T20: Art des Bare-Inline-Schnellwegs (siehe CodeGen::bare_schnellweg).
#[derive(Clone, Copy)]
enum BareSchnell {
    /// 0 = add, 1 = sub, 2 = mul, 3 = div, 4 = mod
    Rechnen(u8),
    /// 0 = and, 1 = or, 2 = xor, 3 = lshift, 4 = rshift
    Bit(u8),
    BitNicht,
    Negativ,
    Vergleich(inkwell::FloatPredicate),
    Wahrheit,
    /// moo_number(double): nur Tag 0 + Bits, ohne kalten Pfad
    Zahl,
}

/// P2-T85 (Plan 983e8a9c E1): Art des aktuellen Codegen-Rahmens. Native Aufrufe
/// mit Argumentadaptern brauchen einen dynamischen Standardrahmen (MooValue-
/// Rueckgabe, Ende ueber emit_defers + release_function_locals).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum RahmenArt { Sonstig, Funktion, Methode, Lambda }

/// P2-T85 A1 (Plan 983e8a9c, Review c23f9a29): kompletter Rahmenzustand der
/// umgebenden Funktion. rahmen_betreten sichert und neutralisiert ihn beim
/// Eintritt in Funktion/Methode/Lambda, rahmen_verlassen stellt ihn wieder her;
/// so sickern weder Guard-Bloecke noch owned SSA-Werte eines aeusseren
/// Statement-Ausdrucks in den neuen Rahmen.
struct RahmenZustand<'ctx> {
    rahmen_art: RahmenArt,
    native_guard_enabled: bool,
    native_guard_exit: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    native_skip_target: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    native_try_direct: bool,
    stmt_root_guard: bool,
    stmt_recursive_guard: bool,
    stmt_owned_context: Vec<StructValue<'ctx>>,
}

/// Levenshtein-Distanz fuer "Meintest du...?" Vorschlaege
fn levenshtein(a: &str, b: &str) -> usize {
    let (la, lb) = (a.len(), b.len());
    let mut prev: Vec<usize> = (0..=lb).collect();
    let mut curr = vec![0; lb + 1];
    for (i, ca) in a.chars().enumerate() {
        curr[0] = i + 1;
        for (j, cb) in b.chars().enumerate() {
            let cost = if ca == cb { 0 } else { 1 };
            curr[j + 1] = (prev[j] + cost).min(prev[j + 1] + 1).min(curr[j] + 1);
        }
        std::mem::swap(&mut prev, &mut curr);
    }
    prev[lb]
}

pub struct CodeGen<'ctx> {
    context: &'ctx Context,
    module: Module<'ctx>,
    builder: Builder<'ctx>,
    rt: RuntimeBindings<'ctx>,
    runtime_windows_abi: bool,
    target_data: Option<inkwell::targets::TargetData>,
    native_structs: crate::native_struct::NativeStructRegistry<'ctx>,
    native_interfaces: crate::native_interface::NativeInterfaces,
    extern_c_names: std::collections::HashSet<String>,
    extern_bindings: crate::ffi_types::ExternRegistry<'ctx>,
    native_functions: HashMap<String, FunctionValue<'ctx>>,
    native_signatures: HashMap<String, crate::codegen_native::NSignature>,
    /// P2-T50 B4: puffer[T]-Positionen dyn aufrufbarer Systemkoerper (Some(T));
    /// native_signatures traegt dort T nur als Platzhalter (siehe dyn_signature).
    native_buffer_params: HashMap<String, Vec<Option<crate::native_system::PufferArt>>>,
    /// P2-T23/T53 B3: dyn aufrufbare Systemkoerper mit Zeigerergebnis als Griff
    /// (Zeigerziel: None = ptr[void], Some(Struktur)).
    native_handle_results: HashMap<String, Option<String>>,
    /// P2-T49 (Plan 38c59c64 B2): dyn aufrufbare Systemkoerper mit Strukturwert-
    /// Ergebnis (Strukturname; sret-Puffer -> moo_struct_box_value).
    native_struct_results: HashMap<String, String>,
    /// P2-T49 E5: Grund, warum eine native Funktion mit Strukturwerten nicht aus
    /// dynamischem Moo aufrufbar ist (Diagnose an der Aufrufstelle).
    native_dyn_sperre: HashMap<String, String>,
    /// Native Funktionswerte: Thunk (Lambda-Funktion) und Anzeigename je
    /// @nativ-Name, einmal je Modul erzeugt.
    native_wert_thunks: HashMap<String, (FunctionValue<'ctx>, PointerValue<'ctx>)>,
    /// P2-T49 B1: Laufzeitbeschreibungen v2 (__moo_struktur.<Name>), je Modul einmal.
    struktur_deskriptoren: HashMap<String, PointerValue<'ctx>>,
    /// P2-T83 B1 (Plan f3217c27 E3): @liest(n)-Pufferparameter (0-basiert) je Funktion.
    native_read_params: HashMap<String, std::collections::HashSet<usize>>,
    /// P2-T85 (Plan 983e8a9c E1): Art des aktuellen Rahmens (main = Sonstig).
    rahmen_art: RahmenArt,
    /// P2-T12 C (Plan 31194239): sichere @nativ-Koerper mit Kontrollfluss im
    /// System-Emitter (Sicher-Modus); dyn. ohne unsicher: aufrufbar.
    safe_native_functions: std::collections::HashSet<String>,
    native_ffi_functions: std::collections::HashSet<String>,
    // P2-T66: hosted x86_64-Systemkoerper (nur !bare_mode). write_object
    // schaltet IR-O2 ausschliesslich ein, wenn diese Liste nicht leer ist.
    hosted_system_functions: Vec<FunctionValue<'ctx>>,
    // P2-T66 (b): Namen der benannten Top-Level-Funktionen (keine Lambdas,
    // keine Methoden) fuer den dyn-Aufruf skalarer Systemkoerper.
    dyn_named_functions: std::collections::HashSet<String>,
    native_try_direct: bool,
    // P2-T78: Skip-Block des aktuellen Nicht-Direktform-Systemaufrufs; Adapter
    // springen bei Fehler dorthin (Aufruf liefert nichts, Flag bleibt gesetzt).
    native_skip_target: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    // P2-T82: funktionsweiter Pruefpunkt-Ausgang fuer benannte Funktionen mit
    // hosted fangbaren @nativ-Aufrufen ohne eigenes versuche:/aufraeumen
    // (native_guard_exit_block); try_stack bleibt davon unberuehrt.
    native_guard_enabled: bool,
    /// P2-T95: Quellzeile der gerade uebersetzten wirf-Anweisung.
    wirf_zeile: usize,
    /// P2-T95: Hauptquelldatei und Quelldatei je Modulfunktion (Hinweise).
    quelldatei: Option<String>,
    funktion_quellen: HashMap<String, String>,
    native_guard_exit: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    native_try_zero_arg_users: std::collections::HashSet<String>,
    variables: HashMap<String, PointerValue<'ctx>>,
    /// Globale Variablen (top-level Assignments) — als LLVM-Globals angelegt,
    /// damit sie aus jeder Funktion sichtbar sind. load_var/store_var fallen
    /// auf diese Map zurueck wenn die Variable nicht lokal ist.
    globals: HashMap<String, PointerValue<'ctx>>,
    current_function: Option<FunctionValue<'ctx>>,
    // Fuer break/continue
    loop_stack: Vec<(inkwell::basic_block::BasicBlock<'ctx>, inkwell::basic_block::BasicBlock<'ctx>, usize)>,
    /// Aktive try-Blocks (innermost zuletzt). Wenn wirf/Stmt::Throw im Codegen
    /// auf einen nicht-leeren Stack trifft, emittiert er nach dem moo_throw
    /// einen unconditional branch zum entsprechenden catch_bb — das terminiert
    /// den try-body an der Throw-Stelle und verhindert dass weiterer Code
    /// (inkl. gib_zurueck) den Throw "verschluckt".
    try_stack: Vec<inkwell::basic_block::BasicBlock<'ctx>>,
    // Guards are limited to the outer expression of a checked statement.
    stmt_root_guard: bool,
    stmt_recursive_guard: bool,
    stmt_owned_context: Vec<StructValue<'ctx>>,
    // Klassen-Konstruktoren: class_name -> FunctionValue
    class_constructors: HashMap<String, FunctionValue<'ctx>>,
    // Klassen-Methoden: (class_name, method_name) -> FunctionValue
    class_methods: HashMap<(String, String), FunctionValue<'ctx>>,
    // Vererbung: child_class -> parent_class (fuer Method-Dispatch-Walk)
    class_parents: HashMap<String, String>,
    // Statisches Typ-Tracking fuer Variablen die per `neu X()` erzeugt wurden.
    // name -> class_name. Wird fuer korrektes Method-Dispatch bei Vererbung
    // genutzt (sonst wuerde immer die erste gleichnamige Methode gewinnen).
    object_var_types: HashMap<String, String>,
    // Aktuelle Klasse waehrend compile_class_def laeuft — damit selbst.method()
    // in Methoden auf die richtige Klasse dispatcht.
    current_class: Option<String>,
    // Lambda-Mapping: variable_name -> LLVM function name
    lambda_names: HashMap<String, String>,
    // Lambda-Captures: lambda_fn_name -> [captured var names]
    lambda_captures: HashMap<String, Vec<String>>,
    // Lambda-Counter
    lambda_counter: usize,
    for_iter_counter: usize,
    method_obj_counter: usize,
    // Receiver-Slots der Methodenaufrufe, die direkt in main angelegt wurden.
    // Der main-Epilog gibt genau diese frei (fang_matrix, #38221).
    main_mcall_slots: Vec<PointerValue<'ctx>>,
    /// P2-T87: der naechste MethodCall laeuft nur ueber die Builtin-Arme
    /// (Builtin-Zweig der Methodenweiche, Klassenpfad schon geprueft).
    methode_nur_builtin: bool,
    // Bisher compilerseitiger Stack von Aktions-IDs im aktuellen Defer-Katalog.
    defer_stack: Vec<u32>,
    defer_catalog: defer_catalog::DeferCatalog,
    // Slot des aktuellen Aufrufs; der Runtime-Stack wird erst bei Bedarf angelegt.
    defer_frame: Option<PointerValue<'ctx>>,
    defer_runtime: bool,
    defer_safe_calls: std::collections::HashSet<String>,
    defer_guard_targets: std::collections::HashSet<String>,
    defer_borrowed_handler: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    // Crystal-inspiriert: Typ-Tracking fuer Warnungen (kein Enforcement!)
    variable_types: HashMap<String, &'static str>,
    warnings: Vec<crate::diagnose::Diagnose>,
    /// P2-T97: Quellzeile fuer Diagnosen (0 = unbekannt), siehe compile_stmt.
    diag_zeile: usize,
    // Rust-inspiriert: unsafe-Kontext Flag
    unsafe_context: bool,
    // Test-Framework: registrierte Tests (display_name, fn_name)
    test_names: Vec<(String, String)>,
    // Profiling-Modus
    profiling: bool,
    // Bare-Metal-Kernel-Modus (Plan-010): erlaubt die kern_*-Builtins.
    // Gesetzt von der CLI bei --no-stdlib / *-bare-Target oder --erlaube-kern.
    bare_mode: bool,
    // UEFI mit Container-Runtime verwaltet dynamische Werte per Refcount.
    bare_runtime_refcounts: bool,
    // P10-T14: explizit gekennzeichneter nativer Thread-Träger.
    native_thread_runtime: bool,
    // P011-A1/A3: Funktionen UND Daten-Globals mit @benutzt/@einstieg bzw.
    // @rohdaten_u32 — werden am Ende von compile_program in das
    // llvm.used-Global geschrieben (DCE-Schutz).
    used_globals: Vec<PointerValue<'ctx>>,
    /// P2-T28 (Plan 607b20ba B1): per @sektion belegte Sektionen (Name, Deklaration).
    sektionen: Vec<(String, String)>,
    /// P2-T114 Phase C: Laufzeitpruefung der Globalen (typpruefung::globale_pruefen).
    global_pruefplan: crate::typpruefung::GlobalePruefplan,
    /// P2-T114 B: Besitzer-Pruefung je Funktion einmal am Eintritt (i1 fremd).
    global_fremd_cache: std::cell::RefCell<HashMap<String, inkwell::values::IntValue<'ctx>>>,
}

pub const MOOS_USER_TARGET: &str = "x86_64-moos";

impl<'ctx> CodeGen<'ctx> {
    pub fn new(context: &'ctx Context, module_name: &str, target: &str) -> Result<Self, String> {
        let module = context.create_module(module_name);
        let triple_str = Self::resolve_triple(target);
        Target::initialize_all(&InitializationConfig::default());
        let triple = inkwell::targets::TargetTriple::create(&triple_str);
        let target_obj = Target::from_triple(&triple)
            .map_err(|e| format!("Target-Fehler fuer '{}': {}", triple_str, e.to_string()))?;
        let native = target.is_empty() || target == "native";
        let cpu = if native { TargetMachine::get_host_cpu_name().to_string() } else { "generic".to_string() };
        let features = if native { TargetMachine::get_host_cpu_features().to_string() } else { String::new() };
        let reloc = if triple_str.contains("-none") && target != MOOS_USER_TARGET {
            RelocMode::Static
        } else {
            RelocMode::PIC
        };
        let machine = target_obj.create_target_machine(
            &triple, &cpu, &features, OptimizationLevel::Default, reloc, CodeModel::Default,
        ).ok_or(format!("Konnte TargetMachine fuer '{}' nicht erstellen", triple_str))?;
        module.set_triple(&triple);
        let data = machine.get_target_data();
        module.set_data_layout(&data.get_data_layout());
        let target_data = Some(data);
        // x86_64 UEFI uses the same aggregate C ABI as Windows x64, although
        // its LLVM target triple contains `uefi` rather than `windows`.
        let windows_x64_abi = triple_str.starts_with("x86_64-")
            && (triple_str.contains("windows") || triple_str.contains("-uefi"));
        let builder = context.create_builder();
        let rt = RuntimeBindings::declare(context, &module, windows_x64_abi);

        Ok(Self {
            context,
            module,
            builder,
            rt,
            runtime_windows_abi: windows_x64_abi,
            target_data,
            native_structs: crate::native_struct::NativeStructRegistry::new(),
            native_interfaces: crate::native_interface::NativeInterfaces::default(),
            extern_c_names: std::collections::HashSet::new(),
            extern_bindings: crate::ffi_types::ExternRegistry::default(),
            native_functions: HashMap::new(),
            native_signatures: HashMap::new(),
            native_buffer_params: HashMap::new(),
            native_handle_results: HashMap::new(),
            native_struct_results: HashMap::new(),
            native_dyn_sperre: HashMap::new(),
            native_wert_thunks: HashMap::new(),
            struktur_deskriptoren: HashMap::new(),
            native_read_params: HashMap::new(),
            rahmen_art: RahmenArt::Sonstig,
            native_ffi_functions: std::collections::HashSet::new(),
            hosted_system_functions: Vec::new(),
            safe_native_functions: std::collections::HashSet::new(),
            dyn_named_functions: std::collections::HashSet::new(),
            native_try_direct: false,
            native_skip_target: None,
            native_guard_enabled: false,
            wirf_zeile: 0,
            quelldatei: None,
            funktion_quellen: HashMap::new(),
            native_guard_exit: None,
            native_try_zero_arg_users: std::collections::HashSet::new(),
            variables: HashMap::new(),
            globals: HashMap::new(),
            current_function: None,
            loop_stack: Vec::new(),
            try_stack: Vec::new(),
            stmt_root_guard: false,
            stmt_recursive_guard: false,
            stmt_owned_context: Vec::new(),
            class_constructors: HashMap::new(),
            class_methods: HashMap::new(),
            class_parents: HashMap::new(),
            object_var_types: HashMap::new(),
            current_class: None,
            lambda_names: HashMap::new(),
            lambda_captures: HashMap::new(),
            lambda_counter: 0,
            for_iter_counter: 0,
            method_obj_counter: 0,
            main_mcall_slots: Vec::new(),
            methode_nur_builtin: false,
            defer_stack: Vec::new(),
            defer_catalog: defer_catalog::DeferCatalog::default(),
            defer_frame: None,
            defer_runtime: false,
            defer_safe_calls: std::collections::HashSet::new(),
            defer_guard_targets: std::collections::HashSet::new(),
            defer_borrowed_handler: None,
            variable_types: HashMap::new(),
            warnings: Vec::new(),
            diag_zeile: 0,
            unsafe_context: false,
            test_names: Vec::new(),
            profiling: false,
            bare_mode: false,
            bare_runtime_refcounts: false,
            native_thread_runtime: false,
            used_globals: Vec::new(),
            sektionen: Vec::new(),
            global_pruefplan: Default::default(),
            global_fremd_cache: Default::default(),
        })
    }

    pub(crate) fn target_data(&self) -> Result<&inkwell::targets::TargetData, String> {
        self.target_data.as_ref().ok_or_else(||
            "Ziel-Datenlayout ist nicht initialisiert".to_string())
    }

    fn windows_x64_abi(&self) -> bool {
        self.runtime_windows_abi
    }

    pub fn set_profiling(&mut self, enabled: bool) {
        self.profiling = enabled;
    }

    /// Bare-Metal-Kernel-Modus (Plan-010). Wird von der CLI gesetzt, wenn
    /// --no-stdlib / ein *-bare-Target aktiv ist oder --erlaube-kern als
    /// explizites hosted-Opt-in uebergeben wurde. Steuert die kern_*-Builtins.
    pub fn set_bare_mode(&mut self, enabled: bool) {
        self.bare_mode = enabled;
    }

    pub fn set_bare_runtime_refcounts(&mut self, enabled: bool) {
        self.bare_runtime_refcounts = enabled;
    }

    /// Wache fuer kern_*-Builtins: harte hosted/bare-Trennung (GPT-Luecke 1+2,
    /// Thoughts 501192c2 + b740a19f). Ohne bare_mode ist ein kern_*-Aufruf ein
    /// Compile-Fehler mit klarem Hinweis statt eines kaputten hosted-Links.
    fn require_bare(&self, builtin: &str) -> Result<(), String> {
        if !self.bare_mode {
            return Err(format!(
                "Kernel-Builtin '{builtin}' ist nur im Bare-Metal-Build erlaubt. \
                 Kompiliere mit --no-stdlib und einem *-bare-Target (z.B. \
                 --target x86_64-bare) oder gib --erlaube-kern fuer ein \
                 explizites Opt-in (z.B. Host-Test-Harness) an."
            ));
        }
        Ok(())
    }

    /// Reservierte kern_*-Builtin-Namen (Plan-010, DE/EN-Aliases). Diese Namen
    /// sind IMMER als LLVM-Declarations im Modul (RuntimeBindings, C-Symbole
    /// heissen unpraefixt kern_*). Der User-Funktions-Vorrang in
    /// Expr::FunctionCall (module.get_function) wuerde sie sonst faelschlich
    /// als user_call dispatchen und require_bare() umgehen — genau die
    /// hosted/bare-Luecke, die C1 schliessen soll. Nur die EXAKTEN Namen sind
    /// reserviert; andere kern_*-User-Funktionen bleiben unberuehrt.
    fn is_kern_builtin(name: &str) -> bool {
        matches!(name,
            "kern_seriell_init" | "kern_serial_init"
            | "kern_seriell_zeichen" | "kern_serial_char"
            | "kern_seriell_dez" | "kern_serial_dec"
            | "kern_seriell_hex" | "kern_serial_hex"
            | "kern_vga_init"
            | "kern_vga_farbe" | "kern_vga_color"
            | "kern_vga_zeichen" | "kern_vga_char"
            | "kern_speicher_init" | "kern_mem_init"
            | "kern_alloc" | "kern_reserviere"
            | "kern_frei" | "kern_free"
            | "kern_speicher_frei" | "kern_mem_free_bytes"
            | "kern_speicher_belegt" | "kern_mem_used"
            | "kern_speicher_peak" | "kern_mem_peak"
            | "kern_idt_init" | "kern_pic_remap"
            | "kern_pic_maskiere" | "kern_pic_mask"
            | "kern_pic_demaskiere" | "kern_pic_unmask"
            | "kern_pic_eoi" | "kern_timer_init" | "kern_ticks"
            | "kern_stackprot_selbsttest"
            | "kern_inw" | "kern_outw" | "kern_inl" | "kern_outl"
            | "kern_rdmsr_lo" | "kern_rdmsr_hi" | "kern_wrmsr"
            | "kern_msr_lese_lo" | "kern_msr_lese_hi" | "kern_msr_schreibe"
            | "kern_cr_lese" | "kern_cr_read" | "kern_cr_setze" | "kern_cr_write"
            | "kern_lgdt" | "kern_lidt"
            // P7-T2: Framebuffer-Builtins (UEFI-GOP heute, eigener Loader spaeter)
            | "kern_bild_breite" | "kern_fb_width"
            | "kern_bild_hoehe" | "kern_bild_höhe" | "kern_fb_height"
            | "kern_bild_zeige" | "kern_fb_present"
            // P7-T2: Eingabe im Boot-Umfeld (UEFI ConIn / Pointer-Protokolle)
            | "kern_taste_lesen" | "kern_key_read"
            | "kern_maus_vorhanden" | "kern_mouse_present"
            | "kern_maus_lesen" | "kern_mouse_poll"
            | "kern_maus_x" | "kern_mouse_x"
            | "kern_maus_y" | "kern_mouse_y"
            | "kern_maus_gedrueckt" | "kern_maus_gedrückt" | "kern_mouse_pressed"
            | "kern_eingabe_warten" | "kern_input_wait"
            // P7-T9: Netzwerk-Konsole (UEFI SimpleNetworkProtocol, UDP-Broadcast)
            | "kern_netz_sende" | "kern_net_send"
            | "kern_netz_status" | "kern_net_status"
            // P7-T9: Status der vom Datentraeger geladenen UEFI-Treiber
            | "kern_treiber_status" | "kern_driver_status"
            // P7-T14: ACPI-Wurzel (RSDP) aus der UEFI-Konfigurationstabelle
            | "kern_acpi_rsdp"
            // P7-T15: Wartezeit (UEFI Stall) und Text <-> Bytepuffer fuer Treiber in Moo
            | "kern_warte_us" | "kern_wait_us"
            | "kern_text_in_puffer" | "kern_text_to_buffer"
            | "kern_text_aus_puffer" | "kern_text_from_buffer"
            // P7-T10: EFI-Image aus dem Speicher starten (Netz-Lader)
            | "kern_bild_starten" | "kern_image_start"
        )
    }

    /// P011-A2: Fixed-width-Cast-Builtins als_u8..als_u64 (EN as_u8..as_u64).
    fn fixed_cast_bits(name: &str) -> Option<u32> {
        match name {
            "als_u8" | "as_u8" => Some(8),
            "als_u16" | "as_u16" => Some(16),
            "als_u32" | "as_u32" => Some(32),
            "als_u64" | "as_u64" => Some(64),
            _ => None,
        }
    }

    /// P011-A2: double -> unsigned wrap der Zielbreite -> double.
    /// Semantik: saturierende Konversion nach i64 (llvm.fptosi.sat: NaN -> 0,
    /// out-of-range -> i64::MIN/MAX, KEIN poison), dann trunc auf Zielbreite
    /// (Zweierkomplement-Wrap: -1 -> max), dann unsigned zurueck nach double.
    /// Grenze: double traegt 53 Mantissen-Bits — als_u64 > 2^53 rundet (dokumentiert).
    /// Reine IR-Emission, hosted UND bare (kein require_bare — pure Arithmetik).
    fn compile_fixed_width_cast(
        &mut self,
        bits: u32,
        name: &str,
        args: &[Expr],
    ) -> Result<StructValue<'ctx>, String> {
        if args.len() != 1 {
            return Err(format!("{name} erwartet genau 1 Argument"));
        }
        let v = self.compile_expr(&args[0])?;
        let data = self
            .builder
            .build_extract_value(v, 1, "cast_data")
            .map_err(|e| format!("{e}"))?
            .into_int_value();
        let f = self
            .builder
            .build_bit_cast(data, self.context.f64_type(), "cast_f")
            .map_err(|e| format!("{e}"))?
            .into_float_value();
        // Owning-Konvention: compile_expr liefert +1 — Temp freigeben.
        self.call_rt_void(self.rt.moo_release, &[v.into()], "cast_rel")?;
        let i64_ty = self.context.i64_type();
        let sat = inkwell::intrinsics::Intrinsic::find("llvm.fptosi.sat")
            .ok_or("llvm.fptosi.sat-Intrinsic nicht gefunden")?;
        let sat_fn = sat
            .get_declaration(&self.module, &[i64_ty.into(), self.context.f64_type().into()])
            .ok_or("llvm.fptosi.sat-Declaration fehlgeschlagen")?;
        let as_i64 = match self
            .builder
            .build_call(sat_fn, &[f.into()], "cast_sat")
            .map_err(|e| format!("{e}"))?
            .try_as_basic_value()
        {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("fptosi.sat ohne Rueckgabewert".to_string()),
        };
        let wrapped = if bits < 64 {
            let small_ty = match bits {
                8 => self.context.i8_type(),
                16 => self.context.i16_type(),
                _ => self.context.i32_type(),
            };
            let t = self
                .builder
                .build_int_truncate(as_i64, small_ty, "cast_trunc")
                .map_err(|e| format!("{e}"))?;
            self.builder
                .build_int_z_extend(t, i64_ty, "cast_zext")
                .map_err(|e| format!("{e}"))?
        } else {
            as_i64
        };
        let back = self
            .builder
            .build_unsigned_int_to_float(wrapped, self.context.f64_type(), "cast_back")
            .map_err(|e| format!("{e}"))?;
        self.call_rt(self.rt.moo_number, &[back.into()], "cast_num")
    }


    /// P011-A3: Compile-Zeit-Konstantenfaltung fuer RawData-Werte.
    /// Unterstuetzt: Number-Literale, unaeres Minus, +,-,*,/,%,** auf
    /// konstanten Operanden sowie als_uN/as_uN mit konstantem Argument
    /// (Wrap-Semantik identisch zur Laufzeit: Rust `as` ist saturierend +
    /// NaN->0, exakt wie llvm.fptosi.sat; danach trunc-Wrap).
    fn eval_const_f64(expr: &Expr) -> Result<f64, String> {
        match expr {
            Expr::Number(n) => Ok(*n),
            Expr::UnaryOp { op: crate::ast::UnaryOpKind::Neg, operand } => {
                Ok(-Self::eval_const_f64(operand)?)
            }
            Expr::BinaryOp { left, op, right } => {
                let a = Self::eval_const_f64(left)?;
                let b = Self::eval_const_f64(right)?;
                match op {
                    BinOp::Add => Ok(a + b),
                    BinOp::Sub => Ok(a - b),
                    BinOp::Mul => Ok(a * b),
                    BinOp::Div => Ok(a / b),
                    BinOp::Mod => Ok(a % b),
                    BinOp::Pow => Ok(a.powf(b)),
                    _ => Err("Nur arithmetische Operatoren in konstanten Ausdruecken".to_string()),
                }
            }
            Expr::FunctionCall { name, args, .. } => {
                if let Some(bits) = Self::fixed_cast_bits(name) {
                    if args.len() != 1 {
                        return Err(format!("{name} erwartet genau 1 Argument"));
                    }
                    let v = Self::eval_const_f64(&args[0])?;
                    let i = v as i64;
                    let wrapped: u64 = match bits {
                        8 => (i as u8) as u64,
                        16 => (i as u16) as u64,
                        32 => (i as u32) as u64,
                        _ => i as u64,
                    };
                    return Ok(wrapped as f64);
                }
                Err(format!("Funktion '{name}' ist nicht compile-time-konstant"))
            }
            _ => Err("Ausdruck ist nicht compile-time-konstant".to_string()),
        }
    }

    /// Uebersetzt portable @sektion-Namen fuer Mach-O in die dort erforderliche
    /// Segment/Section-Form. Funktionen muessen in einer ausfuehrbaren
    /// __TEXT-Section liegen; Rohdaten gehen nach __DATA. Vollstaendige native
    /// Mach-O-Angaben (mit Komma) bleiben fuer Experten unveraendert erlaubt.
    /// P2-T28 (Plan 607b20ba B1): ob eine native Deklaration per @sektion in
    /// diese Sektion gelegt wurde (z. B. ".multiboot2" fuer build_kernel).
    pub fn hat_sektion(&self, name: &str) -> bool {
        self.sektionen.iter().any(|(n, _)| n == name)
    }

    fn target_section_name(&self, requested: &str, executable: bool) -> Result<String, String> {
        let triple = self.module.get_triple();
        let triple = triple.as_str().to_str().unwrap_or("");
        if !triple.contains("apple") && !triple.contains("darwin") {
            return Ok(requested.to_string());
        }
        if requested.contains(',') {
            return Ok(requested.to_string());
        }
        let base = requested.trim_start_matches('.');
        if base.is_empty() || !base.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_') {
            return Err(format!(
                "@sektion: Mach-O-Name '{requested}' darf nach dem optionalen Punkt nur ASCII-Buchstaben, Ziffern und '_' enthalten"
            ));
        }
        let section = format!("__{base}");
        if section.len() > 16 {
            return Err(format!(
                "@sektion: Mach-O-Section '{section}' ist laenger als 16 Bytes (maximal 14 Zeichen im portablen Namen)"
            ));
        }
        if executable {
            Ok(format!("__TEXT,{section},regular,pure_instructions"))
        } else {
            Ok(format!("__DATA,{section}"))
        }
    }

    /// P011-A3 PoC: lowert @rohdaten_u32-Konstanten zu reinen i32-Array-Globals
    /// (Layout-Daten, KEIN MooValue — nicht als moo-Wert lesbar; dokumentierte
    /// PoC-Grenze). Implizit 'used' (llvm.used), @sektion/@ausrichten anwendbar.
    fn compile_raw_data_u32(
        &mut self,
        name: &str,
        value: &Expr,
        decorators: &[crate::ast::Decorator],
    ) -> Result<(), String> {
        let items = match value {
            Expr::List(items) => items,
            _ => return Err(format!("@rohdaten_u32 '{name}': Wert muss ein Listen-Literal sein")),
        };
        if items.is_empty() {
            return Err(format!("@rohdaten_u32 '{name}': Liste darf nicht leer sein"));
        }
        let i32_ty = self.context.i32_type();
        let mut consts = Vec::with_capacity(items.len());
        for (idx, it) in items.iter().enumerate() {
            let f = Self::eval_const_f64(it)
                .map_err(|e| format!("@rohdaten_u32 '{name}' Element {idx}: {e}"))?;
            let i = f as i64; // saturierend (wie fptosi.sat)
            let w = i as u32; // Zweierkomplement-Wrap (als_u32-Semantik)
            consts.push(i32_ty.const_int(w as u64, false));
        }
        let arr_ty = i32_ty.array_type(consts.len() as u32);
        let g = self.module.add_global(arr_ty, None, name);
        g.set_initializer(&i32_ty.const_array(&consts));
        for d in decorators {
            match d.name.as_str() {
                "rohdaten_u32" | "rawdata_u32" => {}
                "sektion" | "section" => match d.args.as_slice() {
                    [crate::ast::DecoratorArg::Text(s)] => {
                        // P2-T28 (Review 4571f610): der Multiboot2-Kopf ist eine @nativ konstante.
                        if s == ".multiboot2" {
                            return Err(format!(
                                "@{}(\".multiboot2\") ist nur fuer eine @nativ konstante (Multiboot2-Kopf) zulaessig, nicht fuer @rohdaten_u32",
                                d.name
                            ));
                        }
                        let section = self.target_section_name(s, false)?;
                        g.set_section(Some(&section));
                    }
                    _ => return Err(format!("@{} erwartet genau ein Text-Argument", d.name)),
                },
                "ausrichten" | "align" => match d.args.as_slice() {
                    [crate::ast::DecoratorArg::Zahl(n)] => {
                        let a = *n as u64;
                        if *n <= 0.0 || (*n != a as f64) || (a & a.wrapping_sub(1)) != 0 {
                            return Err(format!("@{} erwartet eine positive Zweierpotenz", d.name));
                        }
                        g.set_alignment(a as u32);
                    }
                    _ => return Err(format!("@{} erwartet genau ein Zahl-Argument", d.name)),
                },
                other => {
                    return Err(format!(
                        "Decorator @{other} ist auf @rohdaten_u32-Konstanten nicht erlaubt"
                    ));
                }
            }
        }
        // Layout-Daten sind per Definition 'used' — sonst kann DCE sie kippen.
        self.used_globals.push(g.as_pointer_value());
        Ok(())
    }

    /// P7-T4 (2A): @sektion/@ausrichten/@benutzt (EN section/align/used) an
    /// nativen Konstanten und @nativ-Funktionen; @einstieg/@entry impliziert
    /// benutzt. Objektalignment ist getrennt vom Typalignment, darf es aber
    /// nicht unterschreiten. Andere Decorators prueft der Aufrufer.
    fn apply_native_layout_decorators(
        &mut self,
        global: inkwell::values::GlobalValue<'ctx>,
        decorators: &[crate::ast::Decorator],
        executable: bool,
        type_alignment: u32,
        what: &str,
    ) -> Result<(), String> {
        let mut seen = std::collections::HashSet::new();
        let mut used = false;
        for d in decorators {
            let kind = match d.name.as_str() {
                "sektion" | "section" => "sektion",
                "ausrichten" | "align" => "ausrichten",
                "benutzt" | "used" => "benutzt",
                "einstieg" | "entry" => "einstieg",
                "verschiebbar" | "relocatable" if executable => { used = true; continue; }
                _ => continue,
            };
            if !seen.insert(kind) {
                return Err(format!("{what}: @{} ist doppelt angegeben", d.name));
            }
            match (kind, d.args.as_slice()) {
                ("sektion", [crate::ast::DecoratorArg::Text(s)]) => {
                    // P2-T28 (Plan 607b20ba B3): genau ein Multiboot2-Kopf, nur x86_64.
                    if s == ".multiboot2" {
                        // Review 4571f610: genau eine @nativ konstante ist der Kopf.
                        if executable {
                            return Err(format!(
                                "{what}: @{}(\".multiboot2\") ist nur fuer eine @nativ konstante (Multiboot2-Kopf) zulaessig",
                                d.name
                            ));
                        }
                        let triple = self.module.get_triple();
                        let triple = triple.as_str().to_string_lossy().into_owned();
                        if !triple.starts_with("x86_64") {
                            return Err(format!(
                                "{what}: @{}(\".multiboot2\") gibt es nur fuer x86_64 (Multiboot2 ist ein x86-Bootprotokoll), Ziel ist {triple}",
                                d.name
                            ));
                        }
                        if let Some((_, frueher)) = self.sektionen.iter().find(|(n, _)| n == ".multiboot2") {
                            return Err(format!(
                                "{what}: Sektion .multiboot2 ist bereits durch {frueher} belegt (genau ein Multiboot2-Kopf)"
                            ));
                        }
                    }
                    let section = self.target_section_name(s, executable)?;
                    global.set_section(Some(&section));
                    self.sektionen.push((s.clone(), what.to_string()));
                }
                ("ausrichten", [crate::ast::DecoratorArg::Zahl(n)]) => {
                    let a = *n as u64;
                    if *n <= 0.0 || *n != a as f64 || !a.is_power_of_two() || a > (1u64 << 29) {
                        return Err(format!("{what}: @{} erwartet eine positive Zweierpotenz, bekommen: {n}", d.name));
                    }
                    if (a as u32) < type_alignment {
                        return Err(format!("{what}: @{} ({a}) ist kleiner als das Typalignment {type_alignment}", d.name));
                    }
                    global.set_alignment(a as u32);
                }
                ("benutzt" | "einstieg", []) => used = true,
                _ => return Err(format!("{what}: ungültige Argumente für @{}", d.name)),
            }
        }
        if used {
            self.used_globals.push(global.as_pointer_value());
        }
        Ok(())
    }

    /// Crystal-inspiriert: Bestimmt den statischen Typ einer Expression (wenn bekannt)
    fn infer_type(&self, expr: &Expr) -> Option<&'static str> {
        match expr {
            Expr::Number(_) => Some("Zahl"),
            Expr::String(_) => Some("Text"),
            Expr::Boolean(_) => Some("Bool"),
            Expr::None => Some("Nichts"),
            Expr::List(_) | Expr::ListComprehension { .. } => Some("Liste"),
            Expr::Dict(_) => Some("Dict"),
            Expr::Range { .. } => Some("Liste"),
            Expr::Identifier(name) => self.variable_types.get(name.as_str()).copied(),
            _ => None,
        }
    }

    fn mv_type(&self) -> StructType<'ctx> {
        self.rt.moo_value_type
    }

    // Ein eigener, NULL-initialisierter Slot pro Aktivierung; noch ohne Runtime-Aufruf.
    fn init_defer_frame(&mut self) -> Result<(), String> {
        self.defer_frame = if self.defer_catalog.actions().is_empty() {
            None
        } else {
            let ptr = self.context.ptr_type(AddressSpace::default());
            let slot = self.builder.build_alloca(ptr, "defer.stack.slot")
                .map_err(|e| format!("{e}"))?;
            self.builder.build_store(slot, ptr.const_null()).map_err(|e| format!("{e}"))?;
            Some(slot)
        };
        Ok(())
    }

    // One registration path for compilation and the read-only layout diagnostic.
    fn register_native_structs(&mut self, program: &Program) -> Result<(), String> {
        // P2-T16 (Plan 3f03704a E1/E2): eingebaute vec2/vec3/vec4/mat4 nur bei
        // Bedarf. Eine eigene Definition gleichen Namens (struktur, Funktion,
        // Klasse) gewinnt; eine eigene struktur dann mit Warnung (Freigabe #38101).
        let eigene: std::collections::HashSet<&str> = program.statements.iter()
            .filter_map(|stmt| match stmt {
                Stmt::StructDef { name, .. } | Stmt::FunctionDef { name, .. }
                | Stmt::ClassDef { name, .. } | Stmt::DataClassDef { name, .. } => Some(name.as_str()),
                _ => None,
            })
            .collect();
        for stmt in &program.statements {
            if let Stmt::StructDef { name, .. } = stmt {
                if crate::native_struct::vecmat_art_name(name).is_some() || crate::native_simd::art(name).is_some() {
                    let warnung = self.diag_ort(crate::diagnose::Diagnose::hinweis("M-H002",
                        format!("eigene struktur '{name}' ueberdeckt den eingebauten Typ {name} (ohne eingebaute Operatoren)")));
                    if !self.warnings.iter().any(|w| w.meldung == warnung.meldung) { self.warnings.push(warnung); }
                }
            }
        }
        let eingebaute: Vec<(&'static str, Vec<(String, TypeRef)>)> = crate::native_vecmat::bedarf(&program.statements)
            .into_iter()
            .filter(|name| !eigene.contains(name))
            .filter_map(|name| crate::native_struct::vecmat_felder(name).or_else(|| crate::native_simd::felder(name)).map(|felder| (name, felder)))
            .collect();
        if eingebaute.iter().any(|(name, _)| *name == "mat4") && eigene.contains("vec4") {
            return Err("eingebauter Typ mat4 braucht den eingebauten Typ vec4; eine eigene Definition 'vec4' verdeckt ihn".into());
        }
        let mut definitions: Vec<(&str, &[(String, TypeRef)], &[Decorator])> = program.statements.iter()
            .filter_map(|stmt| match stmt {
                Stmt::StructDef { name, fields, decorators, .. } =>
                    Some((name.as_str(), fields.as_slice(), decorators.as_slice())),
                _ => None,
            })
            .collect();
        let keine: &[Decorator] = &[];
        definitions.extend(eingebaute.iter().map(|(name, felder)| (*name, felder.as_slice(), keine)));
        if !definitions.is_empty() {
            let target = self.target_data.as_ref()
                .ok_or_else(|| "Ziel-Datenlayout für Struktur fehlt".to_string())?;
            self.native_structs.register_all(self.context, target, &definitions)?;
            for stmt in &program.statements {
                if let Stmt::StructDef { name, bitfelder, .. } = stmt {
                    self.native_structs.register_bitfelder(name, bitfelder)?;
                }
            }
        }
        for (name, _) in &eingebaute {
            self.native_structs.markiere_eingebaut(name);
        }
        Ok(())
    }

    pub(crate) fn diagnostic_layouts(&mut self, program: &Program)
        -> Result<crate::native_struct::LayoutSnapshot, String>
    {
        self.register_native_structs(program)?;
        self.native_structs.snapshot()
    }

    /// P2-T13/T48 (Plan a71947f7 E1): dieselbe Registrierung wie
    /// diagnostic_layouts, ausgegeben als vom Ziel-C-Compiler prüfbarer Header.
    pub(crate) fn diagnostic_c_header(&mut self, program: &Program, triple: &str, guard: &str)
        -> Result<String, String>
    {
        self.register_native_structs(program)?;
        let target = self.target_data.as_ref()
            .ok_or_else(|| "Ziel-Datenlayout für C-Header fehlt".to_string())?;
        self.native_structs.c_header(self.context, target, triple, guard)
    }

    /// Kompiliert ein ganzes Programm
    /// P2-T95: Quelldatei der Hauptdatei fuer Fehlerhinweise (Datei:Zeile).
    pub fn set_quelldatei(&mut self, datei: &str) {
        self.quelldatei = Some(datei.to_string());
    }

    /// P2-T97: Codegen mit einheitlichen Diagnosen. Hinweise (self.warnings)
    /// gehen an den Diagnose-Sammler; ein Fehler ohne strukturierte Diagnose
    /// bekommt hier Code (M-N000 in nativem Code, sonst M-C000) und Ort.
    pub fn compile_program(&mut self, program: &Program) -> Result<(), String> {
        // P2-T100: letztes Netz. Ein Panic im Codegen wird M-C999 mit Ort und
        // innerstem offenen Builtin-Aufruf statt eines Absturzes.
        let ergebnis = match crate::interner_fehler::im_netz(|| self.compile_program_kern(program)) {
            Ok(ergebnis) => ergebnis,
            Err(panik) => {
                // Nachweislich ein Lesen ueber das Ende der Argumente des
                // innersten Builtin-Aufrufs (exakte args[..]-Stelle, Laenge =
                // Argumentzahl): Nutzerfehler M-C003. Sonst M-C999.
                let ueberlauf = panik.aufruf.as_ref().zip(panik.ort.as_ref()).and_then(|(a, (datei, z, s))| {
                    crate::builtin_aritaet::args_ueberlauf(&a.name, a.anzahl, datei, *z, *s, &panik.nachricht)
                        .map(|index| (a, index + 1))
                });
                let mut d = match ueberlauf {
                    Some((a, mindestens)) => {
                        let wort = if mindestens == 1 { "Argument" } else { "Argumente" };
                        crate::diagnose::Diagnose::fehler("M-C003", format!(
                            "'{}' braucht mindestens {mindestens} {wort}, bekommen {}.", a.name, a.anzahl))
                            .erwartet(format!("mindestens {mindestens} {wort}"))
                            .gefunden(format!("{}", a.anzahl))
                            .vorschlag(format!("'{}' mit mindestens {mindestens} {} aufrufen.", a.name,
                                if mindestens == 1 { "Argument" } else { "Argumenten" }))
                    }
                    None => crate::diagnose::Diagnose::fehler("M-C999", panik.meldung()),
                };
                if let Some(a) = panik.aufruf.as_ref().filter(|a| a.zeile > 0) {
                    d = d.ort(a.zeile, 0);
                }
                let meldung = d.meldung.clone();
                crate::diagnose::melden(self.diag_ort(d));
                Err(meldung)
            }
        };
        // x86-Firmware und Kernel garantieren keine Red-Zone unter RSP.
        // Auch @nativ(SysV) und spaeter eingebettete Leaf-Funktionen brauchen
        // das LLVM-Attribut; -mno-red-zone beim C-Link allein reicht nicht.
        let triple = self.module.get_triple();
        let triple = triple.as_str().to_string_lossy();
        if ergebnis.is_ok() && triple.starts_with("x86_64-")
            && (triple.contains("-none") || triple.contains("-uefi"))
        {
            let kind = inkwell::attributes::Attribute::get_named_enum_kind_id("noredzone");
            let attribute = self.context.create_enum_attribute(kind, 0);
            for function in self.module.get_functions() {
                if function.get_first_basic_block().is_some() {
                    function.add_attribute(inkwell::attributes::AttributeLoc::Function, attribute);
                }
            }
        }
        for hinweis in std::mem::take(&mut self.warnings) {
            crate::diagnose::melden(hinweis);
        }
        if let Err(e) = &ergebnis {
            if !crate::diagnose::hat_fehler() {
                let standard = if self.diag_in_nativ() { "M-N000" } else { "M-C000" };
                // P2-T98: vorgemerkter Speicherfehler-Code (M-M...) samt Zeile.
                let (code, zeile, (spalte, ende)) = crate::diagnose::vorgemerkt(e).unwrap_or((standard, 0, (0, 0)));
                let mut d = crate::diagnose::Diagnose::fehler(code, e.clone());
                if zeile > 0 {
                    d = d.ort(zeile, spalte).end_spalte(ende);
                }
                if let Some((_, vorschlag)) = e.split_once("; Vorschlag: ") {
                    d = d.vorschlag(vorschlag);
                }
                crate::diagnose::melden(self.diag_ort(d));
            }
        }
        ergebnis
    }

    fn compile_program_kern(&mut self, program: &Program) -> Result<(), String> {
        let (prepared, interfaces) = crate::native_interface::prepare(program)?;
        self.native_interfaces = interfaces;
        let program = &prepared;
        self.native_thread_runtime = crate::runtime_provider::enabled(program);
        if self.native_thread_runtime { self.bare_runtime_refcounts = true; }
        self.funktion_quellen = program.funktion_quellen.clone();
        self.register_native_structs(program)?;
        // P7-T4 (1A): Layoutabfragen und skalare native Konstanten falten.
        let folded = crate::native_const::fold_program(
            program, self.context, self.target_data.as_ref(), &self.native_structs,
        )?;
        let program = &folded;
        // Zuerst alle Funktionen und Klassen deklarieren (Forward-Declarations)
        // P7-T5: native Konstanten entstehen in forward_declare vor den nativen
        // Koerpern, damit diese ihre Adressen nehmen koennen (adresse_von).
        self.forward_declare(program)?;
        for stmt in &program.statements {
            if let Stmt::NativeConst { name, decorators, .. } = stmt {
                if decorators.iter().any(|d| d.name == "pro_kern") { continue; }
                let global = self.module.get_global(name).ok_or("Native Konstante fehlt im Modul")?;
                let alignment = self.target_data.as_ref()
                    .map(|target| target.get_abi_alignment(&global.get_value_type())).unwrap_or(1);
                self.apply_native_layout_decorators(global, decorators, false, alignment, &format!("Native Konstante '{name}'"))?;
            }
        }
        for fun in self.module.get_functions() {
            if let Some(attr) = fun.get_string_attribute(inkwell::attributes::AttributeLoc::Function, crate::native_hardware::TRAMPOLIN_ATTR) {
                let section = attr.get_string_value().to_str().map_err(|e| e.to_string())?;
                if self.sektionen.iter().filter(|(s, _)| s == section).count() != 1 {
                    return Err(format!("Verschiebbare Sektion '{section}' verlangt genau einen eigenen Koerper und keine weiteren Daten/Funktionen"));
                }
            }
        }
        self.prepare_runtime_defer_graph(program)?;

        // main-Funktion erstellen
        let i32_type = self.context.i32_type();
        let main_type = i32_type.fn_type(&[], false);
        let main_fn = self.module.add_function("main", main_type, None);
        let entry = self.context.append_basic_block(main_fn, "entry");
        self.builder.position_at_end(entry);
        self.current_function = Some(main_fn);
        self.defer_catalog = defer_catalog::DeferCatalog::collect(&program.statements)?;
        self.init_defer_frame()?;

        // Top-Level-Assignments als LLVM-Globals anlegen — damit sie aus
        // Funktionen heraus sichtbar sind (load_var/store_var fallen auf
        // self.globals zurueck wenn eine Variable nicht lokal gefunden wird).
        let none_init = self.context.i64_type().const_int(tag::NONE, false);
        let zero_data = self.context.i64_type().const_int(0, false);
        let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
        for stmt in &program.statements {
            if let Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. } = stmt {
                if !self.globals.contains_key(name) {
                    let g = self.module.add_global(self.mv_type(), None, &format!("__moo_g_{name}"));
                    g.set_initializer(&none_val);
                    g.set_linkage(inkwell::module::Linkage::Internal);
                    let ptr = g.as_pointer_value();
                    self.globals.insert(name.clone(), ptr);
                    // Im main-Frame sieht variables direkt den Global-Pointer,
                    // damit store_var den existing-Path nimmt (release+retain).
                    self.variables.insert(name.clone(), ptr);
                }
            }
        }


        // Pre-Scan: Alle restlichen Variablennamen vorab im Entry-Block allokieren
        // Verhindert dass alloca in bedingten Bloecken/Schleifen erzeugt wird
        let var_names = Self::collect_all_var_names(&program.statements);
        for name in &var_names {
            if !self.variables.contains_key(name.as_str()) {
                let alloca = self.builder.build_alloca(self.mv_type(), name)
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(alloca, none_val).map_err(|e| format!("{e}"))?;
                self.variables.insert(name.clone(), alloca);
            }
        }

        let main_defer_handler = self.defer_frame_begin()?;
        for stmt in &program.statements {
            // Ein Return beendet den Laufzeitpfad, nicht die Emission bereits
            // vorwaerts deklarierter Funktions-/Klassen-/Testkoerper.
            if self.builder.get_insert_block().unwrap().get_terminator().is_some()
                && !matches!(stmt, Stmt::FunctionDef { .. } | Stmt::ClassDef { .. }
                    | Stmt::DataClassDef { .. } | Stmt::TestDef { .. }) { continue; }
            // Nur direkte Top-Level-Externs wurden vorab deklariert.
            // Verschachtelte Deklarationen bleiben in compile_stmt fail-closed.
            if matches!(stmt, Stmt::ExternC { .. } | Stmt::StructDef { .. } | Stmt::NativeConst { .. }) { continue; }
            if let Stmt::FunctionDef { name, .. } = stmt {
                if self.native_functions.contains_key(name) { continue; }
            }
            self.compile_stmt(stmt)?;
        }

        // Profiling: Report am Ende von main ausgeben
        if self.profiling {
            if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                self.builder.build_call(self.rt.moo_profile_report, &[], "prof_report")
                    .map_err(|e| format!("{e}"))?;
            }
        }

        // Sicherstellen dass main terminiert ist
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            // Wie bei Funktionen muessen Defers vor dem Local-Cleanup laufen.
            self.defer_frame_finish()?;
            // Normal-main-Epilog: Nur die per collect_all_var_names vorallokierten
            // Main-Frame-Locals freigeben. Direkte Top-Level-Zuweisungen sind
            // LLVM-Globals und behalten bewusst ihre bisherige Prozess-Lifetime.
            for name in &var_names {
                if self.globals.contains_key(name) {
                    continue;
                }
                if let Some(ptr) = self.variables.get(name).copied() {
                    let val = self.builder.build_load(self.mv_type(), ptr, "main_local_exit")
                        .map_err(|e| format!("{e}"))?.into_struct_value();
                    self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_main_local")?;
                }
            }
            // Receiver-Slots der Methodenaufrufe (siehe MethodCall) gehoeren
            // ebenfalls zum Main-Frame. In Funktionen gibt sie
            // release_function_locals frei; in main blieb der letzte temporaere
            // Receiver (z. B. `neu K().m()`) sonst bis Prozessende unerreichbar
            // (LSan-Befund skripte/p2/ffi/fang_matrix.sh, Fall 12). Freigegeben
            // wird nur, was bei der Anlage in main_mcall_slots registriert wurde,
            // kein Namensfilter: Benutzer-Locals sind oben schon freigegeben.
            for ptr in self.main_mcall_slots.clone() {
                let val = self.builder.build_load(self.mv_type(), ptr, "main_mcall_exit")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_main_mcall")?;
            }
            // P2-T117: hat teste_alle() ein FAIL gemeldet, endet main mit 1.
            if self.module.get_global("__moo_tests_fehlgeschlagen").is_some() {
                let merker = self.tests_fehlgeschlagen_global();
                let code = self.builder.build_load(i32_type, merker, "tests.exit_code")
                    .map_err(|e| format!("{e}"))?.into_int_value();
                self.builder.build_return(Some(&code))
                    .map_err(|e| format!("Return-Fehler: {e}"))?;
            } else {
                self.builder.build_return(Some(&i32_type.const_int(0, false)))
                    .map_err(|e| format!("Return-Fehler: {e}"))?;
            }
        }

        self.defer_frame_end(main_defer_handler)?;

        // P011-A1: llvm.used emittieren — @benutzt/@einstieg-Funktionen vor DCE schuetzen.
        // Appending-Linkage + Section llvm.metadata = LLVM-Konvention fuer used-Symbole.
        if !self.used_globals.is_empty() {
            let ptr_vals: Vec<inkwell::values::PointerValue> = self.used_globals.clone();
            let ptr_ty = ptr_vals[0].get_type();
            let arr = ptr_ty.const_array(&ptr_vals);
            let g = self.module.add_global(
                ptr_ty.array_type(ptr_vals.len() as u32),
                None,
                "llvm.used",
            );
            g.set_linkage(inkwell::module::Linkage::Appending);
            g.set_section(Some("llvm.metadata"));
            g.set_initializer(&arr);
        }

        if self.native_thread_runtime { crate::runtime_provider::entry(self.context, &self.module)?; }

        // P7-T4 (7A): direkte Aufrufe erben @aufrufkonvention ihres Ziels.
        crate::ffi_types::sync_direct_call_conventions(&self.module)?;

        // Typ-Hinweise (Crystal-inspiriert) meldet compile_program an den
        // Diagnose-Sammler (P2-T97), auch wenn danach ein Fehler folgt.

        self.module.verify().map_err(|e| format!("LLVM Verifikation fehlgeschlagen: {}", e.to_string()))
    }

    /// P2-T21/T57 B2 (Plan f4655d20 E1/E4/E10): je @rückruf-Funktion genau ein
    /// Thunk __moo_rueckruf.<name> mit exakter C-Signatur (interne Linkage).
    fn rueckruf_thunks(
        &self,
        program: &Program,
        callees: &mut HashMap<String, crate::native_system::NativeCallee<'ctx>>,
        nominals: &crate::native_type_id::NominalTypes,
    ) -> Result<(), String> {
        let triple = self.module.get_triple();
        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
        let hosted = !self.bare_mode && !triple.starts_with("wasm");
        for stmt in &program.statements {
            let Stmt::FunctionDef { name, param_types, return_type, decorators, .. } = stmt else { continue };
            let Some(deko) = decorators.iter().find(|d| matches!(d.name.as_str(), "rückruf" | "rueckruf" | "callback")) else { continue };
            if !decorators.iter().any(|d| d.name == "nativ" || d.name == "native") {
                return Err(format!("@rückruf an '{name}': nur für Top-Level-@nativ-Funktionen (Lambdas, Methoden und dynamische Funktionen: user_data-Muster)"));
            }
            if decorators.iter().any(|d| matches!(d.name.as_str(), "einstieg" | "entry")) {
                return Err(format!("@rückruf an '{name}': @einstieg-Exporte haben bereits die C-ABI"));
            }
            if deko.args.len() > 1 {
                return Err(format!("@rückruf an '{name}': höchstens ein Fehlerwert"));
            }
            let callee = callees.get(name).cloned()
                .ok_or_else(|| format!("@rückruf an '{name}': diese native Funktionsart ist nicht als Rückruf nutzbar"))?;
            let inputs = param_types.iter()
                .map(|ty| ty.clone().ok_or_else(|| format!("@rückruf an '{name}': Parametertyp fehlt")))
                .collect::<Result<Vec<_>, _>>()?;
            let result = return_type.clone().unwrap_or(crate::ast::TypeRef {
                kind: TypeKind::Name("void".into()), line: 0, column: 0, end_line: 0, end_column: 0,
            });
            let result_void = matches!(&result.kind, TypeKind::Name(n) if n == "void");
            if inputs.iter().any(|ty| !crate::ffi_types::rueckruf_wert_zulaessig(ty))
                || (!result_void && !crate::ffi_types::rueckruf_wert_zulaessig(&result))
            {
                return Err(format!("@rückruf an '{name}': Signatur ist kein C-Rückruf (erlaubt: i8..u64, bool, f32, f64, Zeiger, Strukturwerte; kein puffer/Array)"));
            }
            let convention = crate::ffi_types::decorator_call_convention(decorators, &triple)?;
            let vertrag = if hosted && callee.fehlbar {
                let struktur = matches!(&result.kind, TypeKind::Name(n) if self.native_structs.layout(n).is_some());
                if struktur && !self.native_interfaces.is_adapter(name) {
                    return Err(format!("Rückruf '{name}' kann fehlschlagen und liefert eine Struktur: kein darstellbarer Fehlerwert (Fehler durch Vorprüfung ausschließen)"));
                }
                // Interne Vtable-Aufrufe prüfen wie native Fremdaufrufe das
                // Moo-Fehlerflag, bevor sie das Resultat verwenden. Nur dort
                // darf ein definierter Nullwert als Platzhalter zurückkehren.
                let fehlerwert = if result_void || struktur { None } else {
                    let null = crate::ast::DecoratorArg::Zahl(0.0);
                    let wert = if self.native_interfaces.is_adapter(name) { Some(&null) } else { deko.args.first() };
                    Some(crate::ffi_types::rueckruf_fehlerwert(self.context, &result, wert, name)?)
                };
                Some(crate::ffi_types::RueckrufVertrag { name: name.clone(), fehlerwert })
            } else {
                None
            };
            let target = self.target_data.as_ref().ok_or("Ziel-Datenlayout fuer die C-ABI fehlt")?;
            let thunk = crate::ffi_types::emit_abi_export(
                self.context, &self.module, target, &format!("__moo_rueckruf.{name}"), callee.function,
                &inputs, &result, &self.native_structs, convention, inkwell::module::Linkage::Internal, vertrag,
            ).map_err(|e| format!("@rückruf an '{name}': {e}"))?;
            let konvention = decorators.iter().find(|d| crate::ffi_types::is_call_convention_decorator(d))
                .and_then(|d| match d.args.as_slice() {
                    [crate::ast::DecoratorArg::Text(text)] => Some(text.clone()),
                    _ => None,
                });
            let identity = crate::native_type_id::NativeTypeId::Function {
                params: inputs.iter().map(|ty| crate::native_type_id::resolve_type(ty, nominals))
                    .collect::<Result<Vec<_>, _>>()?,
                result: Box::new(crate::native_type_id::resolve_type(&result, nominals)?),
                variadic: false,
                convention: konvention,
            };
            if let Some(eintrag) = callees.get_mut(name) {
                eintrag.rueckruf = Some((thunk, identity));
            }
        }
        Ok(())
    }

    fn forward_declare(&mut self, program: &Program) -> Result<(), String> {
        crate::native_cleanup::prepare(self.context, &self.module, &program.statements, self.bare_mode)?;
        for stmt in &program.statements {
            if let Stmt::ExternC { name, .. } = stmt {
                let user_collision = program.statements.iter().any(|item| match item {
                    Stmt::FunctionDef { name: user, .. } => user == name,
                    Stmt::ClassDef { name: class, body, .. } => body.iter().any(|method| match method {
                        Stmt::FunctionDef { name: method, .. } => &format!("{class}__{method}") == name,
                        _ => false,
                    }),
                    Stmt::DataClassDef { name: class, .. } =>
                        &format!("{class}__erstelle") == name || &format!("{class}__to_string") == name,
                    _ => false,
                });
                if name == "main" || user_collision || self.module.get_function(name).is_some()
                    || !self.extern_c_names.insert(name.clone()) {
                    return Err(format!("extern C: Symbolkollision bei '{name}'"));
                }
            }
        }
        // Alle C-Signaturen vor nativen Funktionskoerpern erfassen, unabhaengig
        // von ihrer Reihenfolge im Quelltext.
        for stmt in &program.statements {
            if let Stmt::ExternC { name, params, result, decorators, .. } = stmt {
                // P2-T98 S4/S5: Besitzvertrag @verbraucht(n)/@behaelt(n).
                let vertrag = crate::ffi_types::extern_vertrag(name, params.len(), decorators)?;
                self.extern_bindings.setze_vertrag(name, vertrag);
                let triple = self.module.get_triple();
                let triple = triple.as_str().to_str().map_err(|e| format!("Ungueltiges Ziel-Triple: {e}"))?;
                let types: Vec<TypeRef> = params.iter().map(|(_, ty)| ty.clone()).collect();
                let is_struct = types.iter().chain(std::iter::once(result))
                    .any(|ty| match &ty.kind {
                        TypeKind::Name(n) => self.native_structs.layout(n).is_some(),
                        _ => false,
                    });
                if is_struct {
                    // P2-T19/T56 (Plan 91033131): Strukturwerte by value laufen
                    // ueber den allgemeinen Klassifizierer (ffi_abi): SysV x86_64,
                    // MS x64, AAPCS64; @aufrufkonvention waehlt die ABI.
                    let convention = crate::ffi_types::decorator_call_convention(decorators, triple)?;
                    let target = self.target_data.as_ref()
                        .ok_or("Ziel-Datenlayout fuer die C-ABI fehlt")?;
                    self.extern_bindings.declare_abi(
                        self.context, &self.module, target, name, &types, result,
                        &self.native_structs, convention,
                    ).map_err(|fehler| format!("extern C '{name}': {fehler}"))?;
                } else {
                    let convention = crate::ffi_types::decorator_call_convention(decorators, triple)?;
                    let module_name = self.module.get_name().to_str()
                        .map_err(|e| format!("Ungueltiger nativer Modulname: {e}"))?.to_string();
                    let nominals = self.native_structs.nominal_types(&module_name);
                    let signature = crate::ffi_types::FfiSignature::validate(triple, &types, result, false, &nominals)?
                        .with_convention(convention);
                    self.extern_bindings.declare(self.context, &self.module, name, &signature)?;
                }
            }
        }
        // CPU-Libc-Signaturen erst nach Benutzer-Externs zusammenfuehren;
        // identische Deklarationen sind legal und werden wiederverwendet.
        crate::native_cpu_scope::prepare(self.context, &self.module, &program.statements)?;
        // Native Signaturen zuerst deklarieren, damit reine Vorwaertsaufrufe
        // die gleiche Typpruefung wie Rueckwaertsaufrufe erhalten.
        let mut pure_bodies: HashMap<String, &[Stmt]> = HashMap::new();
        let mut pure_functions = HashMap::new();
        let mut pointer_functions: HashMap<String, (FunctionValue<'ctx>, crate::codegen_native::NativePointerSignature)> = HashMap::new();
        // P7-T4: @nativ-Funktionen aus Hardware-Intrinsics (volatile_*, Barrieren, Port-I/O).
        let mut hardware_functions: HashMap<String, (FunctionValue<'ctx>, crate::codegen_native::NativePointerSignature)> = HashMap::new();
        // P7-T5: @nativ-Systemkoerper (Kontrollfluss, Locals, Speicher, Aufrufe).
        let mut system_functions: HashMap<String, (FunctionValue<'ctx>, crate::native_system::SystemSignature)> = HashMap::new();
        // Typsichere ptr[Struktur]-Parameter benutzen das vorhandene Layout
        // statt eines zweiten Hardware- oder FFI-Typregisters.
        let module_name = self.module.get_name().to_str()
            .map_err(|e| format!("Ungueltiger nativer Modulname: {e}"))?;
        let native_nominals = self.native_structs.nominal_types(module_name);
        // P2-T83 B3: Puffer-/@liest-Positionen aller Funktionen (Weitergabepruefung).
        let liest_karte = crate::native_system::liest_karte(&program.statements);
        // Rekursion pfadunabhaengig: reine Koerper auf einem nativen Aufrufzyklus
        // (und ihre nativen Aufrufer) laufen wie Koerper mit Kontrollfluss im
        // System-Emitter (Sicher-Modus); die Pure-Form bleibt zyklusfrei.
        let zyklische_natives = crate::codegen_native::zyklische_native_funktionen(&program.statements);
        for stmt in &program.statements {
            if let Stmt::FunctionDef { name, params, param_types, return_type, defaults, body, decorators, .. } = stmt {
                if !decorators.iter().any(|d| d.name == "nativ" || d.name == "native") { continue; }
                use crate::codegen_native::{NType, NSignature};
                if params.len() != param_types.len() || defaults.iter().any(Option::is_some)
                    // P7-T4 (7A): neben @nativ ist nur @aufrufkonvention zulaessig.
                    || decorators.iter().filter(|d| d.name == "nativ" || d.name == "native").count() != 1
                    || !decorators.iter().all(|d| (matches!(d.name.as_str(), "nativ" | "native" | "nackt" | "naked"
                            | "benutzt" | "used" | "einstieg" | "entry" | "verschiebbar" | "relocatable") && d.args.is_empty())
                        || matches!(d.name.as_str(), "sektion" | "section" | "ausrichten" | "align" | "unterbrechung" | "interrupt")
                        // P2-T21/T57: @rückruf(fehlerwert?) - Pruefung in rueckruf_thunks.
                        || matches!(d.name.as_str(), "rückruf" | "rueckruf" | "callback")
                        // P2-T23/T53: @verbraucht(n) - Pruefung beim dyn-Adapter.
                        || matches!(d.name.as_str(), "verbraucht" | "consumes")
                        // P2-T83: @liest(n) - Pruefung beim dyn-Adapter.
                        || matches!(d.name.as_str(), "liest" | "reads" | "simd" | "cpu_bereich")
                        || crate::ffi_types::is_call_convention_decorator(d))
                    || program.statements.iter().filter(|s| matches!(s, Stmt::FunctionDef { name: other, .. } if other == name)).count() != 1
                    || self.module.get_function(name).is_some()
                    || self.native_functions.contains_key(name)
                {
                    return Err(format!("Native Funktion '{name}': Signatur, Defaults, Decorators oder Name nicht unterstützt"));
                }
                let convention = {
                    let triple = self.module.get_triple();
                    let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
                    crate::ffi_types::decorator_call_convention(decorators, &triple)
                        .map_err(|e| format!("Native Funktion '{name}': {e}"))?
                };
                let simd = crate::native_simd_dispatch::marker(decorators)?;
                let cpu_scope = crate::native_cpu_scope::marker(decorators)?;
                // P7-T4 (3A): @nackt nur fuer reine Assemblerkoerper (Pruefung im Emitter).
                let naked = decorators.iter().any(|d| d.name == "nackt" || d.name == "naked");
                if !naked && decorators.iter().any(|d| matches!(d.name.as_str(), "verschiebbar" | "relocatable")) {
                    return Err(format!("Native Funktion '{name}': @verschiebbar verlangt @nackt und eine eigene @sektion"));
                }
                if naked && !matches!(crate::native_hardware::classify_body(body), crate::native_hardware::BodyKind::Hardware) {
                    return Err(format!("Native Funktion '{name}': @nackt verlangt einen Körper aus unsicher: assembler(...)"));
                }
                // P7-T4 (4A): @unterbrechung(vektor) an @nativ mit Vektorvertrag.
                let interrupts: Vec<&crate::ast::Decorator> = decorators.iter()
                    .filter(|d| d.name == "unterbrechung" || d.name == "interrupt").collect();
                let interrupt = match interrupts.as_slice() {
                    [] => None,
                    [d] => match d.args.as_slice() {
                        [crate::ast::DecoratorArg::Zahl(n)] if n.fract() == 0.0 && *n >= 0.0 && *n <= 255.0 => Some(*n as u8),
                        _ => return Err(format!("Native Funktion '{name}': @{} erwartet die Vektornummer 0..255, z.B. @unterbrechung(14)", d.name)),
                    },
                    _ => return Err(format!("Native Funktion '{name}': @unterbrechung ist doppelt angegeben")),
                };
                if interrupt.is_some() && (naked || convention != 0) {
                    return Err(format!("Native Funktion '{name}': @unterbrechung verlangt einen unsicher:-Hardware- oder Systemkörper ohne @nackt/@aufrufkonvention; Sonderfälle als @nackt-Trampolin"));
                }
                // P2-T98 S3: Rohzugriff ausserhalb unsicher: ist M-M005 mit Zeile,
                // fuer reine wie fuer Systemkoerper (vorher M-C000 ohne Zeile).
                if let Some((roh, zeile, spalten)) = crate::native_system::rohzugriff_ohne_unsicher(body) {
                    return Err(crate::diagnose::vormerken_span("M-M005", zeile, spalten, format!(
                        "Native Funktion '{name}': Aufruf '{roh}' ist nur innerhalb von unsicher: erlaubt (Rohzugriff erfordert unsicher); Vorschlag: den Rohzugriff '{roh}(...)' in eine eigene, passend typisierte Variable innerhalb eines engen unsicher:-Blocks herausziehen und diese Variable im umgebenden Ausdruck verwenden")));
                }
                match if simd.is_some() || cpu_scope { crate::native_hardware::BodyKind::Other }
                    else { crate::native_hardware::classify_body(body) } {
                    crate::native_hardware::BodyKind::MissingUnsafe(zeile) => {
                        // P2-T98 S3: M-M005 mit Zeile des ersten Hardwarezugriffs.
                        return Err(crate::diagnose::vormerken("M-M005", zeile, format!(
                            "Native Funktion '{name}': Hardwarezugriff erfordert unsicher (Vorschlag: den Zugriff in einen unsicher:-Block einschliessen)")));
                    }
                    crate::native_hardware::BodyKind::Hardware => {
                        if !self.bare_mode {
                            return Err(format!("Native Funktion '{name}': Hardware-Intrinsics sind nur im Bare-Profil verfügbar"));
                        }
                        let sig = crate::native_hardware::signature(param_types, return_type.as_ref())?;
                        let internal = format!("__moo_native.{name}");
                        if self.module.get_function(&internal).is_some() {
                            return Err(format!("Native Funktion '{name}': interner Symbolkonflikt"));
                        }
                        let fun = self.module.add_function(&internal, sig.llvm_type(self.context),
                            Some(inkwell::module::Linkage::Internal));
                        if convention != 0 { fun.set_call_conventions(convention); }
                        if let Some(vector) = interrupt {
                            let triple = self.module.get_triple();
                            let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
                            crate::native_hardware::configure_interrupt(self.context, fun, &sig, vector, &triple)
                                .map_err(|e| format!("Native Funktion '{name}': {e}"))?;
                        }
                        if naked {
                            for attribute in ["naked", "noinline"] {
                                let kind = inkwell::attributes::Attribute::get_named_enum_kind_id(attribute);
                                fun.add_attribute(inkwell::attributes::AttributeLoc::Function,
                                    self.context.create_enum_attribute(kind, 0));
                            }
                        }
                        crate::native_hardware::deklariere_trampolin(self.context, &self.module, fun, decorators)?;
                        self.native_functions.insert(name.clone(), fun);
                        // P7-T22: reine Hardwarekoerper (nur Intrinsics) mit skalarer
                        // Signatur sind wie Bare-Systemkoerper aus dynamischem Moo
                        // aufrufbar (Aufrufstelle: unsicher:, main/Funktion, kein
                        // versuche:). Die Emission bleibt beim Hardware-Emitter.
                        let skalar: Option<Vec<NType>> = sig.params.iter().map(|p| match p {
                            crate::codegen_native::NativeParameter::Scalar(ty) => Some(*ty),
                            _ => None,
                        }).collect();
                        let dyn_grund = if interrupt.is_some() {
                            Some("Unterbrechungsbehandler (@unterbrechung) ruft nur die CPU auf")
                        } else if naked {
                            Some("@nackt-Koerper haben keinen Aufrufrahmen; aus nativem Code oder per Adresse aufrufen")
                        } else if convention != 0 {
                            Some("fremde @aufrufkonvention; aus nativem Code aufrufen")
                        } else if skalar.is_none() {
                            Some("Zeigerparameter (ptr[T]) gibt es in dynamischem Moo nicht; die Adresse als u64 uebergeben und im Koerper mit zeiger(adresse) umwandeln")
                        } else {
                            None
                        };
                        match (dyn_grund, skalar) {
                            (None, Some(params)) => {
                                self.hosted_system_functions.push(fun);
                                self.native_signatures.insert(name.clone(), NSignature { params, result: sig.result });
                            }
                            (grund, _) => {
                                self.native_dyn_sperre.insert(name.clone(), grund.unwrap_or("Signatur ohne dynamische Form").to_string());
                            }
                        }
                        hardware_functions.insert(name.clone(), (fun, sig));
                        continue;
                    }
                    crate::native_hardware::BodyKind::Other => {}
                }
                // P7-T5: Bare-Systemkoerper und ein enger Hosted-Win64-Slice
                // fuer native Strukturwerte. Der restliche Hosted-Code nutzt
                // weiterhin seine bisherigen Pure-/FFI-Pfade.
                let returns_struct = return_type.as_ref().is_some_and(|ty|
                    matches!(&ty.kind, TypeKind::Name(n) if native_nominals.contains_key(n)));
                let hosted_x86 = {
                    let triple = self.module.get_triple();
                    let name = triple.as_str().to_str().map_err(|e| e.to_string())?;
                    // P2-T66 (c): hosted Usermode-Triples (x86_64 Linux/Windows,
                    // aarch64 Linux/Darwin); Name beibehalten, Bedeutung = hosted Usermode.
                    matches!(name, "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu"
                        | "x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"
                        | "aarch64-unknown-linux-gnu")
                        || crate::ffi_types::darwin_aarch64(name)
                };
                let has_registration = param_types.iter().flatten().chain(return_type.iter()).any(|ty|
                    matches!(&ty.kind, TypeKind::Generic { name, .. } if name == "rueckruf" || name == "rückruf"));
                if has_registration && (self.bare_mode || !hosted_x86) {
                    return Err(format!("Native Funktion '{name}': registrierte dynamische Rueckrufe brauchen hosted Moo"));
                }
                if return_type.as_ref().is_some_and(|ty|
                    matches!(&ty.kind, TypeKind::Generic { name, .. } if name == "rueckruf" || name == "rückruf")) {
                    return Err(format!("Native Funktion '{name}': rueckruf[fn] ist ein verwalteter Griff; nativ nur als geliehenen Parameter verwenden, Griffe aus dynamischem Moo zurueckgeben"));
                }
                let has_pointer = param_types.iter().flatten().any(|ty| matches!(
                    &ty.kind, TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger"
                ));
                let struct_param = param_types.iter().flatten().any(|ty|
                    matches!(&ty.kind, TypeKind::Name(n) if native_nominals.contains_key(n)));
                let returns_pointer = return_type.as_ref().is_some_and(|ty| matches!(
                    &ty.kind, TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger"
                ));
                // P2-T50 B1: puffer[T]-Parameter vorerst nur hosted in unsicher:-
                // Systemkoerpern (sichere Koerper folgen mit P2-T12, Bare spaeter).
                let has_buffer = param_types.iter().flatten().any(|ty| matches!(
                    &ty.kind, TypeKind::Generic { name, .. } if name == "puffer" || name == "buffer"
                ));
                // P2-T48 A5 (Plan a71947f7): feste Arrays by value wie in C nicht
                // als Parameter/Rueckgabe (vor der Pfadwahl, auch fuer Einzeiler).
                if param_types.iter().flatten().chain(return_type.iter())
                    .any(|ty| matches!(ty.kind, TypeKind::Array { .. }))
                {
                    return Err(format!("Native Funktion '{name}': Feste Arrays sind als Parameter/Rueckgabe by value nicht freigegeben (ptr[T] oder eine Struktur mit Arrayfeld verwenden)"));
                }
                // P2-T124: interne Bare-Puffer nutzen denselben geprueften
                // RAM-Deskriptor wie hosted. C-/UEFI-Exporte bleiben gesperrt.
                // Ein einzelner skalarer extern-C-Aufruf bleibt im bestehenden
                // FFI-Pfad. Mit Zeiger- oder Strukturtypen in der Signatur
                // uebernimmt der Systemkoerper: der skalare FFI-Emitter kennt
                // keine Zeiger (ffi_types.rs scalar_parameters/scalar_result).
                let ffi_single = !(has_pointer || returns_pointer || struct_param || returns_struct || has_buffer || has_registration)
                    && matches!(body.as_slice(), [Stmt::UnsafeBlock { body: inner, .. }]
                    if matches!(inner.as_slice(),
                        [Stmt::Return(Some(Expr::FunctionCall { name, .. }), _)]
                        | [Stmt::Expression(Expr::FunctionCall { name, .. }, _)]
                        if self.extern_c_names.contains(name)));
                // P2-T66: hosted x86_64 (Linux/Windows, Usermode) nutzt den
                // Systemkoerper fuer unsicher:-Koerper mit Zeigern, Struktur-
                // werten oder mehreren Anweisungen (vorher Compilefehler).
                // Skalare Pure-/FFI-Formen behalten ihre Emitter; Hardware-
                // koerper, kern_*-Builtins und bare_mode bleiben unberuehrt.
                let hosted_system = hosted_x86 && !self.bare_mode && !ffi_single
                    && matches!(body.as_slice(), [Stmt::UnsafeBlock { .. }])
                    && (returns_struct || struct_param || has_pointer || returns_pointer || has_buffer || has_registration
                        || crate::native_system::has_struct_values(body, &native_nominals)
                        || crate::native_system::is_system_body(body, params));
                // P2-T12 C2 (Plan 31194239): sichere Koerper mit Kontrollfluss
                // (wenn/solange/für, Neuzuweisung, unsicher:-Inseln) laufen im
                // System-Emitter im Sicher-Modus; Pure-Slices, FFI-Einzelaufruf,
                // [unsicher:]-Koerper, Zeiger-/Puffer-Signaturen und Bare-
                // Strukturrueckgaben behalten ihre bisherigen Pfade.
                // Hosted puffer[T]-/ptr-Signaturen (Review 074b5d17) laufen auch in
                // Pure-Slice-Form hier (Pure-Emitter kennt sie nicht). Bestehende
                // unsicher:-Bare-Zeigerformen behalten ihren engen Bestandspfad.
                let zeiger_puffer = has_pointer || returns_pointer || has_buffer || has_registration;
                // P2-T16: ebenso Strukturwerte (z. B. vec3/mat4) in Signatur oder
                // Locals; der Pure-Emitter kennt nur Skalare.
                let pure_ungeeignet = zeiger_puffer || struct_param || returns_struct
                    || crate::native_system::has_struct_values(body, &native_nominals);
                // P2-T89 (Befund #38466): auch Bare-sichere Pure-Slices mit
                // Strukturwerten (vec4(...)[i], Struktur-Local/-Parameter) laufen
                // im System-Emitter im Sicher-Modus; Bare-Strukturrueckgaben und
                // Zeiger/Puffer behalten ihre Pfade (Ausschluss unten).
                let bare_strukturwerte = self.bare_mode && (struct_param
                    || crate::native_system::has_struct_values(body, &native_nominals));
                let lokaler_speicher = crate::native_system::has_struct_locals(body, &native_nominals);
                let sicher_system = !matches!(body.as_slice(), [Stmt::UnsafeBlock { .. }])
                    && (!crate::native_system::is_pure_slice(body) || (pure_ungeeignet && !self.bare_mode) || has_pointer || returns_pointer
                        || bare_strukturwerte || has_buffer || lokaler_speicher || zyklische_natives.contains(name))
                    // Pointer weiterreichen braucht auch bare kein unsicher.
                    // Reale Dereferenzierungen prüft der sichere System-Emitter.
                    && !(self.bare_mode && returns_struct)
                    && (hosted_x86 || self.bare_mode);
                if (self.bare_mode && (crate::native_system::is_system_body(body, params) || returns_struct || has_buffer))
                    || hosted_system || sicher_system || simd.is_some() || cpu_scope
                {
                    let sig = crate::native_system::SystemSignature::from_annotations(
                        param_types, return_type.as_ref(), &native_nominals,
                    ).map_err(|e| format!("Native Funktion '{name}': {e}"))?;
                    if sig.uses_struct_values() {
                        // C-Exporte und Rueckrufe erhalten einen typisierten
                        // C-ABI-Thunk (ffi_types::emit_abi_export). Die interne
                        // Funktion verwendet weiterhin die Moo-Speicher-ABI.
                        // Ohne diesen Thunk sind fremde Strukturwert-ABIs unzulaessig.
                        if convention != 0 && !decorators.iter().any(|d|
                            matches!(d.name.as_str(), "einstieg" | "entry" | "rueckruf" | "rückruf" | "callback")) {
                            return Err(format!(
                                "Native Funktion '{name}': Strukturwerte mit @aufrufkonvention nur als @einstieg-Export oder @rueckruf (C-ABI-Thunk)"
                            ));
                        }
                    }
                    if sig.uses_buffers() && (convention != 0 || decorators.iter().any(|d|
                        matches!(d.name.as_str(), "einstieg" | "entry")))
                    {
                        return Err(format!(
                            "Native Funktion '{name}': puffer[T]-Parameter sind nur fuer interne Moo-Aufrufe freigegeben (keine C-Exporte)"
                        ));
                    }
                    let internal = format!("__moo_native.{name}");
                    if self.module.get_function(&internal).is_some() {
                        return Err(format!("Native Funktion '{name}': interner Symbolkonflikt"));
                    }
                    let fun_type = sig.llvm_type(self.context, &self.native_structs)?;
                    let fun = self.module.add_function(&internal, fun_type,
                        Some(inkwell::module::Linkage::Internal));
                    if convention != 0 { fun.set_call_conventions(convention); }
                    if let Some(vector) = interrupt {
                        let triple = self.module.get_triple();
                        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
                        crate::native_hardware::configure_interrupt_system(
                            self.context, fun, &sig, vector, &triple,
                        ).map_err(|e| format!("Native Funktion '{name}': {e}"))?;
                    }
                    self.native_functions.insert(name.clone(), fun);
                    if sicher_system || (simd.is_some() && !matches!(body.as_slice(), [Stmt::UnsafeBlock { .. }])) {
                        self.safe_native_functions.insert(name.clone());
                    }
                    // P2-T43: Bare-Systemkoerper ebenfalls registrieren; die
                    // Aufrufstelle laesst im Bare-Profil nur runtime-freie
                    // Literal-Argumente zu (hosted_system_functions wird sonst
                    // nur bei !bare_mode ausgewertet, write_object).
                    if interrupt.is_none() && (hosted_system || sicher_system || self.bare_mode) {
                        self.hosted_system_functions.push(fun);
                        // P2-T66 Stufe 2a: rein skalare Signatur -> aus dynamischem
                        // Moo aufrufbar; die Aufrufstelle verlangt unsicher:/main/kein try.
                        // P2-T50 B4: auch mit puffer[T]-Parametern (Pin-Adapter).
                        let verbraucht_dekos: Vec<_> = decorators.iter()
                            .filter(|d| matches!(d.name.as_str(), "verbraucht" | "consumes")).collect();
                        // P2-T49 (Plan 38c59c64 E5): Strukturwerte nur hosted und boxbar;
                        // sonst bleibt die Funktion intern aufrufbar, die Aufrufstelle nennt den Grund.
                        let mut dyn_form = sig.dyn_signature();
                        if param_types.iter().flatten().chain(return_type.iter()).any(|t|
                            matches!(&t.kind, TypeKind::Name(n) if n.starts_with("ergebnis["))) {
                            dyn_form = None;
                            self.native_dyn_sperre.insert(name.clone(), "ergebnis[T, F] ist nur im typisierten @nativ-Körper nutzbar".into());
                        }
                        if let Some((_, puffer, ergebnis)) = &dyn_form {
                            let mut strukturen: Vec<String> = puffer.iter().filter_map(|p| match p {
                                Some(crate::native_system::PufferArt::Wert(s)) => Some(s.clone()),
                                _ => None,
                            }).collect();
                            if let Some(crate::native_system::DynErgebnis::Struktur(s)) = ergebnis { strukturen.push(s.clone()); }
                            let grund = if !strukturen.is_empty() && self.bare_mode {
                                Some("Strukturwerte brauchen die hosted Runtime (nicht im Bare-Profil)".to_string())
                            } else {
                                strukturen.iter().find_map(|s| self.native_structs.boxbar(s).err()
                                    .map(|g| format!("Struktur '{s}' ist nicht als dynamischer Wert nutzbar: {g}")))
                            };
                            if let Some(grund) = grund {
                                self.native_dyn_sperre.insert(name.clone(), grund);
                                dyn_form = None;
                            }
                        }
                        if let Some((dyn_sig, mut puffer, dyn_ergebnis)) = dyn_form {
                            // P2-T23/T53 B3 (Plan 36aca1f2 E3): @verbraucht(n) markiert einen
                            // Griffparameter; der Adapter schliesst den Griff nach dem Aufruf.
                            for deko in &verbraucht_dekos {
                                let n = match deko.args.as_slice() {
                                    [crate::ast::DecoratorArg::Zahl(z)] if z.fract() == 0.0 && *z >= 1.0 => *z as usize,
                                    _ => return Err(format!("@verbraucht an '{name}': genau eine Parameternummer ab 1 angeben")),
                                };
                                match puffer.get_mut(n - 1) {
                                    Some(Some(crate::native_system::PufferArt::Griff { verbraucht, .. })) => *verbraucht = true,
                                    _ => return Err(format!("@verbraucht({n}) an '{name}': Parameter {n} ist kein Griffparameter (ptr[Struktur] oder ptr[void])")),
                                }
                            }
                            // P2-T83 B1 (Plan f3217c27 E3): @liest(n) nur auf Pufferparametern.
                            let mut nur_lesen = std::collections::HashSet::new();
                            for deko in decorators.iter().filter(|d| matches!(d.name.as_str(), "liest" | "reads")) {
                                let n = match deko.args.as_slice() {
                                    [crate::ast::DecoratorArg::Zahl(z)] if z.fract() == 0.0 && *z >= 1.0 => *z as usize,
                                    _ => return Err(format!("@liest an '{name}': genau eine Parameternummer ab 1 angeben")),
                                };
                                match puffer.get(n - 1) {
                                    Some(Some(crate::native_system::PufferArt::Skalar(_) | crate::native_system::PufferArt::Struktur(_))) => {
                                        if let Some(param) = params.get(n - 1) {
                                            if crate::native_system::schreibt_puffer(body, param) {
                                                return Err(format!("@liest({n}) an '{name}': Parameter '{param}' ist nur-lesend, der Koerper schreibt aber Elemente ({param}[i] = ...)"));
                                            }
                                            // P2-T83 B3 (Review 6315dba0): auch keine Weitergabe an
                                            // schreibbare Pufferparameter, kein Alias, puffer_zeiger nur in unsicher:.
                                            if let Some(grund) = crate::native_system::liest_verletzung(body, param, &liest_karte) {
                                                return Err(format!("@liest({n}) an '{name}': Parameter '{param}' ist nur-lesend, aber {grund}"));
                                            }
                                        }
                                        nur_lesen.insert(n - 1);
                                    }
                                    _ => return Err(format!("@liest({n}) an '{name}': Parameter {n} ist kein puffer[T]-Parameter")),
                                }
                            }
                            if !nur_lesen.is_empty() {
                                self.native_read_params.insert(name.clone(), nur_lesen);
                            }
                            if puffer.iter().any(Option::is_some) {
                                self.native_buffer_params.insert(name.clone(), puffer);
                            }
                            match dyn_ergebnis {
                                Some(crate::native_system::DynErgebnis::Griff(ziel)) => {
                                    self.native_handle_results.insert(name.clone(), ziel);
                                }
                                Some(crate::native_system::DynErgebnis::Struktur(s)) => {
                                    self.native_struct_results.insert(name.clone(), s);
                                }
                                None => {}
                            }
                            self.native_signatures.insert(name.clone(), dyn_sig);
                        } else if !verbraucht_dekos.is_empty() {
                            return Err(format!("@verbraucht an '{name}': nur fuer aus dynamischem Moo aufrufbare native Funktionen mit Griffparameter"));
                        } else if decorators.iter().any(|d| matches!(d.name.as_str(), "liest" | "reads")) {
                            return Err(format!("@liest an '{name}': nur fuer aus dynamischem Moo aufrufbare native Funktionen mit puffer[T]-Parameter"));
                        }
                    }
                    system_functions.insert(name.clone(), (fun, sig));
                    continue;
                }
                if has_pointer {
                    if !self.bare_mode {
                        return Err("Native Zeigerfunktionen sind ohne unsicher:-Systemkoerper nur im Bare-Profil verfügbar (hosted x86_64/aarch64 Linux, x86_64 Windows: Koerper als unsicher:-Block schreiben, P2-T66)".into());
                    }
                    // Nominal types need their canonical module identity; until
                    // the registry exposes it, unknown nominal names fail closed.
                    let sig = crate::codegen_native::NativePointerSignature::from_annotations(
                        param_types, return_type.as_ref(), &native_nominals,
                    )?;
                    let internal = format!("__moo_native.{name}");
                    if self.module.get_function(&internal).is_some() {
                        return Err(format!("Native Funktion '{name}': interner Symbolkonflikt"));
                    }
                    let fun = self.module.add_function(&internal, sig.llvm_type(self.context),
                        Some(inkwell::module::Linkage::Internal));
                    if convention != 0 { fun.set_call_conventions(convention); }
                    self.native_functions.insert(name.clone(), fun);
                    // P7-T22: klare Meldung statt "noch nicht unterstuetzt".
                    self.native_dyn_sperre.insert(name.clone(),
                        "Zeigerparameter oder -rueckgabe (ptr[T]) gibt es in dynamischem Moo nicht; die Adresse als u64 uebergeben und im Koerper mit zeiger(adresse) umwandeln (unsicher:-Systemkoerper)".to_string());
                    pointer_functions.insert(name.clone(), (fun, sig));
                    continue;
                }
                let sig = NSignature::from_annotations(param_types, return_type.as_ref())?;
                let result_ty = sig.result;
                let unsafe_ffi = matches!(body.as_slice(), [Stmt::UnsafeBlock { .. }]);
                let valid = match result_ty {
                    // P2-T19 Batch C: C-bool (i1, FfiType::Bool) ist FFI-Werttyp.
                    Some(ty) if unsafe_ffi => matches!(ty, NType::I64 | NType::U64 | NType::I32 | NType::U32 | NType::I16 | NType::U16 | NType::I8 | NType::U8 | NType::F32 | NType::F64 | NType::Bool)
                        && sig.params.iter().all(|param| matches!(param, NType::I64 | NType::U64 | NType::I32 | NType::U32 | NType::I16 | NType::U16 | NType::I8 | NType::U8 | NType::F32 | NType::F64 | NType::Bool)),
                    // P2-T34: reine native Koerper tragen jede skalare Signatur
                    // (gemischte Breiten, Float, bool); Casts/Vergleiche verbinden
                    // die Typen, der dyn-Adapter kennt alle Skalare (i64/u64 wide,
                    // Float, bool, 8-32 Bit). Erstslice-Schranke I32/I8/U8 entfaellt.
                    Some(_) => true,
                    None => unsafe_ffi && sig.params.iter().all(|param| matches!(param, NType::I64 | NType::U64 | NType::I32 | NType::U32 | NType::I16 | NType::U16 | NType::I8 | NType::U8 | NType::F32 | NType::F64 | NType::Bool)),
                };
                if !valid {
                    return Err(format!("Native Funktion '{name}': Signatur wird nicht unterstützt"));
                }
                let internal = format!("__moo_native.{name}");
                if self.module.get_function(&internal).is_some() {
                    return Err(format!("Native Funktion '{name}': interner Symbolkonflikt"));
                }
                let fun = self.module.add_function(&internal, sig.llvm_type(self.context),
                    Some(inkwell::module::Linkage::Internal));
                if convention != 0 { fun.set_call_conventions(convention); }
                if matches!(body.as_slice(), [Stmt::UnsafeBlock { .. }]) {
                    if self.bare_mode {
                        return Err("Native C-Aufrufe sind im ersten Slice nur hosted unterstützt".into());
                    }
                    self.native_ffi_functions.insert(name.clone());
                } else {
                    pure_bodies.insert(name.clone(), body.as_slice());
                    // P2-T9 (Plan 4500717b A2): jede skalare Rueckgabe ist direkt
                    // aus anderen reinen @nativ-Funktionen aufrufbar (vorher nur
                    // i32 bzw. bare u64). void bleibt aussen vor:
                    // lower_nontrapping_expr verlangt signature.result == expected.
                    if result_ty.is_some() {
                        pure_functions.insert(name.clone(), (fun, sig.clone()));
                    }
                }
                self.native_functions.insert(name.clone(), fun);
                self.native_signatures.insert(name.clone(), sig);
            }
        }
        // P7-T4 (2A): Layout-Decorators an @nativ; @einstieg exportiert die
        // Funktion unter ihrem Moo-Namen (externe Bindung, implizit benutzt).
        for stmt in &program.statements {
            let Stmt::FunctionDef { name, decorators, param_types, return_type, .. } = stmt else { continue };
            if !decorators.iter().any(|d| d.name == "nativ" || d.name == "native") { continue; }
            let Some(fun) = self.native_functions.get(name).copied() else { continue };
            let mut export = fun;
            if decorators.iter().any(|d| d.name == "einstieg" || d.name == "entry") {
                if name == "main" || self.module.get_function(name).is_some() || self.module.get_global(name).is_some() {
                    return Err(format!("Native Funktion '{name}': @einstieg-Symbolkollision"));
                }
                if system_functions.get(name).is_some_and(|(_, sig)| sig.uses_struct_values()) {
                    // P2-T19/T56 D1: interne Funktion bleibt __moo_native.<name>
                    // (Zeiger-auf-Kopie/sret); der Export ist ein C-ABI-Thunk.
                    let inputs = param_types.iter()
                        .map(|ty| ty.clone().ok_or_else(|| format!("Native Funktion '{name}': Parametertyp fehlt")))
                        .collect::<Result<Vec<_>, _>>()?;
                    let result = return_type.clone().unwrap_or(crate::ast::TypeRef {
                        kind: crate::ast::TypeKind::Name("void".into()), line: 0, column: 0, end_line: 0, end_column: 0,
                    });
                    let triple = self.module.get_triple();
                    let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
                    let convention = crate::ffi_types::decorator_call_convention(decorators, &triple)?;
                    let target = self.target_data.as_ref().ok_or("Ziel-Datenlayout fuer die C-ABI fehlt")?;
                    export = crate::ffi_types::emit_abi_export(
                        self.context, &self.module, target, name, fun, &inputs, &result,
                        &self.native_structs, convention, inkwell::module::Linkage::External, None,
                    )?;
                } else {
                    fun.as_global_value().set_name(name);
                    fun.set_linkage(inkwell::module::Linkage::External);
                    if fun.count_params() as usize == param_types.len() {
                        let triple = self.module.get_triple();
                        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
                        let convention = crate::ffi_types::decorator_call_convention(decorators, &triple)?;
                        crate::ffi_types::apply_export_extensions(
                            self.context, fun, &triple, convention, param_types, return_type.as_ref(),
                        );
                    }
                }
            }
            self.apply_native_layout_decorators(export.as_global_value(), decorators, true, 1, &format!("Native Funktion '{name}'"))?;
        }
        crate::codegen_native::validate_pure_call_graph(&pure_bodies)?;
        // Gewöhnliche Konstanten behalten ihre bisherige Deklarationsreihenfolge.
        // Nur die neu erzeugten Vtables benötigen bereits definierte ABI-Thunks.
        let early_constants = if self.native_interfaces.has_vtables() {
            let mut early = program.clone();
            early.statements.retain(|s| !matches!(s, Stmt::NativeConst { name, .. }
                if self.native_interfaces.is_vtable(name)));
            Some(early)
        } else { None };
        crate::native_const::emit_constants(
            early_constants.as_ref().unwrap_or(program), self.context, &self.module, self.target_data.as_ref(),
            &self.native_structs, &self.native_functions,
        )?;
        // P7-T5: Aufrufziele fuer Systemkoerper (alle nativen Funktionsarten).
        let mut native_callees: HashMap<String, crate::native_system::NativeCallee<'ctx>> = HashMap::new();
        // P2-T55 A3: fehlbare hosted Rueckrufe -> FFI-Einzelform prueft nach dem
        // C-Aufruf das Fehlerflag (wie Emitter::nach_fremdaufruf_pruefen).
        let mut fremd_fehlbar = false;
        let hat_rueckruf = program.statements.iter().any(|stmt| matches!(stmt,
            Stmt::FunctionDef { decorators, .. }
                if decorators.iter().any(|d| matches!(d.name.as_str(), "rückruf" | "rueckruf" | "callback"))));
        if !system_functions.is_empty() || hat_rueckruf {
            for (name, (fun, sig)) in hardware_functions.iter().chain(pointer_functions.iter()) {
                native_callees.insert(name.clone(), crate::native_system::NativeCallee::new(*fun, sig)?);
            }
            for (name, (fun, sig)) in &system_functions {
                native_callees.insert(name.clone(),
                    crate::native_system::NativeCallee::from_system(*fun, sig));
            }
            for name in pure_bodies.keys() {
                if let (Some(fun), Some(sig)) = (self.native_functions.get(name), self.native_signatures.get(name)) {
                    let pointer_sig = crate::codegen_native::NativePointerSignature {
                        params: sig.params.iter().map(|ty| crate::codegen_native::NativeParameter::Scalar(*ty)).collect(),
                        result: sig.result,
                    };
                    if let Ok(callee) = crate::native_system::NativeCallee::new(*fun, &pointer_sig) {
                        native_callees.insert(name.clone(), callee);
                    }
                }
            }
            // P2-T58 C1: fehlbare Aufrufziele (Fixpunkt ueber alle nativen Koerper);
            // Systemkoerper pruefen nach ihrem Aufruf das Fehlerflag (C3).
            let defs: Vec<crate::native_system::FehlbarDef> = program.statements.iter().filter_map(|stmt| match stmt {
                Stmt::FunctionDef { name, params, param_types, body, return_type, decorators, .. }
                    if decorators.iter().any(|d| d.name == "nativ" || d.name == "native")
                        && native_callees.contains_key(name) =>
                    Some(crate::native_system::FehlbarDef {
                        name: name.clone(),
                        body: body.as_slice(),
                        ergebnis: return_type.as_ref(),
                        params: params.iter().map(String::as_str)
                            .zip(param_types.iter().map(Option::as_ref)).collect(),
                    }),
                _ => None,
            }).collect();
            let bitfelder: std::collections::HashMap<String, std::collections::HashSet<String>> = program.statements.iter()
                .filter_map(|s| match s {
                    Stmt::StructDef { name, bitfelder, .. } if !bitfelder.is_empty() =>
                        Some((name.clone(), bitfelder.iter().map(|f| f.name.clone()).collect())),
                    _ => None,
                }).collect();
            let mut fehlbar = crate::native_system::fehlbare_funktionen(&defs, &std::collections::HashSet::new(), &bitfelder);
            // P2-T21/T57 C3: gibt es fehlbare hosted Rueckrufe, kann jeder Fremdaufruf
            // mit anstehendem Moo-Fehler zurueckkehren -> Fremdaufrufe gelten als fehlbar.
            let fehlbarer_rueckruf = program.statements.iter().any(|stmt| matches!(stmt,
                Stmt::FunctionDef { name, decorators, .. }
                    if fehlbar.contains(name)
                        && decorators.iter().any(|d| matches!(d.name.as_str(), "rückruf" | "rueckruf" | "callback"))));
            if fehlbarer_rueckruf && !self.bare_mode {
                fremd_fehlbar = true;
                let mut fremd: std::collections::HashSet<String> = self.extern_c_names.iter().cloned().collect();
                // Vtable-Ziele sind interne C-ABI-Aufrufe, auch ohne extern-C-
                // Deklaration im Programm. Ihr Fehlerflag muss bis zum nativen
                // Aufrufer propagieren, bevor dieser Folgeaktionen ausführt.
                fremd.extend(self.native_interfaces.dispatch_targets().cloned());
                fehlbar = crate::native_system::fehlbare_funktionen(&defs, &fremd, &bitfelder);
            }
            for (name, callee) in native_callees.iter_mut() {
                callee.fehlbar = fehlbar.contains(name);
            }
            // P2-T21/T57 B2 (Plan f4655d20): @rückruf-Thunks nach der Fehlbar-Analyse
            // und vor der Koerper-Emission (Batch C sieht den Thunk im NativeCallee).
            self.rueckruf_thunks(program, &mut native_callees, &native_nominals)?;
        }
        if self.native_interfaces.has_vtables() {
            let mut tables = program.clone();
            tables.statements.retain(|s| matches!(s, Stmt::NativeConst { name, .. }
                if self.native_interfaces.is_vtable(name)));
            crate::native_const::emit_constants(
                &tables, self.context, &self.module, self.target_data.as_ref(),
                &self.native_structs, &self.native_functions,
            )?;
        }
        for stmt in &program.statements {
            match stmt {
                Stmt::ExternC { .. } => {}
                Stmt::FunctionDef { name, params, param_types, return_type, defaults, body, decorators, .. } => {
                    if decorators.iter().any(|d| d.name == "nativ" || d.name == "native") {
                        if let Some((fun, sig)) = system_functions.get(name) {
                            let target = self.target_data.as_ref()
                                .ok_or("Ziel-Datenlayout für Systemkörper fehlt")?;
                            crate::native_system::emit_system_function(
                                self.context, &self.module, target, &self.native_structs,
                                &native_nominals, &self.native_interfaces, *fun, sig, params, body,
                                &native_callees, &self.extern_bindings,
                                self.safe_native_functions.contains(name),
                                decorators.iter().any(|d| matches!(d.name.as_str(), "einstieg" | "entry")),
                                &program.statements,
                            ).map_err(|e| format!("Native Funktion '{name}': {e}"))?;
                            continue;
                        }
                        if let Some((fun, sig)) = hardware_functions.get(name) {
                            let target = self.target_data.as_ref()
                                .ok_or("Ziel-Datenlayout für Hardwarezugriffe fehlt")?;
                            let triple = self.module.get_triple();
                            let triple = triple.as_str().to_str().map_err(|e| e.to_string())?;
                            crate::native_hardware::emit_hardware_function(
                                self.context, &self.module, target, triple, *fun, sig, params, body,
                            )?;
                            continue;
                        }
                        if let Some((fun, sig)) = pointer_functions.get(name) {
                            let target = self.target_data.as_ref()
                                .ok_or("Ziel-Datenlayout für Zeiger fehlt")?;
                            if matches!(body.as_slice(), [Stmt::UnsafeBlock { body, .. }]
                                if matches!(body.as_slice(), [Stmt::Return(Some(Expr::FunctionCall { .. }), _)] | [Stmt::Expression(Expr::FunctionCall { .. }, _)])) {
                                let result = return_type.as_ref().ok_or("Nativer Rückgabetyp fehlt")?;
                                crate::codegen_native::emit_indirect_return_function(
                                    self.context, &self.module, *fun, sig, params, param_types, result, body,
                                    &native_nominals,
                                )?;
                            } else {
                                let triple = self.module.get_triple();
                                let triple = triple.as_str().to_str().map_err(|e| e.to_string())?;
                                if !matches!(triple, "x86_64-unknown-none" | "x86_64-unknown-uefi" | "aarch64-unknown-none") {
                                    return Err(format!("Nativer Zeigerlesezugriff: Ziel '{triple}' noch nicht freigegeben"));
                                }
                                crate::codegen_native::emit_pointer_load_function(
                                    self.context, target, *fun, sig, params, body,
                                )?;
                            }
                            continue;
                        }
                        let sig = crate::codegen_native::NSignature::from_annotations(param_types, return_type.as_ref())?;
                        let fun = *self.native_functions.get(name).ok_or("Native Vorabdeklaration fehlt")?;
                        // P2-T34: Fehlervertrag nach Ziel: bare/wasm trappen, hosted
                        // liefert einen fangbaren Moo-Fehler (moo_native_fehler).
                        // C-Exporte (@einstieg) trappen auch hosted: kein Moo-Fehler
                        // ueber fremde C-Frames (ADR §11), Objekt ohne Runtime linkbar.
                        let einstieg = decorators.iter().any(|d| matches!(d.name.as_str(), "einstieg" | "entry"));
                        let wasm = {
                            let triple = self.module.get_triple();
                            triple.as_str().to_str().map(|t| t.starts_with("wasm")).unwrap_or(true)
                        };
                        let trap = self.bare_mode || einstieg || wasm;
                        // P2-T84: hosted C-Export -> moo_native_abbruch statt nacktem Trap.
                        let abbruch = einstieg && !self.bare_mode && !wasm;
                        let fehler = crate::codegen_native::NativeFehler { module: &self.module, trap, abbruch, funktion: name };
                        if self.native_ffi_functions.contains(name) {
                            crate::codegen_native::emit_ffi_return_function(
                                self.context, &self.module, &self.extern_bindings,
                                fun, &sig, params, body, fehler, fremd_fehlbar,
                            ).map_err(|e| format!("Native Funktion '{name}': {e}"))?;
                        } else {
                            // Fehler mit Funktionsnamen wie im System-Emitter (Gate
                            // frontend negativ: exponent_* erwartet "Native Funktion").
                            crate::codegen_native::emit_nontrapping_function(
                                self.context, fun, &sig, params, body, &pure_functions, fehler,
                            ).map_err(|e| format!("Native Funktion '{name}': {e}"))?;
                        }
                        continue;
                    }
                    // Duplikat-Guard: ueberspringe wenn Funktion schon deklariert
                    if self.module.get_function(name).is_some() {
                        continue;
                    }
                    if params.is_empty() && decorators.is_empty() {
                        self.native_try_zero_arg_users.insert(name.clone());
                    }
                    let mv = self.mv_type();
                    let param_types: Vec<BasicMetadataTypeEnum> = params.iter()
                        .map(|_| BasicMetadataTypeEnum::from(mv))
                        .collect();
                    let has_interrupt = decorators.iter().any(|d| d.name == "interrupt" || d.name == "unterbrechung");
                    let fn_type = if has_interrupt {
                        self.context.void_type().fn_type(&param_types, false)
                    } else {
                        mv.fn_type(&param_types, false)
                    };
                    self.module.add_function(name, fn_type, None);
                    self.dyn_named_functions.insert(name.clone());
                }
                Stmt::ClassDef { name, parent, body, .. } => {
                    if let Some(parent_name) = parent {
                        self.class_parents.insert(name.clone(), parent_name.clone());
                    }
                    for method in body {
                        if let Stmt::FunctionDef { name: method_name, params, decorators, .. } = method {
                            if decorators.iter().any(|d| d.name == "nativ" || d.name == "native") {
                                return Err(format!("Native Methode '{name}.{method_name}' wird noch nicht unterstützt"));
                            }
                            let mv = self.mv_type();
                            // Methoden bekommen `self` als ersten Parameter
                            let mut param_types: Vec<BasicMetadataTypeEnum> = vec![mv.into()];
                            param_types.extend(params.iter().map(|_| BasicMetadataTypeEnum::from(mv)));
                            let fn_type = mv.fn_type(&param_types, false);
                            let full_name = format!("{}__{}", name, method_name);
                            let func = self.module.add_function(&full_name, fn_type, None);
                            self.class_methods.insert((name.clone(), method_name.clone()), func);
                        }
                    }
                }
                Stmt::DataClassDef { name, fields, .. } => {
                    // erstelle-Konstruktor forward-declarieren
                    let mv = self.mv_type();
                    let mut param_types: Vec<BasicMetadataTypeEnum> = vec![mv.into()]; // self
                    param_types.extend(fields.iter().map(|_| BasicMetadataTypeEnum::from(mv)));
                    let fn_type = mv.fn_type(&param_types, false);
                    let ctor_name = format!("{name}__erstelle");
                    let func = self.module.add_function(&ctor_name, fn_type, None);
                    self.class_methods.insert((name.clone(), "erstelle".to_string()), func);
                    // to_string Methode
                    let ts_type = mv.fn_type(&[mv.into()], false);
                    let ts_name = format!("{name}__to_string");
                    let ts_func = self.module.add_function(&ts_name, ts_type, None);
                    self.class_methods.insert((name.clone(), "to_string".to_string()), ts_func);
                }
                _ => {}
            }
        }
        if crate::runtime_provider::enabled(program) {
            let target = self.target_data.as_ref().ok_or("Ziel-Datenlayout für Runtime-Provider fehlt")?;
            crate::runtime_provider::bind(self.context, &self.module, target, program, &self.native_functions, &self.native_structs)?;
        }
        crate::native_cleanup::bind(self.context, &self.module, &self.native_functions)?;
        Ok(())
    }

    pub fn write_ir(&self, path: &Path) -> Result<(), String> {
        self.module.print_to_file(path)
            .map_err(|e| format!("IR-Datei schreiben fehlgeschlagen: {}", e.to_string()))
    }

    /// Mapped einen kurzen Target-Namen auf ein LLVM Triple
    pub fn resolve_triple(target: &str) -> String {
        match target {
            "native" | "" => TargetMachine::get_default_triple().as_str().to_string_lossy().into_owned(),
            "x86_64" | "x64" => "x86_64-unknown-linux-gnu".to_string(),
            // P013: Windows-Cross-Target (COFF, MinGW-ABI). Objekt-only wie
            // alle Cross-Targets — gelinkt wird auf dem Zielsystem
            // (x86_64-w64-mingw32-gcc bzw. nativ unter Windows, siehe
            // runtime/win_regex/README.md + P013-Doku).
            "x86_64-windows" | "windows" | "win64" => "x86_64-pc-windows-gnu".to_string(),
            "x86_64-bare" | "x64-bare" | MOOS_USER_TARGET => "x86_64-unknown-none".to_string(),
            // P012-B1: 32-bit-x86-Bare explizit (Legacy-Stage2-Pfad, P011-F1).
            "i386-bare" | "i686-bare" | "x86-bare" => "i686-unknown-none".to_string(),
            "x86" | "i686" | "i386" => "i686-unknown-linux-gnu".to_string(),
            "arm" | "armv7" => "arm-unknown-linux-gnueabihf".to_string(),
            "arm-bare" => "arm-none-eabi".to_string(),
            // P012-B1: ARMv7-Bare explizit (LLVM akzeptiert armv7-none-eabi).
            "armv7-bare" => "armv7-none-eabi".to_string(),
            "aarch64" | "arm64" => "aarch64-unknown-linux-gnu".to_string(),
            "aarch64-bare" | "arm64-bare" => "aarch64-unknown-none".to_string(),
            "riscv64" => "riscv64-unknown-linux-gnu".to_string(),
            "riscv64-bare" => "riscv64-unknown-none".to_string(),
            "riscv32" => "riscv32-unknown-linux-gnu".to_string(),
            "mips" => "mips-unknown-linux-gnu".to_string(),
            "mips64" => "mips64-unknown-linux-gnuabi64".to_string(),
            "ppc64" | "powerpc64" => "ppc64le-unknown-linux-gnu".to_string(),
            "wasm" | "wasm32" => "wasm32-unknown-unknown".to_string(),
            // Wenn es schon ein Triple ist, direkt verwenden
            other if other.contains('-') => other.to_string(),
            other => {
                crate::diagnose::melden(crate::diagnose::Diagnose::hinweis("M-H005",
                    format!("Unbekanntes Target '{other}', verwende als Triple")));
                other.to_string()
            }
        }
    }

    /// Verfuegbare Targets auflisten
    pub fn list_targets() -> Vec<(&'static str, &'static str)> {
        vec![
            ("native", "Host-System (Standard)"),
            ("x86_64", "64-bit x86 (Linux)"),
            ("x86_64-windows", "64-bit x86 (Windows, COFF — Linken via MinGW/Windows)"),
            ("x86_64-bare", "Freestanding x86_64 (Bare-Metal OS)"),
            (MOOS_USER_TARGET, "MoOS-Benutzerprogramm (statisches x86-64 ELF)"),
            ("i686-bare", "Freestanding 32-bit x86 (Bare-Metal, Legacy-Stage2)"),
            ("x86", "32-bit x86 (Linux)"),
            ("arm", "ARMv7 (Linux, z.B. Raspberry Pi)"),
            ("arm-bare", "Freestanding ARM (Bare-Metal OS)"),
            ("armv7-bare", "Freestanding ARMv7 (Bare-Metal OS)"),
            ("aarch64", "ARM 64-bit (Linux, z.B. Apple M-Serie)"),
            ("aarch64-bare", "Freestanding AArch64 (Bare-Metal OS)"),
            ("riscv64", "RISC-V 64-bit (Linux)"),
            ("riscv64-bare", "Freestanding RISC-V 64-bit (Bare-Metal OS)"),
            ("riscv32", "RISC-V 32-bit"),
            ("mips", "MIPS 32-bit (Linux)"),
            ("mips64", "MIPS 64-bit (Linux)"),
            ("ppc64", "PowerPC 64-bit LE (Linux)"),
            ("wasm", "WebAssembly 32-bit"),
        ]
    }

    /// P2-T43: @bibliothek als Link-Direktive ins Modul legen (siehe ffi_link).
    pub fn embed_link_libraries(&self, libraries: &[String], triple: &str) -> Result<(), String> {
        crate::ffi_link::embed_link_options(self.context, &self.module, libraries, triple)
    }

    pub fn write_object(&self, path: &Path, target: &str) -> Result<(), String> {
        let is_native = target.is_empty() || target == "native";
        let triple_str = Self::resolve_triple(target);

        if triple_str.starts_with("wasm") {
            return self.write_wasm(path);
        }

        // Alle LLVM Targets initialisieren fuer Cross-Compilation
        Target::initialize_all(&InitializationConfig::default());

        // Fuer native: direkt das Host-Triple verwenden (vermeidet Debug-Format)
        let triple = if is_native {
            TargetMachine::get_default_triple()
        } else {
            inkwell::targets::TargetTriple::create(&triple_str)
        };
        let target_obj = Target::from_triple(&triple)
            .map_err(|e| format!("Target-Fehler fuer '{}': {}", triple_str, e.to_string()))?;
        let (cpu_str, feat_str) = if is_native {
            (
                TargetMachine::get_host_cpu_name().to_string(),
                TargetMachine::get_host_cpu_features().to_string(),
            )
        } else {
            ("generic".to_string(), String::new())
        };

        // P011-F1: Bare-Metal-Targets (freestanding, *-none-Triple) brauchen
        // RelocMode::Static. PIC erzeugt auf i686 GOT-relative Zugriffe
        // (_GLOBAL_OFFSET_TABLE_) — im Bootloader/Kernel gibt es aber keine
        // GOT, der Loader laedt ein flaches Image an eine feste Adresse.
        // Auf x86_64-bare ist es ein No-op (RIP-relativ, keine GOT noetig);
        // auf i686-none (Legacy-Stage2) behebt es den fehlenden-GOT-Crash.
        // Das MoOS-Benutzerfenster beginnt oberhalb des Small-Code-Model-
        // Adressbereichs. PIC hält Symbolzugriffe PC-relativ.
        let reloc_mode = if triple_str.contains("-none") && target != MOOS_USER_TARGET {
            RelocMode::Static
        } else {
            RelocMode::PIC
        };

        // P7-T7: Der x86_64-Kern liegt bei 0xFFFFFFFF80000000. Das
        // Default/Small-Code-Model erzeugt fuer adresse_von(Funktion) R_X86_64_32
        // und kann IDT-Ziele in der Hochhaelfte nicht darstellen. Kernel-Code-
        // Model behaelt PC-relative Zugriffe und erlaubt signierte 32-Bit-
        // Funktionsadressen im oberen 2-GiB-Fenster.
        let code_model = if triple_str.starts_with("x86_64-")
            && triple_str.contains("-none")
            && self.bare_mode
            && target != MOOS_USER_TARGET
        {
            CodeModel::Kernel
        } else if triple_str.starts_with("riscv64-")
            && triple_str.contains("-none")
            && self.bare_mode
        {
            // P7-T21: QEMU-virt RAM beginnt bei 0x80000000. Small/medlow
            // kann dort positive Globals nicht mit signiertem HI20 laden.
            // Medium/medany verwendet PC-relative LLA (LLVM RISCV-Lowering).
            CodeModel::Medium
        } else {
            CodeModel::Default
        };

        let machine = target_obj
            .create_target_machine(
                &triple,
                &cpu_str,
                &feat_str,
                OptimizationLevel::Default,
                reloc_mode,
                code_model,
            )
            .ok_or(format!("Konnte TargetMachine fuer '{}' nicht erstellen", triple_str))?;

        // Bare-Objekte werden voll optimiert, auch ohne @nativ. Zusaetzlich
        // gilt weiterhin (P2-T66): hosted x86_64 mit Systemkoerpern wird nur
        // fuer die nativen Koerper optimiert. Regulare
        // Hosted-/Legacy-Programme ohne Systemkoerper und WASM behalten ihren
        // Codepfad. Kein Fast-Math; Pointer-Alignment bleibt bei den belegten Werten.
        let freestanding_native = triple_str.contains("-none");
        let win64_no_stdlib = self.bare_mode
            && triple_str.starts_with("x86_64-")
            && triple_str.contains("-windows-");
        // P2-T66: hosted x86_64 (Linux/Windows, Usermode). Der dynamische Teil
        // (main, Userfunktionen, Lambdas, Klassen, Wrapper) wird waehrend der
        // IR-Paesse mit optnone/noinline geschuetzt; danach werden genau die
        // gesetzten Attribute wieder entfernt, damit sein Maschinencode wie
        // bisher entsteht. Optimiert werden so nur die @nativ-Funktionen; der
        // dyn->native-Aufruf bleibt ein Call auf die unveraenderte Signatur.
        let hosted_x86_system = !self.bare_mode
            && !self.hosted_system_functions.is_empty()
            && (matches!(triple_str.as_str(), "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu"
                | "x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"
                | "aarch64-unknown-linux-gnu")
                || crate::ffi_types::darwin_aarch64(&triple_str));
        // P7-T21: Bare-Objekte bekommen auch ohne @nativ volle Optimierung.
        // Hosted behält die bisherige Abschirmung; WASM bleibt unveraendert.
        let bare_o2 = self.bare_mode && !triple_str.starts_with("wasm");
        let o2_laeuft = bare_o2
            || ((freestanding_native || win64_no_stdlib || hosted_x86_system)
                && !self.native_functions.is_empty());
        if o2_laeuft {
            use inkwell::attributes::{Attribute, AttributeLoc};
            use inkwell::values::AsValueRef;
            let shield_kinds = ["optnone", "noinline"].map(Attribute::get_named_enum_kind_id);
            // (Funktion, je Attribut: von uns gesetzt?) — nur eigene Attribute werden entfernt.
            // O2 darf unbenutzte interne Helfer loeschen. LLVM-Handles
            // ueber die Pass-Pipeline hinweg waeren danach ungueltig.
            let mut shielded: Vec<(String, [bool; 2])> = Vec::new();
            if hosted_x86_system {
                let native: std::collections::HashSet<_> = self.native_functions.values()
                    .map(|f| f.as_value_ref()).collect();
                for fun in self.module.get_functions() {
                    if fun.count_basic_blocks() == 0 || native.contains(&fun.as_value_ref())
                        || crate::native_simd_dispatch::ist_helfer(fun)
                        || crate::native_crc32c::ist_helfer(fun)
                        || crate::native_cpu_scope::ist_helfer(fun) { continue; }
                    let mut added = [false; 2];
                    for (i, kind) in shield_kinds.iter().enumerate() {
                        if fun.get_enum_attribute(AttributeLoc::Function, *kind).is_none() {
                            fun.add_attribute(AttributeLoc::Function, self.context.create_enum_attribute(*kind, 0));
                            added[i] = true;
                        }
                    }
                    shielded.push((fun.get_name().to_str().map_err(|e| e.to_string())?.to_owned(), added));
                }
            }
            self.module.verify()
                .map_err(|e| format!("LLVM-IR vor O2 ungueltig: {e}"))?;
            self.module.run_passes("default<O2>", &machine, PassBuilderOptions::create())
                .map_err(|e| format!("LLVM-IR-Optimierung fehlgeschlagen: {e}"))?;
            if crate::native_arithmetic::konstante_reste_kanonisieren(self.context, &self.module)? != 0 {
                // Nach dem Entrollen gemeinsame Modulos zusammenfassen. Der
                // dynamische Hosted-Pfad traegt noch seine optnone-Abschirmung.
                // Kein zweites InstCombine: dessen disjoint-Markierung am
                // Rest-OR laesst den Backendpfad wieder zwei Modulos bilden.
                self.module.run_passes("function(early-cse)", &machine, PassBuilderOptions::create())
                    .map_err(|e| format!("Native Modulo-Nachoptimierung fehlgeschlagen: {e}"))?;
            }
            for (name, added) in &shielded {
                if let Some(fun) = self.module.get_function(name) {
                    for (i, kind) in shield_kinds.iter().enumerate() {
                        if added[i] { fun.remove_enum_attribute(AttributeLoc::Function, *kind); }
                    }
                }
            }
            self.module.verify()
                .map_err(|e| format!("LLVM-IR nach O2 ungueltig: {e}"))?;
        } else if self.bare_mode {
            // P7-T20: Fuer Bare-Ziele ausserhalb des Object-O2-Pfads bleibt
            // der vorhandene Always-Inliner erhalten.
            self.module.run_passes("always-inline", &machine, PassBuilderOptions::create())
                .map_err(|e| format!("LLVM always-inline fehlgeschlagen: {e}"))?;
        }

        machine
            .write_to_file(&self.module, FileType::Object, path)
            .map_err(|e| format!("Object-File schreiben fehlgeschlagen: {}", e.to_string()))
    }

    pub fn write_wasm(&self, path: &Path) -> Result<(), String> {
        Target::initialize_webassembly(&InitializationConfig::default());

        let triple = inkwell::targets::TargetTriple::create("wasm32-unknown-unknown");
        let target = Target::from_triple(&triple)
            .map_err(|e| format!("WASM Target-Fehler: {}", e.to_string()))?;

        let machine = target
            .create_target_machine(
                &triple,
                "generic",
                "",
                OptimizationLevel::Default,
                RelocMode::PIC,
                CodeModel::Default,
            )
            .ok_or("Konnte WASM TargetMachine nicht erstellen")?;

        machine
            .write_to_file(&self.module, FileType::Object, path)
            .map_err(|e| format!("WASM-Datei schreiben fehlgeschlagen: {}", e.to_string()))
    }

    // === Hilfsfunktionen ===

    /// Ruft eine Runtime-Funktion auf und gibt das Ergebnis als StructValue zurueck
    fn call_rt(&self, func: FunctionValue<'ctx>, args: &[BasicMetadataValueEnum<'ctx>], name: &str)
        -> Result<StructValue<'ctx>, String>
    {
        let func = self.bare_schnellweg(func);
        if self.windows_x64_abi() && func.get_type().get_return_type().is_none() {
            // MSVC x64 ABI: 16-Byte-MooValue wird via verstecktem sret-Puffer
            // als erstem Argument zurueckgegeben.
            let result_ptr = self.entry_alloca(self.mv_type(), &format!("{name}_sret"))
                .map_err(|e| format!("{e}"))?;
            let mut win_args = Vec::with_capacity(args.len() + 1);
            win_args.push(result_ptr.into());
            for (index, arg) in args.iter().enumerate() {
                match arg {
                    BasicMetadataValueEnum::StructValue(value) => {
                        // MSVC x64 uebergibt 16-Byte-Aggregate ebenfalls indirekt.
                        let arg_ptr = self.entry_alloca(
                            self.mv_type(),
                            &format!("{name}_arg{index}"),
                        )?;
                        self.builder.build_store(arg_ptr, *value)
                            .map_err(|e| format!("{e}"))?;
                        win_args.push(arg_ptr.into());
                    }
                    other => win_args.push(*other),
                }
            }
            self.builder.build_call(func, &win_args, name)
                .map_err(|e| format!("{e}"))?;
            return self.builder.build_load(self.mv_type(), result_ptr, &format!("{name}_value"))
                .map(|value| value.into_struct_value())
                .map_err(|e| format!("{e}"));
        }

        let result = self.builder.build_call(func, args, name)
            .map_err(|e| format!("{e}"))?;
        match result.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => Ok(v.into_struct_value()),
            _ => Err(format!("Runtime-Call '{name}' hat keinen Wert zurueckgegeben")),
        }
    }

    /// Ruft einen MooValue-Builtin mit geborgten Argumenten auf.
    ///
    /// `compile_expr` liefert fuer jedes Argument eine owning +1-Referenz.
    /// Surface-Runtimefunktionen konsumieren diese Argumente nicht, daher
    /// werden alle Temps unmittelbar nach dem Call wieder freigegeben.
    fn call_rt_borrowed_builtin(
        &mut self,
        func: FunctionValue<'ctx>,
        args: &[Expr],
        expected_args: usize,
        builtin: &str,
        ir_name: &str,
    ) -> Result<StructValue<'ctx>, String> {
        if args.len() != expected_args {
            return Err(format!(
                "{builtin} erwartet {expected_args} Argumente, erhalten: {}",
                args.len()
            ));
        }

        let values: Vec<_> = args
            .iter()
            .map(|arg| self.compile_expr(arg))
            .collect::<Result<Vec<_>, _>>()?;
        let call_args: Vec<BasicMetadataValueEnum<'ctx>> =
            values.iter().map(|value| (*value).into()).collect();
        let result = self.call_rt(func, &call_args, ir_name)?;
        for value in values {
            self.call_rt_void(
                self.rt.moo_release,
                &[value.into()],
                &format!("{ir_name}_arg_release"),
            )?;
        }
        Ok(result)
    }

    /// Ruft eine void Runtime-Funktion auf
    fn call_rt_void(&self, func: FunctionValue<'ctx>, args: &[BasicMetadataValueEnum<'ctx>], name: &str)
        -> Result<(), String>
    {
        // Minimale Bare-Runtimes brauchen keine Refcounts. Die UEFI-App
        // mit Container-Runtime verwendet dieselben Ownership-Aufrufe wie hosted.
        if self.bare_mode && !self.bare_runtime_refcounts
            && (func == self.rt.moo_release || func == self.rt.moo_retain)
        {
            return Ok(());
        }
        if self.windows_x64_abi() {
            let mut win_args = Vec::with_capacity(args.len());
            for (index, arg) in args.iter().enumerate() {
                match arg {
                    BasicMetadataValueEnum::StructValue(value) => {
                        let arg_ptr = self.entry_alloca(
                            self.mv_type(),
                            &format!("{name}_arg{index}"),
                        )?;
                        self.builder.build_store(arg_ptr, *value)
                            .map_err(|e| format!("{e}"))?;
                        win_args.push(arg_ptr.into());
                    }
                    other => win_args.push(*other),
                }
            }
            self.builder.build_call(func, &win_args, name)
                .map_err(|e| format!("{e}"))?;
        } else {
            self.builder.build_call(func, args, name)
                .map_err(|e| format!("{e}"))?;
        }
        Ok(())
    }


    /// Runtime-Call mit beliebigem Rueckgabetyp; passt unter MSVC x64
    /// 16-Byte-MooValue-Argumente auf indirekte Zeiger an.
    fn call_rt_abi(
        &self,
        func: FunctionValue<'ctx>,
        args: &[BasicMetadataValueEnum<'ctx>],
        name: &str,
    ) -> Result<inkwell::values::CallSiteValue<'ctx>, String> {
        let func = self.bare_schnellweg(func);
        if self.windows_x64_abi() {
            let mut win_args = Vec::with_capacity(args.len());
            for (index, arg) in args.iter().enumerate() {
                match arg {
                    BasicMetadataValueEnum::StructValue(value) => {
                        let arg_ptr = self.entry_alloca(
                            self.mv_type(),
                            &format!("{name}_arg{index}"),
                        )?;
                        self.builder.build_store(arg_ptr, *value)
                            .map_err(|e| format!("{e}"))?;
                        win_args.push(arg_ptr.into());
                    }
                    other => win_args.push(*other),
                }
            }
            self.builder.build_call(func, &win_args, name)
                .map_err(|e| format!("{e}"))
        } else {
            self.builder.build_call(func, args, name)
                .map_err(|e| format!("{e}"))
        }
    }


    /// P7-T20: Inline-Schnellweg der Bare-Zahlarithmetik.
    ///
    /// Seit P7-T17 (Tags 30/31 fuer exakte i64/u64) muss moo_bare.c den Tag
    /// pruefen. Als externer Aufruf kostete das +8..25 % gegenueber dem Stand
    /// vor T17 (Thought fc5a3ec3). Hier entsteht je Operator eine interne
    /// alwaysinline-Funktion: Sind beide Operanden Tag 0 (Zahl), rechnet sie
    /// direkt in double, mit derselben Semantik wie der Schnellweg in
    /// moo_bare.c (fadd/fsub/fmul/fdiv, fcmp o** bzw. une fuer !=). Jeder
    /// andere Tag ruft die Runtime auf (kalter Pfad, T17-Semantik unveraendert).
    /// Modulo und Bitops konvertieren nur nach geprueftem i64-Bereich;
    /// ausserhalb bleibt der bisherige Runtime-Pfad statt LLVM-poison.
    /// Nur im Bare-Profil und nicht unter MSVC-ABI (dort indirekte Argumente).
    fn bare_schnellweg(&self, func: FunctionValue<'ctx>) -> FunctionValue<'ctx> {
        if !self.bare_mode || self.windows_x64_abi() {
            return func;
        }
        use inkwell::FloatPredicate as P;
        // --erlaube-kern setzt bare_mode auch fuer die Hosted-Runtime.
        // Neue P7-T21-Regeln gelten nur fuer moo_bare.c, nicht moo_ops.c.
        let freestanding = self.module.get_triple().as_str().to_string_lossy().contains("-none");
        let name = match func.get_name().to_str() {
            Ok(n) => n.to_string(),
            Err(_) => return func,
        };
        let art = match name.as_str() {
            "moo_add" => BareSchnell::Rechnen(0),
            "moo_sub" => BareSchnell::Rechnen(1),
            "moo_mul" => BareSchnell::Rechnen(2),
            "moo_div" => BareSchnell::Rechnen(3),
            "moo_mod" if freestanding => BareSchnell::Rechnen(4),
            "moo_neg" if freestanding => BareSchnell::Negativ,
            "moo_bitand" if freestanding => BareSchnell::Bit(0),
            "moo_bitor" if freestanding => BareSchnell::Bit(1),
            "moo_bitxor" if freestanding => BareSchnell::Bit(2),
            "moo_lshift" if freestanding => BareSchnell::Bit(3),
            "moo_rshift" if freestanding => BareSchnell::Bit(4),
            "moo_bitnot" if freestanding => BareSchnell::BitNicht,
            "moo_lt" => BareSchnell::Vergleich(P::OLT),
            "moo_gt" => BareSchnell::Vergleich(P::OGT),
            "moo_lte" => BareSchnell::Vergleich(P::OLE),
            "moo_gte" => BareSchnell::Vergleich(P::OGE),
            "moo_eq" => BareSchnell::Vergleich(P::OEQ),
            "moo_neq" => BareSchnell::Vergleich(P::UNE),
            "moo_is_truthy" => BareSchnell::Wahrheit,
            "moo_number" => BareSchnell::Zahl,
            _ => return func,
        };
        // P7-T21: Nur diese kanonischen Bare-Operatoren schreiben keinen
        // Speicher (moo_bare.c: Arithmetik/Vergleich/truthy). Stringvergleich
        // und truthy lesen ggf. Heap-Daten: deshalb memory(read), NICHT none.
        // Keine Aussage ueber Hardware-, Print-, Allocator- oder Useraufrufe.
        // Indirekte Windows-ABI wurde oben bereits ausgeschlossen.
        // LLVM 22 MemoryEffects::readOnly().toIntValue() = 0x555; per
        // LLVMParseIRInContext("memory(read)")/LLVMGetEnumAttributeValue belegt.
        let memory_kind = inkwell::attributes::Attribute::get_named_enum_kind_id("memory");
        if freestanding && memory_kind != 0 && func.count_basic_blocks() == 0 {
            func.add_attribute(inkwell::attributes::AttributeLoc::Function,
                self.context.create_enum_attribute(memory_kind, 0x555));
        }
        let name_schnell = format!("{name}.bare_schnell");
        if let Some(f) = self.module.get_function(&name_schnell) {
            return f;
        }
        let f = self.module.add_function(
            &name_schnell, func.get_type(), Some(inkwell::module::Linkage::Internal));
        let kind = inkwell::attributes::Attribute::get_named_enum_kind_id("alwaysinline");
        f.add_attribute(inkwell::attributes::AttributeLoc::Function,
            self.context.create_enum_attribute(kind, 0));
        match self.baue_bare_schnellweg(func, f, art) {
            Ok(()) => f,
            Err(_) => {
                // Unvollstaendigen Rumpf verwerfen; dann bleibt der Runtime-Aufruf.
                unsafe { f.delete(); }
                func
            }
        }
    }

    fn baue_bare_schnellweg(
        &self,
        func: FunctionValue<'ctx>,
        f: FunctionValue<'ctx>,
        art: BareSchnell,
    ) -> Result<(), String> {
        // Tags wie BareTag in moo_bare.c: 0 = Zahl, 2 = bool.
        const TAG_ZAHL: u64 = 0;
        const TAG_BOOL: u64 = 2;
        let b = self.context.create_builder();
        let i64t = self.context.i64_type();
        let f64t = self.context.f64_type();
        let fehler = |e: inkwell::builder::BuilderError| format!("{e}");
        let start = self.context.append_basic_block(f, "start");
        let wert = |tag: u64, daten: inkwell::values::IntValue<'ctx>|
            -> Result<StructValue<'ctx>, String> {
            let v = b.build_insert_value(self.mv_type().get_undef(),
                i64t.const_int(tag, false), 0, "v0").map_err(fehler)?;
            Ok(b.build_insert_value(v, daten, 1, "v").map_err(fehler)?.into_struct_value())
        };
        let zahl = |v: StructValue<'ctx>, n: &str|
            -> Result<inkwell::values::FloatValue<'ctx>, String> {
            let bits = b.build_extract_value(v, 1, n).map_err(fehler)?.into_int_value();
            Ok(b.build_bit_cast(bits, f64t, n).map_err(fehler)?.into_float_value())
        };
        // fptosi ist nur in [-2^63,2^63) definiert. Geordnete Vergleiche
        // schliessen auch NaN/Inf aus; ausserhalb bleibt der Runtime-Pfad.
        let im_i64 = |x: inkwell::values::FloatValue<'ctx>|
            -> Result<inkwell::values::IntValue<'ctx>, String> {
            let unten = b.build_float_compare(inkwell::FloatPredicate::OGE, x,
                f64t.const_float(-9223372036854775808.0), "i64_unten").map_err(fehler)?;
            let oben = b.build_float_compare(inkwell::FloatPredicate::OLT, x,
                f64t.const_float(9223372036854775808.0), "i64_oben").map_err(fehler)?;
            b.build_and(unten, oben, "i64_bereich").map_err(fehler)
        };
        b.position_at_end(start);
        if let BareSchnell::Zahl = art {
            let x = f.get_nth_param(0).ok_or("bare_schnellweg: Parameter fehlt")?.into_float_value();
            let bits = b.build_bit_cast(x, i64t, "bits").map_err(fehler)?.into_int_value();
            b.build_return(Some(&wert(TAG_ZAHL, bits)?)).map_err(fehler)?;
            return Ok(());
        }
        let schnell = self.context.append_basic_block(f, "schnell");
        let kalt = self.context.append_basic_block(f, "kalt");
        let params: Vec<BasicMetadataValueEnum<'ctx>> =
            f.get_param_iter().map(|p| p.into()).collect();
        let a = f.get_nth_param(0).ok_or("bare_schnellweg: Parameter fehlt")?.into_struct_value();
        let tag_a = b.build_extract_value(a, 0, "tag_a").map_err(fehler)?.into_int_value();
        match art {
            BareSchnell::Wahrheit => {
                let ret_t = func.get_type().get_return_type()
                    .ok_or("moo_is_truthy ohne Rueckgabe")?.into_int_type();
                let als_ret = |v: inkwell::values::IntValue<'ctx>|
                    -> Result<inkwell::values::IntValue<'ctx>, String> {
                    if ret_t.get_bit_width() == 1 { Ok(v) }
                    else { b.build_int_z_extend(v, ret_t, "w").map_err(fehler) }
                };
                let pruef_bool = self.context.append_basic_block(f, "pruef_bool");
                let ist_bool = self.context.append_basic_block(f, "ist_bool");
                let ist_zahl = b.build_int_compare(inkwell::IntPredicate::EQ, tag_a,
                    i64t.const_zero(), "ist_zahl").map_err(fehler)?;
                b.build_conditional_branch(ist_zahl, schnell, pruef_bool).map_err(fehler)?;
                b.position_at_end(schnell);
                let w = b.build_float_compare(inkwell::FloatPredicate::UNE, zahl(a, "x")?,
                    f64t.const_zero(), "w").map_err(fehler)?;
                b.build_return(Some(&als_ret(w)?)).map_err(fehler)?;
                b.position_at_end(pruef_bool);
                let ist_b = b.build_int_compare(inkwell::IntPredicate::EQ, tag_a,
                    i64t.const_int(TAG_BOOL, false), "ist_b").map_err(fehler)?;
                b.build_conditional_branch(ist_b, ist_bool, kalt).map_err(fehler)?;
                b.position_at_end(ist_bool);
                let daten = b.build_extract_value(a, 1, "d").map_err(fehler)?.into_int_value();
                let w = b.build_int_compare(inkwell::IntPredicate::NE, daten,
                    i64t.const_zero(), "wb").map_err(fehler)?;
                b.build_return(Some(&als_ret(w)?)).map_err(fehler)?;
            }
            BareSchnell::Negativ | BareSchnell::BitNicht => {
                let ist_zahl = b.build_int_compare(inkwell::IntPredicate::EQ, tag_a,
                    i64t.const_zero(), "ist_zahl").map_err(fehler)?;
                b.build_conditional_branch(ist_zahl, schnell, kalt).map_err(fehler)?;
                b.position_at_end(schnell);
                let x = zahl(a, "x")?;
                let r = if let BareSchnell::Negativ = art {
                    b.build_float_neg(x, "neg").map_err(fehler)?
                } else {
                    let konvertieren = self.context.append_basic_block(f, "konvertieren");
                    b.build_conditional_branch(im_i64(x)?, konvertieren, kalt).map_err(fehler)?;
                    b.position_at_end(konvertieren);
                    let ganz = b.build_float_to_signed_int(x, i64t, "ganz").map_err(fehler)?;
                    let bits = b.build_not(ganz, "bitnicht").map_err(fehler)?;
                    b.build_signed_int_to_float(bits, f64t, "zurueck").map_err(fehler)?
                };
                let bits = b.build_bit_cast(r, i64t, "bits").map_err(fehler)?.into_int_value();
                b.build_return(Some(&wert(TAG_ZAHL, bits)?)).map_err(fehler)?;
            }
            BareSchnell::Rechnen(_) | BareSchnell::Vergleich(_) | BareSchnell::Bit(_) => {
                let bb = f.get_nth_param(1).ok_or("bare_schnellweg: Parameter fehlt")?.into_struct_value();
                let tag_b = b.build_extract_value(bb, 0, "tag_b").map_err(fehler)?.into_int_value();
                let beide = b.build_or(tag_a, tag_b, "tags").map_err(fehler)?;
                let ist_zahl = b.build_int_compare(inkwell::IntPredicate::EQ, beide,
                    i64t.const_zero(), "zwei_zahlen").map_err(fehler)?;
                b.build_conditional_branch(ist_zahl, schnell, kalt).map_err(fehler)?;
                b.position_at_end(schnell);
                let x = zahl(a, "x")?;
                let y = zahl(bb, "y")?;
                let (tag, daten) = match art {
                    BareSchnell::Rechnen(op) => {
                        let r = if op == 4 {
                            // Gleiche Formel wie moo_bare.c, nicht frem/fmod:
                            // x - (double)(int64_t)(x/y) * y. Cast nur im Bereich.
                            let q = b.build_float_div(x, y, "quotient").map_err(fehler)?;
                            let konvertieren = self.context.append_basic_block(f, "konvertieren");
                            b.build_conditional_branch(im_i64(q)?, konvertieren, kalt).map_err(fehler)?;
                            b.position_at_end(konvertieren);
                            let ganz = b.build_float_to_signed_int(q, i64t, "quotient_ganz").map_err(fehler)?;
                            let zurueck = b.build_signed_int_to_float(ganz, f64t, "quotient_zahl").map_err(fehler)?;
                            let produkt = b.build_float_mul(zurueck, y, "produkt").map_err(fehler)?;
                            b.build_float_sub(x, produkt, "rest").map_err(fehler)?
                        } else {
                            match op {
                                0 => b.build_float_add(x, y, "r"),
                                1 => b.build_float_sub(x, y, "r"),
                                2 => b.build_float_mul(x, y, "r"),
                                _ => b.build_float_div(x, y, "r"),
                            }.map_err(fehler)?
                        };
                        (TAG_ZAHL, b.build_bit_cast(r, i64t, "bits").map_err(fehler)?.into_int_value())
                    }
                    BareSchnell::Bit(op) => {
                        let konvertieren = self.context.append_basic_block(f, "konvertieren");
                        let bereich = b.build_and(im_i64(x)?, im_i64(y)?, "beide_im_bereich").map_err(fehler)?;
                        b.build_conditional_branch(bereich, konvertieren, kalt).map_err(fehler)?;
                        b.position_at_end(konvertieren);
                        let xi = b.build_float_to_signed_int(x, i64t, "x_ganz").map_err(fehler)?;
                        let yi = b.build_float_to_signed_int(y, i64t, "y_ganz").map_err(fehler)?;
                        let bits = match op {
                            0 => b.build_and(xi, yi, "bitund"),
                            1 => b.build_or(xi, yi, "bitoder"),
                            2 => b.build_xor(xi, yi, "bitxor"),
                            3 => {
                                let count = b.build_and(yi, i64t.const_int(63, false), "shift63").map_err(fehler)?;
                                b.build_left_shift(xi, count, "links")
                            }
                            _ => {
                                let count = b.build_and(yi, i64t.const_int(63, false), "shift63").map_err(fehler)?;
                                b.build_right_shift(xi, count, true, "rechts")
                            }
                        }.map_err(fehler)?;
                        let r = b.build_signed_int_to_float(bits, f64t, "zurueck").map_err(fehler)?;
                        (TAG_ZAHL, b.build_bit_cast(r, i64t, "bits").map_err(fehler)?.into_int_value())
                    }
                    BareSchnell::Vergleich(pred) => {
                        let c = b.build_float_compare(pred, x, y, "c").map_err(fehler)?;
                        (TAG_BOOL, b.build_int_z_extend(c, i64t, "cb").map_err(fehler)?)
                    }
                    _ => unreachable!("durch aeusseren Match ausgeschlossen"),
                };
                b.build_return(Some(&wert(tag, daten)?)).map_err(fehler)?;
            }
            BareSchnell::Zahl => unreachable!("oben behandelt"),
        }
        b.position_at_end(kalt);
        let r = b.build_call(func, &params, "kalt").map_err(fehler)?;
        match r.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => { b.build_return(Some(&v)).map_err(fehler)?; }
            _ => return Err("bare_schnellweg: Runtime ohne Rueckgabe".to_string()),
        }
        Ok(())
    }

    /// Erzeugt fuer einen internen Moo-Funktionstyp einen Adapter, den die
    /// C-Runtime unter MSVC x64 sicher als MooValue(*)(MooValue...) aufrufen kann.
    fn windows_callable_wrapper(
        &self,
        function: FunctionValue<'ctx>,
        source_name: &str,
    ) -> Result<FunctionValue<'ctx>, String> {
        if !self.windows_x64_abi() {
            return self.c_aufteilungs_wrapper(function, source_name);
        }

        let wrapper_name = format!("{source_name}.__moo_winabi");
        if let Some(existing) = self.module.get_function(&wrapper_name) {
            return Ok(existing);
        }

        let ptr_type = self.context.ptr_type(AddressSpace::default());
        let inner_param_types = function.get_type().get_param_types();
        let mut wrapper_param_types: Vec<BasicMetadataTypeEnum<'ctx>> =
            Vec::with_capacity(inner_param_types.len() + 1);
        wrapper_param_types.push(ptr_type.into());
        wrapper_param_types.extend(inner_param_types.iter().map(|param| match param {
            BasicMetadataTypeEnum::StructType(_) => ptr_type.into(),
            other => *other,
        }));

        // Die MSVC-sret-Konvention liefert den Rueckgabepuffer zusaetzlich in RAX.
        let wrapper_type = ptr_type.fn_type(&wrapper_param_types, false);
        let wrapper = self.module.add_function(&wrapper_name, wrapper_type, None);
        let entry = self.context.append_basic_block(wrapper, "entry");
        let previous_block = self.builder.get_insert_block();
        self.builder.position_at_end(entry);

        let mut inner_args = Vec::with_capacity(inner_param_types.len());
        for (index, param_type) in inner_param_types.iter().enumerate() {
            let wrapper_arg = wrapper.get_nth_param((index + 1) as u32)
                .ok_or_else(|| format!("Windows-ABI-Wrapper '{source_name}': Argument fehlt"))?;
            match param_type {
                BasicMetadataTypeEnum::StructType(_) => {
                    let loaded = self.builder.build_load(
                        self.mv_type(),
                        wrapper_arg.into_pointer_value(),
                        &format!("arg{index}"),
                    ).map_err(|e| format!("{e}"))?;
                    inner_args.push(loaded.into());
                }
                _ => inner_args.push(wrapper_arg.into()),
            }
        }

        let result = self.builder.build_call(function, &inner_args, "inner")
            .map_err(|e| format!("{e}"))?
            .try_as_basic_value();
        let value = match result {
            inkwell::values::ValueKind::Basic(value) => value.into_struct_value(),
            _ => return Err(format!("Windows-ABI-Wrapper '{source_name}': kein MooValue")),
        };
        let result_ptr = wrapper.get_nth_param(0)
            .ok_or_else(|| format!("Windows-ABI-Wrapper '{source_name}': sret fehlt"))?
            .into_pointer_value();
        self.builder.build_store(result_ptr, value).map_err(|e| format!("{e}"))?;
        self.builder.build_return(Some(&result_ptr)).map_err(|e| format!("{e}"))?;

        if let Some(block) = previous_block {
            self.builder.position_at_end(block);
        }
        Ok(wrapper)
    }

    /// P2-T117: C ruft Moo-Funktionen ueber Funktionszeiger (moo_func_call_N,
    /// Closure-Trampolin `(MooFunc* env, MooValue...)`). Fuer C ist ein MooValue
    /// ein 16-Byte-Struct aus zwei Integern: SysV x86-64 und AAPCS64 legen es
    /// GANZ auf den Stack, sobald keine zwei Integerregister mehr frei sind.
    /// LLVM teilt ein first-class `{i64, i64}` dagegen auf (letztes Register +
    /// Stack). Beim Trampolin belegt env ein Register; ab dem dritten MooValue
    /// las die Funktion deshalb Muell (test_closure_4arg_call_abort: SIGSEGV,
    /// `falsch` kam als Zahl 9.88e-324 an). Der Wrapper nimmt genau diese
    /// Parameter wie clang entgegen (x86-64: `ptr byval`, AArch64: `[2 x i64]`)
    /// und reicht sie an die unveraenderte Funktion weiter. Wuerde kein
    /// Parameter aufgeteilt, bleibt die Funktion unveraendert (kein IR-Diff).
    fn c_aufteilungs_wrapper(
        &self,
        function: FunctionValue<'ctx>,
        source_name: &str,
    ) -> Result<FunctionValue<'ctx>, String> {
        #[derive(Clone, Copy, PartialEq)]
        enum Art { Direkt, Byval, Feld }
        let triple = self.module.get_triple().as_str().to_string_lossy().to_string();
        let (x86, a64) = if triple.is_empty() {
            (cfg!(target_arch = "x86_64"), cfg!(target_arch = "aarch64"))
        } else {
            // Auch vollstaendige arm64-Triple brauchen den AArch64-Wrapper.
            (
                triple.starts_with("x86_64"),
                triple.starts_with("aarch64") || triple.starts_with("arm64"),
            )
        };
        if !(x86 || a64) || triple.contains("windows") {
            return Ok(function);
        }
        let mv = self.mv_type();
        let param_types = function.get_type().get_param_types();
        let mut frei: u32 = if x86 { 6 } else { 8 };
        let mut arten = Vec::with_capacity(param_types.len());
        for t in &param_types {
            match t {
                BasicMetadataTypeEnum::StructType(st) if *st == mv => {
                    if frei >= 2 {
                        frei -= 2;
                        arten.push(Art::Direkt);
                    } else if x86 {
                        // SysV: Struct in den Speicher, Restregister bleibt frei.
                        arten.push(Art::Byval);
                    } else {
                        // AAPCS64 C.11: NGRN = 8, alles Weitere auf den Stack.
                        frei = 0;
                        arten.push(Art::Feld);
                    }
                }
                BasicMetadataTypeEnum::IntType(_) | BasicMetadataTypeEnum::PointerType(_) => {
                    frei = frei.saturating_sub(1);
                    arten.push(Art::Direkt);
                }
                _ => arten.push(Art::Direkt),
            }
        }
        if arten.iter().all(|a| *a == Art::Direkt) {
            return Ok(function);
        }

        let wrapper_name = format!("{source_name}.__moo_cabi");
        if let Some(existing) = self.module.get_function(&wrapper_name) {
            return Ok(existing);
        }
        let ptr_type = self.context.ptr_type(AddressSpace::default());
        let feld_type = self.context.i64_type().array_type(2);
        let wrapper_params: Vec<BasicMetadataTypeEnum<'ctx>> = param_types.iter().zip(&arten)
            .map(|(t, a)| match a {
                Art::Direkt => *t,
                Art::Byval => ptr_type.into(),
                Art::Feld => feld_type.into(),
            })
            .collect();
        let wrapper_type = match function.get_type().get_return_type() {
            Some(ret) => inkwell::types::BasicType::fn_type(&ret, &wrapper_params, false),
            None => self.context.void_type().fn_type(&wrapper_params, false),
        };
        let wrapper = self.module.add_function(&wrapper_name, wrapper_type, None);
        {
            use inkwell::attributes::{Attribute, AttributeLoc};
            let byval = Attribute::get_named_enum_kind_id("byval");
            let align = Attribute::get_named_enum_kind_id("align");
            for (i, a) in arten.iter().enumerate() {
                if *a == Art::Byval {
                    let mv_any = inkwell::types::AnyType::as_any_type_enum(&mv);
                    wrapper.add_attribute(AttributeLoc::Param(i as u32),
                        self.context.create_type_attribute(byval, mv_any));
                    wrapper.add_attribute(AttributeLoc::Param(i as u32),
                        self.context.create_enum_attribute(align, 8));
                }
            }
        }
        let entry = self.context.append_basic_block(wrapper, "entry");
        let previous_block = self.builder.get_insert_block();
        self.builder.position_at_end(entry);

        let mut inner_args: Vec<BasicMetadataValueEnum<'ctx>> = Vec::with_capacity(arten.len());
        for (i, a) in arten.iter().enumerate() {
            let p = wrapper.get_nth_param(i as u32)
                .ok_or_else(|| format!("C-ABI-Wrapper '{source_name}': Argument fehlt"))?;
            match a {
                Art::Direkt => inner_args.push(p.into()),
                Art::Byval => {
                    let v = self.builder.build_load(mv, p.into_pointer_value(), &format!("arg{i}"))
                        .map_err(|e| format!("{e}"))?;
                    inner_args.push(v.into());
                }
                Art::Feld => {
                    let feld = p.into_array_value();
                    let lo = self.builder.build_extract_value(feld, 0, &format!("arg{i}.0"))
                        .map_err(|e| format!("{e}"))?;
                    let hi = self.builder.build_extract_value(feld, 1, &format!("arg{i}.1"))
                        .map_err(|e| format!("{e}"))?;
                    let v = self.builder.build_insert_value(mv.get_undef(), lo, 0, &format!("arg{i}.a"))
                        .map_err(|e| format!("{e}"))?;
                    let v = self.builder.build_insert_value(v, hi, 1, &format!("arg{i}"))
                        .map_err(|e| format!("{e}"))?;
                    inner_args.push(v.into_struct_value().into());
                }
            }
        }
        let call = self.builder.build_call(function, &inner_args, "inner")
            .map_err(|e| format!("{e}"))?;
        match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => {
                self.builder.build_return(Some(&v)).map_err(|e| format!("{e}"))?;
            }
            _ => {
                self.builder.build_return(None).map_err(|e| format!("{e}"))?;
            }
        }
        if let Some(block) = previous_block {
            self.builder.position_at_end(block);
        }
        Ok(wrapper)
    }

    fn store_var(&mut self, name: &str, val: StructValue<'ctx>) -> Result<(), String> {
        let ptr = if let Some(existing) = self.variables.get(name) {
            // Release alten Wert bevor wir ueberschreiben
            let old = self.builder.build_load(self.mv_type(), *existing, "old")
                .map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_release, &[old.into()], "release_old")?;
            *existing
        } else if let Some(global_ptr) = self.globals.get(name).copied() {
            // Zuweisung auf globale Variable (z.B. aus einer Funktion heraus)
            self.global_zugriff_pruefen(name)?;
            let old = self.builder.build_load(self.mv_type(), global_ptr, "old_g")
                .map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_release, &[old.into()], "release_old_g")?;
            global_ptr
        } else {
            let func = self.current_function.unwrap();
            let entry = func.get_first_basic_block().unwrap();
            let current_block = self.builder.get_insert_block().unwrap();

            if let Some(first_instr) = entry.get_first_instruction() {
                self.builder.position_before(&first_instr);
            } else {
                self.builder.position_at_end(entry);
            }
            let alloca = self.builder.build_alloca(self.mv_type(), name)
                .map_err(|e| format!("{e}"))?;
            // Sofort mit None initialisieren damit release auf definierten Wert trifft
            let none_init = self.context.i64_type().const_int(tag::NONE, false);
            let zero_data = self.context.i64_type().const_int(0, false);
            let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
            self.builder.build_store(alloca, none_val).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(current_block);
            self.variables.insert(name.to_string(), alloca);
            alloca
        };
        // Transfer-Semantik: Producer hat val mit refcount=1 erzeugt,
        // store uebernimmt die Referenz. Kein retain hier, sonst Leak.
        // Bei load_var-Quelle wird dort geretained (clone-Semantik).
        self.builder.build_store(ptr, val).map_err(|e| format!("{e}"))?;
        Ok(())
    }

    /// P2-T114 Phase C: setzt den Pruefplan der Effektpruefung
    /// (typpruefung::globale_pruefen). Leer fuer Programme ohne starte.
    pub fn set_global_pruefplan(&mut self, plan: crate::typpruefung::GlobalePruefplan) {
        self.global_pruefplan = plan;
    }

    /// Geteilte Werte und Globale: Hosted oder der explizite native Thread-
    /// Träger stellen moo_teilen/moo_global_pruefen bereit; wasm weiterhin nicht.
    fn rc_gehostet(&self) -> bool {
        (!self.bare_mode || self.native_thread_runtime) && !self.module.get_triple().as_str().to_str()
            .map(|t| t.starts_with("wasm")).unwrap_or(true)
    }

    /// P2-T114 Phase C: Globale gehoeren dem Hauptthread. In Code, den die
    /// Effektpruefung als vielleicht thread-erreichbar, aber nicht beweisbar
    /// eingestuft hat (Funktionswerte, Methoden), prueft moo_global_pruefen
    /// vor dem Zugriff, ob ein Moo-Thread zugreift. Sonst kein Code.
    fn global_zugriff_pruefen(&self, name: &str) -> Result<(), String> {
        let plan = &self.global_pruefplan;
        if plan.funktionen.is_empty() && !plan.lambdas {
            return Ok(());
        }
        if plan.geteilt.contains(name) || !self.rc_gehostet() {
            return Ok(());
        }
        let Some(funktion) = self.current_function else { return Ok(()) };
        let fname = funktion.get_name().to_str().unwrap_or("");
        if !plan.funktionen.contains(fname) && !(plan.lambdas && fname.starts_with("__lambda_")) {
            return Ok(());
        }
        // P2-T114 B: gehoben. Die Thread-Kennung wird einmal am Funktionseintritt
        // gelesen (Linux: TLS-Laden wie moo_error_flag, sonst ein Aufruf); je
        // Zugriff bleibt ein Sprung auf einen Registerwert, schleifeninvariant.
        // Semantik wie zuvor: Abbruch nur beim tatsaechlichen Zugriff.
        let fremd = self.global_fremd_flag(funktion, fname)?;
        let ptr_ty = self.context.ptr_type(AddressSpace::default());
        let abbruch = match self.module.get_function("moo_global_fremdzugriff") {
            Some(f) => f,
            None => {
                let f = self.module.add_function("moo_global_fremdzugriff",
                    self.context.void_type().fn_type(&[ptr_ty.into()], false), None);
                for attr in ["noreturn", "cold", "nounwind"] {
                    let kind = inkwell::attributes::Attribute::get_named_enum_kind_id(attr);
                    f.add_attribute(inkwell::attributes::AttributeLoc::Function,
                        self.context.create_enum_attribute(kind, 0));
                }
                f
            }
        };
        let fail = self.context.append_basic_block(funktion, "global.fremd");
        let ok = self.context.append_basic_block(funktion, "global.ok");
        self.builder.build_conditional_branch(fremd, fail, ok).map_err(|e| format!("{e}"))?;
        self.builder.position_at_end(fail);
        let text = self.make_global_str(name, "global_name")?;
        self.builder.build_call(abbruch, &[text.into()], "").map_err(|e| format!("{e}"))?;
        self.builder.build_unreachable().map_err(|e| format!("{e}"))?;
        self.builder.position_at_end(ok);
        Ok(())
    }

    /// P2-T114: Thread-Kennung einmal im Eintrittsblock, vor dessen
    /// Terminierung erzeugt; dominiert jeden spaeteren globalen Zugriff.
    fn global_fremd_flag(&self, funktion: FunctionValue<'ctx>, fname: &str) -> Result<inkwell::values::IntValue<'ctx>, String> {
        use inkwell::values::AsValueRef;
        let fname = format!("{fname}@{:p}", funktion.as_value_ref());
        if let Some(v) = self.global_fremd_cache.borrow().get(&fname) {
            return Ok(*v);
        }
        let entry = funktion.get_first_basic_block().ok_or("Funktion ohne Eintrittsblock")?;
        let b = self.context.create_builder();
        // P2-T114: Nicht vor Parametern/Retains laden. Sonst muss das Flag
        // fremde Aufrufe ueberleben; beim ersten Zugriff im Entry verhindert
        // das etwa x86 cmpb direkt aus TLS. Vor dem Entry-Terminator bleibt
        // die Definition auch fuer Zugriffe in verschiedenen Zweigen dominant.
        match entry.get_terminator() {
            Some(i) => b.position_before(&i),
            None => b.position_at_end(entry),
        }
        let i8_t = self.context.i8_type();
        let elf_tls = self.module.get_triple().as_str().to_string_lossy().contains("linux");
        let wert = if elf_tls {
            let g = self.module.get_global("moo_ist_moo_worker").unwrap_or_else(|| {
                let g = self.module.add_global(i8_t, None, "moo_ist_moo_worker");
                g.set_thread_local_mode(Some(inkwell::ThreadLocalMode::LocalExecTLSModel));
                g
            });
            b.build_load(i8_t, g.as_pointer_value(), "global.worker").map_err(|e| format!("{e}"))?.into_int_value()
        } else {
            let f = match self.module.get_function("moo_global_fremd") {
                Some(f) => f,
                None => self.module.add_function("moo_global_fremd", i8_t.fn_type(&[], false), None),
            };
            match b.build_call(f, &[], "global.worker").map_err(|e| format!("{e}"))?.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("moo_global_fremd liefert keinen Wert".into()),
            }
        };
        let fremd = b.build_int_compare(inkwell::IntPredicate::NE, wert, i8_t.const_zero(), "global.fremd")
            .map_err(|e| format!("{e}"))?;
        self.global_fremd_cache.borrow_mut().insert(fname.to_string(), fremd);
        Ok(fremd)
    }

    /// P2-T114 B: Runtime-Funktion (MooValue^n) -> MooValue erst bei Nutzung
    /// deklarieren, damit das IR von Programmen ohne Atom/Sperre gleich bleibt.
    /// Windows x64: sret-Puffer und Aggregate per Zeiger (wie decl_mv_mv!).
    fn rt_mv_lazy(&self, symbol: &str, n: usize) -> FunctionValue<'ctx> {
        if let Some(f) = self.module.get_function(symbol) {
            return f;
        }
        let ptr = self.context.ptr_type(AddressSpace::default());
        if self.windows_x64_abi() {
            let params: Vec<BasicMetadataTypeEnum<'ctx>> = (0..=n).map(|_| ptr.into()).collect();
            self.module.add_function(symbol, self.context.void_type().fn_type(&params, false), None)
        } else {
            let params: Vec<BasicMetadataTypeEnum<'ctx>> = (0..n).map(|_| self.mv_type().into()).collect();
            self.module.add_function(symbol, self.mv_type().fn_type(&params, false), None)
        }
    }

    /// P2-T114 B: atom(z), atom_*(a, ...), sperre(w) und die Hilfen des
    /// `mit s als k:`-Blocks (Parser-Entzuckerung). Argumente geliehen und
    /// danach freigegeben; sperre(w) uebernimmt w (owning).
    fn sync_builtin(&mut self, name: &str, args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        if !self.rc_gehostet() {
            return Err(format!("'{name}' gibt es nur gehostet; in @nativ/Bare die atom_*-Intrinsics auf ptr[T] verwenden"));
        }
        let (symbol, n, uebernimmt) = match name {
            "atom" | "atomic" => ("moo_atom_neu", 1, false),
            "atom_laden" | "atomic_load" => ("moo_atom_laden", 1, false),
            "atom_speichern" | "atomic_store" => ("moo_atom_speichern", 2, false),
            "atom_addieren" | "atomic_add" => ("moo_atom_addieren", 2, false),
            "atom_tauschen" | "atomic_swap" => ("moo_atom_tauschen", 2, false),
            "atom_vergleichen_und_tauschen" | "atomic_compare_exchange" => ("moo_atom_vergleichen_und_tauschen", 3, false),
            "sperre" | "lock" => ("moo_sperre_neu", 1, true),
            "__moo_sperre_betreten" => ("moo_sperre_betreten", 1, false),
            _ => ("moo_sperre_verlassen", 2, false),
        };
        if args.len() != n {
            return Err(format!("{name} erwartet {n} Argument(e), erhalten {}", args.len()));
        }
        let mut werte = Vec::with_capacity(n);
        for a in args {
            let v = self.compile_expr(a)?;
            werte.push(v);
        }
        let f = self.rt_mv_lazy(symbol, n);
        let call_args: Vec<BasicMetadataValueEnum<'ctx>> = werte.iter().map(|v| (*v).into()).collect();
        let r = self.call_rt(f, &call_args, "sync")?;
        if !uebernimmt {
            for v in &werte {
                self.call_rt_void(self.rt.moo_release, &[(*v).into()], "sync_rel")?;
            }
        }
        Ok(r)
    }

    fn load_var(&self, name: &str) -> Result<StructValue<'ctx>, String> {
        let ptr = match self.variables.get(name) {
            Some(p) => p,
            None => {
                let p = self.globals.get(name).ok_or(format!("Variable '{name}' nicht gefunden"))?;
                self.global_zugriff_pruefen(name)?;
                p
            }
        };
        let val = self.builder.build_load(self.mv_type(), *ptr, name)
            .map_err(|e| format!("{e}"))?.into_struct_value();
        // Clone-Semantik: load liefert einen eigenen owning-ref, damit
        // store_var transferieren kann und Aliasing sicher ist.
        self.call_rt_void(self.rt.moo_retain, &[val.into()], "retain_load")?;
        Ok(val)
    }

    fn make_global_str(&self, s: &str, name: &str) -> Result<PointerValue<'ctx>, String> {
        let gstr = self.builder.build_global_string_ptr(s, name)
            .map_err(|e| format!("{e}"))?;
        Ok(gstr.as_pointer_value())
    }

    // === Statement-Kompilierung ===

    /// P2-T97: Diagnosezeile je Statement: die eigene (B5b, 0 = unbekannt), bis
    /// ein innerer Knoten mit Zeile (Aufruf, wirf) sie praezisiert; nach Erfolg gilt wieder die des
    /// umgebenden Statements, nach einem Fehler bleibt die innerste stehen.
    /// So ist eine gemeldete Zeile richtig oder fehlt, aber nie veraltet.
    fn compile_stmt(&mut self, stmt: &Stmt) -> Result<(), String> {
        let aussen = self.diag_zeile;
        self.diag_zeile = stmt.zeile();
        let ergebnis = self.compile_stmt_kern(stmt);
        if ergebnis.is_ok() {
            self.diag_zeile = aussen;
        }
        ergebnis
    }

    fn compile_stmt_kern(&mut self, stmt: &Stmt) -> Result<(), String> {
        let guarded = self.defer_runtime || self.defer_borrowed_handler.is_some();
        let result = match stmt {
            Stmt::ExternC { .. } => Err("extern C ist noch nicht implementiert".to_string()),
            Stmt::StructDef { .. } => Err("Native Strukturen sind noch nicht implementiert".to_string()),
            Stmt::Assignment { name, value, .. } | Stmt::ConstAssignment { name, value, .. } => {
                // Crystal-inspiriert: Typ der Zuweisung tracken
                if let Some(typ) = self.infer_type(value) {
                    self.variable_types.insert(name.clone(), typ);
                }
                // Objekt-Typ tracken fuer korrektes Method-Dispatch bei Vererbung:
                // setze k auf neu K() → object_var_types[k] = "K"
                // setze a auf b → uebertrage den Typ von b, falls bekannt
                match value {
                    Expr::New { class_name, .. } => {
                        self.object_var_types.insert(name.clone(), class_name.clone());
                    }
                    Expr::Identifier(other) => {
                        if let Some(t) = self.object_var_types.get(other).cloned() {
                            self.object_var_types.insert(name.clone(), t);
                        } else {
                            self.object_var_types.remove(name);
                        }
                    }
                    _ => { self.object_var_types.remove(name); }
                }
                // Lambda-Zuweisung: merke den Lambda-Namen fuer spaetere Aufrufe
                if let Expr::Lambda { .. } = value {
                    let lambda_name = format!("__lambda_{}", self.lambda_counter);
                    self.lambda_names.insert(name.clone(), lambda_name);
                    // Counter wird in compile_expr/Lambda erhoeht
                }
                // P2-T74: Direktpfad wie bei zeige (native_try_direct_root).
                let val = self.compile_statement_expr_direct(value)?;
                self.guard_local_expr_error(&[val])?;
                // P2-T114 Phase C: `geteilt` markierte Globale werden bei der
                // Initialisierung tief eingefroren (geteilt = eingefroren).
                // P2-T114 B: ebenso unveraenderliche Text-Konstanten, die Threads
                // ohne Markierung lesen (atomarer Zaehler statt Rennen).
                let text_teilen = matches!(stmt, Stmt::ConstAssignment { name: n, geteilt: false, .. }
                    if self.global_pruefplan.text_teilen.contains(n));
                if (text_teilen || matches!(stmt, Stmt::ConstAssignment { geteilt: true, .. })) && self.rc_gehostet() {
                    let teilen = match self.module.get_function("moo_teilen") {
                        Some(f) => f,
                        None => self.module.add_function("moo_teilen", self.rt.moo_release.get_type(), None),
                    };
                    self.call_rt_void(teilen, &[val.into()], "geteilt")?;
                }
                self.store_var(name, val)
            }
            Stmt::CompoundAssignment { name, op, value, .. } => {
                let current = self.load_var(name)?;
                let rhs = self.compile_defer_expr(value, &[current])?;
                let func = match op.as_str() {
                    "+=" => self.rt.moo_add,
                    "-=" => self.rt.moo_sub,
                    _ => return Err(format!("Unbekannter Operator: {op}")),
                };
                let result = self.call_rt(func,
                    &[current.into(), rhs.into()], "compound")?;
                if self.defer_runtime {
                    self.call_rt_void(self.rt.moo_release, &[current.into()], "compound.release_left")?;
                    self.call_rt_void(self.rt.moo_release, &[rhs.into()], "compound.release_right")?;
                    self.guard_local_expr_error(&[result])?;
                }
                self.store_var(name, result)
            }
            Stmt::PropertyAssignment { object, property, value, .. } => {
                let obj = self.compile_defer_expr(object, &[])?;
                let val = self.compile_defer_expr(value, &[obj])?;
                let prop_str = self.make_global_str(property, "prop")?;
                self.call_rt_void(self.rt.moo_object_set,
                    &[obj.into(), prop_str.into(), val.into()], "obj_set")?;
                self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_obj_propset")?;
                Ok(())
            }
            Stmt::IndexAssignment { object, index, value, .. } => {
                let obj = self.compile_statement_expr(object)?;
                self.guard_local_expr_error(&[obj])?;
                let idx = self.compile_owned_statement_expr(index, &[obj])?;
                self.guard_local_expr_error(&[obj, idx])?;
                let val = self.compile_owned_statement_expr(value, &[obj, idx])?;
                self.guard_local_expr_error(&[obj, idx, val])?;
                self.call_rt_void(self.rt.moo_index_set,
                    &[obj.into(), idx.into(), val.into()], "idx_set")?;
                // moo_index_set / moo_dict_set uebernehmen Ownership von
                // idx und val (transfer). obj (Container) wird nur genutzt,
                // der Load-Temp haelt noch +1 und muss hier freigegeben
                // werden, sonst leak pro Mutation.
                self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_obj")?;
                Ok(())
            }
            Stmt::Show(expr, _) => {
                // Direktpfad fuer native Argumentadapter in versuche: (P2-T66,
                // Kriterium in native_try_direct_root; P2-T74 auch setze/gib_zurück).
                let val = self.compile_statement_expr_direct(expr)?;
                self.guard_local_expr_error(&[val])?;
                self.call_rt_void(self.rt.moo_print, &[val.into()], "print")?;

                // moo_print liest nur; den Expression-Temp freigeben.
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_show")?;
                Ok(())
            }
            Stmt::If { condition, body, else_body, .. } => {
                self.compile_if(condition, body, else_body)
            }
            Stmt::While { condition, body, .. } => {
                self.compile_while(condition, body)
            }
            Stmt::For { var_name, iterable, body, .. } => {
                self.compile_for(var_name, iterable, body)
            }
            Stmt::FunctionDef { name, params, defaults, body, decorators, .. } => {
                if decorators.iter().any(|d| d.name == "nativ" || d.name == "native") {
                    return Err("Verschachtelte native Funktionen sind noch nicht unterstützt".into());
                }
                self.compile_function_def(name, params, defaults, body, decorators)?;
                // Decorators: func = decorator(func)
                for dec in decorators.iter().rev() {
                    // P011-A1: Builtin-Decorators (Interrupt/Layout) sind keine User-Wrapper.
                    if matches!(
                        dec.name.as_str(),
                        "interrupt" | "unterbrechung" | "benutzt" | "used"
                            | "einstieg" | "entry" | "sektion" | "section"
                            | "ausrichten" | "align"
                    ) {
                        continue;
                    }
                    if let Some(dec_fn) = self.module.get_function(&dec.name) {
                        let func_val = self.load_var(name)?;
                        let result = self.builder.build_call(dec_fn, &[func_val.into()], "decorated")
                            .map_err(|e| format!("{e}"))?
                            .try_as_basic_value();
                        match result {
                            inkwell::values::ValueKind::Basic(v) => {
                                self.store_var(name, v.into_struct_value())?;
                            }
                            _ => {}
                        }
                    }
                }
                Ok(())
            }
            Stmt::RawDataU32 { name, value, decorators, .. } => {
                self.compile_raw_data_u32(name, value, decorators)
            }
            Stmt::DataClassDef { name, fields, .. } => {
                self.compile_data_class(name, fields)
            }
            Stmt::Return(value, _) => self.compile_return(value),
            Stmt::Break(_) => {
                if let Some((_, after_bb, target_depth)) = self.loop_stack.last().copied() {
                    // P2-T101: anstehenden Fehler vor dem Sprung zum lokalen Catch.
                    self.fehler_vor_schleifensprung()?;
                    self.leave_loop_handlers(target_depth)?;
                    self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
                }
                Ok(())
            }
            Stmt::Continue(_) => {
                if let Some((cond_bb, _, target_depth)) = self.loop_stack.last().copied() {
                    // P2-T101: anstehenden Fehler vor dem Sprung zum lokalen Catch.
                    self.fehler_vor_schleifensprung()?;
                    self.leave_loop_handlers(target_depth)?;
                    self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;
                }
                Ok(())
            }
            Stmt::ClassDef { name, parent, body, .. } => {
                self.compile_class_def(name, parent, body)
            }
            Stmt::InterfaceDef { .. } => {
                // Interfaces sind nur syntaktischer Zucker — kein Runtime-Code
                Ok(())
            }
            Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                self.compile_try_catch(try_body, catch_var, catch_body)
            }
            // ERDE-13: reine Deklaration, wirkt in compile_function_def (keine Local-Alloca).
            Stmt::Global(_, _) => Ok(()),
            Stmt::Throw(expr, wirf_zeile) => {
                self.wirf_zeile = *wirf_zeile;
                if *wirf_zeile > 0 {
                    self.diag_zeile = *wirf_zeile;
                }
                let val = self.compile_statement_expr(expr)?;
                self.guard_local_expr_error(&[val])?;
                if self.defer_runtime && !self.try_stack.iter().any(|bb| bb.get_parent() == self.current_function) {
                    // Ohne lokalen Catch verlaesst wirf den Frame: registrierte
                    // Aktionen zuerst abarbeiten, val wird zum Erstfehler.
                    let (has, first) = self.drain_runtime_defers(Some(val))?;
                    self.route_runtime_defer_error(has, first, None, true)?;
                    return Ok(());
                }
                // P2-T95: Wurfort fuer den Hinweis bei unbehandeltem Fehler.
                if self.bare_mode {
                    self.call_rt_void(self.rt.moo_throw, &[val.into()], "throw")?;
                } else {
                    let ort = self.fehler_ort_konstante()?;
                    let throw_ort = self.fehler_rt_funktion("moo_throw_ort", true);
                    self.call_rt_void(throw_ort, &[val.into(), ort.into()], "throw")?;
                }
                // Wenn wir in einem try-Block sind: sofort zum innersten catch_bb
                // branchen. Das terminiert den aktuellen Block und stellt sicher,
                // dass Code nach 'wirf' (inkl. return) nicht ausgefuehrt wird.
                if let Some(catch_bb) = self.try_stack.last().copied() {
                    if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                        self.builder.build_unconditional_branch(catch_bb)
                            .map_err(|e| format!("{e}"))?;
                    }
                } else if !self.bare_mode && self.im_standardrahmen() && !self.in_main_function()
                    && self.builder.get_insert_block().unwrap().get_terminator().is_none()
                {
                    // P2-T93 (Befund dbea7b86): ohne Handler im eigenen Rahmen kehrt
                    // moo_throw nur bei aktivem Aufrufer-Handler zurueck (sonst
                    // Prozessende). Die Funktion bricht dann ab wie an einem
                    // Pruefpunkt (P2-T82/T92): registrierte statische aufraeumen-
                    // Aktionen laufen, Locals frei, Rueckkehr mit nichts, Flag bleibt
                    // gesetzt. Nachfolgender Code landet in einem toten Block.
                    let exit = self.native_guard_exit_block()?;
                    self.builder.build_unconditional_branch(exit).map_err(|e| format!("{e}"))?;
                    let function = self.current_function.ok_or("wirf ohne Funktion")?;
                    let tot = self.context.append_basic_block(function, "wirf.danach");
                    self.builder.position_at_end(tot);
                }
                Ok(())
            }
            Stmt::Match { value, cases, .. } => {
                self.compile_match(value, cases)
            }
            Stmt::Defer(expr, _) => {
                let id = self.defer_catalog.id_for(expr)
                    .ok_or("Defer-Aktionsstelle fehlt im Funktionskatalog")?;
                if !self.defer_runtime {
                    self.defer_stack.push(id);
                    return Ok(());
                }
                let func = self.current_function.ok_or("Defer ohne Funktion")?;
                let slot = self.defer_frame.ok_or("Defer ohne Aktivierungs-Slot")?;
                let abi = defer_catalog::DeferAbi::declare(self.context, &self.module)?;
                let status = abi.emit_register(self.context, &self.builder, func, slot,
                    self.context.i32_type().const_int(id as u64, false))?;
                let ok = self.builder.build_int_compare(
                    inkwell::IntPredicate::EQ, status, self.context.i32_type().const_zero(), "defer.register.ok",
                ).map_err(|e| e.to_string())?;
                let success = self.context.append_basic_block(func, "defer.register.success");
                let failure = self.context.append_basic_block(func, "defer.register.failure");
                self.builder.build_conditional_branch(ok, success, failure).map_err(|e| e.to_string())?;
                self.builder.position_at_end(failure);
                // EMPTY ist bei push/new ungueltig; als unbekannten Status melden.
                let empty = self.builder.build_int_compare(
                    inkwell::IntPredicate::EQ, status, self.context.i32_type().const_int(1, false), "defer.register.empty",
                ).map_err(|e| e.to_string())?;
                let mapped = self.builder.build_select(empty, self.context.i32_type().const_int(6, false),
                    status, "defer.register.error_status").map_err(|e| e.to_string())?.into_int_value();
                let error = self.defer_status_error(&abi, mapped)?;
                let alloc = self.builder.build_int_compare(
                    inkwell::IntPredicate::EQ, status, self.context.i32_type().const_int(3, false), "defer.register.alloc",
                ).map_err(|e| e.to_string())?;
                let overflow = self.builder.build_int_compare(
                    inkwell::IntPredicate::EQ, status, self.context.i32_type().const_int(4, false), "defer.register.overflow",
                ).map_err(|e| e.to_string())?;
                let safe = self.builder.build_or(alloc, overflow, "defer.register.stack_valid").map_err(|e| e.to_string())?;
                let drain = self.context.append_basic_block(func, "defer.register.drain");
                let invalid = self.context.append_basic_block(func, "defer.register.invalid");
                self.builder.build_conditional_branch(safe, drain, invalid).map_err(|e| e.to_string())?;
                self.builder.position_at_end(drain);
                let (has, first) = self.drain_runtime_defers(Some(error))?;
                self.route_runtime_defer_error(has, first, None, true)?;
                self.builder.position_at_end(invalid);
                self.route_invalid_defer_stack(error)?;
                self.builder.position_at_end(success);
                Ok(())
            }
            Stmt::Guard { condition, else_body, .. } => {
                self.compile_guard(condition, else_body)
            }
            Stmt::UnsafeBlock { body, .. } => {
                // Rust-inspiriert: unsafe_context erlaubt gefaehrliche Operationen
                let prev = self.unsafe_context;
                self.unsafe_context = true;
                let result = body.iter().try_for_each(|s| self.compile_stmt(s));
                self.unsafe_context = prev;
                result
            }
            Stmt::Expression(expr, _) => {
                if matches!(expr, Expr::FunctionCall { name, .. } if name == "__moo_sperre_verlassen") {
                    // Nur vor einer abgeschlossenen Entsperr-Anweisung:
                    // kein noch aktiver Receiver einer aeusseren Expression.
                    // Compiler-Temps sind keine entkommenen Benutzeraliase.
                    let mut slots: Vec<_> = self.variables.iter()
                        .filter(|(n, _)| n.starts_with("__mcall_obj."))
                        .map(|(n, p)| (n.clone(), *p)).collect();
                    slots.sort_by(|a, b| a.0.cmp(&b.0));
                    let i64_t = self.context.i64_type();
                    let nichts = self.mv_type().const_named_struct(&[
                        i64_t.const_int(tag::NONE, false).into(), i64_t.const_zero().into()]);
                    for (_, slot) in slots {
                        let v = self.builder.build_load(self.mv_type(), slot, "mit_temp")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        self.call_rt_void(self.rt.moo_release, &[v.into()], "mit_temp_frei")?;
                        self.builder.build_store(slot, nichts).map_err(|e| format!("{e}"))?;
                    }
                }
                // Ergebnis wird nicht konsumiert → Expression-Temp freigeben,
                // sonst leakt z.B. eine nackte `d[k]`-Zeile den Return-Ref.
                let guarded = self.defer_runtime || self.defer_borrowed_handler.is_some();
                // P2-T77: reiner Aufruf als Statement ist eine Wurzel ohne
                // Besitztemporaere, Direktform wie bei zeige; der Kompilierpfad
                // (guarded/ungeguardet) bleibt unveraendert (Legacy-IR).
                let previous_direct = self.native_try_direct;
                self.native_try_direct = self.native_try_direct_root(expr);
                let compiled = if guarded {
                    self.compile_statement_expr(expr)
                } else {
                    self.compile_expr(expr)
                };
                self.native_try_direct = previous_direct;
                let val = compiled?;
                if guarded {
                    self.guard_local_expr_error(&[val])?;
                }
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_expr_stmt")?;
                Ok(())
            }
            Stmt::ParallelFor { var_name, iterable, body, .. } => {
                // Fallback: als normale For-Schleife kompilieren
                // EHRLICH: Echte Parallelisierung braucht thread-sichere Codegen
                self.compile_for(var_name, iterable, body)
            }
            Stmt::Precondition { condition, message, .. } | Stmt::Postcondition { condition, message, .. } => {
                let cond_val = self.compile_expr(condition)?;
                let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                    &[cond_val.into()], "contract").map_err(|e| format!("{e}"))?;
                let cond_bool = match is_true.try_as_basic_value() {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("truthy fehlgeschlagen".to_string()),
                };
                // Cond-Temp freigeben — wurde nur fuer truthy-Check genutzt.
                self.call_rt_void(self.rt.moo_release, &[cond_val.into()], "rel_contract_cond")?;
                let func = self.current_function.unwrap();
                let fail_bb = self.context.append_basic_block(func, "contract_fail");
                let ok_bb = self.context.append_basic_block(func, "contract_ok");
                self.builder.build_conditional_branch(cond_bool, ok_bb, fail_bb)
                    .map_err(|e| format!("{e}"))?;
                self.builder.position_at_end(fail_bb);
                let msg_ptr = self.make_global_str(message, "contract_msg")?;
                let msg_val = self.call_rt(self.rt.moo_string_new, &[msg_ptr.into()], "contract_str")?;
                self.call_rt_void(self.rt.moo_throw, &[msg_val.into()], "contract_throw")?;
                self.builder.build_unconditional_branch(ok_bb).map_err(|e| format!("{e}"))?;
                self.builder.position_at_end(ok_bb);
                Ok(())
            }
            Stmt::TestDef { name, body, .. } => {
                // Test-Framework: teste "name": body → generiert Funktion __test_<name>
                // und registriert den Namen fuer teste_alle()
                let fn_name = format!("__test_{}", name.replace(' ', "_").replace('"', ""));
                let mv = self.mv_type();
                let fn_type = mv.fn_type(&[], false);
                let function = self.module.add_function(&fn_name, fn_type, None);
                let entry = self.context.append_basic_block(function, "entry");

                let prev_fn = self.current_function;
                let prev_vars = self.variables.clone();
                let prev_block = self.builder.get_insert_block();
                let prev_defers = std::mem::take(&mut self.defer_stack);
                let prev_catalog = std::mem::take(&mut self.defer_catalog);
                let prev_defer_frame = self.defer_frame.take();
                let prev_defer_runtime = std::mem::replace(&mut self.defer_runtime, false);
                let prev_defer_borrowed = self.defer_borrowed_handler.take();
                self.defer_catalog = defer_catalog::DeferCatalog::collect(body)?;

                self.current_function = Some(function);
                self.builder.position_at_end(entry);
                self.variables.clear();
                self.init_defer_frame()?;

                // Wie Funktions-Locals sind Test-Locals beim Eintritt definiert,
                // auch wenn ein fruehes Cleanup sie vor ihrer Zuweisung liest.
                // Test-Assignments auf bestehende Globals bleiben global.
                let none_tag = self.context.i64_type().const_int(tag::NONE, false);
                let zero_data = self.context.i64_type().const_zero();
                let none = self.mv_type().const_named_struct(&[none_tag.into(), zero_data.into()]);
                for name in Self::collect_all_var_names(body) {
                    if self.globals.contains_key(&name) { continue; }
                    let slot = self.builder.build_alloca(self.mv_type(), &name)
                        .map_err(|e| format!("{e}"))?;
                    self.builder.build_store(slot, none).map_err(|e| format!("{e}"))?;
                    self.variables.insert(name, slot);
                }

                let test_rahmen = self.rahmen_betreten(RahmenArt::Sonstig);
                let test_defer_handler = self.defer_frame_begin()?;
                for s in body {
                    if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
                    self.compile_stmt(s)?;
                }

                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    // Defers lesen noch Locals; erst danach die Slots freigeben.
                    self.defer_frame_finish()?;
                    self.release_function_locals()?;
                    let bval = self.context.bool_type().const_int(1, false);
                    let result = self.call_rt(self.rt.moo_bool_fn, &[bval.into()], "pass")?;
                    self.builder.build_return(Some(&result)).map_err(|e| format!("{e}"))?;
                }

                self.defer_frame_end(test_defer_handler)?;
                self.rahmen_verlassen(test_rahmen);
                self.current_function = prev_fn;
                self.variables = prev_vars;
                self.defer_stack = prev_defers;
                self.defer_catalog = prev_catalog;
                self.defer_frame = prev_defer_frame;
                self.defer_runtime = prev_defer_runtime;
                self.defer_borrowed_handler = prev_defer_borrowed;
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }

                // Test-Name registrieren
                self.test_names.push((name.clone(), fn_name));
                Ok(())
            }
            Stmt::Expect(expr, _) => {
                // erwarte expr → wenn falsch, print Fehler + return false
                let val = self.compile_expr(expr)?;
                let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                    &[val.into()], "expect")
                    .map_err(|e| format!("{e}"))?
                    .try_as_basic_value();
                let cond_bool = match is_true {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("expect truthy fehlgeschlagen".to_string()),
                };
                // Truthiness liest den owning Ausdruck nur aus.
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_expect")?;

                let func = self.current_function.unwrap();
                let fail_bb = self.context.append_basic_block(func, "expect_fail");
                let ok_bb = self.context.append_basic_block(func, "expect_ok");

                self.builder.build_conditional_branch(cond_bool, ok_bb, fail_bb)
                    .map_err(|e| format!("{e}"))?;

                // Fail: print Fehlermeldung + return false
                self.builder.position_at_end(fail_bb);
                let msg = self.make_global_str("  FEHLGESCHLAGEN: Erwartung nicht erfuellt", "expect_msg")?;
                let msg_val = self.call_rt(self.rt.moo_string_new, &[msg.into()], "emsg")?;
                self.call_rt_void(self.rt.moo_print, &[msg_val.into()], "eprint")?;
                self.call_rt_void(self.rt.moo_release, &[msg_val.into()], "rel_expect_msg")?;
                self.defer_frame_finish()?;
                // main fuehrt Globals in variables; diese hier nicht freigeben.
                if func.get_name().to_str() != Ok("main") {
                    self.release_function_locals()?;
                }
                let bfalse = self.context.bool_type().const_int(0, false);
                let fail_val = self.call_rt(self.rt.moo_bool_fn, &[bfalse.into()], "fail")?;
                self.builder.build_return(Some(&fail_val)).map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(ok_bb);
                Ok(())
            }
            _ => Ok(()),
        };
        result?;
        // Auch verschachtelte Block-Statements muessen einen von der Runtime
        // gesetzten Fehler vor dem naechsten Statement weiterleiten.
        if guarded && self.builder.get_insert_block().is_some_and(|bb| bb.get_terminator().is_none()) {
            self.guard_local_expr_error(&[])?;
        }
        Ok(())
    }

    /// Test-Framework: teste_alle() — ruft alle registrierten Tests auf
    fn compile_run_tests(&mut self) -> Result<StructValue<'ctx>, String> {
        let func = self.current_function.unwrap();
        let i32_type = self.context.i32_type();

        // Zaehler: ok, fail
        let ok_ptr = self.builder.build_alloca(i32_type, "test_ok").map_err(|e| format!("{e}"))?;
        let fail_ptr = self.builder.build_alloca(i32_type, "test_fail").map_err(|e| format!("{e}"))?;
        self.builder.build_store(ok_ptr, i32_type.const_int(0, false)).map_err(|e| format!("{e}"))?;
        self.builder.build_store(fail_ptr, i32_type.const_int(0, false)).map_err(|e| format!("{e}"))?;

        // Header
        let header = self.make_global_str("\n=== moo Tests ===", "test_header")?;
        let header_val = self.call_rt(self.rt.moo_string_new, &[header.into()], "hdr")?;
        self.call_rt_void(self.rt.moo_print, &[header_val.into()], "print_hdr")?;

        let tests = self.test_names.clone();
        for (display_name, fn_name) in &tests {
            // Test-Name anzeigen
            let name_str = format!("  teste \"{display_name}\"...");
            let name_ptr = self.make_global_str(&name_str, "tname")?;
            let name_val = self.call_rt(self.rt.moo_string_new, &[name_ptr.into()], "tn")?;
            self.call_rt_void(self.rt.moo_print, &[name_val.into()], "print_tn")?;

            // Test aufrufen
            let test_fn = self.module.get_function(fn_name)
                .ok_or(format!("Test-Funktion '{fn_name}' nicht gefunden"))?;
            let result = self.call_rt(test_fn, &[], "test_result")?;

            // Ergebnis pruefen (truthy = bestanden)
            let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                &[result.into()], "test_pass")
                .map_err(|e| format!("{e}"))?
                .try_as_basic_value();
            let pass_bool = match is_true {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("test truthy fehlgeschlagen".to_string()),
            };

            let ok_bb = self.context.append_basic_block(func, "test_ok");
            let fail_bb = self.context.append_basic_block(func, "test_fail");
            let merge_bb = self.context.append_basic_block(func, "test_merge");

            self.builder.build_conditional_branch(pass_bool, ok_bb, fail_bb)
                .map_err(|e| format!("{e}"))?;

            // OK
            self.builder.position_at_end(ok_bb);
            let ok_msg = self.make_global_str("    OK", "ok_msg")?;
            let ok_val = self.call_rt(self.rt.moo_string_new, &[ok_msg.into()], "ok")?;
            self.call_rt_void(self.rt.moo_print, &[ok_val.into()], "print_ok")?;
            let cur_ok = self.builder.build_load(i32_type, ok_ptr, "cok")
                .map_err(|e| format!("{e}"))?.into_int_value();
            let new_ok = self.builder.build_int_add(cur_ok, i32_type.const_int(1, false), "nok")
                .map_err(|e| format!("{e}"))?;
            self.builder.build_store(ok_ptr, new_ok).map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

            // FAIL
            self.builder.position_at_end(fail_bb);
            let fail_msg = self.make_global_str("    FAIL", "fail_msg")?;
            let fail_val = self.call_rt(self.rt.moo_string_new, &[fail_msg.into()], "fail")?;
            self.call_rt_void(self.rt.moo_print, &[fail_val.into()], "print_fail")?;
            let cur_fail = self.builder.build_load(i32_type, fail_ptr, "cfail")
                .map_err(|e| format!("{e}"))?.into_int_value();
            let new_fail = self.builder.build_int_add(cur_fail, i32_type.const_int(1, false), "nfail")
                .map_err(|e| format!("{e}"))?;
            self.builder.build_store(fail_ptr, new_fail).map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(merge_bb);
        }

        // Zusammenfassung: "X Tests: Y OK, Z FAIL"
        let summary = self.make_global_str("=== Ergebnis ===", "summary")?;
        let summary_val = self.call_rt(self.rt.moo_string_new, &[summary.into()], "sum")?;
        self.call_rt_void(self.rt.moo_print, &[summary_val.into()], "print_sum")?;

        // Anzahl als MooValue ausgeben
        let final_ok = self.builder.build_load(i32_type, ok_ptr, "fok")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let final_fail = self.builder.build_load(i32_type, fail_ptr, "ffail")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let total = self.builder.build_int_add(final_ok, final_fail, "total")
            .map_err(|e| format!("{e}"))?;

        // Konvertiere zu f64 fuer moo_number
        let total_f = self.builder.build_unsigned_int_to_float(total, self.context.f64_type(), "totalf")
            .map_err(|e| format!("{e}"))?;
        let ok_f = self.builder.build_unsigned_int_to_float(final_ok, self.context.f64_type(), "okf")
            .map_err(|e| format!("{e}"))?;
        let fail_f = self.builder.build_unsigned_int_to_float(final_fail, self.context.f64_type(), "failf")
            .map_err(|e| format!("{e}"))?;

        let total_mv = self.call_rt(self.rt.moo_number, &[total_f.into()], "total_mv")?;
        let ok_mv = self.call_rt(self.rt.moo_number, &[ok_f.into()], "ok_mv")?;
        let fail_mv = self.call_rt(self.rt.moo_number, &[fail_f.into()], "fail_mv")?;

        // "X Tests: Y OK, Z FAIL" zusammenbauen
        let total_s = self.call_rt(self.rt.moo_to_string, &[total_mv.into()], "ts")?;
        let ok_s = self.call_rt(self.rt.moo_to_string, &[ok_mv.into()], "os")?;
        let fail_s = self.call_rt(self.rt.moo_to_string, &[fail_mv.into()], "fs")?;
        let t1 = self.make_global_str(" Tests: ", "t1")?;
        let t1_val = self.call_rt(self.rt.moo_string_new, &[t1.into()], "t1v")?;
        let t2 = self.make_global_str(" OK, ", "t2")?;
        let t2_val = self.call_rt(self.rt.moo_string_new, &[t2.into()], "t2v")?;
        let t3 = self.make_global_str(" FAIL", "t3")?;
        let t3_val = self.call_rt(self.rt.moo_string_new, &[t3.into()], "t3v")?;

        let s1 = self.call_rt(self.rt.moo_string_concat, &[total_s.into(), t1_val.into()], "s1")?;
        let s2 = self.call_rt(self.rt.moo_string_concat, &[s1.into(), ok_s.into()], "s2")?;
        let s3 = self.call_rt(self.rt.moo_string_concat, &[s2.into(), t2_val.into()], "s3")?;
        let s4 = self.call_rt(self.rt.moo_string_concat, &[s3.into(), fail_s.into()], "s4")?;
        let s5 = self.call_rt(self.rt.moo_string_concat, &[s4.into(), t3_val.into()], "s5")?;
        self.call_rt_void(self.rt.moo_print, &[s5.into()], "print_result")?;

        // P2-T117: Ein FAIL darf nicht still Exit 0 ergeben. teste_alle() merkt
        // FAIL in einem internen Global (kein sofortiger Abbruch: Programme
        // rufen teste_alle() auch mehrfach); main liefert am Ende Exit-Code 1.
        let merker = self.tests_fehlgeschlagen_global();
        let hat_fail = self.builder.build_int_compare(inkwell::IntPredicate::NE,
            final_fail, i32_type.const_zero(), "tests.hat_fail")
            .map_err(|e| format!("{e}"))?;
        let bisher = self.builder.build_load(i32_type, merker, "tests.bisher")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let neu = self.builder.build_select(hat_fail, i32_type.const_int(1, false), bisher, "tests.merker")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(merker, neu).map_err(|e| format!("{e}"))?;

        self.call_rt(self.rt.moo_none, &[], "none")
    }

    /// P2-T117: interner Merker "ein teste_alle()-Test meldete FAIL" (i32, 0/1).
    /// Nur Programme mit teste_alle() legen ihn an; main gibt ihn als Exit-Code.
    fn tests_fehlgeschlagen_global(&self) -> PointerValue<'ctx> {
        if let Some(g) = self.module.get_global("__moo_tests_fehlgeschlagen") {
            return g.as_pointer_value();
        }
        let i32_type = self.context.i32_type();
        let g = self.module.add_global(i32_type, None, "__moo_tests_fehlgeschlagen");
        g.set_initializer(&i32_type.const_zero());
        g.set_linkage(inkwell::module::Linkage::Internal);
        g.as_pointer_value()
    }

    fn compile_guard(&mut self, condition: &Expr, else_body: &[Stmt]) -> Result<(), String> {
        // Swift guard: wenn Bedingung FALSCH → else_body (muss Return enthalten)
        let cond_val = self.compile_statement_expr(condition)?;
        self.guard_local_expr_error(&[cond_val])?;
        let func = self.current_function.unwrap();

        let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
            &[cond_val.into()], "guard_truthy")
            .map_err(|e| format!("{e}"))?
            .try_as_basic_value();
        let cond_bool = match is_true {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("guard truthy fehlgeschlagen".to_string()),
        };

        // moo_is_truthy leiht den Bedingungswert; der Temp bleibt owning.
        self.call_rt_void(self.rt.moo_release, &[cond_val.into()], "rel_guard_cond")?;

        let else_bb = self.context.append_basic_block(func, "guard_else");
        let cont_bb = self.context.append_basic_block(func, "guard_cont");

        // Wenn true → weiter, wenn false → else_body
        self.builder.build_conditional_branch(cond_bool, cont_bb, else_bb)
            .map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(else_bb);
        for stmt in else_body {
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
            self.compile_stmt(stmt)?;
        }
        // Sicherheits-Return falls else_body keinen hat
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.compile_return(&None)?;
        }

        self.builder.position_at_end(cont_bb);
        Ok(())
    }

    fn compile_if(&mut self, condition: &Expr, body: &[Stmt], else_body: &[Stmt]) -> Result<(), String> {
        // P2-T77: Bedingung ist Statement-Wurzel ohne Besitztemporaere davor,
        // daher Direktform fuer native Argumentadapter (native_try_direct_root).
        let cond_val = self.compile_statement_expr_direct(condition)?;
        self.guard_local_expr_error(&[cond_val])?;
        let func = self.current_function.unwrap();

        let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
            &[cond_val.into()], "truthy")
            .map_err(|e| format!("{e}"))?
            .try_as_basic_value();
        let cond_bool = match is_true {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_is_truthy hat keinen Wert zurueckgegeben".to_string()),
        };

        // Cond-Temp freigeben — wurde nur fuer truthy-Check genutzt.
        self.call_rt_void(self.rt.moo_release, &[cond_val.into()], "rel_cond")?;

        let then_bb = self.context.append_basic_block(func, "then");
        let else_bb = self.context.append_basic_block(func, "else");
        let merge_bb = self.context.append_basic_block(func, "merge");

        self.builder.build_conditional_branch(cond_bool, then_bb, else_bb)
            .map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(then_bb);
        for stmt in body {
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
            self.compile_stmt(stmt)?;
        }
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(else_bb);
        for stmt in else_body {
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
            self.compile_stmt(stmt)?;
        }
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(merge_bb);
        Ok(())
    }

    /// P2-T101: weiter/abbrechen springen aus einem verschachtelten Block, bevor
    /// der Pruefpunkt des umgebenden versuche: (nach dem Top-Level-Statement)
    /// laeuft. Ein anstehender Fehler (z. B. aus `route()` direkt davor im selben
    /// wenn-Zweig) waere sonst verloren: moo_try_leave loescht ihn bei Tiefe 0,
    /// oder die Schleife liefe mit gesetztem Flag weiter. Wie compile_return
    /// (ret_check): anstehend -> innerster lokaler Catch; ohne lokalen Catch
    /// entsteht kein zusaetzlicher Code.
    fn fehler_vor_schleifensprung(&self) -> Result<(), String> {
        if self.bare_mode { return Ok(()); }
        let func = self.current_function.ok_or("Schleifensprung ohne Funktion")?;
        let Some(catch_bb) = self.try_stack.iter().rev().copied()
            .find(|bb| bb.get_parent() == Some(func) && Some(*bb) != self.defer_borrowed_handler)
        else {
            return Ok(());
        };
        let pending = self.builder.build_call(self.rt.moo_try_check, &[], "sprung_check")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let pending = match pending {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_check fehlgeschlagen".into()),
        };
        let is_error = self.builder.build_int_compare(
            inkwell::IntPredicate::NE, pending, self.context.i32_type().const_zero(), "sprung_error",
        ).map_err(|e| e.to_string())?;
        let ok_bb = self.context.append_basic_block(func, "sprung_ok");
        self.builder.build_conditional_branch(is_error, catch_bb, ok_bb)
            .map_err(|e| e.to_string())?;
        self.builder.position_at_end(ok_bb);
        Ok(())
    }

    /// Verlaesst nur Try-Handler, deren Geltungsbereich der Loop-Sprung verlaesst.
    fn leave_loop_handlers(&self, target_depth: usize) -> Result<(), String> {
        let func = self.current_function.ok_or("Schleife ohne Funktion")?;
        let depth = self.try_stack.iter().filter(|bb| bb.get_parent() == Some(func)).count();
        let leaving = depth.checked_sub(target_depth).ok_or("Ungueltige Try-Tiefe am Schleifenziel")?;
        for _ in 0..leaving {
            self.call_rt_void(self.rt.moo_try_leave, &[], "loop_try_leave")?;
        }
        Ok(())
    }

    fn compile_while(&mut self, condition: &Expr, body: &[Stmt]) -> Result<(), String> {
        let func = self.current_function.unwrap();

        let cond_bb = self.context.append_basic_block(func, "while_cond");
        let body_bb = self.context.append_basic_block(func, "while_body");
        let after_bb = self.context.append_basic_block(func, "while_after");

        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(cond_bb);
        let cond_val = self.compile_statement_expr(condition)?;
        self.guard_local_expr_error(&[cond_val])?;
        let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
            &[cond_val.into()], "truthy")
            .map_err(|e| format!("{e}"))?
            .try_as_basic_value();
        let cond_bool = match is_true {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("truthy fehlgeschlagen".to_string()),
        };
        // Cond-Temp freigeben — jede Iteration, im cond_bb-Block.
        self.call_rt_void(self.rt.moo_release, &[cond_val.into()], "rel_while_cond")?;
        self.builder.build_conditional_branch(cond_bool, body_bb, after_bb)
            .map_err(|e| format!("{e}"))?;

        let try_depth = self.try_stack.iter().filter(|bb| bb.get_parent() == Some(func)).count();
        self.loop_stack.push((cond_bb, after_bb, try_depth));
        self.builder.position_at_end(body_bb);
        for stmt in body {
            // Nach Break/Continue ist der Block terminiert — keine weiteren Statements
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() {
                break;
            }
            self.compile_stmt(stmt)?;
        }
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;
        }
        self.loop_stack.pop();

        self.builder.position_at_end(after_bb);
        Ok(())
    }

    fn compile_for(&mut self, var_name: &str, iterable: &Expr, body: &[Stmt]) -> Result<(), String> {
        let func = self.current_function.unwrap();
        let list_val = self.compile_statement_expr(iterable)?;
        self.guard_local_expr_error(&[list_val])?;

        // Laenge holen
        let len_result = self.call_rt_abi(self.rt.moo_list_iter_len,
            &[list_val.into()], "len")
            .map_err(|e| format!("{e}"))?;
        let len = match len_result.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("iter_len fehlgeschlagen".to_string()),
        };

        // Index-Variable
        let i32_type = self.context.i32_type();
        // G1-LEAK-Nachzuegler (REG-G1-Gate-Fund): alloca MUSS in den Entry-Block,
        // sonst waechst der Stack pro Iteration wenn dieser for-Loop selbst in
        // einer Schleife liegt (für-in-solange: SEGV bei ~1M Iterationen).
        let idx_ptr = self.entry_alloca(i32_type, "for_idx")?;
        self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
            .map_err(|e| format!("{e}"))?;

        // Wir brauchen die Liste gespeichert, damit wir sie im Loop-Body laden koennen.
        // Iterable-Ptr in self.variables mit synthetischem Namen ablegen, damit
        // release_function_locals() ihn auch bei early-return aus dem Loop-Body
        // erreicht (sonst leak pro Early-Return-Call).
        // Alloca MUSS im Entry-Block liegen, damit sie alle Uses dominiert.
        let iter_slot_name = format!("__for_iter_{}", self.for_iter_counter);
        self.for_iter_counter += 1;
        let entry_bb = func.get_first_basic_block().unwrap();
        let current_block = self.builder.get_insert_block().unwrap();
        if let Some(first_instr) = entry_bb.get_first_instruction() {
            self.builder.position_before(&first_instr);
        } else {
            self.builder.position_at_end(entry_bb);
        }
        let list_ptr = self.builder.build_alloca(self.mv_type(), &iter_slot_name)
            .map_err(|e| format!("{e}"))?;
        // Mit MOO_NONE initialisieren damit release_function_locals() vor
        // dem Store in diesem Slot (falls Code vor dem for-Loop early-returnt)
        // harmlos wirkt.
        let none_init = self.context.i64_type().const_int(tag::NONE, false);
        let zero_data = self.context.i64_type().const_int(0, false);
        let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
        self.builder.build_store(list_ptr, none_val).map_err(|e| format!("{e}"))?;
        self.builder.position_at_end(current_block);
        self.builder.build_store(list_ptr, list_val).map_err(|e| format!("{e}"))?;
        self.variables.insert(iter_slot_name.clone(), list_ptr);

        let cond_bb = self.context.append_basic_block(func, "for_cond");
        let body_bb = self.context.append_basic_block(func, "for_body");
        let incr_bb = self.context.append_basic_block(func, "for_incr");
        let after_bb = self.context.append_basic_block(func, "for_after");

        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        // Bedingung: idx < len
        self.builder.position_at_end(cond_bb);
        let current_idx = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let cond = self.builder.build_int_compare(
            inkwell::IntPredicate::SLT, current_idx, len, "for_cmp")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_conditional_branch(cond, body_bb, after_bb)
            .map_err(|e| format!("{e}"))?;

        // Body — Continue springt zu incr_bb (nicht cond_bb!), Break zu after_bb
        let try_depth = self.try_stack.iter().filter(|bb| bb.get_parent() == Some(func)).count();
        self.loop_stack.push((incr_bb, after_bb, try_depth));
        self.builder.position_at_end(body_bb);

        // Element holen
        let list_loaded = self.builder.build_load(self.mv_type(), list_ptr, "list")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let element = self.call_rt(self.rt.moo_list_iter_get,
            &[list_loaded.into(), idx_loaded.into()], "elem")?;
        self.store_var(var_name, element)?;

        for stmt in body {
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
            self.compile_stmt(stmt)?;
        }

        // Body → Inkrement
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(incr_bb).map_err(|e| format!("{e}"))?;
        }

        // Inkrement-Block: idx++ → zurück zu cond
        self.builder.position_at_end(incr_bb);
        let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.loop_stack.pop();

        self.builder.position_at_end(after_bb);
        // Iterable-Temp (Liste) nach der Schleife freigeben. list_val
        // wurde mit compile_expr (+1 owning) produziert und in list_ptr
        // gespeichert; ohne release bliebe pro for-Schleife ein Leak.
        let list_end = self.builder.build_load(self.mv_type(), list_ptr, "list_end")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        self.call_rt_void(self.rt.moo_release, &[list_end.into()], "rel_for_list")?;
        // Slot-Wert auf MOO_NONE setzen, damit ein spaeteres
        // release_function_locals() nicht doppelt released.
        let none_init = self.context.i64_type().const_int(tag::NONE, false);
        let zero_data = self.context.i64_type().const_int(0, false);
        let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
        self.builder.build_store(list_ptr, none_val).map_err(|e| format!("{e}"))?;
        Ok(())
    }

    fn compile_function_def(&mut self, name: &str, params: &[String], defaults: &[Option<Expr>], body: &[Stmt], decorators: &[crate::ast::Decorator]) -> Result<(), String> {
        let function = self.module.get_function(name)
            .ok_or(format!("Funktion '{name}' nicht forward-declared"))?;

        // Duplikat-Guard: wenn Funktion schon kompiliert wurde (hat Basic Blocks), ueberspringen
        if function.count_basic_blocks() > 0 {
            return Ok(());
        }

        // Der MooValue-Koerper darf aus einer ISR keine Runtime (SSE) betreten.
        if let Some(d) = decorators.iter().find(|d| d.name == "interrupt" || d.name == "unterbrechung") {
            return Err(format!(
                "@{} an dynamischen Funktionen ist unzulässig; \
                 verwende @nativ @unterbrechung(vektor) mit (rahmen: ptr[u64][, fehlercode: u64]) -> void und unsicher:-Hardwarekoerper \
                 (x86_64) bzw. ein @nativ @nackt-Trampolin (aarch64)",
                d.name
            ));
        }

        // P011-A1: Layout-Decorators — @benutzt/@used (+ @einstieg/@entry: implizit benutzt),
        // @sektion/@section("name"), @ausrichten/@align(n). Unbekannte Decorators bleiben
        // wie bisher ignoriert (Vorwaertskompatibilitaet).
        for d in decorators {
            match d.name.as_str() {
                "benutzt" | "used" | "einstieg" | "entry" => {
                    if !d.args.is_empty() {
                        return Err(format!("@{} erwartet keine Argumente", d.name));
                    }
                    self.used_globals.push(function.as_global_value().as_pointer_value());
                }
                "sektion" | "section" => match d.args.as_slice() {
                    [crate::ast::DecoratorArg::Text(s)] => {
                        if s == ".multiboot2" {
                            return Err(format!(
                                "@{}(\".multiboot2\") ist nur fuer eine @nativ konstante (Multiboot2-Kopf) zulaessig, nicht fuer Funktionen",
                                d.name
                            ));
                        }
                        let section = self.target_section_name(s, true)?;
                        function.as_global_value().set_section(Some(&section));
                    }
                    _ => {
                        return Err(format!(
                            "@{} erwartet genau ein Text-Argument, z.B. @sektion(\".boot\")",
                            d.name
                        ));
                    }
                },
                "ausrichten" | "align" => match d.args.as_slice() {
                    [crate::ast::DecoratorArg::Zahl(n)] => {
                        let a = *n as u64;
                        if *n <= 0.0 || (*n != a as f64) || (a & a.wrapping_sub(1)) != 0 {
                            return Err(format!(
                                "@{} erwartet eine positive Zweierpotenz (z.B. 8, 16, 4096), bekommen: {}",
                                d.name, n
                            ));
                        }
                        function.as_global_value().set_alignment(a as u32);
                    }
                    _ => return Err(format!("@{} erwartet genau ein Zahl-Argument", d.name)),
                },
                "aufrufkonvention" | "calling_convention" => {
                    return Err(format!(
                        "@{} gilt nur für @nativ-Funktionen und extern C; dynamische Moo-Funktionen haben die MooValue-ABI",
                        d.name
                    ));
                }
                "nackt" | "naked" => {
                    return Err(format!(
                        "@{} gilt nur für @nativ-Funktionen mit unsicher: assembler(...); dynamische Moo-Funktionen brauchen Prolog und Runtime",
                        d.name
                    ));
                }
                _ => {}
            }
        }

        let entry = self.context.append_basic_block(function, "entry");

        let prev_fn = self.current_function;
        let prev_vars = self.variables.clone();
        let prev_block = self.builder.get_insert_block();
        let prev_defers = std::mem::take(&mut self.defer_stack);
        let prev_catalog = std::mem::take(&mut self.defer_catalog);
        let prev_defer_frame = self.defer_frame.take();
                let prev_defer_runtime = std::mem::replace(&mut self.defer_runtime, false);
                let prev_defer_borrowed = self.defer_borrowed_handler.take();
        self.defer_catalog = defer_catalog::DeferCatalog::collect(body)?;

        self.current_function = Some(function);
        self.builder.position_at_end(entry);
        self.variables.clear();
        self.init_defer_frame()?;
        // Eigene Registrierungen gehoeren zur Aktivierung, unabhaengig davon,
        // ob der Koerper native, indirekte oder externe Aufrufe enthaelt. Der
        // Callgraph bestimmt nur weiter unten die geliehenen Callee-Guards.
        self.defer_runtime = !self.bare_mode && !self.profiling
            && !self.defer_catalog.actions().is_empty();
        // P2-T82: Benannte Funktionen mit hosted fangbaren @nativ-Aufrufen erhalten
        // einen Pruefpunkt-Ausgang (native_guard_exit_block), damit Statement-
        // und Operanden-Guards auch ohne eigenes versuche: greifen: nach einem
        // uebersprungenen Aufruf (nichts, Flag gesetzt) kehrt die Funktion wie
        // bei der Direktform mit nichts zurueck statt Folgeoperationen und
        // Seiteneffekte auszufuehren (Erstfehler-Verlust, z.B. 10 / nichts).
        // Nicht bei aufraeumen-Aktionen (statische Defers liefen sonst ausserhalb
        // ihrer Registrierungsreihenfolge), nicht im Defer-Frame (eigener
        // Handler), nicht bare/profiling, nur Top-Level-Funktionen.
        // P2-T85 E1/A1: benannte Top-Level-Funktion = Standardrahmen; kompletter
        // Rahmenzustand gesichert und neutralisiert (auch skip/direct, stmt-Guards).
        let rahmen = self.rahmen_betreten(
            if self.dyn_named_functions.contains(name) { RahmenArt::Funktion } else { RahmenArt::Sonstig });
        self.native_guard_enabled = self.dyn_named_functions.contains(name) && {
            let mut calls = std::collections::HashSet::new();
            Self::collect_defer_calls(body, &mut calls);
            self.native_guard_fuer(&calls)
        };

        for (i, param_name) in params.iter().enumerate() {
            let alloca = self.builder.build_alloca(self.mv_type(), param_name)
                .map_err(|e| format!("{e}"))?;
            let param_val = function.get_nth_param(i as u32).unwrap();
            self.builder.build_store(alloca, param_val).map_err(|e| format!("{e}"))?;
            self.variables.insert(param_name.clone(), alloca);

            // Default-Parameter: wenn der Wert none ist, den Default einsetzen
            if let Some(Some(default_expr)) = defaults.get(i) {
                let current_val = self.load_var(param_name)?;
                // Pruefe ob tag == MOO_NONE (tag ist das erste i64 Feld)
                let tag = self.builder.build_extract_value(current_val, 0, "tag")
                    .map_err(|e| format!("{e}"))?;
                let is_none = self.builder.build_int_compare(
                    inkwell::IntPredicate::EQ,
                    tag.into_int_value(),
                    self.context.i64_type().const_int(tag::NONE, false), // MOO_NONE
                    "is_none",
                ).map_err(|e| format!("{e}"))?;

                let default_bb = self.context.append_basic_block(function, "default");
                let continue_bb = self.context.append_basic_block(function, "no_default");
                self.builder.build_conditional_branch(is_none, default_bb, continue_bb)
                    .map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(default_bb);
                let default_val = self.compile_expr(default_expr)?;
                self.store_var(param_name, default_val)?;
                self.builder.build_unconditional_branch(continue_bb)
                    .map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(continue_bb);
            }
        }

        // Pre-Allocation lokaler Variablen — mit Counter-Pattern-Erkennung:
        // Wenn der Name eine globale Variable ist UND im Body als
        // 'X = X + ...' / 'X += ...' verwendet wird, ist das ein bewusster
        // Global-Update — KEINE lokale Alloca anlegen, store_var faellt auf
        // das Global zurueck. Alle anderen Faelle (lokale Loop-Counter,
        // bare write ohne Selbst-Referenz) bekommen eine lokale Alloca um
        // Global-Pollution zu verhindern.
        let none_init = self.context.i64_type().const_int(tag::NONE, false);
        let zero_data = self.context.i64_type().const_int(0, false);
        let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
        let local_names = Self::collect_all_var_names(body);
        // ERDE-13: `global x` auf Funktionsebene -> keine Local-Alloca, store_var
        // schreibt in das Top-Level-Global.
        let global_decls: Vec<String> = body.iter()
            .filter_map(|s| if let Stmt::Global(names, _) = s { Some(names.clone()) } else { None })
            .flatten()
            .collect();
        for g in &global_decls {
            if !self.globals.contains_key(g) {
                return Err(format!(
                    "Funktion '{name}': 'global {g}' — es gibt keine Top-Level-Variable '{g}' (zuerst ausserhalb der Funktion mit 'setze {g} auf ...' anlegen)"
                ));
            }
        }
        for n in &local_names {
            if self.variables.contains_key(n.as_str()) { continue; }
            if global_decls.iter().any(|g| g == n) { continue; }
            // Counter-Pattern auf Global → kein local-shadow
            if self.globals.contains_key(n) {
                match Self::first_use_is_local_write(body, n) {
                    Some(true) => {}
                    _ => continue,
                }
            }
            let alloca = self.builder.build_alloca(self.mv_type(), n)
                .map_err(|e| format!("{e}"))?;
            self.builder.build_store(alloca, none_val).map_err(|e| format!("{e}"))?;
            self.variables.insert(n.clone(), alloca);
        }

        // Profiling: Funktionsname am Anfang registrieren
        if self.profiling {
            let name_ptr = self.make_global_str(name, "prof_name")?;
            let name_val = self.call_rt(self.rt.moo_string_new, &[name_ptr.into()], "prof_str")?;
            self.call_rt_void(self.rt.moo_profile_enter, &[name_val.into()], "prof_enter")?;
        }

        // Defer-Frames besitzen einen Runtime-Handler. Erreichbare Callees
        // ohne Defers borgen nur die Fehlerkante und lassen die Runtime-Tiefe
        // des Aufrufers unangetastet.
        let borrowed = !self.defer_runtime && self.defer_guard_targets.contains(name)
            && self.runtime_defer_scope_supported(body);
        let defer_frame_error = if self.defer_runtime || borrowed {
            if self.defer_runtime {
                self.call_rt_void(self.rt.moo_try_enter, &[], "defer.frame.enter")?;
            }
            let handler = self.context.append_basic_block(function, "defer.frame.error");
            self.try_stack.push(handler);
            if borrowed { self.defer_borrowed_handler = Some(handler); }
            // Bereits gesetzten Aufruferfehler vor jedem Frame-Seiteneffekt beachten.
            self.guard_local_expr_error(&[])?;
            Some(handler)
        } else {
            None
        };
        for stmt in body {
            if defer_frame_error.is_some() && self.builder.get_insert_block().unwrap().get_terminator().is_some() {
                break;
            }
            self.compile_stmt(stmt)?;
            if defer_frame_error.is_some() && self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                self.guard_local_expr_error(&[])?;
            }
        }

        // Default return none — Defers + Profiling vor implizitem Return
        let current_bb = self.builder.get_insert_block().unwrap();
        if current_bb.get_terminator().is_none() {
            if self.profiling {
                let name_ptr = self.make_global_str(name, "prof_name")?;
                let name_val = self.call_rt(self.rt.moo_string_new, &[name_ptr.into()], "prof_str")?;
                self.call_rt_void(self.rt.moo_profile_exit, &[name_val.into()], "prof_exit")?;
            }
            if self.defer_runtime {
                let (has, first) = self.drain_runtime_defers(None)?;
                self.route_runtime_defer_error(has, first, None, false)?;
                self.call_rt_void(self.rt.moo_try_leave, &[], "defer.frame.leave")?;
            } else {
                self.emit_defers()?;
            }
            // Function-Exit-Cleanup: Locals freigeben bevor implicit return.
            self.release_function_locals()?;
            let returns_void = function.get_type().get_return_type().is_none();
            if returns_void {
                self.builder.build_return(None).map_err(|e| format!("{e}"))?;
            } else {
                let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
                self.builder.build_return(Some(&none_val)).map_err(|e| format!("{e}"))?;
            }
        }

        if let Some(handler) = defer_frame_error {
            self.try_stack.pop();
            self.builder.position_at_end(handler);
            if borrowed {
                // Flag und Fehler bleiben beim Aufrufer; kein enter/leave/take.
                self.defer_borrowed_handler = None;
                self.release_function_locals()?;
                if function.get_type().get_return_type().is_none() {
                    self.builder.build_return(None).map_err(|e| format!("{e}"))?;
                } else {
                    let none = self.call_rt(self.rt.moo_none, &[], "defer.borrowed.return")?;
                    self.builder.build_return(Some(&none)).map_err(|e| format!("{e}"))?;
                }
            } else {
                let take = self.rt.declare_try_take_error(self.context, &self.module, self.runtime_windows_abi);
                let error = self.call_rt(take, &[], "defer.frame.take_error")?;
                self.call_rt_void(self.rt.moo_try_leave, &[], "defer.frame.error_leave")?;
                let (has, first) = self.drain_runtime_defers(Some(error))?;
                self.route_runtime_defer_error(has, first, None, true)?;
            }
        }

        self.current_function = prev_fn;
        self.variables = prev_vars;
        self.defer_stack = prev_defers;
        self.defer_catalog = prev_catalog;
        self.defer_frame = prev_defer_frame;
                self.defer_runtime = prev_defer_runtime;
                self.defer_borrowed_handler = prev_defer_borrowed;
        self.rahmen_verlassen(rahmen);
        if let Some(bb) = prev_block {
            self.builder.position_at_end(bb);
        }

        Ok(())
    }

    // Fuer einen Runtime-Drain: erstes Error-Value behalten, spaetere freigeben.
    fn record_defer_error(
        &self, error: StructValue<'ctx>, first: PointerValue<'ctx>,
        has_first: PointerValue<'ctx>, next: inkwell::basic_block::BasicBlock<'ctx>,
    ) -> Result<(), String> {
        let func = self.current_function.ok_or("Defer-Fehler ohne Funktion")?;
        let already = self.builder.build_load(self.context.bool_type(), has_first, "defer.already")
            .map_err(|e| e.to_string())?.into_int_value();
        let discard = self.context.append_basic_block(func, "defer.discard");
        let save = self.context.append_basic_block(func, "defer.save");
        self.builder.build_conditional_branch(already, discard, save).map_err(|e| e.to_string())?;
        self.builder.position_at_end(discard);
        self.call_rt_void(self.rt.moo_release, &[error.into()], "defer.release_later")?;
        self.builder.build_unconditional_branch(next).map_err(|e| e.to_string())?;
        self.builder.position_at_end(save);
        self.builder.build_store(first, error).map_err(|e| e.to_string())?;
        self.builder.build_store(has_first, self.context.bool_type().const_int(1, false))
            .map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(next).map_err(|e| e.to_string())?;
        Ok(())
    }

    // Die Status-API darf bei gueltigem out und einem Fehlerstatus nicht
    // fehlschlagen. Ein ABI-Vertragsbruch terminiert kontrolliert per Trap.
    fn defer_status_error(
        &self, abi: &defer_catalog::DeferAbi<'ctx>, status: inkwell::values::IntValue<'ctx>,
    ) -> Result<StructValue<'ctx>, String> {
        let func = self.current_function.ok_or("Defer-Status ohne Funktion")?;
        let out = self.entry_alloca(self.mv_type(), "defer.status.error")?;
        let result = abi.emit_status_value(&self.builder, status, out)?;
        let ok = self.builder.build_int_compare(
            inkwell::IntPredicate::EQ, result, self.context.i32_type().const_zero(), "defer.status.ok",
        ).map_err(|e| e.to_string())?;
        let valid = self.context.append_basic_block(func, "defer.status.valid");
        let invalid = self.context.append_basic_block(func, "defer.status.invalid");
        self.builder.build_conditional_branch(ok, valid, invalid).map_err(|e| e.to_string())?;
        self.builder.position_at_end(invalid);
        let trap = self.module.get_function("llvm.trap").unwrap_or_else(||
            self.module.add_function("llvm.trap", self.context.void_type().fn_type(&[], false), None));
        self.builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
        self.builder.build_unreachable().map_err(|e| e.to_string())?;
        self.builder.position_at_end(valid);
        Ok(self.builder.build_load(self.mv_type(), out, "defer.status.value")
            .map_err(|e| e.to_string())?.into_struct_value())
    }

    // Noch nicht an Stmt::Defer/Exit angeschlossen. Drain liefert einen
    // owning Erstfehler und ein separates Flag; der Exit entscheidet ueber
    // Catch, Handler-Tiefe, Return-Wert und Propagation.
    #[allow(dead_code)]
    fn drain_runtime_defers(
        &mut self, injected: Option<StructValue<'ctx>>,
    ) -> Result<(inkwell::values::IntValue<'ctx>, PointerValue<'ctx>), String> {
        let func = self.current_function.ok_or("Defer-Drain ohne Funktion")?;
        let slot = self.defer_frame.ok_or("Defer-Drain ohne Aktivierungs-Slot")?;
        let actions = self.defer_catalog.actions().to_vec();
        if !actions.iter().all(|expr| self.recursive_statement_expr_supported(expr)) {
            return Err("Defer-Drain: Ausdruck noch nicht vollstaendig instrumentiert".into());
        }
        let abi = defer_catalog::DeferAbi::declare(self.context, &self.module)?;
        let first = self.entry_alloca(self.mv_type(), "defer.first")?;
        let has_first = self.entry_alloca(self.context.bool_type(), "defer.has_first")?;
        let can_free = self.entry_alloca(self.context.bool_type(), "defer.can_free")?;
        let action_id = self.entry_alloca(self.context.i32_type(), "defer.action_id")?;
        let none = self.mv_type().const_named_struct(&[
            self.context.i64_type().const_int(tag::NONE, false).into(),
            self.context.i64_type().const_zero().into(),
        ]);
        self.builder.build_store(first, injected.unwrap_or(none)).map_err(|e| e.to_string())?;
        self.builder.build_store(has_first, self.context.bool_type().const_int(injected.is_some() as u64, false))
            .map_err(|e| e.to_string())?;
        self.builder.build_store(can_free, self.context.bool_type().const_int(1, false))
            .map_err(|e| e.to_string())?;
        let take = self.rt.declare_try_take_error(self.context, &self.module, self.runtime_windows_abi);
        let pending = self.builder.build_call(self.rt.moo_try_check, &[], "defer.pending")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let pending = match pending {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_check fehlgeschlagen".into()),
        };
        let pending = self.builder.build_int_compare(
            inkwell::IntPredicate::NE, pending, self.context.i32_type().const_zero(), "defer.pending.set",
        ).map_err(|e| e.to_string())?;
        let save_prior = self.context.append_basic_block(func, "defer.prior");
        let begin = self.context.append_basic_block(func, "defer.begin");
        self.builder.build_conditional_branch(pending, save_prior, begin).map_err(|e| e.to_string())?;
        self.builder.position_at_end(save_prior);
        let prior = self.call_rt(take, &[], "defer.prior.error")?;
        if let Some(error) = injected {
            self.call_rt_void(self.rt.moo_release, &[error.into()], "defer.release_injected")?;
        }
        self.builder.build_store(first, prior).map_err(|e| e.to_string())?;
        self.builder.build_store(has_first, self.context.bool_type().const_int(1, false))
            .map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(begin).map_err(|e| e.to_string())?;
        self.builder.position_at_end(begin);
        self.call_rt_void(self.rt.moo_try_enter, &[], "defer.enter")?;
        let loop_bb = self.context.append_basic_block(func, "defer.pop.loop");
        let pop_ok = self.context.append_basic_block(func, "defer.pop.ok");
        let pop_error = self.context.append_basic_block(func, "defer.pop.error");
        let finish = self.context.append_basic_block(func, "defer.finish");
        self.builder.build_unconditional_branch(loop_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(loop_bb);
        let status = abi.emit_pop_or_empty(self.context, &self.builder, func, slot, action_id)?;
        self.builder.build_switch(status, pop_error, &[
            (self.context.i32_type().const_int(0, false), pop_ok),
            (self.context.i32_type().const_int(1, false), finish),
        ]).map_err(|e| e.to_string())?;
        self.builder.position_at_end(pop_error);
        self.builder.build_store(can_free, self.context.bool_type().const_zero()).map_err(|e| e.to_string())?;
        let error = self.defer_status_error(&abi, status)?;
        self.record_defer_error(error, first, has_first, finish)?;

        self.builder.position_at_end(pop_ok);
        let id = self.builder.build_load(self.context.i32_type(), action_id, "defer.id")
            .map_err(|e| e.to_string())?.into_int_value();
        let unknown = self.context.append_basic_block(func, "defer.unknown_action");
        let cases: Vec<_> = actions.iter().enumerate().map(|(i, _)|
            (self.context.i32_type().const_int(i as u64, false),
             self.context.append_basic_block(func, "defer.action"))).collect();
        self.builder.build_switch(id, unknown, &cases).map_err(|e| e.to_string())?;
        self.builder.position_at_end(unknown);
        let unknown_error = self.defer_status_error(&abi, self.context.i32_type().const_int(5, false))?;
        self.record_defer_error(unknown_error, first, has_first, loop_bb)?;
        for (expr, (_, action_bb)) in actions.iter().zip(cases.iter()) {
            self.builder.position_at_end(*action_bb);
            let failed = self.context.append_basic_block(func, "defer.action.failed");
            self.try_stack.push(failed);
            let compiled = self.compile_statement_expr(expr);
            let result = match compiled {
                Ok(value) => {
                    self.guard_local_expr_error(&[value])?;
                    self.call_rt_void(self.rt.moo_release, &[value.into()], "defer.release_result")?;
                    Ok(())
                }
                Err(err) => Err(err),
            };
            self.try_stack.pop();
            result?;
            self.builder.build_unconditional_branch(loop_bb).map_err(|e| e.to_string())?;
            self.builder.position_at_end(failed);
            let error = self.call_rt(take, &[], "defer.action.error")?;
            self.record_defer_error(error, first, has_first, loop_bb)?;
        }
        self.builder.position_at_end(finish);
        self.call_rt_void(self.rt.moo_try_leave, &[], "defer.leave")?;
        let safe = self.builder.build_load(self.context.bool_type(), can_free, "defer.free.safe")
            .map_err(|e| e.to_string())?.into_int_value();
        let free = self.context.append_basic_block(func, "defer.free");
        let no_free = self.context.append_basic_block(func, "defer.no_free");
        let done = self.context.append_basic_block(func, "defer.done");
        self.builder.build_conditional_branch(safe, free, no_free).map_err(|e| e.to_string())?;
        self.builder.position_at_end(free);
        abi.emit_release_slot(self.context, &self.builder, slot)?;
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(no_free);
        self.builder.build_store(slot, self.context.ptr_type(AddressSpace::default()).const_null())
            .map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(done);
        let has = self.builder.build_load(self.context.bool_type(), has_first, "defer.has_error")
            .map_err(|e| e.to_string())?.into_int_value();
        Ok((has, first))
    }

    // Verzweigt nach einem Drain nur auf dem Fehlerpfad; ein lokaler Catch
    // behält seinen Handler und seine Locals. Ohne lokalen Handler wird der
    // Frame vor dem Weiterwurf geschlossen.
    fn route_runtime_defer_error(
        &mut self, has: inkwell::values::IntValue<'ctx>, first: PointerValue<'ctx>,
        abandoned: Option<StructValue<'ctx>>, must_fail: bool,
    ) -> Result<(), String> {
        let func = self.current_function.ok_or("Defer-Exit ohne Funktion")?;
        let failure = self.context.append_basic_block(func, "defer.exit.error");
        let success = self.context.append_basic_block(func, "defer.exit.ok");
        self.builder.build_conditional_branch(has, failure, success).map_err(|e| e.to_string())?;
        self.builder.position_at_end(failure);
        if let Some(value) = abandoned {
            self.call_rt_void(self.rt.moo_release, &[value.into()], "defer.release_return")?;
        }
        let error = self.builder.build_load(self.mv_type(), first, "defer.exit.first")
            .map_err(|e| e.to_string())?.into_struct_value();
        if let Some(catch_bb) = self.try_stack.iter().rev().copied()
            .find(|bb| bb.get_parent() == Some(func))
        {
            self.call_rt_void(self.rt.moo_throw, &[error.into()], "defer.to_catch")?;
            self.builder.build_unconditional_branch(catch_bb).map_err(|e| e.to_string())?;
        } else {
            self.release_defer_frame_locals()?;
            self.call_rt_void(self.rt.moo_throw, &[error.into()], "defer.propagate")?;
            if self.in_main_function() {
                self.builder.build_return(Some(&self.context.i32_type().const_int(1, false)))
                    .map_err(|e| e.to_string())?;
            } else if func.get_type().get_return_type().is_none() {
                self.builder.build_return(None).map_err(|e| e.to_string())?;
            } else {
                let none = self.call_rt(self.rt.moo_none, &[], "defer.error.return")?;
                self.builder.build_return(Some(&none)).map_err(|e| e.to_string())?;
            }
        }
        self.builder.position_at_end(success);
        if must_fail {
            let trap = self.module.get_function("llvm.trap").unwrap_or_else(||
                self.module.add_function("llvm.trap", self.context.void_type().fn_type(&[], false), None));
            self.builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
            self.builder.build_unreachable().map_err(|e| e.to_string())?;
        }
        Ok(())
    }

    // Bei ungueltigem Stack ist weder weiteres Poppen noch free sicher.
    // Ein bereits gesetzter Fehler hat Vorrang vor dem neuen Statusfehler.
    fn route_invalid_defer_stack(&mut self, error: StructValue<'ctx>) -> Result<(), String> {
        let func = self.current_function.ok_or("Defer-Status ohne Funktion")?;
        let first = self.entry_alloca(self.mv_type(), "defer.invalid.first")?;
        let pending = self.builder.build_call(self.rt.moo_try_check, &[], "defer.invalid.pending")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let pending = match pending {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_check fehlgeschlagen".into()),
        };
        let has_pending = self.builder.build_int_compare(
            inkwell::IntPredicate::NE, pending, self.context.i32_type().const_zero(), "defer.invalid.has_pending",
        ).map_err(|e| e.to_string())?;
        let prior_bb = self.context.append_basic_block(func, "defer.invalid.prior");
        let new_bb = self.context.append_basic_block(func, "defer.invalid.new");
        let merge = self.context.append_basic_block(func, "defer.invalid.merge");
        self.builder.build_conditional_branch(has_pending, prior_bb, new_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(prior_bb);
        self.call_rt_void(self.rt.moo_release, &[error.into()], "defer.invalid.release_new")?;
        let take = self.rt.declare_try_take_error(self.context, &self.module, self.runtime_windows_abi);
        let prior = self.call_rt(take, &[], "defer.invalid.take_prior")?;
        self.builder.build_store(first, prior).map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(merge).map_err(|e| e.to_string())?;
        self.builder.position_at_end(new_bb);
        self.builder.build_store(first, error).map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(merge).map_err(|e| e.to_string())?;
        self.builder.position_at_end(merge);
        if let Some(slot) = self.defer_frame {
            self.builder.build_store(slot, self.context.ptr_type(AddressSpace::default()).const_null())
                .map_err(|e| e.to_string())?;
        }
        self.route_runtime_defer_error(self.context.bool_type().const_int(1, false), first, None, true)
    }

    /// Führt alle Defers in LIFO-Reihenfolge aus (Go-Semantik)
    fn emit_defers(&mut self) -> Result<(), String> {
        let defers: Vec<Expr> = self.defer_stack.iter().rev()
            .map(|id| self.defer_catalog.action(*id).cloned()
                .ok_or_else(|| format!("Unbekannte Defer-Aktions-ID: {id}")))
            .collect::<Result<_, _>>()?;
        if defers.is_empty() { return Ok(()); }
        // Nur vollstaendig instrumentierte Ausdrucksbaeume duerfen die
        // synthetische Fehlerkante nutzen. Insbesondere bleiben native/FFI-
        // Calls auf dem bisherigen Pfad, bis ihre Handlergrenzen sicher sind.
        if self.bare_mode || !defers.iter().all(|expr| self.recursive_statement_expr_supported(expr)) {
            for expr in &defers {
                let value = self.compile_expr(expr)?;
                self.call_rt_void(self.rt.moo_release, &[value.into()], "rel_defer_expr")?;
            }
            return Ok(());
        }
        let func = self.current_function.unwrap();
        let first = self.entry_alloca(self.mv_type(), "defer_first_error")?;
        let has_first = self.entry_alloca(self.context.bool_type(), "defer_has_first")?;
        self.builder.build_store(has_first, self.context.bool_type().const_zero())
            .map_err(|e| e.to_string())?;
        let take = self.rt.declare_try_take_error(self.context, &self.module, self.runtime_windows_abi);
        let pending = self.builder.build_call(self.rt.moo_try_check, &[], "defer_pending")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let pending = match pending {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_check fehlgeschlagen".into()),
        };
        let pending = self.builder.build_int_compare(
            inkwell::IntPredicate::NE, pending, self.context.i32_type().const_zero(), "defer_has_pending",
        ).map_err(|e| e.to_string())?;
        let save_pending = self.context.append_basic_block(func, "defer_save_pending");
        let begin = self.context.append_basic_block(func, "defer_begin");
        self.builder.build_conditional_branch(pending, save_pending, begin).map_err(|e| e.to_string())?;
        self.builder.position_at_end(save_pending);
        let prior = self.call_rt(take, &[], "defer_prior")?;
        self.builder.build_store(first, prior).map_err(|e| e.to_string())?;
        self.builder.build_store(has_first, self.context.bool_type().const_int(1, false)).map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(begin).map_err(|e| e.to_string())?;
        self.builder.position_at_end(begin);
        self.call_rt_void(self.rt.moo_try_enter, &[], "defer_enter")?;
        for expr in &defers {
            let failed = self.context.append_basic_block(func, "defer_action_failed");
            let next = self.context.append_basic_block(func, "defer_action_next");
            self.try_stack.push(failed);
            let compiled = self.compile_statement_expr(expr);
            let result = match compiled {
                Ok(value) => {
                    self.guard_local_expr_error(&[value])?;
                    self.call_rt_void(self.rt.moo_release, &[value.into()], "rel_defer_expr")?;
                    Ok(())
                }
                Err(err) => Err(err),
            };
            self.try_stack.pop();
            result?;
            self.builder.build_unconditional_branch(next).map_err(|e| e.to_string())?;
            self.builder.position_at_end(failed);
            let error = self.call_rt(take, &[], "defer_action_error")?;
            let already = self.builder.build_load(self.context.bool_type(), has_first, "defer_already")
                .map_err(|e| e.to_string())?.into_int_value();
            let discard = self.context.append_basic_block(func, "defer_discard_error");
            let save = self.context.append_basic_block(func, "defer_save_error");
            self.builder.build_conditional_branch(already, discard, save).map_err(|e| e.to_string())?;
            self.builder.position_at_end(discard);
            self.call_rt_void(self.rt.moo_release, &[error.into()], "rel_later_defer_error")?;
            self.builder.build_unconditional_branch(next).map_err(|e| e.to_string())?;
            self.builder.position_at_end(save);
            self.builder.build_store(first, error).map_err(|e| e.to_string())?;
            self.builder.build_store(has_first, self.context.bool_type().const_int(1, false)).map_err(|e| e.to_string())?;
            self.builder.build_unconditional_branch(next).map_err(|e| e.to_string())?;
            self.builder.position_at_end(next);
        }
        self.call_rt_void(self.rt.moo_try_leave, &[], "defer_leave")?;
        let have_error = self.builder.build_load(self.context.bool_type(), has_first, "defer_error_final")
            .map_err(|e| e.to_string())?.into_int_value();
        let propagate = self.context.append_basic_block(func, "defer_propagate");
        let done = self.context.append_basic_block(func, "defer_done");
        self.builder.build_conditional_branch(have_error, propagate, done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(propagate);
        let error = self.builder.build_load(self.mv_type(), first, "defer_first")
            .map_err(|e| e.to_string())?.into_struct_value();
        self.call_rt_void(self.rt.moo_throw, &[error.into()], "defer_throw_first")?;
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(done);
        Ok(())
    }

    /// Gibt alle function-lokalen Variablen vor einem Return frei.
    /// Der Return-Wert haelt seine +1 ueber load_var-retain bzw. Producer
    /// unabhaengig vom Alloca-Slot — das Aufraeumen der Slots kann den
    /// Return nicht ueberschreiben. Ohne diesen Cleanup leaken Locals die
    /// im Hot-Loop genutzt werden (gemessen: detect_changes mit 1600 Keys
    /// leakt ~480KB pro Call).
    fn release_function_locals(&self) -> Result<(), String> {
        // DEDUPE nach Alloca-Pointer (CG1-v4): "selbst" und "this" zeigen als
        // Aliase auf DENSELBEN Slot. Ohne Dedupe wird der Receiver-Slot pro
        // Methoden-Epilog ZWEIMAL released => netto -1 pro Call, akkumuliert
        // ueber lange Schleifen bis rc=0 zu frueh => UAF/SIGABRT (~350k Iter,
        // ASan: heap-use-after-free in moo_release/main). Ein Slot haelt genau
        // EINE Ref, egal unter wie vielen Namen er registriert ist.
        let mut vars: Vec<(String, PointerValue<'ctx>)> =
            self.variables.iter().map(|(k, v)| (k.clone(), *v)).collect();
        // HashMap-Reihenfolge ist pro Prozess zufaellig. Sortierung vor der
        // Pointer-Deduplizierung gibt jedem Slot eine stabile Freigabereihenfolge;
        // Aliase wie selbst/this werden weiterhin genau einmal freigegeben.
        vars.sort_unstable_by(|a, b| a.0.cmp(&b.0));
        let mut seen: Vec<PointerValue<'ctx>> = Vec::with_capacity(vars.len());
        vars.retain(|(_, p)| {
            if seen.contains(p) { false } else { seen.push(*p); true }
        });
        for (_name, ptr) in vars {
            let v = self.builder.build_load(self.mv_type(), ptr, "local_exit")
                .map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_release, &[v.into()], "rel_local")?;
        }
        Ok(())
    }

    fn compile_return(&mut self, value: &Option<Expr>) -> Result<(), String> {
        // Sonderfall: 'gib_zurueck' im top-level (main-Funktion). main hat
        // i32 als return-type, kann keinen MooValue zurueckgeben. Werte
        // werden ignoriert, return als i32(0) emittiert.
        let func = self.current_function.unwrap();
        if func.get_name().to_str() == Ok("main") {
            let local_handlers: Vec<_> = self.try_stack.iter().copied()
                .filter(|bb| bb.get_parent() == Some(func) && Some(*bb) != self.defer_borrowed_handler).collect();
            let checked = !local_handlers.is_empty() && !self.bare_mode;
            // Expression noch evaluieren falls Side-Effects (z.B. Funktionsaufruf).
            if let Some(expr) = value {
                let result = self.compile_statement_expr_direct(expr)?;
                // main verwirft den owning Ausdruckswert auch ohne lokalen try.
                self.call_rt_void(self.rt.moo_release, &[result.into()], "main_return_release")?;
            }
            if checked {
                let catch_bb = *local_handlers.last().unwrap();
                let pending = self.builder.build_call(self.rt.moo_try_check, &[], "main_ret_check")
                    .map_err(|e| e.to_string())?.try_as_basic_value();
                let pending = match pending {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("moo_try_check fehlgeschlagen".into()),
                };
                let is_error = self.builder.build_int_compare(
                    inkwell::IntPredicate::NE, pending,
                    self.context.i32_type().const_zero(), "main_ret_error",
                ).map_err(|e| e.to_string())?;
                let success_bb = self.context.append_basic_block(func, "main_return_ok");
                self.builder.build_conditional_branch(is_error, catch_bb, success_bb)
                    .map_err(|e| e.to_string())?;
                self.builder.position_at_end(success_bb);
            }
            if self.defer_runtime {
                let (has, first) = self.drain_runtime_defers(None)?;
                self.route_runtime_defer_error(has, first, None, false)?;
            } else {
                self.emit_defers()?;
            }
            if checked && self.defer_stack.is_empty() {
                for _ in &local_handlers {
                    self.call_rt_void(self.rt.moo_try_leave, &[], "main_return_try_leave")?;
                }
            } else if checked {
                // Wie bei Funktions-Return: nur nach fehlerfreien Defers verlassen.
                let pending = self.builder.build_call(self.rt.moo_try_check, &[], "main_defer_check")
                    .map_err(|e| e.to_string())?.try_as_basic_value();
                let pending = match pending {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("moo_try_check fehlgeschlagen".into()),
                };
                let is_error = self.builder.build_int_compare(
                    inkwell::IntPredicate::NE, pending,
                    self.context.i32_type().const_zero(), "main_defer_error",
                ).map_err(|e| e.to_string())?;
                let leave_bb = self.context.append_basic_block(func, "main_defer_leave");
                let resume_bb = self.context.append_basic_block(func, "main_defer_resume");
                self.builder.build_conditional_branch(is_error, resume_bb, leave_bb)
                    .map_err(|e| e.to_string())?;
                self.builder.position_at_end(leave_bb);
                for _ in &local_handlers {
                    self.call_rt_void(self.rt.moo_try_leave, &[], "main_return_try_leave")?;
                }
                self.builder.build_unconditional_branch(resume_bb).map_err(|e| e.to_string())?;
                self.builder.position_at_end(resume_bb);
            }
            if self.defer_runtime { self.release_defer_frame_locals()?; }
            let zero = self.context.i32_type().const_int(0, false);
            self.builder.build_return(Some(&zero)).map_err(|e| format!("{e}"))?;
            return Ok(());
        }

        let val = if let Some(expr) = value {
            // P2-T74: Direktpfad wie bei zeige (native_try_direct_root).
            self.compile_statement_expr_direct(expr)?
        } else {
            self.call_rt(self.rt.moo_none, &[], "none")?
        };

        if self.defer_borrowed_handler.is_some() {
            self.guard_local_expr_error(&[val])?;
        }

        // Wenn wir in einem try-Block sind: pruefen ob der Expression einen Fehler
        // gesetzt hat (z.B. via Methoden-Aufruf 'gib_zurueck k.methode()'). Falls ja,
        // direkt zum catch_bb branchen statt zu returnen.
        let local_handlers: Vec<_> = self.try_stack.iter().copied()
            .filter(|bb| bb.get_parent() == Some(func) && Some(*bb) != self.defer_borrowed_handler).collect();
        if let Some(catch_bb) = local_handlers.last().copied() {
            let check_result = self.builder.build_call(self.rt.moo_try_check, &[], "ret_check")
                .map_err(|e| format!("{e}"))?;
            let has_error = match check_result.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("moo_try_check fehlgeschlagen".to_string()),
            };
            let is_error = self.builder.build_int_compare(
                inkwell::IntPredicate::NE, has_error,
                self.context.i32_type().const_int(0, false), "ret_is_err",
            ).map_err(|e| format!("{e}"))?;
            let ret_bb = self.context.append_basic_block(func, "return_ok");
            let error_bb = self.context.append_basic_block(func, "return_error");
            self.builder.build_conditional_branch(is_error, error_bb, ret_bb)
                .map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(error_bb);
            self.call_rt_void(self.rt.moo_release, &[val.into()], "return_release_error")?;
            self.builder.build_unconditional_branch(catch_bb).map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(ret_bb);
        }

        // Dynamischer Stack: bei Fehler den Return-Wert freigeben und
        // zum lokalen Catch oder nach Frame-Cleanup zum Aufrufer propagieren.
        if self.defer_runtime {
            let (has, first) = self.drain_runtime_defers(None)?;
            self.route_runtime_defer_error(has, first, Some(val), false)?;
            for _ in &local_handlers {
                self.call_rt_void(self.rt.moo_try_leave, &[], "return_try_leave")?;
            }
            self.release_function_locals()?;
            if func.get_type().get_return_type().is_none() {
                self.call_rt_void(self.rt.moo_release, &[val.into()], "return_void_release")?;
                self.builder.build_return(None).map_err(|e| format!("{e}"))?;
            } else {
                self.builder.build_return(Some(&val)).map_err(|e| format!("{e}"))?;
            }
            return Ok(());
        }

        // Defer-Stack vor Return ausführen (Go-Semantik)
        self.emit_defers()?;
        // Ohne Defers ist die Fehlerkante bereits geprueft. Nur lokal aktive
        // Handler verlassen; Handler des Aufrufers bleiben bestehen.
        if self.defer_stack.is_empty() {
            for _ in &local_handlers {
                self.call_rt_void(self.rt.moo_try_leave, &[], "return_try_leave")?;
            }
        } else if !local_handlers.is_empty() {
            // Bei erfolgreichem Defer den lokalen Try-Rahmen verlassen.
            // Fehlernde Defers behalten vorerst den bisherigen Fehlerpfad.
            let pending = self.builder.build_call(self.rt.moo_try_check, &[], "defer_ret_check")
                .map_err(|e| e.to_string())?.try_as_basic_value();
            let pending = match pending {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("moo_try_check fehlgeschlagen".into()),
            };
            let is_error = self.builder.build_int_compare(
                inkwell::IntPredicate::NE, pending,
                self.context.i32_type().const_zero(), "defer_ret_error",
            ).map_err(|e| e.to_string())?;
            let leave_bb = self.context.append_basic_block(func, "defer_return_leave");
            let resume_bb = self.context.append_basic_block(func, "defer_return_resume");
            self.builder.build_conditional_branch(is_error, resume_bb, leave_bb)
                .map_err(|e| e.to_string())?;
            self.builder.position_at_end(leave_bb);
            for _ in &local_handlers {
                self.call_rt_void(self.rt.moo_try_leave, &[], "return_try_leave")?;
            }
            self.builder.build_unconditional_branch(resume_bb).map_err(|e| e.to_string())?;
            self.builder.position_at_end(resume_bb);
        }
        // Locals vor dem return freigeben. val haelt seine eigene +1 ueber
        // load_var-retain / Producer und ueberlebt den Slot-Cleanup.
        self.release_function_locals()?;
        let returns_void = func.get_type().get_return_type().is_none();
        if returns_void {
            self.builder.build_return(None).map_err(|e| format!("{e}"))?;
        } else {
            self.builder.build_return(Some(&val)).map_err(|e| format!("{e}"))?;
        }
        Ok(())
    }

    fn compile_class_def(&mut self, name: &str, _parent: &Option<String>, body: &[Stmt]) -> Result<(), String> {
        let prev_class = self.current_class.take();
        self.current_class = Some(name.to_string());
        // Methoden kompilieren
        for stmt in body {
            if let Stmt::FunctionDef { name: method_name, params, defaults, body: method_body, .. } = stmt {
                let func = self.class_methods.get(&(name.to_string(), method_name.clone()))
                    .ok_or(format!("Methode {name}.{method_name} nicht gefunden"))?
                    .clone();

                let entry = self.context.append_basic_block(func, "entry");
                let prev_fn = self.current_function;
                let prev_vars = self.variables.clone();
                let prev_block = self.builder.get_insert_block();
                let prev_defers = std::mem::take(&mut self.defer_stack);
                let prev_catalog = std::mem::take(&mut self.defer_catalog);
                let prev_defer_frame = self.defer_frame.take();
                let prev_defer_runtime = std::mem::replace(&mut self.defer_runtime, false);
                let prev_defer_borrowed = self.defer_borrowed_handler.take();
                self.defer_catalog = defer_catalog::DeferCatalog::collect(method_body)?;

                self.current_function = Some(func);
                self.builder.position_at_end(entry);
                self.variables.clear();
                self.init_defer_frame()?;
                // P2-T85 (Plan 983e8a9c A1/B2): Methode = Standardrahmen mit eigenem
                // Pruefpunkt-Ausgang wie benannte Funktionen; Rahmenzustand der
                // umgebenden Funktion sichern (kein Sprung in fremde Bloecke).
                let rahmen = self.rahmen_betreten(RahmenArt::Methode);
                self.native_guard_enabled = {
                    let mut calls = std::collections::HashSet::new();
                    Self::collect_defer_calls(method_body, &mut calls);
                    self.native_guard_fuer(&calls)
                };

                // self/selbst ist der erste Parameter
                let self_alloca = self.builder.build_alloca(self.mv_type(), "self")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(self_alloca, func.get_nth_param(0).unwrap())
                    .map_err(|e| format!("{e}"))?;
                self.variables.insert("selbst".to_string(), self_alloca);
                self.variables.insert("this".to_string(), self_alloca);
                // selbst und this haben den Typ der aktuellen Klasse — wichtig
                // fuer korrektes Method-Dispatch bei Vererbung.
                let prev_selbst_type = self.object_var_types.insert("selbst".to_string(), name.to_string());
                let prev_this_type = self.object_var_types.insert("this".to_string(), name.to_string());
                let _ = (prev_selbst_type, prev_this_type);

                // Regulaere Parameter
                for (i, param_name) in params.iter().enumerate() {
                    let alloca = self.builder.build_alloca(self.mv_type(), param_name)
                        .map_err(|e| format!("{e}"))?;
                    let param_val = func.get_nth_param((i + 1) as u32).unwrap();
                    self.builder.build_store(alloca, param_val).map_err(|e| format!("{e}"))?;
                    self.variables.insert(param_name.clone(), alloca);

                    // Default-Parameter: wenn der Wert none ist, den Default einsetzen
                    if let Some(Some(default_expr)) = defaults.get(i) {
                        let current_val = self.load_var(param_name)?;
                        let tag = self.builder.build_extract_value(current_val, 0, "tag")
                            .map_err(|e| format!("{e}"))?;
                        let is_none = self.builder.build_int_compare(
                            inkwell::IntPredicate::EQ,
                            tag.into_int_value(),
                            self.context.i64_type().const_int(tag::NONE, false), // MOO_NONE
                            "is_none",
                        ).map_err(|e| format!("{e}"))?;

                        let default_bb = self.context.append_basic_block(func, "default");
                        let continue_bb = self.context.append_basic_block(func, "no_default");
                        self.builder.build_conditional_branch(is_none, default_bb, continue_bb)
                            .map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(default_bb);
                        let default_val = self.compile_expr(default_expr)?;
                        self.store_var(param_name, default_val)?;
                        self.builder.build_unconditional_branch(continue_bb)
                            .map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(continue_bb);
                    }
                }

                // Pre-Allocation lokaler Variablen mit Counter-Pattern-Erkennung
                // (analog compile_function_def, siehe Kommentar dort).
                let none_init = self.context.i64_type().const_int(tag::NONE, false);
                let zero_data = self.context.i64_type().const_int(0, false);
                let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
                let local_names = Self::collect_all_var_names(method_body);
                let global_decls: Vec<String> = method_body.iter()
                    .filter_map(|s| if let Stmt::Global(names, _) = s { Some(names.clone()) } else { None })
                    .flatten()
                    .collect();
                for g in &global_decls {
                    if !self.globals.contains_key(g) {
                        return Err(format!("Methode '{name}.{method_name}': 'global {g}' — es gibt keine Top-Level-Variable '{g}'"));
                    }
                }
                for n in &local_names {
                    if self.variables.contains_key(n.as_str()) { continue; }
                    if global_decls.iter().any(|g| g == n) { continue; }
                    if self.globals.contains_key(n) {
                        match Self::first_use_is_local_write(method_body, n) {
                            Some(true) => {}
                            _ => continue,
                        }
                    }
                    let alloca = self.builder.build_alloca(self.mv_type(), n)
                        .map_err(|e| format!("{e}"))?;
                    self.builder.build_store(alloca, none_val).map_err(|e| format!("{e}"))?;
                    self.variables.insert(n.clone(), alloca);
                }

                let method_defer_handler = self.defer_frame_begin()?;
                for s in method_body {
                    if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
                    self.compile_stmt(s)?;
                }

                let current_bb = self.builder.get_insert_block().unwrap();
                if current_bb.get_terminator().is_none() {
                    self.defer_frame_finish()?;
                    // Wie bei explizitem Return erst nach den Defers freigeben.
                    self.release_function_locals()?;
                    let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
                    self.builder.build_return(Some(&none_val)).map_err(|e| format!("{e}"))?;
                }

                self.defer_frame_end(method_defer_handler)?;
                self.rahmen_verlassen(rahmen);
                self.current_function = prev_fn;
                self.variables = prev_vars;
                self.defer_stack = prev_defers;
                self.defer_catalog = prev_catalog;
                self.defer_frame = prev_defer_frame;
                self.defer_runtime = prev_defer_runtime;
                self.defer_borrowed_handler = prev_defer_borrowed;
                self.object_var_types.remove("selbst");
                self.object_var_types.remove("this");
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }
            }
        }

        self.current_class = prev_class;
        Ok(())
    }

    fn compile_data_class(&mut self, name: &str, fields: &[String]) -> Result<(), String> {
        // Data-Klasse: generiert erstelle()-Konstruktor der ein Objekt mit den Feldern erzeugt
        // und eine to_string()-Methode
        let ctor_func = self.class_methods.get(&(name.to_string(), "erstelle".to_string()))
            .ok_or(format!("DataClass {name}__erstelle nicht gefunden"))?.clone();

        let entry = self.context.append_basic_block(ctor_func, "entry");
        let prev_fn = self.current_function;
        let prev_vars = self.variables.clone();
        let prev_block = self.builder.get_insert_block();

        self.current_function = Some(ctor_func);
        self.builder.position_at_end(entry);
        self.variables.clear();

        // self param (index 0) — das neue Objekt
        let self_alloca = self.builder.build_alloca(self.mv_type(), "self")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(self_alloca, ctor_func.get_nth_param(0).unwrap())
            .map_err(|e| format!("{e}"))?;
        self.variables.insert("selbst".to_string(), self_alloca);

        // Felder zuweisen: selbst.feld = param
        for (i, field_name) in fields.iter().enumerate() {
            let param_val = ctor_func.get_nth_param((i + 1) as u32).unwrap().into_struct_value();
            let self_val = self.builder.build_load(self.mv_type(), self_alloca, "self_val")
                .map_err(|e| format!("{e}"))?.into_struct_value();
            let prop_str = self.make_global_str(field_name, "field")?;
            self.call_rt_void(self.rt.moo_object_set,
                &[self_val.into(), prop_str.into(), param_val.into()], "set_field")?;
        }

        let self_val = self.builder.build_load(self.mv_type(), self_alloca, "ret")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        self.builder.build_return(Some(&self_val)).map_err(|e| format!("{e}"))?;

        self.current_function = prev_fn;
        self.variables = prev_vars;
        if let Some(bb) = prev_block {
            self.builder.position_at_end(bb);
        }

        Ok(())
    }

    fn compile_match(&mut self, value: &Expr, cases: &[(Option<Expr>, Option<Expr>, Vec<Stmt>)]) -> Result<(), String> {
        let func = self.current_function.unwrap();
        let val = self.compile_defer_expr(value, &[])?;
        // Match-Wert in Variable speichern fuer Bindings
        let val_ptr = if self.defer_runtime {
            self.defer_hold_value(val)?
        } else {
            let slot = self.builder.build_alloca(self.mv_type(), "match_val")
                .map_err(|e| format!("{e}"))?;
            self.builder.build_store(slot, val).map_err(|e| format!("{e}"))?;
            slot
        };
        let merge_bb = self.context.append_basic_block(func, "match_end");

        for (pattern, guard, body) in cases {
            if let Some(pat_expr) = pattern {
                let case_bb = self.context.append_basic_block(func, "case");
                let next_bb = self.context.append_basic_block(func, "next_case");

                match pat_expr {
                    Expr::Dict(pairs) => {
                        // Dict-Destructuring: prüfe ob jeder Key existiert, binde Values
                        let current_val = self.builder.build_load(self.mv_type(), val_ptr, "mval")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        // Alle Keys prüfen — bei Fehlschlag zu next_bb
                        let mut check_bb = self.builder.get_insert_block().unwrap();
                        for (key_expr, _) in pairs {
                            if matches!(key_expr, Expr::Spread(_)) { continue; }
                            let key_val = self.compile_defer_expr(key_expr, &[])?;
                            let has = self.call_rt(self.rt.moo_dict_has,
                                &[current_val.into(), key_val.into()], "has_key")?;
                            let is_true_result = self.call_rt_abi(self.rt.moo_is_truthy,
                                &[has.into()], "truthy")
                                .map_err(|e| format!("{e}"))?;
                            let is_true = match is_true_result.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("truthy fehlgeschlagen".to_string()),
                            };
                            // moo_dict_has konsumiert String-Keys bereits.
                            let cont_bb = self.context.append_basic_block(func, "destr_cont");
                            self.builder.build_conditional_branch(is_true, cont_bb, next_bb)
                                .map_err(|e| format!("{e}"))?;
                            self.builder.position_at_end(cont_bb);
                            check_bb = cont_bb;
                        }

                        // Guard prüfen falls vorhanden
                        // Zuerst Bindings erstellen (damit Guard auf gebundene Variablen zugreifen kann)
                        for (key_expr, val_expr) in pairs {
                            if matches!(key_expr, Expr::Spread(_)) { continue; }
                            if let Expr::Identifier(binding_name) = val_expr {
                                let key_val = self.compile_defer_expr(key_expr, &[])?;
                                let current_val = self.builder.build_load(self.mv_type(), val_ptr, "mval")
                                    .map_err(|e| format!("{e}"))?.into_struct_value();
                                let bound_val = self.call_rt(self.rt.moo_dict_get,
                                    &[current_val.into(), key_val.into()], "bound")?;
                                if self.defer_runtime {
                                    // moo_dict_get konsumiert String-Keys bereits.
                                    self.defer_local_slot(binding_name)?;
                                }
                                self.store_var(binding_name, bound_val)?;
                            }
                        }

                        if let Some(guard_expr) = guard {
                            let guard_val = self.compile_defer_expr(guard_expr, &[])?;
                            let guard_true_result = self.call_rt_abi(self.rt.moo_is_truthy,
                                &[guard_val.into()], "guard")
                                .map_err(|e| format!("{e}"))?;
                            let guard_bool = match guard_true_result.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("guard truthy fehlgeschlagen".to_string()),
                            };
                            if self.defer_runtime {
                                self.call_rt_void(self.rt.moo_release, &[guard_val.into()], "rel_match_guard")?;
                            }
                            self.builder.build_conditional_branch(guard_bool, case_bb, next_bb)
                                .map_err(|e| format!("{e}"))?;
                        } else {
                            self.builder.build_unconditional_branch(case_bb)
                                .map_err(|e| format!("{e}"))?;
                        }
                    }
                    _ => {
                        let current_val = self.builder.build_load(self.mv_type(), val_ptr, "mval")
                            .map_err(|e| format!("{e}"))?.into_struct_value();

                        // Guard mit Identifier-Pattern = Binding (fall n wenn n > 10)
                        if guard.is_some() {
                            if let Expr::Identifier(name) = pat_expr {
                                // Identifier mit Guard: binde Variable, prüfe nur Guard
                                if self.defer_runtime {
                                    self.defer_local_slot(name)?;
                                    self.call_rt_void(self.rt.moo_retain, &[current_val.into()], "retain_match_binding")?;
                                }
                                self.store_var(name, current_val)?;
                                let guard_val = self.compile_defer_expr(guard.as_ref().unwrap(), &[])?;
                                let guard_true_result = self.call_rt_abi(self.rt.moo_is_truthy,
                                    &[guard_val.into()], "guard")
                                    .map_err(|e| format!("{e}"))?;
                                let guard_bool = match guard_true_result.try_as_basic_value() {
                                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                    _ => return Err("guard truthy fehlgeschlagen".to_string()),
                                };
                                if self.defer_runtime {
                                    self.call_rt_void(self.rt.moo_release, &[guard_val.into()], "rel_match_guard")?;
                                }
                                self.builder.build_conditional_branch(guard_bool, case_bb, next_bb)
                                    .map_err(|e| format!("{e}"))?;
                            } else {
                                // Normaler Vergleich + Guard
                                let pat_val = self.compile_defer_expr(pat_expr, &[])?;
                                let eq = self.call_rt(self.rt.moo_eq, &[current_val.into(), pat_val.into()], "match_eq")?;
                                if self.defer_runtime {
                                    self.call_rt_void(self.rt.moo_release, &[pat_val.into()], "rel_match_pattern")?;
                                }
                                let is_true_result = self.call_rt_abi(self.rt.moo_is_truthy,
                                    &[eq.into()], "truthy")
                                    .map_err(|e| format!("{e}"))?;
                                let is_true = match is_true_result.try_as_basic_value() {
                                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                    _ => return Err("truthy fehlgeschlagen".to_string()),
                                };
                                let guard_bb = self.context.append_basic_block(func, "guard");
                                self.builder.build_conditional_branch(is_true, guard_bb, next_bb)
                                    .map_err(|e| format!("{e}"))?;

                                self.builder.position_at_end(guard_bb);
                                let guard_val = self.compile_defer_expr(guard.as_ref().unwrap(), &[])?;
                                let guard_true_result = self.call_rt_abi(self.rt.moo_is_truthy,
                                    &[guard_val.into()], "guard")
                                    .map_err(|e| format!("{e}"))?;
                                let guard_bool = match guard_true_result.try_as_basic_value() {
                                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                    _ => return Err("guard truthy fehlgeschlagen".to_string()),
                                };
                                if self.defer_runtime {
                                    self.call_rt_void(self.rt.moo_release, &[guard_val.into()], "rel_match_guard")?;
                                }
                                self.builder.build_conditional_branch(guard_bool, case_bb, next_bb)
                                    .map_err(|e| format!("{e}"))?;
                            }
                        } else {
                            // Normaler Wert-Vergleich ohne Guard
                            let pat_val = self.compile_defer_expr(pat_expr, &[])?;
                            let eq = self.call_rt(self.rt.moo_eq, &[current_val.into(), pat_val.into()], "match_eq")?;
                            if self.defer_runtime {
                                self.call_rt_void(self.rt.moo_release, &[pat_val.into()], "rel_match_pattern")?;
                            }
                            let is_true_result = self.call_rt_abi(self.rt.moo_is_truthy,
                                &[eq.into()], "truthy")
                                .map_err(|e| format!("{e}"))?;
                            let is_true = match is_true_result.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("truthy fehlgeschlagen".to_string()),
                            };
                            self.builder.build_conditional_branch(is_true, case_bb, next_bb)
                                .map_err(|e| format!("{e}"))?;
                        }
                    }
                }

                self.builder.position_at_end(case_bb);
                for stmt in body {
                    if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
                    self.compile_stmt(stmt)?;
                }
                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                }

                self.builder.position_at_end(next_bb);
            } else {
                // Default / Wildcard case
                for stmt in body {
                    if self.builder.get_insert_block().unwrap().get_terminator().is_some() { break; }
                    self.compile_stmt(stmt)?;
                }
                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                }
            }
        }

        // Falls kein default und kein match: zum merge springen
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(merge_bb);
        if self.defer_runtime { self.defer_clear_value(val_ptr)?; }
        Ok(())
    }

    fn compile_try_catch(&mut self, try_body: &[Stmt], catch_var: &Option<String>, catch_body: &[Stmt]) -> Result<(), String> {
        let func = self.current_function.unwrap();

        // Catch/After schon hier anlegen, damit wir sie in den try_stack pushen
        // koennen — Stmt::Throw im try-Body soll direkt zu catch_bb springen.
        let catch_bb = self.context.append_basic_block(func, "catch_body");
        let success_bb = self.context.append_basic_block(func, "try_success");
        let after_bb = self.context.append_basic_block(func, "after_try");

        // moo_try_enter() — markiert dass wir in einem try-Block sind
        self.call_rt_void(self.rt.moo_try_enter, &[], "try_enter")?;

        // Try-Body ausfuehren — nach einem Terminator (z.B. gib_zurueck, wirf)
        // abbrechen. Zusaetzlich wird nach JEDEM Statement moo_try_check
        // aufgerufen und bei gesetztem Error-Flag direkt zu catch_bb gebrancht —
        // sonst wuerden Fehler, die von Funktions-/Methoden-Aufrufen gesetzt
        // werden (nur Flag-basiert, kein direkter Branch), erst am Ende des
        // try-Bodys bemerkt, und ein 'gib_zurueck' dazwischen wuerde sie
        // verschlucken.
        self.try_stack.push(catch_bb);
        for s in try_body {
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() {
                break;
            }
            self.compile_stmt(s)?;

            // Check nach dem Statement — nur wenn noch nicht terminiert
            if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                let check_result = self.builder.build_call(self.rt.moo_try_check, &[], "try_check_stmt")
                    .map_err(|e| format!("{e}"))?;
                let has_error = match check_result.try_as_basic_value() {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("moo_try_check fehlgeschlagen".to_string()),
                };
                let is_error = self.builder.build_int_compare(
                    inkwell::IntPredicate::NE, has_error,
                    self.context.i32_type().const_int(0, false), "is_err_stmt",
                ).map_err(|e| format!("{e}"))?;
                let cont_bb = self.context.append_basic_block(func, "try_cont");
                self.builder.build_conditional_branch(is_error, catch_bb, cont_bb)
                    .map_err(|e| format!("{e}"))?;
                self.builder.position_at_end(cont_bb);
            }
        }
        self.try_stack.pop();
        let try_terminated = self.builder.get_insert_block().unwrap().get_terminator().is_some();

        if !try_terminated {
            // Nach dem try-body: pruefen ob ein Fehler aufgetreten ist
            let check_result = self.builder.build_call(self.rt.moo_try_check, &[], "check")
                .map_err(|e| format!("{e}"))?;
            let has_error = match check_result.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("moo_try_check fehlgeschlagen".to_string()),
            };
            let is_error = self.builder.build_int_compare(
                inkwell::IntPredicate::NE, has_error,
                self.context.i32_type().const_int(0, false), "is_error",
            ).map_err(|e| format!("{e}"))?;
            self.builder.build_conditional_branch(is_error, catch_bb, success_bb)
                .map_err(|e| format!("{e}"))?;
        }
        // Wenn try_terminated: kein Branch — catch_bb/after_bb bleiben formal
        // erreichbar fuer LLVM (beide haben eigene Terminatoren), werden aber
        // auf diesem Pfad nie besucht.

        // Erfolg und Catch verlassen den Handler jeweils genau einmal.
        self.builder.position_at_end(success_bb);
        self.call_rt_void(self.rt.moo_try_leave, &[], "try_leave_ok")?;
        self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;

        // Fehlerwert und Flag vor dem Catch-Body konsumieren; der Wert ist +1.
        self.builder.position_at_end(catch_bb);
        let take_error = self.rt.declare_try_take_error(self.context, &self.module, self.runtime_windows_abi);
        let error_val = self.call_rt(take_error, &[], "error")?;
        if let Some(var_name) = catch_var {
            let error_str = self.call_rt(self.rt.moo_to_string, &[error_val.into()], "err_str")?;
            // moo_to_string gibt fuer MOO_STRING denselben borrowed Wert zurueck.
            // Dann wird der Wert aus moo_try_take_error direkt in die Catch-Variable
            // transferiert. Fuer alle anderen Tags ist error_str ein frischer +1-
            // Wert und nur der retained Quellwert muss freigegeben werden.
            let error_tag = self.builder.build_extract_value(error_val, 0, "error_tag")
                .map_err(|e| format!("{e}"))?.into_int_value();
            let is_string = self.builder.build_int_compare(
                inkwell::IntPredicate::EQ,
                error_tag,
                self.context.i64_type().const_int(tag::STRING, false),
                "error_is_string",
            ).map_err(|e| format!("{e}"))?;
            let release_bb = self.context.append_basic_block(func, "catch_release_error");
            let store_bb = self.context.append_basic_block(func, "catch_store_error");
            self.builder.build_conditional_branch(is_string, store_bb, release_bb)
                .map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(release_bb);
            self.call_rt_void(self.rt.moo_release, &[error_val.into()], "release_error")?;
            self.builder.build_unconditional_branch(store_bb).map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(store_bb);
            self.store_var(var_name, error_str)?;
        } else {
            self.call_rt_void(self.rt.moo_release, &[error_val.into()], "discard_caught_error")?;
        }
        // try_leave raeumt auf
        self.call_rt_void(self.rt.moo_try_leave, &[], "try_leave")?;
        for s in catch_body {
            if self.builder.get_insert_block().unwrap().get_terminator().is_some() {
                break;
            }
            self.compile_stmt(s)?;
        }
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
        }

        // Beide regulaeren Pfade haben ihren Handler bereits verlassen.
        self.builder.position_at_end(after_bb);
        Ok(())
    }

    // === Expression-Kompilierung ===

    // Alloca IMMER im Entry-Block der aktuellen Funktion platzieren.
    // Ein alloca am aktuellen Insert-Point landet sonst im Schleifenkoerper
    // und waechst den Stack pro Iteration (G1-LEAK-Befund: list_tmp/dict_tmp/
    // comp_*-Slots = 16B/Iteration => SEGV nach ~700k Iterationen bei 12MB
    // Stack). Gleiches Muster wie der __mcall_obj-Slot.
    fn entry_alloca<T: inkwell::types::BasicType<'ctx>>(
        &self, ty: T, name: &str,
    ) -> Result<inkwell::values::PointerValue<'ctx>, String> {
        let func = self.current_function
            .ok_or_else(|| "entry_alloca: keine aktuelle Funktion".to_string())?;
        let entry_bb = func.get_first_basic_block()
            .ok_or_else(|| "entry_alloca: kein Entry-Block".to_string())?;
        let cur = self.builder.get_insert_block()
            .ok_or_else(|| "entry_alloca: kein Insert-Block".to_string())?;
        if let Some(first) = entry_bb.get_first_instruction() {
            self.builder.position_before(&first);
        } else {
            self.builder.position_at_end(entry_bb);
        }
        let p = self.builder.build_alloca(ty, name).map_err(|e| format!("{e}"))?;
        self.builder.position_at_end(cur);
        Ok(p)
    }

    fn registration_builtin_visible(&self, name: &str) -> bool {
        matches!(name, "rueckruf_registrieren" | "rückruf_registrieren" | "rueckruf_abmelden")
            && self.module.get_function(name).is_none()
            && !self.variables.contains_key(name) && !self.globals.contains_key(name)
    }

    fn runtime_defer_expr_supported(&self, expr: &Expr) -> bool {
        if !self.recursive_statement_expr_supported(expr) { return false; }
        match expr {
            Expr::FunctionCall { name, args, .. } if name == "rueckruf_abmelden"
                && self.registration_builtin_visible(name) =>
                args.iter().all(|arg| self.runtime_defer_expr_supported(arg)),
            Expr::FunctionCall { name, args, .. } =>
                self.defer_safe_calls.contains(name)
                    && args.iter().all(|arg| self.runtime_defer_expr_supported(arg)),
            Expr::BinaryOp { left, right, .. } =>
                self.runtime_defer_expr_supported(left) && self.runtime_defer_expr_supported(right),
            Expr::List(elements) => elements.iter().all(|e| self.runtime_defer_expr_supported(e)),
            Expr::IndexAccess { object, index } =>
                self.runtime_defer_expr_supported(object) && self.runtime_defer_expr_supported(index),
            _ => true,
        }
    }

    // Ein Lambda-Alias beeinflusst die globale Codegen-Namensauflösung.
    // Solche Namen werden deshalb aus dem direkten Callgraph ausgeschlossen.
    fn collect_defer_lambda_aliases(stmts: &[Stmt], names: &mut std::collections::HashSet<String>) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { name, value: Expr::Lambda { .. }, .. }
                | Stmt::ConstAssignment { name, value: Expr::Lambda { .. }, .. } => { names.insert(name.clone()); }
                Stmt::FunctionDef { body, .. } | Stmt::ClassDef { body, .. }
                | Stmt::TestDef { body, .. } | Stmt::While { body, .. }
                | Stmt::For { body, .. } | Stmt::ParallelFor { body, .. }
                | Stmt::UnsafeBlock { body, .. } => Self::collect_defer_lambda_aliases(body, names),
                Stmt::If { body, else_body, .. } => {
                    Self::collect_defer_lambda_aliases(body, names);
                    Self::collect_defer_lambda_aliases(else_body, names);
                }
                Stmt::Guard { else_body, .. } => Self::collect_defer_lambda_aliases(else_body, names),
                Stmt::TryCatch { try_body, catch_body, .. } => {
                    Self::collect_defer_lambda_aliases(try_body, names);
                    Self::collect_defer_lambda_aliases(catch_body, names);
                }
                Stmt::Match { cases, .. } => {
                    for (_, _, body) in cases { Self::collect_defer_lambda_aliases(body, names); }
                }
                _ => {}
            }
        }
    }

    // Wird nur fuer zuvor durch runtime_defer_scope_supported akzeptierte
    // Baeume verwendet; dort sind alle anderen Ausdrucksformen ausgeschlossen.
    fn collect_defer_calls_expr(expr: &Expr, names: &mut std::collections::HashSet<String>) {
        match expr {
            Expr::FunctionCall { name, args, .. } => {
                names.insert(name.clone());
                for arg in args { Self::collect_defer_calls_expr(arg, names); }
            }
            Expr::BinaryOp { left, right, .. } => {
                Self::collect_defer_calls_expr(left, names);
                Self::collect_defer_calls_expr(right, names);
            }
            Expr::List(elements) => {
                for element in elements { Self::collect_defer_calls_expr(element, names); }
            }
            Expr::IndexAccess { object, index } => {
                Self::collect_defer_calls_expr(object, names);
                Self::collect_defer_calls_expr(index, names);
            }
            _ => {}
        }
    }

    fn collect_defer_calls(stmts: &[Stmt], names: &mut std::collections::HashSet<String>) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
                | Stmt::Show(value, _) | Stmt::Throw(value, _) | Stmt::Expression(value, _)
                | Stmt::Defer(value, _) | Stmt::Return(Some(value), _) =>
                    Self::collect_defer_calls_expr(value, names),
                Stmt::If { condition, body, else_body, .. } => {
                    Self::collect_defer_calls_expr(condition, names);
                    Self::collect_defer_calls(body, names);
                    Self::collect_defer_calls(else_body, names);
                }
                Stmt::While { condition, body, .. } => {
                    Self::collect_defer_calls_expr(condition, names);
                    Self::collect_defer_calls(body, names);
                }
                Stmt::For { iterable, body, .. } => {
                    Self::collect_defer_calls_expr(iterable, names);
                    Self::collect_defer_calls(body, names);
                }
                Stmt::TryCatch { try_body, catch_body, .. } => {
                    Self::collect_defer_calls(try_body, names);
                    Self::collect_defer_calls(catch_body, names);
                }
                Stmt::Guard { condition, else_body, .. } => {
                    Self::collect_defer_calls_expr(condition, names);
                    Self::collect_defer_calls(else_body, names);
                }
                // P2-T82: Systemkoerper-Aufrufe stehen in unsicher:-Bloecken;
                // ohne Abstieg erhielt z.B. paar() keinen native.guard.exit.
                // P2-T93: runtime_defer_scope_supported steigt ebenfalls ab
                // (native Aufrufe darin bleiben dort ausgeschlossen).
                Stmt::UnsafeBlock { body, .. } => Self::collect_defer_calls(body, names),
                _ => {}
            }
        }
    }

    fn prepare_runtime_defer_graph(&mut self, program: &Program) -> Result<(), String> {
        self.defer_safe_calls.clear();
        self.defer_guard_targets.clear();
        if self.bare_mode || self.profiling {
            return Ok(());
        }
        let mut aliases = std::collections::HashSet::new();
        Self::collect_defer_lambda_aliases(&program.statements, &mut aliases);
        let defs: std::collections::HashMap<String, &[Stmt]> = program.statements.iter().filter_map(|stmt| match stmt {
            Stmt::FunctionDef { name, defaults, decorators, body, .. }
                if defaults.iter().all(Option::is_none) && decorators.is_empty() && !aliases.contains(name)
                    && !self.native_functions.contains_key(name) && !self.extern_c_names.contains(name) =>
                    Some((name.clone(), body.as_slice())),
            _ => None,
        }).collect();
        self.defer_safe_calls = defs.keys().cloned().collect();
        loop {
            let next: std::collections::HashSet<String> = defs.iter()
                .filter(|(name, body)| self.defer_safe_calls.contains(*name) && self.runtime_defer_scope_supported(body))
                .map(|(name, _)| name.clone()).collect();
            if next == self.defer_safe_calls { break; }
            self.defer_safe_calls = next;
        }
        let mut pending = Vec::new();
        for (name, body) in &defs {
            if self.defer_safe_calls.contains(name) && !defer_catalog::DeferCatalog::collect(body)?.actions().is_empty() {
                pending.push(name.clone());
            }
        }
        while let Some(name) = pending.pop() {
            if !self.defer_guard_targets.insert(name.clone()) { continue; }
            if let Some(body) = defs.get(&name) {
                let mut calls = std::collections::HashSet::new();
                Self::collect_defer_calls(body, &mut calls);
                pending.extend(calls.into_iter().filter(|callee| self.defer_safe_calls.contains(callee)));
            }
        }
        Ok(())
    }

    // Laufzeit-Defer vorerst nur in Frames, deren ausgefuehrte Ausdruecke
    // und Kontrollformen durch den geprueften Fehlerpfad erfasst werden.
    // Fremde/native Calls, Methoden und unbekannte Formen bleiben im
    // bisherigen Fallback, bis ihre Fehlergrenzen angebunden sind.
    fn runtime_defer_scope_supported(&self, stmts: &[Stmt]) -> bool {
        stmts.iter().all(|stmt| match stmt {
            Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
            | Stmt::Show(value, _) | Stmt::Throw(value, _) | Stmt::Expression(value, _)
            | Stmt::Defer(value, _) | Stmt::Return(Some(value), _) =>
                self.runtime_defer_expr_supported(value),
            Stmt::Return(None, _) | Stmt::Break(_) | Stmt::Continue(_) | Stmt::Global(_, _) => true,
            Stmt::If { condition, body, else_body, .. } =>
                self.runtime_defer_expr_supported(condition)
                    && self.runtime_defer_scope_supported(body)
                    && self.runtime_defer_scope_supported(else_body),
            Stmt::While { condition, body, .. } =>
                self.runtime_defer_expr_supported(condition)
                    && self.runtime_defer_scope_supported(body),
            Stmt::For { iterable, body, .. } =>
                self.runtime_defer_expr_supported(iterable)
                    && self.runtime_defer_scope_supported(body),
            Stmt::TryCatch { try_body, catch_body, .. } =>
                self.runtime_defer_scope_supported(try_body)
                    && self.runtime_defer_scope_supported(catch_body),
            Stmt::Guard { condition, else_body, .. } =>
                self.runtime_defer_expr_supported(condition)
                    && self.runtime_defer_scope_supported(else_body),
            // P2-T93 (Befund dbea7b86): unsicher: aendert im dynamischen Code nur
            // die Erlaubnis gefaehrlicher Builtins; die Anweisungen kompilieren
            // wie ausserhalb. Ihre Ausdruecke bleiben einzeln geprueft (native
            // und Speicher-Builtins sind keine defer_safe_calls -> weiter false).
            Stmt::UnsafeBlock { body, .. } => self.runtime_defer_scope_supported(body),
            _ => false,
        })
    }

    /// Nur vollstaendig instrumentierte Ausdrucksbaeume duerfen rekursiv
    /// auf eine gemeinsame Fehlerkante verzweigen.
    fn recursive_statement_expr_supported(&self, expr: &Expr) -> bool {
        match expr {
            Expr::Number(_) | Expr::String(_) | Expr::Boolean(_) | Expr::None | Expr::Identifier(_) => true,
            Expr::BinaryOp { left, right, .. } =>
                self.recursive_statement_expr_supported(left) && self.recursive_statement_expr_supported(right),
            Expr::UnaryOp { operand, .. } if self.defer_runtime =>
                self.recursive_statement_expr_supported(operand),
            Expr::PropertyAccess { object, .. } | Expr::OptionalChain { object, .. } if self.defer_runtime =>
                self.recursive_statement_expr_supported(object),
            Expr::Ternary { condition, then_val, else_val } if self.defer_runtime =>
                self.recursive_statement_expr_supported(condition)
                    && self.recursive_statement_expr_supported(then_val)
                    && self.recursive_statement_expr_supported(else_val),
            Expr::NullishCoalesce { left, right } if self.defer_runtime =>
                self.recursive_statement_expr_supported(left) && self.recursive_statement_expr_supported(right),
            Expr::New { args, .. } if self.defer_runtime => args.iter().all(|a| self.recursive_statement_expr_supported(a)),
            Expr::This | Expr::Lambda { .. } if self.defer_runtime => true,
            Expr::MethodCall { object, method, args } if self.defer_runtime
                && (args.len() <= 1 || self.class_methods.keys().any(|(_, m)| m == method)) =>
                self.recursive_statement_expr_supported(object)
                    && args.iter().all(|a| self.recursive_statement_expr_supported(a)),
            Expr::MatchExpr { value, cases } if self.defer_runtime =>
                self.recursive_statement_expr_supported(value) && cases.iter().all(|(p, g, r)|
                    p.as_ref().map_or(true, |e| self.recursive_statement_expr_supported(e))
                        && g.as_ref().map_or(true, |e| self.recursive_statement_expr_supported(e))
                        && self.recursive_statement_expr_supported(r)),
            Expr::QueryExpr { source, where_cond, order_expr, select_expr, .. } if self.defer_runtime =>
                self.recursive_statement_expr_supported(source)
                    && where_cond.as_ref().map_or(true, |e| self.recursive_statement_expr_supported(e))
                    && order_expr.as_ref().map_or(true, |e| self.recursive_statement_expr_supported(e))
                    && self.recursive_statement_expr_supported(select_expr),
            Expr::Spread(inner) if self.defer_runtime => self.recursive_statement_expr_supported(inner),
            Expr::Pipe { left, right } if self.defer_runtime =>
                self.recursive_statement_expr_supported(left) && self.recursive_statement_expr_supported(right),
            Expr::List(elements) => elements.iter().all(|e| self.recursive_statement_expr_supported(e)),
            Expr::Dict(pairs) if self.defer_runtime => pairs.iter().all(|(key, value)|
                self.recursive_statement_expr_supported(key) && self.recursive_statement_expr_supported(value)),
            Expr::ListComprehension { expr, iterable, condition, .. } if self.defer_runtime =>
                self.recursive_statement_expr_supported(expr)
                    && self.recursive_statement_expr_supported(iterable)
                    && condition.as_ref().map_or(true, |c| self.recursive_statement_expr_supported(c)),
            // Endpunkte duerfen keine Seiteneffekte enthalten; C-Konversion
            // bleibt innerhalb des definierten int32-Bereichs.
            Expr::Range { start, end } => match (start.as_ref(), end.as_ref()) {
                (Expr::Number(s), Expr::Number(e)) => [s, e].iter().all(|n|
                    n.is_finite() && **n >= i32::MIN as f64 && **n <= i32::MAX as f64),
                _ => false,
            },
            Expr::IndexAccess { object, index } if self.defer_runtime && matches!(index.as_ref(), Expr::Range { .. }) => {
                let Expr::Range { start, end } = index.as_ref() else { unreachable!() };
                self.recursive_statement_expr_supported(object)
                    && self.recursive_statement_expr_supported(start) && self.recursive_statement_expr_supported(end)
            },
            // Ausserhalb des Defer-Frames bleibt die bisherige Slice-Grenze.
            Expr::IndexAccess { object, index } =>
                !matches!(index.as_ref(), Expr::Range { .. })
                    && self.recursive_statement_expr_supported(object) && self.recursive_statement_expr_supported(index),
            // P2-T82: hosted fangbare reine/System-@nativ-Aufrufe sind instrumentiert:
            // Adapterfehler enden im lokalen Catch (Direktform) oder im Skip-Block
            // (nichts, Flag gesetzt; T78/T80), Argumentwerte werden auf jedem Pfad
            // freigegeben, das Ergebnis ist +1 owning wie jeder compile_expr-Wert.
            // Erst damit greifen die rekursiven Guards um den Aufruf herum: der
            // Erstfehler bleibt erhalten (vorher lief z.B. 10 / nichts in
            // 'Division durch Null!', moo_ops.c) und Folgeaufrufe mit dem
            // nichts-Ergebnis unterbleiben.
            Expr::FunctionCall { name, args, .. } if self.native_call_fangbar(name) =>
                args.iter().all(|a| self.recursive_statement_expr_supported(a)),
            // In einem Defer-Frame ist auch ein Wertaufruf an den lokalen
            // Handler angebunden; dessen Argumente und Eltern bleiben owning.
            Expr::FunctionCall { name, args, .. } if self.defer_runtime
                && (self.variables.contains_key(name) || self.globals.contains_key(name)) =>
                args.iter().all(|a| self.recursive_statement_expr_supported(a)),
            Expr::FunctionCall { name, args, .. } =>
                !self.native_functions.contains_key(name)
                    && !self.extern_c_names.contains(name)
                    && !self.lambda_names.contains_key(name)
                    && !Self::is_kern_builtin(name)
                    && Self::fixed_cast_bits(name).is_none()
                    && self.module.get_function(name).is_some()
                    && args.iter().all(|a| self.recursive_statement_expr_supported(a)),
            _ => false,
        }
    }

    /// P2-T82: hosted fangbarer @nativ-Aufruf aus dynamischem Code (Spiegel der
    /// Kriterien system_fangbar/plain_fangbar im Native-Arm von compile_expr):
    /// skalare Signatur (seit P2-T55 auch die FFI-Einzelform), nicht bare/wasm,
    /// interner Name __moo_native.* (C-Exporte verlieren ihn per set_name und trappen).
    /// @nativ-Funktion als Funktionswert (Grenze nativ -> dynamisch). Der Thunk ist
    /// ein synthetisches Lambda `(a0, ..) => name(a0, ..)`: Er nutzt die Argument-
    /// adapter (Tag, Bereich, Exaktheit), das Ergebnis-Boxing und den Fehlerausgang
    /// des dynamischen Direktaufrufs und entsteht nur bei Wertnutzung. Direkte und
    /// nativ->nativ-Aufrufe bleiben ohne Box. moo_func_strikt laesst moo_func_call_N
    /// die Argumentzahl vor dem Aufruf pruefen (Moo-Fehler statt Ersatzwert).
    fn nativer_funktionswert(&mut self, name: &str) -> Result<StructValue<'ctx>, String> {
        let zeile = self.diag_zeile;
        let wasm_target = self.module.get_triple().as_str().to_str()
            .map(|t| t.starts_with("wasm")).unwrap_or(true);
        if self.bare_mode || wasm_target {
            return Err(crate::diagnose::vormerken("M-N002", zeile, format!(
                "Nativer Funktionswert '{name}' ist nur hosted verfügbar (nicht im Bare-/UEFI-Profil und nicht für wasm); '{name}' dort direkt aufrufen")));
        }
        let sperre: Option<String> = match self.native_signatures.get(name) {
            None => Some(self.native_dyn_sperre.get(name).cloned()
                .unwrap_or_else(|| "Zeigersignatur".into())),
            Some(_) if self.native_struct_results.contains_key(name) => Some("Strukturergebnis".into()),
            Some(_) if self.native_handle_results.contains_key(name) => Some("Zeigerergebnis".into()),
            Some(_) if self.native_buffer_params.get(name)
                .is_some_and(|p| p.iter().any(Option::is_some)) => Some("Struktur-, Zeiger- oder Pufferparameter".into()),
            Some(_) => None,
        };
        if let Some(grund) = sperre {
            return Err(crate::diagnose::vormerken("M-N001", zeile, format!(
                "Nativer Funktionswert '{name}': als Wert sind nur Signaturen aus Zahlen und Wahrheitswerten möglich ({grund}); '{name}' direkt aufrufen oder in eine Moo-Funktion einpacken")));
        }
        let function = *self.native_functions.get(name).ok_or("Native Funktion fehlt")?;
        if self.hosted_system_functions.contains(&function) && !self.safe_native_functions.contains(name) {
            return Err(crate::diagnose::vormerken("M-N001", zeile, format!(
                "Nativer Funktionswert '{name}': Systemkörper sind nur in unsicher: aufrufbar und deshalb kein Wert; eine Moo-Funktion mit unsicher: als Hülle verwenden")));
        }
        if !self.native_call_fangbar(name) {
            return Err(crate::diagnose::vormerken("M-N001", zeile, format!(
                "Nativer Funktionswert '{name}': nur hosted fangbare @nativ-Funktionen sind als Wert möglich (C-Exporte und Bare-Körper nicht); '{name}' direkt aufrufen")));
        }
        let anzahl = self.native_signatures.get(name).map(|s| s.params.len()).unwrap_or(0);
        let (wert, name_ptr) = match self.native_wert_thunks.get(name).copied() {
            Some((thunk_fn, name_ptr)) => {
                // Thunk schon erzeugt: nur einen neuen Funktionswert darauf bauen.
                let thunk_name = thunk_fn.get_name().to_string_lossy().into_owned();
                let callable_fn = self.windows_callable_wrapper(thunk_fn, &thunk_name)?;
                let fn_ptr = callable_fn.as_global_value().as_pointer_value();
                let arity = self.context.i32_type().const_int(anzahl as u64, false);
                let wert = self.call_rt(self.rt.moo_func_new,
                    &[fn_ptr.into(), arity.into(), name_ptr.into()], "nw_wert")?;
                (wert, name_ptr)
            }
            None => {
                let params: Vec<String> = (0..anzahl).map(|i| format!("__nw_arg{i}")).collect();
                let thunk = Expr::Lambda {
                    params: params.clone(),
                    param_types: vec![None; anzahl],
                    body: Box::new(Expr::FunctionCall {
                        zeile,
                        spalten: (0, 0),
                        type_args: Vec::new(),
                        name: name.to_string(),
                        args: params.iter().map(|p| Expr::Identifier(p.clone())).collect(),
                    }),
                };
                let lambda_name = format!("__lambda_{}", self.lambda_counter);
                let wert = self.compile_expr(&thunk)?;
                let thunk_fn = self.module.get_function(&lambda_name)
                    .ok_or_else(|| format!("Nativer Funktionswert '{name}': Thunk fehlt"))?;
                let name_ptr = self.make_global_str(name, &format!("__nwname_{name}"))?;
                self.native_wert_thunks.insert(name.to_string(), (thunk_fn, name_ptr));
                (wert, name_ptr)
            }
        };
        let ptr_type = self.context.ptr_type(AddressSpace::default());
        let strikt = match self.module.get_function("moo_func_strikt") {
            Some(f) => f,
            None => {
                // MSVC x64: das 16-Byte-MooValue geht indirekt (call_rt_void).
                let wert_typ: BasicMetadataTypeEnum = if self.windows_x64_abi() {
                    ptr_type.into()
                } else {
                    self.mv_type().into()
                };
                self.module.add_function("moo_func_strikt",
                    self.context.void_type().fn_type(&[wert_typ, ptr_type.into()], false), None)
            }
        };
        self.call_rt_void(strikt, &[wert.into(), name_ptr.into()], "nw_strikt")?;
        Ok(wert)
    }

    fn native_call_fangbar(&self, name: &str) -> bool {
        if !self.bare_mode && self.registration_builtin_visible(name) { return true; }
        if self.bare_mode || !self.native_signatures.contains_key(name) {
            return false;
        }
        let Some(function) = self.native_functions.get(name) else { return false; };
        let wasm_target = self.module.get_triple().as_str().to_str()
            .map(|t| t.starts_with("wasm")).unwrap_or(true);
        !wasm_target && function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."))
    }

    fn compile_statement_expr(&mut self, expr: &Expr) -> Result<StructValue<'ctx>, String> {
        self.compile_owned_statement_expr(expr, &[])
    }

    /// Direktpfad der nativen Argumentadapter in versuche: (native_try_direct).
    /// Ist der Statement-Ausdruck ein Aufruf einer nativen Nicht-FFI-Funktion,
    /// dessen Argumente keine eigenen Besitzwerte hinterlassen (Variablen,
    /// Zahlenliterale, parameterlose Moo-Funktionen), darf ein Adapter bei
    /// ungueltigem Argument oder fremdem Fehlerflag direkt in den lokalen
    /// Catch springen (compile_native_*_argument, local_catch). Der Handler
    /// muss zur aktuellen Funktion gehoeren. Vorher nur fuer `zeige` (P2-T66);
    /// seit P2-T74 auch fuer `setze` und `gib_zurück`, damit hosted
    /// Systemkoerper mit dynamischen Argumenten in versuche: nutzbar sind.
    fn native_try_direct_root(&self, expr: &Expr) -> bool {
        // Reine Formpruefung; ob ein lokaler Handler existiert, entscheidet
        // native_local_catch an der Aufrufstelle. Ohne Handler nutzt eine
        // benannte Funktion die Form fuer die flag-basierte Propagation
        // (native_argument_fatal_exit, P2-T74 Batch 4).
        match expr {
            Expr::FunctionCall { name, args, .. } =>
                self.native_functions.contains_key(name)
                    && args.iter().all(|arg| match arg {
                        Expr::Identifier(_) | Expr::Number(_) => true,
                        Expr::FunctionCall { name, args, .. } =>
                            args.is_empty() && self.native_try_zero_arg_users.contains(name),
                        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } =>
                            matches!(operand.as_ref(), Expr::Number(_)),
                        _ => false,
                    }),
            _ => false,
        }
    }

    /// Lokaler Catch fuer native Argumentadapter: nur bei Direktform und nur
    /// ein Handler der aktuellen Funktion (try_stack kann fremde Handler tragen).
    fn native_local_catch(&self) -> Option<inkwell::basic_block::BasicBlock<'ctx>> {
        if !self.native_try_direct { return None; }
        self.try_stack.last().copied().filter(|bb| bb.get_parent() == self.current_function)
    }

    /// Statement-Ausdruck mit aktiviertem Direktpfad (siehe native_try_direct_root).
    fn compile_statement_expr_direct(&mut self, expr: &Expr) -> Result<StructValue<'ctx>, String> {
        let previous = self.native_try_direct;
        self.native_try_direct = self.native_try_direct_root(expr);
        let compiled = self.compile_statement_expr(expr);
        self.native_try_direct = previous;
        compiled
    }

    fn compile_owned_statement_expr(&mut self, expr: &Expr, parents: &[StructValue<'ctx>]) -> Result<StructValue<'ctx>, String> {
        let previous = self.stmt_root_guard;
        let previous_recursive = self.stmt_recursive_guard;
        let previous_owned = std::mem::take(&mut self.stmt_owned_context);
        self.stmt_owned_context.extend_from_slice(parents);
        self.stmt_root_guard = matches!(expr, Expr::BinaryOp { .. } | Expr::List(_) | Expr::FunctionCall { .. } | Expr::IndexAccess { .. });
        let function = self.current_function.unwrap();
        // P2-T82: auch ohne eigenen Handler, wenn die Funktion den Pruefpunkt-
        // Ausgang fuer fangbare native Aufrufe traegt (native_guard_enabled).
        self.stmt_recursive_guard = !self.bare_mode
            && (self.try_stack.iter().any(|bb| bb.get_parent() == Some(function)) || self.native_guard_enabled)
            && self.recursive_statement_expr_supported(expr);
        let result = self.compile_expr(expr);
        self.stmt_root_guard = previous;
        self.stmt_recursive_guard = previous_recursive;
        self.stmt_owned_context = previous_owned;
        result
    }

    /// P2-T50: `puffer_erstellen(T, n)` / `buffer_create(T, n)` -> MOO_BUFFER.
    /// T ist ein Elementtypname (i8..u64, f32, f64), kein Ausdruck; unbekannte
    /// Namen sind Compilefehler. n prueft die Runtime (moo_buffer_create wirft
    /// fangbar bei negativer/gebrochener Laenge oder Speicherfehler). Lazy
    /// deklariert, damit Programme ohne Puffer ihr bisheriges IR behalten.
    fn compile_buffer_create(&mut self, name: &str, args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        if self.bare_mode {
            return Err(format!("{name}: typisierte Puffer sind im Bare-Profil noch nicht verfuegbar"));
        }
        const ERLAUBT: &str = "i8, u8, i16, u16, i32, u32, i64, u64, f32, f64";
        let [typ, laenge] = args else {
            return Err(format!("{name} erwartet genau zwei Argumente: Elementtyp und Laenge"));
        };
        let Expr::Identifier(typ_name) = typ else {
            return Err(format!("{name}: erstes Argument muss ein Elementtyp sein ({ERLAUBT})"));
        };
        // P2-T48 D (Plan a71947f7 D2): registrierte Struktur als Elementtyp;
        // Groesse, Ausrichtung und Struktur-ID als Konstanten an die Runtime.
        if let Some(layout) = self.native_structs.layout(typ_name) {
            if layout.alignment > 16 {
                return Err(format!("{name}: puffer[{typ_name}] mit Ausrichtung {} ist nicht freigegeben (hoechstens 16)", layout.alignment));
            }
            let (size, align) = (layout.size, u64::from(layout.alignment));
            let id = self.native_structs.struktur_id(typ_name)
                .ok_or_else(|| format!("{name}: Struktur-ID fuer '{typ_name}' fehlt"))?;
            let create = match self.module.get_function("moo_buffer_create_struct") {
                Some(f) => f,
                None => {
                    let windows = self.windows_x64_abi();
                    let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
                    let i64t = self.context.i64_type();
                    let mut params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
                    if windows { params.push(ptr.into()); }
                    params.extend([BasicMetadataTypeEnum::from(i64t), i64t.into(), i64t.into()]);
                    params.push(if windows { ptr.into() } else { self.mv_type().into() });
                    let fn_type = if windows {
                        self.context.void_type().fn_type(&params, false)
                    } else {
                        self.mv_type().fn_type(&params, false)
                    };
                    self.module.add_function("moo_buffer_create_struct", fn_type, None)
                }
            };
            let laenge = self.compile_expr(laenge)?;
            let i64t = self.context.i64_type();
            let wert = self.call_rt(create, &[
                i64t.const_int(size, false).into(), i64t.const_int(align, false).into(),
                i64t.const_int(id, false).into(), laenge.into(),
            ], "puffer_struktur_neu")?;
            if self.native_structs.boxbar(typ_name).is_ok() {
                let beschreibung = self.struktur_deskriptor(typ_name)?;
                let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
                let beschreiben = self.module.get_function("moo_buffer_struct_beschreiben")
                    .unwrap_or_else(|| self.module.add_function("moo_buffer_struct_beschreiben",
                        self.context.void_type().fn_type(&[ptr.into(), ptr.into()], false), None));
                let slot = self.entry_alloca(self.mv_type(), "strukturpuffer_beschreibung")?;
                self.builder.build_store(slot, wert).map_err(|e| e.to_string())?;
                self.builder.build_call(beschreiben, &[slot.into(), beschreibung.into()], "").map_err(|e| e.to_string())?;
            }
            return Ok(wert);
        }
        // Codes = MooBufferScalarType (compiler/runtime/moo_buffer.h).
        let code: u64 = match typ_name.as_str() {
            "i8" => 1, "u8" => 2, "i16" => 3, "u16" => 4, "i32" => 5,
            "u32" => 6, "i64" => 7, "u64" => 8, "f32" => 9, "f64" => 10,
            other => return Err(format!("{name}: unbekannter Elementtyp '{other}' ({ERLAUBT})")),
        };
        let create = self.buffer_rt_function("moo_buffer_create", true, 1, true);
        let laenge = self.compile_expr(laenge)?;
        let typ_code = self.context.i32_type().const_int(code, false);
        self.call_rt(create, &[typ_code.into(), laenge.into()], "puffer_neu")
    }

    /// P2-T50 (Plan bb3878ef A3): Puffer-Runtimefunktion erst bei Bedarf
    /// deklarieren, damit Programme ohne Puffer ihr bisheriges IR behalten.
    /// Parameter: optional ein i32 vorweg, dann `mv_params` MooValues; Ergebnis
    /// MooValue (`wert`) oder void. Windows-x64 wie decl_mv_mv: sret-Puffer
    /// zuerst, MooValue-Parameter indirekt (call_rt/call_rt_void passen an).
    fn buffer_rt_function(&self, symbol: &str, i32_zuerst: bool, mv_params: usize, wert: bool) -> FunctionValue<'ctx> {
        if let Some(function) = self.module.get_function(symbol) {
            return function;
        }
        let windows = self.windows_x64_abi();
        let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
        let mut params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
        if windows && wert { params.push(ptr.into()); }
        if i32_zuerst { params.push(self.context.i32_type().into()); }
        for _ in 0..mv_params {
            params.push(if windows { ptr.into() } else { self.mv_type().into() });
        }
        let fn_type = if wert && !windows {
            self.mv_type().fn_type(&params, false)
        } else {
            self.context.void_type().fn_type(&params, false)
        };
        self.module.add_function(symbol, fn_type, None)
    }

    /// P2-T50 A3: `puffer_anhängen(p, wert)` / `buffer_append(p, value)` haengt
    /// ein Element an (moo_buffer_append_value: Puffer geliehen, Wert
    /// konsumiert); Ergebnis nichts.
    fn compile_buffer_append(&mut self, name: &str, args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        if self.bare_mode {
            return Err(format!("{name}: typisierte Puffer sind im Bare-Profil noch nicht verfuegbar"));
        }
        let [puffer, wert] = args else {
            return Err(format!("{name} erwartet genau zwei Argumente: Puffer und Wert"));
        };
        let append = self.buffer_rt_function("moo_buffer_append_value", false, 2, false);
        let puffer = self.compile_expr(puffer)?;
        let wert = self.compile_expr(wert)?;
        self.call_rt_void(append, &[puffer.into(), wert.into()], "puffer_anhaengen")?;
        self.call_rt_void(self.rt.moo_release, &[puffer.into()], "puffer_anhaengen_rel")?;
        self.call_rt(self.rt.moo_none, &[], "none")
    }

    /// P2-T50 A3: `puffer_ansicht(p, start, laenge)` / `buffer_view(p, start, len)`
    /// liefert eine Ansicht auf denselben Speicher (moo_buffer_view_value:
    /// Puffer geliehen, Start/Laenge konsumiert, Ergebnis owning).
    fn compile_buffer_view(&mut self, name: &str, args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        if self.bare_mode {
            return Err(format!("{name}: typisierte Puffer sind im Bare-Profil noch nicht verfuegbar"));
        }
        let [puffer, start, laenge] = args else {
            return Err(format!("{name} erwartet genau drei Argumente: Puffer, Start und Laenge"));
        };
        let view = self.buffer_rt_function("moo_buffer_view_value", false, 3, true);
        let puffer = self.compile_expr(puffer)?;
        let start = self.compile_expr(start)?;
        let laenge = self.compile_expr(laenge)?;
        let ansicht = self.call_rt(view, &[puffer.into(), start.into(), laenge.into()], "puffer_ansicht")?;
        self.call_rt_void(self.rt.moo_release, &[puffer.into()], "puffer_ansicht_rel")?;
        Ok(ansicht)
    }

    /// P2-T23/T53 A3 (Plan 36aca1f2): Griff-Abfragen und Text-Bruecke.
    /// griff_gültig(h) -> Wahrheitswert; griffe_offen() -> Zahl offener Griffe;
    /// puffer_aus_text(t) -> puffer[u8] (UTF-8-Bytes); puffer_als_text(p[, n]) -> Text
    /// (strikt UTF-8). Erstes Argument geliehen und danach freigegeben, Laenge
    /// von der Runtime konsumiert (moo_handle.h, moo_buffer_index.h).
    fn compile_griff_builtin(&mut self, name: &str, args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        if self.bare_mode {
            return Err(format!("{name}: Griffe und Text-Puffer sind im Bare-Profil nicht verfuegbar"));
        }
        match name {
            "griffe_offen" | "handles_open" => {
                if !args.is_empty() {
                    return Err(format!("{name} erwartet keine Argumente"));
                }
                let f = self.buffer_rt_function("moo_handle_open_count", false, 0, true);
                self.call_rt(f, &[], "griffe_offen")
            }
            "puffer_als_text" | "buffer_to_text" => {
                let (puffer, laenge) = match args {
                    [p] => (p, None),
                    [p, n] => (p, Some(n)),
                    _ => return Err(format!("{name} erwartet einen Puffer und optional eine Laenge")),
                };
                let f = self.buffer_rt_function("moo_buffer_to_text", false, 2, true);
                let puffer = self.compile_expr(puffer)?;
                let laenge = match laenge {
                    Some(n) => self.compile_expr(n)?,
                    None => self.call_rt(self.rt.moo_none, &[], "none")?,
                };
                let text = self.call_rt(f, &[puffer.into(), laenge.into()], "puffer_als_text")?;
                self.call_rt_void(self.rt.moo_release, &[puffer.into()], "puffer_als_text_rel")?;
                Ok(text)
            }
            _ => {
                let [arg] = args else {
                    return Err(format!("{name} erwartet genau ein Argument"));
                };
                let symbol = if matches!(name, "puffer_aus_text" | "buffer_from_text") {
                    "moo_buffer_from_text"
                } else {
                    "moo_handle_valid"
                };
                let f = self.buffer_rt_function(symbol, false, 1, true);
                let wert = self.compile_expr(arg)?;
                let ergebnis = self.call_rt(f, &[wert.into()], "griff_builtin")?;
                self.call_rt_void(self.rt.moo_release, &[wert.into()], "griff_builtin_rel")?;
                Ok(ergebnis)
            }
        }
    }

    /// P2-T82: Pruefpunkt-Ausgang einer benannten Funktion ohne eigenes
    /// versuche:/aufraeumen, deren Koerper hosted fangbare @nativ-Aufrufe
    /// enthaelt (native_guard_enabled, compile_function_def). Analog zur
    /// Direktform in native_argument_fatal_exit: Locals freigeben und flag-
    /// basiert mit `nichts` zurueckkehren; der Aufrufer prueft das Flag nach
    /// seinem Statement. Einmal je Funktion angelegt; guard_local_expr_error
    /// gibt die Werte des Statements vor dem Sprung frei. Der try_stack bleibt
    /// unberuehrt (FFI-/Systemkoerper-Schranken, native_local_catch, T74-IR).
    /// P2-T92: sind an der Pruefstelle statische aufraeumen-Aktionen
    /// registriert, bekommt die Stelle einen eigenen Ausgang, der genau diese
    /// Aktionen (emit_defers: Erstfehler gesichert und danach erneut geworfen,
    /// Reihenfolge wie compile_return) VOR dem Freigeben der Locals ausfuehrt.
    fn native_guard_exit_block(&mut self) -> Result<inkwell::basic_block::BasicBlock<'ctx>, String> {
        let mit_aufraeumen = !self.defer_stack.is_empty();
        if !mit_aufraeumen {
            if let Some(exit) = self.native_guard_exit { return Ok(exit); }
        }
        let function = self.current_function.ok_or("Pruefpunkt-Ausgang ohne Funktion")?;
        let current = self.builder.get_insert_block().ok_or("Pruefpunkt-Ausgang ohne Block")?;
        let exit = self.context.append_basic_block(function,
            if mit_aufraeumen { "native.guard.exit.aufraeumen" } else { "native.guard.exit" });
        self.builder.position_at_end(exit);
        if mit_aufraeumen { self.emit_defers()?; }
        self.release_function_locals()?;
        if function.get_type().get_return_type().is_none() {
            self.builder.build_return(None).map_err(|e| e.to_string())?;
        } else {
            let none = self.call_rt(self.rt.moo_none, &[], "native.guard.none")?;
            self.builder.build_return(Some(&none)).map_err(|e| e.to_string())?;
        }
        self.builder.position_at_end(current);
        if !mit_aufraeumen { self.native_guard_exit = Some(exit); }
        Ok(exit)
    }

    /// Prueft eigene Expression-Resultate vor einem weiteren Seiteneffekt.
    /// Die Fehlerausbreitung innerhalb verschachtelter Ausdruecke bleibt separat.
    /// P2-T95: Laufzeitfunktion des Hinweiskanals bei Bedarf deklarieren:
    /// moo_throw_ort(MooValue, const MooFehlerOrt*) bzw.
    /// moo_fehler_unbehandelt(const MooFehlerStelle*). MooValue wie moo_throw
    /// (Windows-x64 indirekt, call_rt_void passt an).
    fn fehler_rt_funktion(&self, symbol: &str, mit_wert: bool) -> FunctionValue<'ctx> {
        if let Some(function) = self.module.get_function(symbol) {
            return function;
        }
        let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
        let mut params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
        if mit_wert {
            params.push(if self.windows_x64_abi() { ptr.into() } else { self.mv_type().into() });
        }
        params.push(ptr.into());
        self.module.add_function(symbol, self.context.void_type().fn_type(&params, false), None)
    }

    /// P2-T95: Name der aktuellen Moo-Funktion fuer Hinweise (main = None).
    fn fehler_funktionsname(&self) -> Option<String> {
        let function = self.current_function?;
        let name = function.get_name().to_str().ok()?.to_string();
        if name == "main" { None } else { Some(name) }
    }

    /// P2-T95: Quelldatei der aktuellen Funktion (Modulfunktion oder Hauptdatei).
    /// Nur der Dateiname: keine Host-Pfade im IR (reproduzierbar, Legacy-IR-Gate).
    fn fehler_datei(&self) -> Option<String> {
        let name = self.fehler_funktionsname();
        let pfad = name.and_then(|n| self.funktion_quellen.get(&n).cloned()).or_else(|| self.quelldatei.clone())?;
        std::path::Path::new(&pfad).file_name().map(|d| d.to_string_lossy().into_owned())
    }

    /// P2-T97: Moo-Name der aktuellen Funktion fuer Diagnosen (ohne nativen Praefix).
    fn diag_funktion(&self) -> Option<String> {
        let name = self.fehler_funktionsname()?;
        Some(name.strip_prefix("__moo_native.").unwrap_or(&name).to_string())
    }

    fn diag_in_nativ(&self) -> bool {
        let roh = self.fehler_funktionsname();
        roh.as_deref().is_some_and(|n| n.starts_with("__moo_native."))
            || self.diag_funktion().is_some_and(|n| self.native_functions.contains_key(&n))
    }

    /// P2-T97: Funktion und Datei (Pfad wie angegeben; Modulfunktion = Moduldatei)
    /// ergaenzen, soweit die Diagnose sie nicht selbst traegt.
    fn diag_ort(&self, mut d: crate::diagnose::Diagnose) -> crate::diagnose::Diagnose {
        if d.funktion.is_none() {
            d.funktion = self.diag_funktion();
        }
        if d.zeile == 0 {
            d.zeile = self.diag_zeile;
        }
        if d.datei.is_none() {
            d.datei = d.funktion.as_ref().and_then(|n| self.funktion_quellen.get(n).cloned())
                .or_else(|| self.quelldatei.clone());
        }
        d
    }

    /// P2-T100: Argumentzahl eines Builtins pruefen (Tabelle aus dem Build).
    /// Zu wenige: Fehler M-C003 (der Arm laese sonst ueber das Ende der
    /// Argumente). Zu viele, nur bei exakter Anzahl: Hinweis M-C004 (der Rest
    /// wird nicht ausgewertet), still in importierten stdlib-Modulen.
    /// P7-T22: Hinweis M-H008, wenn ein Uebergangsalias statt des kanonischen
    /// Namens benutzt wird (still in stdlib-Modulen, je Meldung einmal).
    fn veralteter_name(&mut self, name: &str, alt: &str, neu: &str) {
        if name != alt { return; }
        let d = self.diag_ort(crate::diagnose::Diagnose::hinweis("M-H008", format!(
            "'{alt}' ist veraltet; kanonischer Name ist '{neu}' (gleiche Wirkung).")))
            .gefunden(alt.to_string())
            .vorschlag(format!("'{alt}' durch '{neu}' ersetzen."));
        let stdlib_modul = d.datei.as_deref().is_some_and(|datei| {
            Some(datei) != self.quelldatei.as_deref() && crate::fehlerpfade::ist_stdlib(datei)
        });
        if !stdlib_modul && !self.warnings.contains(&d) {
            self.warnings.push(d);
        }
    }

    fn builtin_aritaet_pruefen(&mut self, name: &str, erwartet: usize, exakt: bool, anzahl: usize) -> Result<(), String> {
        let wort = |n: usize| if n == 1 { "Argument" } else { "Argumente" };
        let mindestens = if exakt { "" } else { "mindestens " };
        if anzahl < erwartet {
            let d = crate::diagnose::Diagnose::fehler("M-C003", format!(
                "'{name}' braucht {mindestens}{erwartet} {}, bekommen {anzahl}.", wort(erwartet)))
                .erwartet(format!("{mindestens}{erwartet} {}", wort(erwartet)))
                .gefunden(format!("{anzahl}"))
                .vorschlag(format!("'{name}' mit {mindestens}{erwartet} {} aufrufen.",
                    if erwartet == 1 { "Argument" } else { "Argumenten" }));
            return Err(self.diag_fehler(d));
        }
        if exakt && anzahl > erwartet {
            let d = self.diag_ort(crate::diagnose::Diagnose::hinweis("M-C004", format!(
                "'{name}' liest nur {erwartet} {}; {}.", wort(erwartet),
                if anzahl - erwartet == 1 { "das ueberzaehlige wird nicht ausgewertet".to_string() }
                else { format!("die {} ueberzaehligen werden nicht ausgewertet", anzahl - erwartet) })))
                .erwartet(format!("{erwartet} {}", wort(erwartet)))
                .gefunden(format!("{anzahl}"));
            let stdlib_modul = d.datei.as_deref().is_some_and(|datei| {
                Some(datei) != self.quelldatei.as_deref() && crate::fehlerpfade::ist_stdlib(datei)
            });
            if !stdlib_modul && !self.warnings.contains(&d) {
                self.warnings.push(d);
            }
        }
        Ok(())
    }

    /// P2-T97: strukturierten Fehler ablegen; liefert die Meldung als Err-Text.
    fn diag_fehler(&self, d: crate::diagnose::Diagnose) -> String {
        let meldung = d.meldung.clone();
        crate::diagnose::melden(self.diag_ort(d));
        meldung
    }

    fn fehler_text_ptr(&self, text: Option<&str>) -> Result<inkwell::values::PointerValue<'ctx>, String> {
        match text {
            Some(text) => Ok(self.builder.build_global_string_ptr(text, "moo.hinweis.text")
                .map_err(|e| e.to_string())?.as_pointer_value()),
            None => Ok(self.context.ptr_type(inkwell::AddressSpace::default()).const_null()),
        }
    }

    /// P2-T95: statischer MooFehlerOrt {funktion, datei, zeile} der wirf-Stelle.
    fn fehler_ort_konstante(&mut self) -> Result<inkwell::values::PointerValue<'ctx>, String> {
        let funktion = self.fehler_funktionsname();
        let funktion = self.fehler_text_ptr(funktion.as_deref())?;
        let datei = self.fehler_datei();
        let datei = self.fehler_text_ptr(datei.as_deref())?;
        let i64t = self.context.i64_type();
        let zeile = i64t.const_int(self.wirf_zeile as u64, false);
        let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
        let ty = self.context.struct_type(&[ptr.into(), ptr.into(), i64t.into()], false);
        let global = self.module.add_global(ty, None, "moo.fehler.ort");
        global.set_linkage(inkwell::module::Linkage::Private);
        global.set_constant(true);
        global.set_initializer(&ty.const_named_struct(&[funktion.into(), datei.into(), zeile.into()]));
        Ok(global.as_pointer_value())
    }

    /// P2-T95 (User-Entscheid): Nach einem Moo-Aufruf in einem Rahmen OHNE
    /// eigenen Pruefweg (kein lokales versuche:, kein Laufzeit-Defer-Rahmen,
    /// kein geliehener Handler, kein native_guard) laeuft der Aufrufer nach
    /// einem Callee-Fehler weiter. Steht ein Fehler an, meldet die Laufzeit das
    /// einmal (moo_fehler_unbehandelt); der Fehler bleibt fuer den aeusseren
    /// Pruefpunkt anstehend. main braucht keine Pruefung: ohne eigenes versuche:
    /// ist dort die Tiefe 0, und moo_throw hat den Fehler schon gemeldet.
    fn fehler_hinweis_nach_aufruf(&mut self, aufruf: &str, zeile: usize) -> Result<(), String> {
        if self.bare_mode || self.in_main_function() || self.defer_runtime
            || self.defer_borrowed_handler.is_some() || self.native_guard_enabled
        {
            return Ok(());
        }
        let Some(function) = self.current_function else { return Ok(()) };
        if self.try_stack.iter().any(|bb| bb.get_parent() == Some(function)) {
            return Ok(());
        }
        let funktion = self.fehler_funktionsname();
        let funktion = self.fehler_text_ptr(funktion.as_deref())?;
        let aufruf_ptr = self.fehler_text_ptr(Some(aufruf))?;
        let datei = self.fehler_datei();
        let datei = self.fehler_text_ptr(datei.as_deref())?;
        let i64t = self.context.i64_type();
        let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
        let ty = self.context.struct_type(&[ptr.into(), ptr.into(), ptr.into(), i64t.into()], false);
        let stelle = self.module.add_global(ty, None, "moo.fehler.stelle");
        stelle.set_linkage(inkwell::module::Linkage::Private);
        stelle.set_constant(true);
        stelle.set_initializer(&ty.const_named_struct(&[
            funktion.into(), aufruf_ptr.into(), datei.into(), i64t.const_int(zeile as u64, false).into(),
        ]));
        let pending = self.builder.build_call(self.rt.moo_try_check, &[], "fehler.anstehend")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let pending = match pending {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_check fehlgeschlagen".into()),
        };
        let anstehend = self.builder.build_int_compare(
            inkwell::IntPredicate::NE, pending, self.context.i32_type().const_zero(), "fehler.anstehend.ja",
        ).map_err(|e| e.to_string())?;
        let hinweis = self.context.append_basic_block(function, "fehler.hinweis");
        let weiter = self.context.append_basic_block(function, "fehler.weiter");
        self.builder.build_conditional_branch(anstehend, hinweis, weiter).map_err(|e| e.to_string())?;
        self.builder.position_at_end(hinweis);
        let melden = self.fehler_rt_funktion("moo_fehler_unbehandelt", false);
        self.builder.build_call(melden, &[stelle.as_pointer_value().into()], "").map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(weiter).map_err(|e| e.to_string())?;
        self.builder.position_at_end(weiter);
        Ok(())
    }

    /// Nativer Funktionswert an der Grenze nativ -> dynamisch (ADR: kein
    /// Ersatzwert): nach einem indirekten Aufruf prueft die Laufzeit „Fehler
    /// gesetzt UND Funktionswert nativ (strikt)“. Liefert die Bedingung nur, wenn
    /// es einen Fehlerausgang gibt (lokales fange oder Standardrahmen ausser main);
    /// Moo-Funktionswerte behalten die bisherige Semantik (Bedingung bleibt 0).
    fn nativer_wert_fehler(&mut self, fn_val: StructValue<'ctx>)
        -> Result<Option<inkwell::values::IntValue<'ctx>>, String>
    {
        if !self.nativer_wert_ausgang_vorhanden() { return Ok(None); }
        let pruef = match self.module.get_function("moo_func_nativ_fehler") {
            Some(f) => f,
            None => {
                // MSVC x64: das 16-Byte-MooValue geht indirekt (call_rt_abi).
                let wert_typ: BasicMetadataTypeEnum = if self.windows_x64_abi() {
                    self.context.ptr_type(AddressSpace::default()).into()
                } else {
                    self.mv_type().into()
                };
                self.module.add_function("moo_func_nativ_fehler",
                    self.context.i32_type().fn_type(&[wert_typ], false), None)
            }
        };
        let ergebnis = self.call_rt_abi(pruef, &[fn_val.into()], "nw_fehler")?.try_as_basic_value();
        let ergebnis = match ergebnis {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_func_nativ_fehler fehlgeschlagen".into()),
        };
        let gesetzt = self.builder.build_int_compare(inkwell::IntPredicate::NE, ergebnis,
            self.context.i32_type().const_zero(), "nw_fehler_gesetzt").map_err(|e| e.to_string())?;
        Ok(Some(gesetzt))
    }

    /// P2-T113: Fehlerzustand des aktuellen Threads fuer die nativen
    /// Argument-Adapter. Rueckgabe (Tiefe, Flag), je ungleich 0 genau dann,
    /// wenn gesetzt. ELF/Linux: direkte thread-lokale Loads (local-exec wie
    /// MOO_TLS in moo_runtime.h: je ein %fs:off-Laden) - billiger als der
    /// fruehere Global-Load plus moo_try_check-Aufruf. Andere Ziele (MinGW-GCC: emutls, LLVM dort
    /// natives TLS - nicht verknuepfbar): EIN Aufruf moo_fehler_zustand()
    /// mit Bit 0 = Fehler anstehend, Bit 1 = Handler aktiv.
    fn fehler_zustand_laden(&self, name: &str)
        -> Result<(inkwell::values::IntValue<'ctx>, inkwell::values::IntValue<'ctx>), String> {
        let i32_t = self.context.i32_type();
        let elf_tls = self.module.get_triple().as_str().to_string_lossy().contains("linux");
        if elf_tls {
            let tls_global = |sym: &str| {
                let g = self.module.get_global(sym).unwrap_or_else(|| {
                    let g = self.module.add_global(i32_t, None, sym);
                    g.set_thread_local_mode(Some(inkwell::ThreadLocalMode::LocalExecTLSModel));
                    g
                });
                g.as_pointer_value()
            };
            let tiefe = self.builder.build_load(i32_t, tls_global("moo_try_depth"), "native.arg.handler_tiefe")
                .map_err(|e| e.to_string())?.into_int_value();
            let flag = self.builder.build_load(i32_t, tls_global("moo_error_flag"), name)
                .map_err(|e| e.to_string())?.into_int_value();
            return Ok((tiefe, flag));
        }
        let zustand_fn = match self.module.get_function("moo_fehler_zustand") {
            Some(f) => f,
            None => self.module.add_function("moo_fehler_zustand",
                self.context.i32_type().fn_type(&[], false), None),
        };
        let wert = self.builder.build_call(zustand_fn, &[], name)
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let zustand = match wert {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_fehler_zustand liefert keinen Wert".into()),
        };
        let i32_t = self.context.i32_type();
        let tiefe = self.builder.build_and(zustand, i32_t.const_int(2, false), "native.arg.handler_bit")
            .map_err(|e| e.to_string())?;
        let flag = self.builder.build_and(zustand, i32_t.const_int(1, false), "native.arg.fehler_bit")
            .map_err(|e| e.to_string())?;
        Ok((tiefe, flag))
    }

    fn nativer_wert_ausgang_vorhanden(&self) -> bool {
        if self.bare_mode { return false; }
        let Some(function) = self.current_function else { return false; };
        self.try_stack.iter().any(|bb| bb.get_parent() == Some(function))
            || (self.im_standardrahmen() && !self.in_main_function())
    }

    /// Bricht bei `bedingung` wie der native Direktaufruf ab: eigene Werte und
    /// Statement-Temporaries freigeben, dann zum lokalen fange bzw. zum
    /// Pruefpunkt-Ausgang (aufraeumen-Aktionen, Locals, Rueckkehr mit nichts).
    fn nativer_wert_abbruch(&mut self, bedingung: inkwell::values::IntValue<'ctx>,
        values: &[StructValue<'ctx>]) -> Result<(), String>
    {
        let function = self.current_function.ok_or("Nativer Funktionswert ohne Funktion")?;
        let local_catch = self.try_stack.iter().rev().copied().find(|bb| bb.get_parent() == Some(function));
        let ziel = match local_catch {
            Some(bb) => bb,
            None => self.native_guard_exit_block()?,
        };
        let fehler_bb = self.context.append_basic_block(function, "nw_abbruch");
        let ok_bb = self.context.append_basic_block(function, "nw_ok");
        self.builder.build_conditional_branch(bedingung, fehler_bb, ok_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(fehler_bb);
        let freigeben: Vec<StructValue<'ctx>> = values.iter().rev()
            .chain(self.stmt_owned_context.iter().rev()).copied().collect();
        for value in freigeben {
            self.call_rt_void(self.rt.moo_release, &[value.into()], "rel_nw_abbruch")?;
        }
        self.builder.build_unconditional_branch(ziel).map_err(|e| e.to_string())?;
        self.builder.position_at_end(ok_bb);
        Ok(())
    }

    fn guard_local_expr_error(&mut self, values: &[StructValue<'ctx>]) -> Result<(), String> {
        // Der native Thread-Träger besitzt denselben fangbaren Fehlerkanal.
        if self.bare_mode && !self.native_thread_runtime { return Ok(()); }
        let function = self.current_function.unwrap();
        // Erst binden, dann verzweigen: der Iterator-Borrow auf try_stack darf
        // nicht in den &mut-Arm (native_guard_exit_block) hineinreichen.
        let local_catch = self.try_stack.iter().rev().copied().find(|bb| bb.get_parent() == Some(function));
        let catch_bb = match local_catch {
            Some(bb) => bb,
            // P2-T82: ohne eigenen Handler, aber mit fangbaren nativen Aufrufen:
            // funktionsweiter Pruefpunkt-Ausgang (Locals freigeben, nichts).
            None if self.native_guard_enabled => self.native_guard_exit_block()?,
            None => return Ok(()),
        };
        let pending = self.builder.build_call(self.rt.moo_try_check, &[], "expr_stmt_check")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let pending = match pending {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_check fehlgeschlagen".into()),
        };
        let failed = self.builder.build_int_compare(
            inkwell::IntPredicate::NE, pending, self.context.i32_type().const_zero(),
            "expr_stmt_error",
        ).map_err(|e| e.to_string())?;
        let error_bb = self.context.append_basic_block(function, "expr_stmt_error");
        let ok_bb = self.context.append_basic_block(function, "expr_stmt_ok");
        self.builder.build_conditional_branch(failed, error_bb, ok_bb)
            .map_err(|e| e.to_string())?;
        self.builder.position_at_end(error_bb);
        for value in values.iter().rev().chain(self.stmt_owned_context.iter().rev()) {
            self.call_rt_void(self.rt.moo_release, &[(*value).into()], "rel_failed_expr")?;
        }
        self.builder.build_unconditional_branch(catch_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(ok_bb);
        Ok(())
    }

    /// Erwarteter Handlerzustand der nativen Argumentadapter: mit lokalem Catch
    /// muss ein Handler aktiv sein; in main ohne Catch darf keiner aktiv sein
    /// (der Fehlerausgang beendet main). P2-T74: In einer benannten Funktion
    /// ohne lokalen Catch ist bei Direktform (native_try_direct) ein aktiver
    /// Aufrufer-Handler erlaubt, der Fehler wird flag-basiert propagiert
    /// (native_argument_fatal_exit); andere Formen behalten den alten Vertrag.
    fn native_argument_wrong_depth(
        &self,
        has_depth: inkwell::values::IntValue<'ctx>,
        catch: Option<inkwell::basic_block::BasicBlock<'ctx>>,
        name: &str,
    ) -> Result<inkwell::values::IntValue<'ctx>, String> {
        // P2-T77: Der geliehene Defer-Handler (compile_function_def, borrowed)
        // oeffnet keinen eigenen try-Rahmen; Flag und Tiefe gehoeren dem
        // Aufrufer. Ohne Aufrufer-Handler beendet moo_throw den Prozess, mit
        // Handler springt der Adapter in den geliehenen Block, der mit nichts
        // zurueckkehrt. Eine Tiefenforderung waere hier ein falscher Trap.
        if self.native_flag_only_target(catch) {
            return Ok(self.context.bool_type().const_zero());
        }
        if catch.is_some() {
            return self.builder.build_not(has_depth, name).map_err(|e| e.to_string());
        }
        if self.native_try_direct && !self.in_main_function() {
            return Ok(self.context.bool_type().const_zero());
        }
        Ok(has_depth)
    }

    fn in_main_function(&self) -> bool {
        self.current_function.is_some_and(|f| f.get_name().to_str() == Ok("main"))
    }

    /// Ziele, die nur ein anhaengiges Fehlerflag erreichen und keine eigene
    /// try-Tiefe verlangen: der geliehene Defer-Handler (P2-T77) und der
    /// Skip-Block eines Nicht-Direktform-Systemaufrufs (P2-T78).
    fn native_flag_only_target(&self, catch: Option<inkwell::basic_block::BasicBlock<'ctx>>) -> bool {
        catch.is_some() && (catch == self.defer_borrowed_handler || catch == self.native_skip_target)
    }

    /// Behandelt inkonsistente Handlerzustaende ohne erfundenen Ergebniswert.
    /// Fehlerausgang der nativen Argumentkonvertierung ohne lokalen Catch.
    /// main: wie bisher `return 1` (moo_throw hat den Fehler ausgegeben).
    /// Benannte Funktion mit Direktform (P2-T74): der Fehler ist per moo_throw
    /// gemeldet (Flag bei aktivem Aufrufer-Handler, sonst Prozessende); die
    /// Funktion gibt ihre Locals frei und kehrt wie bei jedem Moo-Fehler
    /// flag-basiert mit `nichts` zurueck, der Aufrufer prueft das Flag nach
    /// dem Statement. Defers gibt es hier nicht (Schranke d: Systemkoerper
    /// nicht in Funktionen mit aufraeumen). Andere Formen (P2-T66 b): moo_throw
    /// beendet den Prozess bei try_depth 0; ein MooValue-Rahmen hat keinen
    /// i32-Return, daher bleibt der Rest fatal abgesichert (llvm.trap).
    /// P2-T78: Nicht-Direktform in benannten Funktionen (und main in versuche:)
    /// kommt hier nicht mehr an: die Adapter springen in den Skip-Block des
    /// Aufrufs (native_skip_target), der Aufruf liefert `nichts`.
    fn native_argument_fatal_exit(&mut self) -> Result<(), String> {
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        if function.get_name().to_str() == Ok("main") {
            self.builder.build_return(Some(&self.context.i32_type().const_int(1, false)))
                .map_err(|e| e.to_string())?;
            return Ok(());
        }
        if self.native_try_direct {
            // P2-T77: statische Defers wie in compile_return VOR dem Freigeben
            // der Locals ausfuehren (Aktionen lesen Locals); emit_defers sichert
            // den anhaengigen Erstfehler und wirft ihn danach erneut. Runtime-
            // Frame und geliehener Handler kommen hier nicht an: dort liegt ein
            // lokaler Catch auf dem try_stack (native_local_catch).
            self.emit_defers()?;
            self.release_function_locals()?;
            if function.get_type().get_return_type().is_none() {
                self.builder.build_return(None).map_err(|e| e.to_string())?;
            } else {
                let none = self.call_rt(self.rt.moo_none, &[], "native.arg.none")?;
                self.builder.build_return(Some(&none)).map_err(|e| e.to_string())?;
            }
            return Ok(());
        }
        let intrinsic = inkwell::intrinsics::Intrinsic::find("llvm.trap").ok_or("llvm.trap fehlt")?;
        let trap = intrinsic.get_declaration(&self.module, &[]).ok_or("llvm.trap-Deklaration fehlt")?;
        self.builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
        self.builder.build_unreachable().map_err(|e| e.to_string())?;
        Ok(())
    }

    fn native_argument_state_failure(
        &mut self,
        has_depth: inkwell::values::IntValue<'ctx>,
        has_error: inkwell::values::IntValue<'ctx>,
        target: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    ) -> Result<(), String> {
        if let Some(catch) = target {
            // P2-T77/P2-T78: geliehener Defer-Handler bzw. Skip-Block: nur ein
            // anhaengiger Fehler fuehrt hierher (keine Tiefenforderung), er
            // bleibt beim Aufrufer.
            if self.native_flag_only_target(target) {
                self.builder.build_unconditional_branch(catch).map_err(|e| e.to_string())?;
                return Ok(());
            }
            let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
            let fatal = self.context.append_basic_block(function, "native.arg.invalid_handler_state");
            let can_catch = self.builder.build_and(has_depth, has_error, "native.arg.can_catch")
                .map_err(|e| e.to_string())?;
            self.builder.build_conditional_branch(can_catch, catch, fatal).map_err(|e| e.to_string())?;
            self.builder.position_at_end(fatal);
            let intrinsic = inkwell::intrinsics::Intrinsic::find("llvm.trap").ok_or("llvm.trap fehlt")?;
            let trap = intrinsic.get_declaration(&self.module, &[]).ok_or("llvm.trap-Deklaration fehlt")?;
            self.builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
            self.builder.build_unreachable().map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        Ok(())
    }

    /// Validiert ein dynamisches Number-Argument vor jeder fptosi-Konversion.
    fn compile_native_integer_argument(&mut self, expr: &Expr, ty: crate::codegen_native::NType, catch: Option<inkwell::basic_block::BasicBlock<'ctx>>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        use crate::codegen_native::NType;
        let (minimum, maximum, type_name) = match ty {
            NType::I32 => (i32::MIN as f64, i32::MAX as f64, "i32"),
            NType::U32 => (u32::MIN as f64, u32::MAX as f64, "u32"),
            NType::I16 => (i16::MIN as f64, i16::MAX as f64, "i16"),
            NType::U16 => (u16::MIN as f64, u16::MAX as f64, "u16"),
            NType::I8 => (i8::MIN as f64, i8::MAX as f64, "i8"),
            NType::U8 => (u8::MIN as f64, u8::MAX as f64, "u8"),
            _ => return Err("Nicht unterstuetztes natives Integerargument".into()),
        };
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let state_ok = self.context.append_basic_block(function, "native.arg.state_ok");
        let state_error = self.context.append_basic_block(function, "native.arg.state_error");
        let (depth_value, pending) = self.fehler_zustand_laden("native.arg.zustand")?;
        let has_depth = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_value,
            self.context.i32_type().const_zero(), "native.arg.has_handler").map_err(|e| e.to_string())?;
        let has_error = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending,
            self.context.i32_type().const_zero(), "native.arg.has_error").map_err(|e| e.to_string())?;
        let wrong_depth = self.native_argument_wrong_depth(has_depth, catch, "native.arg.no_handler")?;
        let invalid_state = self.builder.build_or(wrong_depth, has_error, "native.arg.invalid_state")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(invalid_state, state_error, state_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(state_error);
        self.native_argument_state_failure(has_depth, has_error, catch)?;
        self.builder.position_at_end(state_ok);
        let value = self.compile_expr(expr)?;
        let value_ok = self.context.append_basic_block(function, "native.arg.value_ok");
        let value_error = self.context.append_basic_block(function, "native.arg.value_error");
        let (depth_after, pending_after) = self.fehler_zustand_laden("native.arg.zustand_after")?;
        let has_depth_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_after,
            self.context.i32_type().const_zero(), "native.arg.has_handler_after").map_err(|e| e.to_string())?;
        let has_error_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending_after,
            self.context.i32_type().const_zero(), "native.arg.has_error_after").map_err(|e| e.to_string())?;
        let wrong_depth_after = self.native_argument_wrong_depth(has_depth_after, catch, "native.arg.no_handler_after")?;
        let bad_after = self.builder.build_or(wrong_depth_after, has_error_after, "native.arg.bad_after")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(bad_after, value_error, value_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(value_error);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.arg.release_pending")?;
        self.native_argument_state_failure(has_depth_after, has_error_after, catch)?;
        self.builder.position_at_end(value_ok);
        let tag_value = self.builder.build_extract_value(value, 0, "native.arg.tag")
            .map_err(|e| e.to_string())?.into_int_value();
        let raw = self.builder.build_extract_value(value, 1, "native.arg.bits")
            .map_err(|e| e.to_string())?.into_int_value();
        let range_bb = self.context.append_basic_block(function, "native.arg.range");
        let convert_bb = self.context.append_basic_block(function, "native.arg.convert");
        let success_bb = self.context.append_basic_block(function, "native.arg.valid");
        let failure_bb = self.context.append_basic_block(function, "native.arg.invalid");
        let is_number = self.builder.build_int_compare(
            inkwell::IntPredicate::EQ, tag_value, self.context.i64_type().const_zero(), "native.arg.is_number"
        ).map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(is_number, range_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(range_bb);
        let number = self.builder.build_bit_cast(raw, self.context.f64_type(), "native.arg.number")
            .map_err(|e| e.to_string())?.into_float_value();
        let lower = self.builder.build_float_compare(
            inkwell::FloatPredicate::OGE, number, self.context.f64_type().const_float(minimum), "native.arg.min"
        ).map_err(|e| e.to_string())?;
        let upper = self.builder.build_float_compare(
            inkwell::FloatPredicate::OLE, number, self.context.f64_type().const_float(maximum), "native.arg.max"
        ).map_err(|e| e.to_string())?;
        let in_range = self.builder.build_and(lower, upper, "native.arg.in_range").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(in_range, convert_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(convert_bb);
        let integer = if ty == NType::U32 {
            self.builder.build_float_to_unsigned_int(number, self.context.i32_type(), "native.arg.u32")
        } else {
            self.builder.build_float_to_signed_int(number, self.context.i32_type(), "native.arg.i32")
        }.map_err(|e| e.to_string())?;
        let roundtrip = if ty == NType::U32 {
            self.builder.build_unsigned_int_to_float(integer, self.context.f64_type(), "native.arg.roundtrip")
        } else {
            self.builder.build_signed_int_to_float(integer, self.context.f64_type(), "native.arg.roundtrip")
        }.map_err(|e| e.to_string())?;
        let integral = self.builder.build_float_compare(
            inkwell::FloatPredicate::OEQ, number, roundtrip, "native.arg.integral"
        ).map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(integral, success_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(failure_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.arg.release_error")?;
        let message = self.make_global_str(&format!("Native Funktion erwartet eine {type_name}-Ganzzahl"), "native_arg_error")?;
        let error = self.call_rt(self.rt.moo_string_new, &[message.into()], "native.arg.error")?;
        self.call_rt_void(self.rt.moo_throw, &[error.into()], "native.arg.throw")?;
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(success_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.arg.release")?;
        if matches!(ty, NType::I8 | NType::U8) {
            self.builder.build_int_truncate(integer, self.context.i8_type(), "native.arg.i8").map_err(|e| e.to_string())
        } else if matches!(ty, NType::I16 | NType::U16) {
            self.builder.build_int_truncate(integer, self.context.i16_type(), "native.arg.i16").map_err(|e| e.to_string())
        } else {
            Ok(integer)
        }
    }

    /// P2-T34: Konvertiert dynamische Wahrheitswerte (MOO_BOOL) an der nativen
    /// Grenze zu i1. Andere Tags (Zahl, Text, nichts) sind ein Moo-Fehler;
    /// es gibt keine implizite Wahrheitsumdeutung (ADR §14: Typpruefung).
    fn compile_native_bool_argument(&mut self, expr: &Expr, catch: Option<inkwell::basic_block::BasicBlock<'ctx>>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let state_ok = self.context.append_basic_block(function, "native.arg.state_ok");
        let state_error = self.context.append_basic_block(function, "native.arg.state_error");
        let (depth_value, pending) = self.fehler_zustand_laden("native.arg.zustand")?;
        let has_depth = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_value,
            self.context.i32_type().const_zero(), "native.arg.has_handler").map_err(|e| e.to_string())?;
        let has_error = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending,
            self.context.i32_type().const_zero(), "native.arg.has_error").map_err(|e| e.to_string())?;
        let wrong_depth = self.native_argument_wrong_depth(has_depth, catch, "native.arg.no_handler")?;
        let invalid_state = self.builder.build_or(wrong_depth, has_error, "native.arg.invalid_state")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(invalid_state, state_error, state_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(state_error);
        self.native_argument_state_failure(has_depth, has_error, catch)?;
        self.builder.position_at_end(state_ok);
        let value = self.compile_expr(expr)?;
        let value_ok = self.context.append_basic_block(function, "native.arg.value_ok");
        let value_error = self.context.append_basic_block(function, "native.arg.value_error");
        let (depth_after, pending_after) = self.fehler_zustand_laden("native.arg.zustand_after")?;
        let has_depth_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_after,
            self.context.i32_type().const_zero(), "native.arg.has_handler_after").map_err(|e| e.to_string())?;
        let has_error_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending_after,
            self.context.i32_type().const_zero(), "native.arg.has_error_after").map_err(|e| e.to_string())?;
        let wrong_depth_after = self.native_argument_wrong_depth(has_depth_after, catch, "native.arg.no_handler_after")?;
        let bad_after = self.builder.build_or(wrong_depth_after, has_error_after, "native.arg.bad_after")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(bad_after, value_error, value_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(value_error);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.arg.release_pending")?;
        self.native_argument_state_failure(has_depth_after, has_error_after, catch)?;
        self.builder.position_at_end(value_ok);
        let tag_value = self.builder.build_extract_value(value, 0, "native.arg.tag")
            .map_err(|e| e.to_string())?.into_int_value();
        let raw = self.builder.build_extract_value(value, 1, "native.arg.bits")
            .map_err(|e| e.to_string())?.into_int_value();
        let success_bb = self.context.append_basic_block(function, "native.bool.valid");
        let failure_bb = self.context.append_basic_block(function, "native.bool.invalid");
        // MOO_BOOL = 2 (compiler/runtime/moo_runtime.h, MooValue.tag).
        let is_bool = self.builder.build_int_compare(inkwell::IntPredicate::EQ, tag_value,
            self.context.i64_type().const_int(2, false), "native.bool.is_bool").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(is_bool, success_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(failure_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.bool.release_error")?;
        let message = self.make_global_str("Native Funktion erwartet einen Wahrheitswert (bool)", "native_bool_error")?;
        let error = self.call_rt(self.rt.moo_string_new, &[message.into()], "native.bool.error")?;
        self.call_rt_void(self.rt.moo_throw, &[error.into()], "native.bool.throw")?;
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(success_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.bool.release")?;
        self.builder.build_int_compare(inkwell::IntPredicate::NE, raw, self.context.i64_type().const_zero(), "native.bool.flag")
            .map_err(|e| e.to_string())
    }

    /// P2-T50 B5 (Plan bb3878ef E1/E5): Pufferargument an der dyn->nativ-Grenze.
    /// Stufe 1 nur als Variable: ihr Wert wird geliehen (+1 bis nach dem Aufruf)
    /// und der Speicher fuer die Dauer des Aufrufs gepinnt (moo_buffer_native_pin):
    /// Datenzeiger stabil, Laengenaenderung gesperrt. Liefert {daten, laenge} fuer
    /// den nativen Parameter sowie Pin und Wert fuer das Loesen nach dem Aufruf.
    /// Falscher Typ: die Runtime wirft; bereits gepinnte Pufferargumente dieses
    /// Aufrufs werden geloest, dann derselbe Ausgang wie bei einem ungueltigen
    /// Skalarargument (catch bzw. native_argument_fatal_exit).
    fn compile_native_buffer_argument(
        &mut self,
        expr: &Expr,
        element: crate::native_system::PufferArt,
        schreibbar: bool,
        frueher: &[(inkwell::values::PointerValue<'ctx>, StructValue<'ctx>)],
        catch: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    ) -> Result<(StructValue<'ctx>, inkwell::values::PointerValue<'ctx>, StructValue<'ctx>), String> {
        use crate::codegen_native::NType;
        use crate::native_system::PufferArt;
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        // Skalar: MooBufferScalarType-Code (i32, compiler/runtime/moo_buffer.h);
        // P2-T48 D: Struktur: Laufzeit-ID (i64, NativeStructRegistry::struktur_id).
        // P2-T83 A1/B2: beides an moo_native_leihen (Puffer, Tensor, Frame).
        let (code, id): (u64, u64) = match &element {
            PufferArt::Skalar(t) => {
                let code: u64 = match t {
                    NType::I8 => 1, NType::U8 => 2, NType::I16 => 3, NType::U16 => 4, NType::I32 => 5,
                    NType::U32 => 6, NType::I64 => 7, NType::U64 => 8, NType::F32 => 9, NType::F64 => 10,
                    NType::Bool => return Err("puffer[bool] ist nicht freigegeben".into()),
                };
                (code, 0)
            }
            PufferArt::Griff { .. } => return Err("Griffargument im Pufferpfad (interner Fehler)".into()),
            PufferArt::Wert(_) => return Err("Strukturwert im Pufferpfad (interner Fehler)".into()),
            PufferArt::Struktur(name) => {
                let id = self.native_structs.struktur_id(name)
                    .ok_or_else(|| format!("puffer[{name}]: Struktur-Layout fehlt"))?;
                (0, id)
            }
        };
        let i32t = self.context.i32_type();
        let i64t = self.context.i64_type();
        let pin_fn = match self.module.get_function("moo_native_leihen") {
            Some(f) => f,
            None => self.module.add_function("moo_native_leihen", ptr_type.fn_type(
                &[ptr_type.into(), i32t.into(), i64t.into(), i32t.into(), ptr_type.into(), ptr_type.into()], false,
            ), None),
        };
        let wert = self.compile_expr(expr)?;
        let wert_slot = self.entry_alloca(self.mv_type(), "native.puffer.wert")?;
        self.builder.build_store(wert_slot, wert).map_err(|e| e.to_string())?;
        let daten_slot = self.entry_alloca(ptr_type, "native.puffer.daten")?;
        let laenge_slot = self.entry_alloca(self.context.i64_type(), "native.puffer.laenge")?;
        let pin = self.builder.build_call(pin_fn, &[
            wert_slot.into(), i32t.const_int(code, false).into(), i64t.const_int(id, false).into(),
            i32t.const_int(u64::from(schreibbar), false).into(),
            daten_slot.into(), laenge_slot.into(),
        ], "native.puffer.pin").map_err(|e| e.to_string())?.try_as_basic_value();
        let pin = match pin {
            inkwell::values::ValueKind::Basic(value) => value.into_pointer_value(),
            _ => return Err("moo_buffer_native_pin liefert keinen Wert".into()),
        };
        let fehlt = self.builder.build_is_null(pin, "native.puffer.fehlt").map_err(|e| e.to_string())?;
        let ok_bb = self.context.append_basic_block(function, "native.puffer.ok");
        let fehler_bb = self.context.append_basic_block(function, "native.puffer.fehler");
        self.builder.build_conditional_branch(fehlt, fehler_bb, ok_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(fehler_bb);
        self.call_rt_void(self.rt.moo_release, &[wert.into()], "native.puffer.release_fehler")?;
        self.native_buffer_unpin(frueher)?;
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(ok_bb);
        let daten = self.builder.build_load(ptr_type, daten_slot, "native.puffer.daten")
            .map_err(|e| e.to_string())?;
        let laenge = self.builder.build_load(self.context.i64_type(), laenge_slot, "native.puffer.laenge")
            .map_err(|e| e.to_string())?;
        let typ = self.context.struct_type(&[ptr_type.into(), self.context.i64_type().into()], false);
        let aggregat = self.builder.build_insert_value(typ.get_undef(), daten, 0, "native.puffer.agg")
            .map_err(|e| e.to_string())?;
        let aggregat = self.builder.build_insert_value(aggregat, laenge, 1, "native.puffer.agg")
            .map_err(|e| e.to_string())?.into_struct_value();
        Ok((aggregat, pin, wert))
    }

    /// P2-T85 A2 (Plan 983e8a9c E4): aktiver versuche:-Handler der AKTUELLEN
    /// Funktion (Handler umgebender Rahmen zaehlen fuer Lambdas/Methoden nicht).
    fn rahmen_hat_handler(&self) -> bool {
        self.try_stack.iter().any(|bb| bb.get_parent() == self.current_function)
    }

    /// P2-T85 B1: dynamischer Standardrahmen (Funktion, Methode, Lambda).
    fn im_standardrahmen(&self) -> bool {
        matches!(self.rahmen_art, RahmenArt::Funktion | RahmenArt::Methode | RahmenArt::Lambda)
    }

    /// P2-T82/T85 B2: Pruefpunkt-Ausgang fuer einen Rahmen mit diesen Aufrufen
    /// (nicht bare/profiling, kein Runtime-Defer-Frame). P2-T92: statische
    /// aufraeumen-Aktionen sind erlaubt (Ausgang je Stelle, native_guard_exit_block).
    fn native_guard_fuer(&self, calls: &std::collections::HashSet<String>) -> bool {
        !self.bare_mode && !self.profiling && !self.defer_runtime
            && calls.iter().any(|callee| self.native_call_fangbar(callee))
    }

    /// P2-T49 (Plan 38c59c64 B1): konstante Laufzeitbeschreibung v2 einer Struktur
    /// (MooStrukturTyp, compiler/runtime/moo_struct_typed.h) als private Konstante
    /// `__moo_struktur.<Name>`, bei erstem Bedarf (rekursiv fuer verschachtelte).
    /// Programme ohne Grenznutzung bekommen keine (E8). Layout: {i32 version,
    /// i32 feld_anzahl, i64 typ_id, {ptr, isize, isize, isize, ptr} basis, ptr felder};
    /// MooStrukturFeld {ptr, i32, i32, i8, i8, i8, i8, i32, ptr}; MooStructField
    /// {ptr, isize, isize}. Natuerliches LLVM-Layout = C-Layout auf allen Zielen.
    fn struktur_deskriptor(&mut self, name: &str) -> Result<PointerValue<'ctx>, String> {
        if let Some(p) = self.struktur_deskriptoren.get(name) { return Ok(*p); }
        self.native_structs.boxbar(name)
            .map_err(|g| format!("Struktur '{name}' ist in dynamischem Moo nicht als Wert nutzbar: {g}"))?;
        let (felder, blaetter, groesse, ausrichtung, isize_t) = {
            let target = self.target_data.as_ref().ok_or("Ziel-Datenlayout fuer Struktur fehlt")?;
            let layout = self.native_structs.layout(name).ok_or_else(|| format!("Struktur '{name}': Layout fehlt"))?;
            (
                self.native_structs.laufzeit_felder(self.context, target, name)?,
                self.native_structs.laufzeit_blaetter(self.context, target, name)?,
                layout.size,
                u64::from(layout.alignment),
                self.context.ptr_sized_int_type(target, None),
            )
        };
        let typ_id = self.native_structs.struktur_id(name)
            .ok_or_else(|| format!("Struktur '{name}': Identitaet fehlt"))?;
        let ptr_t = self.context.ptr_type(inkwell::AddressSpace::default());
        let i8t = self.context.i8_type();
        let i32t = self.context.i32_type();
        let i64t = self.context.i64_type();
        let u32_von = |wert: u64, was: &str| u32::try_from(wert)
            .map_err(|_| format!("Struktur '{name}': {was} zu gross fuer die Laufzeitbeschreibung"));
        let symbol = format!("__moo_struktur.{name}");
        let name_ptr = self.private_ctext(&format!("{symbol}.name"), name);
        // Flache Blaetter (MooStructField).
        let blatt_t = self.context.struct_type(&[ptr_t.into(), isize_t.into(), isize_t.into()], false);
        let mut blatt_werte = Vec::with_capacity(blaetter.len());
        for (i, (pfad, versatz, laenge)) in blaetter.iter().enumerate() {
            let pfad_ptr = self.private_ctext(&format!("{symbol}.blatt.{i}"), pfad);
            blatt_werte.push(blatt_t.const_named_struct(&[
                pfad_ptr.into(), isize_t.const_int(*versatz, false).into(), isize_t.const_int(*laenge, false).into(),
            ]));
        }
        let blatt_arr = blatt_t.const_array(&blatt_werte);
        let blatt_g = self.module.add_global(blatt_arr.get_type(), None, &format!("{symbol}.blaetter"));
        blatt_g.set_initializer(&blatt_arr);
        blatt_g.set_constant(true);
        blatt_g.set_linkage(inkwell::module::Linkage::Private);
        // Deklarierte Felder (MooStrukturFeld), verschachtelte Beschreibungen zuerst.
        let feld_t = self.context.struct_type(&[
            ptr_t.into(), i32t.into(), i32t.into(), i8t.into(), i8t.into(), i8t.into(), i8t.into(), i32t.into(), ptr_t.into(),
        ], false);
        let mut feld_werte = Vec::with_capacity(felder.len());
        for (i, feld) in felder.iter().enumerate() {
            let innen = match &feld.struktur {
                Some(s) => self.struktur_deskriptor(s)?,
                None => ptr_t.const_null(),
            };
            let feld_name = self.private_ctext(&format!("{symbol}.feld.{i}"), &feld.name);
            feld_werte.push(feld_t.const_named_struct(&[
                feld_name.into(),
                i32t.const_int(u64::from(u32_von(feld.versatz, "Feldversatz")?), false).into(),
                i32t.const_int(u64::from(u32_von(feld.groesse, "Feldgroesse")?), false).into(),
                i8t.const_int(u64::from(feld.art), false).into(),
                i8t.const_int(u64::from(feld.element), false).into(),
                i8t.const_int(u64::from(feld.skalar), false).into(),
                i8t.const_zero().into(),
                i32t.const_int(u64::from(u32_von(feld.anzahl, "Elementanzahl")?), false).into(),
                innen.into(),
            ]));
        }
        let feld_arr = feld_t.const_array(&feld_werte);
        let feld_g = self.module.add_global(feld_arr.get_type(), None, &format!("{symbol}.felder"));
        feld_g.set_initializer(&feld_arr);
        feld_g.set_constant(true);
        feld_g.set_linkage(inkwell::module::Linkage::Private);
        let basis_t = self.context.struct_type(&[
            ptr_t.into(), isize_t.into(), isize_t.into(), isize_t.into(), ptr_t.into(),
        ], false);
        let typ_t = self.context.struct_type(&[i32t.into(), i32t.into(), i64t.into(), basis_t.into(), ptr_t.into()], false);
        let basis = basis_t.const_named_struct(&[
            name_ptr.into(), isize_t.const_int(groesse, false).into(), isize_t.const_int(ausrichtung, false).into(),
            isize_t.const_int(blaetter.len() as u64, false).into(), blatt_g.as_pointer_value().into(),
        ]);
        let typ = typ_t.const_named_struct(&[
            i32t.const_int(2, false).into(),
            i32t.const_int(u64::from(u32_von(felder.len() as u64, "Feldanzahl")?), false).into(),
            i64t.const_int(typ_id, false).into(),
            basis.into(),
            feld_g.as_pointer_value().into(),
        ]);
        let typ_g = self.module.add_global(typ_t, None, &symbol);
        typ_g.set_initializer(&typ);
        typ_g.set_constant(true);
        typ_g.set_linkage(inkwell::module::Linkage::Private);
        let zeiger = typ_g.as_pointer_value();
        self.struktur_deskriptoren.insert(name.to_string(), zeiger);
        Ok(zeiger)
    }

    /// P2-T49: private, nullterminierte Textkonstante (Namen der Laufzeitbeschreibung).
    fn private_ctext(&self, symbol: &str, text: &str) -> PointerValue<'ctx> {
        let wert = self.context.const_string(text.as_bytes(), true);
        let g = self.module.add_global(wert.get_type(), None, symbol);
        g.set_initializer(&wert);
        g.set_constant(true);
        g.set_linkage(inkwell::module::Linkage::Private);
        g.as_pointer_value()
    }

    /// P2-T49 B3: ABI-ausgerichteter, genullter Strukturslot (Eintrittsblock).
    fn native_struct_slot(&mut self, name: &str, label: &str) -> Result<PointerValue<'ctx>, String> {
        let layout = self.native_structs.layout(name).ok_or_else(|| format!("Struktur '{name}': Layout fehlt"))?;
        let (ty, groesse, ausrichtung) = (layout.llvm_type, layout.size, layout.alignment);
        let slot = self.entry_alloca(ty, label)?;
        if let Some(alloca) = slot.as_instruction() {
            alloca.set_alignment(ausrichtung).map_err(|e| e.to_string())?;
        }
        self.builder.build_memset(slot, ausrichtung, self.context.i8_type().const_zero(),
            self.context.i64_type().const_int(groesse, false)).map_err(|e| e.to_string())?;
        Ok(slot)
    }

    /// P2-T49 B3 (Plan 38c59c64): Strukturwert an der dyn->nativ-Grenze. Beliebiger
    /// Ausdruck; moo_struct_unbox_value prueft typ_id und kopiert in eine exklusive,
    /// ABI-ausgerichtete Aufruferkopie. Der MooValue wird sofort freigegeben (keine
    /// Leihe; native Aenderungen wirken nie auf die Box, E1). Fehler: derselbe
    /// Ausgang wie ein ungueltiges Skalarargument; Pins/Griffe gibt es zu diesem
    /// Zeitpunkt noch nicht (sie folgen nach allen Wertargumenten).
    fn compile_native_struct_argument(
        &mut self,
        expr: &Expr,
        name: &str,
        catch: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    ) -> Result<PointerValue<'ctx>, String> {
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        let deskriptor = self.struktur_deskriptor(name)?;
        let unbox = match self.module.get_function("moo_struct_unbox_value") {
            Some(f) => f,
            None => self.module.add_function("moo_struct_unbox_value", self.context.i32_type().fn_type(
                &[ptr_type.into(), ptr_type.into(), ptr_type.into()], false), None),
        };
        let wert = self.compile_expr(expr)?;
        let wert_slot = self.entry_alloca(self.mv_type(), "native.struktur.wert")?;
        self.builder.build_store(wert_slot, wert).map_err(|e| e.to_string())?;
        let kopie = self.native_struct_slot(name, "native.struktur.kopie")?;
        let ok = self.builder.build_call(unbox, &[wert_slot.into(), deskriptor.into(), kopie.into()],
            "native.struktur.unbox").map_err(|e| e.to_string())?.try_as_basic_value();
        let ok = match ok {
            inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
            _ => return Err("moo_struct_unbox_value liefert keinen Wert".into()),
        };
        self.call_rt_void(self.rt.moo_release, &[wert.into()], "native.struktur.release")?;
        let fehlt = self.builder.build_int_compare(inkwell::IntPredicate::EQ, ok,
            self.context.i32_type().const_zero(), "native.struktur.fehlt").map_err(|e| e.to_string())?;
        let ok_bb = self.context.append_basic_block(function, "native.struktur.ok");
        let fehler_bb = self.context.append_basic_block(function, "native.struktur.fehler");
        self.builder.build_conditional_branch(fehlt, fehler_bb, ok_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(fehler_bb);
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(ok_bb);
        Ok(kopie)
    }

    /// P2-T49 B3: Strukturergebnis (sret-Puffer) als neue MOO_STRUCT-Box.
    fn native_struct_box(&mut self, zeiger: PointerValue<'ctx>, name: &str) -> Result<StructValue<'ctx>, String> {
        let deskriptor = self.struktur_deskriptor(name)?;
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        let boxer = match self.module.get_function("moo_struct_box_value") {
            Some(f) => f,
            None => self.module.add_function("moo_struct_box_value", self.context.void_type().fn_type(
                &[ptr_type.into(), ptr_type.into(), ptr_type.into()], false), None),
        };
        let out = self.entry_alloca(self.mv_type(), "native.struktur.box")?;
        self.builder.build_call(boxer, &[deskriptor.into(), zeiger.into(), out.into()], "")
            .map_err(|e| e.to_string())?;
        Ok(self.builder.build_load(self.mv_type(), out, "native.struktur.boxwert")
            .map_err(|e| e.to_string())?.into_struct_value())
    }

    /// P2-T49 B3 (Review #38118): Strukturergebnis eines nativen Aufrufs nur
    /// boxen, wenn der Aufruf kein Fehlerflag hinterlassen hat. Auf dem
    /// Fehlerpfad entsteht keine Heapbox (ein OOM beim Boxen koennte sonst den
    /// Originalfehler ueberschreiben); das Ergebnis ist dann `nichts`, die
    /// bestehende Statement-Pruefung propagiert den offenen Fehler.
    fn native_struct_box_nach_pruefung(
        &mut self, zeiger: PointerValue<'ctx>, name: &str,
    ) -> Result<StructValue<'ctx>, String> {
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let flag = match self.builder.build_call(self.rt.moo_try_check, &[], "native.struktur.pruef")
            .map_err(|e| e.to_string())?.try_as_basic_value()
        {
            inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
            _ => return Err("moo_try_check liefert keinen Wert".into()),
        };
        let offen = self.builder.build_int_compare(inkwell::IntPredicate::NE, flag,
            flag.get_type().const_zero(), "native.struktur.fehler_offen").map_err(|e| e.to_string())?;
        let ergebnis = self.entry_alloca(self.mv_type(), "native.struktur.ergebnis.slot")?;
        let box_bb = self.context.append_basic_block(function, "native.struktur.boxen");
        let fehler_bb = self.context.append_basic_block(function, "native.struktur.ohne_box");
        let weiter_bb = self.context.append_basic_block(function, "native.struktur.weiter");
        self.builder.build_conditional_branch(offen, fehler_bb, box_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(box_bb);
        let boxed = self.native_struct_box(zeiger, name)?;
        self.builder.build_store(ergebnis, boxed).map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(weiter_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(fehler_bb);
        let none = self.call_rt(self.rt.moo_none, &[], "native.struktur.nichts")?;
        self.builder.build_store(ergebnis, none).map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(weiter_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(weiter_bb);
        Ok(self.builder.build_load(self.mv_type(), ergebnis, "native.struktur.ergebnis")
            .map_err(|e| e.to_string())?.into_struct_value())
    }

    /// P2-T49 B4 (Plan 38c59c64 E6): `Name(a, b, ...)` in dynamischem Code erzeugt
    /// eine Box. Stelligkeit wie native_system.rs construct_struct_into (keine
    /// Argumente = Nullwert, sonst genau ein Wert je Feld; statisch geprueft); die
    /// Werte prueft moo_struct_new_values wie das Feldschreiben (fangbar).
    fn compile_struktur_konstruktor(&mut self, name: &str, args: &[Expr], guard_args: bool) -> Result<StructValue<'ctx>, String> {
        if self.bare_mode {
            return Err(format!("Struktur '{name}': Strukturwerte in dynamischem Moo brauchen die hosted Runtime (nicht im Bare-Profil)"));
        }
        let feldzahl = self.native_structs.layout(name).map(|l| l.fields.len())
            .ok_or_else(|| format!("Struktur '{name}': Layout fehlt"))?;
        if !args.is_empty() && args.len() != feldzahl {
            return Err(format!("Struktur '{name}' erwartet {feldzahl} Werte (oder keinen fuer den Nullwert), bekommen {}", args.len()));
        }
        let deskriptor = self.struktur_deskriptor(name)?;
        let anzahl = u32::try_from(args.len()).map_err(|_| "zu viele Strukturargumente".to_string())?;
        let feld_t = self.mv_type().array_type(anzahl.max(1));
        let feld = self.entry_alloca(feld_t, "struktur.argumente")?;
        let i32t = self.context.i32_type();
        let mut werte = Vec::with_capacity(args.len());
        for (i, arg) in args.iter().enumerate() {
            let base = self.stmt_owned_context.len();
            if self.native_value_temps() { self.stmt_owned_context.extend_from_slice(&werte); }
            let ergebnis = self.compile_expr(arg);
            self.stmt_owned_context.truncate(base);
            let wert = ergebnis?;
            werte.push(wert);
            if guard_args { self.guard_local_expr_error(&werte)?; }
            let platz = unsafe {
                self.builder.build_in_bounds_gep(feld_t, feld,
                    &[i32t.const_zero(), i32t.const_int(i as u64, false)], "struktur.argument")
            }.map_err(|e| e.to_string())?;
            self.builder.build_store(platz, wert).map_err(|e| e.to_string())?;
        }
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        let neu = match self.module.get_function("moo_struct_new_values") {
            Some(f) => f,
            None => self.module.add_function("moo_struct_new_values", self.context.void_type().fn_type(
                &[ptr_type.into(), ptr_type.into(), i32t.into(), ptr_type.into()], false), None),
        };
        let out = self.entry_alloca(self.mv_type(), "struktur.neu")?;
        self.builder.build_call(neu, &[deskriptor.into(), feld.into(),
            i32t.const_int(u64::from(anzahl), false).into(), out.into()], "").map_err(|e| e.to_string())?;
        for wert in &werte {
            self.call_rt_void(self.rt.moo_release, &[(*wert).into()], "struktur.arg_release")?;
        }
        Ok(self.builder.build_load(self.mv_type(), out, "struktur.wert")
            .map_err(|e| e.to_string())?.into_struct_value())
    }


    /// P2-T86: Tabellen-/Closure-Logik wird als normales Moo kompiliert.
    /// LLVM-TLS haelt nur den Zustand; kein C-Registry und kein C-Signaturparser.
    fn registration_library(&mut self) -> Result<inkwell::values::GlobalValue<'ctx>, String> {
        const STATE: &str = "__moo_registered.state";
        if let Some(global) = self.module.get_global(STATE) { return Ok(global); }
        if self.bare_mode || self.module.get_triple().as_str().to_string_lossy().starts_with("wasm") {
            return Err("Registrierte dynamische Rueckrufe brauchen eine hosted 64-Bit-Laufzeit".into());
        }
        let target = self.target_data.as_ref().ok_or("Datenlayout fehlt")?;
        if target.get_pointer_byte_size(None) != 8 {
            return Err("Registrierte dynamische Rueckrufe brauchen 64-Bit-Zeiger".into());
        }
        let tokens = crate::lexer::Lexer::new().tokenize(include_str!("../runtime/moo_rueckruf.moos"))
            .map_err(|e| format!("Interne Rueckruf-Bibliothek: {e:?}"))?;
        let library = crate::parser::Parser::new(tokens).parse()
            .map_err(|e| format!("Interne Rueckruf-Bibliothek: {e:?}"))?;
        for stmt in &library.statements {
            let Stmt::FunctionDef { name, params, .. } = stmt else { return Err("Rueckruf-Bibliothek darf nur Funktionen definieren".into()); };
            if self.module.get_function(name).is_some() {
                return Err(format!("Reservierter interner Rueckrufname '{name}' ist bereits definiert"));
            }
            let params: Vec<BasicMetadataTypeEnum<'ctx>> = params.iter().map(|_| self.mv_type().into()).collect();
            self.module.add_function(name, self.mv_type().fn_type(&params, false), Some(inkwell::module::Linkage::Internal));
            self.dyn_named_functions.insert(name.clone());
        }
        let global = self.module.add_global(self.mv_type(), None, STATE);
        global.set_linkage(inkwell::module::Linkage::Internal);
        global.set_thread_local_mode(Some(inkwell::ThreadLocalMode::GeneralDynamicTLSModel));
        global.set_initializer(&self.registration_none());
        // Die Bibliothek wird auch innerhalb eines versuche:-Aufrufs erstmals
        // benoetigt. Dessen Catch-Block darf nicht in eine fremde Funktion geraten.
        let previous_try = std::mem::take(&mut self.try_stack);
        for stmt in &library.statements {
            if let Stmt::FunctionDef { name, params, defaults, body, decorators, .. } = stmt {
                self.funktion_quellen.insert(name.clone(), "compiler/runtime/moo_rueckruf.moos".into());
                self.compile_function_def(name, params, defaults, body, decorators)?;
            }
        }
        self.try_stack = previous_try;
        Ok(global)
    }

    fn registration_none(&self) -> StructValue<'ctx> {
        self.mv_type().const_named_struct(&[
            self.context.i64_type().const_int(tag::NONE, false).into(),
            self.context.i64_type().const_zero().into(),
        ])
    }

    /// Rueckrufcode leiht sich ausschliesslich das vorhandene Fehlerflag.
    fn registration_guard(&self, error: inkwell::basic_block::BasicBlock<'ctx>) -> Result<(), String> {
        let flag = self.builder.build_call(self.rt.moo_try_check, &[], "registered.error")
            .map_err(|e| e.to_string())?.try_as_basic_value().basic()
            .ok_or("moo_try_check liefert keinen Wert")?.into_int_value();
        let failed = self.builder.build_int_compare(inkwell::IntPredicate::NE, flag,
            flag.get_type().const_zero(), "registered.failed").map_err(|e| e.to_string())?;
        let function = self.current_function.ok_or("Rueckruf ohne Funktion")?;
        let ok = self.context.append_basic_block(function, "registered.ok");
        self.builder.build_conditional_branch(failed, error, ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(ok);
        Ok(())
    }

    /// Globale CAS-Folge wird bei 2^53-1 angehalten, niemals wiederverwendet.
    /// So sind alle Token in Moo exakt und passen verlustfrei durch void*.
    fn registration_token(&mut self, error: inkwell::basic_block::BasicBlock<'ctx>)
        -> Result<inkwell::values::IntValue<'ctx>, String>
    {
        use inkwell::AtomicOrdering::SequentiallyConsistent as SeqCst;
        let i64t = self.context.i64_type();
        let counter = match self.module.get_global("__moo_registered.counter") {
            Some(g) => g,
            None => {
                let g = self.module.add_global(i64t, None, "__moo_registered.counter");
                g.set_linkage(inkwell::module::Linkage::Internal);
                g.set_initializer(&i64t.const_zero());
                g.set_alignment(8);
                g
            }
        };
        let function = self.current_function.ok_or("Registrierung ohne Funktion")?;
        let retry = self.context.append_basic_block(function, "registered.token.retry");
        let attempt = self.context.append_basic_block(function, "registered.token.cas");
        let exhausted = self.context.append_basic_block(function, "registered.token.exhausted");
        let done = self.context.append_basic_block(function, "registered.token.done");
        self.builder.build_unconditional_branch(retry).map_err(|e| e.to_string())?;
        self.builder.position_at_end(retry);
        let old = self.builder.build_load(i64t, counter.as_pointer_value(), "registered.token.old")
            .map_err(|e| e.to_string())?;
        let instruction = old.as_instruction_value().ok_or("Token-Load fehlt")?;
        instruction.set_alignment(8).map_err(|e| e.to_string())?;
        instruction.set_atomic_ordering(SeqCst).map_err(|e| e.to_string())?;
        let old = old.into_int_value();
        let available = self.builder.build_int_compare(inkwell::IntPredicate::ULT, old,
            i64t.const_int(9007199254740991, false), "registered.token.available").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(available, attempt, exhausted).map_err(|e| e.to_string())?;
        self.builder.position_at_end(exhausted);
        let text = self.make_global_str("Rueckruf: Tokenvorrat erschoepft", "registered.exhausted")?;
        let message = self.call_rt(self.rt.moo_string_new, &[text.into()], "registered.message")?;
        self.call_rt_void(self.rt.moo_throw, &[message.into()], "registered.throw")?;
        self.builder.build_unconditional_branch(error).map_err(|e| e.to_string())?;
        self.builder.position_at_end(attempt);
        let next = self.builder.build_int_add(old, i64t.const_int(1, false), "registered.token.next").map_err(|e| e.to_string())?;
        let pair = self.builder.build_cmpxchg(counter.as_pointer_value(), old, next, SeqCst, SeqCst).map_err(|e| e.to_string())?;
        let success = self.builder.build_extract_value(pair, 1, "registered.token.success")
            .map_err(|e| e.to_string())?.into_int_value();
        self.builder.build_conditional_branch(success, done, retry).map_err(|e| e.to_string())?;
        self.builder.position_at_end(done);
        Ok(next)
    }

    fn registration_replace_state(&self, global: inkwell::values::GlobalValue<'ctx>, value: StructValue<'ctx>) -> Result<(), String> {
        let previous = self.builder.build_load(self.mv_type(), global.as_pointer_value(), "registered.previous")
            .map_err(|e| e.to_string())?.into_struct_value();
        self.builder.build_store(global.as_pointer_value(), value).map_err(|e| e.to_string())?;
        self.call_rt_void(self.rt.moo_release, &[previous.into()], "registered.release_previous")
    }

    fn registration_state_argument(&self, global: inkwell::values::GlobalValue<'ctx>) -> Result<StructValue<'ctx>, String> {
        let state = self.builder.build_load(self.mv_type(), global.as_pointer_value(), "registered.state")
            .map_err(|e| e.to_string())?.into_struct_value();
        // Moo-Parameter sind owning. Die TLS-Referenz bleibt bis replace erhalten.
        self.call_rt_void(self.rt.moo_retain, &[state.into()], "registered.retain_state")?;
        Ok(state)
    }


    fn registration_native_type(&self, ty: &TypeRef, parameter: bool) -> Result<Option<BasicTypeEnum<'ctx>>, String> {
        if matches!(&ty.kind, TypeKind::Name(n) if n == "void") { return Ok(None); }
        if let Ok(scalar) = crate::codegen_native::NType::from_type_ref(ty) {
            return Ok(Some(scalar.llvm_type(self.context)));
        }
        if matches!(&ty.kind, TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1) {
            self.registration_pointer_target(ty)?;
            return Ok(Some(self.context.ptr_type(inkwell::AddressSpace::default()).into()));
        }
        if parameter {
            if let TypeKind::Name(name) = &ty.kind {
                self.native_structs.boxbar(name)
                    .map_err(|e| format!("Rueckrufparameter '{name}' ist dynamisch nicht boxbar: {e}"))?;
                return Ok(Some(self.context.ptr_type(inkwell::AddressSpace::default()).into()));
            }
        }
        Err("Registrierter Rueckruf: Skalar, ptr[void]/ptr[Struktur] oder boxbarer Strukturparameter erwartet; Strukturrueckgaben besitzen keinen Fehlerwertvertrag".into())
    }

    fn registration_pointer_target(&self, ty: &TypeRef) -> Result<Option<String>, String> {
        let TypeKind::Generic { args, .. } = &ty.kind else { return Err("Zeigertyp erwartet".into()); };
        match args.as_slice() {
            [TypeRef { kind: TypeKind::Name(name), .. }] if name == "void" => Ok(None),
            [TypeRef { kind: TypeKind::Name(name), .. }] if self.native_structs.layout(name).is_some() || self.native_structs.ist_opak(name) => Ok(Some(name.clone())),
            _ => Err("Dynamische Rueckrufzeiger brauchen ptr[void] oder ptr[Struktur]; Funktions-/Mehrfachzeiger haben keine dynamische Griffdarstellung".into()),
        }
    }

    fn registration_fallback(&self, ty: &TypeRef, expr: Option<&Expr>) -> Result<Option<BasicValueEnum<'ctx>>, String> {
        use crate::codegen_native::NType;
        if matches!(&ty.kind, TypeKind::Name(n) if n == "void") {
            if expr.is_some() { return Err("void-Rueckruf erwartet keinen Fehlerwert".into()); }
            return Ok(None);
        }
        let expr = expr.ok_or("rueckruf_registrieren braucht einen expliziten konstanten Fehlerwert")?;
        if matches!(&ty.kind, TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger") {
            let zero = matches!(expr, Expr::None)
                || matches!(expr, Expr::NativeNumber { text, .. } if text == "0")
                || matches!(expr, Expr::Number(n) if *n == 0.0);
            if !zero { return Err("Zeiger-Rueckruf braucht nichts oder 0 als Fehlerwert".into()); }
            return Ok(Some(self.context.ptr_type(inkwell::AddressSpace::default()).const_null().into()));
        }
        let scalar = NType::from_type_ref(ty)?;
        if scalar == NType::Bool {
            if let Expr::Boolean(value) = expr {
                return Ok(Some(self.context.bool_type().const_int(u64::from(*value), false).into()));
            }
        }
        let (literal, negative) = match expr {
            Expr::NativeNumber { text, .. } => (text.clone(), false),
            Expr::Number(n) if n.is_finite() => (n.abs().to_string(), n.is_sign_negative()),
            Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
                Expr::NativeNumber { text, .. } => (text.clone(), true),
                Expr::Number(n) if n.is_finite() => (n.abs().to_string(), !n.is_sign_negative()),
                _ => return Err("Rueckruf-Fehlerwert muss ein Literal sein".into()),
            },
            _ => return Err("Rueckruf-Fehlerwert muss ein passendes Literal sein".into()),
        };
        if scalar == NType::Bool {
            if negative || (literal != "0" && literal != "1") { return Err("bool-Fehlerwert muss wahr/falsch oder 0/1 sein".into()); }
            return Ok(Some(self.context.bool_type().const_int(u64::from(literal == "1"), false).into()));
        }
        let value = if matches!(scalar, NType::F32 | NType::F64) {
            scalar.float_literal(self.context, &literal, negative)?.0.value
        } else {
            scalar.integer_literal(self.context, &literal, negative)?.value
        };
        Ok(Some(value))
    }

    /// C-Argumentzeiger sind nur waehrend des Rueckrufs geliehen. Escape-Aliase
    /// behalten eine geschlossene Box und werden vor jedem spaeteren C-Aufruf
    /// abgewiesen; das fremde Objekt selbst wird niemals freigegeben.
    fn registration_expire_borrowed(&self, slots: &[PointerValue<'ctx>]) -> Result<(), String> {
        if slots.is_empty() { return Ok(()); }
        let p = self.context.ptr_type(inkwell::AddressSpace::default());
        let close = match self.module.get_function("moo_handle_close") {
            Some(f) => f,
            None => self.module.add_function("moo_handle_close", self.context.void_type().fn_type(&[p.into()], false), None),
        };
        for slot in slots {
            self.builder.build_call(close, &[(*slot).into()], "").map_err(|e| e.to_string())?;
        }
        Ok(())
    }

    /// Native Innenfunktion + vorhandener C-ABI-Export. Alle owning Moo-Werte
    /// liegen in initialisierten Slots und verlassen auch Fehlerpfade sauber.
    fn registration_thunk(&mut self, signature: &TypeRef, fallback: Option<&Expr>)
        -> Result<(FunctionValue<'ctx>, String), String>
    {
        use crate::codegen_native::NType;
        let TypeKind::Function { params, return_type, convention } = &signature.kind else {
            return Err("rueckruf_registrieren erwartet fn(...)->T als Typargument".into());
        };
        if params.len() > 8 { return Err("Dynamische Moo-Funktionen erlauben hoechstens 8 Rueckrufargumente".into()); }
        let global = self.registration_library()?;
        let module_name = self.module.get_name().to_string_lossy().into_owned();
        let nominals = self.native_structs.nominal_types(&module_name);
        let identity = crate::native_type_id::resolve_type(signature, &nominals)?;
        let registration_name = format!("rueckruf[{identity}]");
        let result_ty = self.registration_native_type(return_type, false)?;
        let fallback = self.registration_fallback(return_type, fallback)?;
        let p = self.context.ptr_type(inkwell::AddressSpace::default());
        let mut types: Vec<BasicMetadataTypeEnum<'ctx>> = vec![p.into()];
        for ty in params {
            types.push(self.registration_native_type(ty, true)?.ok_or("void ist kein Rueckrufparameter")?.into());
        }
        let native_type = match result_ty {
            Some(ty) => ty.fn_type(&types, false),
            None => self.context.void_type().fn_type(&types, false),
        };
        let ordinal = self.module.get_functions().count();
        let internal = self.module.add_function(&format!("__moo_registered.body.{ordinal}"), native_type, Some(inkwell::module::Linkage::Internal));
        let entry = self.context.append_basic_block(internal, "entry");
        let error = self.context.append_basic_block(internal, "registered.failure");
        let previous_function = self.current_function.replace(internal);
        let previous_block = self.builder.get_insert_block();
        let previous_variables = std::mem::take(&mut self.variables);
        let previous_borrowed = self.defer_borrowed_handler.take();
        let previous_try = std::mem::take(&mut self.try_stack);
        let frame = self.rahmen_betreten(RahmenArt::Sonstig);
        self.native_skip_target = Some(error);
        self.builder.position_at_end(entry);
        let borrowed: Vec<usize> = params.iter().enumerate().filter_map(|(index, ty)|
            matches!(&ty.kind, TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger").then_some(index)).collect();
        let mut slots = Vec::new();
        for index in 0..params.len() + 2 + borrowed.len() {
            let name = format!("__registered.value.{index}");
            let slot = self.entry_alloca(self.mv_type(), &name)?;
            self.builder.build_store(slot, self.registration_none()).map_err(|e| e.to_string())?;
            self.variables.insert(name, slot);
            slots.push(slot);
        }
        let state = self.registration_state_argument(global)?;
        let token = self.builder.build_ptr_to_int(internal.get_nth_param(0).ok_or("user_data fehlt")?.into_pointer_value(),
            self.context.i64_type(), "registered.token").map_err(|e| e.to_string())?;
        let token = self.builder.build_unsigned_int_to_float(token, self.context.f64_type(), "registered.token.number").map_err(|e| e.to_string())?;
        let token = self.call_rt(self.rt.moo_number, &[token.into()], "registered.token.box")?;
        let sig_ptr = self.make_global_str(&registration_name, "registered.signature")?;
        let sig_value = self.call_rt(self.rt.moo_string_new, &[sig_ptr.into()], "registered.signature.box")?;
        let finder = self.module.get_function("__moo_rueckruf_finden").ok_or("Rueckruf-Suche fehlt")?;
        let closure = self.call_rt(finder, &[state.into(), token.into(), sig_value.into()], "registered.find")?;
        self.builder.build_store(slots[params.len()], closure).map_err(|e| e.to_string())?;
        self.registration_guard(error)?;
        for (index, ty) in params.iter().enumerate() {
            let raw = internal.get_nth_param(index as u32 + 1).ok_or("Rueckrufparameter fehlt")?;
            let boxed = if let Ok(scalar) = NType::from_type_ref(ty) {
                match scalar {
                    NType::Bool => self.call_rt(self.rt.moo_bool_fn, &[raw.into()], "registered.bool")?,
                    NType::F32 | NType::F64 => {
                        let number = if scalar == NType::F32 {
                            self.builder.build_float_ext(raw.into_float_value(), self.context.f64_type(), "registered.float").map_err(|e| e.to_string())?
                        } else { raw.into_float_value() };
                        self.call_rt(self.rt.moo_number, &[number.into()], "registered.float.box")?
                    }
                    NType::I64 | NType::U64 => {
                        let number = self.box_native_wide(raw.into_int_value(), scalar == NType::U64)?;
                        self.call_rt(self.rt.moo_number, &[number.into()], "registered.wide.box")?
                    }
                    _ => {
                        let number = if scalar.is_signed_integer() {
                            self.builder.build_signed_int_to_float(raw.into_int_value(), self.context.f64_type(), "registered.int")
                        } else {
                            self.builder.build_unsigned_int_to_float(raw.into_int_value(), self.context.f64_type(), "registered.uint")
                        }.map_err(|e| e.to_string())?;
                        self.call_rt(self.rt.moo_number, &[number.into()], "registered.int.box")?
                    }
                }
            } else if matches!(&ty.kind, TypeKind::Generic { .. }) {
                let target = self.registration_pointer_target(ty)?;
                self.native_handle_box(raw.into_pointer_value(), target.as_deref())?
            } else if let TypeKind::Name(name) = &ty.kind {
                self.native_struct_box(raw.into_pointer_value(), name)?
            } else { return Err("Rueckrufparameter kann nicht geboxt werden".into()); };
            self.builder.build_store(slots[index], boxed).map_err(|e| e.to_string())?;
            if let Some(pin) = borrowed.iter().position(|&parameter| parameter == index) {
                self.call_rt_void(self.rt.moo_retain, &[boxed.into()], "registered.borrowed.pin")?;
                self.builder.build_store(slots[params.len()+2+pin], boxed).map_err(|e| e.to_string())?;
            }
            self.registration_guard(error)?;
        }
        let mut dynamic_args: Vec<BasicMetadataValueEnum<'ctx>> = vec![closure.into()];
        for slot in &slots[..params.len()] {
            let value = self.builder.build_load(self.mv_type(), *slot, "registered.argument").map_err(|e| e.to_string())?;
            dynamic_args.push(value.into());
            // Der dynamische Aufruf uebernimmt Argumente, die Closure bleibt geliehen.
            self.builder.build_store(*slot, self.registration_none()).map_err(|e| e.to_string())?;
        }
        let call = match params.len() {
            0 => self.rt.moo_func_call_0, 1 => self.rt.moo_func_call_1,
            2 => self.rt.moo_func_call_2, 3 => self.rt.moo_func_call_3,
            4 => self.rt.moo_func_call_4, 5 => self.rt.moo_func_call_5,
            6 => self.rt.moo_func_call_6, 7 => self.rt.moo_func_call_7,
            _ => self.rt.moo_func_call_8,
        };
        let result = self.call_rt(call, &dynamic_args, "registered.invoke")?;
        self.builder.build_store(slots[params.len()+1], result).map_err(|e| e.to_string())?;
        self.registration_guard(error)?;
        let result_expr = Expr::Identifier(format!("__registered.value.{}", params.len()+1));
        let native_result: Option<BasicValueEnum<'ctx>> = if result_ty.is_none() { None }
        else if let Ok(scalar) = NType::from_type_ref(return_type) {
            Some(match scalar {
                NType::Bool => self.compile_native_bool_argument(&result_expr, Some(error))?.into(),
                NType::F32 | NType::F64 => self.compile_native_float_argument(&result_expr, scalar, Some(error))?.into(),
                NType::I64 | NType::U64 => self.compile_native_wide_argument(&result_expr, scalar, Some(error))?.into(),
                _ => self.compile_native_integer_argument(&result_expr, scalar, Some(error))?.into(),
            })
        } else {
            let target = self.registration_pointer_target(return_type)?;
            let (raw, _, pin) = self.compile_native_handle_argument(&result_expr, target.as_deref(), &[], &[], Some(error))?;
            self.call_rt_void(self.rt.moo_release, &[pin.into()], "registered.release_result_pin")?;
            Some(raw.into())
        };
        self.registration_expire_borrowed(&slots[params.len()+2..])?;
        self.release_function_locals()?;
        self.builder.build_return(native_result.as_ref().map(|v| v as &dyn BasicValue<'ctx>)).map_err(|e| e.to_string())?;
        self.builder.position_at_end(error);
        self.registration_expire_borrowed(&slots[params.len()+2..])?;
        self.release_function_locals()?;
        let zero = result_ty.map(|ty| ty.const_zero());
        self.builder.build_return(zero.as_ref().map(|v| v as &dyn BasicValue<'ctx>)).map_err(|e| e.to_string())?;
        self.rahmen_verlassen(frame);
        self.current_function = previous_function;
        self.variables = previous_variables;
        self.defer_borrowed_handler = previous_borrowed;
        self.try_stack = previous_try;
        if let Some(block) = previous_block { self.builder.position_at_end(block); }
        let triple = self.module.get_triple().as_str().to_string_lossy().into_owned();
        let convention = match convention.as_deref() {
            Some(name) => crate::ffi_types::llvm_call_convention(name, &triple)?,
            None => 0, // LLVM C_CC, wie ffi_types::CC_C.
        };
        let mut inputs = vec![TypeRef { kind: TypeKind::Generic { name: "ptr".into(), args: vec![
            TypeRef { kind: TypeKind::Name("void".into()), ..signature.clone() }
        ] }, ..signature.clone() }];
        inputs.extend(params.iter().cloned());
        let thunk = crate::ffi_types::emit_abi_export(self.context, &self.module,
            self.target_data.as_ref().ok_or("Datenlayout fehlt")?, &format!("__moo_registered.thunk.{ordinal}"),
            internal, &inputs, return_type, &self.native_structs, convention, inkwell::module::Linkage::Internal,
            Some(crate::ffi_types::RueckrufVertrag { name: registration_name.clone(), fehlerwert: fallback }))?;
        Ok((thunk, registration_name))
    }


    fn compile_registration(&mut self, types: &[TypeRef], args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        let [signature] = types else { return Err("rueckruf_registrieren[fn(...)->T](funktion, fehlerwert) erwartet genau einen Signaturtyp".into()); };
        if args.is_empty() || args.len() > 2 { return Err("rueckruf_registrieren erwartet Funktion und bei Nicht-void einen Fehlerwert".into()); }
        let global = self.registration_library()?;
        let (thunk, name) = self.registration_thunk(signature, args.get(1))?;
        let type_id = crate::native_type_id::registration_id(&name).ok_or("Rueckruf-Typ-ID fehlt")?;
        let function = self.current_function.ok_or("Registrierung ohne Funktion")?;
        let failure = self.context.append_basic_block(function, "registered.create.failure");
        let done = self.context.append_basic_block(function, "registered.create.done");
        let mut slots = Vec::new();
        for _ in 0..4 {
            let slot = self.entry_alloca(self.mv_type(), "registered.create.owned")?;
            self.builder.build_store(slot, self.registration_none()).map_err(|e| e.to_string())?;
            slots.push(slot);
        }
        let closure = self.compile_expr(&args[0])?;
        self.builder.build_store(slots[0], closure).map_err(|e| e.to_string())?;
        self.registration_guard(failure)?;
        let token = self.registration_token(failure)?;
        let p = self.context.ptr_type(inkwell::AddressSpace::default());
        let record_type = self.context.struct_type(&[p.into(), p.into(), p.into()], false);
        let record = self.entry_alloca(record_type, "registered.record")?;
        let data = self.builder.build_int_to_ptr(token, p, "registered.user_data").map_err(|e| e.to_string())?;
        let fields = [global.as_pointer_value(), thunk.as_global_value().as_pointer_value(), data];
        for (index, value) in fields.iter().enumerate() {
            let field = self.builder.build_struct_gep(record_type, record, index as u32, "registered.record.field").map_err(|e| e.to_string())?;
            self.builder.build_store(field, *value).map_err(|e| e.to_string())?;
        }
        let i64t = self.context.i64_type();
        let size = self.target_data.as_ref().ok_or("Datenlayout fehlt")?.get_abi_size(&record_type);
        let boxer = match self.module.get_function("moo_handle_box_bytes") {
            Some(f) => f,
            None => self.module.add_function("moo_handle_box_bytes", self.context.void_type().fn_type(
                &[p.into(), i64t.into(), i64t.into(), p.into()], false), None),
        };
        self.builder.build_call(boxer, &[record.into(), i64t.const_int(size, false).into(),
            i64t.const_int(type_id, false).into(), slots[3].into()], "").map_err(|e| e.to_string())?;
        self.registration_guard(failure)?;
        let state = self.registration_state_argument(global)?;
        self.builder.build_store(slots[1], state).map_err(|e| e.to_string())?;
        let sig_ptr = self.make_global_str(&name, "registered.signature")?;
        let signature_value = self.call_rt(self.rt.moo_string_new, &[sig_ptr.into()], "registered.signature.box")?;
        self.builder.build_store(slots[2], signature_value).map_err(|e| e.to_string())?;
        self.registration_guard(failure)?;
        let number = self.builder.build_unsigned_int_to_float(token, self.context.f64_type(), "registered.token.number").map_err(|e| e.to_string())?;
        let number = self.call_rt(self.rt.moo_number, &[number.into()], "registered.token.box")?;
        let register = self.module.get_function("__moo_rueckruf_anmelden").ok_or("Moo-Registrierung fehlt")?;
        // Uebergabe der drei owning Parameter; TLS und Griff bleiben separat.
        for slot in &slots[..3] {
            self.builder.build_store(*slot, self.registration_none()).map_err(|e| e.to_string())?;
        }
        let updated = self.call_rt(register, &[state.into(), number.into(), signature_value.into(), closure.into()], "registered.create")?;
        self.builder.build_store(slots[1], updated).map_err(|e| e.to_string())?;
        self.registration_guard(failure)?;
        self.registration_replace_state(global, updated)?;
        self.builder.build_store(slots[1], self.registration_none()).map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(failure);
        let closer = match self.module.get_function("moo_handle_close") {
            Some(f) => f,
            None => self.module.add_function("moo_handle_close", self.context.void_type().fn_type(&[p.into()], false), None),
        };
        self.builder.build_call(closer, &[slots[3].into()], "").map_err(|e| e.to_string())?;
        for slot in &slots {
            let value = self.builder.build_load(self.mv_type(), *slot, "registered.create.release").map_err(|e| e.to_string())?;
            self.call_rt_void(self.rt.moo_release, &[value.into()], "registered.create.release")?;
            self.builder.build_store(*slot, self.registration_none()).map_err(|e| e.to_string())?;
        }
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(done);
        Ok(self.builder.build_load(self.mv_type(), slots[3], "registered.handle").map_err(|e| e.to_string())?.into_struct_value())
    }

    fn compile_registration_close(&mut self, args: &[Expr]) -> Result<StructValue<'ctx>, String> {
        let [arg] = args else { return Err("rueckruf_abmelden erwartet genau einen Registrierungs-Griff".into()); };
        let global = self.registration_library()?;
        let function = self.current_function.ok_or("Abmeldung ohne Funktion")?;
        let done = self.context.append_basic_block(function, "registered.close.done");
        let open = self.context.append_basic_block(function, "registered.close.open");
        let same_thread = self.context.append_basic_block(function, "registered.close.owner");
        let wrong_thread = self.context.append_basic_block(function, "registered.close.wrong_thread");
        let p = self.context.ptr_type(inkwell::AddressSpace::default());
        let i64t = self.context.i64_type();
        let value_slot = self.entry_alloca(self.mv_type(), "registered.close.handle")?;
        let state_slot = self.entry_alloca(self.mv_type(), "registered.close.state")?;
        self.builder.build_store(state_slot, self.registration_none()).map_err(|e| e.to_string())?;
        let value = self.compile_expr(arg)?;
        self.builder.build_store(value_slot, value).map_err(|e| e.to_string())?;
        self.registration_guard(done)?;
        let data_slot = self.entry_alloca(p, "registered.close.payload")?;
        let record_type = self.context.struct_type(&[p.into(), p.into(), p.into()], false);
        let size = self.target_data.as_ref().ok_or("Datenlayout fehlt")?.get_abi_size(&record_type);
        let payload = match self.module.get_function("moo_handle_payload") {
            Some(f) => f,
            None => self.module.add_function("moo_handle_payload", self.context.i32_type().fn_type(
                &[p.into(), i64t.into(), p.into()], false), None),
        };
        let status = self.builder.build_call(payload, &[value_slot.into(), i64t.const_int(size, false).into(), data_slot.into()],
            "registered.close.status").map_err(|e| e.to_string())?.try_as_basic_value().basic().ok_or("Payload-Status fehlt")?.into_int_value();
        let valid = self.builder.build_int_compare(inkwell::IntPredicate::EQ, status, self.context.i32_type().const_int(1, false),
            "registered.close.valid").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(valid, open, done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(open);
        let data = self.builder.build_load(p, data_slot, "registered.close.data").map_err(|e| e.to_string())?.into_pointer_value();
        let owner_slot = self.builder.build_struct_gep(record_type, data, 0, "registered.close.owner_slot").map_err(|e| e.to_string())?;
        let owner = self.builder.build_load(p, owner_slot, "registered.close.owner").map_err(|e| e.to_string())?.into_pointer_value();
        let owner = self.builder.build_int_compare(inkwell::IntPredicate::EQ,
            self.builder.build_ptr_to_int(owner, i64t, "registered.owner.id").map_err(|e| e.to_string())?,
            self.builder.build_ptr_to_int(global.as_pointer_value(), i64t, "registered.current.id").map_err(|e| e.to_string())?,
            "registered.same_thread").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(owner, same_thread, wrong_thread).map_err(|e| e.to_string())?;
        self.builder.position_at_end(wrong_thread);
        let text = self.make_global_str("Rueckruf darf nur im registrierenden Moo-Thread abgemeldet werden", "registered.wrong_thread")?;
        let message = self.call_rt(self.rt.moo_string_new, &[text.into()], "registered.wrong_thread.box")?;
        self.call_rt_void(self.rt.moo_throw, &[message.into()], "registered.wrong_thread.throw")?;
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(same_thread);
        let token_slot = self.builder.build_struct_gep(record_type, data, 2, "registered.close.token_slot").map_err(|e| e.to_string())?;
        let token = self.builder.build_load(p, token_slot, "registered.close.token").map_err(|e| e.to_string())?.into_pointer_value();
        let token = self.builder.build_ptr_to_int(token, i64t, "registered.close.token.id").map_err(|e| e.to_string())?;
        let number = self.builder.build_unsigned_int_to_float(token, self.context.f64_type(), "registered.close.number").map_err(|e| e.to_string())?;
        let number = self.call_rt(self.rt.moo_number, &[number.into()], "registered.close.box")?;
        let state = self.registration_state_argument(global)?;
        let unregister = self.module.get_function("__moo_rueckruf_abmelden").ok_or("Moo-Abmeldung fehlt")?;
        let updated = self.call_rt(unregister, &[state.into(), number.into()], "registered.close")?;
        self.builder.build_store(state_slot, updated).map_err(|e| e.to_string())?;
        self.registration_guard(done)?;
        self.registration_replace_state(global, updated)?;
        self.builder.build_store(state_slot, self.registration_none()).map_err(|e| e.to_string())?;
        let closer = match self.module.get_function("moo_handle_close") {
            Some(f) => f,
            None => self.module.add_function("moo_handle_close", self.context.void_type().fn_type(&[p.into()], false), None),
        };
        self.builder.build_call(closer, &[value_slot.into()], "").map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        self.builder.position_at_end(done);
        for slot in [value_slot, state_slot] {
            let value = self.builder.build_load(self.mv_type(), slot, "registered.close.release").map_err(|e| e.to_string())?;
            self.call_rt_void(self.rt.moo_release, &[value.into()], "registered.close.release")?;
        }
        Ok(self.registration_none())
    }

    /// P2-T23/T53 B3: Laufzeit-Typ-ID eines Griffs (0 = ptr[void], sonst Struktur-ID).
    fn griff_typ_id(&self, ziel: Option<&str>) -> Result<u64, String> {
        match ziel {
            None => Ok(0),
            Some(name) => crate::native_type_id::registration_id(name)
                .or_else(|| self.native_structs.struktur_id(name))
                .ok_or_else(|| format!("Griff ptr[{name}]: Strukturidentitaet fehlt")),
        }
    }

    /// P2-T23/T53 B3 (Plan 36aca1f2 E1-E6): Griffargument an der dyn->nativ-Grenze.
    /// Variable (geliehen, +1 bis nach dem Aufruf) oder `nichts` (Nullzeiger).
    /// moo_handle_unbox prueft Griff, Zeigertyp und Zustand (freigegeben). Fehler:
    /// eigener Wert, alle Puffer-Pins und frueheren Griffwerte dieses Aufrufs
    /// freigeben, dann derselbe Ausgang wie bei Pufferargumenten.
    fn compile_native_handle_argument(
        &mut self,
        expr: &Expr,
        ziel: Option<&str>,
        pins: &[(inkwell::values::PointerValue<'ctx>, StructValue<'ctx>)],
        griffe: &[(inkwell::values::PointerValue<'ctx>, StructValue<'ctx>, bool)],
        catch: Option<inkwell::basic_block::BasicBlock<'ctx>>,
    ) -> Result<(inkwell::values::PointerValue<'ctx>, inkwell::values::PointerValue<'ctx>, StructValue<'ctx>), String> {
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        let i64t = self.context.i64_type();
        let typ_id = self.griff_typ_id(ziel)?;
        let registration = ziel.and_then(crate::native_type_id::registration_id).is_some();
        let owner = if registration { Some(self.registration_library()?.as_pointer_value()) } else { None };
        let unbox_name = if registration { "moo_handle_unbox_owned" } else { "moo_handle_unbox" };
        let mut parameter_types: Vec<BasicMetadataTypeEnum<'ctx>> = vec![ptr_type.into(), i64t.into()];
        if registration { parameter_types.push(i64t.into()); parameter_types.push(ptr_type.into()); }
        parameter_types.push(ptr_type.into());
        let unbox = match self.module.get_function(unbox_name) {
            Some(f) => f,
            None => self.module.add_function(unbox_name, self.context.i32_type().fn_type(&parameter_types, false), None),
        };
        let wert = self.compile_expr(expr)?;
        let wert_slot = self.entry_alloca(self.mv_type(), "native.griff.wert")?;
        self.builder.build_store(wert_slot, wert).map_err(|e| e.to_string())?;
        let zeiger_slot = self.entry_alloca(ptr_type, "native.griff.zeiger")?;
        let mut unbox_args: Vec<BasicMetadataValueEnum<'ctx>> = vec![wert_slot.into(), i64t.const_int(typ_id, false).into()];
        if let Some(owner) = owner {
            let record = self.context.struct_type(&[ptr_type.into(), ptr_type.into(), ptr_type.into()], false);
            let size = self.target_data.as_ref().ok_or("Datenlayout fuer Registrierung fehlt")?.get_abi_size(&record);
            unbox_args.push(i64t.const_int(size, false).into());
            unbox_args.push(owner.into());
        }
        unbox_args.push(zeiger_slot.into());
        let ok = self.builder.build_call(unbox, &unbox_args, "native.griff.unbox")
            .map_err(|e| e.to_string())?.try_as_basic_value();
        let ok = match ok {
            inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
            _ => return Err("moo_handle_unbox liefert keinen Wert".into()),
        };
        let fehlt = self.builder.build_int_compare(inkwell::IntPredicate::EQ, ok,
            self.context.i32_type().const_zero(), "native.griff.fehlt").map_err(|e| e.to_string())?;
        let ok_bb = self.context.append_basic_block(function, "native.griff.ok");
        let fehler_bb = self.context.append_basic_block(function, "native.griff.fehler");
        self.builder.build_conditional_branch(fehlt, fehler_bb, ok_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(fehler_bb);
        self.call_rt_void(self.rt.moo_release, &[wert.into()], "native.griff.release_fehler")?;
        self.native_buffer_unpin(pins)?;
        self.native_handle_release(griffe, false)?;
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(ok_bb);
        let zeiger = self.builder.build_load(ptr_type, zeiger_slot, "native.griff.zeiger")
            .map_err(|e| e.to_string())?.into_pointer_value();
        Ok((zeiger, wert_slot, wert))
    }

    /// P2-T85 C1: seiteneffektfreie Argumentform fuer Puffer/Griffe: Variable,
    /// selbst/this oder Feldzugriff darauf (auch verkettet, ohne Aufrufe/Indizes).
    fn ist_feldkette(expr: &Expr) -> bool {
        match expr {
            Expr::Identifier(_) | Expr::This => true,
            Expr::PropertyAccess { object, .. } => Self::ist_feldkette(object),
            _ => false,
        }
    }

    /// P2-T85 A1: Rahmenzustand sichern und fuer den neuen Rahmen `art`
    /// neutralisieren (Guards aus, keine Sprungziele, kein Statement-Kontext).
    fn rahmen_betreten(&mut self, art: RahmenArt) -> RahmenZustand<'ctx> {
        RahmenZustand {
            rahmen_art: std::mem::replace(&mut self.rahmen_art, art),
            native_guard_enabled: std::mem::replace(&mut self.native_guard_enabled, false),
            native_guard_exit: self.native_guard_exit.take(),
            native_skip_target: self.native_skip_target.take(),
            native_try_direct: std::mem::replace(&mut self.native_try_direct, false),
            stmt_root_guard: std::mem::replace(&mut self.stmt_root_guard, false),
            stmt_recursive_guard: std::mem::replace(&mut self.stmt_recursive_guard, false),
            stmt_owned_context: std::mem::take(&mut self.stmt_owned_context),
        }
    }

    /// P2-T85 A1: Rahmenzustand der umgebenden Funktion wiederherstellen.
    fn rahmen_verlassen(&mut self, zustand: RahmenZustand<'ctx>) {
        self.rahmen_art = zustand.rahmen_art;
        self.native_guard_enabled = zustand.native_guard_enabled;
        self.native_guard_exit = zustand.native_guard_exit;
        self.native_skip_target = zustand.native_skip_target;
        self.native_try_direct = zustand.native_try_direct;
        self.stmt_root_guard = zustand.stmt_root_guard;
        self.stmt_recursive_guard = zustand.stmt_recursive_guard;
        self.stmt_owned_context = zustand.stmt_owned_context;
    }

    /// P2-T87: Methodennamen mit eigenem Builtin-Arm in Expr::MethodCall. Muss
    /// mit den Arm-Koepfen der Kaskade uebereinstimmen (neue Arme hier ergaenzen,
    /// sonst ueberdeckt das Builtin gleichnamige Klassenmethoden).
    fn ist_builtin_methode(name: &str) -> bool {
        matches!(name,
            "mit_gradient" | "with_gradient" | "rueckwaerts" | "rückwärts" | "backward" | "gradient"
            | "gradient_löschen" | "gradient_loeschen" | "zero_grad" | "gradient_setzen" | "set_gradient"
            | "matmul" | "transponieren" | "transpose" | "umformen" | "reshape" | "zeilen" | "rows"
            | "verketten" | "concat" | "summe" | "sum" | "mittel" | "mean" | "max_wert" | "max_val"
            | "exp" | "log" | "wurzel" | "sqrt" | "negieren" | "neg" | "hoch" | "pow"
            | "hadamard" | "hadamard_inv" | "relu" | "sigmoid" | "tanh" | "gelu" | "softmax" | "logsoftmax"
            | "als_dtype" | "as_dtype" | "form" | "shape" | "größe" | "groesse" | "size"
            | "zu_liste" | "to_list" | "wert" | "value_at" | "wert_setzen" | "set_value"
            | "schritt" | "step" | "trainiere" | "train" | "vorhersage" | "predict"
            | "genauigkeit" | "accuracy" | "speichern" | "save"
            | "append" | "hinzufügen" | "length" | "länge" | "pop" | "leeren" | "clear"
            | "entfernen" | "remove_at" | "einfügen" | "einfuegen" | "insert" | "reverse" | "umkehren"
            | "sort" | "sortieren" | "join" | "verbinden" | "contains" | "enthält" | "keys" | "schlüssel"
            | "has" | "hat" | "upper" | "gross" | "lower" | "klein" | "trim" | "trimmen"
            | "slice" | "teilstring" | "split" | "teilen" | "replace" | "ersetzen"
            | "str_contains" | "text_enthält" | "warten" | "wait" | "binde" | "bind"
            | "ausfuehren" | "ausführen" | "execute" | "abfrage" | "query"
            | "zuruecksetzen" | "zurücksetzen" | "reset" | "fertig" | "done" | "senden" | "send"
            | "empfangen" | "recv" | "schliessen" | "close" | "web_annehmen" | "web_accept"
            | "antworten_mit_headers" | "respond_with_headers" | "json_antworten_mit_headers"
            | "json_respond_with_headers" | "antworten" | "respond" | "json_antworten" | "json_respond"
            | "starten" | "start" | "annehmen" | "accept" | "lesen" | "read" | "schreiben" | "write"
            | "lesen_bytes" | "read_bytes" | "schreiben_bytes" | "schreibe_bytes" | "write_bytes"
            | "timeout_setzen" | "set_timeout" | "udp_verbinden" | "udp_connect"
            | "map" | "abbilden" | "filter" | "filtern" | "bei" | "on" | "auslösen" | "emit")
    }

    /// P2-T87: Methode einer Klasse inklusive Elternkette (None = kein Member).
    fn klassen_methode(&self, class: &str, method: &str) -> Result<Option<FunctionValue<'ctx>>, String> {
        let mut current = class.to_string();
        let mut visited = std::collections::HashSet::new();
        loop {
            if !visited.insert(current.clone()) {
                return Err(format!("Zyklische Klassenvererbung bei {class}"));
            }
            if let Some(func) = self.class_methods.get(&(current.clone(), method.to_string())) {
                return Ok(Some(*func));
            }
            match self.class_parents.get(&current) {
                Some(parent) => current = parent.clone(),
                None => return Ok(None),
            }
        }
    }

    /// P2-T87 (Befund 9ff1eb58): Laufzeitweiche fuer einen Methodennamen, den
    /// eine Klasse UND ein Builtin-Arm kennt, bei statisch unbekanntem Empfaenger.
    /// Objekt einer Klasse mit dieser Methode (inkl. Vererbung) -> Klassenmethode;
    /// jeder andere Empfaenger -> Builtin-Arme ueber den Receiver-Slot (der
    /// Empfaenger wird nicht erneut ausgewertet, Argumente je Zweig genau einmal).
    fn methoden_weiche(
        &mut self,
        obj: StructValue<'ctx>,
        slot_name: &str,
        method: &str,
        args: &[Expr],
    ) -> Result<StructValue<'ctx>, String> {
        let function = self.current_function.ok_or("Methodenaufruf ohne Funktion")?;
        let mut classes: Vec<String> = self.class_methods.keys()
            .map(|(class, _)| class.clone()).chain(self.class_parents.keys().cloned()).collect();
        classes.sort();
        classes.dedup();
        let mut kandidaten = Vec::new();
        for class in classes {
            if let Some(func) = self.klassen_methode(&class, method)? {
                if func.count_params() == 0 { return Err(format!("{class}.{method} hat keinen Receiver")); }
                kandidaten.push((class, func));
            }
        }
        let result_ptr = self.entry_alloca(self.mv_type(), "weiche_ergebnis")?;
        let ziel_ptr = self.entry_alloca(self.context.i32_type(), "weiche_ziel")?;
        let klasse_bb = self.context.append_basic_block(function, "weiche_klasse");
        let aufruf_bb = self.context.append_basic_block(function, "weiche_klassen_aufruf");
        let builtin_bb = self.context.append_basic_block(function, "weiche_builtin");
        let nach_bb = self.context.append_basic_block(function, "weiche_nach");
        let tag_wert = self.builder.build_extract_value(obj, 0, "weiche_tag")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let ist_objekt = self.builder.build_int_compare(inkwell::IntPredicate::EQ, tag_wert,
            self.context.i64_type().const_int(tag::OBJECT, false), "weiche_ist_objekt").map_err(|e| format!("{e}"))?;
        self.builder.build_conditional_branch(ist_objekt, klasse_bb, builtin_bb).map_err(|e| format!("{e}"))?;

        // Klassenname gegen alle Klassen mit dieser Methode; ohne Treffer -> Builtin.
        self.builder.position_at_end(klasse_bb);
        let class_call = self.call_rt_abi(self.rt.moo_object_class_name, &[obj.into()], "weiche_klasse_name")?;
        let class_ptr = match class_call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_pointer_value(),
            _ => return Err("moo_object_class_name hat keinen Wert".to_string()),
        };
        let strcmp_fn = self.module.get_function("strcmp").unwrap_or_else(|| {
            let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
            self.module.add_function("strcmp", self.context.i32_type().fn_type(&[ptr.into(), ptr.into()], false), None)
        });
        for (index, (class, _)) in kandidaten.iter().enumerate() {
            let name_ptr = self.make_global_str(class, &format!("weiche_klasse_{index}"))?;
            let cmp = match self.builder.build_call(strcmp_fn, &[class_ptr.into(), name_ptr.into()], "weiche_cmp")
                .map_err(|e| format!("{e}"))?.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("strcmp hat keinen Wert".to_string()),
            };
            let gleich = self.builder.build_int_compare(inkwell::IntPredicate::EQ, cmp,
                self.context.i32_type().const_zero(), "weiche_gleich").map_err(|e| format!("{e}"))?;
            let treffer_bb = self.context.append_basic_block(function, "weiche_treffer");
            let weiter_bb = self.context.append_basic_block(function, "weiche_weiter");
            self.builder.build_conditional_branch(gleich, treffer_bb, weiter_bb).map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(treffer_bb);
            self.builder.build_store(ziel_ptr, self.context.i32_type().const_int(index as u64, false))
                .map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(aufruf_bb).map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(weiter_bb);
        }
        self.builder.build_unconditional_branch(builtin_bb).map_err(|e| format!("{e}"))?;

        // Klassenzweig: Argumente einmal auswerten, dann gezielt aufrufen. Der
        // Receiver-Slot bleibt Besitzer; die Methode konsumiert eine eigene Referenz.
        self.builder.position_at_end(aufruf_bb);
        let mut werte = Vec::with_capacity(args.len());
        for arg in args { werte.push(self.compile_expr(arg)?); }
        if kandidaten.is_empty() {
            for wert in &werte { self.call_rt_void(self.rt.moo_release, &[(*wert).into()], "weiche_arg_frei")?; }
            self.builder.build_unconditional_branch(builtin_bb).map_err(|e| format!("{e}"))?;
        } else {
            let ziel = self.builder.build_load(self.context.i32_type(), ziel_ptr, "weiche_ziel_wert")
                .map_err(|e| format!("{e}"))?.into_int_value();
            let aufruf_bloecke: Vec<_> = kandidaten.iter()
                .map(|_| self.context.append_basic_block(function, "weiche_methode")).collect();
            let faelle: Vec<_> = aufruf_bloecke.iter().enumerate()
                .map(|(index, bb)| (self.context.i32_type().const_int(index as u64, false), *bb)).collect();
            self.builder.build_switch(ziel, aufruf_bloecke[0], &faelle).map_err(|e| format!("{e}"))?;
            for ((_, func), bb) in kandidaten.iter().zip(&aufruf_bloecke) {
                self.builder.position_at_end(*bb);
                let erwartet = func.count_params() as usize;
                self.call_rt_void(self.rt.moo_retain, &[obj.into()], "weiche_receiver")?;
                let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                for wert in werte.iter().take(erwartet - 1) { call_args.push((*wert).into()); }
                for wert in werte.iter().skip(erwartet - 1) {
                    self.call_rt_void(self.rt.moo_release, &[(*wert).into()], "weiche_arg_ueber")?;
                }
                while call_args.len() < erwartet {
                    call_args.push(self.call_rt(self.rt.moo_none, &[], "weiche_arg_none")?.into());
                }
                let ergebnis = self.call_rt(*func, &call_args, "weiche_methoden_ergebnis")?;
                self.builder.build_store(result_ptr, ergebnis).map_err(|e| format!("{e}"))?;
                self.builder.build_unconditional_branch(nach_bb).map_err(|e| format!("{e}"))?;
            }
        }

        // Builtin-Zweig: dieselbe Methode ueber den Receiver-Slot, nur Builtin-Arme.
        self.builder.position_at_end(builtin_bb);
        self.methode_nur_builtin = true;
        let builtin_ausdruck = Expr::MethodCall {
            object: Box::new(Expr::Identifier(slot_name.to_string())),
            method: method.to_string(),
            args: args.to_vec(),
        };
        let builtin_ergebnis = self.compile_expr(&builtin_ausdruck);
        self.methode_nur_builtin = false;
        let builtin_ergebnis = builtin_ergebnis?;
        self.builder.build_store(result_ptr, builtin_ergebnis).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(nach_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(nach_bb);
        Ok(self.builder.build_load(self.mv_type(), result_ptr, "weiche_wert")
            .map_err(|e| format!("{e}"))?.into_struct_value())
    }

    /// P2-T23/T53 B3: geliehene Griffwerte eines nativen Aufrufs freigeben; nach
    /// dem Aufruf (`nach_aufruf`) vorher @verbraucht-Griffe schliessen. Auch wenn
    /// der Aufruf einen Fehler meldet: lieber ein Leck als Doppelfreigabe (R2).
    fn native_handle_release(
        &mut self,
        griffe: &[(inkwell::values::PointerValue<'ctx>, StructValue<'ctx>, bool)],
        nach_aufruf: bool,
    ) -> Result<(), String> {
        if griffe.is_empty() { return Ok(()); }
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        for (slot, wert, verbraucht) in griffe {
            if nach_aufruf && *verbraucht {
                let close = match self.module.get_function("moo_handle_close") {
                    Some(f) => f,
                    None => self.module.add_function("moo_handle_close",
                        self.context.void_type().fn_type(&[ptr_type.into()], false), None),
                };
                self.builder.build_call(close, &[(*slot).into()], "").map_err(|e| e.to_string())?;
            }
            self.call_rt_void(self.rt.moo_release, &[(*wert).into()], "native.griff.release")?;
        }
        Ok(())
    }

    /// P2-T23/T53 B3: Zeigerergebnis einer nativen Funktion als Griff (NULL -> nichts).
    fn native_handle_box(
        &mut self,
        zeiger: inkwell::values::PointerValue<'ctx>,
        ziel: Option<&str>,
    ) -> Result<StructValue<'ctx>, String> {
        let typ_id = self.griff_typ_id(ziel)?;
        let i64t = self.context.i64_type();
        let boxer = match self.module.get_function("moo_handle_box") {
            Some(f) => f,
            None => {
                let windows = self.windows_x64_abi();
                let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
                let mut params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
                if windows { params.push(ptr.into()); }
                params.extend([BasicMetadataTypeEnum::from(ptr), i64t.into()]);
                let fn_type = if windows {
                    self.context.void_type().fn_type(&params, false)
                } else {
                    self.mv_type().fn_type(&params, false)
                };
                self.module.add_function("moo_handle_box", fn_type, None)
            }
        };
        self.call_rt(boxer, &[zeiger.into(), i64t.const_int(typ_id, false).into()], "native.griff.box")
    }

    /// P2-T50 B5: Pins eines nativen Aufrufs loesen und die geliehenen Werte freigeben.
    fn native_buffer_unpin(&mut self, pins: &[(inkwell::values::PointerValue<'ctx>, StructValue<'ctx>)]) -> Result<(), String> {
        if pins.is_empty() { return Ok(()); }
        let ptr_type = self.context.ptr_type(inkwell::AddressSpace::default());
        // P2-T83 B2: Leihe zurueckgeben (Pin loesen bzw. Tensor-Maske nach Schreiben).
        let unpin = match self.module.get_function("moo_native_zurueck") {
            Some(f) => f,
            None => self.module.add_function("moo_native_zurueck",
                self.context.void_type().fn_type(&[ptr_type.into()], false), None),
        };
        for (pin, wert) in pins {
            self.builder.build_call(unpin, &[(*pin).into()], "").map_err(|e| e.to_string())?;
            self.call_rt_void(self.rt.moo_release, &[(*wert).into()], "native.puffer.release")?;
        }
        Ok(())
    }

    /// Konvertiert dynamische Number-Werte an der Float-FFI-Grenze.
    fn compile_native_float_argument(&mut self, expr: &Expr, ty: crate::codegen_native::NType, catch: Option<inkwell::basic_block::BasicBlock<'ctx>>) -> Result<inkwell::values::FloatValue<'ctx>, String> {
        use crate::codegen_native::NType;
        if !matches!(ty, NType::F32 | NType::F64) { return Err("Floattyp erwartet".into()); }
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let state_ok = self.context.append_basic_block(function, "native.arg.state_ok");
        let state_error = self.context.append_basic_block(function, "native.arg.state_error");
        let (depth_value, pending) = self.fehler_zustand_laden("native.arg.zustand")?;
        let has_depth = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_value,
            self.context.i32_type().const_zero(), "native.arg.has_handler").map_err(|e| e.to_string())?;
        let has_error = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending,
            self.context.i32_type().const_zero(), "native.arg.has_error").map_err(|e| e.to_string())?;
        let wrong_depth = self.native_argument_wrong_depth(has_depth, catch, "native.arg.no_handler")?;
        let invalid_state = self.builder.build_or(wrong_depth, has_error, "native.arg.invalid_state")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(invalid_state, state_error, state_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(state_error);
        self.native_argument_state_failure(has_depth, has_error, catch)?;
        self.builder.position_at_end(state_ok);
        let value = self.compile_expr(expr)?;
        let value_ok = self.context.append_basic_block(function, "native.arg.value_ok");
        let value_error = self.context.append_basic_block(function, "native.arg.value_error");
        let (depth_after, pending_after) = self.fehler_zustand_laden("native.arg.zustand_after")?;
        let has_depth_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_after,
            self.context.i32_type().const_zero(), "native.arg.has_handler_after").map_err(|e| e.to_string())?;
        let has_error_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending_after,
            self.context.i32_type().const_zero(), "native.arg.has_error_after").map_err(|e| e.to_string())?;
        let wrong_depth_after = self.native_argument_wrong_depth(has_depth_after, catch, "native.arg.no_handler_after")?;
        let bad_after = self.builder.build_or(wrong_depth_after, has_error_after, "native.arg.bad_after")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(bad_after, value_error, value_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(value_error);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.arg.release_pending")?;
        self.native_argument_state_failure(has_depth_after, has_error_after, catch)?;
        self.builder.position_at_end(value_ok);
        let tag_value = self.builder.build_extract_value(value, 0, "native.arg.tag")
            .map_err(|e| e.to_string())?.into_int_value();
        let raw = self.builder.build_extract_value(value, 1, "native.arg.bits")
            .map_err(|e| e.to_string())?.into_int_value();
        let number_bb = self.context.append_basic_block(function, "native.float.number");
        let convert_bb = self.context.append_basic_block(function, "native.float.convert");
        let failure_bb = self.context.append_basic_block(function, "native.float.invalid");
        let is_number = self.builder.build_int_compare(inkwell::IntPredicate::EQ, tag_value,
            self.context.i64_type().const_zero(), "native.float.is_number").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(is_number, number_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(number_bb);
        let number = self.builder.build_bit_cast(raw, self.context.f64_type(), "native.float.number")
            .map_err(|e| e.to_string())?.into_float_value();
        let converted = if ty == NType::F32 {
            let narrowed = self.builder.build_float_trunc(number, self.context.f32_type(), "native.float.f32").map_err(|e| e.to_string())?;
            // Nur endliche Eingaben, deren korrekt gerundetes Ergebnis unendlich ist, scheitern.
            let pos_inf = self.context.f32_type().const_float(f64::INFINITY);
            let neg_inf = self.context.f32_type().const_float(f64::NEG_INFINITY);
            let pos = self.builder.build_float_compare(inkwell::FloatPredicate::OEQ, narrowed, pos_inf, "native.float.pos_inf").map_err(|e| e.to_string())?;
            let neg = self.builder.build_float_compare(inkwell::FloatPredicate::OEQ, narrowed, neg_inf, "native.float.neg_inf").map_err(|e| e.to_string())?;
            let rounded_inf = self.builder.build_or(pos, neg, "native.float.rounded_inf").map_err(|e| e.to_string())?;
            let upper = self.builder.build_float_compare(inkwell::FloatPredicate::OLE, number, self.context.f64_type().const_float(f64::MAX), "native.float.finite_upper").map_err(|e| e.to_string())?;
            let lower = self.builder.build_float_compare(inkwell::FloatPredicate::OGE, number, self.context.f64_type().const_float(-f64::MAX), "native.float.finite_lower").map_err(|e| e.to_string())?;
            let finite = self.builder.build_and(upper, lower, "native.float.finite").map_err(|e| e.to_string())?;
            let overflow = self.builder.build_and(finite, rounded_inf, "native.float.overflow").map_err(|e| e.to_string())?;
            self.builder.build_conditional_branch(overflow, failure_bb, convert_bb).map_err(|e| e.to_string())?;
            narrowed
        } else {
            self.builder.build_unconditional_branch(convert_bb).map_err(|e| e.to_string())?;
            number
        };
        self.builder.position_at_end(failure_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.float.release_error")?;
        let message = self.make_global_str("Native Funktion erwartet eine darstellbare Floatzahl", "native_float_error")?;
        let error = self.call_rt(self.rt.moo_string_new, &[message.into()], "native.float.error")?;
        self.call_rt_void(self.rt.moo_throw, &[error.into()], "native.float.throw")?;
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(convert_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.float.release")?;
        Ok(converted)
    }

    /// Prueft dynamische i64/u64 vor jeder LLVM-Konversion.
    fn compile_native_wide_argument(&mut self, expr: &Expr, ty: crate::codegen_native::NType, catch: Option<inkwell::basic_block::BasicBlock<'ctx>>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        use crate::codegen_native::NType;
        if !matches!(ty, NType::I64 | NType::U64) { return Err("64-Bit-Ganzzahltyp erwartet".into()); }
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let state_ok = self.context.append_basic_block(function, "native.arg.state_ok");
        let state_error = self.context.append_basic_block(function, "native.arg.state_error");
        let (depth_value, pending) = self.fehler_zustand_laden("native.arg.zustand")?;
        let has_depth = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_value,
            self.context.i32_type().const_zero(), "native.arg.has_handler").map_err(|e| e.to_string())?;
        let has_error = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending,
            self.context.i32_type().const_zero(), "native.arg.has_error").map_err(|e| e.to_string())?;
        let wrong_depth = self.native_argument_wrong_depth(has_depth, catch, "native.arg.no_handler")?;
        let invalid_state = self.builder.build_or(wrong_depth, has_error, "native.arg.invalid_state")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(invalid_state, state_error, state_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(state_error);
        self.native_argument_state_failure(has_depth, has_error, catch)?;
        self.builder.position_at_end(state_ok);
        let value = self.compile_expr(expr)?;
        let value_ok = self.context.append_basic_block(function, "native.arg.value_ok");
        let value_error = self.context.append_basic_block(function, "native.arg.value_error");
        let (depth_after, pending_after) = self.fehler_zustand_laden("native.arg.zustand_after")?;
        let has_depth_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, depth_after,
            self.context.i32_type().const_zero(), "native.arg.has_handler_after").map_err(|e| e.to_string())?;
        let has_error_after = self.builder.build_int_compare(inkwell::IntPredicate::NE, pending_after,
            self.context.i32_type().const_zero(), "native.arg.has_error_after").map_err(|e| e.to_string())?;
        let wrong_depth_after = self.native_argument_wrong_depth(has_depth_after, catch, "native.arg.no_handler_after")?;
        let bad_after = self.builder.build_or(wrong_depth_after, has_error_after, "native.arg.bad_after")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(bad_after, value_error, value_ok).map_err(|e| e.to_string())?;
        self.builder.position_at_end(value_error);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.arg.release_pending")?;
        self.native_argument_state_failure(has_depth_after, has_error_after, catch)?;
        self.builder.position_at_end(value_ok);
        let tag_value = self.builder.build_extract_value(value, 0, "native.arg.tag")
            .map_err(|e| e.to_string())?.into_int_value();
        let raw = self.builder.build_extract_value(value, 1, "native.arg.bits")
            .map_err(|e| e.to_string())?.into_int_value();
        let range_bb = self.context.append_basic_block(function, "native.wide.range");
        let convert_bb = self.context.append_basic_block(function, "native.wide.convert");
        let success_bb = self.context.append_basic_block(function, "native.wide.valid");
        let failure_bb = self.context.append_basic_block(function, "native.wide.invalid");
        let is_number = self.builder.build_int_compare(inkwell::IntPredicate::EQ, tag_value,
            self.context.i64_type().const_zero(), "native.wide.is_number").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(is_number, range_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(range_bb);
        let number = self.builder.build_bit_cast(raw, self.context.f64_type(), "native.wide.number")
            .map_err(|e| e.to_string())?.into_float_value();
        // Obere Grenzen sind exklusiv; diese f64-Werte sind exakt 2^63 bzw. 2^64.
        let (minimum, upper) = if ty == NType::I64 {
            (-9223372036854775808.0_f64, 9223372036854775808.0_f64)
        } else { (0.0_f64, 18446744073709551616.0_f64) };
        let lower_ok = self.builder.build_float_compare(inkwell::FloatPredicate::OGE, number,
            self.context.f64_type().const_float(minimum), "native.wide.lower").map_err(|e| e.to_string())?;
        let upper_ok = self.builder.build_float_compare(inkwell::FloatPredicate::OLT, number,
            self.context.f64_type().const_float(upper), "native.wide.upper").map_err(|e| e.to_string())?;
        let range_ok = self.builder.build_and(lower_ok, upper_ok, "native.wide.in_range").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(range_ok, convert_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(convert_bb);
        let integer = if ty == NType::I64 {
            self.builder.build_float_to_signed_int(number, self.context.i64_type(), "native.wide.i64")
        } else {
            self.builder.build_float_to_unsigned_int(number, self.context.i64_type(), "native.wide.u64")
        }.map_err(|e| e.to_string())?;
        let back = if ty == NType::I64 {
            self.builder.build_signed_int_to_float(integer, self.context.f64_type(), "native.wide.back")
        } else {
            self.builder.build_unsigned_int_to_float(integer, self.context.f64_type(), "native.wide.back")
        }.map_err(|e| e.to_string())?;
        let integral = self.builder.build_float_compare(inkwell::FloatPredicate::OEQ, number, back, "native.wide.integral")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(integral, success_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(failure_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.wide.release_error")?;
        let message = self.make_global_str("Native Funktion erwartet eine darstellbare 64-Bit-Ganzzahl", "native_wide_error")?;
        let error = self.call_rt(self.rt.moo_string_new, &[message.into()], "native.wide.error")?;
        self.call_rt_void(self.rt.moo_throw, &[error.into()], "native.wide.throw")?;
        if let Some(target) = catch {
            self.builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
        } else {
            self.native_argument_fatal_exit()?;
        }
        self.builder.position_at_end(success_bb);
        self.call_rt_void(self.rt.moo_release, &[value.into()], "native.wide.release")?;
        Ok(integer)
    }

    /// Erlaubt Boxing nur, wenn genau dieser i64/u64-Wert in f64 darstellbar ist.
    fn box_native_wide(&mut self, integer: inkwell::values::IntValue<'ctx>, unsigned: bool) -> Result<inkwell::values::FloatValue<'ctx>, String> {
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let range_bb = self.context.append_basic_block(function, "native.box.range");
        let compare_bb = self.context.append_basic_block(function, "native.box.compare");
        let failure_bb = self.context.append_basic_block(function, "native.box.invalid");
        let success_bb = self.context.append_basic_block(function, "native.box.exact");
        let done_bb = self.context.append_basic_block(function, "native.box.done");
        let number = if unsigned {
            self.builder.build_unsigned_int_to_float(integer, self.context.f64_type(), "native.box.wide")
        } else {
            self.builder.build_signed_int_to_float(integer, self.context.f64_type(), "native.box.wide")
        }.map_err(|e| e.to_string())?;
        self.builder.build_unconditional_branch(range_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(range_bb);
        let upper = if unsigned { 18446744073709551616.0_f64 } else { 9223372036854775808.0_f64 };
        let valid = self.builder.build_float_compare(inkwell::FloatPredicate::OLT, number,
            self.context.f64_type().const_float(upper), "native.box.in_range").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(valid, compare_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(compare_bb);
        // Erst nach dem Bereichscheck ist die Rueckkonversion frei von LLVM-Poison.
        let back = if unsigned {
            self.builder.build_float_to_unsigned_int(number, self.context.i64_type(), "native.box.back")
        } else {
            self.builder.build_float_to_signed_int(number, self.context.i64_type(), "native.box.back")
        }.map_err(|e| e.to_string())?;
        let exact = self.builder.build_int_compare(inkwell::IntPredicate::EQ, integer, back, "native.box.exact")
            .map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(exact, success_bb, failure_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(failure_bb);
        let message = self.make_global_str("Native Funktion liefert eine nicht exakt darstellbare 64-Bit-Ganzzahl", "native_box_error")?;
        let error = self.call_rt(self.rt.moo_string_new, &[message.into()], "native.box.error")?;
        self.call_rt_void(self.rt.moo_throw, &[error.into()], "native.box.throw")?;
        // P2-T58 (B5-Befund): frueher 'ret i32 1' - ausserhalb von main ungueltige
        // IR (Verify-Abbruch bei i64/u64-Rueckgabe in benannten Funktionen), in
        // main auch innerhalb versuche: ein Programmabbruch. Jetzt wie der Skip-
        // Pfad (T78/T80): ohne Handler beendet moo_throw den Prozess wie bisher,
        // mit Handler bleibt das Flag gesetzt, der Aufruf liefert den Platzhalter
        // 0 und der naechste Guard bzw. Pruefpunkt faengt den Fehler.
        self.builder.build_unconditional_branch(done_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(success_bb);
        self.builder.build_unconditional_branch(done_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(done_bb);
        let phi = self.builder.build_phi(self.context.f64_type(), "native.box.wert").map_err(|e| e.to_string())?;
        let platzhalter = self.context.f64_type().const_float(0.0);
        phi.add_incoming(&[(&number, success_bb), (&platzhalter, failure_bb)]);
        Ok(phi.as_basic_value().into_float_value())
    }

    /// P7-T17: MooValue-Tags der Bare-Runtime fuer exakte 64-Bit-Ganzzahlen
    /// (moo_bare.c BARE_TAG_I64/U64; in moo_runtime.h reserviert). data traegt
    /// die Bits direkt, kein double-Umweg.
    const BARE_TAG_I64: u64 = 30;
    const BARE_TAG_U64: u64 = 31;

    /// P7-T17: i64/u64-Bits als exakten Bare-Wert boxen (reine IR).
    fn bare_box_wide(&mut self, bits: inkwell::values::IntValue<'ctx>, unsigned: bool) -> Result<StructValue<'ctx>, String> {
        let tag = self.context.i64_type().const_int(if unsigned { Self::BARE_TAG_U64 } else { Self::BARE_TAG_I64 }, false);
        let leer = self.mv_type().get_undef();
        let mit_tag = self.builder.build_insert_value(leer, tag, 0, "bare.wide.tag").map_err(|e| e.to_string())?;
        let wert = self.builder.build_insert_value(mit_tag, bits, 1, "bare.wide.bits").map_err(|e| e.to_string())?;
        Ok(wert.into_struct_value())
    }

    /// P7-T17: Trap im Bare-Profil (kein fangbarer Fehlerkanal, Vertrag P2-T73).
    fn bare_trap(&mut self) -> Result<(), String> {
        let intrinsic = inkwell::intrinsics::Intrinsic::find("llvm.trap").ok_or("llvm.trap fehlt")?;
        let trap = intrinsic.get_declaration(&self.module, &[]).ok_or("llvm.trap-Deklaration fehlt")?;
        self.builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
        self.builder.build_unreachable().map_err(|e| e.to_string())?;
        Ok(())
    }

    /// P7-T17: Konvertiert einen dynamischen Moo-Wert im Bare/UEFI-Profil
    /// geprueft in einen nativen Ganzzahl- oder bool-Parameter.
    /// Zahl (Tag 0): endlich, ganzzahlig, im Zielbereich (wie hosted).
    /// Exakte 64-Bit-Werte (Tag 30 i64 / 31 u64): Bits direkt, nur
    /// Bereichspruefung als Ganzzahl - kein double, kein Praezisionsverlust.
    /// bool: nur Tag 2. Alles andere und jeder Bereichsfehler: llvm.trap.
    /// Werte dieser Tags tragen keinen Heap, daher kein release noetig.
    fn compile_bare_native_argument(&mut self, expr: &Expr, ty: crate::codegen_native::NType) -> Result<BasicMetadataValueEnum<'ctx>, String> {
        use crate::codegen_native::NType;
        let function = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
        let i64t = self.context.i64_type();
        let f64t = self.context.f64_type();
        let value = self.compile_expr(expr)?;
        let tag = self.builder.build_extract_value(value, 0, "bare.arg.tag")
            .map_err(|e| e.to_string())?.into_int_value();
        let bits = self.builder.build_extract_value(value, 1, "bare.arg.bits")
            .map_err(|e| e.to_string())?.into_int_value();
        let trap_bb = self.context.append_basic_block(function, "bare.arg.trap");
        if ty == NType::Bool {
            let ok_bb = self.context.append_basic_block(function, "bare.arg.bool");
            let ist_bool = self.builder.build_int_compare(inkwell::IntPredicate::EQ, tag,
                i64t.const_int(2, false), "bare.arg.is_bool").map_err(|e| e.to_string())?;
            self.builder.build_conditional_branch(ist_bool, ok_bb, trap_bb).map_err(|e| e.to_string())?;
            self.builder.position_at_end(trap_bb);
            self.bare_trap()?;
            self.builder.position_at_end(ok_bb);
            let flag = self.builder.build_int_compare(inkwell::IntPredicate::NE, bits,
                i64t.const_zero(), "bare.arg.flag").map_err(|e| e.to_string())?;
            return Ok(flag.into());
        }
        let (minimum, maximum, breite): (i128, i128, u32) = match ty {
            NType::I8 => (i8::MIN as i128, i8::MAX as i128, 8),
            NType::U8 => (0, u8::MAX as i128, 8),
            NType::I16 => (i16::MIN as i128, i16::MAX as i128, 16),
            NType::U16 => (0, u16::MAX as i128, 16),
            NType::I32 => (i32::MIN as i128, i32::MAX as i128, 32),
            NType::U32 => (0, u32::MAX as i128, 32),
            NType::I64 => (i64::MIN as i128, i64::MAX as i128, 64),
            NType::U64 => (0, u64::MAX as i128, 64),
            _ => return Err("Nicht unterstuetzter nativer Parametertyp im Bare-Profil".into()),
        };
        let zahl_bb = self.context.append_basic_block(function, "bare.arg.zahl");
        let wandel_bb = self.context.append_basic_block(function, "bare.arg.wandel");
        let signed_bb = self.context.append_basic_block(function, "bare.arg.i64");
        let unsigned_bb = self.context.append_basic_block(function, "bare.arg.u64");
        let fertig_bb = self.context.append_basic_block(function, "bare.arg.fertig");
        self.builder.build_switch(tag, trap_bb, &[
            (i64t.const_zero(), zahl_bb),
            (i64t.const_int(Self::BARE_TAG_I64, false), signed_bb),
            (i64t.const_int(Self::BARE_TAG_U64, false), unsigned_bb),
        ]).map_err(|e| e.to_string())?;
        self.builder.position_at_end(trap_bb);
        self.bare_trap()?;
        // Zahl: Bereich vor der Konversion (kein LLVM-Poison), dann Rundreise.
        self.builder.position_at_end(zahl_bb);
        let zahl = self.builder.build_bit_cast(bits, f64t, "bare.arg.f64")
            .map_err(|e| e.to_string())?.into_float_value();
        let (unten, oben, oben_inklusiv) = match ty {
            NType::I64 => (-9223372036854775808.0_f64, 9223372036854775808.0_f64, false),
            NType::U64 => (0.0_f64, 18446744073709551616.0_f64, false),
            _ => (minimum as f64, maximum as f64, true),
        };
        let unten_ok = self.builder.build_float_compare(inkwell::FloatPredicate::OGE, zahl,
            f64t.const_float(unten), "bare.arg.unten").map_err(|e| e.to_string())?;
        let oben_ok = self.builder.build_float_compare(
            if oben_inklusiv { inkwell::FloatPredicate::OLE } else { inkwell::FloatPredicate::OLT },
            zahl, f64t.const_float(oben), "bare.arg.oben").map_err(|e| e.to_string())?;
        let im_bereich = self.builder.build_and(unten_ok, oben_ok, "bare.arg.bereich").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(im_bereich, wandel_bb, trap_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(wandel_bb);
        let ganz = if ty == NType::U64 {
            self.builder.build_float_to_unsigned_int(zahl, i64t, "bare.arg.ganz")
        } else {
            self.builder.build_float_to_signed_int(zahl, i64t, "bare.arg.ganz")
        }.map_err(|e| e.to_string())?;
        let zurueck = if ty == NType::U64 {
            self.builder.build_unsigned_int_to_float(ganz, f64t, "bare.arg.zurueck")
        } else {
            self.builder.build_signed_int_to_float(ganz, f64t, "bare.arg.zurueck")
        }.map_err(|e| e.to_string())?;
        let ganzzahlig = self.builder.build_float_compare(inkwell::FloatPredicate::OEQ, zahl, zurueck,
            "bare.arg.ganzzahlig").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(ganzzahlig, fertig_bb, trap_bb).map_err(|e| e.to_string())?;
        // Exaktes i64 (Tag 30): vorzeichenbehafteter Bereichsvergleich.
        self.builder.position_at_end(signed_bb);
        let s_min = minimum.max(i64::MIN as i128) as i64;
        let s_max = maximum.min(i64::MAX as i128) as i64;
        let s_unten = self.builder.build_int_compare(inkwell::IntPredicate::SGE, bits,
            i64t.const_int(s_min as u64, true), "bare.arg.s_unten").map_err(|e| e.to_string())?;
        let s_oben = self.builder.build_int_compare(inkwell::IntPredicate::SLE, bits,
            i64t.const_int(s_max as u64, true), "bare.arg.s_oben").map_err(|e| e.to_string())?;
        let s_ok = self.builder.build_and(s_unten, s_oben, "bare.arg.s_ok").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(s_ok, fertig_bb, trap_bb).map_err(|e| e.to_string())?;
        // Exaktes u64 (Tag 31): vorzeichenloser Vergleich gegen das Maximum.
        self.builder.position_at_end(unsigned_bb);
        let u_max = maximum.min(u64::MAX as i128) as u64;
        let u_ok = self.builder.build_int_compare(inkwell::IntPredicate::ULE, bits,
            i64t.const_int(u_max, false), "bare.arg.u_ok").map_err(|e| e.to_string())?;
        self.builder.build_conditional_branch(u_ok, fertig_bb, trap_bb).map_err(|e| e.to_string())?;
        self.builder.position_at_end(fertig_bb);
        let phi = self.builder.build_phi(i64t, "bare.arg.wert").map_err(|e| e.to_string())?;
        phi.add_incoming(&[(&ganz, wandel_bb), (&bits, signed_bb), (&bits, unsigned_bb)]);
        let wert = phi.as_basic_value().into_int_value();
        let ziel = match breite {
            8 => self.builder.build_int_truncate(wert, self.context.i8_type(), "bare.arg.i8").map_err(|e| e.to_string())?,
            16 => self.builder.build_int_truncate(wert, self.context.i16_type(), "bare.arg.i16").map_err(|e| e.to_string())?,
            32 => self.builder.build_int_truncate(wert, self.context.i32_type(), "bare.arg.i32").map_err(|e| e.to_string())?,
            _ => wert,
        };
        Ok(ziel.into())
    }

    fn compile_expr(&mut self, expr: &Expr) -> Result<StructValue<'ctx>, String> {
        match expr {
            Expr::NativeNumber { line, column, .. } => Err(format!("Natives Zahlenliteral bei {line}:{column} wird noch nicht unterstützt")),
            Expr::Number(n) => {
                let f = self.context.f64_type().const_float(*n);
                self.call_rt(self.rt.moo_number, &[f.into()], "num")
            }
            Expr::String(s) => {
                let ptr = self.make_global_str(s, "str")?;
                self.call_rt(self.rt.moo_string_new, &[ptr.into()], "str")
            }
            Expr::Boolean(b) => {
                let bval = self.context.bool_type().const_int(*b as u64, false);
                self.call_rt(self.rt.moo_bool_fn, &[bval.into()], "bool")
            }
            Expr::None => {
                self.call_rt(self.rt.moo_none, &[], "none")
            }
            Expr::Identifier(name) => {
                // Variable (lokal, global) ODER Funktionsname als First-Class-Value.
                // Erstverwendung-Fallback auf module.get_function erlaubt:
                //   starte(worker, arg)
                //   setze f auf meine_fn
                //   liste.map(transformiere)
                if let Ok(v) = self.load_var(name) {
                    return Ok(v);
                }
                if self.native_functions.contains_key(name) {
                    return self.nativer_funktionswert(name);
                }
                if self.extern_c_names.contains(name) {
                    return Err(crate::diagnose::vormerken("M-N003", self.diag_zeile, format!(
                        "extern-C-Funktionswert '{name}' ist nicht möglich: als Moo-Wert eine @nativ-Hülle verwenden (@nativ-Funktion, die '{name}' in unsicher: aufruft); C-Funktionszeiger über @rückruf")));
                }
                if let Some(llvm_fn) = self.module.get_function(name) {
                    let callable_fn = self.windows_callable_wrapper(llvm_fn, name)?;
                    let fn_ptr = callable_fn.as_global_value().as_pointer_value();
                    let arity = self.context.i32_type()
                        .const_int(llvm_fn.count_params() as u64, false);
                    let name_ptr = self.make_global_str(name, &format!("__fname_{name}"))?;
                    return self.call_rt(self.rt.moo_func_new,
                        &[fn_ptr.into(), arity.into(), name_ptr.into()],
                        &format!("__func_val_{name}"));
                }
                Err(format!("Variable '{name}' nicht gefunden"))
            }
            Expr::This => {
                // selbst/this laden
                self.load_var("selbst")
                    .or_else(|_| self.load_var("this"))
            }
            Expr::BinaryOp { left, op, right } => {
                let guard_operands = std::mem::take(&mut self.stmt_root_guard) || self.stmt_recursive_guard;
                // Zig-inspiriert: Const-Folding — bei Number-Literalen zur Compile-Zeit berechnen
                if let (Expr::Number(a), Expr::Number(b)) = (left.as_ref(), right.as_ref()) {
                    let result = match op {
                        BinOp::Add => Some(*a + *b),
                        BinOp::Sub => Some(*a - *b),
                        BinOp::Mul => Some(*a * *b),
                        BinOp::Div if *b != 0.0 => Some(*a / *b),
                        BinOp::Mod if *b != 0.0 => Some(a % b),
                        BinOp::Pow => Some(a.powf(*b)),
                        _ => None,
                    };
                    if let Some(val) = result {
                        let f = self.context.f64_type().const_float(val);
                        return self.call_rt(self.rt.moo_number, &[f.into()], "const");
                    }
                    // Bool-Vergleiche zur Compile-Zeit
                    let bool_result = match op {
                        BinOp::Eq => Some(*a == *b),
                        BinOp::NotEq => Some(*a != *b),
                        BinOp::Less => Some(*a < *b),
                        BinOp::Greater => Some(*a > *b),
                        BinOp::LessEq => Some(*a <= *b),
                        BinOp::GreaterEq => Some(*a >= *b),
                        _ => None,
                    };
                    if let Some(val) = bool_result {
                        let bval = self.context.bool_type().const_int(val as u64, false);
                        return self.call_rt(self.rt.moo_bool_fn, &[bval.into()], "const_cmp");
                    }
                }
                // String-Concat von Literalen zur Compile-Zeit
                if let (BinOp::Add, Expr::String(a), Expr::String(b)) = (op, left.as_ref(), right.as_ref()) {
                    let combined = format!("{a}{b}");
                    let ptr = self.make_global_str(&combined, "const_str")?;
                    return self.call_rt(self.rt.moo_string_new, &[ptr.into()], "const_concat");
                }

                let lhs = self.compile_expr(left)?;
                if guard_operands { self.guard_local_expr_error(&[lhs])?; }
                if self.native_value_temps() { self.stmt_owned_context.push(lhs); }
                let rhs_result = self.compile_expr(right);
                if self.native_value_temps() { self.stmt_owned_context.pop(); }
                let rhs = rhs_result?;
                if guard_operands { self.guard_local_expr_error(&[lhs, rhs])?; }

                // Operator-Ueberladung: pruefe ob eine Klasse __op__ Methode hat
                let dunder_name = match op {
                    BinOp::Add => "__add__", BinOp::Sub => "__sub__",
                    BinOp::Mul => "__mul__", BinOp::Div => "__div__",
                    BinOp::Mod => "__mod__", BinOp::Eq => "__eq__",
                    BinOp::Less => "__lt__", BinOp::Greater => "__gt__",
                    BinOp::LessEq => "__lte__", BinOp::GreaterEq => "__gte__",
                    _ => "",
                };

                // Sammle alle Klassen die diese __op__ Methode haben
                let overloads: Vec<(String, FunctionValue)> = if !dunder_name.is_empty() {
                    self.class_methods.iter()
                        .filter(|((_, method), _)| method == dunder_name)
                        .map(|((class, _), func)| (class.clone(), *func))
                        .collect()
                } else {
                    vec![]
                };

                if !overloads.is_empty() {
                    let func_cur = self.current_function.unwrap();
                    // Alloca VOR dem Branch (im aktuellen Block)
                    let result_ptr = self.builder.build_alloca(self.mv_type(), "op_res")
                        .map_err(|e| format!("{e}"))?;

                    let tag = self.builder.build_extract_value(lhs, 0, "tag")
                        .map_err(|e| format!("{e}"))?;
                    let is_obj = self.builder.build_int_compare(
                        inkwell::IntPredicate::EQ, tag.into_int_value(),
                        self.context.i64_type().const_int(tag::OBJECT, false), // MOO_OBJECT
                        "is_obj").map_err(|e| format!("{e}"))?;

                    let obj_bb = self.context.append_basic_block(func_cur, "op_obj");
                    let normal_bb = self.context.append_basic_block(func_cur, "op_normal");
                    let merge_bb = self.context.append_basic_block(func_cur, "op_merge");

                    self.builder.build_conditional_branch(is_obj, obj_bb, normal_bb)
                        .map_err(|e| format!("{e}"))?;

                    // Anhand der tatsaechlichen Klasse dispatchen. Unterklassen
                    // erben die naechste definierte Ueberladung ihrer Eltern.
                    self.builder.position_at_end(obj_bb);
                    let class_call = self.call_rt_abi(
                        self.rt.moo_object_class_name, &[lhs.into()], "op_class")?;
                    let class_ptr = match class_call.try_as_basic_value() {
                        inkwell::values::ValueKind::Basic(v) => v.into_pointer_value(),
                        _ => return Err("moo_object_class_name hat keinen Wert".to_string()),
                    };
                    let strcmp_fn = self.module.get_function("strcmp").unwrap_or_else(|| {
                        let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
                        self.module.add_function("strcmp",
                            self.context.i32_type().fn_type(&[ptr.into(), ptr.into()], false), None)
                    });
                    let mut classes: Vec<String> = self.class_methods.keys()
                        .map(|(class, _)| class.clone())
                        .chain(self.class_parents.keys().cloned())
                        .collect();
                    classes.sort();
                    classes.dedup();
                    for (index, class_name) in classes.iter().enumerate() {
                        let mut current = class_name.clone();
                        let mut visited = std::collections::HashSet::new();
                        let method_fn = loop {
                            if !visited.insert(current.clone()) {
                                return Err(format!("Zyklische Klassenvererbung bei {class_name}"));
                            }
                            if let Some(func) = self.class_methods.get(&(current.clone(), dunder_name.to_string())) {
                                break Some(*func);
                            }
                            match self.class_parents.get(&current) {
                                Some(parent) => current = parent.clone(),
                                None => break None,
                            }
                        };
                        let Some(method_fn) = method_fn else { continue };
                        if method_fn.count_params() != 2 {
                            return Err(format!("{class_name}.{dunder_name} erwartet genau ein Argument"));
                        }
                        let name_ptr = self.make_global_str(class_name, &format!("op_class_{index}"))?;
                        let comparison = self.builder.build_call(
                            strcmp_fn, &[class_ptr.into(), name_ptr.into()], "op_class_cmp"
                        ).map_err(|e| format!("{e}"))?;
                        let cmp = match comparison.try_as_basic_value() {
                            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                            _ => return Err("strcmp hat keinen Wert".to_string()),
                        };
                        let matches = self.builder.build_int_compare(
                            inkwell::IntPredicate::EQ, cmp, self.context.i32_type().const_zero(), "op_class_match"
                        ).map_err(|e| format!("{e}"))?;
                        let call_bb = self.context.append_basic_block(func_cur, "op_overload_call");
                        let next_bb = self.context.append_basic_block(func_cur, "op_overload_next");
                        self.builder.build_conditional_branch(matches, call_bb, next_bb)
                            .map_err(|e| format!("{e}"))?;
                        self.builder.position_at_end(call_bb);
                        // User-Methoden konsumieren beide Operand-Refs; kein Post-Call-Release.
                        let result = self.call_rt(method_fn, &[lhs.into(), rhs.into()], "op_overload")?;
                        self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;
                        self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                        self.builder.position_at_end(next_bb);
                    }
                    // Keine passende Ueberladung: normaler Runtime-Operator.
                    self.builder.build_unconditional_branch(normal_bb).map_err(|e| format!("{e}"))?;

                    // Normal-Branch: Standard Runtime-Call
                    self.builder.position_at_end(normal_bb);
                    let rt_func = match op {
                        BinOp::Add => self.rt.moo_add,
                        BinOp::Sub => self.rt.moo_sub,
                        BinOp::Mul => self.rt.moo_mul,
                        BinOp::Div => self.rt.moo_div,
                        BinOp::Mod => self.rt.moo_mod,
                        BinOp::Pow => self.rt.moo_pow,
                        BinOp::Eq => self.rt.moo_eq,
                        BinOp::NotEq => self.rt.moo_neq,
                        BinOp::Less => self.rt.moo_lt,
                        BinOp::Greater => self.rt.moo_gt,
                        BinOp::LessEq => self.rt.moo_lte,
                        BinOp::GreaterEq => self.rt.moo_gte,
                        BinOp::And => self.rt.moo_and,
                        BinOp::Or => self.rt.moo_or,
                        BinOp::BitAnd => self.rt.moo_bitand,
                        BinOp::BitOr => self.rt.moo_bitor,
                        BinOp::BitXor => self.rt.moo_bitxor,
                        BinOp::LShift => self.rt.moo_lshift,
                        BinOp::RShift => self.rt.moo_rshift,
                    };
                    let normal_result = self.call_rt(rt_func, &[lhs.into(), rhs.into()], "op_normal")?;
                    self.builder.build_store(result_ptr, normal_result).map_err(|e| format!("{e}"))?;
                    // Analog zu Z.1923-1924 im else-Branch: Operand-Temps freigeben.
                    // Bisher fehlte hier das Release wenn Overloads existierten, aber
                    // das tatsaechliche Objekt keinen Object-Tag hatte (z.B. Vec-Klasse
                    // mit __add__ definiert, aber Zahl + Zahl ueber diesen Pfad).
                    self.call_rt_void(self.rt.moo_release, &[lhs.into()], "rel_lhs_op_normal")?;
                    self.call_rt_void(self.rt.moo_release, &[rhs.into()], "rel_rhs_op_normal")?;
                    self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

                    self.builder.position_at_end(merge_bb);
                    let final_val = self.builder.build_load(self.mv_type(), result_ptr, "op_result")
                        .map_err(|e| format!("{e}"))?.into_struct_value();
                    Ok(final_val)
                } else {
                    // Kein Operator-Overload: normaler Call
                    let func = match op {
                        BinOp::Add => self.rt.moo_add,
                        BinOp::Sub => self.rt.moo_sub,
                        BinOp::Mul => self.rt.moo_mul,
                        BinOp::Div => self.rt.moo_div,
                        BinOp::Mod => self.rt.moo_mod,
                        BinOp::Pow => self.rt.moo_pow,
                        BinOp::Eq => self.rt.moo_eq,
                        BinOp::NotEq => self.rt.moo_neq,
                        BinOp::Less => self.rt.moo_lt,
                        BinOp::Greater => self.rt.moo_gt,
                        BinOp::LessEq => self.rt.moo_lte,
                        BinOp::GreaterEq => self.rt.moo_gte,
                        BinOp::And => self.rt.moo_and,
                        BinOp::Or => self.rt.moo_or,
                        BinOp::BitAnd => self.rt.moo_bitand,
                        BinOp::BitOr => self.rt.moo_bitor,
                        BinOp::BitXor => self.rt.moo_bitxor,
                        BinOp::LShift => self.rt.moo_lshift,
                        BinOp::RShift => self.rt.moo_rshift,
                    };
                    let op_result = self.call_rt(func, &[lhs.into(), rhs.into()], "op")?;
                    // Operand-Temps freigeben: die Op hat ihre eigenen Refs
                    // auf das Ergebnis gesetzt, die +1-owning der Operanden
                    // gehoeren zu diesem Stmt-scope und leakten sonst.
                    self.call_rt_void(self.rt.moo_release, &[lhs.into()], "rel_lhs")?;
                    self.call_rt_void(self.rt.moo_release, &[rhs.into()], "rel_rhs")?;
                    Ok(op_result)
                }
            }
            Expr::UnaryOp { op, operand } => {
                let val = self.compile_defer_expr(operand, &[])?;
                let func = match op {
                    UnaryOpKind::Neg => self.rt.moo_neg,
                    UnaryOpKind::Not => self.rt.moo_not,
                    UnaryOpKind::BitNot => self.rt.moo_bitnot,
                };
                let res = self.call_rt(func, &[val.into()], "unary")?;
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_un")?;
                Ok(res)
            }
            Expr::FunctionCall { name, type_args, args, zeile: aufruf_zeile, .. } => {
                let aufruf_zeile = *aufruf_zeile;
                if aufruf_zeile > 0 {
                    self.diag_zeile = aufruf_zeile;
                }
                let guard_args = std::mem::take(&mut self.stmt_root_guard) || self.stmt_recursive_guard;
                if matches!(name.as_str(), "rueckruf_registrieren" | "rückruf_registrieren") {
                    if self.registration_builtin_visible(name) {
                        return self.compile_registration(type_args, args);
                    }
                    if !type_args.is_empty() {
                        return Err(format!("'{name}' ist durch eine eigene Funktion verdeckt; typisierte Registrierung ist hier nicht eindeutig"));
                    }
                }
                if name == "rueckruf_abmelden" && self.registration_builtin_visible(name) {
                    if !type_args.is_empty() { return Err("rueckruf_abmelden erwartet keine Typargumente".into()); }
                    return self.compile_registration_close(args);
                }
                if let Some(function) = self.native_functions.get(name).copied() {
                    let signature = match self.native_signatures.get(name).cloned() {
                        Some(signature) => signature,
                        None => return Err(match self.native_dyn_sperre.get(name) {
                            Some(grund) => format!("Native Funktion '{name}' ist aus dynamischem Moo nicht aufrufbar: {grund}"),
                            None => format!("Native Funktion '{name}' ist aus dynamischem Moo nicht aufrufbar: ihre Signatur hat keine dynamische Form; aus nativem Code aufrufen oder eine @nativ-Huelle mit Ganzzahl-/bool-Parametern schreiben"),
                        }),
                    };
                    // P2-T66 Stufe 2a: hosted Systemkoerper rechnen C-artig (Zeiger,
                    // Wrap, kein Boxing), deshalb nur explizit `unsicher:` und nur in
                    // main oder einer benannten Funktion. P2-T74: Moo-interne hosted
                    // Koerper (__moo_native.*, Spiegel von native_system.rs fangbar())
                    // melden Fehler seit P2-T34 ueber moo_native_fehler und kehren mit
                    // Nullwert zurueck; versuche: prueft das Flag nach jedem Statement
                    // (compile_try_catch) bzw. der Statement-Guard vor dem naechsten
                    // Seiteneffekt (guard_local_expr_error). Der Aufruf ist dort also
                    // erlaubt. Bare-Koerper und C-Exporte (@einstieg) trappen weiter
                    // und bleiben ausserhalb von versuche:/aufraeumen gesperrt.
                    // P2-T12 C3: sichere @nativ-Koerper (safe_native_functions) sind
                    // kein unsicher:-Systemaufruf, sondern laufen wie reine Natives
                    // ueber plain_fangbar (ohne unsicher:-Pflicht an der Aufrufstelle).
                    let system_call = self.hosted_system_functions.contains(&function)
                        && !self.safe_native_functions.contains(name);
                    let system_fangbar = system_call && !self.bare_mode
                        && function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."));
                    // P2-T80: reine @nativ-Koerper (ohne unsicher:, kein FFI) melden hosted
                    // Fehler ebenfalls ueber moo_native_fehler (Weak-Hook, codegen_native::
                    // native_fehler_hook); der Trap-Vertrag gilt nur fuer bare/wasm und
                    // C-Exporte (@einstieg verliert den __moo_native.-Namen, set_name).
                    // Ihre Argumentadapter duerfen deshalb wie bei Systemkoerpern in
                    // benannten Funktionen und versuche:-Teilausdruecken propagieren.
                    let wasm_target = self.module.get_triple().as_str().to_str()
                        .map(|t| t.starts_with("wasm")).unwrap_or(true);
                    // P2-T55 (Plan f5b52a0b E1): auch die FFI-Einzelform (unsicher:-
                    // Wrapper um einen skalaren C-Aufruf) ist hosted fangbar; ihre
                    // Argumentadapter und das Ergebnis-Boxing teilen die Fehlerausgaenge.
                    let plain_fangbar = !system_call && !self.bare_mode && !wasm_target
                        && function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."));
                    let fangbar = system_fangbar || plain_fangbar;
                    let in_main = self.current_function == self.module.get_function("main");
                    // P2-T66 (b) / P2-T85 B1: dynamischer Standardrahmen (benannte
                    // Top-Level-Funktion, Methode, Lambda) mit Fehlerausgang.
                    let in_named_function = !in_main && self.im_standardrahmen();
                    if system_call {
                        if !self.unsafe_context {
                            return Err(format!("Aufruf des nativen Systemkoerpers '{name}' nur innerhalb von unsicher: (C-artige Semantik; Fehler sind hosted fangbar, im Bare-Profil und in C-Exporten fatal)"));
                        }
                        if !(in_main || in_named_function) {
                            return Err(format!("Aufruf des nativen Systemkoerpers '{name}' ist nur in main, einer Funktion, Methode oder einem Lambda erlaubt"));
                        }
                        if self.rahmen_hat_handler() && !system_fangbar {
                            return Err(format!("Aufruf des nativen Systemkoerpers '{name}' ist innerhalb von versuche: oder einem aufraeumen-Rahmen nur hosted erlaubt (im Bare-Profil waere ein Trap nicht fangbar)"));
                        }
                        // P2-T77: fangbare Koerper laufen in Funktionen mit aufraeumen
                        // ueber dieselben Pfade wie Moo-Fehler (emit_defers sichert den
                        // Erstfehler; Runtime-Frame/geliehener Handler fangen das Flag);
                        // der Adapter-Propagationsausgang fuehrt die Defers selbst aus.
                        if (self.defer_runtime || !self.defer_stack.is_empty() || self.defer_borrowed_handler.is_some())
                            && !system_fangbar
                        {
                            return Err(format!("Aufruf des nativen Systemkoerpers '{name}' ist in Funktionen mit aufraeumen nur hosted erlaubt (im Bare-Profil waere ein Trap nicht fangbar)"));
                        }
                    }
                    // Argumentkonvertierung ausserhalb von main nur fuer Systemkoerper-
                    // und (P2-T80) fangbare reine @nativ-Aufrufe in benannten Funktionen;
                    // deren Fehlerpfad propagiert flag-basiert (native_argument_fatal_exit
                    // Direktform bzw. Skip-Block) statt mit dem main-Return.
                    let argument_frame_ok = in_main || ((system_call || plain_fangbar) && in_named_function);
                    // P2-T55 A1: hosted laeuft die FFI-Einzelform ueber plain_fangbar
                    // (Direktform, Skip-Block, Pruefpunkt, aufraeumen-Rahmen wie reine
                    // @nativ-Aufrufe). Nur nicht fangbare Ziele (wasm) behalten die
                    // alten Sperren: dort beendet der Wide-Fehlerpfad main.
                    if self.native_ffi_functions.contains(name) && !plain_fangbar
                        && !self.defer_catalog.actions().is_empty()
                        && (matches!(signature.result, Some(crate::codegen_native::NType::I64 | crate::codegen_native::NType::U64))
                            || signature.params.iter().any(|ty| matches!(ty, crate::codegen_native::NType::I64 | crate::codegen_native::NType::U64)))
                    {
                        return Err("Native 64-Bit-FFI bei vorhandenem aufräumen ist noch nicht unterstützt".into());
                    }
                    if self.native_ffi_functions.contains(name) && !plain_fangbar
                        && (self.current_function != self.module.get_function("main")
                            || self.rahmen_hat_handler() || self.bare_mode)
                    {
                        return Err("Native FFI-Aufrufe sind derzeit nur in main ausserhalb von try unterstützt".into());
                    }
                    // P2-T49 B3: ein Strukturergebnis kommt ueber den sret-Puffer (Parameter 0).
                    let struktur_ergebnis = self.native_struct_results.get(name).cloned();
                    let erwartet = (function.count_params() as usize)
                        .saturating_sub(usize::from(struktur_ergebnis.is_some()));
                    if args.len() != erwartet {
                        return Err(crate::diagnose::vormerken("M-N004", self.diag_zeile,
                            crate::codegen_native::aritaet_meldung(name, erwartet, args.len())));
                    }
                    if self.bare_mode {
                        // P2-T43 (#37326) / P7-T17: runtime-frei nur Systemkoerper.
                        // Ganzzahl- und bool-Argumente duerfen beliebige Ausdruecke
                        // sein: Literale bleiben Konstanten, alles andere laeuft durch
                        // compile_bare_native_argument (Tagpruefung, Bereich, exakte
                        // 64-Bit-Tags 30/31 ohne double, Trap bei Fehler - das
                        // Bare-Profil hat keinen fangbaren Fehlerkanal). i64/u64-
                        // Rueckgaben werden exakt als Tag 30/31 geboxt. Float-, Puffer-,
                        // Griff- und Strukturargumente bleiben gesperrt.
                        let ohne_puffer = self.native_buffer_params.get(name)
                            .is_none_or(|p| p.iter().all(|e| e.is_none()));
                        let args_ok = ohne_puffer && signature.params.iter().all(|ty| matches!(ty,
                            crate::codegen_native::NType::Bool
                            | crate::codegen_native::NType::I8 | crate::codegen_native::NType::U8
                            | crate::codegen_native::NType::I16 | crate::codegen_native::NType::U16
                            | crate::codegen_native::NType::I32 | crate::codegen_native::NType::U32
                            | crate::codegen_native::NType::I64 | crate::codegen_native::NType::U64));
                        if !(system_call && args_ok) {
                            return Err("Dynamische native Aufrufe sind im Bare-Profil noch nicht unterstützt (erlaubt: unsicher:-Systemkoerper mit Ganzzahl-/bool-Argumenten; keine Float-, Puffer-, Griff- oder Strukturargumente)".into());
                        }
                    }
                    // P2-T78: Nicht-Direktform (Aufruf in Argumentposition oder
                    // Teilausdruck, kein lokaler Catch) in benannten Funktionen und in
                    // main innerhalb versuche:. Ein Adapterfehler wird wie ein
                    // Koerperfehler behandelt: moo_throw setzt das Flag (ohne Handler
                    // Prozessende), der Aufruf wird uebersprungen (native.skip) und
                    // liefert nichts; der Rest des Statements laeuft wie bei jedem
                    // dynamischen Moo-Fehler weiter, der naechste Pruefpunkt
                    // (compile_try_catch, guard_local_expr_error, Aufrufer) faengt.
                    // Kein direkter Sprung zum Handler: bereits kompilierte
                    // Geschwisterwerte des umgebenden Ausdrucks wuerden leaken.
                    // Nur hosted fangbare Koerper (fangbar: Systemkoerper, seit P2-T80
                    // auch reine @nativ-Koerper); Direktform, Bare/Exporte/FFI und main
                    // ohne Handler behalten den Bestand.
                    let local_catch = self.native_local_catch();
                    let needs_adapter = args.iter().zip(&signature.params).any(|(arg, ty)| match ty {
                        crate::codegen_native::NType::Bool => !matches!(arg, Expr::Boolean(_)),
                        crate::codegen_native::NType::I64 | crate::codegen_native::NType::U64
                        | crate::codegen_native::NType::F32 | crate::codegen_native::NType::F64 => true,
                        _ => !matches!(arg, Expr::Number(_))
                            && !matches!(arg, Expr::UnaryOp { op: crate::ast::UnaryOpKind::Neg, operand }
                                if matches!(operand.as_ref(), Expr::Number(_))),
                    });
                    // P2-T95: main ohne Handler ebenfalls (auch Direktform): moo_throw
                    // beendet das Programm nicht mehr (Hinweis, Fehler verworfen), der
                    // Aufruf liefert nichts und main laeuft weiter statt return 1.
                    let main_ohne_handler = in_main && !self.rahmen_hat_handler();
                    let skip_target = if local_catch.is_none()
                        && (!self.native_try_direct || main_ohne_handler)
                        && fangbar && needs_adapter
                        && (in_named_function || in_main)
                    {
                        let current = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
                        Some(self.context.append_basic_block(current, "native.skip"))
                    } else {
                        None
                    };
                    let adapter_catch = local_catch.or(skip_target);
                    let previous_skip = self.native_skip_target;
                    self.native_skip_target = skip_target;
                    let mut native_args = Vec::with_capacity(args.len());
                    let puffer_params = self.native_buffer_params.get(name).cloned();
                    for (index, (arg, ty)) in args.iter().zip(&signature.params).enumerate() {
                        // P2-T49 B3: Strukturwert - beliebiger Ausdruck, sofort in eine
                        // Aufruferkopie entboxt (keine Leihe), Zeiger an die Funktion.
                        if let Some(Some(crate::native_system::PufferArt::Wert(struktur))) = puffer_params.as_ref().and_then(|p| p.get(index)) {
                            if adapter_catch.is_none() && (!argument_frame_ok || self.rahmen_hat_handler()) {
                                return Err("Strukturargumente an native Funktionen sind nur hosted in main oder benannten Funktionen unterstützt (in versuche:-Teilausdruecken nur fuer fangbare Koerper)".into());
                            }
                            let kopie = self.compile_native_struct_argument(arg, struktur, adapter_catch)?;
                            native_args.push(kopie.into());
                            continue;
                        }
                        // P2-T50 B5: Pufferpositionen tragen in signature nur einen
                        // Platzhalter (dyn_signature) und laufen nie durch die
                        // Skalarpfade; sie werden nach allen Skalaren gepinnt.
                        if puffer_params.as_ref().is_some_and(|p| matches!(p.get(index), Some(Some(_)))) {
                            let griff = puffer_params.as_ref().is_some_and(|p| matches!(p.get(index),
                                Some(Some(crate::native_system::PufferArt::Griff { .. }))));
                            // P2-T23/T53 E4/E6: Griffargumente Variable oder nichts (Nullzeiger).
                            // P2-T85 C1: auch seiteneffektfreier Feldzugriff (selbst.griff,
                            // a.b.puffer). Der Adapter liest ihn wie eine Variable (+1),
                            // leiht ihn fuer den Aufruf und gibt ihn auf jedem Pfad frei.
                            if griff && !(Self::ist_feldkette(arg) || matches!(arg, Expr::None)) {
                                return Err(format!("Native Funktion '{name}': Griffargument {} muss vorerst eine Variable oder nichts sein (auch Feldzugriff wie selbst.griff)", index + 1));
                            }
                            if !griff && !Self::ist_feldkette(arg) {
                                return Err(format!("Native Funktion '{name}': Pufferargument {} muss vorerst eine Variable sein (auch Feldzugriff wie selbst.puffer)", index + 1));
                            }
                            if adapter_catch.is_none() && (!argument_frame_ok || self.rahmen_hat_handler()) {
                                return Err("Pufferargumente an native Funktionen sind nur hosted in main oder benannten Funktionen unterstützt".into());
                            }
                            native_args.push(self.context.i8_type().const_zero().into());
                            continue;
                        }
                        // P7-T17: Bare/UEFI - Literale bis 32 Bit und wahr/falsch nehmen
                        // weiter den Konstantenpfad unten, alles andere den Bare-Adapter.
                        if self.bare_mode {
                            let int_literal = matches!(ty,
                                crate::codegen_native::NType::I8 | crate::codegen_native::NType::U8
                                | crate::codegen_native::NType::I16 | crate::codegen_native::NType::U16
                                | crate::codegen_native::NType::I32 | crate::codegen_native::NType::U32)
                                && (matches!(arg, Expr::Number(_))
                                    || matches!(arg, Expr::UnaryOp { op: crate::ast::UnaryOpKind::Neg, operand }
                                        if matches!(operand.as_ref(), Expr::Number(_))));
                            let bool_literal = matches!(ty, crate::codegen_native::NType::Bool)
                                && matches!(arg, Expr::Boolean(_));
                            if !int_literal && !bool_literal {
                                native_args.push(self.compile_bare_native_argument(arg, *ty)?);
                                continue;
                            }
                        }
                        if matches!(ty, crate::codegen_native::NType::I64 | crate::codegen_native::NType::U64) {
                            let local_catch = adapter_catch;
                            if local_catch.is_none()
                                && (!argument_frame_ok || self.rahmen_hat_handler())
                            {
                                return Err("Dynamische 64-Bit-Argumente an native Funktionen sind nur hosted in main oder benannten Funktionen unterstützt (in versuche:-Teilausdruecken nur fuer fangbare Koerper; nicht in Lambdas/Methoden, C-Exporten, FFI-Koerpern oder im Bare-Profil)".into());
                            }
                            native_args.push(self.compile_native_wide_argument(arg, *ty, local_catch)?.into());
                            continue;
                        }
                        if matches!(ty, crate::codegen_native::NType::Bool) {
                            // P2-T34: wahr/falsch direkt, sonst Adapter mit MOO_BOOL-Tagpruefung.
                            if let Expr::Boolean(flag) = arg {
                                native_args.push(self.context.bool_type().const_int(u64::from(*flag), false).into());
                                continue;
                            }
                            let local_catch = adapter_catch;
                            if local_catch.is_none()
                                && (!argument_frame_ok || self.rahmen_hat_handler())
                            {
                                return Err("Dynamische bool-Argumente an native Funktionen sind nur hosted in main oder benannten Funktionen unterstützt (in versuche:-Teilausdruecken nur fuer fangbare Koerper; nicht in Lambdas/Methoden, C-Exporten, FFI-Koerpern oder im Bare-Profil)".into());
                            }
                            native_args.push(self.compile_native_bool_argument(arg, local_catch)?.into());
                            continue;
                        }
                        if matches!(ty, crate::codegen_native::NType::F32 | crate::codegen_native::NType::F64) {
                            let local_catch = adapter_catch;
                            if local_catch.is_none()
                                && (!argument_frame_ok || self.rahmen_hat_handler())
                            {
                                return Err("Dynamische Float-Argumente an native Funktionen sind nur hosted in main oder benannten Funktionen unterstützt (in versuche:-Teilausdruecken nur fuer fangbare Koerper; nicht in Lambdas/Methoden, C-Exporten, FFI-Koerpern oder im Bare-Profil)".into());
                            }
                            native_args.push(self.compile_native_float_argument(arg, *ty, local_catch)?.into());
                            continue;
                        }
                        let (minimum, maximum) = match ty {
                            crate::codegen_native::NType::I32 => (i32::MIN as f64, i32::MAX as f64),
                            crate::codegen_native::NType::U32 => (u32::MIN as f64, u32::MAX as f64),
                            crate::codegen_native::NType::I16 => (i16::MIN as f64, i16::MAX as f64),
                            crate::codegen_native::NType::U16 => (u16::MIN as f64, u16::MAX as f64),
                            crate::codegen_native::NType::I8 => (i8::MIN as f64, i8::MAX as f64),
                            crate::codegen_native::NType::U8 => (u8::MIN as f64, u8::MAX as f64),
                            _ => return Err("Nicht unterstuetzter nativer Parametertyp".into()),
                        };
                        let literal = match arg {
                            Expr::Number(value) => Some(*value),
                            Expr::UnaryOp { op: crate::ast::UnaryOpKind::Neg, operand } =>
                                match operand.as_ref() {
                                    Expr::Number(value) => Some(-*value),
                                    _ => None,
                                },
                            _ => None,
                        };
                        let value = if let Some(number) = literal {
                            if !number.is_finite() || number.fract() != 0.0
                                || number < minimum || number > maximum
                            {
                                return Err("Nativer i32-Parameter ist keine darstellbare Ganzzahl".into());
                            }
                            let int_ty = match ty {
                                crate::codegen_native::NType::I32 | crate::codegen_native::NType::U32 => self.context.i32_type(),
                                crate::codegen_native::NType::I16 | crate::codegen_native::NType::U16 => self.context.i16_type(),
                                _ => self.context.i8_type(),
                            };
                            int_ty.const_int(number as i64 as u64, false)
                        } else {
                            let local_catch = adapter_catch;
                            if local_catch.is_none()
                                && (!argument_frame_ok || self.rahmen_hat_handler())
                            {
                                return Err("Dynamische i32-Argumente an native Funktionen sind nur hosted in main oder benannten Funktionen unterstützt (in versuche:-Teilausdruecken nur fuer fangbare Koerper; nicht in Lambdas/Methoden, C-Exporten, FFI-Koerpern oder im Bare-Profil)".into());
                            }
                            self.compile_native_integer_argument(arg, *ty, local_catch)?

                        };
                        native_args.push(value.into());
                    }
                    // P2-T50 B5: Pufferargumente zuletzt (ein Skalarfehler laesst so
                    // keinen Pin offen, Plan bb3878ef R1).
                    let mut puffer_pins = Vec::new();
                    if let Some(puffer) = &puffer_params {
                        for (index, element) in puffer.iter().enumerate() {
                            let Some(element) = element else { continue };
                            if matches!(element, crate::native_system::PufferArt::Griff { .. }
                                | crate::native_system::PufferArt::Wert(_)) { continue; }
                            let schreibbar = !self.native_read_params.get(name).is_some_and(|s| s.contains(&index));
                            let (aggregat, pin, wert) = self.compile_native_buffer_argument(
                                &args[index], element.clone(), schreibbar, &puffer_pins, adapter_catch,
                            )?;
                            native_args[index] = aggregat.into();
                            puffer_pins.push((pin, wert));
                        }
                    }
                    // P2-T23/T53 B3: Griffe nach den Puffern (Fehler loest alle Pins).
                    let mut griffe = Vec::new();
                    if let Some(puffer) = &puffer_params {
                        for (index, element) in puffer.iter().enumerate() {
                            let Some(crate::native_system::PufferArt::Griff { ziel, verbraucht }) = element else { continue };
                            let (zeiger, slot, wert) = self.compile_native_handle_argument(
                                &args[index], ziel.as_deref(), &puffer_pins, &griffe, adapter_catch,
                            )?;
                            native_args[index] = zeiger.into();
                            griffe.push((slot, wert, *verbraucht));
                        }
                    }
                    // P2-T49 B3: sret-Puffer (ABI-ausgerichtet, genullt) als Parameter 0.
                    let sret = match &struktur_ergebnis {
                        Some(struktur) => {
                            let slot = self.native_struct_slot(struktur, "native.struktur.ergebnis")?;
                            native_args.insert(0, slot.into());
                            Some((slot, struktur.clone()))
                        }
                        None => None,
                    };
                    let call_name = if signature.result.is_some() { "native.call" } else { "" };
                    let result = self.builder.build_call(function, &native_args, call_name)
                        .map_err(|e| e.to_string())?.try_as_basic_value();
                    self.native_buffer_unpin(&puffer_pins)?;
                    self.native_handle_release(&griffe, true)?;
                    self.native_skip_target = previous_skip;
                    let griff_ergebnis = self.native_handle_results.get(name).cloned();
                    let produced = if let Some((slot, struktur)) = sret {
                        // Review #38118: erst nach dem Fehlercheck boxen (Plan B3).
                        self.native_struct_box_nach_pruefung(slot, &struktur)?
                    } else if let Some(ziel) = griff_ergebnis {
                        let zeiger = match result {
                            inkwell::values::ValueKind::Basic(BasicValueEnum::PointerValue(p)) => p,
                            _ => return Err("Native Funktion lieferte keinen Zeiger fuer den Griff".into()),
                        };
                        self.native_handle_box(zeiger, ziel.as_deref())?
                    } else if signature.result.is_none() {
                        self.call_rt(self.rt.moo_none, &[], "native.void")?
                    } else {
                        let value = match result {
                            inkwell::values::ValueKind::Basic(value) => value,
                            _ => return Err("Native Funktion lieferte keinen Wert".into()),
                        };
                        // P2-T34: bool-Ergebnis wird ein Moo-Wahrheitswert, kein Zahlenumweg.
                        if let (Some(crate::codegen_native::NType::Bool), BasicValueEnum::IntValue(flag)) = (signature.result, value) {
                            self.call_rt(self.rt.moo_bool_fn, &[flag.into()], "native.bool")?
                        } else if let (true, Some(wide @ (crate::codegen_native::NType::I64 | crate::codegen_native::NType::U64)), BasicValueEnum::IntValue(bits))
                            = (self.bare_mode, signature.result, value)
                        {
                            // P7-T17: exakt, ohne double und ohne Laufzeitfunktion.
                            self.bare_box_wide(bits, wide == crate::codegen_native::NType::U64)?
                        } else {
                            let boxed = match (signature.result, value) {
                                (Some(crate::codegen_native::NType::I64), BasicValueEnum::IntValue(value)) =>
                                    self.box_native_wide(value, false)?,
                                (Some(crate::codegen_native::NType::U64), BasicValueEnum::IntValue(value)) =>
                                    self.box_native_wide(value, true)?,
                                (Some(crate::codegen_native::NType::F32), BasicValueEnum::FloatValue(value)) =>
                                    self.builder.build_float_ext(value, self.context.f64_type(), "native.box").map_err(|e| e.to_string())?,
                                (Some(crate::codegen_native::NType::F64), BasicValueEnum::FloatValue(value)) => value,
                                (Some(crate::codegen_native::NType::U8 | crate::codegen_native::NType::U16 | crate::codegen_native::NType::U32), BasicValueEnum::IntValue(value)) =>
                                    self.builder.build_unsigned_int_to_float(value, self.context.f64_type(), "native.box").map_err(|e| e.to_string())?,
                                (Some(crate::codegen_native::NType::I8 | crate::codegen_native::NType::I16 | crate::codegen_native::NType::I32), BasicValueEnum::IntValue(value)) =>
                                    self.builder.build_signed_int_to_float(value, self.context.f64_type(), "native.box").map_err(|e| e.to_string())?,
                                _ => return Err("Native Funktion lieferte einen unerwarteten Werttyp".into()),
                            };
                            self.call_rt(self.rt.moo_number, &[boxed.into()], "native.number")?
                        }
                    };
                    let Some(skip) = skip_target else { return Ok(produced); };
                    // P2-T78: Zusammenfuehrung mit dem Skip-Block ueber einen Slot im
                    // Eintrittsblock (wie defer_status_error); der uebersprungene Aufruf
                    // liefert nichts, das Fehlerflag ist bereits gesetzt.
                    let current = self.current_function.ok_or("Nativer Aufruf ohne Funktion")?;
                    let slot = self.entry_alloca(self.mv_type(), "native.skip.slot")?;
                    let merge = self.context.append_basic_block(current, "native.skip.merge");
                    self.builder.build_store(slot, produced).map_err(|e| e.to_string())?;
                    self.builder.build_unconditional_branch(merge).map_err(|e| e.to_string())?;
                    self.builder.position_at_end(skip);
                    let none = self.call_rt(self.rt.moo_none, &[], "native.skip.none")?;
                    self.builder.build_store(slot, none).map_err(|e| e.to_string())?;
                    self.builder.build_unconditional_branch(merge).map_err(|e| e.to_string())?;
                    self.builder.position_at_end(merge);
                    return Ok(self.builder.build_load(self.mv_type(), slot, "native.skip.result")
                        .map_err(|e| e.to_string())?.into_struct_value());
                }
                // P2-T49 B4: `Name(...)` einer registrierten Struktur in dynamischem
                // Code = Strukturwert (Box). Eigene Funktionen, Variablen, Globale und
                // Lambdas gleichen Namens behalten Vorrang (wie bei Builtins).
                if self.native_structs.layout(name).is_some()
                    && self.module.get_function(name).is_none()
                    && !self.variables.contains_key(name)
                    && !self.globals.contains_key(name)
                    && !self.lambda_names.contains_key(name)
                {
                    return self.compile_struktur_konstruktor(name, args, guard_args);
                }
                if self.extern_c_names.contains(name) {
                    return Err(format!("extern-C-Aufruf '{name}' ist noch nicht unterstützt"));
                }
                // P011-A2: Fixed-width-Casts (als_u8..als_u64) — reservierte
                // Namen, dispatchen VOR dem User-Funktions-Vorrang (wie kern_*).
                // Hosted UND bare verfuegbar (pure Arithmetik).
                if let Some(bits) = Self::fixed_cast_bits(name) {
                    return self.compile_fixed_width_cast(bits, name, args);
                }
                // User-Funktionen haben Vorrang vor Builtins. Wenn eine Funktion
                // mit diesem Namen vom User definiert wurde (forward-declared in
                // self.module), call sie direkt. Sonst Builtin-Match. Verhindert
                // dass z.B. eine User-Funktion 'eval' / 'apply' / 'finde' /
                // 'ersetze' silent von einem gleichnamigen Builtin geshadowed
                // wird (gemeldet von K4 Mini-Interpreter und Mini-Lisp).
                if self.module.get_function(name).is_some()
                    && !self.lambda_names.contains_key(name)
                    // kern_*-Builtins sind als Declarations immer im Modul —
                    // sie MUESSEN durch den Builtin-Match (require_bare-Guard),
                    // sonst hosted/bare-Leck via user_call (Plan-010 C1).
                    && !Self::is_kern_builtin(name)
                {
                    // Call user function directly via the existing path below
                    // — fall through to the post-builtin call site by skipping
                    // the builtin match. Wir markieren das per early-jump.
                    let func = self.module.get_function(name).unwrap();
                    let mut call_args: Vec<BasicMetadataValueEnum> = Vec::new();
                    let mut owned_args = Vec::new();
                    for a in args {
                        let base = self.stmt_owned_context.len();
                        if self.native_value_temps() { self.stmt_owned_context.extend_from_slice(&owned_args); }
                        let value_result = self.compile_expr(a);
                        self.stmt_owned_context.truncate(base);
                        let value = value_result?;
                        owned_args.push(value);
                        if guard_args { self.guard_local_expr_error(&owned_args)?; }
                        call_args.push(value.into());
                    }
                    let expected = func.count_params() as usize;
                    while call_args.len() < expected {
                        let none_val = self.call_rt(self.rt.moo_none, &[], "none_default")?;
                        call_args.push(none_val.into());
                    }
                    let ergebnis = self.call_rt(func, &call_args, "user_call")?;
                    self.fehler_hinweis_nach_aufruf(name, aufruf_zeile)?;
                    return Ok(ergebnis);
                }

                // P2-T100: Aufrufkontext fuer das Panic-Netz (M-C999) und
                // Argumentzahl aus der Build-Tabelle (nur Builtins mit
                // nachweisbar exakter Anzahl; Nutzerfunktionen sind oben
                // schon abgezweigt).
                let _aufruf_wache = crate::interner_fehler::aufruf_betreten(
                    name, args.len(), if aufruf_zeile > 0 { aufruf_zeile } else { self.diag_zeile });
                if let Some((erwartet, exakt)) = crate::builtin_aritaet::erwartet(name) {
                    self.builtin_aritaet_pruefen(name, erwartet, exakt, args.len())?;
                }

                // Builtin-Funktionen pruefen
                match name.as_str() {
                    // P2-T114 Phase B: Atom (atomare Zahl) und Sperre. Gehostet;
                    // in @nativ gibt es dieselben atom_*-Namen auf ptr[T].
                    "atom" | "atomic" | "atom_laden" | "atomic_load" | "atom_speichern" | "atomic_store"
                    | "atom_addieren" | "atomic_add" | "atom_tauschen" | "atomic_swap"
                    | "atom_vergleichen_und_tauschen" | "atomic_compare_exchange"
                    | "sperre" | "lock" | "__moo_sperre_betreten" | "__moo_sperre_verlassen"
                        if !self.variables.contains_key(name)
                            && !self.globals.contains_key(name)
                            && !self.lambda_names.contains_key(name) =>
                    {
                        return self.sync_builtin(name, args);
                    }
                    "übergib" | "uebergib" | "transfer"
                        if !self.variables.contains_key(name)
                            && !self.globals.contains_key(name)
                            && !self.lambda_names.contains_key(name) =>
                    {
                        return Err("übergib ist nur als Argument von senden erlaubt: kanal.senden(übergib puffer)".into());
                    }
                    // P2-T50: typisierter Puffer. Eigene Funktionen gleichen Namens
                    // haben oben schon Vorrang; Variable/Global/Lambda verschatten
                    // das Builtin ebenfalls (indirekter Aufruf weiter unten).
                    "puffer_erstellen" | "buffer_create"
                        if !self.variables.contains_key(name)
                            && !self.globals.contains_key(name)
                            && !self.lambda_names.contains_key(name) =>
                    {
                        return self.compile_buffer_create(name, args);
                    }
                    "puffer_anhängen" | "puffer_anhaengen" | "buffer_append"
                        if !self.variables.contains_key(name)
                            && !self.globals.contains_key(name)
                            && !self.lambda_names.contains_key(name) =>
                    {
                        return self.compile_buffer_append(name, args);
                    }
                    "puffer_ansicht" | "buffer_view"
                        if !self.variables.contains_key(name)
                            && !self.globals.contains_key(name)
                            && !self.lambda_names.contains_key(name) =>
                    {
                        return self.compile_buffer_view(name, args);
                    }
                    "griff_gültig" | "griff_gueltig" | "handle_valid" | "griffe_offen" | "handles_open"
                    | "puffer_aus_text" | "buffer_from_text" | "puffer_als_text" | "buffer_to_text"
                        if !self.variables.contains_key(name)
                            && !self.globals.contains_key(name)
                            && !self.lambda_names.contains_key(name) =>
                    {
                        return self.compile_griff_builtin(name, args);
                    }
                    // Result-Typ (Rust-inspiriert): ok(wert) / fehler(msg)
                    "ok" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_result_ok, &[arg.into()], "ok");
                    }
                    "fehler" | "err" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_result_err, &[arg.into()], "err");
                    }
                    "ist_ok" | "is_ok" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_result_is_ok, &[arg.into()], "is_ok");
                    }
                    "ist_fehler" | "is_err" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_result_is_err, &[arg.into()], "is_err");
                    }
                    "entpacke" | "unwrap" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_result_unwrap, &[arg.into()], "unwrap");
                    }
                    "abs" | "wurzel" | "sqrt" => {
                        let func = match name.as_str() {
                            "abs" => self.rt.moo_abs,
                            "wurzel" | "sqrt" => self.rt.moo_sqrt,
                            _ => unreachable!(),
                        };
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(func, &[arg.into()], "builtin");
                    }
                    "sinus" | "sin" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sin, &[arg.into()], "sin");
                    }
                    "cosinus" | "cos" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_cos, &[arg.into()], "cos");
                    }
                    "tangens" | "tan" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tan, &[arg.into()], "tan");
                    }
                    "atan2" | "arkustangens2" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_atan2, &[a.into(), b.into()], "atan2");
                    }
                    "asin" | "arkussinus" if args.len() == 1 => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_asin, &[arg.into()], "asin");
                    }
                    "acos" | "arkuskosinus" | "arkuscosinus" if args.len() == 1 => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_acos, &[arg.into()], "acos");
                    }
                    // ERDE-11: Rauschen (Seed optional, Default 0).
                    "rauschen_2d" | "noise_2d" if args.len() == 2 || args.len() == 3 => {
                        let x = self.compile_expr(&args[0])?;
                        let y = self.compile_expr(&args[1])?;
                        let seed = if args.len() == 3 { self.compile_expr(&args[2])? } else { self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(0.0).into()], "seed0")? };
                        return self.call_rt(self.rt.moo_rauschen_2d, &[x.into(), y.into(), seed.into()], "rauschen_2d");
                    }
                    "rauschen_3d" | "noise_3d" if args.len() == 3 || args.len() == 4 => {
                        let x = self.compile_expr(&args[0])?;
                        let y = self.compile_expr(&args[1])?;
                        let z = self.compile_expr(&args[2])?;
                        let seed = if args.len() == 4 { self.compile_expr(&args[3])? } else { self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(0.0).into()], "seed0")? };
                        return self.call_rt(self.rt.moo_rauschen_3d, &[x.into(), y.into(), z.into(), seed.into()], "rauschen_3d");
                    }
                    "rauschen_fbm" | "noise_fbm" if args.len() == 3 || args.len() == 4 => {
                        let x = self.compile_expr(&args[0])?;
                        let y = self.compile_expr(&args[1])?;
                        let okt = self.compile_expr(&args[2])?;
                        let seed = if args.len() == 4 { self.compile_expr(&args[3])? } else { self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(0.0).into()], "seed0")? };
                        return self.call_rt(self.rt.moo_rauschen_fbm, &[x.into(), y.into(), okt.into(), seed.into()], "rauschen_fbm");
                    }
                    "runde" | "round" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_round, &[arg.into()], "round");
                    }
                    "boden" | "floor" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_floor, &[arg.into()], "floor");
                    }
                    "decke" | "ceil" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ceil, &[arg.into()], "ceil");
                    }
                    "min" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_min, &[a.into(), b.into()], "min");
                    }
                    "max" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_max, &[a.into(), b.into()], "max");
                    }
                    "zufall" | "random" => {
                        return self.call_rt(self.rt.moo_random, &[], "random");
                    }
                    "typ_von" | "type_of" => {
                        // moo_type_of borrows its input and returns a fresh Text.
                        // compile_expr produced an owning temporary, so release it
                        // after the call without touching the returned value.
                        let arg = self.compile_expr(&args[0])?;
                        let result = self.call_rt(
                            self.rt.moo_type_of,
                            &[arg.into()],
                            "typeof",
                        )?;
                        self.call_rt_void(self.rt.moo_release, &[arg.into()], "rel_typeof_arg")?;
                        return Ok(result);
                    }
                    "tensor" => {
                        // Plan-014 A3. Tensor-Konvention: Runtime borrowed —
                        // Codegen released ALLE Heap-Args post-call (Muster 072834f).
                        let form = self.compile_expr(&args[0])?;
                        let res = if args.len() > 1 {
                            let fill = self.compile_expr(&args[1])?;
                            let r = self.call_rt(self.rt.moo_tensor_neu,
                                &[form.into(), fill.into()], "tensor_neu")?;
                            self.call_rt_void(self.rt.moo_release, &[fill.into()], "rel_t_fill")?;
                            r
                        } else {
                            self.call_rt(self.rt.moo_tensor_nullen, &[form.into()], "tensor_neu0")?
                        };
                        self.call_rt_void(self.rt.moo_release, &[form.into()], "rel_t_form")?;
                        return Ok(res);
                    }
                    "tensor_nullen" | "tensor_zeros" => {
                        let form = self.compile_expr(&args[0])?;
                        let res = self.call_rt(self.rt.moo_tensor_nullen, &[form.into()], "t_nullen")?;
                        self.call_rt_void(self.rt.moo_release, &[form.into()], "rel_tn_form")?;
                        return Ok(res);
                    }
                    "tensor_einsen" | "tensor_ones" => {
                        let form = self.compile_expr(&args[0])?;
                        let res = self.call_rt(self.rt.moo_tensor_einsen, &[form.into()], "t_einsen")?;
                        self.call_rt_void(self.rt.moo_release, &[form.into()], "rel_te_form")?;
                        return Ok(res);
                    }
                    "tensor_zufall" | "tensor_random" => {
                        let form = self.compile_expr(&args[0])?;
                        let seed = if args.len() > 1 {
                            self.compile_expr(&args[1])?
                        } else {
                            self.call_rt(self.rt.moo_none, &[], "t_seed_none")?
                        };
                        let res = self.call_rt(self.rt.moo_tensor_zufall,
                            &[form.into(), seed.into()], "t_zufall")?;
                        self.call_rt_void(self.rt.moo_release, &[form.into()], "rel_tz_form")?;
                        return Ok(res);
                    }
                    "tensor_aus_liste" | "tensor_from_list" => {
                        let l = self.compile_expr(&args[0])?;
                        let res = self.call_rt(self.rt.moo_tensor_aus_liste, &[l.into()], "t_ausl")?;
                        self.call_rt_void(self.rt.moo_release, &[l.into()], "rel_tal_liste")?;
                        return Ok(res);
                    }
                    "autograd_reset" => {
                        return self.call_rt(self.rt.moo_ag_reset, &[], "ag_reset");
                    }
                    "autograd_an" | "autograd_on" => {
                        return self.call_rt(self.rt.moo_ag_an, &[], "ag_an");
                    }
                    "autograd_aus" | "autograd_off" => {
                        return self.call_rt(self.rt.moo_ag_aus, &[], "ag_aus");
                    }
                    // === NN-Builtins (Plan-014 C1). Tensor-Konvention: Runtime
                    // borrowed — Codegen released Heap-Args post-call. Zahlen-
                    // Args ohne Release (kein Heap). ===
                    "schicht_dicht" | "layer_dense" => {
                        let ein = self.compile_expr(&args[0])?;
                        let aus = self.compile_expr(&args[1])?;
                        let akt = if args.len() > 2 { self.compile_expr(&args[2])? }
                                  else { self.call_rt(self.rt.moo_none, &[], "nn_akt_none")? };
                        let seed = if args.len() > 3 { self.compile_expr(&args[3])? }
                                   else { self.call_rt(self.rt.moo_none, &[], "nn_seed_none")? };
                        let r = self.call_rt(self.rt.moo_nn_schicht_dicht,
                            &[ein.into(), aus.into(), akt.into(), seed.into()], "nn_dicht")?;
                        self.call_rt_void(self.rt.moo_release, &[akt.into()], "rel_nnd_akt")?;
                        return Ok(r);
                    }
                    "schicht_faltung" | "layer_conv2d" => {
                        let cin=self.compile_expr(&args[0])?;
                        let cout=self.compile_expr(&args[1])?;
                        let kernel=self.compile_expr(&args[2])?;
                        let schritt=if args.len()>3 {self.compile_expr(&args[3])?} else {self.call_rt(self.rt.moo_none,&[],"conv_s_none")?};
                        let polster=if args.len()>4 {self.compile_expr(&args[4])?} else {self.call_rt(self.rt.moo_none,&[],"conv_p_none")?};
                        let akt=if args.len()>5 {self.compile_expr(&args[5])?} else {self.call_rt(self.rt.moo_none,&[],"conv_a_none")?};
                        let seed=if args.len()>6 {self.compile_expr(&args[6])?} else {self.call_rt(self.rt.moo_none,&[],"conv_seed_none")?};
                        let r=self.call_rt(self.rt.moo_nn_schicht_faltung,&[cin.into(),cout.into(),kernel.into(),schritt.into(),polster.into(),akt.into(),seed.into()],"nn_faltung")?;
                        self.call_rt_void(self.rt.moo_release,&[akt.into()],"rel_conv_akt")?;
                        return Ok(r);
                    }
                    "schicht_pooling" | "layer_pooling" => {
                        let art=self.compile_expr(&args[0])?;
                        let groesse=self.compile_expr(&args[1])?;
                        let schritt=if args.len()>2 {self.compile_expr(&args[2])?} else {self.call_rt(self.rt.moo_none,&[],"pool_s_none")?};
                        let r=self.call_rt(self.rt.moo_nn_schicht_pooling,&[art.into(),groesse.into(),schritt.into()],"nn_pooling")?;
                        self.call_rt_void(self.rt.moo_release,&[art.into()],"rel_pool_art")?;
                        return Ok(r);
                    }
                    "schicht_flach" | "layer_flatten" => {
                        return self.call_rt(self.rt.moo_nn_schicht_flach,&[],"nn_flach");
                    }
                    "schicht_dropout" | "layer_dropout" => {
                        let rate = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_nn_schicht_dropout,
                            &[rate.into()], "nn_dropout");
                    }
                    "schicht_layernorm" | "layer_layernorm" => {
                        let dim = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_nn_schicht_layernorm,
                            &[dim.into()], "nn_layernorm");
                    }
                    "schicht_rmsnorm" | "layer_rmsnorm" => {
                        let dim = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_nn_schicht_rmsnorm,
                            &[dim.into()], "nn_rmsnorm");
                    }
                    // KI-R1: AttnRes-Pseudo-Query-Schicht (PAPER-VERIFY-05)
                    "schicht_attnres" | "layer_attnres" => {
                        let dim = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_nn_schicht_attnres,
                            &[dim.into()], "nn_attnres_schicht");
                    }
                    "schicht_ffn_gated" | "layer_ffn_gated" => {
                        let dim = self.compile_expr(&args[0])?;
                        let versteckt = self.compile_expr(&args[1])?;
                        let art = if args.len() > 2 { self.compile_expr(&args[2])? }
                                  else { self.call_rt(self.rt.moo_none, &[], "nn_ffn_art_none")? };
                        let r = self.call_rt(self.rt.moo_nn_schicht_ffn_gated,
                            &[dim.into(), versteckt.into(), art.into()], "nn_ffn_gated")?;
                        self.call_rt_void(self.rt.moo_release, &[art.into()], "rel_ffn_art")?;
                        return Ok(r);
                    }
                    "schicht_embedding" | "layer_embedding" => {
                        let vokab = self.compile_expr(&args[0])?;
                        let dim = self.compile_expr(&args[1])?;
                        let seed = if args.len() > 2 { self.compile_expr(&args[2])? }
                                   else { self.call_rt(self.rt.moo_none, &[], "nn_emb_seed_none")? };
                        return self.call_rt(self.rt.moo_nn_schicht_embedding,
                            &[vokab.into(), dim.into(), seed.into()], "nn_embedding");
                    }
                    "vorwaerts" | "vorwärts" | "forward" => {
                        let netz = self.compile_expr(&args[0])?;
                        let x = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_vorwaerts,
                            &[netz.into(), x.into()], "nn_vorwaerts")?;
                        self.call_rt_void(self.rt.moo_release, &[netz.into()], "rel_nnf_netz")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_nnf_x")?;
                        return Ok(r);
                    }
                    "parameter" | "parameters" => {
                        let netz = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_parameter,
                            &[netz.into()], "nn_parameter")?;
                        self.call_rt_void(self.rt.moo_release, &[netz.into()], "rel_nnp_netz")?;
                        return Ok(r);
                    }
                    "mse" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_mse,
                            &[a.into(), b.into()], "nn_mse")?;
                        self.call_rt_void(self.rt.moo_release, &[a.into()], "rel_mse_a")?;
                        self.call_rt_void(self.rt.moo_release, &[b.into()], "rel_mse_b")?;
                        return Ok(r);
                    }
                    "kosinus" | "cosine" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_kosinus,
                            &[a.into(), b.into()], "nn_kosinus")?;
                        self.call_rt_void(self.rt.moo_release, &[a.into()], "rel_kos_a")?;
                        self.call_rt_void(self.rt.moo_release, &[b.into()], "rel_kos_b")?;
                        return Ok(r);
                    }
                    "kontrastiv" | "contrastive" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let temperatur = if args.len() > 2 { self.compile_expr(&args[2])? }
                            else { self.call_rt(self.rt.moo_none, &[], "nn_kon_temp_none")? };
                        let r = self.call_rt(self.rt.moo_nn_kontrastiv,
                            &[a.into(), b.into(), temperatur.into()], "nn_kontrastiv")?;
                        self.call_rt_void(self.rt.moo_release, &[a.into()], "rel_kon_a")?;
                        self.call_rt_void(self.rt.moo_release, &[b.into()], "rel_kon_b")?;
                        self.call_rt_void(self.rt.moo_release, &[temperatur.into()], "rel_kon_temp")?;
                        return Ok(r);
                    }
                    // KI-R1: Tiefen-Attention-Residual (PAPER-VERIFY-05)
                    "attnres" => {
                        let s = self.compile_expr(&args[0])?;
                        let quellen = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_attnres,
                            &[s.into(), quellen.into()], "nn_attnres")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_ares_s")?;
                        self.call_rt_void(self.rt.moo_release, &[quellen.into()], "rel_ares_q")?;
                        return Ok(r);
                    }
                    // KI-Q1: QJL-Trio (inferenz-only, Vertrag in moo_quant.c)
                    "sign_jl" => {
                        let k = self.compile_expr(&args[0])?;
                        let m = self.compile_expr(&args[1])?;
                        let s = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_quant_sign_jl,
                            &[k.into(), m.into(), s.into()], "qjl_sign")?;
                        self.call_rt_void(self.rt.moo_release, &[k.into()], "rel_qjls_k")?;
                        self.call_rt_void(self.rt.moo_release, &[m.into()], "rel_qjls_m")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_qjls_s")?;
                        return Ok(r);
                    }
                    "jl_projektion" | "jl_projection" => {
                        let q = self.compile_expr(&args[0])?;
                        let m = self.compile_expr(&args[1])?;
                        let s = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_quant_jl_projektion,
                            &[q.into(), m.into(), s.into()], "qjl_proj")?;
                        self.call_rt_void(self.rt.moo_release, &[q.into()], "rel_qjlp_q")?;
                        self.call_rt_void(self.rt.moo_release, &[m.into()], "rel_qjlp_m")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_qjlp_s")?;
                        return Ok(r);
                    }
                    "sign_jl_skalarprodukt" | "sign_jl_dot" => {
                        let qp = self.compile_expr(&args[0])?;
                        let paket = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_quant_sign_jl_skalarprodukt,
                            &[qp.into(), paket.into()], "qjl_dot")?;
                        self.call_rt_void(self.rt.moo_release, &[qp.into()], "rel_qjld_qp")?;
                        self.call_rt_void(self.rt.moo_release, &[paket.into()], "rel_qjld_paket")?;
                        return Ok(r);
                    }
                    "kreuzentropie" | "cross_entropy" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let r = if args.len() > 2 {
                            let m = self.compile_expr(&args[2])?;
                            let r = self.call_rt(self.rt.moo_nn_kreuzentropie_maskiert,
                                &[a.into(), b.into(), m.into()], "nn_ce_m")?;
                            self.call_rt_void(self.rt.moo_release, &[m.into()], "rel_ce_m")?;
                            r
                        } else {
                            self.call_rt(self.rt.moo_nn_kreuzentropie,
                                &[a.into(), b.into()], "nn_ce")?
                        };
                        self.call_rt_void(self.rt.moo_release, &[a.into()], "rel_ce_a")?;
                        self.call_rt_void(self.rt.moo_release, &[b.into()], "rel_ce_b")?;
                        return Ok(r);
                    }
                    "checkpoint_waehlen" => {
                        if args.len() != 1 {
                            return Err("checkpoint_waehlen erwartet eine Val-Berichtsliste".to_string());
                        }
                        let berichte = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_train_eval_waehlen, &[berichte.into()], "eval_waehlen")?;
                        self.call_rt_void(self.rt.moo_release, &[berichte.into()], "rel_eval_berichte")?;
                        return Ok(r);
                    }
                    "evaluiere_modell" => {
                        if args.len() != 3 {
                            return Err("evaluiere_modell erwartet 3 Argumente (factory, daten, konfig)".to_string());
                        }
                        let factory = self.compile_expr(&args[0])?;
                        let daten = self.compile_expr(&args[1])?;
                        let konfig = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_train_eval_modell, &[factory.into(), daten.into(), konfig.into()], "eval_p5")?;
                        self.call_rt_void(self.rt.moo_release, &[factory.into()], "rel_eval_factory")?;
                        self.call_rt_void(self.rt.moo_release, &[daten.into()], "rel_eval_daten")?;
                        self.call_rt_void(self.rt.moo_release, &[konfig.into()], "rel_eval_konfig")?;
                        return Ok(r);
                    }
                    "trainiere_sft" | "trainiere_korrekturen" | "trainiere_distillation" | "trainiere_praeferenzen" | "trainiere_methode" => {
                        if args.len() != 3 {
                            return Err(format!("{} erwartet 3 Argumente (forward, daten, konfig)", name));
                        }
                        let forward = self.compile_expr(&args[0])?;
                        let daten = self.compile_expr(&args[1])?;
                        let konfig = self.compile_expr(&args[2])?;
                        let fun = match name.as_str() {
                            "trainiere_sft" => self.rt.moo_train_sft,
                            "trainiere_korrekturen" => self.rt.moo_train_korrekturen,
                            "trainiere_distillation" => self.rt.moo_train_distillation,
                            "trainiere_praeferenzen" => self.rt.moo_train_praeferenzen,
                            _ => self.rt.moo_train_methode,
                        };
                        let r = self.call_rt(fun, &[forward.into(), daten.into(), konfig.into()], "train_p5")?;
                        self.call_rt_void(self.rt.moo_release, &[forward.into()], "rel_p5_forward")?;
                        self.call_rt_void(self.rt.moo_release, &[daten.into()], "rel_p5_daten")?;
                        self.call_rt_void(self.rt.moo_release, &[konfig.into()], "rel_p5_konfig")?;
                        return Ok(r);
                    }
                    "optimierer_sgd" | "optimizer_sgd" => {
                        let params = self.compile_expr(&args[0])?;
                        let rate = self.compile_expr(&args[1])?;
                        let mom = if args.len() > 2 { self.compile_expr(&args[2])? }
                                  else { self.call_rt(self.rt.moo_none, &[], "nn_mom_none")? };
                        let r = self.call_rt(self.rt.moo_nn_opt_sgd,
                            &[params.into(), rate.into(), mom.into()], "nn_sgd")?;
                        self.call_rt_void(self.rt.moo_release, &[params.into()], "rel_sgd_p")?;
                        return Ok(r);
                    }
                    "optimierer_adam" | "optimizer_adam" => {
                        let params = self.compile_expr(&args[0])?;
                        let rate = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_opt_adam,
                            &[params.into(), rate.into()], "nn_adam")?;
                        self.call_rt_void(self.rt.moo_release, &[params.into()], "rel_adam_p")?;
                        return Ok(r);
                    }
                    "optimierer_adamw" | "optimizer_adamw" => {
                        let params = self.compile_expr(&args[0])?;
                        let rate = self.compile_expr(&args[1])?;
                        let dec = if args.len() > 2 { self.compile_expr(&args[2])? }
                                  else { self.call_rt(self.rt.moo_none, &[], "nn_wd_none")? };
                        let r = self.call_rt(self.rt.moo_nn_opt_adamw,
                            &[params.into(), rate.into(), dec.into()], "nn_adamw")?;
                        self.call_rt_void(self.rt.moo_release, &[params.into()], "rel_adamw_p")?;
                        return Ok(r);
                    }
                    // Kinderleicht-API (Plan-014 D1)
                    "schicht_attention" | "layer_attention" => {
                        let dim = self.compile_expr(&args[0])?;
                        let koepfe = self.compile_expr(&args[1])?;
                        let seed = if args.len() > 2 { self.compile_expr(&args[2])? }
                                   else { self.call_rt(self.rt.moo_none, &[], "nn_att_seed")? };
                        let kv = if args.len() > 3 { self.compile_expr(&args[3])? }
                                 else { self.call_rt(self.rt.moo_none, &[], "nn_att_kv")? };
                        let maske = if args.len() > 4 { self.compile_expr(&args[4])? }
                                    else { self.call_rt(self.rt.moo_none, &[], "nn_att_maske")? };
                        let fenster = if args.len() > 5 { self.compile_expr(&args[5])? }
                                      else { self.call_rt(self.rt.moo_none, &[], "nn_att_fenster")? };
                        let rope = if args.len() > 6 { self.compile_expr(&args[6])? }
                                   else { self.call_rt(self.rt.moo_none, &[], "nn_att_rope")? };
                        let r = self.call_rt(self.rt.moo_nn_schicht_attention,
                            &[dim.into(), koepfe.into(), seed.into(), kv.into(),
                              maske.into(), fenster.into(), rope.into()], "nn_attention")?;
                        self.call_rt_void(self.rt.moo_release, &[maske.into()], "rel_att_maske")?;
                        self.call_rt_void(self.rt.moo_release, &[rope.into()], "rel_att_rope")?;
                        return Ok(r);
                    }
                    "schicht_position" | "layer_position" => {
                        let max = self.compile_expr(&args[0])?;
                        let dim = self.compile_expr(&args[1])?;
                        let art = if args.len() > 2 { self.compile_expr(&args[2])? }
                                  else { self.call_rt(self.rt.moo_none, &[], "nn_pos_art")? };
                        let seed = if args.len() > 3 { self.compile_expr(&args[3])? }
                                   else { self.call_rt(self.rt.moo_none, &[], "nn_pos_seed")? };
                        let r = self.call_rt(self.rt.moo_nn_schicht_position,
                            &[max.into(), dim.into(), art.into(), seed.into()], "nn_position")?;
                        self.call_rt_void(self.rt.moo_release, &[art.into()], "rel_nnp_art")?;
                        return Ok(r);
                    }
                    "schicht_moe" | "layer_moe" => {
                        let dim = self.compile_expr(&args[0])?;
                        let versteckt = self.compile_expr(&args[1])?;
                        let n = self.compile_expr(&args[2])?;
                        let k = self.compile_expr(&args[3])?;
                        let seed = if args.len() > 4 { self.compile_expr(&args[4])? }
                                   else { self.call_rt(self.rt.moo_none, &[], "nn_moe_seed")? };
                        return self.call_rt(self.rt.moo_nn_schicht_moe,
                            &[dim.into(), versteckt.into(), n.into(), k.into(),
                              seed.into()], "nn_moe");
                    }
                    "moe_balance" | "moe_balance_loss" => {
                        let netz = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_moe_balance,
                            &[netz.into()], "nn_moe_bal")?;
                        self.call_rt_void(self.rt.moo_release, &[netz.into()], "rel_moeb_netz")?;
                        return Ok(r);
                    }
                    "cache_leeren" | "clear_cache" => {
                        let netz = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_cache_leeren,
                            &[netz.into()], "nn_cache_leeren")?;
                        self.call_rt_void(self.rt.moo_release, &[netz.into()], "rel_cl_netz")?;
                        return Ok(r);
                    }
                    "sequence_packen" | "pack_sequence" => {
                        let docs = self.compile_expr(&args[0])?;
                        let bl = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_sequence_packen,
                            &[docs.into(), bl.into()], "nn_pack")?;
                        self.call_rt_void(self.rt.moo_release, &[docs.into()], "rel_pack_docs")?;
                        return Ok(r);
                    }
                    "packung_setzen" | "set_packing" => {
                        let netz = self.compile_expr(&args[0])?;
                        let m = self.compile_expr(&args[1])?;
                        let p = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_nn_packung_setzen,
                            &[netz.into(), m.into(), p.into()], "nn_packset")?;
                        self.call_rt_void(self.rt.moo_release, &[netz.into()], "rel_ps_netz")?;
                        self.call_rt_void(self.rt.moo_release, &[m.into()], "rel_ps_m")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_ps_p")?;
                        return Ok(r);
                    }
                    "packung_leeren" | "clear_packing" => {
                        let netz = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_packung_leeren,
                            &[netz.into()], "nn_packclr")?;
                        self.call_rt_void(self.rt.moo_release, &[netz.into()], "rel_pl_netz")?;
                        return Ok(r);
                    }
                    "gradienten_kappen" | "clip_gradients" => {
                        let params = self.compile_expr(&args[0])?;
                        let m = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_grad_clip,
                            &[params.into(), m.into()], "nn_gradclip")?;
                        self.call_rt_void(self.rt.moo_release, &[params.into()], "rel_gc_p")?;
                        return Ok(r);
                    }
                    "ki_netz" | "ai_net" => {
                        let schichten = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_ki_netz,
                            &[schichten.into()], "nn_kinetz")?;
                        self.call_rt_void(self.rt.moo_release, &[schichten.into()], "rel_kinetz_s")?;
                        return Ok(r);
                    }
                    "ki_laden" | "ai_load" | "netz_laden" => {
                        let pfad = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_laden,
                            &[pfad.into()], "nn_laden")?;
                        self.call_rt_void(self.rt.moo_release, &[pfad.into()], "rel_kiladen_p")?;
                        return Ok(r);
                    }
                    // Daten-Pipeline (Plan-014 E1)
                    "safetensors_laden" | "safetensors_load" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_safetensors, &[p.into()], "nn_sft")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_sft_p")?;
                        return Ok(r);
                    }
                    "mnist_laden" | "mnist_load" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_ds_mnist, &[p.into()], "ds_mnist")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_dsmnist_p")?;
                        return Ok(r);
                    }
                    "datensatz_csv" | "dataset_csv" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_ds_csv, &[p.into()], "ds_csv")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_dscsv_p")?;
                        return Ok(r);
                    }
                    "bild_laden" | "image_load" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_ds_bild, &[p.into()], "ds_bild")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_dsbild_p")?;
                        return Ok(r);
                    }
                    "text_tokenizer" | "char_tokenizer" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_ds_tokenizer, &[p.into()], "ds_tok")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_dstok_p")?;
                        return Ok(r);
                    }
                    // Byte-level BPE-Tokenizer (KIP-T2). Alle Args geliehen ->
                    // Post-Call-Release (pure-Reader-Muster wie ds_*).
                    "zeichen_fixed_trainiere" | "char_fixed_train" => {
                        if args.len() != 2 { return Err(format!("zeichen_fixed_trainiere: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_trainiere, &[a0.into(), a1.into()], "char_fixed_trainiere")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_trainiere_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_char_fixed_trainiere_1")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_kodiere" | "char_fixed_encode" => {
                        if args.len() != 2 { return Err(format!("zeichen_fixed_kodiere: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_kodiere, &[a0.into(), a1.into()], "char_fixed_kodiere")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_kodiere_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_char_fixed_kodiere_1")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_dekodiere" | "char_fixed_decode" => {
                        if args.len() != 2 { return Err(format!("zeichen_fixed_dekodiere: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_dekodiere, &[a0.into(), a1.into()], "char_fixed_dekodiere")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_dekodiere_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_char_fixed_dekodiere_1")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_speichern" | "char_fixed_save" => {
                        if args.len() != 2 { return Err(format!("zeichen_fixed_speichern: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_speichern, &[a0.into(), a1.into()], "char_fixed_speichern")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_speichern_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_char_fixed_speichern_1")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_laden" | "char_fixed_load" => {
                        if args.len() != 1 { return Err(format!("zeichen_fixed_laden: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_laden, &[a0.into()], "char_fixed_laden")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_laden_0")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_info" | "char_fixed_info" => {
                        if args.len() != 1 { return Err(format!("zeichen_fixed_info: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_info, &[a0.into()], "char_fixed_info")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_info_0")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_hash" | "char_fixed_hash" => {
                        if args.len() != 1 { return Err(format!("zeichen_fixed_hash: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_hash, &[a0.into()], "char_fixed_hash")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_hash_0")?;
                        return Ok(r);
                    }
                    "zeichen_fixed_hash64" | "char_fixed_hash64" => {
                        if args.len() != 1 { return Err(format!("zeichen_fixed_hash64: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_char_fixed_hash64, &[a0.into()], "char_fixed_hash64")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_char_fixed_hash64_0")?;
                        return Ok(r);
                    }
                    "silben_trainiere" | "syllable_train" => {
                        if args.len() != 3 { return Err(format!("silben_trainiere: erwartet 3 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let a2 = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_silben_trainiere, &[a0.into(), a1.into(), a2.into()], "silben_trainiere")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_trainiere_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_silben_trainiere_1")?;
                        self.call_rt_void(self.rt.moo_release, &[a2.into()], "rel_silben_trainiere_2")?;
                        return Ok(r);
                    }
                    "silben_grenzen" | "syllable_boundaries" => {
                        if args.len() != 2 { return Err("silben_grenzen: erwartet 2 Argumente".to_string()); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_silben_grenzen, &[a0.into(), a1.into()], "silben_grenzen")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_grenzen_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_silben_grenzen_1")?;
                        return Ok(r);
                    }
                    "silben_kodiere" | "syllable_encode" => {
                        if args.len() != 2 { return Err(format!("silben_kodiere: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_silben_kodiere, &[a0.into(), a1.into()], "silben_kodiere")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_kodiere_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_silben_kodiere_1")?;
                        return Ok(r);
                    }
                    "silben_dekodiere" | "syllable_decode" => {
                        if args.len() != 2 { return Err(format!("silben_dekodiere: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_silben_dekodiere, &[a0.into(), a1.into()], "silben_dekodiere")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_dekodiere_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_silben_dekodiere_1")?;
                        return Ok(r);
                    }
                    "silben_speichern" | "syllable_save" => {
                        if args.len() != 2 { return Err(format!("silben_speichern: erwartet 2 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_silben_speichern, &[a0.into(), a1.into()], "silben_speichern")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_speichern_0")?;
                        self.call_rt_void(self.rt.moo_release, &[a1.into()], "rel_silben_speichern_1")?;
                        return Ok(r);
                    }
                    "silben_laden" | "syllable_load" => {
                        if args.len() != 1 { return Err(format!("silben_laden: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_silben_laden, &[a0.into()], "silben_laden")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_laden_0")?;
                        return Ok(r);
                    }
                    "silben_info" | "syllable_info" => {
                        if args.len() != 1 { return Err(format!("silben_info: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_silben_info, &[a0.into()], "silben_info")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_info_0")?;
                        return Ok(r);
                    }
                    "silben_hash" | "syllable_hash" => {
                        if args.len() != 1 { return Err(format!("silben_hash: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_silben_hash, &[a0.into()], "silben_hash")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_hash_0")?;
                        return Ok(r);
                    }
                    "silben_hash64" | "syllable_hash64" => {
                        if args.len() != 1 { return Err(format!("silben_hash64: erwartet 1 Argument(e)")); }
                        let a0 = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_silben_hash64, &[a0.into()], "silben_hash64")?;
                        self.call_rt_void(self.rt.moo_release, &[a0.into()], "rel_silben_hash64_0")?;
                        return Ok(r);
                    }
                    "tokenizer_trainiere_parser" => {
                        if args.len() != 2 { return Err("tokenizer_trainiere_parser: erwartet 2 Argumente".to_string()); }
                        let k = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_trainiere_parser, &[k.into(), v.into()], "tok_train_parser")?;
                        self.call_rt_void(self.rt.moo_release, &[k.into()], "rel_tok_parser_k")?;
                        self.call_rt_void(self.rt.moo_release, &[v.into()], "rel_tok_parser_v")?;
                        return Ok(r);
                    }
                    "tokenizer_trainiere" | "tokenizer_train" => {
                        let k = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_trainiere, &[k.into(), v.into()], "tok_train")?;
                        self.call_rt_void(self.rt.moo_release, &[k.into()], "rel_toktr_k")?;
                        self.call_rt_void(self.rt.moo_release, &[v.into()], "rel_toktr_v")?;
                        return Ok(r);
                    }
                    "tokenizer_kodiere" | "tokenizer_encode" => {
                        let t = self.compile_expr(&args[0])?;
                        let x = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_kodiere, &[t.into(), x.into()], "tok_enc")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokenc_t")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_tokenc_x")?;
                        return Ok(r);
                    }
                    "tokenizer_dekodiere" | "tokenizer_decode" => {
                        let t = self.compile_expr(&args[0])?;
                        let x = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_dekodiere, &[t.into(), x.into()], "tok_dec")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokdec_t")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_tokdec_x")?;
                        return Ok(r);
                    }
                    "tokenizer_kodiere_stapel" | "tokenizer_encode_batch" => {
                        let t = self.compile_expr(&args[0])?;
                        let x = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_kodiere_stapel, &[t.into(), x.into()], "tok_encb")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokencb_t")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_tokencb_x")?;
                        return Ok(r);
                    }
                    "tokenizer_speichern" | "tokenizer_save" => {
                        let t = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_speichern, &[t.into(), p.into()], "tok_save")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_toksav_t")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_toksav_p")?;
                        return Ok(r);
                    }
                    "tokenizer_laden" | "tokenizer_load" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tok_laden, &[p.into()], "tok_load")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_tokld_p")?;
                        return Ok(r);
                    }
                    "tokenizer_info" => {
                        let t = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tok_info, &[t.into()], "tok_info")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokinf_t")?;
                        return Ok(r);
                    }
                    "tokenizer_hash" => {
                        let t = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tok_hash, &[t.into()], "tok_hash")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokhsh_t")?;
                        return Ok(r);
                    }
                    // KIP-T3: Chat-/Special-Tokens + Render
                    "tokenizer_chat_init" | "chat_tokenizer" => {
                        let t = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tok_chat_init, &[t.into()], "tok_chatinit")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokci_t")?;
                        return Ok(r);
                    }
                    "tokenizer_spezial_id" | "tokenizer_special_id" => {
                        let t = self.compile_expr(&args[0])?;
                        let n = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tok_spezial_id, &[t.into(), n.into()], "tok_spid")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokspid_t")?;
                        self.call_rt_void(self.rt.moo_release, &[n.into()], "rel_tokspid_n")?;
                        return Ok(r);
                    }
                    "tokenizer_rendern" | "tokenizer_render" | "chat_rendern" => {
                        let t = self.compile_expr(&args[0])?;
                        let dlg = self.compile_expr(&args[1])?;
                        let mb = if args.len() > 2 { self.compile_expr(&args[2])? }
                                 else { self.call_rt(self.rt.moo_none, &[], "tok_rnd_none")? };
                        let r = self.call_rt(self.rt.moo_tok_rendern, &[t.into(), dlg.into(), mb.into()], "tok_render")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_tokrnd_t")?;
                        self.call_rt_void(self.rt.moo_release, &[dlg.into()], "rel_tokrnd_d")?;
                        self.call_rt_void(self.rt.moo_release, &[mb.into()], "rel_tokrnd_b")?;
                        return Ok(r);
                    }
                    // KIP-E1: Streaming-Token-Shards. Alle Args geliehen -> Release.
                    "shard_schreiben" | "shard_write" => {
                        let p = self.compile_expr(&args[0])?;
                        let d = self.compile_expr(&args[1])?;
                        let a = self.compile_expr(&args[2])?;
                        let tv = self.compile_expr(&args[3])?;
                        let mk = if args.len() > 4 { self.compile_expr(&args[4])? }
                                 else { self.call_rt(self.rt.moo_none, &[], "shard_wr_none")? };
                        let r = self.call_rt(self.rt.moo_shard_schreiben, &[p.into(), d.into(), a.into(), tv.into(), mk.into()], "shard_wr")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shwr_p")?;
                        self.call_rt_void(self.rt.moo_release, &[d.into()], "rel_shwr_d")?;
                        self.call_rt_void(self.rt.moo_release, &[a.into()], "rel_shwr_a")?;
                        self.call_rt_void(self.rt.moo_release, &[tv.into()], "rel_shwr_tv")?;
                        self.call_rt_void(self.rt.moo_release, &[mk.into()], "rel_shwr_mk")?;
                        return Ok(r);
                    }
                    "shard_info" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_shard_info, &[p.into()], "shard_info")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shinf_p")?;
                        return Ok(r);
                    }
                    "shard_pruefen" | "shard_check" => {
                        let p = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_shard_pruefen, &[p.into()], "shard_check")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shchk_p")?;
                        return Ok(r);
                    }
                    "shard_fenster" | "shard_window" => {
                        let p = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let l = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_shard_fenster, &[p.into(), s.into(), l.into()], "shard_win")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shwin_p")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_shwin_s")?;
                        self.call_rt_void(self.rt.moo_release, &[l.into()], "rel_shwin_l")?;
                        return Ok(r);
                    }
                    "shard_fenster_maske" | "shard_mask_window" => {
                        let p = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let l = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_shard_fenster_maske, &[p.into(), s.into(), l.into()], "shard_mwin")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shmwin_p")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_shmwin_s")?;
                        self.call_rt_void(self.rt.moo_release, &[l.into()], "rel_shmwin_l")?;
                        return Ok(r);
                    }
                    "shard_reihenfolge" | "shard_order" => {
                        let p = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let l = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_shard_reihenfolge, &[p.into(), s.into(), l.into()], "shard_order")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shord_p")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_shord_s")?;
                        self.call_rt_void(self.rt.moo_release, &[l.into()], "rel_shord_l")?;
                        return Ok(r);
                    }
                    // KIP-E2: CPU-Voll-Checkpoint v2. Args geliehen -> Release.
                    "checkpoint_speichern" | "checkpoint_save" => {
                        let z = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_ckpt_speichern, &[z.into(), p.into()], "ckpt_save")?;
                        self.call_rt_void(self.rt.moo_release, &[z.into()], "rel_cksav_z")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_cksav_p")?;
                        return Ok(r);
                    }
                    "checkpoint_laden" | "checkpoint_load" => {
                        let p = self.compile_expr(&args[0])?;
                        let e = if args.len() > 1 { self.compile_expr(&args[1])? }
                                else { self.call_rt(self.rt.moo_none, &[], "ckpt_ld_none")? };
                        let r = self.call_rt(self.rt.moo_nn_ckpt_laden, &[p.into(), e.into()], "ckpt_load")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_ckld_p")?;
                        self.call_rt_void(self.rt.moo_release, &[e.into()], "rel_ckld_e")?;
                        return Ok(r);
                    }
                    "checkpoint_rotieren" | "checkpoint_rotate" => {
                        let d = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        let n = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_nn_ckpt_rotieren, &[d.into(), p.into(), n.into()], "ckpt_rot")?;
                        self.call_rt_void(self.rt.moo_release, &[d.into()], "rel_ckrot_d")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_ckrot_p")?;
                        self.call_rt_void(self.rt.moo_release, &[n.into()], "rel_ckrot_n")?;
                        return Ok(r);
                    }
                    "shard_split" => {
                        let p = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        let s = self.compile_expr(&args[2])?;
                        let r = self.call_rt(self.rt.moo_shard_split, &[p.into(), v.into(), s.into()], "shard_split")?;
                        self.call_rt_void(self.rt.moo_release, &[p.into()], "rel_shspl_p")?;
                        self.call_rt_void(self.rt.moo_release, &[v.into()], "rel_shspl_v")?;
                        self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_shspl_s")?;
                        return Ok(r);
                    }
                    "mischen" | "shuffle_data" => {
                        let x = self.compile_expr(&args[0])?;
                        let y = self.compile_expr(&args[1])?;
                        let s = if args.len() > 2 { self.compile_expr(&args[2])? }
                                else { self.call_rt(self.rt.moo_none, &[], "ds_mi_none")? };
                        let r = self.call_rt(self.rt.moo_ds_mischen,
                            &[x.into(), y.into(), s.into()], "ds_mischen")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_dsmi_x")?;
                        self.call_rt_void(self.rt.moo_release, &[y.into()], "rel_dsmi_y")?;
                        return Ok(r);
                    }
                    "normalisieren" | "normalize" => {
                        let t = self.compile_expr(&args[0])?;
                        let a = if args.len() > 1 { self.compile_expr(&args[1])? }
                                else { self.call_rt(self.rt.moo_none, &[], "ds_no_none")? };
                        let r = self.call_rt(self.rt.moo_ds_normalisieren,
                            &[t.into(), a.into()], "ds_normalisieren")?;
                        self.call_rt_void(self.rt.moo_release, &[t.into()], "rel_dsno_t")?;
                        self.call_rt_void(self.rt.moo_release, &[a.into()], "rel_dsno_a")?;
                        return Ok(r);
                    }
                    "länge" | "len" => {
                        // Pure-Reader-Builtin: moo_length released sein Arg NICHT
                        // (verifiziert moo_stdlib.c) — compile_expr liefert +1 owning
                        // (INV-1), also hier Post-Call-Release. [VERIFY]-Altlast CG1,
                        // Leak-Beweis: 81MB vs 21MB Baseline bei 1M Iterationen.
                        let arg = self.compile_expr(&args[0])?;
                        let res = self.call_rt(self.rt.moo_length, &[arg.into()], "len")?;
                        self.call_rt_void(self.rt.moo_release, &[arg.into()], "rel_len_arg")?;
                        return Ok(res);
                    }
                    "text" | "str" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_to_string, &[arg.into()], "str");
                    }
                    "zahl" | "num" => {
                        // Pure-Reader-Builtin wie länge: moo_to_number (moo_core.c)
                        // released nicht — Post-Call-Release des +1-Args.
                        let arg = self.compile_expr(&args[0])?;
                        let res = self.call_rt(self.rt.moo_to_number, &[arg.into()], "to_num")?;
                        self.call_rt_void(self.rt.moo_release, &[arg.into()], "rel_num_arg")?;
                        return Ok(res);
                    }
                    "eingabe" | "input" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_input, &[arg.into()], "input");
                    }
                    "datei_lesen" | "file_read" | "dl" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_read, &[arg.into()], "file_read");
                    }
                    "datei_schreiben" | "file_write" | "ds" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_file_write, &[a.into(), b.into()], "file_write");
                    }
                    "datei_lesen_bytes" | "file_read_bytes" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_read_bytes, &[arg.into()], "file_read_bytes");
                    }
                    "datei_schreiben_bytes" | "file_write_bytes" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_file_write_bytes, &[a.into(), b.into()], "file_write_bytes");
                    }
                    "datei_anhängen" | "file_append" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_file_append, &[a.into(), b.into()], "file_append");
                    }
                    "datei_zeilen" | "file_lines" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_lines, &[arg.into()], "file_lines");
                    }
                    "datei_existiert" | "file_exists" | "de" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_exists, &[arg.into()], "file_exists");
                    }
                    "datei_löschen" | "file_delete" | "dd" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_delete, &[arg.into()], "file_delete");
                    }
                    "datei_mtime" | "file_mtime" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_mtime, &[arg.into()], "file_mtime");
                    }
                    "datei_ist_verzeichnis" | "file_is_dir" | "ist_verzeichnis" | "is_dir" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_is_dir, &[arg.into()], "file_is_dir");
                    }
                    "datei_mkdir" | "file_mkdir" | "mkdir" | "verzeichnis_erstelle" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_file_mkdir, &[arg.into()], "file_mkdir");
                    }
                    "verzeichnis_liste" | "dir_list" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_dir_list, &[arg.into()], "dir_list");
                    }
                    "starte" | "spawn" => {
                        // P2-T119: beliebig viele Argumente. Genau eines: bisheriger
                        // Weg (IR unveraendert); sonst als Liste an moo_thread_spawn_n
                        // (nur bei Bedarf deklariert), jedes Element nach der Teilen-Regel.
                        if args.len() == 2 {
                            let func = self.compile_expr(&args[0])?;
                            let arg = self.compile_expr(&args[1])?;
                            return self.call_rt(self.rt.moo_thread_spawn, &[func.into(), arg.into()], "spawn");
                        }
                        if args.is_empty() || args.len() > 9 {
                            return Err(format!(
                                "'{name}' braucht eine Funktion und hoechstens 8 Argumente fuer sie, bekommen {} Argumente.",
                                args.len()));
                        }
                        let func = self.compile_expr(&args[0])?;
                        let liste = self.compile_expr(&Expr::List(args[1..].to_vec()))?;
                        let spawn_n = match self.module.get_function("moo_thread_spawn_n") {
                            Some(f) => f,
                            None => self.module.add_function("moo_thread_spawn_n", self.rt.moo_thread_spawn.get_type(), None),
                        };
                        return self.call_rt(spawn_n, &[func.into(), liste.into()], "spawn_n");
                    }
                    "kanal" | "channel" => {
                        if args.is_empty() {
                            let cap = self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(16.0).into()], "cap16")?;
                            return self.call_rt(self.rt.moo_channel_new, &[cap.into()], "channel");
                        } else {
                            let cap = self.compile_expr(&args[0])?;
                            return self.call_rt(self.rt.moo_channel_new, &[cap.into()], "channel");
                        }
                    }
                    // JSON
                    "json_parse" | "json_lesen" | "jp" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_json_parse, &[arg.into()], "json_parse");
                    }
                    "json_string" | "json_text" | "js" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_json_string, &[arg.into()], "json_string");
                    }
                    // HTTP
                    "http_hole_mit_headers" | "http_get_with_headers" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_http_get_with_headers, &[a.into(), b.into()], "http_get_h");
                    }
                    "http_sende_mit_headers" | "http_post_with_headers" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_http_post_with_headers, &[a.into(), b.into(), c.into()], "http_post_h");
                    }
                    "http_get" | "http_hole" | "hg" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_http_get, &[arg.into()], "http_get");
                    }
                    "http_post" | "http_sende" | "hp" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_http_post, &[a.into(), b.into()], "http_post");
                    }
                    // Crypto & Security
                    "sha256" | "sh" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sha256, &[arg.into()], "sha256");
                    }
                    "sha1" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sha1, &[arg.into()], "sha1");
                    }
                    "sha1_bytes" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sha1_bytes, &[arg.into()], "sha1_bytes");
                    }
                    "sha256_bytes" | "sha256_rohbytes" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sha256_bytes, &[arg.into()], "sha256_bytes");
                    }
                    "hmac_sha256" | "hmac" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_hmac_sha256, &[a.into(), b.into()], "hmac_sha256");
                    }
                    "aes_verschluessle" | "aes_encrypt" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_aes_encrypt, &[a.into(), b.into()], "aes_verschluessle");
                    }
                    "aes_entschluessle" | "aes_decrypt" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_aes_decrypt, &[a.into(), b.into()], "aes_entschluessle");
                    }
                    "pbkdf2_sha256" | "pbkdf2" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        let d = self.compile_expr(&args[3])?;
                        return self.call_rt(self.rt.moo_pbkdf2_sha256, &[a.into(), b.into(), c.into(), d.into()], "pbkdf2_sha256");
                    }
                    "sichere_zufall" | "secure_random" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_secure_random, &[arg.into()], "secure_random");
                    }
                    // NETZK-4: Bytepuffer sind laengenmarkierte Moo-Strings.
                    "hkdf_sha256" | "hkdf" => {
                        let a = self.compile_expr(&args[0])?; let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?; let d = self.compile_expr(&args[3])?;
                        return self.call_rt(self.rt.moo_krypto_hkdf_api, &[a.into(), b.into(), c.into(), d.into()], "hkdf_sha256");
                    }
                    "aead_verschluessle" | "aead_encrypt" => {
                        let a = self.compile_expr(&args[0])?; let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?; let d = self.compile_expr(&args[3])?;
                        let e = self.compile_expr(&args[4])?;
                        return self.call_rt(self.rt.moo_krypto_aead_encrypt_api, &[a.into(), b.into(), c.into(), d.into(), e.into()], "aead_encrypt");
                    }
                    "aead_entschluessle" | "aead_decrypt" => {
                        let a = self.compile_expr(&args[0])?; let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?; let d = self.compile_expr(&args[3])?;
                        let e = self.compile_expr(&args[4])?;
                        return self.call_rt(self.rt.moo_krypto_aead_decrypt_api, &[a.into(), b.into(), c.into(), d.into(), e.into()], "aead_decrypt");
                    }
                    "x25519" | "schluessel_tausch" => {
                        let a = self.compile_expr(&args[0])?; let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_krypto_x25519_api, &[a.into(), b.into()], "x25519");
                    }
                    "x25519_oeffentlich" | "x25519_public" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_krypto_x25519_public_api, &[a.into()], "x25519_public");
                    }
                    "base64_encode" | "base64_kodieren" | "b64e" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_base64_encode, &[arg.into()], "base64_encode");
                    }
                    "base64_decode" | "base64_dekodieren" | "b64d" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_base64_decode, &[arg.into()], "base64_decode");
                    }
                    "html_bereinigen" | "sanitize_html" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sanitize_html, &[arg.into()], "sanitize_html");
                    }
                    "sql_bereinigen" | "sanitize_sql" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sanitize_sql, &[arg.into()], "sanitize_sql");
                    }
                    // Datenbank
                    "db_verbinde" | "db_connect" | "dbv" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_db_connect, &[arg.into()], "db_connect");
                    }
                    "db_vorbereite" | "db_prepare" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_db_prepare, &[a.into(), b.into()], "db_prep");
                    }
                    "db_abfrage_mit_params" | "db_query_with_params" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_db_query_with_params, &[a.into(), b.into(), c.into()], "db_query_p");
                    }
                    "db_ausführen_mit_params" | "db_execute_with_params" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_db_execute_with_params, &[a.into(), b.into(), c.into()], "db_exec_p");
                    }
                    "db_abfrage" | "db_query" | "dba" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_db_query, &[a.into(), b.into()], "db_query");
                    }
                    "db_ausführen" | "db_execute" | "dbe" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_db_execute, &[a.into(), b.into()], "db_execute");
                    }
                    "db_schliessen" | "db_close" | "dbs" => {
                        let arg = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_db_close, &[arg.into()], "db_close")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Raw Memory (GEFAEHRLICH)
                    "speicher_lesen" | "mem_read" => {
                        let addr = self.compile_expr(&args[0])?;
                        let size = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_mem_read, &[addr.into(), size.into()], "mem_read");
                    }
                    "speicher_schreiben" | "mem_write" => {
                        let addr = self.compile_expr(&args[0])?;
                        let val = self.compile_expr(&args[1])?;
                        let size = self.compile_expr(&args[2])?;
                        self.call_rt_void(self.rt.moo_mem_write, &[addr.into(), val.into(), size.into()], "mem_write")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // CPU / Hardware
                    "halt" | "anhalten" => {
                        self.call_rt_void(self.rt.moo_cpu_halt, &[], "cpu_halt")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "cli" | "interrupts_aus" => {
                        self.call_rt_void(self.rt.moo_cpu_cli, &[], "cpu_cli")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "sti" | "interrupts_an" => {
                        self.call_rt_void(self.rt.moo_cpu_sti, &[], "cpu_sti")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "port_lese" | "port_in" => {
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_io_inb, &[port.into()], "io_inb");
                    }
                    "port_schreibe" | "port_out" => {
                        let port = self.compile_expr(&args[0])?;
                        let val = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_io_outb, &[port.into(), val.into()], "io_outb")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ======================================================
                    // Kernel (bare-metal, kern_* — Plan-010)
                    // Harte hosted/bare-Trennung: require_bare() laesst diese
                    // Builtins nur im --no-stdlib/*-bare-Build (oder mit
                    // --erlaube-kern) zu. Alle MooValue-returning.
                    // P011-B1: 16/32-bit Port-I/O (require_bare — im Gegensatz
                    // zu den 8-bit-Altarmen port_lese/port_in oben; Folgepunkt).
                    // P7-T22: kanonisch port_lese*/port_schreibe* (EN port_in*/port_out*),
                    // gleiche Namen wie die typisierten @nativ-Intrinsics; kern_in*/
                    // kern_out* sind Uebergangsaliase mit Hinweis M-H008.
                    "kern_inw" | "port_in16" | "port_lese16" => {
                        self.require_bare(name)?;
                        self.veralteter_name(name, "kern_inw", "port_lese16");
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_io_inw, &[port.into()], "io_inw");
                    }
                    "kern_outw" | "port_out16" | "port_schreibe16" => {
                        self.require_bare(name)?;
                        self.veralteter_name(name, "kern_outw", "port_schreibe16");
                        let port = self.compile_expr(&args[0])?;
                        let val = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_io_outw, &[port.into(), val.into()], "io_outw")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "kern_inl" | "port_in32" | "port_lese32" => {
                        self.require_bare(name)?;
                        self.veralteter_name(name, "kern_inl", "port_lese32");
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_io_inl, &[port.into()], "io_inl");
                    }
                    "kern_outl" | "port_out32" | "port_schreibe32" => {
                        self.require_bare(name)?;
                        self.veralteter_name(name, "kern_outl", "port_schreibe32");
                        let port = self.compile_expr(&args[0])?;
                        let val = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_io_outl, &[port.into(), val.into()], "io_outl")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // P011-B2: CPU-Steuer-Builtins (rdmsr/wrmsr, CRx, lgdt/lidt)
                    "kern_rdmsr_lo" | "kern_msr_lese_lo" => {
                        self.require_bare(name)?;
                        let msr = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_rdmsr_lo, &[msr.into()], "rdmsr_lo");
                    }
                    "kern_rdmsr_hi" | "kern_msr_lese_hi" => {
                        self.require_bare(name)?;
                        let msr = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_rdmsr_hi, &[msr.into()], "rdmsr_hi");
                    }
                    "kern_wrmsr" | "kern_msr_schreibe" => {
                        self.require_bare(name)?;
                        let msr = self.compile_expr(&args[0])?;
                        let lo = self.compile_expr(&args[1])?;
                        let hi = self.compile_expr(&args[2])?;
                        self.call_rt_void(self.rt.kern_wrmsr, &[msr.into(), lo.into(), hi.into()], "wrmsr")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "kern_cr_lese" | "kern_cr_read" => {
                        self.require_bare(name)?;
                        let n = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_cr_lese, &[n.into()], "cr_lese");
                    }
                    "kern_cr_setze" | "kern_cr_write" => {
                        self.require_bare(name)?;
                        let n = self.compile_expr(&args[0])?;
                        let wert = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.kern_cr_setze, &[n.into(), wert.into()], "cr_setze")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "kern_lgdt" => {
                        self.require_bare(name)?;
                        let basis = self.compile_expr(&args[0])?;
                        let limit = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.kern_lgdt, &[basis.into(), limit.into()], "lgdt")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "kern_lidt" => {
                        self.require_bare(name)?;
                        let basis = self.compile_expr(&args[0])?;
                        let limit = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.kern_lidt, &[basis.into(), limit.into()], "lidt")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ======================================================
                    // K2 — Early-Console (seriell + VGA)
                    "kern_seriell_init" | "kern_serial_init" => {
                        self.require_bare("kern_seriell_init")?;
                        return self.call_rt(self.rt.kern_seriell_init, &[], "kern_seriell_init");
                    }
                    "kern_seriell_zeichen" | "kern_serial_char" => {
                        self.require_bare("kern_seriell_zeichen")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_seriell_zeichen, &[a.into()], "kern_seriell_zeichen");
                    }
                    "kern_seriell_dez" | "kern_serial_dec" => {
                        self.require_bare("kern_seriell_dez")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_seriell_dez, &[a.into()], "kern_seriell_dez");
                    }
                    "kern_seriell_hex" | "kern_serial_hex" => {
                        self.require_bare("kern_seriell_hex")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_seriell_hex, &[a.into()], "kern_seriell_hex");
                    }
                    "kern_vga_init" => {
                        self.require_bare("kern_vga_init")?;
                        return self.call_rt(self.rt.kern_vga_init, &[], "kern_vga_init");
                    }
                    "kern_vga_farbe" | "kern_vga_color" => {
                        self.require_bare("kern_vga_farbe")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_vga_farbe, &[a.into()], "kern_vga_farbe");
                    }
                    "kern_vga_zeichen" | "kern_vga_char" => {
                        self.require_bare("kern_vga_zeichen")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_vga_zeichen, &[a.into()], "kern_vga_zeichen");
                    }
                    // K3 — Bare-Allocator
                    "kern_speicher_init" | "kern_mem_init" => {
                        self.require_bare("kern_speicher_init")?;
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.kern_speicher_init, &[a.into(), b.into()], "kern_speicher_init");
                    }
                    "kern_alloc" | "kern_reserviere" => {
                        self.require_bare("kern_alloc")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_alloc, &[a.into()], "kern_alloc");
                    }
                    "kern_frei" | "kern_free" => {
                        self.require_bare("kern_frei")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_frei, &[a.into()], "kern_frei");
                    }
                    "kern_speicher_frei" | "kern_mem_free_bytes" => {
                        self.require_bare("kern_speicher_frei")?;
                        return self.call_rt(self.rt.kern_speicher_frei, &[], "kern_speicher_frei");
                    }
                    "kern_speicher_belegt" | "kern_mem_used" => {
                        self.require_bare("kern_speicher_belegt")?;
                        return self.call_rt(self.rt.kern_speicher_belegt, &[], "kern_speicher_belegt");
                    }
                    "kern_speicher_peak" | "kern_mem_peak" => {
                        self.require_bare("kern_speicher_peak")?;
                        return self.call_rt(self.rt.kern_speicher_peak, &[], "kern_speicher_peak");
                    }
                    // K4 — Interrupt-Infrastruktur (IDT/PIC/PIT)
                    "kern_idt_init" => {
                        self.require_bare("kern_idt_init")?;
                        return self.call_rt(self.rt.kern_idt_init, &[], "kern_idt_init");
                    }
                    "kern_pic_remap" => {
                        self.require_bare("kern_pic_remap")?;
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.kern_pic_remap, &[a.into(), b.into()], "kern_pic_remap");
                    }
                    "kern_pic_maskiere" | "kern_pic_mask" => {
                        self.require_bare("kern_pic_maskiere")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_pic_maskiere, &[a.into()], "kern_pic_maskiere");
                    }
                    "kern_pic_demaskiere" | "kern_pic_unmask" => {
                        self.require_bare("kern_pic_demaskiere")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_pic_demaskiere, &[a.into()], "kern_pic_demaskiere");
                    }
                    "kern_pic_eoi" => {
                        self.require_bare("kern_pic_eoi")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_pic_eoi, &[a.into()], "kern_pic_eoi");
                    }
                    "kern_timer_init" => {
                        self.require_bare("kern_timer_init")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_timer_init, &[a.into()], "kern_timer_init");
                    }
                    "kern_ticks" => {
                        self.require_bare("kern_ticks")?;
                        return self.call_rt(self.rt.kern_ticks, &[], "kern_ticks");
                    }
                    "kern_stackprot_selbsttest" => {
                        self.require_bare("kern_stackprot_selbsttest")?;
                        return self.call_rt(self.rt.kern_stackprot_selbsttest, &[], "kern_stackprot_selbsttest");
                    }
                    // P7-T2: Framebuffer (Bildschirm). Masse des Hardware-Framebuffers
                    // abfragen und eine Surface (surface_new/surface_rect/...) hinein
                    // zeigen. C-Seite ist nur Boot-Adapter (UEFI: boot/uefi_entry.c).
                    "kern_bild_breite" | "kern_fb_width" => {
                        self.require_bare("kern_bild_breite")?;
                        return self.call_rt(self.rt.kern_bild_breite, &[], "kern_bild_breite");
                    }
                    "kern_bild_hoehe" | "kern_bild_höhe" | "kern_fb_height" => {
                        self.require_bare("kern_bild_hoehe")?;
                        return self.call_rt(self.rt.kern_bild_hoehe, &[], "kern_bild_hoehe");
                    }
                    "kern_bild_zeige" | "kern_fb_present" => {
                        self.require_bare("kern_bild_zeige")?;
                        if args.len() != 1 {
                            return Err("kern_bild_zeige erwartet genau ein Argument (Surface)".to_string());
                        }
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_bild_zeige, &[a.into()], "kern_bild_zeige");
                    }
                    // P7-T2: Eingabe im Boot-Umfeld. Tastennamen wie ui_moo ("Tab",
                    // "Return", "Escape", "Up"/"Down"/"Left"/"Right", "BackSpace", "space",
                    // Einzelzeichen) bzw. "" ohne Taste; Maus wie 2D-API maus_x/maus_y/
                    // maus_gedrueckt, aber gepollt (kern_maus_lesen) und ohne Fenster.
                    "kern_taste_lesen" | "kern_key_read" => {
                        self.require_bare("kern_taste_lesen")?;
                        return self.call_rt(self.rt.kern_taste_lesen, &[], "kern_taste_lesen");
                    }
                    "kern_maus_vorhanden" | "kern_mouse_present" => {
                        self.require_bare("kern_maus_vorhanden")?;
                        return self.call_rt(self.rt.kern_maus_vorhanden, &[], "kern_maus_vorhanden");
                    }
                    "kern_maus_lesen" | "kern_mouse_poll" => {
                        self.require_bare("kern_maus_lesen")?;
                        return self.call_rt(self.rt.kern_maus_lesen, &[], "kern_maus_lesen");
                    }
                    "kern_maus_x" | "kern_mouse_x" => {
                        self.require_bare("kern_maus_x")?;
                        return self.call_rt(self.rt.kern_maus_x, &[], "kern_maus_x");
                    }
                    "kern_maus_y" | "kern_mouse_y" => {
                        self.require_bare("kern_maus_y")?;
                        return self.call_rt(self.rt.kern_maus_y, &[], "kern_maus_y");
                    }
                    "kern_maus_gedrueckt" | "kern_maus_gedrückt" | "kern_mouse_pressed" => {
                        self.require_bare("kern_maus_gedrueckt")?;
                        return self.call_rt(self.rt.kern_maus_gedrueckt, &[], "kern_maus_gedrueckt");
                    }
                    "kern_eingabe_warten" | "kern_input_wait" => {
                        self.require_bare("kern_eingabe_warten")?;
                        return self.call_rt(self.rt.kern_eingabe_warten, &[], "kern_eingabe_warten");
                    }
                    // P7-T9: eine Textzeile als UDP-Broadcast-Log senden (Bool; falsch ohne Netz).
                    "kern_netz_sende" | "kern_net_send" => {
                        self.require_bare("kern_netz_sende")?;
                        if args.len() != 1 {
                            return Err("kern_netz_sende erwartet genau ein Argument (Text)".to_string());
                        }
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_netz_sende, &[a.into()], "kern_netz_sende");
                    }
                    // Netzwerk
                    "kern_netz_status" | "kern_net_status" => {
                        self.require_bare("kern_netz_status")?;
                        return self.call_rt(self.rt.kern_netz_status, &[], "kern_netz_status");
                    }
                    "kern_treiber_status" | "kern_driver_status" => {
                        self.require_bare("kern_treiber_status")?;
                        return self.call_rt(self.rt.kern_treiber_status, &[], "kern_treiber_status");
                    }
                    "kern_acpi_rsdp" => {
                        self.require_bare("kern_acpi_rsdp")?;
                        return self.call_rt(self.rt.kern_acpi_rsdp, &[], "kern_acpi_rsdp");
                    }
                    "kern_warte_us" | "kern_wait_us" => {
                        self.require_bare("kern_warte_us")?;
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.kern_warte_us, &[a.into()], "kern_warte_us");
                    }
                    "kern_text_in_puffer" | "kern_text_to_buffer" => {
                        self.require_bare("kern_text_in_puffer")?;
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.kern_text_in_puffer, &[a.into(), b.into(), c.into()], "kern_text_in_puffer");
                    }
                    "kern_text_aus_puffer" | "kern_text_from_buffer" => {
                        self.require_bare("kern_text_aus_puffer")?;
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.kern_text_aus_puffer, &[a.into(), b.into()], "kern_text_aus_puffer");
                    }
                    "kern_bild_starten" | "kern_image_start" => {
                        self.require_bare("kern_bild_starten")?;
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.kern_bild_starten, &[a.into(), b.into()], "kern_bild_starten");
                    }
                    "ausfuehren" | "eval" => {
                        let code = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_eval, &[code.into()], "eval");
                    }
                    "web_antworten" | "web_respond" => {
                        let req = self.compile_expr(&args[0])?;
                        let body = self.compile_expr(&args[1])?;
                        let status = if args.len() > 2 {
                            self.compile_expr(&args[2])?
                        } else {
                            self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(200.0).into()], "s200")?
                        };
                        return self.call_rt(self.rt.moo_web_respond, &[req.into(), body.into(), status.into()], "respond");
                    }
                    "web_json" => {
                        let req = self.compile_expr(&args[0])?;
                        let data = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_web_json, &[req.into(), data.into()], "json");
                    }
                    "web_datei" | "web_file" => {
                        let req = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_web_file, &[req.into(), path.into()], "web_file");
                    }
                    "web_template" | "web_vorlage" => {
                        let req = self.compile_expr(&args[0])?;
                        let html = self.compile_expr(&args[1])?;
                        let vars = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_web_template, &[req.into(), html.into(), vars.into()], "web_template");
                    }
                    "web_server" | "web_erstelle" => {
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_web_server, &[port.into()], "web_server");
                    }
                    "tcp_server" => {
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tcp_server, &[port.into()], "tcp_server");
                    }
                    "tcp_verbinde" | "tcp_connect" => {
                        let host = self.compile_expr(&args[0])?;
                        let port = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tcp_connect, &[host.into(), port.into()], "tcp_connect");
                    }
                    "tls_verbinde" | "tls_connect" => {
                        let host = self.compile_expr(&args[0])?;
                        let port = self.compile_expr(&args[1])?;
                        if args.len() >= 3 {
                            let t = self.compile_expr(&args[2])?;
                            return self.call_rt(self.rt.moo_tls_connect_timeout, &[host.into(), port.into(), t.into()], "tls_connect");
                        }
                        return self.call_rt(self.rt.moo_tls_connect, &[host.into(), port.into()], "tls_connect");
                    }
                    "tls_starttls" | "tls_upgrade" => {
                        let sock = self.compile_expr(&args[0])?;
                        let host = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tls_starttls, &[sock.into(), host.into()], "tls_starttls");
                    }
                    "tls_sende" | "tls_send" => {
                        let h = self.compile_expr(&args[0])?;
                        let d = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tls_send, &[h.into(), d.into()], "tls_send");
                    }
                    "tls_empfange" | "tls_recv" => {
                        let h = self.compile_expr(&args[0])?;
                        let m = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tls_recv, &[h.into(), m.into()], "tls_recv");
                    }
                    "tls_schliesse" | "tls_close" => {
                        let h = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tls_close, &[h.into()], "tls_close");
                    }
                    "udp_socket" => {
                        let port = if args.is_empty() {
                            self.call_rt(self.rt.moo_none, &[], "none")?
                        } else {
                            self.compile_expr(&args[0])?
                        };
                        return self.call_rt(self.rt.moo_udp_socket, &[port.into()], "udp_socket");
                    }
                    // Grafik — Fenster
                    "fenster_erstelle" | "window_create" | "fe" => {
                        let title = self.compile_expr(&args[0])?;
                        let w = self.compile_expr(&args[1])?;
                        let h = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_window_create, &[title.into(), w.into(), h.into()], "win");
                    }
                    // Hybrid 2D+3D Pipeline (M5)
                    "fenster_unified" | "unified_window" | "fenster_hybrid" => {
                        let title = self.compile_expr(&args[0])?;
                        let w = self.compile_expr(&args[1])?;
                        let h = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_hybrid_create, &[title.into(), w.into(), h.into()], "hybrid_win");
                    }
                    "hybrid_offen" | "hybrid_is_open" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_hybrid_is_open, &[win.into()], "hybrid_open");
                    }
                    "hybrid_löschen" | "hybrid_clear" => {
                        let win = self.compile_expr(&args[0])?;
                        let r = self.compile_expr(&args[1])?;
                        let g = self.compile_expr(&args[2])?;
                        let b = self.compile_expr(&args[3])?;
                        self.call_rt_void(self.rt.moo_hybrid_clear, &[win.into(), r.into(), g.into(), b.into()], "hybrid_clr")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "hybrid_aktualisieren" | "hybrid_update" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_hybrid_update, &[win.into()], "hybrid_upd")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "hybrid_schliessen" | "hybrid_close" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_hybrid_close, &[win.into()], "hybrid_close")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zeichne_rechteck_z" | "draw_rect_z" => {
                        let win = self.compile_expr(&args[0])?;
                        let x = self.compile_expr(&args[1])?;
                        let y = self.compile_expr(&args[2])?;
                        let z = self.compile_expr(&args[3])?;
                        let w = self.compile_expr(&args[4])?;
                        let h = self.compile_expr(&args[5])?;
                        let c = self.compile_expr(&args[6])?;
                        self.call_rt_void(self.rt.moo_hybrid_rect_z, &[win.into(), x.into(), y.into(), z.into(), w.into(), h.into(), c.into()], "rect_z")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-10: (fenster, text, x, y, z, groesse, farbe)
                    "zeichne_text_z" | "draw_text_z" if args.len() == 7 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_hybrid_text_z, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "text_z")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zeichne_linie_z" | "draw_line_z" => {
                        let win = self.compile_expr(&args[0])?;
                        let x1 = self.compile_expr(&args[1])?;
                        let y1 = self.compile_expr(&args[2])?;
                        let x2 = self.compile_expr(&args[3])?;
                        let y2 = self.compile_expr(&args[4])?;
                        let z = self.compile_expr(&args[5])?;
                        let c = self.compile_expr(&args[6])?;
                        self.call_rt_void(self.rt.moo_hybrid_line_z, &[win.into(), x1.into(), y1.into(), x2.into(), y2.into(), z.into(), c.into()], "line_z")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zeichne_kreis_z" | "draw_circle_z" => {
                        let win = self.compile_expr(&args[0])?;
                        let cx = self.compile_expr(&args[1])?;
                        let cy = self.compile_expr(&args[2])?;
                        let z = self.compile_expr(&args[3])?;
                        let r = self.compile_expr(&args[4])?;
                        let c = self.compile_expr(&args[5])?;
                        self.call_rt_void(self.rt.moo_hybrid_circle_z, &[win.into(), cx.into(), cy.into(), z.into(), r.into(), c.into()], "circ_z")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "sprite_zeichnen_z" | "sprite_draw_z" => {
                        let win = self.compile_expr(&args[0])?;
                        let id = self.compile_expr(&args[1])?;
                        let x = self.compile_expr(&args[2])?;
                        let y = self.compile_expr(&args[3])?;
                        let z = self.compile_expr(&args[4])?;
                        let w = self.compile_expr(&args[5])?;
                        let h = self.compile_expr(&args[6])?;
                        self.call_rt_void(self.rt.moo_hybrid_sprite_z, &[win.into(), id.into(), x.into(), y.into(), z.into(), w.into(), h.into()], "spr_z")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "sprite_laden_unified" | "unified_sprite_load" | "hybrid_sprite_load" => {
                        let win = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_hybrid_sprite_load, &[win.into(), path.into()], "spr_load_z");
                    }
                    "fenster_löschen" | "window_clear" | "fl" => {
                        let win = self.compile_expr(&args[0])?;
                        let color = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_window_clear, &[win.into(), color.into()], "clear")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "fenster_aktualisieren" | "window_update" | "fa" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_window_update, &[win.into()], "update")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "fenster_offen" | "window_is_open" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_window_is_open, &[win.into()], "is_open");
                    }
                    "fenster_schliessen" | "window_close" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_window_close, &[win.into()], "close")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Grafik — Zeichnen
                    "zeichne_rechteck" | "draw_rect" | "zr" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_draw_rect, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "rect")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zeichne_kreis" | "draw_circle" | "zk" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_draw_circle, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "circle")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zeichne_linie" | "draw_line" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_draw_line, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "line")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zeichne_pixel" | "draw_pixel" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_draw_pixel, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "pixel")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Sprites (SDL2_image)
                    "sprite_laden" | "sprite_load" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_sprite_load, &[a[0].into(), a[1].into()], "spr_load");
                    }
                    "sprite_zeichnen" | "sprite_draw" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_sprite_draw, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "spr_draw")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "sprite_zeichnen_skaliert" | "sprite_draw_scaled" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_sprite_draw_scaled, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "spr_scaled")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "sprite_ausschnitt" | "sprite_region" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_sprite_draw_region, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into(), a[8].into(), a[9].into()], "spr_region")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "sprite_breite" | "sprite_width" => {
                        let id = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sprite_width, &[id.into()], "spr_w");
                    }
                    "sprite_hoehe" | "sprite_height" => {
                        let id = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_sprite_height, &[id.into()], "spr_h");
                    }
                    "sprite_freigeben" | "sprite_free" => {
                        let id = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_sprite_free, &[id.into()], "spr_free")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Grafik — Input (K4)
                    "taste_gedrückt" | "key_pressed" => {
                        let key = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_key_pressed, &[key.into()], "key");
                    }
                    "maus_x" | "mouse_x" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_mouse_x, &[win.into()], "mx");
                    }
                    "maus_y" | "mouse_y" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_mouse_y, &[win.into()], "my");
                    }
                    "maus_gedrückt" | "mouse_pressed" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_mouse_pressed, &[win.into()], "mp");
                    }
                    "warte" | "delay" => {
                        let ms = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_delay, &[ms.into()], "delay")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "ereignisse_pumpen" | "pump_events" => {
                        self.call_rt_void(self.rt.moo_pump_events, &[], "pump_events")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Freeze/Immutable
                    "zeit_ms" | "time_ms" => {
                        return self.call_rt(self.rt.moo_time_ms, &[], "time_ms");
                    }
                    "bytes_neu" | "bytes_new" => {
                        // bytes_neu(liste) → binary-safe String
                        let list = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_bytes_to_string, &[list.into()], "bytes_new");
                    }
                    "bytes_zu_liste" | "bytes_to_list" | "string_zu_bytes" | "string_to_bytes" => {
                        let s = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_string_to_bytes, &[s.into()], "str_to_bytes");
                    }
                    "zeit" | "time" => {
                        return self.call_rt(self.rt.moo_time, &[], "time");
                    }
                    "gpu_statistik" => {
                        return self.call_rt(self.rt.moo_ki_gpu_statistik, &[], "gpu_statistik");
                    }
                    "gpu_statistik_reset" => {
                        return self.call_rt(self.rt.moo_ki_gpu_statistik_reset, &[], "gpu_statistik_reset");
                    }
                    "haltepunkt" | "breakpoint" => {
                        // Zeilennummer als Argument übergeben
                        let line_num = if args.is_empty() {
                            self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(0.0).into()], "bp_line")?
                        } else {
                            self.compile_expr(&args[0])?
                        };
                        self.call_rt_void(self.rt.moo_breakpoint, &[line_num.into()], "bp")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "systemaufruf" | "syscall" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_syscall,
                            &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "syscall");
                    }
                    "einfrieren" | "freeze" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_freeze, &[arg.into()], "freeze");
                    }
                    "ist_eingefroren" | "is_frozen" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_is_frozen, &[arg.into()], "is_frozen");
                    }
                    // Events
                    "ereignis_bei" | "event_on" => {
                        let obj = self.compile_expr(&args[0])?;
                        let event = self.compile_expr(&args[1])?;
                        let cb = self.compile_expr(&args[2])?;
                        self.call_rt_void(self.rt.moo_event_on, &[obj.into(), event.into(), cb.into()], "ev_on")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "ereignis_auslösen" | "event_emit" => {
                        let obj = self.compile_expr(&args[0])?;
                        let event = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_event_emit, &[obj.into(), event.into()], "ev_emit")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // 3D Grafik
                    "3d_erstelle" | "3d_create" | "raum_erstelle" | "space_create" | "re" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_3d_create, &[a[0].into(), a[1].into(), a[2].into()], "3d_win");
                    }
                    "3d_offen" | "3d_is_open" | "raum_offen" | "space_is_open" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_is_open, &[win.into()], "3d_open");
                    }
                    "3d_löschen" | "3d_clear" | "raum_löschen" | "space_clear" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_clear, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "3d_clr")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_aktualisieren" | "3d_update" | "raum_aktualisieren" | "space_update" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_update, &[win.into()], "3d_upd")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_schliessen" | "3d_close" | "raum_schliessen" | "space_close" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_close, &[win.into()], "3d_cls")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_perspektive" | "3d_perspective" | "raum_perspektive" | "space_perspective" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_perspective, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "3d_persp")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_kamera" | "3d_camera" | "raum_kamera" | "space_camera" | "rk" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_camera, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "3d_cam")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_rotiere" | "3d_rotate" | "raum_rotiere" | "space_rotate" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_rotate, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "3d_rot")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-5: (fenster, s) uniform oder (fenster, sx, sy, sz)
                    "raum_skaliere" | "space_scale" | "3d_skaliere" if args.len() == 2 || args.len() == 4 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        let (sx, sy, sz) = if a.len() == 2 { (a[1], a[1], a[1]) } else { (a[1], a[2], a[3]) };
                        self.call_rt_void(self.rt.moo_3d_scale, &[a[0].into(), sx.into(), sy.into(), sz.into()], "3d_skal")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-7: (fenster, idx, x, y, z, farbe) — idx 0 Hauptlicht, 1..3 Zusatzlichter
                    "raum_licht_hinzufuegen" | "raum_licht_hinzufügen" | "space_add_light" if args.len() == 6 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_light_add, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "3d_licht_add")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-7: (fenster, idx, x, y, z, radius, farbe) — radius 0 = aus
                    "raum_punktlicht" | "space_point_light" if args.len() == 7 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_point_light, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "3d_punktlicht")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-8: (fenster, staerke) — Eigenleuchten, 0 = aus
                    "raum_leuchten" | "space_emissive" if args.len() == 2 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_emissive, &[a[0].into(), a[1].into()], "3d_leuchten")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-8: (fenster, "additiv" | "normal")
                    "raum_mischmodus" | "space_blend_mode" if args.len() == 2 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_blend_mode, &[a[0].into(), a[1].into()], "3d_mischmodus")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-10: (fenster, x1, y1, z1, x2, y2, z2, farbe) — unbeleuchtet
                    "raum_linie" | "space_line" if args.len() == 8 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_line, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into()], "3d_linie")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-10: (fenster, x, y, z, groesse, farbe) — groesse in Pixeln
                    "raum_punkt" | "space_point" if args.len() == 6 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_point, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "3d_punkt")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Licht-Stufe 3: (fenster, an) — Smooth-Normalen beim chunk_ende
                    "raum_glaetten" | "raum_glätten" | "space_smooth" if args.len() == 2 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_smooth, &[a[0].into(), a[1].into()], "3d_glaetten")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-15: (fenster, pfad) -> Textur-ID oder nichts
                    "raum_textur_laden" | "space_load_texture" if args.len() == 2 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_3d_texture_load, &[a[0].into(), a[1].into()], "3d_textur_laden");
                    }
                    // ERDE-15: (fenster, flaeche) -> Textur-ID oder nichts
                    "raum_textur_aus_flaeche" | "raum_textur_aus_fläche" | "space_texture_from_surface" if args.len() == 2 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_3d_texture_from_surface, &[a[0].into(), a[1].into()], "3d_textur_flaeche");
                    }
                    // ERDE-15: (fenster, id[, skala]) — id 0 = aus, skala Default 1
                    "raum_textur" | "space_texture" if args.len() == 2 || args.len() == 3 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        let skala = if a.len() == 3 { a[2] } else { self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(1.0).into()], "tex_skala1")? };
                        self.call_rt_void(self.rt.moo_3d_texture, &[a[0].into(), a[1].into(), skala.into()], "3d_textur")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Licht-Stufe 4: (fenster, an[, aufloesung[, bereich]])
                    "raum_schatten" | "space_shadows" if args.len() >= 2 && args.len() <= 4 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        let null = self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(0.0).into()], "schatten_default")?;
                        let size = if a.len() >= 3 { a[2] } else { null };
                        let range = if a.len() >= 4 { a[3] } else { null };
                        self.call_rt_void(self.rt.moo_3d_shadow, &[a[0].into(), a[1].into(), size.into(), range.into()], "3d_schatten")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-16: (fenster, staerke[, schwelle]) — staerke 0 = aus, schwelle Default 0.8
                    // ERDE-17: optional radius = Blur-Durchgaenge 1..8 (Default 2)
                    "raum_bloom" | "raum_glühen" | "raum_gluehen" | "space_bloom" if args.len() >= 2 && args.len() <= 4 => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        let schwelle = if a.len() >= 3 { a[2] } else { self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(0.8).into()], "bloom_schwelle")? };
                        let radius = if a.len() == 4 { a[3] } else { self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(2.0).into()], "bloom_radius")? };
                        self.call_rt_void(self.rt.moo_3d_bloom, &[a[0].into(), a[1].into(), schwelle.into(), radius.into()], "3d_bloom")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // ERDE-5: Farbton pro Instanz (fenster, "#RRGGBB")
                    "raum_farbton" | "space_tint" | "3d_farbton" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_tint, &[a[0].into(), a[1].into()], "3d_farbton")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_verschiebe" | "3d_translate" | "raum_verschiebe" | "space_translate" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_translate, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "3d_tr")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_push" | "raum_push" | "space_push" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_push, &[win.into()], "3d_push")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_pop" | "raum_pop" | "space_pop" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_pop, &[win.into()], "3d_pop")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_dreieck" | "3d_triangle" | "raum_dreieck" | "space_triangle" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_triangle, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into(), a[8].into(), a[9].into(), a[10].into()], "3d_tri")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_würfel" | "3d_cube" | "raum_würfel" | "space_cube" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_cube, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "3d_cube")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "3d_kugel" | "3d_sphere" | "raum_kugel" | "space_sphere" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_sphere, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "3d_sph")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_taste" | "space_key" | "3d_taste" | "3d_key_pressed" => {
                        let win_arg = self.compile_expr(&args[0])?;
                        let key_arg = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_3d_key_pressed, &[win_arg.into(), key_arg.into()], "3d_key");
                    }
                    // Maus
                    "raum_maus_fangen" | "space_capture_mouse" | "3d_maus_fangen" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_capture_mouse, &[win.into()], "cap_mouse")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_maus_dx" | "space_mouse_dx" | "3d_maus_dx" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_mouse_dx, &[win.into()], "mouse_dx");
                    }
                    "raum_maus_dy" | "space_mouse_dy" | "3d_maus_dy" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_mouse_dy, &[win.into()], "mouse_dy");
                    }
                    "raum_maus_freigeben" | "space_release_mouse" | "3d_maus_freigeben" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_release_mouse, &[win.into()], "rel_mouse")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_maus_x" | "space_mouse_x" | "3d_maus_x" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_mouse_x, &[win.into()], "mouse_x");
                    }
                    "raum_maus_y" | "space_mouse_y" | "3d_maus_y" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_mouse_y, &[win.into()], "mouse_y");
                    }
                    "raum_maus_taste" | "space_mouse_button" | "3d_maus_taste" => {
                        let win = self.compile_expr(&args[0])?;
                        let btn = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_3d_mouse_button, &[win.into(), btn.into()], "mouse_btn");
                    }
                    "raum_maus_rad" | "space_mouse_wheel" | "3d_maus_rad" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_mouse_wheel, &[win.into()], "mouse_wheel");
                    }
                    "raum_sim_maus_pos" | "space_sim_mouse_pos" | "3d_sim_maus_pos" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_simulate_mouse_pos, &[a[0].into(), a[1].into(), a[2].into()], "sim_mpos")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_sim_maus_taste" | "space_sim_mouse_button" | "3d_sim_maus_taste" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_simulate_mouse_button, &[a[0].into(), a[1].into(), a[2].into()], "sim_mbtn")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_sim_rad" | "space_sim_scroll" | "3d_sim_rad" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_simulate_scroll, &[a[0].into(), a[1].into()], "sim_scroll")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // raum_sim_* = direkte 3D-Route (Aliases erhalten, keine Breaking Changes).
                    // Die backend-agnostischen test_sim_* sind weiter unten (tag-dispatch, A2).
                    // Tastatur-Sim (Tri-State, Plan-008 A1a). Args: (fenster, taste, gedrueckt)
                    "raum_sim_taste" | "space_sim_key" | "3d_sim_taste" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_simulate_key, &[a[0].into(), a[1].into(), a[2].into()], "sim_key")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Maus-Delta-Sim (consume-on-read, Plan-008 A1b). Args: (fenster, dx, dy)
                    "raum_sim_maus_delta" | "space_sim_mouse_delta" | "3d_sim_maus_delta" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_simulate_mouse_delta, &[a[0].into(), a[1].into(), a[2].into()], "sim_mdelta")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Sim-Reset (direkte 3D-Route). Args: (fenster)
                    "raum_sim_reset" | "space_sim_reset" | "3d_sim_reset" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_simulate_reset, &[win.into()], "sim_reset")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_screenshot_bmp" | "raum_screenshot" | "3d_screenshot_bmp" | "space_screenshot" => {
                        let win = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_3d_screenshot_bmp, &[win.into(), path.into()], "shot3d");
                    }
                    // Fog + Licht (DE + EN). Args: (fenster, ...)
                    "raum_licht" | "space_light" | "3d_licht" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_light, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "3d_licht")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_umgebungslicht" | "space_ambient" | "3d_umgebungslicht" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_ambient, &[a[0].into(), a[1].into()], "3d_ambient")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_hoehennebel" | "space_height_fog" => {
                        if args.len() != 4 { return Err("Hoehennebel erwartet 4 Argumente".into()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_height_fog, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "height_fog")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_hoehennebel_aus" | "space_height_fog_off" => {
                        if args.len() != 1 { return Err("Hoehennebel aus erwartet 1 Argument".into()); }
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_height_fog_off, &[win.into()], "height_fog_off")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_nebel" | "space_fog" | "3d_nebel" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_fog, &[a[0].into(), a[1].into()], "3d_nebel")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_nebel_farbe" | "space_fog_color" | "3d_nebel_farbe" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_fog_color, &[a[0].into(), a[1].into()], "3d_nebelfarbe")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_himmel" | "space_sky" | "3d_himmel" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_sky, &[a[0].into(), a[1].into(), a[2].into()], "3d_himmel")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Globale Transparenz: (fenster, alpha 0..1) — gilt fuer folgende Draws
                    "raum_transparenz" | "space_alpha" | "3d_transparenz" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_alpha, &[a[0].into(), a[1].into()], "3d_alpha")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_hdr" | "space_hdr" => {
                        if args.len() != 2 { return Err("raum_hdr: falsche Argumentzahl".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_hdr, &[a[0].into(), a[1].into()], "3d_hdr")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_ao" | "space_ao" => {
                        if args.len() != 4 { return Err("raum_ao: erwartet 4 Argumente".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_ao, &[a[0].into(),a[1].into(),a[2].into(),a[3].into()], "3d_ao")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_ao_aus" | "space_ao_off" => {
                        if args.len() != 1 { return Err("raum_ao_aus: erwartet 1 Argumente".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_ao_off, &[a[0].into()], "3d_ao_off")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_ao_debug" | "space_ao_debug" => {
                        if args.len() != 2 { return Err("raum_ao_debug: erwartet 2 Argumente".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_ao_debug, &[a[0].into(),a[1].into()], "3d_ao_debug")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_opak_ende" | "space_opaque_end" => {
                        if args.len() != 1 { return Err("raum_opak_ende: erwartet 1 Argumente".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_opaque_end, &[a[0].into()], "3d_opaque_end")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_pbr" | "space_pbr" => {
                        if args.len() != 3 { return Err("raum_pbr: erwartet win,metallic,roughness".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_pbr, &[a[0].into(),a[1].into(),a[2].into()], "3d_pbr")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_pbr_aus" | "space_pbr_off" => {
                        if args.len() != 1 { return Err("raum_pbr_aus: erwartet win".to_string()); }
                        let a=self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_pbr_off, &[a.into()], "3d_pbr_off")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_licht_linear" | "space_light_linear" => {
                        if args.len() != 5 { return Err("raum_licht_linear: erwartet win,slot,r,g,b".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_linear_light, &[a[0].into(),a[1].into(),a[2].into(),a[3].into(),a[4].into()], "3d_linear_light")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_auto_belichtung" | "space_auto_exposure" => {
                        if args.len() != 5 { return Err("raum_auto_belichtung: erwartet win,min,max,key,tau".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_auto_exposure, &[a[0].into(),a[1].into(),a[2].into(),a[3].into(),a[4].into()], "3d_auto_exposure")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_belichtung_wert" | "space_exposure_value" => {
                        if args.len() != 1 { return Err("raum_belichtung_wert: erwartet win".to_string()); }
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_exposure_value, &[a.into()], "3d_exposure_value");
                    }
                    "raum_belichtung" | "space_exposure" => {
                        if args.len() != 2 { return Err("raum_belichtung: falsche Argumentzahl".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_exposure, &[a[0].into(), a[1].into()], "3d_exposure")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_szene_ende" | "space_scene_end" => {
                        if args.len() != 1 { return Err("raum_szene_ende: falsche Argumentzahl".to_string()); }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_scene_end, &[a[0].into()], "3d_scene_end")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Explizite Renderzeit: additive GL33-/Hybrid-Replay-API.
                    "raum_renderzeit" | "space_render_time" => {
                        if args.len() != 2 {
                            return Err("raum_renderzeit erwartet (fenster, sekunden)".to_string());
                        }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_render_time, &[a[0].into(), a[1].into()], "3d_render_time")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "raum_renderzeit_live" | "space_render_time_live" => {
                        if args.len() != 1 {
                            return Err("raum_renderzeit_live erwartet (fenster)".to_string());
                        }
                        let a = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_render_time_live, &[a.into()], "3d_render_time_live")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Wasserwellen: (fenster, amplitude, frequenz, tempo) — amp 0 = aus
                    "raum_wellen" | "space_waves" | "3d_wellen" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_wave, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "3d_wellen")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Lichtfarbe: (fenster, "#RRGGBB")
                    "raum_lichtfarbe" | "space_light_color" | "3d_lichtfarbe" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_light_color, &[a[0].into(), a[1].into()], "3d_lichtfarbe")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Specular-Glanz: (fenster, staerke, haerte) — staerke 0 = aus
                    "raum_glanz" | "space_specular" | "3d_glanz" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_spec, &[a[0].into(), a[1].into(), a[2].into()], "3d_glanz")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Clear mit Hex-Farbe: (fenster, "#RRGGBB")
                    "raum_löschen_farbe" | "raum_loeschen_farbe" | "space_clear_color" | "3d_löschen_farbe" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_clear_hex, &[a[0].into(), a[1].into()], "3d_clear_hex")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Tageszeit-Atmosphaere: (fenster, t 0..1) -> Himmelsfarbe "#RRGGBB"
                    "raum_tageszeit" | "space_time_of_day" | "3d_tageszeit" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_3d_time_of_day, &[a[0].into(), a[1].into()], "3d_tageszeit");
                    }
                    // Gouraud-Dreieck: (fenster, x1,y1,z1, x2,y2,z2, x3,y3,z3, farbe1, farbe2, farbe3)
                    "raum_dreieck_farben" | "space_triangle_colors" | "3d_dreieck_farben" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_3d_triangle_colors, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into(), a[8].into(), a[9].into(), a[10].into(), a[11].into(), a[12].into()], "3d_tri_farben")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }

                    // ============================================================
                    // Einheitlicher Test-API-Layer (Plan-008 A2, tag-dispatch
                    // 2D/3D/Hybrid). Erstes Argument ist immer das Fenster.
                    // ============================================================
                    "test_sim_taste" | "test_simulate_key" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_test_sim_taste, &[a[0].into(), a[1].into(), a[2].into()], "t_sim_key")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "test_sim_maus_pos" | "test_sim_mouse_pos" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_test_sim_maus_pos, &[a[0].into(), a[1].into(), a[2].into()], "t_sim_mpos")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "test_sim_maus_taste" | "test_sim_mouse_button" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_test_sim_maus_taste, &[a[0].into(), a[1].into(), a[2].into()], "t_sim_mbtn")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "test_sim_maus_rad" | "test_sim_mouse_wheel" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_test_sim_maus_rad, &[a[0].into(), a[1].into()], "t_sim_rad")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "test_sim_maus_delta" | "test_sim_mouse_delta" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_test_sim_maus_delta, &[a[0].into(), a[1].into(), a[2].into()], "t_sim_mdelta")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "test_sim_reset" | "test_simulate_reset" => {
                        let win = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_test_sim_reset, &[win.into()], "t_sim_reset")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "test_screenshot" | "test_bildschirmfoto" => {
                        let win = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_test_screenshot, &[win.into(), path.into()], "t_shot");
                    }
                    "test_renderer_info" => {
                        if args.len() != 1 {
                            return Err("test_renderer_info erwartet genau 1 Argument".to_string());
                        }
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_test_renderer_info, &[win.into()], "t_renderer_info");
                    }
                    "test_fenster_info" | "test_window_info" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_test_fenster_info, &[win.into()], "t_win_info");
                    }
                    // Frame-Grab / Pixel (Plan-008 A3A, opaker MOO_FRAME-Heap-Typ).
                    // test_frame_grab(win) -> MOO_FRAME (RGBA8, top-left origin).
                    "test_frame_grab" | "test_frame_packen" => {
                        let win = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_test_frame_grab, &[win.into()], "t_frame_grab");
                    }
                    // test_pixel(frame_oder_win, x, y) -> Dict {rot,gruen,blau,alpha}.
                    "test_pixel" | "test_farbe_an" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_test_pixel, &[a[0].into(), a[1].into(), a[2].into()], "t_pixel");
                    }
                    // test_frame_save_bmp(frame, pfad) -> Bool.
                    "test_frame_save_bmp" | "test_frame_speichern_bmp" => {
                        let frame = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_test_frame_save_bmp, &[frame.into(), path.into()], "t_frame_bmp");
                    }
                    // test_frame_diff(frame_a, frame_b) -> Dict {maxdiff, geaenderte_pixel, prozent}.
                    "test_frame_diff" | "test_frame_vergleich" => {
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_test_frame_diff, &[a0.into(), a1.into()], "t_frame_diff");
                    }
                    "raum_wasser_material" | "water_material" => {
                        if args.len() != 2 { return Err("water_material erwartet 2 Argumente".to_string()); }
                        let a0 = self.compile_expr(&args[0])?;
                        let a1 = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_3d_water_material, &[a0.into(), a1.into()], "water_material");
                    }
                    "raum_wasser_hintergrund" | "water_background" => {
                        if args.len() != 1 { return Err("water_background erwartet 1 Argument".to_string()); }
                        let a0 = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_water_background, &[a0.into()], "water_background");
                    }
                    "raum_wasser_aus" | "water_off" => {
                        if args.len() != 1 { return Err("water_off erwartet 1 Argument".to_string()); }
                        let a0 = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_3d_water_off, &[a0.into()], "water_off");
                    }
                    // Generic RGBA8 ROI statistics; validate arity before indexing.
                    "test_frame_statistik" | "test_frame_statistics" => {
                        if args.len() != 5 {
                            return Err("test_frame_statistik erwartet 5 Argumente".to_string());
                        }
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_test_frame_statistics, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "t_frame_statistics");
                    }
                    // test_frame_region(frame, x, y, b, h) -> Dict {rot, gruen, blau, alpha} (Durchschnittsfarbe).
                    "test_frame_region" | "test_frame_bereich" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_test_frame_region, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "t_frame_region");
                    }
                    // test_frame_save_png(frame, pfad) -> Bool (SDL2_image).
                    "test_frame_save_png" | "test_frame_speichern_png" => {
                        let frame = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_test_frame_save_png, &[frame.into(), path.into()], "t_frame_png");
                    }
                    // tensor_aus_frame(frame, modus?) -> Tensor [h,b,4] float 0..1 (KI-MULTI-V1).
                    "tensor_aus_frame" | "tensor_from_frame" => {
                        let frame = self.compile_expr(&args[0])?;
                        let modus = if args.len() > 1 {
                            self.compile_expr(&args[1])?
                        } else {
                            self.call_rt(self.rt.moo_none, &[], "taf_none")?
                        };
                        return self.call_rt(self.rt.moo_tensor_aus_frame, &[frame.into(), modus.into()], "t_aus_frame");
                    }
                    // frame_aus_tensor(t) -> MOO_FRAME (Clamp 0..1 -> RGBA8).
                    "frame_aus_tensor" | "frame_from_tensor" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_frame_aus_tensor, &[t.into()], "f_aus_tensor");
                    }
                    // Toolkit-freie RGBA8888-Surface (P016-O1).
                    // Der C-Vertrag borgt alle MooValue-Argumente; der gemeinsame
                    // Helper released deshalb jedes von compile_expr erzeugte Temp.
                    "surface_new" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_new,
                            args,
                            2,
                            name,
                            "surface_new",
                        );
                    }
                    "surface_clear" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_clear,
                            args,
                            5,
                            name,
                            "surface_clear",
                        );
                    }
                    "surface_clip_push" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_clip_push,
                            args,
                            5,
                            name,
                            "surface_clip_push",
                        );
                    }
                    "surface_clip_pop" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_clip_pop,
                            args,
                            1,
                            name,
                            "surface_clip_pop",
                        );
                    }
                    "surface_rect" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_rect,
                            args,
                            9,
                            name,
                            "surface_rect",
                        );
                    }
                    "surface_roundrect" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_roundrect,
                            args,
                            10,
                            name,
                            "surface_roundrect",
                        );
                    }
                    "surface_circle" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_circle,
                            args,
                            8,
                            name,
                            "surface_circle",
                        );
                    }
                    "surface_line" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_line,
                            args,
                            9,
                            name,
                            "surface_line",
                        );
                    }
                    "surface_read_pixel" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_read_pixel,
                            args,
                            3,
                            name,
                            "surface_read_pixel",
                        );
                    }
                    "surface_roundrect_stapel" => {
                        let z = self.compile_expr(&args[0])?;
                        let st = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_surface_roundrect_stapel, &[z.into(), st.into()], "surface_roundrect_stapel");
                    }
                    "surface_glass_farbpass" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_glass_farbpass,
                            args,
                            6,
                            name,
                            "surface_glass_farbpass",
                        );
                    }
                    "surface_blit_frame" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_blit_frame,
                            args,
                            4,
                            name,
                            "surface_blit_frame",
                        );
                    }
                    "frame_lade_bmp" | "frame_load_bmp" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_frame_lade_bmp,
                            args,
                            1,
                            name,
                            "frame_lade_bmp",
                        );
                    }
                    "surface_text" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_text,
                            args,
                            9,
                            name,
                            "surface_text",
                        );
                    }
                    "surface_text_stil" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_text_stil,
                            args,
                            10,
                            name,
                            "surface_text_stil",
                        );
                    }
                    "surface_text_breite" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_text_breite,
                            args,
                            2,
                            name,
                            "surface_text_breite",
                        );
                    }
                    "surface_text_breite_stil" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_text_breite_stil,
                            args,
                            3,
                            name,
                            "surface_text_breite_stil",
                        );
                    }
                    // P7-T2: Glyphenhoehe (Font-Hoehe * Skala) fuer Layouts ohne Festwerte.
                    "surface_text_hoehe" | "surface_text_höhe" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_text_hoehe,
                            args,
                            1,
                            name,
                            "surface_text_hoehe",
                        );
                    }
                    "surface_text_glyphen_status" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_text_glyphen_status,
                            args,
                            2,
                            name,
                            "surface_text_glyphen_status",
                        );
                    }
                    "surface_hash" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_hash,
                            args,
                            1,
                            name,
                            "surface_hash",
                        );
                    }
                    "surface_snapshot_to_frame" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_surface_snapshot_to_frame,
                            args,
                            1,
                            name,
                            "surface_snapshot_to_frame",
                        );
                    }
                    // Audio-Feature-Extraktion (KI-MULTI-A1, kein Autograd).
                    // fft/spektrum(t) -> [bins,2] mit Real-/Imaginaerteil.
                    "fft" | "spektrum" | "spectrum" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_fft, &[t.into()], "audio_fft");
                    }
                    // spektrum_betrag(t) -> [bins].
                    "spektrum_betrag" | "magnitude_spectrum" | "spectrum_magnitude" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_spektrum_betrag, &[t.into()], "audio_mag");
                    }
                    // spektrogramm(t, fenster, schritt) -> Hann-|STFT| [frames,bins].
                    "spektrogramm" | "spectrogram" => {
                        let t = self.compile_expr(&args[0])?;
                        let fenster = self.compile_expr(&args[1])?;
                        let schritt = self.compile_expr(&args[2])?;
                        return self.call_rt(
                            self.rt.moo_spektrogramm,
                            &[t.into(), fenster.into(), schritt.into()],
                            "audio_stft",
                        );
                    }
                    // wav_lesen(pfad) -> {daten: Tensor[n], rate: Zahl}.
                    "wav_lesen" | "wav_read" => {
                        let pfad = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_wav_lesen, &[pfad.into()], "audio_wav");
                    }
                    // Realtime-Capture (KI-MULTI-C1). Runtime-Argumente sind borrowed;
                    // alle kompilierten Heap-Temps werden nach dem Call released.
                    "kamera_liste" | "camera_list" => {
                        if !args.is_empty() { return Err(format!("{name} erwartet keine Argumente")); }
                        return self.call_rt(self.rt.moo_kamera_liste, &[], "camera_list");
                    }
                    "kamera_oeffnen" | "camera_open" => {
                        if args.len() > 4 { return Err(format!("{name} erwartet hoechstens 4 Argumente")); }
                        let mut values = Vec::with_capacity(4);
                        for i in 0..4 {
                            values.push(if i < args.len() { self.compile_expr(&args[i])? }
                                else { self.call_rt(self.rt.moo_none, &[], "camera_none")? });
                        }
                        let result = self.call_rt(self.rt.moo_kamera_oeffnen,
                            &[values[0].into(), values[1].into(), values[2].into(), values[3].into()], "camera_open")?;
                        for value in values { self.call_rt_void(self.rt.moo_release, &[value.into()], "camera_arg_release")?; }
                        return Ok(result);
                    }
                    "kamera_frame" | "camera_frame" => {
                        if args.is_empty() || args.len() > 2 { return Err(format!("{name} erwartet 1 oder 2 Argumente")); }
                        let camera = self.compile_expr(&args[0])?;
                        let timeout = if args.len() > 1 { self.compile_expr(&args[1])? }
                            else { self.call_rt(self.rt.moo_none, &[], "camera_timeout_none")? };
                        let result = self.call_rt(self.rt.moo_kamera_frame,
                            &[camera.into(), timeout.into()], "camera_frame")?;
                        self.call_rt_void(self.rt.moo_release, &[camera.into()], "camera_release")?;
                        self.call_rt_void(self.rt.moo_release, &[timeout.into()], "camera_timeout_release")?;
                        return Ok(result);
                    }
                    "kamera_schliessen" | "camera_close" => {
                        if args.len() != 1 { return Err(format!("{name} erwartet genau 1 Argument")); }
                        let camera = self.compile_expr(&args[0])?;
                        let result = self.call_rt(self.rt.moo_kamera_schliessen, &[camera.into()], "camera_close")?;
                        self.call_rt_void(self.rt.moo_release, &[camera.into()], "camera_close_release")?;
                        return Ok(result);
                    }
                    "mikro_oeffnen" | "microphone_open" => {
                        if args.len() > 3 { return Err(format!("{name} erwartet hoechstens 3 Argumente")); }
                        let mut values = Vec::with_capacity(3);
                        for i in 0..3 {
                            values.push(if i < args.len() { self.compile_expr(&args[i])? }
                                else { self.call_rt(self.rt.moo_none, &[], "microphone_none")? });
                        }
                        let result = self.call_rt(self.rt.moo_mikro_oeffnen,
                            &[values[0].into(), values[1].into(), values[2].into()], "microphone_open")?;
                        for value in values { self.call_rt_void(self.rt.moo_release, &[value.into()], "microphone_arg_release")?; }
                        return Ok(result);
                    }
                    "mikro_lesen" | "microphone_read" => {
                        if args.len() < 2 || args.len() > 3 { return Err(format!("{name} erwartet 2 oder 3 Argumente")); }
                        let microphone = self.compile_expr(&args[0])?;
                        let samples = self.compile_expr(&args[1])?;
                        let timeout = if args.len() > 2 { self.compile_expr(&args[2])? }
                            else { self.call_rt(self.rt.moo_none, &[], "microphone_timeout_none")? };
                        let result = self.call_rt(self.rt.moo_mikro_lesen,
                            &[microphone.into(), samples.into(), timeout.into()], "microphone_read")?;
                        for value in [microphone, samples, timeout] {
                            self.call_rt_void(self.rt.moo_release, &[value.into()], "microphone_read_release")?;
                        }
                        return Ok(result);
                    }
                    "mikro_schliessen" | "microphone_close" => {
                        if args.len() != 1 { return Err(format!("{name} erwartet genau 1 Argument")); }
                        let microphone = self.compile_expr(&args[0])?;
                        let result = self.call_rt(self.rt.moo_mikro_schliessen, &[microphone.into()], "microphone_close")?;
                        self.call_rt_void(self.rt.moo_release, &[microphone.into()], "microphone_close_release")?;
                        return Ok(result);
                    }
                    // GIF-Recorder (Plan-008 A3B). Frame-bounded: streamt direkt
                    // in die Datei, sammelt KEINE Frame-Sequenz im RAM.
                    // test_gif_start(win_oder_frame, pfad, fps) -> MOO_GIF.
                    "test_gif_start" | "test_gif_aufnahme_start" => {
                        let win = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        let fps = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_test_gif_start, &[win.into(), path.into(), fps.into()], "t_gif_start");
                    }
                    // test_gif_frame(gif, frame_oder_win) -> Bool.
                    "test_gif_frame" | "test_gif_bild" => {
                        let gif = self.compile_expr(&args[0])?;
                        let frame = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_test_gif_frame, &[gif.into(), frame.into()], "t_gif_frame");
                    }
                    // test_gif_ende(gif) -> Bool.
                    "test_gif_ende" | "test_gif_end" => {
                        let gif = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_test_gif_ende, &[gif.into()], "t_gif_ende");
                    }
                    // MP4-Video-Recorder (Plan-009 V1). Frame-bounded: piped
                    // jeden Frame direkt nach ffmpeg-stdin, sammelt KEINE
                    // Frame-Sequenz im RAM.
                    // test_video_start(win_oder_frame, pfad, fps) -> MOO_VIDEO.
                    "test_video_start" | "test_video_aufnahme_start" => {
                        let win = self.compile_expr(&args[0])?;
                        let path = self.compile_expr(&args[1])?;
                        let fps = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_test_video_start, &[win.into(), path.into(), fps.into()], "t_video_start");
                    }
                    // test_video_frame(video, frame_oder_win) -> Bool.
                    "test_video_frame" | "test_video_bild" => {
                        let video = self.compile_expr(&args[0])?;
                        let frame = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_test_video_frame, &[video.into(), frame.into()], "t_video_frame");
                    }
                    // test_video_ende(video) -> Bool.
                    "test_video_ende" | "test_video_end" => {
                        let video = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_test_video_ende, &[video.into()], "t_video_ende");
                    }
                    // Voxel-Welt (Plan-005 Phase 1a Kern-Builtins).
                    // Nur die nach RT1 stabilen 4 Builtins; mesh_bauen/strahl folgen
                    // erst nach RT2/RT4 (keine Aliases fuer unfertige Semantik).
                    "voxel_welt_neu" | "voxel_world_new" => {
                        let seed = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_voxel_welt_neu, &[seed.into()], "voxel_welt_neu");
                    }
                    // Worldgen (Plan-005 RT5): generieren(welt, cx, cz) -> Anzahl neuer Chunks.
                    // Heightmap seed-deterministisch via moo_noise_fbm; voxel_holen generiert
                    // unberuehrte Chunks zusaetzlich lazy beim ersten Lesen.
                    "voxel_generieren" | "voxel_generate" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_voxel_generieren,
                            &[a[0].into(), a[1].into(), a[2].into()], "voxel_generieren");
                    }
                    "voxel_setzen" | "block_setzen" | "set_block" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_voxel_setzen,
                            &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "voxel_setzen");
                    }
                    "voxel_holen" | "block_holen" | "get_block" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_voxel_holen,
                            &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "voxel_holen");
                    }
                    "voxel_ram_statistik" | "voxel_ram" | "voxel_stats" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_voxel_ram_statistik, &[w.into()], "voxel_ram");
                    }
                    // Voxel-Optimize (Plan-006 R3): expliziter Section-Downgrade-Lauf
                    // (PALETTE->SOLID/EMPTY + Palette-Kompaktierung). Main-thread-only.
                    // welt_optimieren(welt) -> Number (Anzahl geaenderter Chunks).
                    "voxel_welt_optimieren" | "voxel_optimieren" | "voxel_optimize" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_voxel_welt_optimieren, &[w.into()], "voxel_welt_optimieren");
                    }
                    // Voxel-Mesher (Plan-005 Phase 1b, CG2 nach RT2).
                    // mesh_bauen(welt, cx, cy, cz) -> Render-Chunk-ID (-1 = kein Cache);
                    // aktualisieren(welt) -> Anzahl neu gemeshter dirty Chunks.
                    // KEINE Raycast-Aliases hier (CG3-Scope nach RT4).
                    "voxel_mesh_bauen" | "build_chunk_mesh" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_voxel_mesh_bauen,
                            &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "voxel_mesh_bauen");
                    }
                    "voxel_aktualisieren" | "voxel_update" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_voxel_aktualisieren, &[w.into()], "voxel_aktualisieren");
                    }
                    // Voxel-Raycast/AABB (Plan-005 Phase 1d, CG3 nach RT4).
                    // strahl(welt, ox,oy,oz, dx,dy,dz, max_dist) -> Dict {hit,x,y,z,nx,ny,nz,id,dist};
                    // aabb(welt, minx,miny,minz, maxx,maxy,maxz) -> Dict {hit,count,x,y,z}.
                    // Rueckgabe-Dicts gemaess P0.5-Contract (nicht aendern).
                    "voxel_strahl" | "voxel_raycast" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_voxel_strahl,
                            &[a[0].into(), a[1].into(), a[2].into(), a[3].into(),
                              a[4].into(), a[5].into(), a[6].into(), a[7].into()], "voxel_strahl");
                    }
                    "voxel_aabb" | "voxel_overlap" => {
                        let a: Vec<_> = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_voxel_aabb,
                            &[a[0].into(), a[1].into(), a[2].into(),
                              a[3].into(), a[4].into(), a[5].into(), a[6].into()], "voxel_aabb");
                    }
                    // Chunk Display Lists
                    "chunk_erstelle" | "chunk_create" => {
                        return self.call_rt(self.rt.moo_3d_chunk_create, &[], "chunk_create");
                    }
                    "chunk_beginne" | "chunk_begin" => {
                        let id = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_chunk_begin, &[id.into()], "chunk_begin")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "chunk_ende" | "chunk_end" => {
                        self.call_rt_void(self.rt.moo_3d_chunk_end, &[], "chunk_end")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "chunk_zeichne" | "chunk_draw" => {
                        let id = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_chunk_draw, &[id.into()], "chunk_draw")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "chunk_lösche" | "chunk_delete" => {
                        let id = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_3d_chunk_delete, &[id.into()], "chunk_delete")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Welt (Game-Dev Runtime) — DE + EN
                    "__welt_erstelle" | "__world_create" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_world_create, &[a[0].into(), a[1].into(), a[2].into()], "world");
                    }
                    "__welt_offen" | "__world_is_open" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_world_is_open, &[w.into()], "world_open");
                    }
                    "__welt_aktualisieren" | "__world_update" => {
                        let w = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_world_update, &[w.into()], "world_upd")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_beenden" | "__world_close" => {
                        let w = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_world_close, &[w.into()], "world_cls")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_seed" | "__world_seed" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_seed, &[a[0].into(), a[1].into()], "world_seed")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_biom" | "__world_biome" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_biome, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "world_biome")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_baeume" | "__world_trees" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_trees, &[a[0].into(), a[1].into(), a[2].into()], "world_trees")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_sonne" | "__world_sun" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_sun, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "world_sun")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_nebel" | "__world_fog" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_fog, &[a[0].into(), a[1].into()], "world_fog")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_meeresspiegel" | "__world_sea_level" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_sea_level, &[a[0].into(), a[1].into()], "world_sea")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_render_distanz" | "__world_render_dist" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_render_dist, &[a[0].into(), a[1].into()], "world_rd")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_tageszeit" | "__world_time_of_day" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_world_time_of_day, &[a[0].into(), a[1].into()], "world_tod")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "__welt_hoehe_bei" | "__world_height_at" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_world_height_at, &[a[0].into(), a[1].into(), a[2].into()], "world_h");
                    }
                    // Test-API
                    "screenshot" | "bildschirmfoto" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_screenshot, &[a[0].into(), a[1].into()], "screenshot");
                    }
                    "taste_simulieren" | "simulate_key" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_simulate_key, &[a[0].into(), a[1].into()], "sim_key")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "maus_simulieren" | "simulate_mouse" => {
                        let a = args.iter().map(|a| self.compile_expr(a)).collect::<Result<Vec<_>, _>>()?;
                        self.call_rt_void(self.rt.moo_simulate_mouse, &[a[0].into(), a[1].into(), a[2].into()], "sim_mouse")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Regex (POSIX)
                    "regex" | "muster" => {
                        let pat = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_regex_new, &[pat.into()], "regex");
                    }
                    "passt" | "matches" => {
                        let text = self.compile_expr(&args[0])?;
                        let rx = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_regex_match, &[text.into(), rx.into()], "match");
                    }
                    "finde" | "find" => {
                        let text = self.compile_expr(&args[0])?;
                        let rx = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_regex_find, &[text.into(), rx.into()], "find");
                    }
                    "finde_alle" | "find_all" => {
                        let text = self.compile_expr(&args[0])?;
                        let rx = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_regex_find_all, &[text.into(), rx.into()], "findall");
                    }
                    "ersetze" | "replace" => {
                        let text = self.compile_expr(&args[0])?;
                        let rx = self.compile_expr(&args[1])?;
                        let rep = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_regex_replace, &[text.into(), rx.into(), rep.into()], "replace");
                    }
                    // Test-Framework
                    "teste_alle" | "run_tests" => {
                        return self.compile_run_tests();
                    }
                    // Kern-Builtins
                    "schlafe" | "sleep" => {
                        let dur = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_sleep, &[dur.into()], "sleep")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "umgebung" | "env" => {
                        let name = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_env, &[name.into()], "env");
                    }
                    "beende" | "exit" => {
                        let code = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_exit, &[code.into()], "exit")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "zahl" | "num" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_to_number, &[arg.into()], "to_num");
                    }
                    "argumente" | "args" => {
                        return self.call_rt(self.rt.moo_args, &[], "args");
                    }
                    "prozess_id" | "pid" | "getpid" => {
                        return self.call_rt(self.rt.moo_pid, &[], "pid");
                    }
                    "tray_erstelle" | "tray_create" => {
                        let t = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_create, &[t.into(), i.into()], "tray_create");
                    }
                    "tray_menu_add" | "tray_menu_hinzufuegen" => {
                        let t = self.compile_expr(&args[0])?;
                        let l = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_tray_menu_add, &[t.into(), l.into(), c.into()], "tray_menu_add");
                    }
                    "tray_menu_clear" | "tray_menu_leeren" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tray_menu_clear, &[t.into()], "tray_menu_clear");
                    }
                    "tray_timer_add" | "tray_timer_hinzufuegen" => {
                        let ms = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_timer_add, &[ms.into(), c.into()], "tray_timer_add");
                    }
                    "tray_timer_remove" | "tray_timer_entfernen" => {
                        let id = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tray_timer_remove, &[id.into()], "tray_timer_remove");
                    }
                    "tray_titel_setze" | "tray_title_set" => {
                        let t = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_titel_setze, &[t.into(), v.into()], "tray_titel_setze");
                    }
                    "tray_icon_setze" | "tray_icon_set" => {
                        let t = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_icon_setze, &[t.into(), v.into()], "tray_icon_setze");
                    }
                    "tray_aktiv" | "tray_active" => {
                        let t = self.compile_expr(&args[0])?;
                        let a = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_aktiv, &[t.into(), a.into()], "tray_aktiv");
                    }
                    "tray_separator_add" | "tray_trenner_add" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tray_separator_add, &[t.into()], "tray_separator_add");
                    }
                    "tray_submenu_add" | "tray_untermenue_add" => {
                        let t = self.compile_expr(&args[0])?;
                        let l = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_submenu_add, &[t.into(), l.into()], "tray_submenu_add");
                    }
                    "tray_menu_add_to" | "tray_menu_hinzufuegen_zu" => {
                        let sm = self.compile_expr(&args[0])?;
                        let l = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_tray_menu_add_to, &[sm.into(), l.into(), c.into()], "tray_menu_add_to");
                    }
                    "tray_separator_add_to" | "tray_trenner_add_to" => {
                        let sm = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tray_separator_add_to, &[sm.into()], "tray_separator_add_to");
                    }
                    "tray_check_add" => {
                        let t = self.compile_expr(&args[0])?;
                        let l = self.compile_expr(&args[1])?;
                        let i = self.compile_expr(&args[2])?;
                        let c = self.compile_expr(&args[3])?;
                        return self.call_rt(self.rt.moo_tray_check_add, &[t.into(), l.into(), i.into(), c.into()], "tray_check_add");
                    }
                    "tray_check_add_to" => {
                        let sm = self.compile_expr(&args[0])?;
                        let l = self.compile_expr(&args[1])?;
                        let i = self.compile_expr(&args[2])?;
                        let c = self.compile_expr(&args[3])?;
                        return self.call_rt(self.rt.moo_tray_check_add_to, &[sm.into(), l.into(), i.into(), c.into()], "tray_check_add_to");
                    }
                    "tray_check_wert" | "tray_check_value" => {
                        let i = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tray_check_wert, &[i.into()], "tray_check_wert");
                    }
                    "tray_check_set" | "tray_check_setze" => {
                        let i = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_check_set, &[i.into(), v.into()], "tray_check_set");
                    }
                    "tray_item_aktiv" | "tray_item_active" => {
                        let i = self.compile_expr(&args[0])?;
                        let a = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_item_aktiv, &[i.into(), a.into()], "tray_item_aktiv");
                    }
                    "tray_item_label_setze" | "tray_item_label_set" => {
                        let i = self.compile_expr(&args[0])?;
                        let l = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tray_item_label_setze, &[i.into(), l.into()], "tray_item_label_setze");
                    }
                    // ============================================================
                    // UI (Cross-Platform Widgets — moo_ui.h)
                    // ============================================================
                    "ui_init" => {
                        return self.call_rt(self.rt.moo_ui_init, &[], "ui_init");
                    }
                    "ui_laufen" | "ui_run" => {
                        return self.call_rt(self.rt.moo_ui_laufen, &[], "ui_laufen");
                    }
                    "ui_beenden" | "ui_quit" => {
                        return self.call_rt(self.rt.moo_ui_beenden, &[], "ui_beenden");
                    }
                    "ui_pump" => {
                        return self.call_rt(self.rt.moo_ui_pump, &[], "ui_pump");
                    }
                    "ui_debug" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_debug, &[a.into()], "ui_debug");
                    }
                    "ui_fenster" | "ui_window" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_fenster, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_fenster");
                    }
                    "ui_fenster_titel_setze" | "ui_window_title_set" => {
                        let f = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_titel_setze, &[f.into(), t.into()], "ui_fenster_titel_setze");
                    }
                    "ui_fenster_icon_setze" | "ui_window_icon_set" => {
                        let f = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_icon_setze, &[f.into(), p.into()], "ui_fenster_icon_setze");
                    }
                    "ui_fenster_groesse_setze" | "ui_window_size_set" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_fenster_groesse_setze, &[a[0].into(), a[1].into(), a[2].into()], "ui_fenster_groesse_setze");
                    }
                    "ui_fenster_position_setze" | "ui_window_position_set" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_fenster_position_setze, &[a[0].into(), a[1].into(), a[2].into()], "ui_fenster_position_setze");
                    }
                    "ui_fenster_schliessen" | "ui_window_close" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_fenster_schliessen, &[f.into()], "ui_fenster_schliessen");
                    }
                    "ui_zeige" | "ui_show" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_zeige, &[f.into()], "ui_zeige");
                    }
                    "ui_zeige_nebenbei" | "ui_show_detached" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_zeige_nebenbei, &[f.into()], "ui_zeige_nebenbei");
                    }
                    "ui_fenster_on_close" | "ui_window_on_close" => {
                        let f = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_on_close, &[f.into(), c.into()], "ui_fenster_on_close");
                    }
                    "ui_fenster_on_resize" | "ui_window_on_resize" => {
                        let f = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_on_resize, &[f.into(), c.into()], "ui_fenster_on_resize");
                    }
                    "ui_label" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_label, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_label");
                    }
                    "ui_label_setze" | "ui_label_set" => {
                        let l = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_label_setze, &[l.into(), t.into()], "ui_label_setze");
                    }
                    "ui_label_text" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_label_text, &[l.into()], "ui_label_text");
                    }
                    "ui_knopf" | "ui_button" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_knopf, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "ui_knopf");
                    }
                    "ui_checkbox" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_checkbox, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into()], "ui_checkbox");
                    }
                    "ui_checkbox_wert" | "ui_checkbox_value" => {
                        let c = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_checkbox_wert, &[c.into()], "ui_checkbox_wert");
                    }
                    "ui_checkbox_setze" | "ui_checkbox_set" => {
                        let c = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_checkbox_setze, &[c.into(), v.into()], "ui_checkbox_setze");
                    }
                    "ui_radio" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_radio, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into()], "ui_radio");
                    }
                    "ui_radio_wert" | "ui_radio_value" => {
                        let r = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_radio_wert, &[r.into()], "ui_radio_wert");
                    }
                    "ui_eingabe" | "ui_input" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_eingabe, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "ui_eingabe");
                    }
                    "ui_eingabe_text" | "ui_input_text" => {
                        let e = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_eingabe_text, &[e.into()], "ui_eingabe_text");
                    }
                    "ui_eingabe_setze" | "ui_input_set" => {
                        let e = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_eingabe_setze, &[e.into(), t.into()], "ui_eingabe_setze");
                    }
                    "ui_eingabe_on_change" | "ui_input_on_change" => {
                        let e = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_eingabe_on_change, &[e.into(), c.into()], "ui_eingabe_on_change");
                    }
                    "ui_eingabe_on_enter" | "ui_input_on_enter" => {
                        let e = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_eingabe_on_enter, &[e.into(), c.into()], "ui_eingabe_on_enter");
                    }
                    "ui_textbereich" | "ui_textarea" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_textbereich, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_textbereich");
                    }
                    "ui_textbereich_text" | "ui_textarea_text" => {
                        let tb = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_textbereich_text, &[tb.into()], "ui_textbereich_text");
                    }
                    "ui_textbereich_setze" | "ui_textarea_set" => {
                        let tb = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_textbereich_setze, &[tb.into(), t.into()], "ui_textbereich_setze");
                    }
                    "ui_textbereich_anhaengen" | "ui_textarea_append" => {
                        let tb = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_textbereich_anhaengen, &[tb.into(), t.into()], "ui_textbereich_anhaengen");
                    }
                    "ui_textbereich_on_key" | "ui_textarea_on_key" => {
                        let tb = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_textbereich_on_key, &[tb.into(), c.into()], "ui_textbereich_on_key");
                    }
                    "ui_textbereich_zeile_anzahl" | "ui_textarea_line_count" => {
                        let tb = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_textbereich_zeile_anzahl, &[tb.into()], "ui_textbereich_zeile_anzahl");
                    }
                    "ui_dropdown" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_dropdown, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "ui_dropdown");
                    }
                    "ui_dropdown_auswahl" | "ui_dropdown_selection" => {
                        let d = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_dropdown_auswahl, &[d.into()], "ui_dropdown_auswahl");
                    }
                    "ui_dropdown_auswahl_setze" | "ui_dropdown_selection_set" => {
                        let d = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_dropdown_auswahl_setze, &[d.into(), i.into()], "ui_dropdown_auswahl_setze");
                    }
                    "ui_dropdown_text" => {
                        let d = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_dropdown_text, &[d.into()], "ui_dropdown_text");
                    }
                    "ui_liste" | "ui_list" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_liste, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_liste");
                    }
                    "ui_liste_zeile_hinzu" | "ui_list_row_add" => {
                        let l = self.compile_expr(&args[0])?;
                        let z = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_zeile_hinzu, &[l.into(), z.into()], "ui_liste_zeile_hinzu");
                    }
                    "ui_liste_zeilen_hinzu_bulk" | "ui_list_rows_add_bulk" | "ui_liste_bulk" => {
                        let l = self.compile_expr(&args[0])?;
                        let z = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_zeilen_hinzu_bulk, &[l.into(), z.into()], "ui_liste_zeilen_hinzu_bulk");
                    }
                    "ui_liste_auswahl" | "ui_list_selection" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_liste_auswahl, &[l.into()], "ui_liste_auswahl");
                    }
                    "ui_liste_zeile" | "ui_list_row" => {
                        let l = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_zeile, &[l.into(), i.into()], "ui_liste_zeile");
                    }
                    "ui_liste_leeren" | "ui_list_clear" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_liste_leeren, &[l.into()], "ui_liste_leeren");
                    }
                    "ui_liste_on_auswahl" | "ui_list_on_selection" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_on_auswahl, &[l.into(), c.into()], "ui_liste_on_auswahl");
                    }
                    "ui_liste_on_scroll" | "ui_list_on_scroll" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_on_scroll, &[l.into(), c.into()], "ui_liste_on_scroll");
                    }
                    "ui_liste_scroll_zu" | "ui_list_scroll_to" => {
                        let l = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_scroll_zu, &[l.into(), i.into()], "ui_liste_scroll_zu");
                    }
                    "ui_liste_scroll_unten" | "ui_list_scroll_bottom" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_liste_scroll_unten, &[l.into()], "ui_liste_scroll_unten");
                    }
                    "moo_ui_liste_spalte_breite" | "ui_liste_spalte_breite" | "liste_spalte_breite" => {
                        let l = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let b = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_liste_spalte_breite, &[l.into(), s.into(), b.into()], "ui_liste_spalte_breite");
                    }
                    "moo_ui_liste_spalte_min_breite" | "ui_liste_spalte_min_breite" | "liste_spalte_min_breite" => {
                        let l = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let b = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_liste_spalte_min_breite, &[l.into(), s.into(), b.into()], "ui_liste_spalte_min_breite");
                    }
                    "moo_ui_liste_spalten_autosize" | "ui_liste_spalten_autosize" | "liste_spalten_autosize" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_liste_spalten_autosize, &[l.into()], "ui_liste_spalten_autosize");
                    }
                    "moo_ui_liste_spalte_breite_lese" | "ui_liste_spalte_breite_lese" | "liste_spalte_breite_lese" => {
                        let l = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_spalte_breite_lese, &[l.into(), i.into()], "ui_liste_spalte_breite_lese");
                    }
                    "ui_liste_ist_unten" | "ui_list_is_at_bottom" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_liste_ist_unten, &[l.into()], "ui_liste_ist_unten");
                    }
                    "ui_liste_on_rechtsklick" | "ui_list_on_right_click" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_on_rechtsklick, &[l.into(), c.into()], "ui_liste_on_rechtsklick");
                    }
                    "ui_clipboard_setze" | "ui_clipboard_set" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_clipboard_setze, &[t.into()], "ui_clipboard_setze");
                    }
                    "ui_menue_eintrag_data" | "ui_menu_eintrag_data" | "ui_menu_item_data" => {
                        let e = self.compile_expr(&args[0])?;
                        let k = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_menue_eintrag_data, &[e.into(), k.into()], "ui_menue_eintrag_data");
                    }
                    "ui_menue_eintrag_lookup" | "ui_menu_eintrag_lookup" | "ui_menu_item_lookup" => {
                        let e = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_menue_eintrag_lookup, &[e.into()], "ui_menue_eintrag_lookup");
                    }
                    "ui_menue_eintrag_aktiv" | "ui_menu_eintrag_aktiv" | "ui_menu_item_active" => {
                        return self.call_rt(self.rt.moo_ui_menue_eintrag_aktiv, &[], "ui_menue_eintrag_aktiv");
                    }
                    "ui_zeit_jetzt" | "ui_time_now" => {
                        return self.call_rt(self.rt.moo_ui_zeit_jetzt, &[], "ui_zeit_jetzt");
                    }
                    "ui_zeit_lokal" | "ui_time_local" => {
                        let i = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_zeit_lokal, &[i.into()], "ui_zeit_lokal");
                    }
                    "ui_zeit_format" | "ui_time_format" => {
                        let i = self.compile_expr(&args[0])?;
                        let f = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_zeit_format, &[i.into(), f.into()], "ui_zeit_format");
                    }
                    "moo_ui_liste_sortierbar" | "ui_liste_sortierbar" | "liste_sortierbar" => {
                        let l = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let a = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_liste_sortierbar, &[l.into(), s.into(), a.into()], "ui_liste_sortierbar");
                    }
                    "moo_ui_liste_sortiere" | "ui_liste_sortiere" | "liste_sortiere" => {
                        let l = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let a = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_liste_sortiere, &[l.into(), s.into(), a.into()], "ui_liste_sortiere");
                    }
                    "moo_ui_liste_zeile_setze" | "ui_liste_zeile_setze" | "liste_zeile_setze" => {
                        let l = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        let z = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_liste_zeile_setze, &[l.into(), i.into(), z.into()], "ui_liste_zeile_setze");
                    }
                    "moo_ui_liste_zelle_setze" | "ui_liste_zelle_setze" | "liste_zelle_setze" => {
                        let l = self.compile_expr(&args[0])?;
                        let zi = self.compile_expr(&args[1])?;
                        let si = self.compile_expr(&args[2])?;
                        let w = self.compile_expr(&args[3])?;
                        return self.call_rt(self.rt.moo_ui_liste_zelle_setze, &[l.into(), zi.into(), si.into(), w.into()], "ui_liste_zelle_setze");
                    }
                    "moo_ui_liste_entferne" | "ui_liste_entferne" | "liste_entferne" => {
                        let l = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_liste_entferne, &[l.into(), i.into()], "ui_liste_entferne");
                    }
                    "ui_slider" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_slider, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into(), a[7].into(), a[8].into()], "ui_slider");
                    }
                    "ui_slider_wert" | "ui_slider_value" => {
                        let s = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_slider_wert, &[s.into()], "ui_slider_wert");
                    }
                    "ui_slider_setze" | "ui_slider_set" => {
                        let s = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_slider_setze, &[s.into(), v.into()], "ui_slider_setze");
                    }
                    "ui_fortschritt" | "ui_progress" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_fortschritt, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_fortschritt");
                    }
                    "ui_fortschritt_setze" | "ui_progress_set" => {
                        let b = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fortschritt_setze, &[b.into(), v.into()], "ui_fortschritt_setze");
                    }
                    "ui_bild" | "ui_image" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_bild, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_bild");
                    }
                    "ui_bild_setze" | "ui_image_set" => {
                        let b = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_bild_setze, &[b.into(), p.into()], "ui_bild_setze");
                    }
                    "ui_leinwand" | "ui_canvas" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_leinwand, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_leinwand");
                    }
                    "ui_leinwand_anfordern" | "ui_leinwand_erneuern" | "ui_canvas_request" | "ui_canvas_refresh" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_anfordern, &[l.into()], "ui_leinwand_anfordern");
                    }
                    "moo_ui_leinwand_on_maus" | "ui_leinwand_on_maus" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_on_maus, &[l.into(), c.into()], "ui_leinwand_on_maus");
                    }
                    "moo_ui_leinwand_on_bewegung" | "ui_leinwand_on_bewegung" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_on_bewegung, &[l.into(), c.into()], "ui_leinwand_on_bewegung");
                    }
                    "ui_leinwand_on_maus_los" | "ui_canvas_on_mouse_up" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_on_maus_los, &[l.into(), c.into()], "ui_leinwand_on_maus_los");
                    }
                    "ui_leinwand_on_rad" | "ui_canvas_on_scroll" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_on_rad, &[l.into(), c.into()], "ui_leinwand_on_rad");
                    }
                    "ui_leinwand_on_taste" | "ui_canvas_on_key" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_on_taste, &[l.into(), c.into()], "ui_leinwand_on_taste");
                    }
                    "ui_leinwand_fokus_setze" | "ui_canvas_focus" => {
                        let l = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_fokus_setze, &[l.into()], "ui_leinwand_fokus_setze");
                    }
                    "ui_leinwand_on_fokus" | "ui_canvas_on_focus" => {
                        let l = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_leinwand_on_fokus, &[l.into(), c.into()], "ui_leinwand_on_fokus");
                    }
                    "ui_zeichne_rechteck_rund" | "ui_draw_round_rect" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_rechteck_rund, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into(), a[6].into()], "ui_zeichne_rechteck_rund");
                    }
                    "ui_zeichne_clip_setze" | "ui_draw_clip_set" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_clip_setze, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_zeichne_clip_setze");
                    }
                    "ui_zeichne_clip_loesche" | "ui_draw_clip_clear" => {
                        let z = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_zeichne_clip_loesche, &[z.into()], "ui_zeichne_clip_loesche");
                    }
                    "ui_fenster_cursor_setze" | "ui_window_cursor_set" => {
                        let f = self.compile_expr(&args[0])?;
                        let n = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_cursor_setze, &[f.into(), n.into()], "ui_fenster_cursor_setze");
                    }
                    "ui_fenster_drag_start" | "ui_window_drag_start" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_fenster_drag_start, &[f.into()], "ui_fenster_drag_start");
                    }
                    "ui_fenster_resize_start" | "ui_window_resize_start" => {
                        let f = self.compile_expr(&args[0])?;
                        let k = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_resize_start, &[f.into(), k.into()], "ui_fenster_resize_start");
                    }
                    "ui_fenster_minimiere" | "ui_window_minimize" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_fenster_minimiere, &[f.into()], "ui_fenster_minimiere");
                    }
                    "ui_fenster_maximiere_umschalten" | "ui_window_maximize_toggle" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_fenster_maximiere_umschalten, &[f.into()], "ui_fenster_maximiere_umschalten");
                    }
                    "ui_fenster_transparenz" | "ui_window_transparency" => {
                        let f = self.compile_expr(&args[0])?;
                        let a = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_fenster_transparenz, &[f.into(), a.into()], "ui_fenster_transparenz");
                    }
                    "ui_host_backend" | "ui_host_backend_name" => {
                        return self.call_rt(self.rt.moo_ui_host_backend_name, &[], "ui_host_backend");
                    }
                    "ui_pointer_meta_hole" | "ui_pointer_meta_get" => {
                        return self.call_rt_borrowed_builtin(
                            self.rt.moo_ui_pointer_meta_hole,
                            args,
                            3,
                            name,
                            "ui_pointer_meta_hole",
                        );
                    }
                    "host_fenster" | "host_window" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        let d = self.compile_expr(&args[3])?;
                        return self.call_rt(self.rt.moo_ui_host_fenster, &[a.into(), b.into(), c.into(), d.into()], "host_fenster");
                    }
                    "host_zeige" | "host_show" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_host_zeige, &[a.into()], "host_zeige");
                    }
                    "host_schliessen" | "host_close" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_host_schliessen, &[a.into()], "host_schliessen");
                    }
                    "host_present" | "host_praesentiere" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_host_praesentiere, &[a.into(), b.into()], "host_present");
                    }
                    "host_on_input" | "host_input" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_host_input_callback, &[a.into(), b.into()], "host_on_input");
                    }
                    "host_laufen" | "host_run" => {
                        return self.call_rt(self.rt.moo_ui_host_laufen, &[], "host_laufen");
                    }
                    "host_pump" => {
                        return self.call_rt(self.rt.moo_ui_host_pump, &[], "host_pump");
                    }
                    "host_beenden" | "host_quit" => {
                        return self.call_rt(self.rt.moo_ui_host_beenden, &[], "host_beenden");
                    }
                    "host_drag_start" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_host_drag_start, &[a.into()], "host_drag_start");
                    }
                    "host_resize_start" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_host_resize_start, &[a.into(), b.into()], "host_resize_start");
                    }
                    "host_minimiere" | "host_minimize" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_host_minimiere, &[a.into()], "host_minimiere");
                    }
                    "host_maximiere" | "host_maximiere_umschalten" | "host_maximize_toggle" => {
                        let a = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_host_maximiere_umschalten, &[a.into()], "host_maximiere");
                    }
                    "host_cursor" | "host_cursor_setze" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_host_cursor_setze, &[a.into(), b.into()], "host_cursor");
                    }
                    "ui_zeichne_farbe" | "ui_draw_color" | "ui_draw_colour" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_farbe, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_zeichne_farbe");
                    }
                    "ui_zeichne_linie" | "ui_draw_line" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_linie, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_zeichne_linie");
                    }
                    "ui_zeichne_rechteck" | "ui_draw_rect" | "ui_draw_rectangle" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_rechteck, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_zeichne_rechteck");
                    }
                    "ui_zeichne_kreis" | "ui_draw_circle" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_kreis, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_zeichne_kreis");
                    }
                    "ui_zeichne_text" | "ui_draw_text" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_text, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_zeichne_text");
                    }
                    "moo_ui_zeichne_text_breite" | "ui_zeichne_text_breite" | "zeichne_text_breite" => {
                        let z = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        let g = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_zeichne_text_breite, &[z.into(), t.into(), g.into()], "ui_zeichne_text_breite");
                    }
                    "ui_zeichne_bild" | "ui_draw_image" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_bild, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_zeichne_bild");
                    }
                    "ui_zeichne_frame" | "ui_draw_frame" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_zeichne_frame, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_zeichne_frame");
                    }
                    "ui_rahmen" | "ui_frame" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_rahmen, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into(), a[5].into()], "ui_rahmen");
                    }
                    "ui_trenner" | "ui_separator" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_trenner, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_trenner");
                    }
                    "ui_tabs" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_tabs, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_tabs");
                    }
                    "ui_tab_hinzu" | "ui_tab_add" => {
                        let t = self.compile_expr(&args[0])?;
                        let ti = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_tab_hinzu, &[t.into(), ti.into()], "ui_tab_hinzu");
                    }
                    "ui_tabs_auswahl" | "ui_tabs_selection" => {
                        let t = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_tabs_auswahl, &[t.into()], "ui_tabs_auswahl");
                    }
                    "ui_tabs_auswahl_setze" | "ui_tabs_selection_set" => {
                        let t = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_tabs_auswahl_setze, &[t.into(), i.into()], "ui_tabs_auswahl_setze");
                    }
                    "ui_scroll" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_scroll, &[a[0].into(), a[1].into(), a[2].into(), a[3].into(), a[4].into()], "ui_scroll");
                    }
                    "ui_sichtbar" | "ui_visible" => {
                        let w = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_sichtbar, &[w.into(), s.into()], "ui_sichtbar");
                    }
                    "ui_aktiv" | "ui_active" => {
                        let w = self.compile_expr(&args[0])?;
                        let a = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_aktiv, &[w.into(), a.into()], "ui_aktiv");
                    }
                    "ui_position_setze" | "ui_position_set" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_position_setze, &[a[0].into(), a[1].into(), a[2].into()], "ui_position_setze");
                    }
                    "ui_groesse_setze" | "ui_size_set" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_groesse_setze, &[a[0].into(), a[1].into(), a[2].into()], "ui_groesse_setze");
                    }
                    "ui_farbe_setze" | "ui_color_set" => {
                        let w = self.compile_expr(&args[0])?;
                        let h = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_farbe_setze, &[w.into(), h.into()], "ui_farbe_setze");
                    }
                    "ui_schrift_setze" | "ui_font_set" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_schrift_setze, &[a[0].into(), a[1].into(), a[2].into()], "ui_schrift_setze");
                    }
                    "ui_tooltip_setze" | "ui_tooltip_set" => {
                        let w = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_tooltip_setze, &[w.into(), t.into()], "ui_tooltip_setze");
                    }
                    "ui_zerstoere" | "ui_destroy" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_zerstoere, &[w.into()], "ui_zerstoere");
                    }
                    "moo_ui_widget_id_setze" | "ui_widget_id_setze" | "widget_id_setze" => {
                        let w = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_widget_id_setze, &[w.into(), i.into()], "ui_widget_id_setze");
                    }
                    "moo_ui_widget_id_hole" | "ui_widget_id_hole" | "widget_id" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_widget_id_hole, &[w.into()], "ui_widget_id_hole");
                    }
                    "moo_ui_widget_info" | "ui_widget_info" | "widget_info" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_widget_info, &[w.into()], "ui_widget_info");
                    }
                    "moo_ui_widget_baum" | "ui_widget_baum" | "widget_baum" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_widget_baum, &[f.into()], "ui_widget_baum");
                    }
                    "moo_ui_widget_suche" | "ui_widget_suche" | "widget_suche" => {
                        let f = self.compile_expr(&args[0])?;
                        let i = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_widget_suche, &[f.into(), i.into()], "ui_widget_suche");
                    }
                    "moo_ui_test_snapshot" | "ui_test_snapshot" | "test_snapshot" => {
                        let f = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_test_snapshot, &[f.into(), p.into()], "ui_test_snapshot");
                    }
                    "moo_ui_test_snapshot_widget" | "ui_test_snapshot_widget" | "test_snapshot_widget" => {
                        let w = self.compile_expr(&args[0])?;
                        let p = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_test_snapshot_widget, &[w.into(), p.into()], "ui_test_snapshot_widget");
                    }
                    "moo_ui_test_klick" | "ui_test_klick" | "test_klick" => {
                        let w = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_test_klick, &[w.into()], "ui_test_klick");
                    }
                    "moo_ui_test_klick_xy" | "ui_test_klick_xy" | "test_klick_xy" => {
                        let f = self.compile_expr(&args[0])?;
                        let x = self.compile_expr(&args[1])?;
                        let y = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_test_klick_xy, &[f.into(), x.into(), y.into()], "ui_test_klick_xy");
                    }
                    "moo_ui_test_text_setze" | "ui_test_text_setze" | "test_text_setze" => {
                        let w = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_test_text_setze, &[w.into(), t.into()], "ui_test_text_setze");
                    }
                    "moo_ui_test_shortcut" | "ui_test_shortcut" | "test_shortcut" => {
                        let f = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_test_shortcut, &[f.into(), s.into()], "ui_test_shortcut");
                    }
                    "moo_ui_test_warte" | "ui_test_warte" | "test_warte" => {
                        let ms = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_test_warte, &[ms.into()], "ui_test_warte");
                    }
                    "moo_ui_test_pump" | "ui_test_pump" | "test_pump" => {
                        return self.call_rt(self.rt.moo_ui_test_pump, &[], "ui_test_pump");
                    }
                    "ui_timer_hinzu" | "ui_timer_add" => {
                        let ms = self.compile_expr(&args[0])?;
                        let c = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_timer_hinzu, &[ms.into(), c.into()], "ui_timer_hinzu");
                    }
                    "ui_timer_entfernen" | "ui_timer_remove" => {
                        let id = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_timer_entfernen, &[id.into()], "ui_timer_entfernen");
                    }
                    "ui_info" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_info, &[a[0].into(), a[1].into(), a[2].into()], "ui_info");
                    }
                    "ui_warnung" | "ui_warning" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_warnung, &[a[0].into(), a[1].into(), a[2].into()], "ui_warnung");
                    }
                    "ui_fehler" | "ui_error" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_fehler, &[a[0].into(), a[1].into(), a[2].into()], "ui_fehler");
                    }
                    "ui_frage" | "ui_question" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_frage, &[a[0].into(), a[1].into(), a[2].into()], "ui_frage");
                    }
                    "ui_eingabe_dialog" | "ui_input_dialog" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_eingabe_dialog, &[a[0].into(), a[1].into(), a[2].into(), a[3].into()], "ui_eingabe_dialog");
                    }
                    "ui_datei_oeffnen" | "ui_file_open" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_datei_oeffnen, &[a[0].into(), a[1].into(), a[2].into()], "ui_datei_oeffnen");
                    }
                    "ui_datei_speichern" | "ui_file_save" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_datei_speichern, &[a[0].into(), a[1].into(), a[2].into()], "ui_datei_speichern");
                    }
                    "ui_ordner_waehlen" | "ui_folder_choose" => {
                        let p = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_ordner_waehlen, &[p.into(), t.into()], "ui_ordner_waehlen");
                    }
                    "ui_menueleiste" | "ui_menubar" => {
                        let f = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_menueleiste, &[f.into()], "ui_menueleiste");
                    }
                    "ui_menue" | "ui_menu" => {
                        let l = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_menue, &[l.into(), t.into()], "ui_menue");
                    }
                    "ui_menue_eintrag" | "ui_menu_item" => {
                        let a: Vec<_> = args.iter().map(|x| self.compile_expr(x)).collect::<Result<Vec<_>, _>>()?;
                        return self.call_rt(self.rt.moo_ui_menue_eintrag, &[a[0].into(), a[1].into(), a[2].into()], "ui_menue_eintrag");
                    }
                    "ui_menue_trenner" | "ui_menu_separator" => {
                        let m = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_ui_menue_trenner, &[m.into()], "ui_menue_trenner");
                    }
                    "ui_menue_untermenue" | "ui_menu_submenu" => {
                        let m = self.compile_expr(&args[0])?;
                        let t = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_menue_untermenue, &[m.into(), t.into()], "ui_menue_untermenue");
                    }
                    "ui_shortcut_bind" | "ui_shortcut_binde" | "ui_kuerzel_binde" => {
                        let f = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        let c = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_ui_shortcut_bind, &[f.into(), s.into(), c.into()], "ui_shortcut_bind");
                    }
                    "ui_shortcut_loese" | "ui_shortcut_unbind" | "ui_kuerzel_loese" => {
                        let f = self.compile_expr(&args[0])?;
                        let s = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_ui_shortcut_loese, &[f.into(), s.into()], "ui_shortcut_loese");
                    }
                    "tray_run" | "tray_loop" => {
                        // DEPRECATED: Alias auf ui_laufen (siehe moo_tray.h).
                        return self.call_rt(self.rt.moo_tray_run, &[], "tray_run");
                    }
                    _ => {}
                }

                // Lambda-Lookup: wenn name ein Lambda-Alias ist, den echten Namen nutzen
                let actual_name = self.lambda_names.get(name)
                    .cloned()
                    .unwrap_or(name.clone());
                let function = match self.module.get_function(&actual_name) {
                    Some(f) => f,
                    None => {
                        // Fallback: Name ist eine Variable die einen MOO_FUNC haelt
                        // (z.B. setze f auf benannte_fn; f(21)). Indirect-Call
                        // ueber moo_func_call_N mit dem passenden Helper.
                        if self.variables.contains_key(name) || self.globals.contains_key(name) {
                            let fn_val = self.load_var(name)?;
                            let mut compiled_args: Vec<BasicMetadataValueEnum> = Vec::new();
                            compiled_args.push(fn_val.into());
                            let mut owned_args = vec![fn_val];
                            for a in args {
                                let owned_len = self.stmt_owned_context.len();
                                if self.native_value_temps() { self.stmt_owned_context.extend_from_slice(&owned_args); }
                                let compiled = self.compile_expr(a);
                                self.stmt_owned_context.truncate(owned_len);
                                let v = compiled?;
                                owned_args.push(v);
                                if guard_args { self.guard_local_expr_error(&owned_args)?; }
                                compiled_args.push(v.into());
                            }
                            let helper = match args.len() {
                                0 => self.rt.moo_func_call_0,
                                1 => self.rt.moo_func_call_1,
                                2 => self.rt.moo_func_call_2,
                                3 => self.rt.moo_func_call_3,
                                4 => self.rt.moo_func_call_4,
                                5 => self.rt.moo_func_call_5,
                                6 => self.rt.moo_func_call_6,
                                7 => self.rt.moo_func_call_7,
                                8 => self.rt.moo_func_call_8,
                                n => return Err(format!(
                                    "Indirekter Aufruf von '{name}' mit {n} Argumenten \
                                     nicht unterstuetzt (max 8). Umschreiben auf \
                                     benannte Funktion oder weniger Argumente."
                                )),
                            };
                            let result = self.call_rt(
                                helper,
                                &compiled_args,
                                &format!("indirect_{name}"),
                            )?;
                            // Nativer Funktionswert mit Fehler: kein Ersatzwert.
                            let nativ_fehler = self.nativer_wert_fehler(fn_val)?;
                            // load_var created an owning function temporary.
                            // moo_func_call_N only borrows it; the generated
                            // callee owns the forwarded argument temporaries.
                            self.call_rt_void(
                                self.rt.moo_release,
                                &[fn_val.into()],
                                "rel_indirect_fn",
                            )?;
                            if let Some(bedingung) = nativ_fehler {
                                self.nativer_wert_abbruch(bedingung, &[result])?;
                            }
                            return Ok(result);
                        }
                        // Weder LLVM-Function noch Variable → Fehler mit Suggestion
                        let mut suggestion = String::new();
                        let mut best_dist = usize::MAX;
                        let mut func_iter = self.module.get_first_function();
                        while let Some(f) = func_iter {
                            let fname = f.get_name().to_string_lossy().to_string();
                            if !fname.starts_with("moo_") && !fname.starts_with("llvm.") && fname != "main" {
                                let dist = levenshtein(name, &fname);
                                if dist < best_dist && dist <= 3 {
                                    best_dist = dist;
                                    suggestion = fname;
                                }
                            }
                            func_iter = f.get_next_function();
                        }
                        let mut d = crate::diagnose::Diagnose::fehler("M-C001", if suggestion.is_empty() {
                            format!("Funktion '{name}' nicht gefunden.")
                        } else {
                            format!("Funktion '{name}' nicht gefunden. Meintest du '{suggestion}'?")
                        }).gefunden(name.to_string());
                        if !suggestion.is_empty() {
                            d = d.vorschlag(format!("Meintest du '{suggestion}'?"));
                        }
                        return Err(self.diag_fehler(d));
                    }
                };
                let mut compiled_args: Vec<BasicMetadataValueEnum> = Vec::new();
                let mut owned_args = Vec::new();
                for a in args {
                    let owned_len = self.stmt_owned_context.len();
                    if self.native_value_temps() { self.stmt_owned_context.extend_from_slice(&owned_args); }
                    let compiled = self.compile_expr(a);
                    self.stmt_owned_context.truncate(owned_len);
                    let v = compiled?;
                    owned_args.push(v);
                    if guard_args { self.guard_local_expr_error(&owned_args)?; }
                    compiled_args.push(v.into());
                }
                // Closure-Captures anhaengen wenn vorhanden
                if let Some(captures) = self.lambda_captures.get(&actual_name).cloned() {
                    for var_name in &captures {
                        if let Ok(val) = self.load_var(var_name) {
                            compiled_args.push(val.into());
                        } else {
                            let none_val = self.call_rt(self.rt.moo_none, &[], "cap_none")?;
                            compiled_args.push(none_val.into());
                        }
                    }
                }

                // Fehlende Argumente mit moo_none() auffuellen (fuer Default-Parameter)
                let expected_params = function.count_params() as usize;
                if compiled_args.len() > expected_params {
                    compiled_args.truncate(expected_params);
                } else {
                    while compiled_args.len() < expected_params {
                        let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                        compiled_args.push(none_val.into());
                    }
                }
                self.call_rt(function, &compiled_args, "call")
            }
            Expr::MethodCall { object, method, args } => {
                // Crystal-inspiriert: Typ-Warnung bei inkompatiblen Methoden.
                // P2-T29 S4: Mit --typpruefung meldet der Vorpass M-H001
                // (dominanzsicher, mit --typ-streng ein Fehler); hier nur ohne ihn.
                if let Some(typ) = self.infer_type(object).filter(|_| !crate::typpruefung::aktiv()) {
                    let mismatch = crate::typpruefung::methode_unpassend(typ, method);
                    if mismatch {
                        if let Expr::Identifier(name) = object.as_ref() {
                            let hinweis = self.diag_ort(crate::diagnose::Diagnose::hinweis("M-H001", format!(
                                "'{name}' ist vom Typ {typ}, aber '.{method}()' ist nicht fuer {typ} definiert"
                            )));
                            self.warnings.push(hinweis);
                        }
                    }
                }

                let obj_val = self.compile_defer_expr(object, &[])?;

                // Per-Expression-Slot im Entry-Block fuer den obj-Temp:
                // - Releaset VOR dem Store die letzte Iteration (oder NONE),
                //   sodass bei einer `for`/`while`-Schleife pro Iteration nur
                //   EIN obj gleichzeitig lebt.
                // - Am Function-Exit released release_function_locals() den
                //   letzten Slot-Inhalt — deckt alle Match-Arm-Early-Returns
                //   der unten folgenden Kaskade ab, ohne jede Arm einzeln
                //   editieren zu muessen.
                // - Die Match-Kaskade nutzt weiterhin die lokale Variable
                //   `obj` (nicht den Slot), damit LLVM dominance trivial bleibt.
                // Der Punkt ist kein Bezeichnerzeichen: Der Slot kann keinen
                // gleichnamigen Benutzer-Local in self.variables ueberschreiben.
                let slot_name = format!("__mcall_obj.{}", self.method_obj_counter);
                self.method_obj_counter += 1;
                let func_cur = self.current_function.unwrap();
                let entry_bb = func_cur.get_first_basic_block().unwrap();
                let current_block = self.builder.get_insert_block().unwrap();
                if let Some(first_instr) = entry_bb.get_first_instruction() {
                    self.builder.position_before(&first_instr);
                } else {
                    self.builder.position_at_end(entry_bb);
                }
                let obj_slot = self.builder.build_alloca(self.mv_type(), &slot_name)
                    .map_err(|e| format!("{e}"))?;
                let none_tag = self.context.i64_type().const_int(tag::NONE, false);
                let zero_data = self.context.i64_type().const_int(0, false);
                let none_struct = self.mv_type().const_named_struct(&[none_tag.into(), zero_data.into()]);
                self.builder.build_store(obj_slot, none_struct).map_err(|e| format!("{e}"))?;
                self.variables.insert(slot_name.clone(), obj_slot);
                if self.in_main_function() {
                    self.main_mcall_slots.push(obj_slot);
                }
                self.builder.position_at_end(current_block);

                // Pre-Release der letzten Schleifeniteration im Slot (sonst
                // leakt der vorherige obj-Wert bei Loop-Wiederholung).
                let prev = self.builder.build_load(self.mv_type(), obj_slot, "prev_mcall_obj")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                self.call_rt_void(self.rt.moo_release, &[prev.into()], "rel_prev_mcall_obj")?;
                self.builder.build_store(obj_slot, obj_val).map_err(|e| format!("{e}"))?;
                // Der Slot UEBERNIMMT die +1 von compile_expr (load_var-retain):
                // Runtime-Konvention "Receiver borrowed, Slot released" (siehe
                // moo_smart_contains-Kommentar). Ein zusaetzliches retain hier
                // (historisch "retain_for_slot") liess pro Methoden-Call netto
                // +1 auf dem Receiver zurueck — bei frischen Receivern ein
                // messbarer Heap-Leak (G1-LEAK: 430B/Iteration), bei loop-
                // invarianten nur RSS-unsichtbare Refcount-Inflation.
                // Gate: regression/test_method_multi_call_abort.moos.

                let obj = obj_val;
                // P014-A3: Tensor-Methoden nur, wenn KEINE User-Klasse den Namen
                // definiert (Muster "abfrage"|"query"-Arm) — Klassen-Methoden
                // gewinnen immer (raytracer Vec3.plus/mal, Kinder-Klassen).
                // P2-T87 (Befund 9ff1eb58): Klassenmethoden haben bei Objekt-
                // Empfaengern Vorrang vor gleichnamigen Builtin-Methoden; das
                // Builtin gilt nur ohne gleichnamigen Member. Im Builtin-Zweig
                // der Weiche (methode_nur_builtin) zaehlt keine Klasse mehr.
                let nur_builtin = std::mem::take(&mut self.methode_nur_builtin);
                let user_hat_methode = !nur_builtin && self.class_methods.keys().any(|(_, m)| m == method);
                let mut nur_klasse = false;
                if user_hat_methode && Self::ist_builtin_methode(method) {
                    let static_class = match object.as_ref() {
                        Expr::Identifier(name) => self.object_var_types.get(name).cloned(),
                        Expr::This => self.current_class.clone(),
                        _ => None,
                    };
                    let statisch = match static_class {
                        Some(class) => self.klassen_methode(&class, method)?.is_some(),
                        None => false,
                    };
                    if statisch {
                        // Statisch bekannte Klasse mit der Methode: direkt in den
                        // Klassen-Arm (_), keine Laufzeitpruefung noetig.
                        nur_klasse = true;
                    } else {
                        return self.methoden_weiche(obj, &slot_name, method, args);
                    }
                }
                // Eingebaute Methoden ("\u{0}" trifft keinen Builtin-Arm -> _)
                match if nur_klasse { "\u{0}" } else { method.as_str() } {
                    // === Tensor-Methoden (Plan-014 A3) ===
                    // Tensor-Konvention: Runtime borrowed — obj laeuft ueber den
                    // Slot (retain_for_slot + Slot-Cleanup), Zusatz-Args werden
                    // hier post-call released.
                    // ARITHMETIK LAEUFT UEBER OPERATOREN (a+b, t*2, -t, t**p):
                    // moo_add/sub/mul/div/pow/neg dispatchen auf den Tensor-Tag
                    // (moo_ops.c) — bewusst KEINE plus/mal-Methoden-Arms, damit
                    // User-Klassen (z.B. raytracer Vec3.plus/mal) nicht
                    // geshadowt werden.
                    "mit_gradient" | "with_gradient" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_mit_gradient, &[obj.into()], "t_mitgrad");
                    }
                    "rueckwaerts" | "rückwärts" | "backward" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_rueckwaerts, &[obj.into()], "t_rueckw");
                    }
                    "gradient" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_gradient, &[obj.into()], "t_grad");
                    }
                    "gradient_löschen" | "gradient_loeschen" | "zero_grad" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_gradient_loeschen, &[obj.into()], "t_zerograd");
                    }
                    // KIP-X1b: t.gradient_setzen(quelle) — quelle Tensor gleicher
                    // Form ODER flache Zahlenliste. Runtime liest die Quelle nur
                    // (kein Retain), daher wird das +1-Arg hier wieder released.
                    "gradient_setzen" | "set_gradient" if !user_hat_methode => {
                        let q = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tensor_gradient_setzen,
                            &[obj.into(), q.into()], "t_gradsetzen")?;
                        self.call_rt_void(self.rt.moo_release, &[q.into()], "rel_tgs_q")?;
                        return Ok(r);
                    }
                    "matmul" if !user_hat_methode => {
                        let b = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tensor_matmul,
                            &[obj.into(), b.into()], "t_matmul")?;
                        self.call_rt_void(self.rt.moo_release, &[b.into()], "rel_tmm_b")?;
                        return Ok(r);
                    }
                    "transponieren" | "transpose" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_transponieren,
                            &[obj.into()], "t_transp");
                    }
                    "umformen" | "reshape" if !user_hat_methode => {
                        let f = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tensor_umformen,
                            &[obj.into(), f.into()], "t_umform")?;
                        self.call_rt_void(self.rt.moo_release, &[f.into()], "rel_tuf_form")?;
                        return Ok(r);
                    }
                    "zeilen" | "rows" if !user_hat_methode => {
                        let s = self.compile_expr(&args[0])?;
                        let e = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_tensor_zeilen,
                            &[obj.into(), s.into(), e.into()], "t_zeilen");
                    }
                    "verketten" | "concat" if !user_hat_methode => {
                        let b = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tensor_verbinden,
                            &[obj.into(), b.into()], "t_verkett")?;
                        self.call_rt_void(self.rt.moo_release, &[b.into()], "rel_tvk_b")?;
                        return Ok(r);
                    }
                    "summe" | "sum" if !user_hat_methode => {
                        let a = if args.is_empty() {
                            self.call_rt(self.rt.moo_none, &[], "t_axis_none")?
                        } else { self.compile_expr(&args[0])? };
                        return self.call_rt(self.rt.moo_tensor_summe,
                            &[obj.into(), a.into()], "t_summe");
                    }
                    "mittel" | "mean" if !user_hat_methode => {
                        let a = if args.is_empty() {
                            self.call_rt(self.rt.moo_none, &[], "t_axis_none2")?
                        } else { self.compile_expr(&args[0])? };
                        return self.call_rt(self.rt.moo_tensor_mittel,
                            &[obj.into(), a.into()], "t_mittel");
                    }
                    "max_wert" | "max_val" if !user_hat_methode => {
                        let a = if args.is_empty() {
                            self.call_rt(self.rt.moo_none, &[], "t_axis_none3")?
                        } else { self.compile_expr(&args[0])? };
                        return self.call_rt(self.rt.moo_tensor_maximum,
                            &[obj.into(), a.into()], "t_max");
                    }
                    "exp" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_exp, &[obj.into()], "t_exp"); }
                    "log" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_log, &[obj.into()], "t_log"); }
                    "wurzel" | "sqrt" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_sqrt, &[obj.into()], "t_sqrt");
                    }
                    "negieren" | "neg" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_neg, &[obj.into()], "t_neg");
                    }
                    "hoch" | "pow" if !user_hat_methode => {
                        let z = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tensor_pow,
                            &[obj.into(), z.into()], "t_pow");
                    }
                    // KI-Q1: randomisierte Walsh-Hadamard-Rotation (Seed als Zahl)
                    "hadamard" if !user_hat_methode => {
                        let z = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tensor_hadamard,
                            &[obj.into(), z.into()], "t_hadamard");
                    }
                    "hadamard_inv" if !user_hat_methode => {
                        let z = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_tensor_hadamard_inv,
                            &[obj.into(), z.into()], "t_hadamard_inv");
                    }
                    "relu" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_relu, &[obj.into()], "t_relu"); }
                    "sigmoid" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_sigmoid, &[obj.into()], "t_sigmoid"); }
                    "tanh" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_tanh, &[obj.into()], "t_tanh"); }
                    "gelu" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_gelu, &[obj.into()], "t_gelu"); }
                    "softmax" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_softmax, &[obj.into()], "t_softmax"); }
                    "logsoftmax" if !user_hat_methode => { return self.call_rt(self.rt.moo_tensor_logsoftmax, &[obj.into()], "t_logsoftmax"); }
                    // D1: Storage-DType wechseln. Runtime borrowed obj + dtype-Text,
                    // gibt denselben (in-place mutierten) Tensor +1 owning zurueck.
                    // Der dtype-Text-Arg wird hier post-released (Tensor-Konvention).
                    "als_dtype" | "as_dtype" if !user_hat_methode => {
                        let dt = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tensor_als_dtype,
                            &[obj.into(), dt.into()], "t_als_dtype")?;
                        self.call_rt_void(self.rt.moo_release, &[dt.into()], "rel_tdt")?;
                        return Ok(r);
                    }
                    "form" | "shape" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_form, &[obj.into()], "t_form");
                    }
                    "größe" | "groesse" | "size" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_groesse, &[obj.into()], "t_groesse");
                    }
                    "zu_liste" | "to_list" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_tensor_zu_liste, &[obj.into()], "t_zuliste");
                    }
                    "wert" | "value_at" if !user_hat_methode => {
                        let idx = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_tensor_holen,
                            &[obj.into(), idx.into()], "t_holen")?;
                        self.call_rt_void(self.rt.moo_release, &[idx.into()], "rel_th_idx")?;
                        return Ok(r);
                    }
                    "wert_setzen" | "set_value" if !user_hat_methode => {
                        let idx = self.compile_expr(&args[0])?;
                        let v = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_tensor_setzen,
                            &[obj.into(), idx.into(), v.into()], "t_setzen")?;
                        self.call_rt_void(self.rt.moo_release, &[idx.into()], "rel_ts_idx")?;
                        return Ok(r);
                    }
                    // NN (Plan-014 C1): Optimizer-Schritt als Methode auf dem
                    // Optimizer-Dict — Laufzeit prueft den "__nn"-Marker.
                    "schritt" | "step" if !user_hat_methode => {
                        return self.call_rt(self.rt.moo_nn_opt_schritt,
                            &[obj.into()], "nn_schritt");
                    }
                    // Kinderleicht-Methoden auf dem ki_netz-Dict (Plan-014 D1).
                    // Laufzeit prueft den "__nn"-Marker; Heap-Args post-released.
                    "trainiere" | "train" if !user_hat_methode => {
                        let x = self.compile_expr(&args[0])?;
                        let y = self.compile_expr(&args[1])?;
                        let o = if args.len() > 2 { self.compile_expr(&args[2])? }
                                else { self.call_rt(self.rt.moo_none, &[], "nn_tr_none")? };
                        let r = self.call_rt(self.rt.moo_nn_trainiere,
                            &[obj.into(), x.into(), y.into(), o.into()], "nn_trainiere")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_tr_x")?;
                        self.call_rt_void(self.rt.moo_release, &[y.into()], "rel_tr_y")?;
                        self.call_rt_void(self.rt.moo_release, &[o.into()], "rel_tr_o")?;
                        return Ok(r);
                    }
                    "vorhersage" | "predict" if !user_hat_methode => {
                        let x = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_vorhersage,
                            &[obj.into(), x.into()], "nn_vorhersage")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_vh_x")?;
                        return Ok(r);
                    }
                    "genauigkeit" | "accuracy" if !user_hat_methode => {
                        let x = self.compile_expr(&args[0])?;
                        let y = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_nn_genauigkeit,
                            &[obj.into(), x.into(), y.into()], "nn_genauigkeit")?;
                        self.call_rt_void(self.rt.moo_release, &[x.into()], "rel_gk_x")?;
                        self.call_rt_void(self.rt.moo_release, &[y.into()], "rel_gk_y")?;
                        return Ok(r);
                    }
                    "speichern" | "save" if !user_hat_methode => {
                        let pfad = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_nn_speichern,
                            &[obj.into(), pfad.into()], "nn_speichern")?;
                        self.call_rt_void(self.rt.moo_release, &[pfad.into()], "rel_sp_pfad")?;
                        return Ok(r);
                    }
                    "append" | "hinzufügen" => {
                        let arg = self.compile_expr(&args[0])?;
                        // P2-T90 (Review 263bb18d): Bei einem fangbaren append-Fehler
                        // (keine Liste, eingefroren, Kapazitaet) uebernimmt die Runtime
                        // arg NICHT. Freigabe nur, wenn genau dieser Aufruf den Fehler
                        // gesetzt hat (Flag vorher frei, nachher gesetzt); bei Erfolg
                        // gehoert arg der Liste, ein schon offener Fehler bleibt unberuehrt.
                        let fehler_vorher = if self.bare_mode { None } else {
                            match self.builder.build_call(self.rt.moo_try_check, &[], "append.fehler_vorher")
                                .map_err(|e| e.to_string())?.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => Some(v.into_int_value()),
                                _ => return Err("moo_try_check fehlgeschlagen".into()),
                            }
                        };
                        self.call_rt_void(self.rt.moo_list_append,
                            &[obj.into(), arg.into()], "append")?;
                        if let Some(vorher) = fehler_vorher {
                            let nachher = match self.builder.build_call(self.rt.moo_try_check, &[], "append.fehler_nachher")
                                .map_err(|e| e.to_string())?.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("moo_try_check fehlgeschlagen".into()),
                            };
                            let null = self.context.i32_type().const_zero();
                            let war_frei = self.builder.build_int_compare(inkwell::IntPredicate::EQ, vorher, null, "append.war_frei")
                                .map_err(|e| e.to_string())?;
                            let jetzt = self.builder.build_int_compare(inkwell::IntPredicate::NE, nachher, null, "append.jetzt")
                                .map_err(|e| e.to_string())?;
                            let abgelehnt = self.builder.build_and(war_frei, jetzt, "append.abgelehnt")
                                .map_err(|e| e.to_string())?;
                            let function = self.current_function.ok_or("append ohne Funktion")?;
                            let freigabe_bb = self.context.append_basic_block(function, "append.abgelehnt");
                            let weiter_bb = self.context.append_basic_block(function, "append.weiter");
                            self.builder.build_conditional_branch(abgelehnt, freigabe_bb, weiter_bb)
                                .map_err(|e| e.to_string())?;
                            self.builder.position_at_end(freigabe_bb);
                            self.call_rt_void(self.rt.moo_release, &[arg.into()], "rel_append_abgelehnt")?;
                            self.builder.build_unconditional_branch(weiter_bb).map_err(|e| e.to_string())?;
                            self.builder.position_at_end(weiter_bb);
                        }
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "length" | "länge" => {
                        // Generische Laenge: String->length, List->length, Dict->count.
                        // (Vorher hart moo_list_length -> 0 fuer Strings/Dicts.)
                        return self.call_rt(self.rt.moo_length, &[obj.into()], "len");
                    }
                    "pop" => {
                        return self.call_rt(self.rt.moo_list_pop, &[obj.into()], "pop");
                    }
                    // ERDE-11: Listen leeren/entfernen/einfuegen. Klassen-Methoden
                    // gleichen Namens gewinnen (z.B. mini_db.einfuegen).
                    "leeren" | "clear" if !user_hat_methode && args.is_empty() => {
                        return self.call_rt(self.rt.moo_list_clear, &[obj.into()], "clear");
                    }
                    "entfernen" | "remove_at" if !user_hat_methode && args.len() == 1 => {
                        let idx = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_list_remove_at, &[obj.into(), idx.into()], "remove_at")?;
                        self.call_rt_void(self.rt.moo_release, &[idx.into()], "rel_remove_idx")?;
                        return Ok(r);
                    }
                    "einfügen" | "einfuegen" | "insert" if !user_hat_methode && args.len() == 2 => {
                        let idx = self.compile_expr(&args[0])?;
                        let item = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_list_insert, &[obj.into(), idx.into(), item.into()], "insert")?;
                        self.call_rt_void(self.rt.moo_release, &[idx.into()], "rel_insert_idx")?;
                        return Ok(r);
                    }
                    "reverse" | "umkehren" => {
                        return self.call_rt(self.rt.moo_list_reverse, &[obj.into()], "rev");
                    }
                    "sort" | "sortieren" => {
                        return self.call_rt(self.rt.moo_list_sort, &[obj.into()], "sort");
                    }
                    "join" | "verbinden" => {
                        let delim = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_list_join,
                            &[obj.into(), delim.into()], "join")?;
                        // CG2: delim wird von list_join nur gelesen (nicht gespeichert) → release.
                        // obj bleibt — T1-Slot kuemmert sich darum.
                        self.call_rt_void(self.rt.moo_release, &[delim.into()], "rel_join_delim")?;
                        return Ok(r);
                    }
                    "contains" | "enthält" => {
                        // Tag-dispatch: dict.enthält(key) → moo_dict_has,
                        // list.enthält(item) → moo_list_contains,
                        // string.enthält(sub) → moo_string_contains.
                        // (vorher hart auf moo_list_contains gemappt → false auf dicts)
                        let item = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_smart_contains,
                            &[obj.into(), item.into()], "smart_contains");
                    }
                    "keys" | "schlüssel" => {
                        return self.call_rt(self.rt.moo_dict_keys, &[obj.into()], "keys");
                    }
                    "has" | "hat" => {
                        let key = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_dict_has,
                            &[obj.into(), key.into()], "has");
                    }
                    "upper" | "gross" => {
                        return self.call_rt(self.rt.moo_string_upper, &[obj.into()], "upper");
                    }
                    "lower" | "klein" => {
                        return self.call_rt(self.rt.moo_string_lower, &[obj.into()], "lower");
                    }
                    "trim" | "trimmen" => {
                        return self.call_rt(self.rt.moo_string_trim, &[obj.into()], "trim");
                    }
                    "slice" | "teilstring" => {
                        let start = self.compile_expr(&args[0])?;
                        let end = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_string_slice,
                            &[obj.into(), start.into(), end.into()], "slice");
                    }
                    "split" | "teilen" => {
                        let delim = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_string_split,
                            &[obj.into(), delim.into()], "split")?;
                        // CG2: delim read-only → release. obj via T1-Slot.
                        self.call_rt_void(self.rt.moo_release, &[delim.into()], "rel_split_delim")?;
                        return Ok(r);
                    }
                    "replace" | "ersetzen" => {
                        let old_s = self.compile_expr(&args[0])?;
                        let new_s = self.compile_expr(&args[1])?;
                        let r = self.call_rt(self.rt.moo_string_replace,
                            &[obj.into(), old_s.into(), new_s.into()], "replace")?;
                        // CG2: old_s/new_s read-only → release. obj via T1-Slot.
                        self.call_rt_void(self.rt.moo_release, &[old_s.into()], "rel_replace_old")?;
                        self.call_rt_void(self.rt.moo_release, &[new_s.into()], "rel_replace_new")?;
                        return Ok(r);
                    }
                    "str_contains" | "text_enthält" => {
                        let needle = self.compile_expr(&args[0])?;
                        let r = self.call_rt(self.rt.moo_string_contains,
                            &[obj.into(), needle.into()], "str_contains")?;
                        // CG2: needle read-only → release. obj via T1-Slot.
                        self.call_rt_void(self.rt.moo_release, &[needle.into()], "rel_contains_needle")?;
                        return Ok(r);
                    }
                    "warten" | "wait" => {
                        return self.call_rt(self.rt.moo_thread_wait, &[obj.into()], "thread_wait");
                    }
                    "binde" | "bind" => {
                        let key = self.compile_expr(&args[0])?;
                        let val = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_db_stmt_bind, &[obj.into(), key.into(), val.into()], "stmt_bind");
                    }
                    "schritt" | "step" => {
                        return self.call_rt(self.rt.moo_db_stmt_step, &[obj.into()], "stmt_step");
                    }
                    "ausfuehren" | "ausführen" | "execute" | "abfrage" | "query" => {
                        let builtin = if matches!(method.as_str(), "abfrage" | "query") {
                            self.rt.moo_db_stmt_query
                        } else {
                            self.rt.moo_db_stmt_execute
                        };
                        if !user_hat_methode {
                            return self.call_rt(builtin, &[obj.into()], "stmt_dispatch");
                        }
                        let mut values = Vec::with_capacity(args.len());
                        for arg in args { values.push(self.compile_expr(arg)?); }
                        let function = self.current_function.ok_or("Methodenaufruf ohne Funktion")?;
                        let result_ptr = self.builder.build_alloca(self.mv_type(), "stmt_dispatch_result")
                            .map_err(|e| format!("{e}"))?;
                        let class_call = self.call_rt_abi(self.rt.moo_object_class_name, &[obj.into()], "stmt_class")?;
                        let class_ptr = match class_call.try_as_basic_value() {
                            inkwell::values::ValueKind::Basic(v) => v.into_pointer_value(),
                            _ => return Err("moo_object_class_name hat keinen Wert".to_string()),
                        };
                        let strcmp_fn = self.module.get_function("strcmp").unwrap_or_else(|| {
                            let ptr = self.context.ptr_type(inkwell::AddressSpace::default());
                            self.module.add_function("strcmp", self.context.i32_type().fn_type(&[ptr.into(), ptr.into()], false), None)
                        });
                        let mut classes: Vec<String> = self.class_methods.keys()
                            .map(|(class, _)| class.clone()).chain(self.class_parents.keys().cloned()).collect();
                        classes.sort();
                        classes.dedup();
                        let after_bb = self.context.append_basic_block(function, "stmt_dispatch_after");
                        for (index, class_name) in classes.iter().enumerate() {
                            let mut current = class_name.clone();
                            let mut visited = std::collections::HashSet::new();
                            let method_fn = loop {
                                if !visited.insert(current.clone()) {
                                    return Err(format!("Zyklische Klassenvererbung bei {class_name}"));
                                }
                                if let Some(func) = self.class_methods.get(&(current.clone(), method.clone())) {
                                    break Some(*func);
                                }
                                match self.class_parents.get(&current) {
                                    Some(parent) => current = parent.clone(),
                                    None => break None,
                                }
                            };
                            let Some(method_fn) = method_fn else { continue };
                            let expected = method_fn.count_params() as usize;
                            if expected == 0 { return Err(format!("{class_name}.{method} hat keinen Receiver")); }
                            let name_ptr = self.make_global_str(class_name, &format!("stmt_class_{index}"))?;
                            let cmp_call = self.builder.build_call(strcmp_fn, &[class_ptr.into(), name_ptr.into()], "stmt_class_cmp")
                                .map_err(|e| format!("{e}"))?;
                            let cmp = match cmp_call.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("strcmp hat keinen Wert".to_string()),
                            };
                            let matches = self.builder.build_int_compare(
                                inkwell::IntPredicate::EQ, cmp, self.context.i32_type().const_zero(), "stmt_class_match"
                            ).map_err(|e| format!("{e}"))?;
                            let call_bb = self.context.append_basic_block(function, "stmt_user_call");
                            let next_bb = self.context.append_basic_block(function, "stmt_user_next");
                            self.builder.build_conditional_branch(matches, call_bb, next_bb).map_err(|e| format!("{e}"))?;
                            self.builder.position_at_end(call_bb);
                            // Der temporaere Receiver-Slot bleibt Besitzer; die Methode konsumiert eine eigene Referenz.
                            self.call_rt_void(self.rt.moo_retain, &[obj.into()], "stmt_retain_receiver")?;
                            let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                            for value in values.iter().take(expected - 1) { call_args.push((*value).into()); }
                            for value in values.iter().skip(expected - 1) {
                                self.call_rt_void(self.rt.moo_release, &[(*value).into()], "stmt_unused_arg")?;
                            }
                            while call_args.len() < expected {
                                call_args.push(self.call_rt(self.rt.moo_none, &[], "stmt_default_none")?.into());
                            }
                            let result = self.call_rt(method_fn, &call_args, "stmt_user_result")?;
                            self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;
                            self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
                            self.builder.position_at_end(next_bb);
                        }
                        // Das DB-Builtin validiert bei keinem passenden Objekt den Receiver.
                        let fallback = self.call_rt(builtin, &[obj.into()], "stmt_builtin")?;
                        for value in &values {
                            self.call_rt_void(self.rt.moo_release, &[(*value).into()], "stmt_fallback_arg")?;
                        }
                        self.builder.build_store(result_ptr, fallback).map_err(|e| format!("{e}"))?;
                        self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
                        self.builder.position_at_end(after_bb);
                        let result = self.builder.build_load(self.mv_type(), result_ptr, "stmt_dispatch_value")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        return Ok(result);
                    }
                    "zuruecksetzen" | "zurücksetzen" | "reset" => {
                        return self.call_rt(self.rt.moo_db_stmt_reset, &[obj.into()], "stmt_reset");
                    }
                    "fertig" | "done" => {
                        return self.call_rt(self.rt.moo_thread_done, &[obj.into()], "thread_done");
                    }
                    "senden" | "send" => {
                        // P2-T114 B: k.senden(übergib p) - Besitzuebergabe eines
                        // Puffers ohne Kopie und ohne Einfrieren. p ist danach
                        // nichts; scheitert die Pruefung (Alleinbesitz), bleibt p.
                        if let Some(Expr::FunctionCall { name: u, args: ua, .. }) = args.first() {
                            if matches!(u.as_str(), "übergib" | "uebergib" | "transfer") {
                                let Some(Expr::Identifier(var)) = ua.first().filter(|_| ua.len() == 1) else {
                                    return Err("übergib erwartet genau einen Variablennamen: kanal.senden(übergib puffer)".into());
                                };
                                let val = self.load_var(var)?;
                                let nichts = self.call_rt(self.rt.moo_none, &[], "none")?;
                                self.store_var(var, nichts)?;
                                let f = self.rt_mv_lazy("moo_channel_uebergeben", 2);
                                let rest = self.call_rt(f, &[obj.into(), val.into()], "chan_uebergeben")?;
                                self.store_var(var, rest)?;
                                return self.call_rt(self.rt.moo_none, &[], "none");
                            }
                        }
                        let val = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_channel_send,
                            &[obj.into(), val.into()], "chan_send")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "empfangen" | "recv" => {
                        return self.call_rt(self.rt.moo_channel_recv, &[obj.into()], "chan_recv");
                    }
                    "schliessen" | "close" => {
                        // Tag-dispatchender close — sonst wuerde channel_close
                        // (mit pthread_mutex Code) auf Sockets/DBs/Windows
                        // landen und Memory korrumpieren (tpp.c:83 crash).
                        self.call_rt_void(self.rt.moo_smart_close,
                            &[obj.into()], "smart_close")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Webserver-Methoden
                    "web_annehmen" | "web_accept" => {
                        return self.call_rt(self.rt.moo_web_accept, &[obj.into()], "web_accept");
                    }
                    "antworten_mit_headers" | "respond_with_headers" => {
                        let body = self.compile_expr(&args[0])?;
                        let status = self.compile_expr(&args[1])?;
                        let headers = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_web_respond_with_headers,
                            &[obj.into(), body.into(), status.into(), headers.into()], "respond_h");
                    }
                    "json_antworten_mit_headers" | "json_respond_with_headers" => {
                        let data = self.compile_expr(&args[0])?;
                        let status = self.compile_expr(&args[1])?;
                        let headers = self.compile_expr(&args[2])?;
                        return self.call_rt(self.rt.moo_web_json_with_headers,
                            &[obj.into(), data.into(), status.into(), headers.into()], "json_h");
                    }
                    "antworten" | "respond" => {
                        let body = self.compile_expr(&args[0])?;
                        let status = if args.len() > 1 {
                            self.compile_expr(&args[1])?
                        } else {
                            self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(200.0).into()], "s200")?
                        };
                        return self.call_rt(self.rt.moo_web_respond, &[obj.into(), body.into(), status.into()], "respond");
                    }
                    "json_antworten" | "json_respond" => {
                        let data = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_web_json, &[obj.into(), data.into()], "json");
                    }
                    "starten" | "start" => {
                        // Alias fuer web_accept Loop — hier nur accept fuer einen Request
                        return self.call_rt(self.rt.moo_web_accept, &[obj.into()], "start");
                    }
                    // Socket-Methoden
                    "annehmen" | "accept" => {
                        return self.call_rt(self.rt.moo_socket_accept, &[obj.into()], "accept");
                    }
                    "lesen" | "read" => {
                        let max = if args.is_empty() {
                            self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(4096.0).into()], "max")?
                        } else {
                            self.compile_expr(&args[0])?
                        };
                        return self.call_rt(self.rt.moo_socket_read, &[obj.into(), max.into()], "read");
                    }
                    "schreiben" | "write" => {
                        let data = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_socket_write, &[obj.into(), data.into()], "write")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "lesen_bytes" | "read_bytes" => {
                        let max = if args.is_empty() {
                            self.call_rt(self.rt.moo_number, &[self.context.f64_type().const_float(4096.0).into()], "max")?
                        } else {
                            self.compile_expr(&args[0])?
                        };
                        return self.call_rt(self.rt.moo_socket_read_bytes, &[obj.into(), max.into()], "read_bytes");
                    }
                    "schreiben_bytes" | "schreibe_bytes" | "write_bytes" => {
                        let data = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_socket_write_bytes, &[obj.into(), data.into()], "write_bytes")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "timeout_setzen" | "set_timeout" => {
                        let ms = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_socket_set_timeout, &[obj.into(), ms.into()], "set_timeout")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "udp_verbinden" | "udp_connect" => {
                        let host = self.compile_expr(&args[0])?;
                        let port = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_udp_connect, &[obj.into(), host.into(), port.into()], "udp_connect")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "map" | "abbilden" => {
                        // list.map((x) => expr) — create new list, iterate, apply lambda, append
                        let lambda = &args[0];
                        let obj_ptr = self.builder.build_alloca(self.mv_type(), "map_list")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(obj_ptr, obj).map_err(|e| format!("{e}"))?;

                        let len_result = self.call_rt_abi(self.rt.moo_list_iter_len,
                            &[obj.into()], "len")
                            .map_err(|e| format!("{e}"))?;
                        let len = match len_result.try_as_basic_value() {
                            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                            _ => return Err("iter_len fehlgeschlagen".to_string()),
                        };

                        let result = self.call_rt(self.rt.moo_list_new,
                            &[self.context.i32_type().const_int(0, false).into()], "map_result")?;
                        let result_ptr = self.builder.build_alloca(self.mv_type(), "map_result_ptr")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;

                        let i32_type = self.context.i32_type();
                        let idx_ptr = self.builder.build_alloca(i32_type, "map_idx")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
                            .map_err(|e| format!("{e}"))?;

                        let func = self.current_function.unwrap();
                        // Nativer Funktionswert mit Fehler: Schleife abbrechen, danach
                        // wie der native Direktaufruf ohne Ersatzwert verlassen.
                        let nw_merker = if !matches!(lambda, Expr::Lambda { .. }) && self.nativer_wert_ausgang_vorhanden() {
                            let merker = self.builder.build_alloca(self.context.bool_type(), "map_nw_abbruch")
                                .map_err(|e| format!("{e}"))?;
                            self.builder.build_store(merker, self.context.bool_type().const_zero())
                                .map_err(|e| format!("{e}"))?;
                            Some(merker)
                        } else {
                            None
                        };
                        let cond_bb = self.context.append_basic_block(func, "map_cond");
                        let body_bb = self.context.append_basic_block(func, "map_body");
                        let after_bb = self.context.append_basic_block(func, "map_after");

                        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(cond_bb);
                        let current_idx = self.builder.build_load(i32_type, idx_ptr, "idx")
                            .map_err(|e| format!("{e}"))?.into_int_value();
                        let cmp = self.builder.build_int_compare(
                            inkwell::IntPredicate::SLT, current_idx, len, "map_cmp")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_conditional_branch(cmp, body_bb, after_bb)
                            .map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(body_bb);
                        let list_loaded = self.builder.build_load(self.mv_type(), obj_ptr, "list")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
                            .map_err(|e| format!("{e}"))?.into_int_value();
                        let element = self.call_rt(self.rt.moo_list_iter_get,
                            &[list_loaded.into(), idx_loaded.into()], "elem")?;

                        // Apply callback auf element. Zwei Pfade:
                        //   * Direktes Inline-Lambda — Body wird inline kompiliert
                        //     (schneller, kein Call-Overhead, vermeidet MooFunc-Alloc).
                        //   * Beliebige MOO_FUNC-Expression (benannte Funktion,
                        //     Variable, Closure) — indirect call via moo_func_call_1.
                        if let Expr::Lambda { params, body, .. } = lambda {
                            if let Some(param) = params.first() {
                                self.store_var(param, element)?;
                            }
                            let mapped_val = self.compile_expr(body)?;
                            let current_result = self.builder.build_load(self.mv_type(), result_ptr, "res")
                                .map_err(|e| format!("{e}"))?.into_struct_value();
                            self.call_rt_void(self.rt.moo_list_append,
                                &[current_result.into(), mapped_val.into()], "append")?;
                        } else {
                            let fn_val = self.compile_expr(lambda)?;
                            let mapped_val = self.call_rt(self.rt.moo_func_call_1,
                                &[fn_val.into(), element.into()], "map_call")?;
                            let nativ_fehler = match nw_merker {
                                Some(_) => self.nativer_wert_fehler(fn_val)?,
                                None => None,
                            };
                            // compile_expr liefert den Funktionswert owning (+1, je Element
                            // neu bzw. aus der Variablen geladen); moo_func_call_1 leiht ihn.
                            self.call_rt_void(self.rt.moo_release, &[fn_val.into()], "rel_map_fn")?;
                            if let (Some(merker), Some(bedingung)) = (nw_merker, nativ_fehler) {
                                self.builder.build_store(merker, bedingung).map_err(|e| format!("{e}"))?;
                                let abbruch_bb = self.context.append_basic_block(func, "map_nw_stopp");
                                let weiter_bb = self.context.append_basic_block(func, "map_nw_weiter");
                                self.builder.build_conditional_branch(bedingung, abbruch_bb, weiter_bb)
                                    .map_err(|e| format!("{e}"))?;
                                self.builder.position_at_end(abbruch_bb);
                                self.call_rt_void(self.rt.moo_release, &[mapped_val.into()], "rel_map_nw")?;
                                self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
                                self.builder.position_at_end(weiter_bb);
                            }
                            let current_result = self.builder.build_load(self.mv_type(), result_ptr, "res")
                                .map_err(|e| format!("{e}"))?.into_struct_value();
                            self.call_rt_void(self.rt.moo_list_append,
                                &[current_result.into(), mapped_val.into()], "append")?;
                        }

                        let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
                            .map_err(|e| format!("{e}"))?.into_int_value();
                        let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
                        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(after_bb);
                        let final_result = self.builder.build_load(self.mv_type(), result_ptr, "map_final")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        if let Some(merker) = nw_merker {
                            let abbruch = self.builder.build_load(self.context.bool_type(), merker, "map_nw")
                                .map_err(|e| format!("{e}"))?.into_int_value();
                            self.nativer_wert_abbruch(abbruch, &[final_result])?;
                        }
                        return Ok(final_result);
                    }
                    "filter" | "filtern" => {
                        // list.filter((x) => cond) — create new list, iterate, test, append if truthy
                        let lambda = &args[0];
                        let obj_ptr = self.builder.build_alloca(self.mv_type(), "filter_list")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(obj_ptr, obj).map_err(|e| format!("{e}"))?;

                        let len_result = self.call_rt_abi(self.rt.moo_list_iter_len,
                            &[obj.into()], "len")
                            .map_err(|e| format!("{e}"))?;
                        let len = match len_result.try_as_basic_value() {
                            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                            _ => return Err("iter_len fehlgeschlagen".to_string()),
                        };

                        let result = self.call_rt(self.rt.moo_list_new,
                            &[self.context.i32_type().const_int(0, false).into()], "filter_result")?;
                        let result_ptr = self.builder.build_alloca(self.mv_type(), "filter_result_ptr")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;

                        let i32_type = self.context.i32_type();
                        let idx_ptr = self.builder.build_alloca(i32_type, "filter_idx")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
                            .map_err(|e| format!("{e}"))?;

                        let func = self.current_function.unwrap();
                        // Wie list.map: nativer Funktionswert mit Fehler bricht ab.
                        let nw_merker = if !matches!(lambda, Expr::Lambda { .. }) && self.nativer_wert_ausgang_vorhanden() {
                            let merker = self.builder.build_alloca(self.context.bool_type(), "filter_nw_abbruch")
                                .map_err(|e| format!("{e}"))?;
                            self.builder.build_store(merker, self.context.bool_type().const_zero())
                                .map_err(|e| format!("{e}"))?;
                            Some(merker)
                        } else {
                            None
                        };
                        let cond_bb = self.context.append_basic_block(func, "filter_cond");
                        let body_bb = self.context.append_basic_block(func, "filter_body");
                        let after_bb = self.context.append_basic_block(func, "filter_after");

                        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(cond_bb);
                        let current_idx = self.builder.build_load(i32_type, idx_ptr, "idx")
                            .map_err(|e| format!("{e}"))?.into_int_value();
                        let cmp = self.builder.build_int_compare(
                            inkwell::IntPredicate::SLT, current_idx, len, "filter_cmp")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_conditional_branch(cmp, body_bb, after_bb)
                            .map_err(|e| format!("{e}"))?;

                        self.builder.position_at_end(body_bb);
                        let list_loaded = self.builder.build_load(self.mv_type(), obj_ptr, "list")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
                            .map_err(|e| format!("{e}"))?.into_int_value();
                        let element = self.call_rt(self.rt.moo_list_iter_get,
                            &[list_loaded.into(), idx_loaded.into()], "elem")?;

                        // Apply callback. Analog zu list.map: Inline-Lambda
                        // direkt, beliebige MOO_FUNC-Expression via
                        // moo_func_call_1.
                        let test_val = if let Expr::Lambda { params, body, .. } = lambda {
                            if let Some(param) = params.first() {
                                self.store_var(param, element)?;
                            }
                            self.compile_expr(body)?
                        } else {
                            let fn_val = self.compile_expr(lambda)?;
                            let test_val = self.call_rt(self.rt.moo_func_call_1,
                                &[fn_val.into(), element.into()], "filter_call")?;
                            let nativ_fehler = match nw_merker {
                                Some(_) => self.nativer_wert_fehler(fn_val)?,
                                None => None,
                            };
                            // Wie bei list.map: Funktionswert (+1) nach dem Aufruf freigeben.
                            self.call_rt_void(self.rt.moo_release, &[fn_val.into()], "rel_filter_fn")?;
                            if let (Some(merker), Some(bedingung)) = (nw_merker, nativ_fehler) {
                                self.builder.build_store(merker, bedingung).map_err(|e| format!("{e}"))?;
                                let abbruch_bb = self.context.append_basic_block(func, "filter_nw_stopp");
                                let weiter_bb = self.context.append_basic_block(func, "filter_nw_weiter");
                                self.builder.build_conditional_branch(bedingung, abbruch_bb, weiter_bb)
                                    .map_err(|e| format!("{e}"))?;
                                self.builder.position_at_end(abbruch_bb);
                                self.call_rt_void(self.rt.moo_release, &[test_val.into()], "rel_filter_nw")?;
                                self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
                                self.builder.position_at_end(weiter_bb);
                            }
                            test_val
                        };
                        {
                            let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                                &[test_val.into()], "truthy")
                                .map_err(|e| format!("{e}"))?
                                .try_as_basic_value();
                            let cond_bool = match is_true {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("truthy fehlgeschlagen".to_string()),
                            };

                            let append_bb = self.context.append_basic_block(func, "filter_append");
                            let skip_bb = self.context.append_basic_block(func, "filter_skip");
                            self.builder.build_conditional_branch(cond_bool, append_bb, skip_bb)
                                .map_err(|e| format!("{e}"))?;

                            // Append element if condition is true
                            self.builder.position_at_end(append_bb);
                            // Re-load element since we need it after the branch
                            let elem_reload = self.call_rt(self.rt.moo_list_iter_get,
                                &[list_loaded.into(), idx_loaded.into()], "elem_reload")?;
                            let current_result = self.builder.build_load(self.mv_type(), result_ptr, "res")
                                .map_err(|e| format!("{e}"))?.into_struct_value();
                            self.call_rt_void(self.rt.moo_list_append,
                                &[current_result.into(), elem_reload.into()], "append")?;
                            let idx_now_a = self.builder.build_load(i32_type, idx_ptr, "idx")
                                .map_err(|e| format!("{e}"))?.into_int_value();
                            let idx_next_a = self.builder.build_int_add(idx_now_a, i32_type.const_int(1, false), "idx_next")
                                .map_err(|e| format!("{e}"))?;
                            self.builder.build_store(idx_ptr, idx_next_a).map_err(|e| format!("{e}"))?;
                            self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                            // Skip: just increment
                            self.builder.position_at_end(skip_bb);
                            let idx_now_s = self.builder.build_load(i32_type, idx_ptr, "idx")
                                .map_err(|e| format!("{e}"))?.into_int_value();
                            let idx_next_s = self.builder.build_int_add(idx_now_s, i32_type.const_int(1, false), "idx_next")
                                .map_err(|e| format!("{e}"))?;
                            self.builder.build_store(idx_ptr, idx_next_s).map_err(|e| format!("{e}"))?;
                            self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;
                        }

                        self.builder.position_at_end(after_bb);
                        let final_result = self.builder.build_load(self.mv_type(), result_ptr, "filter_final")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        if let Some(merker) = nw_merker {
                            let abbruch = self.builder.build_load(self.context.bool_type(), merker, "filter_nw")
                                .map_err(|e| format!("{e}"))?.into_int_value();
                            self.nativer_wert_abbruch(abbruch, &[final_result])?;
                        }
                        return Ok(final_result);
                    }
                    // Event-Methoden: obj.bei("klick", cb) / obj.on("click", cb)
                    "bei" | "on" => {
                        let event = self.compile_expr(&args[0])?;
                        let cb = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_event_on,
                            &[obj.into(), event.into(), cb.into()], "ev_on")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "auslösen" | "emit" => {
                        let event = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_event_emit,
                            &[obj.into(), event.into()], "ev_emit")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    _ => {
                        // Klassen-Methode aufrufen: suche nach class__method.
                        // User-Funktionen RELEASEN ihre Params im Epilog
                        // (release_function_locals) — der selbst-Arg wird also
                        // KONSUMIERT. Der Slot haelt den load-Ref fuer sein
                        // eigenes Cleanup, deshalb braucht GENAU dieser Pfad
                        // ein zusaetzliches retain auf den Receiver (Builtin-
                        // Arme borrowen und brauchen keins — das alte globale
                        // retain_for_slot leakte dort +1 pro Call).
                        self.call_rt_void(self.rt.moo_retain, &[obj.into()], "retain_selbst_arg")?;
                        let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                        // Der zusaetzliche Receiver-Ref und alle bisherigen
                        // Argumente werden erst beim erfolgreichen Aufruf
                        // an die Methode uebertragen.
                        let mut owned = vec![obj];
                        for a in args {
                            let value = self.compile_defer_expr(a, &owned)?;
                            owned.push(value);
                            call_args.push(value.into());
                        }

                        // Statische Klasse des Receivers bestimmen:
                        //   - Expr::Identifier: in object_var_types nachschauen
                        //   - Expr::This: current_class
                        let static_class: Option<String> = match object.as_ref() {
                            Expr::Identifier(name) => self.object_var_types.get(name).cloned(),
                            Expr::This => self.current_class.clone(),
                            _ => None,
                        };

                        if let Some(mut cls) = static_class {
                            // Parent-Chain walken bis Methode gefunden
                            loop {
                                if let Some(func) = self.class_methods.get(&(cls.clone(), method.clone())).copied() {
                                    let mut local_args = call_args.clone();
                                    let expected_params = func.count_params() as usize;
                                    if local_args.len() > expected_params {
                                        local_args.truncate(expected_params);
                                    } else {
                                        while local_args.len() < expected_params {
                                            let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                                            local_args.push(none_val.into());
                                        }
                                    }
                                    return self.call_rt(func, &local_args, "method_call");
                                }
                                match self.class_parents.get(&cls).cloned() {
                                    Some(p) => cls = p,
                                    None => break,
                                }
                            }
                        }

                        // Dynamic Dispatch: statische Klasse unbekannt (z.B. bei
                        // Property-Access oder Funktions-Parametern). Runtime-
                        // Kaskade: moo_object_class_name() holen, gegen alle
                        // bekannten Klassen vergleichen, in den passenden Call
                        // branchen. Nutzt Alloca + Load statt PHI fuer Lesbarkeit.
                        let mut candidates: Vec<(String, FunctionValue<'ctx>)> =
                            self.class_methods.iter()
                                .filter(|((_, m), _)| m == method)
                                .map(|((c, _), f)| (c.clone(), *f))
                                .collect();
                        candidates.sort_by(|a, b| a.0.cmp(&b.0));

                        if !candidates.is_empty() {
                            let func_ctx = self.current_function.unwrap();
                            // Alloca fuer das Ergebnis
                            let result_ptr = self.builder.build_alloca(self.mv_type(), "dispatch_result")
                                .map_err(|e| format!("{e}"))?;
                            // Runtime: const char* class_name_ptr = moo_object_class_name(obj)
                            let class_ptr_call = self.call_rt_abi(
                                self.rt.moo_object_class_name, &[obj.into()], "obj_class")
                                .map_err(|e| format!("{e}"))?;
                            let class_ptr = match class_ptr_call.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_pointer_value(),
                                _ => return Err("moo_object_class_name hat keinen Wert".to_string()),
                            };

                            let after_bb = self.context.append_basic_block(func_ctx, "dispatch_after");
                            let strcmp_fn = self.module.get_function("strcmp")
                                .unwrap_or_else(|| {
                                    let i32_t = self.context.i32_type();
                                    let ptr_t = self.context.ptr_type(inkwell::AddressSpace::default());
                                    let fn_t = i32_t.fn_type(&[ptr_t.into(), ptr_t.into()], false);
                                    self.module.add_function("strcmp", fn_t, None)
                                });

                            for (idx, (class_name, func)) in candidates.iter().enumerate() {
                                let is_last = idx + 1 == candidates.len();
                                let name_gv = self.make_global_str(class_name, &format!("cls_name_{idx}"))?;
                                let cmp_call = self.builder.build_call(
                                    strcmp_fn,
                                    &[class_ptr.into(), name_gv.into()],
                                    "strcmp_res",
                                ).map_err(|e| format!("{e}"))?;
                                let cmp_val = match cmp_call.try_as_basic_value() {
                                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                    _ => return Err("strcmp hat keinen Wert".to_string()),
                                };
                                let is_match = self.builder.build_int_compare(
                                    inkwell::IntPredicate::EQ, cmp_val,
                                    self.context.i32_type().const_int(0, false), "cls_eq",
                                ).map_err(|e| format!("{e}"))?;

                                let call_bb = self.context.append_basic_block(func_ctx, "dispatch_call");
                                let next_bb = if is_last {
                                    // Fallback: letzter Call wenn keine Klasse matcht.
                                    call_bb
                                } else {
                                    self.context.append_basic_block(func_ctx, "dispatch_next")
                                };

                                if is_last {
                                    self.builder.build_unconditional_branch(call_bb)
                                        .map_err(|e| format!("{e}"))?;
                                } else {
                                    self.builder.build_conditional_branch(is_match, call_bb, next_bb)
                                        .map_err(|e| format!("{e}"))?;
                                }

                                self.builder.position_at_end(call_bb);
                                let mut local_args = call_args.clone();
                                let expected_params = func.count_params() as usize;
                                if local_args.len() > expected_params {
                                    local_args.truncate(expected_params);
                                } else {
                                    while local_args.len() < expected_params {
                                        let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                                        local_args.push(none_val.into());
                                    }
                                }
                                let result = self.call_rt(*func, &local_args, "disp_call")?;
                                self.builder.build_store(result_ptr, result)
                                    .map_err(|e| format!("{e}"))?;
                                self.builder.build_unconditional_branch(after_bb)
                                    .map_err(|e| format!("{e}"))?;

                                if !is_last {
                                    self.builder.position_at_end(next_bb);
                                }
                            }

                            self.builder.position_at_end(after_bb);
                            let result_val = self.builder.build_load(self.mv_type(), result_ptr, "disp_load")
                                .map_err(|e| format!("{e}"))?;
                            return Ok(result_val.into_struct_value());
                        }
                        // UFCS (Nim-inspiriert): 5.quadrat() → quadrat(5)
                        // Versuche als globale Funktion mit obj als erstem Argument
                        let ufcs_name = self.lambda_names.get(method)
                            .cloned()
                            .unwrap_or(method.clone());
                        if let Some(ufcs_fn) = self.module.get_function(&ufcs_name) {
                            // Closure-Captures anhaengen wenn vorhanden
                            if let Some(captures) = self.lambda_captures.get(&ufcs_name).cloned() {
                                for var_name in &captures {
                                    if let Ok(val) = self.load_var(var_name) {
                                        call_args.push(val.into());
                                    } else {
                                        let none_val = self.call_rt(self.rt.moo_none, &[], "cap_none")?;
                                        call_args.push(none_val.into());
                                    }
                                }
                            }
                            let expected = ufcs_fn.count_params() as usize;
                            while call_args.len() < expected {
                                let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                                call_args.push(none_val.into());
                            }
                            return self.call_rt(ufcs_fn, &call_args, "ufcs_call");
                        }
                        // Vorschlag fuer aehnliche Methoden
                        let known: Vec<&str> = vec![
                            "append", "hinzufügen", "length", "länge", "pop",
                            "reverse", "umkehren", "sort", "sortieren", "join", "verbinden",
                            "contains", "enthält", "keys", "schlüssel", "has", "hat",
                            "upper", "gross", "lower", "klein", "trim", "trimmen",
                            "split", "teilen", "replace", "ersetzen", "bei", "on",
                            "auslösen", "emit", "warten", "wait",
                        ];
                        let suggestion = known.iter()
                            .filter(|k| levenshtein(method, k) <= 2)
                            .min_by_key(|k| levenshtein(method, k));
                        let d = match suggestion {
                            Some(s) => crate::diagnose::Diagnose::fehler("M-C002",
                                format!("Methode '{method}' nicht gefunden. Meintest du '{s}'?"))
                                .vorschlag(format!("Meintest du '{s}'?")),
                            None => crate::diagnose::Diagnose::fehler("M-C002", format!("Methode '{method}' nicht gefunden.")),
                        };
                        return Err(self.diag_fehler(d.gefunden(method.to_string())));
                    }
                }
            }
            Expr::PropertyAccess { object, property } => {
                let obj = self.compile_defer_expr(object, &[])?;
                // P2-T49 (Befund LSan/RSS): obj ist ein +1-Temp (load_var-retain bzw.
                // Zwischenergebnis einer Feldkette) und wurde nie freigegeben - Leck
                // je Feldzugriff (1M `d.n`: RSS 23 MB -> 536 MB). Ergebnis von
                // moo_length/moo_object_get ist eigenstaendig (+1; eine Struktur-Sicht
                // haelt ihren Besitzer selbst), danach obj freigeben.
                // Eingebaute Properties
                match property.as_str() {
                    "length" | "länge" => {
                        // Generische Laenge: String->length, List->length, Dict->count.
                        // (Vorher hart moo_list_length -> 0 fuer Strings/Dicts.)
                        let laenge = self.call_rt(self.rt.moo_length, &[obj.into()], "len")?;
                        self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_prop_obj")?;
                        Ok(laenge)
                    }
                    _ => {
                        let prop_str = self.make_global_str(property, "prop")?;
                        let wert = self.call_rt(self.rt.moo_object_get,
                            &[obj.into(), prop_str.into()], "prop_get")?;
                        self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_prop_obj")?;
                        Ok(wert)
                    }
                }
            }
            Expr::OptionalChain { object, property } => {
                let obj = self.compile_defer_expr(object, &[])?;
                let func = self.current_function.unwrap();

                // Prüfe ob obj none ist
                let is_none_result = self.call_rt_abi(self.rt.moo_is_none,
                    &[obj.into()], "is_none")
                    .map_err(|e| format!("{e}"))?
                    .try_as_basic_value();
                let is_none = match is_none_result {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("moo_is_none hat keinen Wert zurueckgegeben".to_string()),
                };

                let then_bb = self.context.append_basic_block(func, "opt_none");
                let else_bb = self.context.append_basic_block(func, "opt_access");
                let merge_bb = self.context.append_basic_block(func, "opt_merge");

                self.builder.build_conditional_branch(is_none, then_bb, else_bb)
                    .map_err(|e| format!("{e}"))?;

                // None-Pfad
                self.builder.position_at_end(then_bb);
                let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                let then_end = self.builder.get_insert_block().unwrap();

                // Access-Pfad
                self.builder.position_at_end(else_bb);
                let prop_str = self.make_global_str(property, "opt_prop")?;
                let prop_val = self.call_rt(self.rt.moo_object_get,
                    &[obj.into(), prop_str.into()], "opt_get")?;
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                let else_end = self.builder.get_insert_block().unwrap();

                // Merge mit phi
                self.builder.position_at_end(merge_bb);
                let phi = self.builder.build_phi(self.mv_type(), "opt_result")
                    .map_err(|e| format!("{e}"))?;
                phi.add_incoming(&[
                    (&none_val as &dyn BasicValue, then_end),
                    (&prop_val as &dyn BasicValue, else_end),
                ]);
                // P2-T49: wie PropertyAccess das +1-Objekt-Temp freigeben (nichts: no-op).
                self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_opt_obj")?;
                Ok(phi.as_basic_value().into_struct_value())
            }
            Expr::NullishCoalesce { left, right } => {
                let lhs = self.compile_defer_expr(left, &[])?;
                let func = self.current_function.unwrap();

                let is_none_result = self.call_rt_abi(self.rt.moo_is_none,
                    &[lhs.into()], "is_none")
                    .map_err(|e| format!("{e}"))?
                    .try_as_basic_value();
                let is_none = match is_none_result {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("moo_is_none hat keinen Wert zurueckgegeben".to_string()),
                };

                let then_bb = self.context.append_basic_block(func, "nc_none");
                let else_bb = self.context.append_basic_block(func, "nc_keep");
                let merge_bb = self.context.append_basic_block(func, "nc_merge");

                self.builder.build_conditional_branch(is_none, then_bb, else_bb)
                    .map_err(|e| format!("{e}"))?;

                // None -> rechte Seite
                self.builder.position_at_end(then_bb);
                let rhs = self.compile_defer_expr(right, &[lhs])?;
                if self.defer_runtime {
                    self.call_rt_void(self.rt.moo_release, &[lhs.into()], "rel_nullish_none")?;
                }
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                let then_end = self.builder.get_insert_block().unwrap();

                // Nicht None -> linke Seite behalten
                self.builder.position_at_end(else_bb);
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                let else_end = self.builder.get_insert_block().unwrap();

                // Merge
                self.builder.position_at_end(merge_bb);
                let phi = self.builder.build_phi(self.mv_type(), "nc_result")
                    .map_err(|e| format!("{e}"))?;
                phi.add_incoming(&[
                    (&rhs as &dyn BasicValue, then_end),
                    (&lhs as &dyn BasicValue, else_end),
                ]);
                Ok(phi.as_basic_value().into_struct_value())
            }
            Expr::IndexAccess { object, index } => {
                let guard_index = (std::mem::take(&mut self.stmt_root_guard) || self.stmt_recursive_guard)
                    && !matches!(index.as_ref(), Expr::Range { .. });
                let obj = self.compile_defer_expr(object, &[])?;
                if guard_index { self.guard_local_expr_error(&[obj])?; }
                // String-Slicing: obj[start..end] -> moo_string_slice(obj, start, end)
                let result = if let Expr::Range { start, end } = index.as_ref() {
                    let s = self.compile_defer_expr(start, &[obj])?;
                    let e = self.compile_defer_expr(end, &[obj, s])?;
                    let slice_res = self.call_rt(self.rt.moo_string_slice,
                        &[obj.into(), s.into(), e.into()], "str_slice")?;
                    self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_s")?;
                    self.call_rt_void(self.rt.moo_release, &[e.into()], "rel_e")?;
                    slice_res
                } else {
                    if self.native_value_temps() { self.stmt_owned_context.push(obj); }
                    let idx_result = self.compile_expr(index);
                    if self.native_value_temps() { self.stmt_owned_context.pop(); }
                    let idx = idx_result?;
                    if guard_index { self.guard_local_expr_error(&[obj, idx])?; }
                    // idx ist Caller-Ref: moo_index_get (→ dict_get/list_get)
                    // nutzt idx intern, ownership wird NICHT transferiert.
                    // String/List release-idx nach Verwendung waere hier noch offen;
                    // dict_get released selbst. Fuer Minimalimpact nur obj freigeben.
                    self.call_rt(self.rt.moo_index_get,
                        &[obj.into(), idx.into()], "idx_get")?
                };
                // Container nur gelesen → load-Temp releasen damit Container
                // nicht pro Lookup einen Refcount verliert/gewinnt.
                self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_idx_obj")?;
                Ok(result)
            }
            Expr::Range { start, end } => {
                let s = self.compile_expr(start)?;
                let e = self.compile_expr(end)?;
                let range_res = self.call_rt(self.rt.moo_range, &[s.into(), e.into()], "range")?;
                self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_s")?;
                self.call_rt_void(self.rt.moo_release, &[e.into()], "rel_e")?;
                Ok(range_res)
            }
            Expr::List(elements) => {
                let guard_elements = (std::mem::take(&mut self.stmt_root_guard) || self.stmt_recursive_guard)
                    && elements.iter().all(|element| !matches!(element, Expr::Spread(_)) );
                let list = self.call_rt(self.rt.moo_list_new,
                    &[self.context.i32_type().const_int(0, false).into()],
                    "list")?;
                let list_ptr = self.entry_alloca(self.mv_type(), "list_tmp")?;
                self.builder.build_store(list_ptr, list).map_err(|e| format!("{e}"))?;

                for elem in elements {
                    if let Expr::Spread(inner) = elem {
                        // Spread: source-Liste iterieren und jedes Element appenden
                        let src = self.compile_defer_expr(inner, &[list])?;
                        self.compile_spread_into_list(list_ptr, src)?;
                        if self.defer_runtime { self.guard_local_expr_error(&[list])?; }
                    } else {
                        if self.native_value_temps() { self.stmt_owned_context.push(list); }
                        let val_result = self.compile_expr(elem);
                        if self.native_value_temps() { self.stmt_owned_context.pop(); }
                        let val = val_result?;
                        if guard_elements { self.guard_local_expr_error(&[list, val])?; }
                        let current_list = self.builder.build_load(self.mv_type(), list_ptr, "list")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        self.call_rt_void(self.rt.moo_list_append,
                            &[current_list.into(), val.into()], "append")?;
                        // Bei Fehler hat moo_list_append val nicht uebernommen.
                        if guard_elements { self.guard_local_expr_error(&[list, val])?; }
                    }
                }

                let final_list = self.builder.build_load(self.mv_type(), list_ptr, "list")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                Ok(final_list)
            }
            Expr::ListComprehension { expr, var_name, iterable, condition } => {
                if self.defer_runtime { self.defer_local_slot(var_name)?; }
                // Create empty result list
                let result_list = self.call_rt(self.rt.moo_list_new,
                    &[self.context.i32_type().const_int(0, false).into()], "comp_list")?;
                let result_ptr = self.entry_alloca(self.mv_type(), "comp_list_ptr")?;
                self.builder.build_store(result_ptr, result_list).map_err(|e| format!("{e}"))?;

                // Compile iterable and get length
                let list_val = self.compile_defer_expr(iterable, &[result_list])?;
                let list_ptr = self.entry_alloca(self.mv_type(), "comp_iter_ptr")?;
                self.builder.build_store(list_ptr, list_val).map_err(|e| format!("{e}"))?;

                let len_result = self.call_rt_abi(self.rt.moo_list_iter_len,
                    &[list_val.into()], "len")
                    .map_err(|e| format!("{e}"))?;
                let len = match len_result.try_as_basic_value() {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("iter_len fehlgeschlagen".to_string()),
                };

                let i32_type = self.context.i32_type();
                let idx_ptr = self.entry_alloca(i32_type, "comp_idx")?;
                self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
                    .map_err(|e| format!("{e}"))?;

                let func = self.current_function.unwrap();
                let cond_bb = self.context.append_basic_block(func, "comp_cond");
                let body_bb = self.context.append_basic_block(func, "comp_body");
                let after_bb = self.context.append_basic_block(func, "comp_after");

                self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                // Condition: idx < len
                self.builder.position_at_end(cond_bb);
                let current_idx = self.builder.build_load(i32_type, idx_ptr, "idx")
                    .map_err(|e| format!("{e}"))?.into_int_value();
                let cmp = self.builder.build_int_compare(
                    inkwell::IntPredicate::SLT, current_idx, len, "comp_cmp")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_conditional_branch(cmp, body_bb, after_bb)
                    .map_err(|e| format!("{e}"))?;

                // Body: get element, optionally filter, compute expr, append
                self.builder.position_at_end(body_bb);
                let list_loaded = self.builder.build_load(self.mv_type(), list_ptr, "list")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
                    .map_err(|e| format!("{e}"))?.into_int_value();
                let element = self.call_rt(self.rt.moo_list_iter_get,
                    &[list_loaded.into(), idx_loaded.into()], "elem")?;
                self.store_var(var_name, element)?;

                // If there's a condition, filter
                let _append_bb = if let Some(cond_expr) = condition {
                    let filter_bb = self.context.append_basic_block(func, "comp_filter");
                    let skip_bb = self.context.append_basic_block(func, "comp_skip");
                    let cond_val = self.compile_defer_expr(cond_expr, &[result_list, list_val])?;
                    let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                        &[cond_val.into()], "truthy")
                        .map_err(|e| format!("{e}"))?
                        .try_as_basic_value();
                    let cond_bool = match is_true {
                        inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                        _ => return Err("truthy fehlgeschlagen".to_string()),
                    };
                    if self.defer_runtime {
                        self.call_rt_void(self.rt.moo_release, &[cond_val.into()], "comp.release_condition")?;
                    }
                    self.builder.build_conditional_branch(cond_bool, filter_bb, skip_bb)
                        .map_err(|e| format!("{e}"))?;

                    // Skip: just increment index
                    self.builder.position_at_end(skip_bb);
                    let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
                        .map_err(|e| format!("{e}"))?.into_int_value();
                    let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
                        .map_err(|e| format!("{e}"))?;
                    self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
                    self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                    self.builder.position_at_end(filter_bb);
                    filter_bb
                } else {
                    body_bb
                };

                // Compute expression and append to result
                let val = self.compile_defer_expr(expr, &[result_list, list_val])?;
                let current_result = self.builder.build_load(self.mv_type(), result_ptr, "res_list")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                self.call_rt_void(self.rt.moo_list_append,
                    &[current_result.into(), val.into()], "append")?;
                if self.defer_runtime { self.guard_local_expr_error(&[result_list, list_val])?; }

                // Increment index
                let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
                    .map_err(|e| format!("{e}"))?.into_int_value();
                let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
                self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(after_bb);
                let final_list = self.builder.build_load(self.mv_type(), result_ptr, "comp_result")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                if self.defer_runtime {
                    self.call_rt_void(self.rt.moo_release, &[list_val.into()], "comp.release_iterable")?;
                }
                Ok(final_list)
            }
            Expr::Dict(pairs) => {
                let dict = self.call_rt(self.rt.moo_dict_new, &[], "dict")?;
                let dict_ptr = self.entry_alloca(self.mv_type(), "dict_tmp")?;
                self.builder.build_store(dict_ptr, dict).map_err(|e| format!("{e}"))?;

                for (key, val) in pairs {
                    if let Expr::Spread(inner) = key {
                        // Spread in Dict: keys holen, iterieren, get+set
                        let src = self.compile_defer_expr(inner, &[dict])?;
                        self.compile_spread_into_dict(dict_ptr, src)?;
                        if self.defer_runtime { self.guard_local_expr_error(&[dict])?; }
                    } else {
                        let k = self.compile_defer_expr(key, &[dict])?;
                        let v = self.compile_defer_expr(val, &[dict, k])?;
                        let current_dict = self.builder.build_load(self.mv_type(), dict_ptr, "dict")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        self.call_rt_void(self.rt.moo_dict_set,
                            &[current_dict.into(), k.into(), v.into()], "dict_set")?;
                        if self.defer_runtime { self.guard_local_expr_error(&[dict])?; }
                    }
                }

                let final_dict = self.builder.build_load(self.mv_type(), dict_ptr, "dict")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                Ok(final_dict)
            }
            Expr::New { class_name, args } => {
                // Neues Objekt erstellen
                let name_ptr = self.make_global_str(class_name, "class")?;
                let obj = self.call_rt(self.rt.moo_object_new, &[name_ptr.into()], "obj")?;

                // Konstruktor aufrufen (erstelle/create). P2-T91: ohne eigenen
                // Konstruktor gilt der der naechsten Elternklasse (je Stufe
                // erstelle vor create; eigener Konstruktor hat immer Vorrang).
                let ctor_fn = {
                    let mut current = class_name.clone();
                    let mut besucht = std::collections::HashSet::new();
                    loop {
                        if !besucht.insert(current.clone()) {
                            return Err(format!("Zyklische Klassenvererbung bei {class_name}"));
                        }
                        let eigener = self.class_methods.get(&(current.clone(), "erstelle".to_string()))
                            .or_else(|| self.class_methods.get(&(current.clone(), "create".to_string())))
                            .copied();
                        if eigener.is_some() { break eigener; }
                        match self.class_parents.get(&current) {
                            Some(parent) => current = parent.clone(),
                            None => break None,
                        }
                    }
                };

                if let Some(func) = ctor_fn {
                    let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                    let mut held = vec![obj];
                    for a in args {
                        let value = self.compile_defer_expr(a, &held)?;
                        held.push(value);
                        call_args.push(value.into());
                    }
                    let expected_params = func.count_params() as usize;
                    if call_args.len() > expected_params {
                        call_args.truncate(expected_params);
                    } else {
                        while call_args.len() < expected_params {
                            let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                            call_args.push(none_val.into());
                        }
                    }
                    // Der Konstruktor konsumiert seinen Receiver; obj selbst
                    // bleibt als owning Ergebnis von `neu` erhalten.
                    self.call_rt_void(self.rt.moo_retain, &[obj.into()], "retain_ctor_receiver")?;
                    let result = self.call_rt(func, &call_args, "ctor")?;
                    self.call_rt_void(self.rt.moo_release, &[result.into()], "release_ctor_result")?;
                    if self.defer_runtime { self.guard_local_expr_error(&[obj])?; }
                }

                Ok(obj)
            }
            Expr::Lambda { params, body, .. } => {
                // Lambdas als benannte Funktionen mit Closure-Captures kompilieren
                let lambda_name = format!("__lambda_{}", self.lambda_counter);
                self.lambda_counter += 1;
                let mv = self.mv_type();

                // Freie Variablen finden (Closure-Captures)
                let mut free_vars = Vec::new();
                Self::collect_free_vars(body, params, &mut free_vars);
                free_vars.sort();
                free_vars.dedup();

                // Capture-Werte VOR dem Funktionswechsel laden
                // Nur Variablen behalten die tatsaechlich existieren
                let free_vars: Vec<String> = free_vars.into_iter()
                    .filter(|v| self.variables.contains_key(v))
                    .collect();

                // Funktion: explizite params + capture params
                let total_params = params.len() + free_vars.len();
                let param_types: Vec<BasicMetadataTypeEnum> = (0..total_params)
                    .map(|_| BasicMetadataTypeEnum::from(mv))
                    .collect();
                let fn_type = mv.fn_type(&param_types, false);
                let function = self.module.add_function(&lambda_name, fn_type, None);

                // Capture-Mapping speichern fuer den Aufrufer
                if !free_vars.is_empty() {
                    self.lambda_captures.insert(lambda_name.clone(), free_vars.clone());
                }

                let entry = self.context.append_basic_block(function, "entry");
                let prev_fn = self.current_function;
                let prev_vars = self.variables.clone();
                let prev_block = self.builder.get_insert_block();
                let prev_defer_frame = self.defer_frame.take();
                let prev_defer_runtime = std::mem::replace(&mut self.defer_runtime, false);
                let prev_defer_borrowed = self.defer_borrowed_handler.take();
                // Lambda ist eine eigene Aktivierung: äußere Defers gehören
                // ausschließlich zum umgebenden Funktionsrahmen.
                let prev_defers = std::mem::take(&mut self.defer_stack);
                let prev_catalog = std::mem::take(&mut self.defer_catalog);
                // P2-T85 (Plan 983e8a9c A1/E5): Lambda = Standardrahmen ohne
                // Pruefpunkt-Ausgang; ein fangbarer nativer Aufruf ist nur als
                // ganzer Koerper erlaubt (sonst liefe der Ausdruck nach dem Fehler
                // mit nichts weiter -> Erstfehler-Verlust).
                let koerper: &Expr = body;
                {
                    let mut calls = std::collections::HashSet::new();
                    Self::collect_defer_calls_expr(koerper, &mut calls);
                    let wurzel_ok = match koerper {
                        Expr::FunctionCall { name: wurzel, args: wurzel_args, .. } if self.native_call_fangbar(wurzel) => {
                            let mut innen = std::collections::HashSet::new();
                            for a in wurzel_args { Self::collect_defer_calls_expr(a, &mut innen); }
                            !innen.iter().any(|c| self.native_call_fangbar(c))
                        }
                        _ => false,
                    };
                    if calls.iter().any(|c| self.native_call_fangbar(c)) && !wurzel_ok {
                        return Err("Lambda: ein nativer Aufruf ist nur als ganzer Lambda-Koerper erlaubt, z.B. (x) => nativ(x); sonst als Funktion schreiben (Fehler duerfen nicht mit nichts weiterlaufen)".into());
                    }
                }
                // P2-T85 A1: Lambda wird mitten in einem aeusseren Ausdruck
                // kompiliert - dessen Guards und owned Werte bleiben draussen.
                let rahmen = self.rahmen_betreten(RahmenArt::Lambda);

                self.current_function = Some(function);
                self.builder.position_at_end(entry);
                self.variables.clear();

                // Explizite Parameter
                for (i, param_name) in params.iter().enumerate() {
                    let alloca = self.builder.build_alloca(mv, param_name)
                        .map_err(|e| format!("{e}"))?;
                    self.builder.build_store(alloca, function.get_nth_param(i as u32).unwrap())
                        .map_err(|e| format!("{e}"))?;
                    self.variables.insert(param_name.clone(), alloca);
                }

                // Capture-Parameter
                for (i, var_name) in free_vars.iter().enumerate() {
                    let param_idx = (params.len() + i) as u32;
                    let alloca = self.builder.build_alloca(mv, var_name)
                        .map_err(|e| format!("{e}"))?;
                    self.builder.build_store(alloca, function.get_nth_param(param_idx).unwrap())
                        .map_err(|e| format!("{e}"))?;
                    self.variables.insert(var_name.clone(), alloca);
                }

                let result = self.compile_expr(body)?;
                // Vor dem Return: Defers abarbeiten + alle lambda-lokalen Slots
                // freigeben (Params + Captures + Locals). Analog compile_return
                // Z.1292 / Z.1271 — ohne dies leaken pro Lambda-Call alle
                // Capture-Slots (Transfer-Semantik §3.3: Caller haendigt +1
                // an callee → callee muss beim Exit freigeben). `result`
                // haelt seine +1 als SSA-Wert unabhaengig vom Slot-Cleanup.
                self.emit_defers()?;
                self.release_function_locals()?;
                self.builder.build_return(Some(&result)).map_err(|e| format!("{e}"))?;
                self.rahmen_verlassen(rahmen);

                // Fuer Closure-Lambdas (free_vars non-empty): Trampoline-Function
                // nach der Inner-Function generieren. Signatur des Trampoline:
                //   (MooFunc* env, MooValue p0, ..., MooValue pk) -> MooValue
                // Body: fuer jedes Capture i moo_func_captured_at(env, i) laden,
                // dann inner(p0, ..., pk, cap_0, ..., cap_m) aufrufen.
                let tramp_opt: Option<FunctionValue<'ctx>> = if !free_vars.is_empty() {
                    let ptr_type = self.context.ptr_type(AddressSpace::default());
                    let mut tramp_param_types: Vec<BasicMetadataTypeEnum> = Vec::new();
                    tramp_param_types.push(ptr_type.into());
                    for _ in 0..params.len() {
                        tramp_param_types.push(mv.into());
                    }
                    let tramp_fn_type = mv.fn_type(&tramp_param_types, false);
                    let tramp_name = format!("{lambda_name}_tramp");
                    let tramp = self.module.add_function(&tramp_name, tramp_fn_type, None);
                    let tramp_entry = self.context.append_basic_block(tramp, "entry");
                    self.current_function = Some(tramp);
                    self.builder.position_at_end(tramp_entry);
                    // P2-T85 A1: Trampolin = Sonstig (kein Standardrahmen).
                    let rahmen_tramp = self.rahmen_betreten(RahmenArt::Sonstig);

                    // Argumente fuer inner-Aufruf: erst user-Params, dann Captures
                    let mut inner_args: Vec<BasicMetadataValueEnum> = Vec::new();
                    for i in 0..params.len() {
                        let p = tramp.get_nth_param((i + 1) as u32).unwrap();
                        inner_args.push(p.into());
                    }
                    let env_arg = tramp.get_nth_param(0).unwrap();
                    for i in 0..free_vars.len() {
                        let i_val = self.context.i32_type().const_int(i as u64, false);
                        let cap = self.call_rt(self.rt.moo_func_captured_at,
                            &[env_arg.into(), i_val.into()],
                            &format!("cap_{i}"))?;
                        // moo_func_captured_at liefert BORROWED ref. Inner-Funktion
                        // released ihren Param am Frame-Ende (release_function_locals).
                        // Ohne extra retain hier: jeder Closure-Aufruf sinkt den
                        // captured-Refcount um 1 -> nach 2-3 Aufrufen freed -> SIGSEGV
                        // (siehe regression/test_closure_multi_call.moos).
                        self.call_rt_void(self.rt.moo_retain,
                            &[cap.into()],
                            &format!("retain_cap_{i}"))?;
                        inner_args.push(cap.into());
                    }
                    let call_site = self.builder.build_call(function, &inner_args, "inner_call")
                        .map_err(|e| format!("{e}"))?;
                    let ret = match call_site.try_as_basic_value() {
                        inkwell::values::ValueKind::Basic(v) => v,
                        _ => return Err("Lambda-Trampoline: inner call ohne Return".into()),
                    };
                    self.builder.build_return(Some(&ret))
                        .map_err(|e| format!("{e}"))?;
                    self.rahmen_verlassen(rahmen_tramp);
                    Some(tramp)
                } else {
                    None
                };

                self.current_function = prev_fn;
                self.variables = prev_vars;
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }

                self.defer_frame = prev_defer_frame;
                self.defer_runtime = prev_defer_runtime;
                self.defer_borrowed_handler = prev_defer_borrowed;
                self.defer_stack = prev_defers;
                self.defer_catalog = prev_catalog;

                // Lambda als First-Class-Value verpacken.
                if free_vars.is_empty() {
                    // Kein Capture-Environment — direkter MOO_FUNC-Wrapper.
                    let callable_fn = self.windows_callable_wrapper(function, &lambda_name)?;
                    let fn_ptr = callable_fn.as_global_value().as_pointer_value();
                    let arity = self.context.i32_type()
                        .const_int(params.len() as u64, false);
                    let name_ptr = self.make_global_str(&lambda_name,
                        &format!("__lname_{lambda_name}"))?;
                    self.call_rt(self.rt.moo_func_new,
                        &[fn_ptr.into(), arity.into(), name_ptr.into()],
                        "lambda_val")
                } else {
                    // Closure: Captures in Stack-Array packen, moo_func_with_captures.
                    let tramp = tramp_opt.expect("Trampoline oben erzeugt");
                    let n = free_vars.len();
                    let caps_array_type = mv.array_type(n as u32);
                    let caps_alloca = self.builder.build_alloca(caps_array_type, "caps_arr")
                        .map_err(|e| format!("{e}"))?;
                    for (i, var_name) in free_vars.iter().enumerate() {
                        let cap_val = self.load_var(var_name)?;
                        let elem_ptr = unsafe {
                            self.builder.build_in_bounds_gep(
                                caps_array_type,
                                caps_alloca,
                                &[
                                    self.context.i32_type().const_zero(),
                                    self.context.i32_type().const_int(i as u64, false),
                                ],
                                &format!("cap_slot_{i}"),
                            ).map_err(|e| format!("{e}"))?
                        };
                        self.builder.build_store(elem_ptr, cap_val)
                            .map_err(|e| format!("{e}"))?;
                    }
                    let callable_tramp = self.windows_callable_wrapper(
                        tramp,
                        &format!("{lambda_name}_tramp"),
                    )?;
                    let tramp_ptr = callable_tramp.as_global_value().as_pointer_value();
                    let arity = self.context.i32_type()
                        .const_int(params.len() as u64, false);
                    let name_ptr = self.make_global_str(&lambda_name,
                        &format!("__lname_{lambda_name}"))?;
                    let n_val = self.context.i32_type().const_int(n as u64, false);
                    self.call_rt(self.rt.moo_func_with_captures,
                        &[tramp_ptr.into(), arity.into(), name_ptr.into(),
                          caps_alloca.into(), n_val.into()],
                        "lambda_closure_val")
                }
            }
            Expr::Ternary { condition, then_val, else_val } => {
                let func = self.current_function.unwrap();
                let cond_val = self.compile_defer_expr(condition, &[])?;
                let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                    &[cond_val.into()], "tern_truthy")
                    .map_err(|e| format!("{e}"))?
                    .try_as_basic_value();
                let cond_bool = match is_true {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("ternary truthy fehlgeschlagen".to_string()),
                };
                if self.defer_runtime {
                    self.call_rt_void(self.rt.moo_release, &[cond_val.into()], "rel_ternary_condition")?;
                }

                let then_bb = self.context.append_basic_block(func, "tern_then");
                let else_bb = self.context.append_basic_block(func, "tern_else");
                let merge_bb = self.context.append_basic_block(func, "tern_merge");

                self.builder.build_conditional_branch(cond_bool, then_bb, else_bb)
                    .map_err(|e| format!("{e}"))?;

                // Then
                self.builder.position_at_end(then_bb);
                let then_result = self.compile_defer_expr(then_val, &[])?;
                let then_final_bb = self.builder.get_insert_block().unwrap();
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

                // Else
                self.builder.position_at_end(else_bb);
                let else_result = self.compile_defer_expr(else_val, &[])?;
                let else_final_bb = self.builder.get_insert_block().unwrap();
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

                // Merge mit Alloca statt Phi (einfacher mit Struct-Type)
                self.builder.position_at_end(merge_bb);
                let result_ptr = self.builder.build_alloca(self.mv_type(), "tern_res")
                    .map_err(|e| format!("{e}"))?;
                // Wir nutzen Alloca + Stores statt Phi fuer StructValues
                // Zurueck zu then/else um stores einzufuegen
                self.builder.position_at_end(then_final_bb);
                // Terminator entfernen und neu aufbauen geht nicht einfach,
                // daher nutzen wir einen anderen Ansatz: store vor branch

                // Neustart: einfacher Ansatz mit temporaerer Variable
                // (Die branch-Instruktionen sind schon gesetzt, also nutzen wir den result_ptr Trick)
                // Wir muessen die Branches entfernen und neu machen
                // Stattdessen: alloca VOR den Branches, store in jedem Block
                // Lass uns das sauberer machen:
                drop(result_ptr);
                // Entferne die Terminators
                then_final_bb.get_terminator().unwrap().erase_from_basic_block();
                else_final_bb.get_terminator().unwrap().erase_from_basic_block();

                let res_ptr = {
                    let entry = func.get_first_basic_block().unwrap();
                    let current = self.builder.get_insert_block().unwrap();
                    if let Some(first) = entry.get_first_instruction() {
                        self.builder.position_before(&first);
                    } else {
                        self.builder.position_at_end(entry);
                    }
                    let p = self.builder.build_alloca(self.mv_type(), "tern_res")
                        .map_err(|e| format!("{e}"))?;
                    self.builder.position_at_end(current);
                    p
                };

                self.builder.position_at_end(then_final_bb);
                self.builder.build_store(res_ptr, then_result).map_err(|e| format!("{e}"))?;
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(else_final_bb);
                self.builder.build_store(res_ptr, else_result).map_err(|e| format!("{e}"))?;
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(merge_bb);
                let result = self.builder.build_load(self.mv_type(), res_ptr, "tern_val")
                    .map_err(|e| format!("{e}"))?;
                Ok(result.into_struct_value())
            }
            Expr::MatchExpr { value, cases } => {
                let func = self.current_function.unwrap();
                let val = self.compile_defer_expr(value, &[])?;
                let vp = if self.defer_runtime { self.defer_hold_value(val)? } else {
                    let p = self.builder.build_alloca(self.mv_type(), "mexpr_val").map_err(|e| format!("{e}"))?;
                    self.builder.build_store(p, val).map_err(|e| format!("{e}"))?;
                    p
                };
                let rp = self.builder.build_alloca(self.mv_type(), "mexpr_res").map_err(|e| format!("{e}"))?;
                let n = self.call_rt(self.rt.moo_none, &[], "none")?;
                self.builder.build_store(rp, n).map_err(|e| format!("{e}"))?;
                let merge = self.context.append_basic_block(func, "mexpr_end");
                for (pat, guard, rexpr) in cases {
                    let cb = self.context.append_basic_block(func, "mexpr_case");
                    let nb = self.context.append_basic_block(func, "mexpr_next");
                    if let Some(pe) = pat {
                        let cv = self.builder.build_load(self.mv_type(), vp, "v").map_err(|e| format!("{e}"))?.into_struct_value();
                        if guard.is_some() {
                            if let Expr::Identifier(name) = pe {
                                if self.defer_runtime {
                                    self.defer_local_slot(name)?;
                                    self.call_rt_void(self.rt.moo_retain, &[cv.into()], "retain_mexpr_binding")?;
                                }
                                self.store_var(name, cv)?;
                                let gv = self.compile_defer_expr(guard.as_ref().unwrap(), &[])?;
                                let gr = self.call_rt_abi(self.rt.moo_is_truthy, &[gv.into()], "g").map_err(|e| format!("{e}"))?;
                                let gb = match gr.try_as_basic_value() { inkwell::values::ValueKind::Basic(v) => v.into_int_value(), _ => return Err("truthy".into()) };
                                if self.defer_runtime { self.call_rt_void(self.rt.moo_release, &[gv.into()], "rel_mexpr_guard")?; }
                                self.builder.build_conditional_branch(gb, cb, nb).map_err(|e| format!("{e}"))?;
                            } else {
                                let pv = self.compile_defer_expr(pe, &[])?;
                                let eq = self.call_rt(self.rt.moo_eq, &[cv.into(), pv.into()], "eq")?;
                            if self.defer_runtime { self.call_rt_void(self.rt.moo_release, &[pv.into()], "rel_mexpr_pattern")?; }
                                let tr = self.call_rt_abi(self.rt.moo_is_truthy, &[eq.into()], "t").map_err(|e| format!("{e}"))?;
                                let tb = match tr.try_as_basic_value() { inkwell::values::ValueKind::Basic(v) => v.into_int_value(), _ => return Err("truthy".into()) };
                                self.builder.build_conditional_branch(tb, cb, nb).map_err(|e| format!("{e}"))?;
                            }
                        } else {
                            let pv = self.compile_defer_expr(pe, &[])?;
                            let eq = self.call_rt(self.rt.moo_eq, &[cv.into(), pv.into()], "eq")?;
                            if self.defer_runtime { self.call_rt_void(self.rt.moo_release, &[pv.into()], "rel_mexpr_pattern")?; }
                            let tr = self.call_rt_abi(self.rt.moo_is_truthy, &[eq.into()], "t").map_err(|e| format!("{e}"))?;
                            let tb = match tr.try_as_basic_value() { inkwell::values::ValueKind::Basic(v) => v.into_int_value(), _ => return Err("truthy".into()) };
                            self.builder.build_conditional_branch(tb, cb, nb).map_err(|e| format!("{e}"))?;
                        }
                    } else {
                        self.builder.build_unconditional_branch(cb).map_err(|e| format!("{e}"))?;
                    }
                    self.builder.position_at_end(cb);
                    let r = self.compile_defer_expr(rexpr, &[])?;
                    self.builder.build_store(rp, r).map_err(|e| format!("{e}"))?;
                    self.builder.build_unconditional_branch(merge).map_err(|e| format!("{e}"))?;
                    self.builder.position_at_end(nb);
                }
                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    self.builder.build_unconditional_branch(merge).map_err(|e| format!("{e}"))?;
                }
                self.builder.position_at_end(merge);
                let fv = self.builder.build_load(self.mv_type(), rp, "mexpr_result").map_err(|e| format!("{e}"))?.into_struct_value();
                if self.defer_runtime { self.defer_clear_value(vp)?; }
                Ok(fv)
            }
            Expr::Pipe { left, right } => {
                if self.defer_runtime || matches!(right.as_ref(), Expr::FunctionCall { type_args, .. } if !type_args.is_empty()) {
                    // Derselbe Aufrufpfad wie f(a, b, c), einschliesslich
                    // nativer Adapter, Funktionswerte und Besitz-Fehlerkanten.
                    let (name, mut args, zeile, spalten, type_args) = match right.as_ref() {
                        Expr::FunctionCall { name, args, zeile, spalten, type_args } =>
                            (name.clone(), args.clone(), *zeile, *spalten, type_args.clone()),
                        Expr::Identifier(name) => (name.clone(), Vec::new(), 0, (0, 0), Vec::new()),
                        _ => return Err("Pipe: rechte Seite muss Funktionsaufruf oder Identifier sein".into()),
                    };
                    args.insert(0, left.as_ref().clone());
                    return self.compile_expr(&Expr::FunctionCall { name, args, zeile, spalten, type_args });
                }
                // a |> f(b, c) wird zu f(a, b, c)
                let left_val = self.compile_expr(left)?;
                match right.as_ref() {
                    Expr::FunctionCall { name, args, .. } => {
                        // Builtin-Check ueberspringen, direkt als Funktionsaufruf mit left als erstem Arg
                        let actual_name = self.lambda_names.get(name)
                            .cloned()
                            .unwrap_or(name.clone());
                        let function = self.module.get_function(&actual_name)
                            .ok_or(format!("Funktion '{name}' nicht gefunden (Pipe)"))?;
                        let mut compiled_args: Vec<BasicMetadataValueEnum> = vec![left_val.into()];
                        for a in args {
                            compiled_args.push(self.compile_expr(a)?.into());
                        }
                        if let Some(captures) = self.lambda_captures.get(&actual_name).cloned() {
                            for var_name in &captures {
                                if let Ok(val) = self.load_var(var_name) {
                                    compiled_args.push(val.into());
                                } else {
                                    let none_val = self.call_rt(self.rt.moo_none, &[], "cap_none")?;
                                    compiled_args.push(none_val.into());
                                }
                            }
                        }
                        let expected_params = function.count_params() as usize;
                        while compiled_args.len() < expected_params {
                            let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                            compiled_args.push(none_val.into());
                        }
                        self.call_rt(function, &compiled_args, "pipe_call")
                    }
                    Expr::MethodCall { .. } => {
                        // a |> obj.method(b) — left wird erstes Arg nach self
                        // Aber typischerweise: a |> method() — left als erstes Arg
                        Err(format!("Pipe in MethodCall noch nicht unterstuetzt"))
                    }
                    _ => {
                        // a |> f — f ist ein Identifier/Lambda, rufe f(a) auf
                        if let Expr::Identifier(name) = right.as_ref() {
                            let actual_name = self.lambda_names.get(name)
                                .cloned()
                                .unwrap_or(name.clone());
                            let function = self.module.get_function(&actual_name)
                                .ok_or(format!("Funktion '{name}' nicht gefunden (Pipe)"))?;
                            let mut compiled_args: Vec<BasicMetadataValueEnum> = vec![left_val.into()];
                            if let Some(captures) = self.lambda_captures.get(&actual_name).cloned() {
                                for var_name in &captures {
                                    if let Ok(val) = self.load_var(var_name) {
                                        compiled_args.push(val.into());
                                    } else {
                                        let none_val = self.call_rt(self.rt.moo_none, &[], "cap_none")?;
                                        compiled_args.push(none_val.into());
                                    }
                                }
                            }
                            let expected_params = function.count_params() as usize;
                            while compiled_args.len() < expected_params {
                                let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                                compiled_args.push(none_val.into());
                            }
                            self.call_rt(function, &compiled_args, "pipe_call")
                        } else {
                            Err("Pipe: rechte Seite muss Funktionsaufruf oder Identifier sein".to_string())
                        }
                    }
                }
            }
            Expr::Spread(_) => {
                Err("Spread-Operator (...) kann nur in Listen [...] oder Dicts {...} verwendet werden".to_string())
            }
            Expr::MatchExpr { value, cases } => {
                self.compile_match_expr(value, cases)
            }
            Expr::QueryExpr { var_name, source, where_cond, order_expr, select_expr } => {
                self.compile_query_expr(var_name, source, where_cond, order_expr, select_expr)
            }
        }
    }

    fn compile_match_expr(&mut self, value: &Expr, cases: &[(Option<Expr>, Option<Expr>, Expr)]) -> Result<StructValue<'ctx>, String> {
        let func = self.current_function.unwrap();
        let val = self.compile_expr(value)?;
        let val_ptr = self.builder.build_alloca(self.mv_type(), "match_val")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(val_ptr, val).map_err(|e| format!("{e}"))?;

        let merge_bb = self.context.append_basic_block(func, "match_expr_end");
        let result_ptr = self.builder.build_alloca(self.mv_type(), "match_result")
            .map_err(|e| format!("{e}"))?;

        for (pattern, guard, result_expr) in cases {
            if let Some(pat_expr) = pattern {
                let case_bb = self.context.append_basic_block(func, "mexpr_case");
                let next_bb = self.context.append_basic_block(func, "mexpr_next");

                let current_val = self.builder.build_load(self.mv_type(), val_ptr, "mval")
                    .map_err(|e| format!("{e}"))?.into_struct_value();

                // Binding: wenn Pattern ein Identifier ist, binde den Wert
                if let Expr::Identifier(binding_name) = pat_expr {
                    self.store_var(binding_name, current_val)?;
                }

                let cond = if let Expr::Identifier(_) = pat_expr {
                    // Binding-Pattern mit Guard
                    if let Some(guard_expr) = guard {
                        let guard_val = self.compile_expr(guard_expr)?;
                        let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                            &[guard_val.into()], "truthy")
                            .map_err(|e| format!("{e}"))?
                            .try_as_basic_value();
                        match is_true {
                            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                            _ => return Err("truthy fehlgeschlagen".to_string()),
                        }
                    } else {
                        self.context.bool_type().const_int(1, false)
                    }
                } else {
                    // Wert-Vergleich
                    let pat_val = self.compile_expr(pat_expr)?;
                    let eq_result = self.call_rt(self.rt.moo_eq,
                        &[current_val.into(), pat_val.into()], "eq")?;
                    let is_true = self.call_rt_abi(self.rt.moo_is_truthy,
                        &[eq_result.into()], "truthy")
                        .map_err(|e| format!("{e}"))?
                        .try_as_basic_value();
                    match is_true {
                        inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                        _ => return Err("truthy fehlgeschlagen".to_string()),
                    }
                };

                self.builder.build_conditional_branch(cond, case_bb, next_bb)
                    .map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(case_bb);
                let result = self.compile_expr(result_expr)?;
                self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;
                self.builder.build_unconditional_branch(merge_bb)
                    .map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(next_bb);
            } else {
                // Default
                let result = self.compile_expr(result_expr)?;
                self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;
                self.builder.build_unconditional_branch(merge_bb)
                    .map_err(|e| format!("{e}"))?;
            }
        }

        // Falls kein Default: none als Fallback
        let current_bb = self.builder.get_insert_block().unwrap();
        if current_bb.get_terminator().is_none() {
            let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
            self.builder.build_store(result_ptr, none_val).map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(merge_bb)
                .map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(merge_bb);
        let result = self.builder.build_load(self.mv_type(), result_ptr, "match_res")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        Ok(result)
    }

    /// LINQ Query: von x in quelle [wo bed] [sortiere] wähle expr
    fn compile_query_expr(&mut self, var_name: &str, source: &Expr,
                          where_cond: &Option<Box<Expr>>, order_expr: &Option<Box<Expr>>,
                          select_expr: &Expr) -> Result<StructValue<'ctx>, String> {
        let func = self.current_function.unwrap();
        let i32_type = self.context.i32_type();
        if self.defer_runtime { self.defer_local_slot(var_name)?; }
        let src_val = self.compile_defer_expr(source, &[])?;

        // Hilfsfunktion: Iteriere Liste, wende closure auf jedes Element an
        // 1. Filter (wo/where)
        let filtered = if let Some(cond) = where_cond {
            let filt = self.call_rt(self.rt.moo_list_new, &[i32_type.const_int(0, false).into()], "q_filt")?;
            let fp = self.builder.build_alloca(self.mv_type(), "qfp").map_err(|e| format!("{e}"))?;
            self.builder.build_store(fp, filt).map_err(|e| format!("{e}"))?;
            let sp = self.builder.build_alloca(self.mv_type(), "qsp").map_err(|e| format!("{e}"))?;
            self.builder.build_store(sp, src_val).map_err(|e| format!("{e}"))?;
            let lr = self.call_rt_abi(self.rt.moo_list_iter_len, &[src_val.into()], "l")
                .map_err(|e| format!("{e}"))?;
            let len = match lr.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("len".into()),
            };
            let ip = self.builder.build_alloca(i32_type, "qi").map_err(|e| format!("{e}"))?;
            self.builder.build_store(ip, i32_type.const_int(0, false)).map_err(|e| format!("{e}"))?;

            let cb = self.context.append_basic_block(func, "qf_c");
            let bb = self.context.append_basic_block(func, "qf_b");
            let ab = self.context.append_basic_block(func, "qf_a");
            self.builder.build_unconditional_branch(cb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(cb);
            let ci = self.builder.build_load(i32_type, ip, "i").map_err(|e| format!("{e}"))?.into_int_value();
            let cm = self.builder.build_int_compare(inkwell::IntPredicate::SLT, ci, len, "c")
                .map_err(|e| format!("{e}"))?;
            self.builder.build_conditional_branch(cm, bb, ab).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(bb);
            let sl = self.builder.build_load(self.mv_type(), sp, "s").map_err(|e| format!("{e}"))?.into_struct_value();
            let il = self.builder.build_load(i32_type, ip, "i").map_err(|e| format!("{e}"))?.into_int_value();
            let el = self.call_rt(self.rt.moo_list_iter_get, &[sl.into(), il.into()], "e")?;
            self.store_var(var_name, el)?;

            let cv = self.compile_defer_expr(cond, &[src_val, filt])?;
            let tr = self.call_rt_abi(self.rt.moo_is_truthy, &[cv.into()], "t")
                .map_err(|e| format!("{e}"))?;
            let tb = match tr.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("t".into()),
            };
            if self.defer_runtime {
                self.call_rt_void(self.rt.moo_release, &[cv.into()], "rel_query_condition")?;
            }
            let adb = self.context.append_basic_block(func, "qf_ad");
            let skb = self.context.append_basic_block(func, "qf_sk");
            self.builder.build_conditional_branch(tb, adb, skb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(adb);
            let el2 = self.load_var(var_name)?;
            let fl = self.builder.build_load(self.mv_type(), fp, "f").map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_list_append, &[fl.into(), el2.into()], "a")?;
            if self.defer_runtime { self.guard_local_expr_error(&[src_val, filt, el2])?; }
            self.builder.build_unconditional_branch(skb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(skb);
            let ni = self.builder.build_load(i32_type, ip, "i").map_err(|e| format!("{e}"))?.into_int_value();
            let nn = self.builder.build_int_add(ni, i32_type.const_int(1, false), "n").map_err(|e| format!("{e}"))?;
            self.builder.build_store(ip, nn).map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(cb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(ab);
            if self.defer_runtime {
                self.call_rt_void(self.rt.moo_release, &[src_val.into()], "rel_query_source")?;
            }
            self.builder.build_load(self.mv_type(), fp, "filtered").map_err(|e| format!("{e}"))?.into_struct_value()
        } else {
            src_val
        };

        // 2. Sort
        let sorted = if order_expr.is_some() {
            self.call_rt(self.rt.moo_list_sort, &[filtered.into()], "sorted")?
        } else {
            filtered
        };

        if self.defer_runtime { self.guard_local_expr_error(&[sorted])?; }

        // 3. Map (wähle/select)
        let res = self.call_rt(self.rt.moo_list_new, &[i32_type.const_int(0, false).into()], "q_res")?;
        let rp = self.builder.build_alloca(self.mv_type(), "qrp").map_err(|e| format!("{e}"))?;
        self.builder.build_store(rp, res).map_err(|e| format!("{e}"))?;
        let sp2 = self.builder.build_alloca(self.mv_type(), "qsp2").map_err(|e| format!("{e}"))?;
        self.builder.build_store(sp2, sorted).map_err(|e| format!("{e}"))?;
        let lr2 = self.call_rt_abi(self.rt.moo_list_iter_len, &[sorted.into()], "l2")
            .map_err(|e| format!("{e}"))?;
        let len2 = match lr2.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("len".into()),
        };
        let ip2 = self.builder.build_alloca(i32_type, "qi2").map_err(|e| format!("{e}"))?;
        self.builder.build_store(ip2, i32_type.const_int(0, false)).map_err(|e| format!("{e}"))?;

        let mc = self.context.append_basic_block(func, "qm_c");
        let mb2 = self.context.append_basic_block(func, "qm_b");
        let ma = self.context.append_basic_block(func, "qm_a");
        self.builder.build_unconditional_branch(mc).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(mc);
        let ci2 = self.builder.build_load(i32_type, ip2, "i").map_err(|e| format!("{e}"))?.into_int_value();
        let cm2 = self.builder.build_int_compare(inkwell::IntPredicate::SLT, ci2, len2, "c")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_conditional_branch(cm2, mb2, ma).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(mb2);
        let sl2 = self.builder.build_load(self.mv_type(), sp2, "s").map_err(|e| format!("{e}"))?.into_struct_value();
        let il2 = self.builder.build_load(i32_type, ip2, "i").map_err(|e| format!("{e}"))?.into_int_value();
        let el3 = self.call_rt(self.rt.moo_list_iter_get, &[sl2.into(), il2.into()], "e")?;
        self.store_var(var_name, el3)?;
        let mapped = self.compile_defer_expr(select_expr, &[sorted, res])?;
        let rl = self.builder.build_load(self.mv_type(), rp, "r").map_err(|e| format!("{e}"))?.into_struct_value();
        self.call_rt_void(self.rt.moo_list_append, &[rl.into(), mapped.into()], "a")?;
        if self.defer_runtime { self.guard_local_expr_error(&[sorted, res, mapped])?; }

        let ni2 = self.builder.build_load(i32_type, ip2, "i").map_err(|e| format!("{e}"))?.into_int_value();
        let nn2 = self.builder.build_int_add(ni2, i32_type.const_int(1, false), "n").map_err(|e| format!("{e}"))?;
        self.builder.build_store(ip2, nn2).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(mc).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(ma);
        if self.defer_runtime {
            self.call_rt_void(self.rt.moo_release, &[sorted.into()], "rel_query_input")?;
        }
        let fv = self.builder.build_load(self.mv_type(), rp, "q_result").map_err(|e| format!("{e}"))?.into_struct_value();
        Ok(fv)
    }

    /// Spread in Liste: iteriert source und appendet jedes Element in die Ziel-Liste
    fn compile_spread_into_list(&mut self, target_list_ptr: PointerValue<'ctx>, source: StructValue<'ctx>) -> Result<(), String> {
        let func = self.current_function.unwrap();
        let i32_type = self.context.i32_type();

        let src_ptr = self.builder.build_alloca(self.mv_type(), "spread_src")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(src_ptr, source).map_err(|e| format!("{e}"))?;

        let len_result = self.call_rt_abi(self.rt.moo_list_iter_len,
            &[source.into()], "spread_len")
            .map_err(|e| format!("{e}"))?;
        let len = match len_result.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("spread iter_len fehlgeschlagen".to_string()),
        };

        let idx_ptr = self.builder.build_alloca(i32_type, "spread_idx")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
            .map_err(|e| format!("{e}"))?;

        let cond_bb = self.context.append_basic_block(func, "spread_cond");
        let body_bb = self.context.append_basic_block(func, "spread_body");
        let after_bb = self.context.append_basic_block(func, "spread_after");

        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(cond_bb);
        let current_idx = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let cmp = self.builder.build_int_compare(
            inkwell::IntPredicate::SLT, current_idx, len, "spread_cmp")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_conditional_branch(cmp, body_bb, after_bb)
            .map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(body_bb);
        let src_loaded = self.builder.build_load(self.mv_type(), src_ptr, "src")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let element = self.call_rt(self.rt.moo_list_iter_get,
            &[src_loaded.into(), idx_loaded.into()], "spread_elem")?;
        let target = self.builder.build_load(self.mv_type(), target_list_ptr, "tgt")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        self.call_rt_void(self.rt.moo_list_append,
            &[target.into(), element.into()], "spread_append")?;
        if self.defer_runtime { self.guard_local_expr_error(&[target, source, element])?; }

        let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(after_bb);
        if self.defer_runtime { self.call_rt_void(self.rt.moo_release, &[source.into()], "rel_spread_source")?; }
        Ok(())
    }

    /// Spread in Dict: keys von source holen, iterieren, get+set in Ziel-Dict
    fn compile_spread_into_dict(&mut self, target_dict_ptr: PointerValue<'ctx>, source: StructValue<'ctx>) -> Result<(), String> {
        let func = self.current_function.unwrap();
        let i32_type = self.context.i32_type();

        let src_ptr = self.builder.build_alloca(self.mv_type(), "dspread_src")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(src_ptr, source).map_err(|e| format!("{e}"))?;

        // Keys als Liste holen
        let keys = self.call_rt(self.rt.moo_dict_keys, &[source.into()], "dspread_keys")?;
        let keys_ptr = self.builder.build_alloca(self.mv_type(), "dspread_keys_ptr")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(keys_ptr, keys).map_err(|e| format!("{e}"))?;

        let len_result = self.call_rt_abi(self.rt.moo_list_iter_len,
            &[keys.into()], "dspread_len")
            .map_err(|e| format!("{e}"))?;
        let len = match len_result.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("dict spread iter_len fehlgeschlagen".to_string()),
        };

        let idx_ptr = self.builder.build_alloca(i32_type, "dspread_idx")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
            .map_err(|e| format!("{e}"))?;

        let cond_bb = self.context.append_basic_block(func, "dspread_cond");
        let body_bb = self.context.append_basic_block(func, "dspread_body");
        let after_bb = self.context.append_basic_block(func, "dspread_after");

        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(cond_bb);
        let current_idx = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let cmp = self.builder.build_int_compare(
            inkwell::IntPredicate::SLT, current_idx, len, "dspread_cmp")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_conditional_branch(cmp, body_bb, after_bb)
            .map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(body_bb);
        // Key holen
        let keys_loaded = self.builder.build_load(self.mv_type(), keys_ptr, "keys")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let key = self.call_rt(self.rt.moo_list_iter_get,
            &[keys_loaded.into(), idx_loaded.into()], "dspread_key")?;
        // Value aus Source-Dict holen
        let src_loaded = self.builder.build_load(self.mv_type(), src_ptr, "src")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        // Lookup konsumiert seinen String-Key; der Set-Aufruf braucht
        // daneben die vom Iterator gelieferte eigene Referenz.
        if self.defer_runtime { self.call_rt_void(self.rt.moo_retain, &[key.into()], "retain_dspread_lookup_key")?; }
        let val = self.call_rt(self.rt.moo_dict_get,
            &[src_loaded.into(), key.into()], "dspread_val")?;
        // In Ziel-Dict setzen
        let target = self.builder.build_load(self.mv_type(), target_dict_ptr, "tgt")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        self.call_rt_void(self.rt.moo_dict_set,
            &[target.into(), key.into(), val.into()], "dspread_set")?;
        if self.defer_runtime { self.guard_local_expr_error(&[target, source, keys, key, val])?; }

        let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(after_bb);
        if self.defer_runtime {
            self.call_rt_void(self.rt.moo_release, &[keys.into()], "rel_dspread_keys")?;
            self.call_rt_void(self.rt.moo_release, &[source.into()], "rel_dspread_source")?;
        }
        Ok(())
    }

    /// Pre-Scan: Sammelt alle Variablennamen aus dem AST (fuer vorab-Allokation)
    /// Prueft ob ein Expression einen Identifier mit dem gegebenen Namen liest.
    /// Wird genutzt um Counter-Pattern zu erkennen ('setze X auf X + 1') —
    /// solche Stellen sollen auf das Modul-globale schreiben statt eine
    /// neue lokale Alloca anzulegen.
    /// P2-T8 (Plan b1f51d65 D1): vollstaendiger Match ohne Sammelarm, damit
    /// neue Ausdrucksknoten hier eine Entscheidung erzwingen.
    fn expr_uses_var(expr: &Expr, name: &str) -> bool {
        match expr {
            Expr::Identifier(n) => n == name,
            Expr::BinaryOp { left, right, .. }
            | Expr::Pipe { left, right }
            | Expr::NullishCoalesce { left, right } =>
                Self::expr_uses_var(left, name) || Self::expr_uses_var(right, name),
            Expr::Range { start, end } =>
                Self::expr_uses_var(start, name) || Self::expr_uses_var(end, name),
            Expr::UnaryOp { operand, .. } => Self::expr_uses_var(operand, name),
            Expr::Spread(inner) => Self::expr_uses_var(inner, name),
            // Der Callee kann ein Funktionswert in einer Variable sein (wie in
            // collect_free_vars): `cb()` liest `cb`.
            Expr::FunctionCall { name: callee, args, .. } =>
                callee == name || args.iter().any(|a| Self::expr_uses_var(a, name)),
            Expr::New { args, .. } | Expr::List(args) =>
                args.iter().any(|a| Self::expr_uses_var(a, name)),
            Expr::MethodCall { object, args, .. } =>
                Self::expr_uses_var(object, name) || args.iter().any(|a| Self::expr_uses_var(a, name)),
            Expr::PropertyAccess { object, .. } | Expr::OptionalChain { object, .. } =>
                Self::expr_uses_var(object, name),
            Expr::IndexAccess { object, index } =>
                Self::expr_uses_var(object, name) || Self::expr_uses_var(index, name),
            Expr::Ternary { condition, then_val, else_val } =>
                Self::expr_uses_var(condition, name)
                    || Self::expr_uses_var(then_val, name)
                    || Self::expr_uses_var(else_val, name),
            Expr::Dict(pairs) => pairs.iter().any(|(k, v)|
                Self::expr_uses_var(k, name) || Self::expr_uses_var(v, name)),
            Expr::MatchExpr { value, cases } =>
                Self::expr_uses_var(value, name)
                    || cases.iter().any(|(muster, waechter, ergebnis)|
                        muster.as_ref().is_some_and(|m| Self::expr_uses_var(m, name))
                            || waechter.as_ref().is_some_and(|w| Self::expr_uses_var(w, name))
                            || Self::expr_uses_var(ergebnis, name)),
            // Ein Lambda liest die aeussere Variable, sofern kein eigener
            // Parameter sie verdeckt.
            Expr::Lambda { params, body, .. } =>
                !params.iter().any(|p| p == name) && Self::expr_uses_var(body, name),
            Expr::ListComprehension { expr, var_name, iterable, condition } =>
                Self::expr_uses_var(iterable, name)
                    || (var_name != name
                        && (Self::expr_uses_var(expr, name)
                            || condition.as_ref().is_some_and(|c| Self::expr_uses_var(c, name)))),
            Expr::QueryExpr { var_name, source, where_cond, order_expr, select_expr } =>
                Self::expr_uses_var(source, name)
                    || (var_name != name
                        && (where_cond.as_ref().is_some_and(|w| Self::expr_uses_var(w, name))
                            || order_expr.as_ref().is_some_and(|o| Self::expr_uses_var(o, name))
                            || Self::expr_uses_var(select_expr, name))),
            Expr::Number(_) | Expr::NativeNumber { .. } | Expr::String(_) | Expr::Boolean(_)
            | Expr::None | Expr::This => false,
        }
    }

    /// Findet den ersten Zugriff auf 'name' im Body und sagt ob er als
    /// LOKAL behandelt werden soll. Heuristik (Python-aehnlich, "first
    /// occurrence determines scope"):
    /// - Erster Zugriff ist 'setze name auf RHS' wo RHS den Namen NICHT liest
    ///   → Some(true): bare write, lokale Variable
    /// - Erster Zugriff ist Compound oder self-referential Assignment
    ///   → Some(false): global update (Counter-Pattern)
    /// - Erster Zugriff ist ein Read im RHS einer anderen Anweisung
    ///   → Some(false): liest globalen Wert vor Schreiben
    /// - Name kommt nicht vor → None
    pub(crate) fn first_use_is_local_write(stmts: &[Stmt], name: &str) -> Option<bool> {
        for stmt in stmts {
            // Pruefe direkt-zuweisende Statements zuerst
            match stmt {
                Stmt::Assignment { name: n, value, .. } if n == name => {
                    return Some(!Self::expr_uses_var(value, name));
                }
                Stmt::ConstAssignment { name: n, value, .. } if n == name => {
                    return Some(!Self::expr_uses_var(value, name));
                }
                Stmt::CompoundAssignment { name: n, .. } if n == name => {
                    return Some(false);
                }
                _ => {}
            }
            // Sonst pruefe ob das Statement den Namen liest oder rekursiv enthaelt
            if let Some(r) = Self::stmt_first_use(stmt, name) {
                return Some(r);
            }
        }
        None
    }

    fn stmt_first_use(stmt: &Stmt, name: &str) -> Option<bool> {
        match stmt {
            Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
            | Stmt::Show(value, _) | Stmt::Throw(value, _) | Stmt::Return(Some(value), _)
            | Stmt::Expression(value, _) | Stmt::CompoundAssignment { value, .. } => {
                if Self::expr_uses_var(value, name) { Some(false) } else { None }
            }
            Stmt::If { condition, body, else_body, .. } => {
                if Self::expr_uses_var(condition, name) { return Some(false); }
                if let Some(r) = Self::first_use_is_local_write(body, name) { return Some(r); }
                Self::first_use_is_local_write(else_body, name)
            }
            Stmt::While { condition, body, .. } => {
                if Self::expr_uses_var(condition, name) { return Some(false); }
                Self::first_use_is_local_write(body, name)
            }
            Stmt::Guard { condition, else_body, .. } => {
                if Self::expr_uses_var(condition, name) { return Some(false); }
                Self::first_use_is_local_write(else_body, name)
            }
            Stmt::UnsafeBlock { body, .. } => Self::first_use_is_local_write(body, name),
            Stmt::For { var_name, iterable, body, .. }
            | Stmt::ParallelFor { var_name, iterable, body, .. } => {
                // First-access-wins: Liest das Iterable denselben Namen,
                // bleibt der Name fuer die GANZE Funktion global (auch der
                // spaetere Binder). Ohne diesen Read ist der Binder der erste
                // lokale Write. Echtes block-lokales Shadowing waere ein
                // separater Sprach-/Scope-Umbau.
                if Self::expr_uses_var(iterable, name) { return Some(false); }
                // Die For-Bindung selbst ist der erste lokale Write.
                if var_name == name { return Some(true); }
                Self::first_use_is_local_write(body, name)
            }
            Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                if let Some(r) = Self::first_use_is_local_write(try_body, name) { return Some(r); }
                // First-access-wins auch hier: Nur wenn try_body den Namen
                // nicht vorher liest/schreibt, ist der Catch-Binder der erste
                // lokale Write.
                if catch_var.as_ref().is_some_and(|v| v == name) { return Some(true); }
                Self::first_use_is_local_write(catch_body, name)
            }
            Stmt::Match { value, cases, .. } => {
                if Self::expr_uses_var(value, name) { return Some(false); }
                for (muster, waechter, body) in cases {
                    // Muster/Waechter in Textreihenfolge (Review 49a04c50). Wie in
                    // compile_match: Bezeichner MIT Waechter und Dict-Werte binden
                    // (erster lokaler Write), sonst ist das Muster ein Vergleichswert.
                    match muster {
                        Some(Expr::Identifier(n)) if waechter.is_some() => {
                            if n == name { return Some(true); }
                        }
                        Some(Expr::Dict(paare)) => {
                            for (schluessel, wert) in paare {
                                if Self::expr_uses_var(schluessel, name) { return Some(false); }
                                if matches!(wert, Expr::Identifier(n) if n == name) { return Some(true); }
                            }
                        }
                        Some(m) => { if Self::expr_uses_var(m, name) { return Some(false); } }
                        None => {}
                    }
                    if let Some(w) = waechter {
                        if Self::expr_uses_var(w, name) { return Some(false); }
                    }
                    if let Some(r) = Self::first_use_is_local_write(body, name) { return Some(r); }
                }
                None
            }
            Stmt::PropertyAssignment { object, value, .. } => {
                if Self::expr_uses_var(object, name) || Self::expr_uses_var(value, name) {
                    Some(false)
                } else { None }
            }
            Stmt::IndexAssignment { object, index, value, .. } => {
                if Self::expr_uses_var(object, name) || Self::expr_uses_var(index, name)
                    || Self::expr_uses_var(value, name) { Some(false) } else { None }
            }
            // P2-T8 (Plan b1f51d65 D1): textuell erster Zugriff (first-access-wins)
            // auch in aufraeumen, erwarte und Vor-/Nachbedingungen.
            Stmt::Defer(value, _) | Stmt::Expect(value, _) => {
                if Self::expr_uses_var(value, name) { Some(false) } else { None }
            }
            Stmt::Precondition { condition, .. } | Stmt::Postcondition { condition, .. } => {
                if Self::expr_uses_var(condition, name) { Some(false) } else { None }
            }
            Stmt::TestDef { body, .. } => Self::first_use_is_local_write(body, name),
            Stmt::Export(inner, _) => Self::stmt_first_use(inner, name),
            // Eigene Geltungsbereiche, reine Deklarationen und Spruenge: kein Zugriff.
            Stmt::Return(None, _) | Stmt::Break(_) | Stmt::Continue(_) | Stmt::Global(_, _)
            | Stmt::FunctionDef { .. } | Stmt::ClassDef { .. } | Stmt::StructDef { .. }
            | Stmt::InterfaceDef { .. } | Stmt::DataClassDef { .. } | Stmt::ExternC { .. }
            | Stmt::RawDataU32 { .. } | Stmt::NativeConst { .. } | Stmt::Import { .. } => None,
        }
    }

    pub(crate) fn collect_all_var_names(stmts: &[Stmt]) -> Vec<String> {
        let mut names = Vec::new();
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. } => {
                    if !names.contains(name) { names.push(name.clone()); }
                }
                Stmt::CompoundAssignment { name, .. } => {
                    if !names.contains(name) { names.push(name.clone()); }
                }
                Stmt::If { body, else_body, .. } => {
                    names.extend(Self::collect_all_var_names(body));
                    names.extend(Self::collect_all_var_names(else_body));
                }
                Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } => {
                    names.extend(Self::collect_all_var_names(body));
                }
                Stmt::Guard { else_body, .. } => {
                    names.extend(Self::collect_all_var_names(else_body));
                }
                Stmt::For { var_name, body, .. }
                | Stmt::ParallelFor { var_name, body, .. } => {
                    if !names.contains(var_name) { names.push(var_name.clone()); }
                    names.extend(Self::collect_all_var_names(body));
                }
                Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                    if let Some(v) = catch_var {
                        if !names.contains(v) { names.push(v.clone()); }
                    }
                    names.extend(Self::collect_all_var_names(try_body));
                    names.extend(Self::collect_all_var_names(catch_body));
                }
                Stmt::Match { cases, .. } => {
                    for (_, _, body) in cases {
                        names.extend(Self::collect_all_var_names(&body));
                    }
                }
                _ => {}
            }
        }
        names.sort();
        names.dedup();
        names
    }

    /// Sammelt alle freien Variablen in einem Ausdruck (Identifier die nicht in params sind)
    fn collect_free_vars(expr: &Expr, params: &[String], out: &mut Vec<String>) {
        match expr {
            Expr::Identifier(name) => {
                if !params.contains(name) {
                    out.push(name.clone());
                }
            }
            Expr::BinaryOp { left, right, .. } => {
                Self::collect_free_vars(left, params, out);
                Self::collect_free_vars(right, params, out);
            }
            Expr::UnaryOp { operand, .. } => {
                Self::collect_free_vars(operand, params, out);
            }
            Expr::FunctionCall { name, args, .. } => {
                // Der Callee kann selbst ein freier Funktionswert sein, z. B.
                // `() => cb()` mit `cb` als Parameter der aeusseren Funktion.
                // Der spaetere self.variables-Filter stellt sicher, dass nur
                // tatsaechlich vorhandene lokale Werte captured werden.
                if !params.contains(name) {
                    out.push(name.clone());
                }
                for a in args { Self::collect_free_vars(a, params, out); }
            }
            Expr::MethodCall { object, args, .. } => {
                Self::collect_free_vars(object, params, out);
                for a in args { Self::collect_free_vars(a, params, out); }
            }
            Expr::PropertyAccess { object, .. } => {
                Self::collect_free_vars(object, params, out);
            }
            Expr::IndexAccess { object, index } => {
                Self::collect_free_vars(object, params, out);
                Self::collect_free_vars(index, params, out);
            }
            Expr::ListComprehension { expr, var_name, iterable, condition } => {
                Self::collect_free_vars(iterable, params, out);
                let mut inner_params: Vec<String> = params.to_vec();
                inner_params.push(var_name.clone());
                Self::collect_free_vars(expr, &inner_params, out);
                if let Some(cond) = condition {
                    Self::collect_free_vars(cond, &inner_params, out);
                }
            }
            Expr::Spread(inner) => {
                Self::collect_free_vars(inner, params, out);
            }
            Expr::Pipe { left, right } => {
                Self::collect_free_vars(left, params, out);
                Self::collect_free_vars(right, params, out);
            }
            Expr::NullishCoalesce { left, right } => {
                Self::collect_free_vars(left, params, out);
                Self::collect_free_vars(right, params, out);
            }
            Expr::QueryExpr { var_name, source, where_cond, order_expr, select_expr } => {
                Self::collect_free_vars(source, params, out);
                let mut inner = params.to_vec();
                inner.push(var_name.clone());
                if let Some(c) = where_cond { Self::collect_free_vars(c, &inner, out); }
                if let Some(o) = order_expr { Self::collect_free_vars(o, &inner, out); }
                Self::collect_free_vars(select_expr, &inner, out);
            }
            // P2-T8 (Plan b1f51d65 D1): vollstaendiger Match. Frueher fing ein
            // Lambda freie Variablen in Liste, Dict, Bereich, Bedingungsausdruck,
            // Match, neu-Argumenten und inneren Lambdas nicht ein.
            Expr::Range { start, end } => {
                Self::collect_free_vars(start, params, out);
                Self::collect_free_vars(end, params, out);
            }
            Expr::List(items) | Expr::New { args: items, .. } => {
                for i in items { Self::collect_free_vars(i, params, out); }
            }
            Expr::Dict(pairs) => {
                for (k, v) in pairs {
                    Self::collect_free_vars(k, params, out);
                    Self::collect_free_vars(v, params, out);
                }
            }
            Expr::OptionalChain { object, .. } => Self::collect_free_vars(object, params, out),
            Expr::Ternary { condition, then_val, else_val } => {
                Self::collect_free_vars(condition, params, out);
                Self::collect_free_vars(then_val, params, out);
                Self::collect_free_vars(else_val, params, out);
            }
            Expr::MatchExpr { value, cases } => {
                Self::collect_free_vars(value, params, out);
                for (muster, waechter, ergebnis) in cases {
                    if let Some(m) = muster { Self::collect_free_vars(m, params, out); }
                    if let Some(w) = waechter { Self::collect_free_vars(w, params, out); }
                    Self::collect_free_vars(ergebnis, params, out);
                }
            }
            Expr::Lambda { params: innere, body, .. } => {
                let mut inner = params.to_vec();
                inner.extend(innere.iter().cloned());
                Self::collect_free_vars(body, &inner, out);
            }
            Expr::Number(_) | Expr::NativeNumber { .. } | Expr::String(_) | Expr::Boolean(_)
            | Expr::None | Expr::This => {}
        }
    }
}

#[cfg(test)]
mod bare_schnell_regression {
    use super::*;
    use inkwell::attributes::{Attribute, AttributeLoc};
    use inkwell::memory_buffer::MemoryBuffer;
    use inkwell::values::AnyValue;

    #[test]
    fn memory_fakten_nur_fuer_kanonische_bare_operatoren() {
        let context = Context::create();
        let mut cg = CodeGen::new(&context, "bare_memory", "x86_64-bare").unwrap();
        cg.set_bare_mode(true);
        let add = cg.module.get_function("moo_add").unwrap();
        cg.bare_schnellweg(add);
        let kind = Attribute::get_named_enum_kind_id("memory");
        // Unabhaengiges Orakel: LLVM selbst parst memory(read). Damit ist
        // auch das versionsabhaengige Enum-Encoding im Produkt geprueft.
        let buffer = MemoryBuffer::create_from_memory_range_copy(
            b"declare void @referenz() memory(read)\n\0", "memory_referenz");
        let reference = context.create_module_from_ir(buffer).unwrap();
        let expected = reference.get_function("referenz").unwrap()
            .get_enum_attribute(AttributeLoc::Function, kind).unwrap().get_enum_value();
        assert_eq!(add.get_enum_attribute(AttributeLoc::Function, kind).unwrap().get_enum_value(), expected);
        let print = cg.module.get_function("moo_print").unwrap();
        assert_eq!(cg.bare_schnellweg(print), print);
        assert!(print.get_enum_attribute(AttributeLoc::Function, kind).is_none());

        let hosted = CodeGen::new(&context, "hosted_memory", "x86_64").unwrap();
        let add = hosted.module.get_function("moo_add").unwrap();
        assert_eq!(hosted.bare_schnellweg(add), add);
        assert!(add.get_enum_attribute(AttributeLoc::Function, kind).is_none());

        let mut hosted_optin = CodeGen::new(&context, "hosted_optin_memory", "x86_64").unwrap();
        hosted_optin.set_bare_mode(true);
        let add = hosted_optin.module.get_function("moo_add").unwrap();
        hosted_optin.bare_schnellweg(add);
        assert!(add.get_enum_attribute(AttributeLoc::Function, kind).is_none());
        for name in ["moo_mod", "moo_neg", "moo_bitand", "moo_bitor", "moo_bitxor",
                     "moo_lshift", "moo_rshift", "moo_bitnot"] {
            let op = hosted_optin.module.get_function(name).unwrap();
            assert_eq!(hosted_optin.bare_schnellweg(op), op);
            assert!(op.get_enum_attribute(AttributeLoc::Function, kind).is_none());
        }

        let mut windows = CodeGen::new(&context, "windows_memory", "x86_64-windows").unwrap();
        windows.set_bare_mode(true);
        let add = windows.module.get_function("moo_add").unwrap();
        assert_eq!(windows.bare_schnellweg(add), add);
        assert!(add.get_enum_attribute(AttributeLoc::Function, kind).is_none());
    }

    fn optimierter_probe(op: &str, values: &[(u64, u64)]) -> String {
        let context = Context::create();
        let mut cg = CodeGen::new(&context, "cast_probe", "x86_64-bare").unwrap();
        cg.set_bare_mode(true);
        let wrapper = cg.bare_schnellweg(cg.module.get_function(op).unwrap());
        let probe = cg.module.add_function("probe", cg.mv_type().fn_type(&[], false), None);
        cg.builder.position_at_end(context.append_basic_block(probe, "entry"));
        let args: Vec<BasicMetadataValueEnum<'_>> = values.iter().map(|(tag, bits)|
            cg.mv_type().const_named_struct(&[
                context.i64_type().const_int(*tag, false).into(),
                context.i64_type().const_int(*bits, false).into(),
            ]).into()).collect();
        let result = match cg.builder.build_call(wrapper, &args, "ergebnis").unwrap()
            .try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v,
            _ => panic!("Probe muss einen MooValue liefern"),
        };
        cg.builder.build_return(Some(&result)).unwrap();
        cg.module.verify().unwrap();
        let triple = cg.module.get_triple();
        let machine = Target::from_triple(&triple).unwrap().create_target_machine(
            &triple, "generic", "", OptimizationLevel::Default,
            RelocMode::Static, CodeModel::Kernel).unwrap();
        cg.module.run_passes("default<O2>", &machine, PassBuilderOptions::create()).unwrap();
        cg.module.verify().unwrap();
        probe.print_to_string().to_string()
    }

    #[test]
    fn ungueltige_float_integer_casts_bleiben_im_runtime_pfad() {
        // Der Test fuehrt die undefinierten alten C-Casts NICHT aus.
        // LLVM muss den kalten Aufruf statt eines fptosi-Poison erzeugen.
        let limit = 9223372036854775808.0_f64;
        for (op, args) in [
            ("moo_mod", vec![(0, limit.to_bits()), (0, 1.0_f64.to_bits())]),
            ("moo_mod", vec![(0, f64::INFINITY.to_bits()), (0, 1.0_f64.to_bits())]),
            ("moo_mod", vec![(0, f64::NAN.to_bits()), (0, 1.0_f64.to_bits())]),
            ("moo_mod", vec![(0, 7.0_f64.to_bits()), (0, 0.0_f64.to_bits())]),
            ("moo_bitand", vec![(0, limit.to_bits()), (0, 1.0_f64.to_bits())]),
            ("moo_bitnot", vec![(0, f64::NAN.to_bits())]),
            ("moo_lshift", vec![(0, 1.0_f64.to_bits()), (0, f64::INFINITY.to_bits())]),
            ("moo_mod", vec![(30, (-9_i64) as u64), (0, 2.0_f64.to_bits())]),
        ] {
            let ir = optimierter_probe(op, &args);
            assert!(ir.contains(&format!("@{op}(")), "{op}: {ir}");
            assert!(!ir.contains("fptosi"), "{op}: {ir}");
        }
    }

    #[test]
    fn definierte_grenzen_und_restformel_werden_gefaltet() {
        let lower = -9223372036854775808.0_f64;
        for (op, args, expected) in [
            ("moo_mod", vec![(0, 7.5_f64.to_bits()), (0, 2.0_f64.to_bits())], 1.5_f64),
            ("moo_mod", vec![(0, (-7.5_f64).to_bits()), (0, 2.0_f64.to_bits())], -1.5_f64),
            ("moo_mod", vec![(0, lower.to_bits()), (0, 1.0_f64.to_bits())], 0.0_f64),
            ("moo_bitand", vec![(0, lower.to_bits()), (0, 1.0_f64.to_bits())], 0.0_f64),
            ("moo_rshift", vec![(0, (-8.0_f64).to_bits()), (0, 65.0_f64.to_bits())], -4.0_f64),
            ("moo_lshift", vec![(0, 1.0_f64.to_bits()), (0, (-1.0_f64).to_bits())], lower),
            ("moo_neg", vec![(0, 0.0_f64.to_bits())], -0.0_f64),
        ] {
            let ir = optimierter_probe(op, &args);
            assert!(!ir.contains(&format!("@{op}(")), "{op}: {ir}");
            assert!(!ir.contains("fptosi"), "{op}: {ir}");
            assert!(ir.contains(&(expected.to_bits() as i64).to_string()), "{op}: {ir}");
        }
    }
}
