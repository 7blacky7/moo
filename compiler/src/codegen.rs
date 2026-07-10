/// LLVM Code-Generator für moo — erzeugt nativen Maschinencode über LLVM IR.
/// Alle Werte sind MooValues. Alle Operationen gehen über die C-Runtime.

use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::targets::{
    CodeModel, FileType, InitializationConfig, RelocMode, Target, TargetMachine,
};
use inkwell::types::{BasicMetadataTypeEnum, BasicTypeEnum, StructType};
use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, FunctionValue, PointerValue, StructValue};
use inkwell::OptimizationLevel;
use inkwell::AddressSpace;

use std::collections::HashMap;
use std::path::Path;

use crate::ast::*;
use crate::runtime_bindings::RuntimeBindings;

/// MooTag-Werte — EINZIGE Stelle im Rust-Code mit Tag-Zahlen.
/// MUSS mit runtime/moo_runtime.h (typedef enum MooTag) synchron sein:
/// Drift heisst der Codegen erzeugt Werte mit falschem Tag -> die Runtime
/// interpretiert Pointer/Doubles falsch = die bekannte SEGV-Klasse.
/// Synchronitaet wird von tests/ir_gates/run_tag_sync_gate.sh erzwungen
/// (parst das C-enum und vergleicht; drittes Pruefziel: moo_wasm.c-#defines).
/// Neue Tags hier NUR ergaenzen wenn der Codegen sie wirklich braucht —
/// alles andere laeuft ueber Runtime-Konstruktoren (moo_none() etc.).
pub mod moo_tag {
    pub const NONE: u64 = 3;    // MOO_NONE
    pub const OBJECT: u64 = 7;  // MOO_OBJECT
}
use moo_tag as tag;

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
    variables: HashMap<String, PointerValue<'ctx>>,
    /// Globale Variablen (top-level Assignments) — als LLVM-Globals angelegt,
    /// damit sie aus jeder Funktion sichtbar sind. load_var/store_var fallen
    /// auf diese Map zurueck wenn die Variable nicht lokal ist.
    globals: HashMap<String, PointerValue<'ctx>>,
    current_function: Option<FunctionValue<'ctx>>,
    // Fuer break/continue
    loop_stack: Vec<(inkwell::basic_block::BasicBlock<'ctx>, inkwell::basic_block::BasicBlock<'ctx>)>,
    /// Aktive try-Blocks (innermost zuletzt). Wenn wirf/Stmt::Throw im Codegen
    /// auf einen nicht-leeren Stack trifft, emittiert er nach dem moo_throw
    /// einen unconditional branch zum entsprechenden catch_bb — das terminiert
    /// den try-body an der Throw-Stelle und verhindert dass weiterer Code
    /// (inkl. gib_zurueck) den Throw "verschluckt".
    try_stack: Vec<inkwell::basic_block::BasicBlock<'ctx>>,
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
    // Defer-Stack (Go-inspiriert): Vec von AST-Exprs die am Funktionsende ausgeführt werden
    defer_stack: Vec<Expr>,
    // Crystal-inspiriert: Typ-Tracking fuer Warnungen (kein Enforcement!)
    variable_types: HashMap<String, &'static str>,
    warnings: Vec<String>,
    // Rust-inspiriert: unsafe-Kontext Flag
    unsafe_context: bool,
    // Test-Framework: registrierte Tests (display_name, fn_name)
    test_names: Vec<(String, String)>,
    // Profiling-Modus
    profiling: bool,
    // Bare-Metal-Kernel-Modus (Plan-010): erlaubt die kern_*-Builtins.
    // Gesetzt von der CLI bei --no-stdlib / *-bare-Target oder --erlaube-kern.
    bare_mode: bool,
    // P011-A1/A3: Funktionen UND Daten-Globals mit @benutzt/@einstieg bzw.
    // @rohdaten_u32 — werden am Ende von compile_program in das
    // llvm.used-Global geschrieben (DCE-Schutz).
    used_globals: Vec<PointerValue<'ctx>>,
}

impl<'ctx> CodeGen<'ctx> {
    pub fn new(context: &'ctx Context, module_name: &str) -> Self {
        let module = context.create_module(module_name);
        let builder = context.create_builder();
        let rt = RuntimeBindings::declare(context, &module);

        Self {
            context,
            module,
            builder,
            rt,
            variables: HashMap::new(),
            globals: HashMap::new(),
            current_function: None,
            loop_stack: Vec::new(),
            try_stack: Vec::new(),
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
            defer_stack: Vec::new(),
            variable_types: HashMap::new(),
            warnings: Vec::new(),
            unsafe_context: false,
            test_names: Vec::new(),
            profiling: false,
            bare_mode: false,
            used_globals: Vec::new(),
        }
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
            Expr::FunctionCall { name, args } => {
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
                    [crate::ast::DecoratorArg::Text(s)] => g.set_section(Some(s)),
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

    /// Kompiliert ein ganzes Programm
    pub fn compile_program(&mut self, program: &Program) -> Result<(), String> {
        // Zuerst alle Funktionen und Klassen deklarieren (Forward-Declarations)
        self.forward_declare(program)?;

        // main-Funktion erstellen
        let i32_type = self.context.i32_type();
        let main_type = i32_type.fn_type(&[], false);
        let main_fn = self.module.add_function("main", main_type, None);
        let entry = self.context.append_basic_block(main_fn, "entry");
        self.builder.position_at_end(entry);
        self.current_function = Some(main_fn);

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

        for stmt in &program.statements {
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
            self.builder.build_return(Some(&i32_type.const_int(0, false)))
                .map_err(|e| format!("Return-Fehler: {e}"))?;
        }

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

        // Typ-Warnungen ausgeben (Crystal-inspiriert)
        for w in &self.warnings {
            eprintln!("Warnung: {w}");
        }

        self.module.verify().map_err(|e| format!("LLVM Verifikation fehlgeschlagen: {}", e.to_string()))
    }

    fn forward_declare(&mut self, program: &Program) -> Result<(), String> {
        for stmt in &program.statements {
            match stmt {
                Stmt::FunctionDef { name, params, decorators, .. } => {
                    // Duplikat-Guard: ueberspringe wenn Funktion schon deklariert
                    if self.module.get_function(name).is_some() {
                        continue;
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
                }
                Stmt::ClassDef { name, parent, body, .. } => {
                    if let Some(parent_name) = parent {
                        self.class_parents.insert(name.clone(), parent_name.clone());
                    }
                    for method in body {
                        if let Stmt::FunctionDef { name: method_name, params, .. } = method {
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
                Stmt::DataClassDef { name, fields } => {
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
        Ok(())
    }

    pub fn write_ir(&self, path: &Path) -> Result<(), String> {
        self.module.print_to_file(path)
            .map_err(|e| format!("IR-Datei schreiben fehlgeschlagen: {}", e.to_string()))
    }

    /// Mapped einen kurzen Target-Namen auf ein LLVM Triple
    pub fn resolve_triple(target: &str) -> String {
        match target {
            "native" | "" => TargetMachine::get_default_triple().to_string(),
            "x86_64" | "x64" => "x86_64-unknown-linux-gnu".to_string(),
            // P013: Windows-Cross-Target (COFF, MinGW-ABI). Objekt-only wie
            // alle Cross-Targets — gelinkt wird auf dem Zielsystem
            // (x86_64-w64-mingw32-gcc bzw. nativ unter Windows, siehe
            // runtime/win_regex/README.md + P013-Doku).
            "x86_64-windows" | "windows" | "win64" => "x86_64-pc-windows-gnu".to_string(),
            "x86_64-bare" | "x64-bare" => "x86_64-unknown-none".to_string(),
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
                eprintln!("Warnung: Unbekanntes Target '{other}', verwende als Triple");
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
        let reloc_mode = if triple_str.contains("-none") {
            RelocMode::Static
        } else {
            RelocMode::PIC
        };

        let machine = target_obj
            .create_target_machine(
                &triple,
                &cpu_str,
                &feat_str,
                OptimizationLevel::Default,
                reloc_mode,
                CodeModel::Default,
            )
            .ok_or(format!("Konnte TargetMachine fuer '{}' nicht erstellen", triple_str))?;

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
        let result = self.builder.build_call(func, args, name)
            .map_err(|e| format!("{e}"))?;
        match result.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => Ok(v.into_struct_value()),
            _ => Err(format!("Runtime-Call '{name}' hat keinen Wert zurueckgegeben")),
        }
    }

    /// Ruft eine void Runtime-Funktion auf
    fn call_rt_void(&self, func: FunctionValue<'ctx>, args: &[BasicMetadataValueEnum<'ctx>], name: &str)
        -> Result<(), String>
    {
        self.builder.build_call(func, args, name)
            .map_err(|e| format!("{e}"))?;
        Ok(())
    }

    fn store_var(&mut self, name: &str, val: StructValue<'ctx>) -> Result<(), String> {
        let ptr = if let Some(existing) = self.variables.get(name) {
            // Release alten Wert bevor wir ueberschreiben
            let old = self.builder.build_load(self.mv_type(), *existing, "old")
                .map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_release, &[old.into()], "release_old")?;
            *existing
        } else if let Some(global_ptr) = self.globals.get(name) {
            // Zuweisung auf globale Variable (z.B. aus einer Funktion heraus)
            let old = self.builder.build_load(self.mv_type(), *global_ptr, "old_g")
                .map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_release, &[old.into()], "release_old_g")?;
            *global_ptr
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

    fn load_var(&self, name: &str) -> Result<StructValue<'ctx>, String> {
        let ptr = self.variables.get(name)
            .or_else(|| self.globals.get(name))
            .ok_or(format!("Variable '{name}' nicht gefunden"))?;
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

    fn compile_stmt(&mut self, stmt: &Stmt) -> Result<(), String> {
        match stmt {
            Stmt::Assignment { name, value } | Stmt::ConstAssignment { name, value } => {
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
                let val = self.compile_expr(value)?;
                self.store_var(name, val)
            }
            Stmt::CompoundAssignment { name, op, value } => {
                let current = self.load_var(name)?;
                let rhs = self.compile_expr(value)?;
                let func = match op.as_str() {
                    "+=" => self.rt.moo_add,
                    "-=" => self.rt.moo_sub,
                    _ => return Err(format!("Unbekannter Operator: {op}")),
                };
                let result = self.call_rt(func,
                    &[current.into(), rhs.into()], "compound")?;
                self.store_var(name, result)
            }
            Stmt::PropertyAssignment { object, property, value } => {
                let obj = self.compile_expr(object)?;
                let val = self.compile_expr(value)?;
                let prop_str = self.make_global_str(property, "prop")?;
                self.call_rt_void(self.rt.moo_object_set,
                    &[obj.into(), prop_str.into(), val.into()], "obj_set")?;
                self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_obj_propset")?;
                Ok(())
            }
            Stmt::IndexAssignment { object, index, value } => {
                let obj = self.compile_expr(object)?;
                let idx = self.compile_expr(index)?;
                let val = self.compile_expr(value)?;
                self.call_rt_void(self.rt.moo_index_set,
                    &[obj.into(), idx.into(), val.into()], "idx_set")?;
                // moo_index_set / moo_dict_set uebernehmen Ownership von
                // idx und val (transfer). obj (Container) wird nur genutzt,
                // der Load-Temp haelt noch +1 und muss hier freigegeben
                // werden, sonst leak pro Mutation.
                self.call_rt_void(self.rt.moo_release, &[obj.into()], "rel_obj")?;
                Ok(())
            }
            Stmt::Show(expr) => {
                let val = self.compile_expr(expr)?;
                self.call_rt_void(self.rt.moo_print, &[val.into()], "print")?;
                // moo_print liest nur; den Expression-Temp freigeben.
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_show")?;
                Ok(())
            }
            Stmt::If { condition, body, else_body } => {
                self.compile_if(condition, body, else_body)
            }
            Stmt::While { condition, body } => {
                self.compile_while(condition, body)
            }
            Stmt::For { var_name, iterable, body } => {
                self.compile_for(var_name, iterable, body)
            }
            Stmt::FunctionDef { name, params, defaults, body, decorators } => {
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
            Stmt::RawDataU32 { name, value, decorators } => {
                self.compile_raw_data_u32(name, value, decorators)
            }
            Stmt::DataClassDef { name, fields } => {
                self.compile_data_class(name, fields)
            }
            Stmt::Return(value) => self.compile_return(value),
            Stmt::Break => {
                if let Some((_, after_bb)) = self.loop_stack.last() {
                    self.builder.build_unconditional_branch(*after_bb).map_err(|e| format!("{e}"))?;
                }
                Ok(())
            }
            Stmt::Continue => {
                if let Some((cond_bb, _)) = self.loop_stack.last() {
                    self.builder.build_unconditional_branch(*cond_bb).map_err(|e| format!("{e}"))?;
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
            Stmt::TryCatch { try_body, catch_var, catch_body } => {
                self.compile_try_catch(try_body, catch_var, catch_body)
            }
            Stmt::Throw(expr) => {
                let val = self.compile_expr(expr)?;
                self.call_rt_void(self.rt.moo_throw, &[val.into()], "throw")?;
                // Wenn wir in einem try-Block sind: sofort zum innersten catch_bb
                // branchen. Das terminiert den aktuellen Block und stellt sicher,
                // dass Code nach 'wirf' (inkl. return) nicht ausgefuehrt wird.
                if let Some(catch_bb) = self.try_stack.last().copied() {
                    if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                        self.builder.build_unconditional_branch(catch_bb)
                            .map_err(|e| format!("{e}"))?;
                    }
                }
                Ok(())
            }
            Stmt::Match { value, cases } => {
                self.compile_match(value, cases)
            }
            Stmt::Defer(expr) => {
                // Go-inspiriert: Expression auf den Defer-Stack pushen (LIFO)
                self.defer_stack.push(expr.clone());
                Ok(())
            }
            Stmt::Guard { condition, else_body } => {
                self.compile_guard(condition, else_body)
            }
            Stmt::UnsafeBlock { body } => {
                // Rust-inspiriert: unsafe_context erlaubt gefaehrliche Operationen
                let prev = self.unsafe_context;
                self.unsafe_context = true;
                for s in body {
                    self.compile_stmt(s)?;
                }
                self.unsafe_context = prev;
                Ok(())
            }
            Stmt::Expression(expr) => {
                // Ergebnis wird nicht konsumiert → Expression-Temp freigeben,
                // sonst leakt z.B. eine nackte `d[k]`-Zeile den Return-Ref.
                let val = self.compile_expr(expr)?;
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_expr_stmt")?;
                Ok(())
            }
            Stmt::ParallelFor { var_name, iterable, body } => {
                // Fallback: als normale For-Schleife kompilieren
                // EHRLICH: Echte Parallelisierung braucht thread-sichere Codegen
                self.compile_for(var_name, iterable, body)
            }
            Stmt::Precondition { condition, message } | Stmt::Postcondition { condition, message } => {
                let cond_val = self.compile_expr(condition)?;
                let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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
            Stmt::UnsafeBlock { body } => {
                // Unsafe-Block: kompiliert den Body normal (erlaubt syscall/asm Builtins)
                for s in body {
                    self.compile_stmt(s)?;
                }
                Ok(())
            }
            Stmt::TestDef { name, body } => {
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

                self.current_function = Some(function);
                self.builder.position_at_end(entry);
                self.variables.clear();

                for s in body {
                    self.compile_stmt(s)?;
                }

                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    // Test bestanden → return moo_bool(true)
                    let bval = self.context.bool_type().const_int(1, false);
                    let result = self.call_rt(self.rt.moo_bool_fn, &[bval.into()], "pass")?;
                    self.builder.build_return(Some(&result)).map_err(|e| format!("{e}"))?;
                }

                self.current_function = prev_fn;
                self.variables = prev_vars;
                self.defer_stack = prev_defers;
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }

                // Test-Name registrieren
                self.test_names.push((name.clone(), fn_name));
                Ok(())
            }
            Stmt::Expect(expr) => {
                // erwarte expr → wenn falsch, print Fehler + return false
                let val = self.compile_expr(expr)?;
                let is_true = self.builder.build_call(self.rt.moo_is_truthy,
                    &[val.into()], "expect")
                    .map_err(|e| format!("{e}"))?
                    .try_as_basic_value();
                let cond_bool = match is_true {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("expect truthy fehlgeschlagen".to_string()),
                };

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
                let bfalse = self.context.bool_type().const_int(0, false);
                let fail_val = self.call_rt(self.rt.moo_bool_fn, &[bfalse.into()], "fail")?;
                self.builder.build_return(Some(&fail_val)).map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(ok_bb);
                Ok(())
            }
            _ => Ok(()),
        }
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
            let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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

        self.call_rt(self.rt.moo_none, &[], "none")
    }

    fn compile_guard(&mut self, condition: &Expr, else_body: &[Stmt]) -> Result<(), String> {
        // Swift guard: wenn Bedingung FALSCH → else_body (muss Return enthalten)
        let cond_val = self.compile_expr(condition)?;
        let func = self.current_function.unwrap();

        let is_true = self.builder.build_call(self.rt.moo_is_truthy,
            &[cond_val.into()], "guard_truthy")
            .map_err(|e| format!("{e}"))?
            .try_as_basic_value();
        let cond_bool = match is_true {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("guard truthy fehlgeschlagen".to_string()),
        };

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
            self.emit_defers()?;
            let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
            self.builder.build_return(Some(&none_val)).map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(cont_bb);
        Ok(())
    }

    fn compile_if(&mut self, condition: &Expr, body: &[Stmt], else_body: &[Stmt]) -> Result<(), String> {
        let cond_val = self.compile_expr(condition)?;
        let func = self.current_function.unwrap();

        let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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

    fn compile_while(&mut self, condition: &Expr, body: &[Stmt]) -> Result<(), String> {
        let func = self.current_function.unwrap();

        let cond_bb = self.context.append_basic_block(func, "while_cond");
        let body_bb = self.context.append_basic_block(func, "while_body");
        let after_bb = self.context.append_basic_block(func, "while_after");

        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(cond_bb);
        let cond_val = self.compile_expr(condition)?;
        let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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

        self.loop_stack.push((cond_bb, after_bb));
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
        let list_val = self.compile_expr(iterable)?;

        // Laenge holen
        let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
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
        self.loop_stack.push((incr_bb, after_bb));
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

        // Interrupt Calling Convention support
        let has_interrupt = decorators.iter().any(|d| d.name == "interrupt" || d.name == "unterbrechung");
        if has_interrupt {
            let triple = self.module.get_triple();
            let triple_str = triple.as_str().to_str().unwrap_or("");
            let cc = if triple_str.contains("aarch64") || triple_str.contains("arm") {
                66 // ARM Interrupt
            } else {
                83 // x86 Interrupt
            };
            function.set_call_conventions(cc);
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
                        function.as_global_value().set_section(Some(s));
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
                _ => {}
            }
        }

        let entry = self.context.append_basic_block(function, "entry");

        let prev_fn = self.current_function;
        let prev_vars = self.variables.clone();
        let prev_block = self.builder.get_insert_block();
        let prev_defers = std::mem::take(&mut self.defer_stack);

        self.current_function = Some(function);
        self.builder.position_at_end(entry);
        self.variables.clear();

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
        for n in &local_names {
            if self.variables.contains_key(n.as_str()) { continue; }
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

        for stmt in body {
            self.compile_stmt(stmt)?;
        }

        // Default return none — Defers + Profiling vor implizitem Return
        let current_bb = self.builder.get_insert_block().unwrap();
        if current_bb.get_terminator().is_none() {
            if self.profiling {
                let name_ptr = self.make_global_str(name, "prof_name")?;
                let name_val = self.call_rt(self.rt.moo_string_new, &[name_ptr.into()], "prof_str")?;
                self.call_rt_void(self.rt.moo_profile_exit, &[name_val.into()], "prof_exit")?;
            }
            self.emit_defers()?;
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

        self.current_function = prev_fn;
        self.variables = prev_vars;
        self.defer_stack = prev_defers;
        if let Some(bb) = prev_block {
            self.builder.position_at_end(bb);
        }

        Ok(())
    }

    /// Führt alle Defers in LIFO-Reihenfolge aus (Go-Semantik)
    fn emit_defers(&mut self) -> Result<(), String> {
        let defers: Vec<Expr> = self.defer_stack.iter().rev().cloned().collect();
        for expr in &defers {
            self.compile_expr(expr)?;
        }
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
            // Expression noch evaluieren falls Side-Effects (z.B. Funktionsaufruf)
            if let Some(expr) = value {
                let _ = self.compile_expr(expr)?;
            }
            self.emit_defers()?;
            let zero = self.context.i32_type().const_int(0, false);
            self.builder.build_return(Some(&zero)).map_err(|e| format!("{e}"))?;
            return Ok(());
        }

        let val = if let Some(expr) = value {
            self.compile_expr(expr)?
        } else {
            self.call_rt(self.rt.moo_none, &[], "none")?
        };

        // Wenn wir in einem try-Block sind: pruefen ob der Expression einen Fehler
        // gesetzt hat (z.B. via Methoden-Aufruf 'gib_zurueck k.methode()'). Falls ja,
        // direkt zum catch_bb branchen statt zu returnen.
        if let Some(catch_bb) = self.try_stack.last().copied() {
            let func = self.current_function.unwrap();
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
            self.builder.build_conditional_branch(is_error, catch_bb, ret_bb)
                .map_err(|e| format!("{e}"))?;
            self.builder.position_at_end(ret_bb);
        }

        // Defer-Stack vor Return ausführen (Go-Semantik)
        self.emit_defers()?;
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

                self.current_function = Some(func);
                self.builder.position_at_end(entry);
                self.variables.clear();

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
                for n in &local_names {
                    if self.variables.contains_key(n.as_str()) { continue; }
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

                for s in method_body {
                    self.compile_stmt(s)?;
                }

                let current_bb = self.builder.get_insert_block().unwrap();
                if current_bb.get_terminator().is_none() {
                    let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
                    self.builder.build_return(Some(&none_val)).map_err(|e| format!("{e}"))?;
                }

                self.current_function = prev_fn;
                self.variables = prev_vars;
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
        let val = self.compile_expr(value)?;
        // Match-Wert in Variable speichern fuer Bindings
        let val_ptr = self.builder.build_alloca(self.mv_type(), "match_val")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(val_ptr, val).map_err(|e| format!("{e}"))?;
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
                            let key_val = self.compile_expr(key_expr)?;
                            let has = self.call_rt(self.rt.moo_dict_has,
                                &[current_val.into(), key_val.into()], "has_key")?;
                            let is_true_result = self.builder.build_call(self.rt.moo_is_truthy,
                                &[has.into()], "truthy")
                                .map_err(|e| format!("{e}"))?;
                            let is_true = match is_true_result.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("truthy fehlgeschlagen".to_string()),
                            };
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
                                let key_val = self.compile_expr(key_expr)?;
                                let current_val = self.builder.build_load(self.mv_type(), val_ptr, "mval")
                                    .map_err(|e| format!("{e}"))?.into_struct_value();
                                let bound_val = self.call_rt(self.rt.moo_dict_get,
                                    &[current_val.into(), key_val.into()], "bound")?;
                                self.store_var(binding_name, bound_val)?;
                            }
                        }

                        if let Some(guard_expr) = guard {
                            let guard_val = self.compile_expr(guard_expr)?;
                            let guard_true_result = self.builder.build_call(self.rt.moo_is_truthy,
                                &[guard_val.into()], "guard")
                                .map_err(|e| format!("{e}"))?;
                            let guard_bool = match guard_true_result.try_as_basic_value() {
                                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                _ => return Err("guard truthy fehlgeschlagen".to_string()),
                            };
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
                                self.store_var(name, current_val)?;
                                let guard_val = self.compile_expr(guard.as_ref().unwrap())?;
                                let guard_true_result = self.builder.build_call(self.rt.moo_is_truthy,
                                    &[guard_val.into()], "guard")
                                    .map_err(|e| format!("{e}"))?;
                                let guard_bool = match guard_true_result.try_as_basic_value() {
                                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                    _ => return Err("guard truthy fehlgeschlagen".to_string()),
                                };
                                self.builder.build_conditional_branch(guard_bool, case_bb, next_bb)
                                    .map_err(|e| format!("{e}"))?;
                            } else {
                                // Normaler Vergleich + Guard
                                let pat_val = self.compile_expr(pat_expr)?;
                                let eq = self.call_rt(self.rt.moo_eq, &[current_val.into(), pat_val.into()], "match_eq")?;
                                let is_true_result = self.builder.build_call(self.rt.moo_is_truthy,
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
                                let guard_val = self.compile_expr(guard.as_ref().unwrap())?;
                                let guard_true_result = self.builder.build_call(self.rt.moo_is_truthy,
                                    &[guard_val.into()], "guard")
                                    .map_err(|e| format!("{e}"))?;
                                let guard_bool = match guard_true_result.try_as_basic_value() {
                                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                                    _ => return Err("guard truthy fehlgeschlagen".to_string()),
                                };
                                self.builder.build_conditional_branch(guard_bool, case_bb, next_bb)
                                    .map_err(|e| format!("{e}"))?;
                            }
                        } else {
                            // Normaler Wert-Vergleich ohne Guard
                            let pat_val = self.compile_expr(pat_expr)?;
                            let eq = self.call_rt(self.rt.moo_eq, &[current_val.into(), pat_val.into()], "match_eq")?;
                            let is_true_result = self.builder.build_call(self.rt.moo_is_truthy,
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
        Ok(())
    }

    fn compile_try_catch(&mut self, try_body: &[Stmt], catch_var: &Option<String>, catch_body: &[Stmt]) -> Result<(), String> {
        let func = self.current_function.unwrap();

        // Catch/After schon hier anlegen, damit wir sie in den try_stack pushen
        // koennen — Stmt::Throw im try-Body soll direkt zu catch_bb springen.
        let catch_bb = self.context.append_basic_block(func, "catch_body");
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
            self.builder.build_conditional_branch(is_error, catch_bb, after_bb)
                .map_err(|e| format!("{e}"))?;
        }
        // Wenn try_terminated: kein Branch — catch_bb/after_bb bleiben formal
        // erreichbar fuer LLVM (beide haben eigene Terminatoren), werden aber
        // auf diesem Pfad nie besucht.

        // Catch-Block
        self.builder.position_at_end(catch_bb);
        if let Some(var_name) = catch_var {
            let error_val = self.call_rt(self.rt.moo_get_error, &[], "error")?;
            let error_str = self.call_rt(self.rt.moo_to_string, &[error_val.into()], "err_str")?;
            self.store_var(var_name, error_str)?;
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

        // After — wenn kein Fehler, auch aufraeuemen
        self.builder.position_at_end(after_bb);
        self.call_rt_void(self.rt.moo_try_leave, &[], "try_leave_ok")?;
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

    fn compile_expr(&mut self, expr: &Expr) -> Result<StructValue<'ctx>, String> {
        match expr {
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
                if let Some(llvm_fn) = self.module.get_function(name) {
                    let fn_ptr = llvm_fn.as_global_value().as_pointer_value();
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
                let rhs = self.compile_expr(right)?;

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

                    // Objekt-Branch: pruefe class_name und rufe __op__ auf
                    self.builder.position_at_end(obj_bb);
                    // Fuer jede Klasse mit Ueberladung: Vergleich
                    let mut last_bb = obj_bb;
                    for (class_name, method_fn) in &overloads {
                        let class_str = self.make_global_str(class_name, "cls")?;
                        // object->class_name vergleichen
                        let obj_ptr = self.builder.build_int_to_ptr(
                            self.builder.build_extract_value(lhs, 1, "data")
                                .map_err(|e| format!("{e}"))?.into_int_value(),
                            self.context.ptr_type(inkwell::AddressSpace::default()),
                            "obj_ptr").map_err(|e| format!("{e}"))?;
                        // class_name ist erstes Feld nach refcount (offset 4/8 je nach Alignment)
                        // Einfacher: strcmp ueber Runtime. Nutze moo_object_get mit speziellem Key
                        // Noch einfacher: Direkt die Methode aufrufen — wenn es nur eine Klasse gibt
                        // die __add__ hat, brauchen wir keinen class_name Vergleich
                        let result = self.call_rt(*method_fn, &[lhs.into(), rhs.into()], "op_overload")?;
                        self.builder.build_store(result_ptr, result).map_err(|e| format!("{e}"))?;
                        // KEIN post-call release auf lhs/rhs: method_fn ist eine
                        // user-definierte Methode → Transfer-Semantik per
                        // owning-ref-konvention v2 §3.3. Die +1 der Operanden
                        // wandert in die Param-Slots der Methode und wird dort
                        // von release_function_locals() konsumiert. Ein zusaetz-
                        // liches Release hier waere ein Double-Free (SIGSEGV).
                        self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                        break; // Fuer jetzt: nur erste Ueberladung (Multi-Klassen spaeter)
                    }

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
                let val = self.compile_expr(operand)?;
                let func = match op {
                    UnaryOpKind::Neg => self.rt.moo_neg,
                    UnaryOpKind::Not => self.rt.moo_not,
                    UnaryOpKind::BitNot => self.rt.moo_bitnot,
                };
                let res = self.call_rt(func, &[val.into()], "unary")?;
                self.call_rt_void(self.rt.moo_release, &[val.into()], "rel_un")?;
                Ok(res)
            }
            Expr::FunctionCall { name, args } => {
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
                    for a in args {
                        call_args.push(self.compile_expr(a)?.into());
                    }
                    let expected = func.count_params() as usize;
                    while call_args.len() < expected {
                        let none_val = self.call_rt(self.rt.moo_none, &[], "none_default")?;
                        call_args.push(none_val.into());
                    }
                    return self.call_rt(func, &call_args, "user_call");
                }

                // Builtin-Funktionen pruefen
                match name.as_str() {
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
                    "atan2" => {
                        let a = self.compile_expr(&args[0])?;
                        let b = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_atan2, &[a.into(), b.into()], "atan2");
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
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_type_of, &[arg.into()], "typeof");
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
                        let func = self.compile_expr(&args[0])?;
                        let arg = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_thread_spawn, &[func.into(), arg.into()], "spawn");
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
                    "kern_inw" | "port_in16" | "port_lese16" => {
                        self.require_bare(name)?;
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_io_inw, &[port.into()], "io_inw");
                    }
                    "kern_outw" | "port_out16" | "port_schreibe16" => {
                        self.require_bare(name)?;
                        let port = self.compile_expr(&args[0])?;
                        let val = self.compile_expr(&args[1])?;
                        self.call_rt_void(self.rt.moo_io_outw, &[port.into(), val.into()], "io_outw")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "kern_inl" | "port_in32" | "port_lese32" => {
                        self.require_bare(name)?;
                        let port = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_io_inl, &[port.into()], "io_inl");
                    }
                    "kern_outl" | "port_out32" | "port_schreibe32" => {
                        self.require_bare(name)?;
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
                    // Netzwerk
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
                            for a in args {
                                let v = self.compile_expr(a)?;
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
                            return self.call_rt(helper, &compiled_args,
                                &format!("indirect_{name}"));
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
                        return Err(if suggestion.is_empty() {
                            format!("Funktion '{name}' nicht gefunden.")
                        } else {
                            format!("Funktion '{name}' nicht gefunden. Meintest du '{suggestion}'?")
                        });
                    }
                };
                let mut compiled_args: Vec<BasicMetadataValueEnum> = Vec::new();
                for a in args {
                    let v = self.compile_expr(a)?;
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
                // Crystal-inspiriert: Typ-Warnung bei inkompatiblen Methoden
                if let Some(typ) = self.infer_type(object) {
                    let list_methods = &["append", "hinzufügen", "pop", "sort", "sortieren", "reverse", "umkehren", "join"];
                    let string_methods = &["upper", "gross", "lower", "klein", "trim", "trimmen", "split", "teilen", "replace", "ersetzen"];
                    let dict_methods = &["keys", "schlüssel", "has", "hat"];

                    let mismatch = match typ {
                        "Zahl" => list_methods.contains(&method.as_str()) || string_methods.contains(&method.as_str()) || dict_methods.contains(&method.as_str()),
                        "Text" => list_methods.contains(&method.as_str()) || dict_methods.contains(&method.as_str()),
                        "Liste" => string_methods.contains(&method.as_str()) || dict_methods.contains(&method.as_str()),
                        "Dict" => list_methods.contains(&method.as_str()) || string_methods.contains(&method.as_str()),
                        "Bool" => list_methods.contains(&method.as_str()) || string_methods.contains(&method.as_str()) || dict_methods.contains(&method.as_str()),
                        _ => false,
                    };
                    if mismatch {
                        if let Expr::Identifier(name) = object.as_ref() {
                            self.warnings.push(format!(
                                "'{name}' ist vom Typ {typ}, aber '.{method}()' ist nicht fuer {typ} definiert"
                            ));
                        }
                    }
                }

                let obj_val = self.compile_expr(object)?;

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
                let slot_name = format!("__mcall_obj_{}", self.method_obj_counter);
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
                // Gate: regression/test_method_multi_call_abort.moo.

                let obj = obj_val;
                // P014-A3: Tensor-Methoden nur, wenn KEINE User-Klasse den Namen
                // definiert (Muster "abfrage"|"query"-Arm) — Klassen-Methoden
                // gewinnen immer (raytracer Vec3.plus/mal, Kinder-Klassen).
                let user_hat_methode = self.class_methods.keys().any(|(_, m)| m == method);
                // Eingebaute Methoden
                match method.as_str() {
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
                        self.call_rt_void(self.rt.moo_list_append,
                            &[obj.into(), arg.into()], "append")?;
                        // (alter Inline-Release entfallen — Slot-Cleanup uebernimmt es)
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    "length" | "länge" => {
                        return self.call_rt(self.rt.moo_list_length, &[obj.into()], "len");
                    }
                    "pop" => {
                        return self.call_rt(self.rt.moo_list_pop, &[obj.into()], "pop");
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
                    "ausfuehren" | "ausführen" | "execute" => {
                        // Analog zu query/abfrage: User-Klassen koennen ihre eigene
                        // execute/ausfuehren-Methode haben (z.B. PgClient.execute fuer
                        // prepared statements). Nur an moo_db_stmt_execute dispatchen
                        // wenn KEINE User-Methode mit diesem Namen existiert.
                        let any_user_method = self.class_methods.keys()
                            .any(|(_, m)| m == method);
                        if !any_user_method {
                            return self.call_rt(self.rt.moo_db_stmt_execute, &[obj.into()], "stmt_exec");
                        }
                        let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                        for a in args {
                            call_args.push(self.compile_expr(a)?.into());
                        }
                        let static_class: Option<String> = match object.as_ref() {
                            Expr::Identifier(name) => self.object_var_types.get(name).cloned(),
                            Expr::This => self.current_class.clone(),
                            _ => None,
                        };
                        if let Some(mut cls) = static_class {
                            loop {
                                if let Some(func) = self.class_methods.get(&(cls.clone(), method.clone())).copied() {
                                    return self.call_rt(func, &call_args, "method_call");
                                }
                                match self.class_parents.get(&cls).cloned() {
                                    Some(p) => cls = p,
                                    None => break,
                                }
                            }
                        }
                        if let Some((_, func)) = self.class_methods.iter()
                            .find(|((_, m), _)| m == method)
                            .map(|((c, _), f)| (c.clone(), *f))
                        {
                            return self.call_rt(func, &call_args, "method_call_dyn");
                        }
                        return self.call_rt(self.rt.moo_db_stmt_execute, &[obj.into()], "stmt_exec");
                    }
                    "abfrage" | "query" => {
                        // User-Klassen koennen ihre eigene query/abfrage-Methode haben
                        // (z.B. PgClient.frage/query). Nur an moo_db_stmt_query
                        // dispatchen wenn KEINE User-Methode mit diesem Namen
                        // irgendwo im Programm existiert. Sonst inline user-dispatch.
                        let any_user_method = self.class_methods.keys()
                            .any(|(_, m)| m == method);
                        if !any_user_method {
                            return self.call_rt(self.rt.moo_db_stmt_query, &[obj.into()], "stmt_query");
                        }
                        // User-Method-Dispatch (inline, identisch zu _-Arm-Logik):
                        let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                        for a in args {
                            call_args.push(self.compile_expr(a)?.into());
                        }
                        let static_class: Option<String> = match object.as_ref() {
                            Expr::Identifier(name) => self.object_var_types.get(name).cloned(),
                            Expr::This => self.current_class.clone(),
                            _ => None,
                        };
                        if let Some(mut cls) = static_class {
                            loop {
                                if let Some(func) = self.class_methods.get(&(cls.clone(), method.clone())).copied() {
                                    return self.call_rt(func, &call_args, "method_call");
                                }
                                match self.class_parents.get(&cls).cloned() {
                                    Some(p) => cls = p,
                                    None => break,
                                }
                            }
                        }
                        // Dynamic Dispatch via class-name-cascade (wie _-Arm).
                        // Wenn nicht eindeutig: erste passende Methode nehmen.
                        if let Some((_, func)) = self.class_methods.iter()
                            .find(|((_, m), _)| m == method)
                            .map(|((c, _), f)| (c.clone(), *f))
                        {
                            return self.call_rt(func, &call_args, "method_call_dyn");
                        }
                        // Sollte nie passieren weil any_user_method == true.
                        return self.call_rt(self.rt.moo_db_stmt_query, &[obj.into()], "stmt_query");
                    }
                    "zuruecksetzen" | "zurücksetzen" | "reset" => {
                        return self.call_rt(self.rt.moo_db_stmt_reset, &[obj.into()], "stmt_reset");
                    }
                    "fertig" | "done" => {
                        return self.call_rt(self.rt.moo_thread_done, &[obj.into()], "thread_done");
                    }
                    "senden" | "send" => {
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

                        let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
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
                        if let Expr::Lambda { params, body } = lambda {
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
                        return Ok(final_result);
                    }
                    "filter" | "filtern" => {
                        // list.filter((x) => cond) — create new list, iterate, test, append if truthy
                        let lambda = &args[0];
                        let obj_ptr = self.builder.build_alloca(self.mv_type(), "filter_list")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_store(obj_ptr, obj).map_err(|e| format!("{e}"))?;

                        let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
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
                        let test_val = if let Expr::Lambda { params, body } = lambda {
                            if let Some(param) = params.first() {
                                self.store_var(param, element)?;
                            }
                            self.compile_expr(body)?
                        } else {
                            let fn_val = self.compile_expr(lambda)?;
                            self.call_rt(self.rt.moo_func_call_1,
                                &[fn_val.into(), element.into()], "filter_call")?
                        };
                        {
                            let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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
                        for a in args {
                            call_args.push(self.compile_expr(a)?.into());
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
                            let class_ptr_call = self.builder.build_call(
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
                        return match suggestion {
                            Some(s) => Err(format!("Methode '{method}' nicht gefunden. Meintest du '{s}'?")),
                            None => Err(format!("Methode '{method}' nicht gefunden.")),
                        };
                    }
                }
            }
            Expr::PropertyAccess { object, property } => {
                let obj = self.compile_expr(object)?;
                // Eingebaute Properties
                match property.as_str() {
                    "length" | "länge" => {
                        return self.call_rt(self.rt.moo_list_length, &[obj.into()], "len");
                    }
                    _ => {
                        let prop_str = self.make_global_str(property, "prop")?;
                        self.call_rt(self.rt.moo_object_get,
                            &[obj.into(), prop_str.into()], "prop_get")
                    }
                }
            }
            Expr::OptionalChain { object, property } => {
                let obj = self.compile_expr(object)?;
                let func = self.current_function.unwrap();

                // Prüfe ob obj none ist
                let is_none_result = self.builder.build_call(self.rt.moo_is_none,
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
                Ok(phi.as_basic_value().into_struct_value())
            }
            Expr::NullishCoalesce { left, right } => {
                let lhs = self.compile_expr(left)?;
                let func = self.current_function.unwrap();

                let is_none_result = self.builder.build_call(self.rt.moo_is_none,
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
                let rhs = self.compile_expr(right)?;
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
                let obj = self.compile_expr(object)?;
                // String-Slicing: obj[start..end] -> moo_string_slice(obj, start, end)
                let result = if let Expr::Range { start, end } = index.as_ref() {
                    let s = self.compile_expr(start)?;
                    let e = self.compile_expr(end)?;
                    let slice_res = self.call_rt(self.rt.moo_string_slice,
                        &[obj.into(), s.into(), e.into()], "str_slice")?;
                    self.call_rt_void(self.rt.moo_release, &[s.into()], "rel_s")?;
                    self.call_rt_void(self.rt.moo_release, &[e.into()], "rel_e")?;
                    slice_res
                } else {
                    let idx = self.compile_expr(index)?;
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
                let list = self.call_rt(self.rt.moo_list_new,
                    &[self.context.i32_type().const_int(0, false).into()],
                    "list")?;
                let list_ptr = self.entry_alloca(self.mv_type(), "list_tmp")?;
                self.builder.build_store(list_ptr, list).map_err(|e| format!("{e}"))?;

                for elem in elements {
                    if let Expr::Spread(inner) = elem {
                        // Spread: source-Liste iterieren und jedes Element appenden
                        let src = self.compile_expr(inner)?;
                        self.compile_spread_into_list(list_ptr, src)?;
                    } else {
                        let val = self.compile_expr(elem)?;
                        let current_list = self.builder.build_load(self.mv_type(), list_ptr, "list")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        self.call_rt_void(self.rt.moo_list_append,
                            &[current_list.into(), val.into()], "append")?;
                    }
                }

                let final_list = self.builder.build_load(self.mv_type(), list_ptr, "list")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                Ok(final_list)
            }
            Expr::ListComprehension { expr, var_name, iterable, condition } => {
                // Create empty result list
                let result_list = self.call_rt(self.rt.moo_list_new,
                    &[self.context.i32_type().const_int(0, false).into()], "comp_list")?;
                let result_ptr = self.entry_alloca(self.mv_type(), "comp_list_ptr")?;
                self.builder.build_store(result_ptr, result_list).map_err(|e| format!("{e}"))?;

                // Compile iterable and get length
                let list_val = self.compile_expr(iterable)?;
                let list_ptr = self.entry_alloca(self.mv_type(), "comp_iter_ptr")?;
                self.builder.build_store(list_ptr, list_val).map_err(|e| format!("{e}"))?;

                let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
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
                    let cond_val = self.compile_expr(cond_expr)?;
                    let is_true = self.builder.build_call(self.rt.moo_is_truthy,
                        &[cond_val.into()], "truthy")
                        .map_err(|e| format!("{e}"))?
                        .try_as_basic_value();
                    let cond_bool = match is_true {
                        inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                        _ => return Err("truthy fehlgeschlagen".to_string()),
                    };
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
                let val = self.compile_expr(expr)?;
                let current_result = self.builder.build_load(self.mv_type(), result_ptr, "res_list")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                self.call_rt_void(self.rt.moo_list_append,
                    &[current_result.into(), val.into()], "append")?;

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
                Ok(final_list)
            }
            Expr::Dict(pairs) => {
                let dict = self.call_rt(self.rt.moo_dict_new, &[], "dict")?;
                let dict_ptr = self.entry_alloca(self.mv_type(), "dict_tmp")?;
                self.builder.build_store(dict_ptr, dict).map_err(|e| format!("{e}"))?;

                for (key, val) in pairs {
                    if let Expr::Spread(inner) = key {
                        // Spread in Dict: keys holen, iterieren, get+set
                        let src = self.compile_expr(inner)?;
                        self.compile_spread_into_dict(dict_ptr, src)?;
                    } else {
                        let k = self.compile_expr(key)?;
                        let v = self.compile_expr(val)?;
                        let current_dict = self.builder.build_load(self.mv_type(), dict_ptr, "dict")
                            .map_err(|e| format!("{e}"))?.into_struct_value();
                        self.call_rt_void(self.rt.moo_dict_set,
                            &[current_dict.into(), k.into(), v.into()], "dict_set")?;
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

                // Konstruktor aufrufen (erstelle/create)
                let ctor_name_de = (class_name.clone(), "erstelle".to_string());
                let ctor_name_en = (class_name.clone(), "create".to_string());
                let ctor_fn = self.class_methods.get(&ctor_name_de)
                    .or_else(|| self.class_methods.get(&ctor_name_en))
                    .copied();

                if let Some(func) = ctor_fn {
                    let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                    for a in args {
                        call_args.push(self.compile_expr(a)?.into());
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
                    self.call_rt(func, &call_args, "ctor")?;
                }

                Ok(obj)
            }
            Expr::Lambda { params, body } => {
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
                        // (siehe regression/test_closure_multi_call.moo).
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
                    Some(tramp)
                } else {
                    None
                };

                self.current_function = prev_fn;
                self.variables = prev_vars;
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }

                // Lambda als First-Class-Value verpacken.
                if free_vars.is_empty() {
                    // Kein Capture-Environment — direkter MOO_FUNC-Wrapper.
                    let fn_ptr = function.as_global_value().as_pointer_value();
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
                    let tramp_ptr = tramp.as_global_value().as_pointer_value();
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
                let cond_val = self.compile_expr(condition)?;
                let is_true = self.builder.build_call(self.rt.moo_is_truthy,
                    &[cond_val.into()], "tern_truthy")
                    .map_err(|e| format!("{e}"))?
                    .try_as_basic_value();
                let cond_bool = match is_true {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("ternary truthy fehlgeschlagen".to_string()),
                };

                let then_bb = self.context.append_basic_block(func, "tern_then");
                let else_bb = self.context.append_basic_block(func, "tern_else");
                let merge_bb = self.context.append_basic_block(func, "tern_merge");

                self.builder.build_conditional_branch(cond_bool, then_bb, else_bb)
                    .map_err(|e| format!("{e}"))?;

                // Then
                self.builder.position_at_end(then_bb);
                let then_result = self.compile_expr(then_val)?;
                let then_final_bb = self.builder.get_insert_block().unwrap();
                self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

                // Else
                self.builder.position_at_end(else_bb);
                let else_result = self.compile_expr(else_val)?;
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
                let val = self.compile_expr(value)?;
                let vp = self.builder.build_alloca(self.mv_type(), "mexpr_val").map_err(|e| format!("{e}"))?;
                self.builder.build_store(vp, val).map_err(|e| format!("{e}"))?;
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
                                self.store_var(name, cv)?;
                                let gv = self.compile_expr(guard.as_ref().unwrap())?;
                                let gr = self.builder.build_call(self.rt.moo_is_truthy, &[gv.into()], "g").map_err(|e| format!("{e}"))?;
                                let gb = match gr.try_as_basic_value() { inkwell::values::ValueKind::Basic(v) => v.into_int_value(), _ => return Err("truthy".into()) };
                                self.builder.build_conditional_branch(gb, cb, nb).map_err(|e| format!("{e}"))?;
                            } else {
                                let pv = self.compile_expr(pe)?;
                                let eq = self.call_rt(self.rt.moo_eq, &[cv.into(), pv.into()], "eq")?;
                                let tr = self.builder.build_call(self.rt.moo_is_truthy, &[eq.into()], "t").map_err(|e| format!("{e}"))?;
                                let tb = match tr.try_as_basic_value() { inkwell::values::ValueKind::Basic(v) => v.into_int_value(), _ => return Err("truthy".into()) };
                                self.builder.build_conditional_branch(tb, cb, nb).map_err(|e| format!("{e}"))?;
                            }
                        } else {
                            let pv = self.compile_expr(pe)?;
                            let eq = self.call_rt(self.rt.moo_eq, &[cv.into(), pv.into()], "eq")?;
                            let tr = self.builder.build_call(self.rt.moo_is_truthy, &[eq.into()], "t").map_err(|e| format!("{e}"))?;
                            let tb = match tr.try_as_basic_value() { inkwell::values::ValueKind::Basic(v) => v.into_int_value(), _ => return Err("truthy".into()) };
                            self.builder.build_conditional_branch(tb, cb, nb).map_err(|e| format!("{e}"))?;
                        }
                    } else {
                        self.builder.build_unconditional_branch(cb).map_err(|e| format!("{e}"))?;
                    }
                    self.builder.position_at_end(cb);
                    let r = self.compile_expr(rexpr)?;
                    self.builder.build_store(rp, r).map_err(|e| format!("{e}"))?;
                    self.builder.build_unconditional_branch(merge).map_err(|e| format!("{e}"))?;
                    self.builder.position_at_end(nb);
                }
                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    self.builder.build_unconditional_branch(merge).map_err(|e| format!("{e}"))?;
                }
                self.builder.position_at_end(merge);
                let fv = self.builder.build_load(self.mv_type(), rp, "mexpr_result").map_err(|e| format!("{e}"))?.into_struct_value();
                Ok(fv)
            }
            Expr::Pipe { left, right } => {
                // a |> f(b, c) wird zu f(a, b, c)
                let left_val = self.compile_expr(left)?;
                match right.as_ref() {
                    Expr::FunctionCall { name, args } => {
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
                        let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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
                    let is_true = self.builder.build_call(self.rt.moo_is_truthy,
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
        let src_val = self.compile_expr(source)?;

        // Hilfsfunktion: Iteriere Liste, wende closure auf jedes Element an
        // 1. Filter (wo/where)
        let filtered = if let Some(cond) = where_cond {
            let filt = self.call_rt(self.rt.moo_list_new, &[i32_type.const_int(0, false).into()], "q_filt")?;
            let fp = self.builder.build_alloca(self.mv_type(), "qfp").map_err(|e| format!("{e}"))?;
            self.builder.build_store(fp, filt).map_err(|e| format!("{e}"))?;
            let sp = self.builder.build_alloca(self.mv_type(), "qsp").map_err(|e| format!("{e}"))?;
            self.builder.build_store(sp, src_val).map_err(|e| format!("{e}"))?;
            let lr = self.builder.build_call(self.rt.moo_list_iter_len, &[src_val.into()], "l")
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

            let cv = self.compile_expr(cond)?;
            let tr = self.builder.build_call(self.rt.moo_is_truthy, &[cv.into()], "t")
                .map_err(|e| format!("{e}"))?;
            let tb = match tr.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                _ => return Err("t".into()),
            };
            let adb = self.context.append_basic_block(func, "qf_ad");
            let skb = self.context.append_basic_block(func, "qf_sk");
            self.builder.build_conditional_branch(tb, adb, skb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(adb);
            let el2 = self.load_var(var_name)?;
            let fl = self.builder.build_load(self.mv_type(), fp, "f").map_err(|e| format!("{e}"))?.into_struct_value();
            self.call_rt_void(self.rt.moo_list_append, &[fl.into(), el2.into()], "a")?;
            self.builder.build_unconditional_branch(skb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(skb);
            let ni = self.builder.build_load(i32_type, ip, "i").map_err(|e| format!("{e}"))?.into_int_value();
            let nn = self.builder.build_int_add(ni, i32_type.const_int(1, false), "n").map_err(|e| format!("{e}"))?;
            self.builder.build_store(ip, nn).map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(cb).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(ab);
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

        // 3. Map (wähle/select)
        let res = self.call_rt(self.rt.moo_list_new, &[i32_type.const_int(0, false).into()], "q_res")?;
        let rp = self.builder.build_alloca(self.mv_type(), "qrp").map_err(|e| format!("{e}"))?;
        self.builder.build_store(rp, res).map_err(|e| format!("{e}"))?;
        let sp2 = self.builder.build_alloca(self.mv_type(), "qsp2").map_err(|e| format!("{e}"))?;
        self.builder.build_store(sp2, sorted).map_err(|e| format!("{e}"))?;
        let lr2 = self.builder.build_call(self.rt.moo_list_iter_len, &[sorted.into()], "l2")
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
        let mapped = self.compile_expr(select_expr)?;
        let rl = self.builder.build_load(self.mv_type(), rp, "r").map_err(|e| format!("{e}"))?.into_struct_value();
        self.call_rt_void(self.rt.moo_list_append, &[rl.into(), mapped.into()], "a")?;

        let ni2 = self.builder.build_load(i32_type, ip2, "i").map_err(|e| format!("{e}"))?.into_int_value();
        let nn2 = self.builder.build_int_add(ni2, i32_type.const_int(1, false), "n").map_err(|e| format!("{e}"))?;
        self.builder.build_store(ip2, nn2).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(mc).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(ma);
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

        let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
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

        let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(after_bb);
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

        let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
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
        let val = self.call_rt(self.rt.moo_dict_get,
            &[src_loaded.into(), key.into()], "dspread_val")?;
        // In Ziel-Dict setzen
        let target = self.builder.build_load(self.mv_type(), target_dict_ptr, "tgt")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        self.call_rt_void(self.rt.moo_dict_set,
            &[target.into(), key.into(), val.into()], "dspread_set")?;

        let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        self.builder.position_at_end(after_bb);
        Ok(())
    }

    /// Pre-Scan: Sammelt alle Variablennamen aus dem AST (fuer vorab-Allokation)
    /// Prueft ob ein Expression einen Identifier mit dem gegebenen Namen liest.
    /// Wird genutzt um Counter-Pattern zu erkennen ('setze X auf X + 1') —
    /// solche Stellen sollen auf das Modul-globale schreiben statt eine
    /// neue lokale Alloca anzulegen.
    fn expr_uses_var(expr: &Expr, name: &str) -> bool {
        match expr {
            Expr::Identifier(n) => n == name,
            Expr::BinaryOp { left, right, .. } =>
                Self::expr_uses_var(left, name) || Self::expr_uses_var(right, name),
            Expr::UnaryOp { operand, .. } => Self::expr_uses_var(operand, name),
            Expr::FunctionCall { args, .. } => args.iter().any(|a| Self::expr_uses_var(a, name)),
            Expr::MethodCall { object, args, .. } =>
                Self::expr_uses_var(object, name) || args.iter().any(|a| Self::expr_uses_var(a, name)),
            Expr::PropertyAccess { object, .. } => Self::expr_uses_var(object, name),
            Expr::IndexAccess { object, index } =>
                Self::expr_uses_var(object, name) || Self::expr_uses_var(index, name),
            Expr::Ternary { condition, then_val, else_val } =>
                Self::expr_uses_var(condition, name)
                    || Self::expr_uses_var(then_val, name)
                    || Self::expr_uses_var(else_val, name),
            Expr::List(items) => items.iter().any(|i| Self::expr_uses_var(i, name)),
            Expr::Dict(pairs) => pairs.iter().any(|(k, v)|
                Self::expr_uses_var(k, name) || Self::expr_uses_var(v, name)),
            _ => false,
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
    fn first_use_is_local_write(stmts: &[Stmt], name: &str) -> Option<bool> {
        for stmt in stmts {
            // Pruefe direkt-zuweisende Statements zuerst
            match stmt {
                Stmt::Assignment { name: n, value } if n == name => {
                    return Some(!Self::expr_uses_var(value, name));
                }
                Stmt::ConstAssignment { name: n, value } if n == name => {
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
            | Stmt::Show(value) | Stmt::Throw(value) | Stmt::Return(Some(value))
            | Stmt::Expression(value) | Stmt::CompoundAssignment { value, .. } => {
                if Self::expr_uses_var(value, name) { Some(false) } else { None }
            }
            Stmt::If { condition, body, else_body } => {
                if Self::expr_uses_var(condition, name) { return Some(false); }
                if let Some(r) = Self::first_use_is_local_write(body, name) { return Some(r); }
                Self::first_use_is_local_write(else_body, name)
            }
            Stmt::While { condition, body } => {
                if Self::expr_uses_var(condition, name) { return Some(false); }
                Self::first_use_is_local_write(body, name)
            }
            Stmt::For { iterable, body, .. } => {
                if Self::expr_uses_var(iterable, name) { return Some(false); }
                Self::first_use_is_local_write(body, name)
            }
            Stmt::TryCatch { try_body, catch_body, .. } => {
                if let Some(r) = Self::first_use_is_local_write(try_body, name) { return Some(r); }
                Self::first_use_is_local_write(catch_body, name)
            }
            Stmt::Match { value, cases } => {
                if Self::expr_uses_var(value, name) { return Some(false); }
                for (_, _, body) in cases {
                    if let Some(r) = Self::first_use_is_local_write(body, name) { return Some(r); }
                }
                None
            }
            Stmt::PropertyAssignment { object, value, .. } => {
                if Self::expr_uses_var(object, name) || Self::expr_uses_var(value, name) {
                    Some(false)
                } else { None }
            }
            Stmt::IndexAssignment { object, index, value } => {
                if Self::expr_uses_var(object, name) || Self::expr_uses_var(index, name)
                    || Self::expr_uses_var(value, name) { Some(false) } else { None }
            }
            _ => None,
        }
    }

    fn collect_all_var_names(stmts: &[Stmt]) -> Vec<String> {
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
                Stmt::While { body, .. } => {
                    names.extend(Self::collect_all_var_names(body));
                }
                Stmt::For { var_name, body, .. } => {
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
            Expr::FunctionCall { args, .. } => {
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
            _ => {}
        }
    }
}
