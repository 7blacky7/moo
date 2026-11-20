mod ast;
mod bindgen;
mod cpp_bindgen;
mod boards;
mod builtin_aritaet;
mod codegen;
mod codegen_native;
mod diagnose;
mod fehlerpfade;
mod interner_fehler;
mod ffi_types;
mod ffi_link;
mod native_arithmetic;
mod native_control_flow;
mod native_cleanup;
mod native_crypto;
mod native_struct;
mod native_interface;
mod ffi_abi;
mod native_pointer;
mod native_hardware;
mod native_const;
mod native_system;
mod native_type_id;
mod lexer;
mod native_vecmat;
mod native_simd;
mod native_simd_dispatch;
mod parser;
mod runtime_bindings;
mod runtime_provider;
mod tokens;
mod typpruefung;

use clap::{Parser as ClapParser, Subcommand};
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(ClapParser)]
#[command(name = "moo", about = "moo — die universelle Programmiersprache (nativer Compiler)")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
    /// Diagnoseformat: text (Standard) | json (ein JSON-Objekt auf stderr, fuer Agenten/Werkzeuge)
    #[arg(long = "diagnose", global = true, default_value = "text", value_name = "FORMAT")]
    diagnose: String,
    /// Typpruefung zur Compilezeit (P2-T29, M-T*); vorerst nur auf Wunsch
    #[arg(long = "typpruefung", global = true)]
    typpruefung: bool,
    /// Typpruefung an; IHRE Hinweise (M-T*, M-H001, M-H006) werden Fehler.
    /// Hinweise anderer Phasen (z. B. Lexer M-H007) bleiben Hinweise.
    /// Unabhaengig von MOO_STRENG (Laufzeit des erzeugten Programms).
    #[arg(long = "typ-streng", global = true)]
    typ_streng: bool,
}

/// P2-T29: `--typpruefung` (Standard aus, bis die Pruefung freigegeben ist).
static TYPPRUEFUNG: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);
/// P2-T29 S4: `--typ-streng` (schliesst --typpruefung ein).
static TYP_STRENG: std::sync::atomic::AtomicBool = std::sync::atomic::AtomicBool::new(false);

fn typpruefung_einschalten_falls_gewuenscht() {
    let streng = TYP_STRENG.load(std::sync::atomic::Ordering::Relaxed);
    if streng || TYPPRUEFUNG.load(std::sync::atomic::Ordering::Relaxed) {
        crate::typpruefung::aktivieren();
    }
    if streng {
        crate::typpruefung::streng_aktivieren();
    }
}

#[derive(Subcommand)]
enum Commands {
    /// Erzeugt einen konservativen C-Header-Bindungsentwurf (Linux x86_64)
    Bindgen {
        /// C-Header
        header: PathBuf,
        /// Namenspräfix (mehrfach möglich)
        #[arg(long = "prefix", required = true)]
        prefix: Vec<String>,
        /// C-Include-Verzeichnis (mehrfach möglich)
        #[arg(short = 'I')]
        include: Vec<PathBuf>,
        /// Unvollständigen Entwurf trotz Diagnosen anzeigen
        #[arg(long)]
        allow_partial: bool,
    },
    /// Erzeugt C++17-C-Adapter und Moo-Griffbindungen (enge, geprüfte Teilmenge)
    BindgenCpp {
        header: PathBuf,
        #[arg(long = "class", required = true)]
        classes: Vec<String>,
        #[arg(long, default_value = "moo_cpp")]
        prefix: String,
        #[arg(long)]
        library: String,
        #[arg(long, default_value = cpp_bindgen::DEFAULT_TARGET)]
        target: String,
        #[arg(long, value_parser = ["cpp", "moo"])]
        emit: String,
        #[arg(short = 'I')]
        include: Vec<PathBuf>,
        #[arg(short = 'D')]
        define: Vec<String>,
    },
    /// Parser-AST zur Diagnose ausgeben (Debugformat, nicht stabil)
    Ast {
        /// Die .moos-Quelldatei
        file: PathBuf,
        /// Imports wie beim Kompilieren auflösen
        #[arg(long)]
        imports: bool,
    },
    /// Registrierte Ziel-Layouts als JSON diagnostizieren
    Layout {
        /// Die .moos-Quelldatei
        file: PathBuf,
        /// Ziel-Architektur wie bei compile
        #[arg(long)]
        target: Option<String>,
        /// C11-Header (typedef + _Static_assert für sizeof/_Alignof/offsetof) statt JSON
        #[arg(long = "c-header")]
        c_header: bool,
    },
    /// Eine .moos-Datei kompilieren und als native Binary erzeugen
    Compile {
        /// Die .moos-Quelldatei
        file: PathBuf,
        /// Ausgabedatei (Standard: Name ohne .moos)
        #[arg(short, long)]
        output: Option<PathBuf>,
        /// Nur LLVM IR erzeugen (.ll)
        #[arg(long)]
        emit_ir: bool,
        /// WebAssembly erzeugen (.wasm)
        #[arg(long)]
        target: Option<String>,
        /// Zusätzlicher Bibliotheks-Suchpfad, mehrfach verwendbar.
        #[arg(long = "lib-pfad", alias = "lib-path")]
        lib_paths: Vec<PathBuf>,
        /// Bare-Metal: ohne Standard-Runtime (nur Zahlen + Bitops + Memory I/O)
        #[arg(long)]
        no_stdlib: bool,
        /// Linker-Script (z.B. kernel.ld) — uebergibt -T an den Linker
        #[arg(long)]
        linker_script: Option<PathBuf>,
        /// Entry-Point (z.B. _start statt main)
        #[arg(long)]
        entry: Option<String>,
        /// Profiling aktivieren (misst Zeit pro Funktion)
        #[arg(long)]
        profile: bool,
        /// Kernel-Builtins (kern_*) im hosted-Build erlauben (Opt-in, z.B. Test-Harness)
        #[arg(long = "erlaube-kern")]
        erlaube_kern: bool,
        /// One-Shot-Kernel-Pipeline: moo + Bare-Runtime + Linker-Script -> bootbares kernel.elf (nur mit --no-stdlib + x86_64-bare)
        #[arg(long)]
        kernel: bool,
        /// Ausgabeformat der Kernel-/Loader-Pipeline: elf (Standard) | flat (objcopy -O binary) | sector (flat + 512-Byte-Boot-Sektor-Gate mit 0x55AA)
        #[arg(long, default_value = "elf")]
        emit: String,
        /// Board-Profil fuer die Kernel-Pipeline (P012-C1): setzt Target + Geraete-Defines. Liste: `moo targets`
        #[arg(long)]
        board: Option<String>,
    },
    /// Eine .moos-Datei kompilieren und sofort ausführen
    Run {
        /// Die .moos-Quelldatei
        file: PathBuf,
    },
    /// Nur pruefen: lexen, parsen, Module aufloesen, Code erzeugen und verifizieren – ohne Binary (P2-T97)
    #[command(name = "pruefe", alias = "check")]
    Pruefe {
        /// Die .moos-Quelldatei
        file: PathBuf,
        /// Ziel-Architektur wie bei compile
        #[arg(long)]
        target: Option<String>,
        /// Zusätzlicher Bibliotheks-Suchpfad, mehrfach verwendbar.
        #[arg(long = "lib-pfad", alias = "lib-path")]
        lib_paths: Vec<PathBuf>,
        /// Bare-Metal wie bei compile
        #[arg(long)]
        no_stdlib: bool,
        /// Kernel-Builtins (kern_*) erlauben wie bei compile
        #[arg(long = "erlaube-kern")]
        erlaube_kern: bool,
    },
    /// Diagnosecode erklären (ohne Code: Katalog auflisten)
    #[command(name = "erklaere", alias = "explain")]
    Erklaere {
        /// z. B. M-S001
        code: Option<String>,
    },
    /// Builtin-Funktionen mit Argumentzahl als JSON (P2-T100, fuer Gates)
    #[command(name = "builtins", hide = true)]
    Builtins,
    /// Zur Laufzeit geladene LLVM-Version (fuer `mise run llvm-check`)
    #[command(name = "llvm-version", hide = true)]
    LlvmVersion,
    /// Interaktiver Modus (REPL)
    Repl,
    /// Verfuegbare Cross-Compilation Targets anzeigen
    Targets,
    /// Paketverwaltung (install/list/remove)
    #[command(name = "paket", alias = "package")]
    Paket {
        #[command(subcommand)]
        action: PaketAction,
    },
}

#[derive(Subcommand)]
enum PaketAction {
    /// Paket von github.com/moo-packages/<name> installieren
    #[command(alias = "installiere")]
    Install {
        /// Paketname
        name: String,
    },
    /// Installierte Pakete auflisten
    #[command(alias = "liste")]
    List,
    /// Paket entfernen
    #[command(alias = "entferne")]
    Remove {
        /// Paketname
        name: String,
    },
}

/// P2-T100: Stack-Stufen des Compiler-Threads. Parser, Typpruefung und
/// Codegen rekursieren ueber die Tiefe des Syntaxbaums; das Tiefenbudget
/// (M-S004) wird an den Stack gekoppelt, der wirklich startet
/// (parser::tiefe_fuer_stack: 512 MiB -> 1000, 128 MiB -> 250, 32 MiB -> 62).
/// Reserviert, nicht belegt. Kein Lauf auf dem Hauptthread: startet keine
/// Stufe, bricht der Compiler mit M-C999 ab (fail-closed, nie Stack Overflow).
const COMPILER_STACKS: [usize; 3] = [512 * 1024 * 1024, 128 * 1024 * 1024, 32 * 1024 * 1024];

fn main() {
    let cli = std::sync::Arc::new(std::sync::Mutex::new(Some(Cli::parse())));
    for &stack in &COMPILER_STACKS {
        parser::setze_max_tiefe(parser::tiefe_fuer_stack(stack));
        let cli_faden = cli.clone();
        let faden = std::thread::Builder::new()
            .name("moo-compiler".into())
            .stack_size(stack)
            .spawn(move || {
                if let Some(cli) = cli_faden.lock().ok().and_then(|mut c| c.take()) {
                    main_kern(cli);
                }
            });
        if let Ok(faden) = faden {
            // Panics faengt der Hook im Compiler-Thread (M-C999, exit 1); hier
            // landet nur ein Panic mit MOO_PANIK_ROH=1 (Rust-Standard-RC 101).
            if faden.join().is_err() {
                std::process::exit(101);
            }
            return;
        }
    }
    if let Some(cli) = cli.lock().ok().and_then(|mut c| c.take()) {
        if let Ok(format) = diagnose::Format::parse(&cli.diagnose) {
            diagnose::setze_format(format);
        }
    }
    let mib: Vec<String> = COMPILER_STACKS.iter().map(|s| (s / (1024 * 1024)).to_string()).collect();
    diagnose::melden(diagnose::Diagnose::fehler("M-C999", format!(
        "Interner Compilerfehler: Compiler-Thread konnte nicht starten (Stack {} MiB nicht verfuegbar, z. B. ulimit -v).",
        mib.join("/"))));
    diagnose::abschliessen();
    std::process::exit(1);
}

fn main_kern(cli: Cli) {
    // P2-T100: Panics im Compiler-Thread werden Diagnose M-C999 statt Absturz.
    interner_fehler::netz_installieren();
    match diagnose::Format::parse(&cli.diagnose) {
        Ok(format) => diagnose::setze_format(format),
        Err(e) => { eprintln!("{e}"); std::process::exit(2); }
    }
    TYPPRUEFUNG.store(cli.typpruefung, std::sync::atomic::Ordering::Relaxed);
    TYP_STRENG.store(cli.typ_streng, std::sync::atomic::Ordering::Relaxed);

    match cli.command {
        Commands::Bindgen { header, prefix, include, allow_partial } => {
            match bindgen::run(&header, &prefix, &include, allow_partial) {
                Ok(code) if code != 0 => std::process::exit(code),
                Ok(_) => {}
                Err(e) => { eprintln!("bindgen: {e}"); std::process::exit(1); }
            }
        }
        Commands::BindgenCpp { header, classes, prefix, library, target, emit, include, define } => {
            if let Err(e) = cpp_bindgen::run(&header, &classes, &prefix, &library, &target, &emit, &include, &define) {
                eprintln!("bindgen-cpp: {e}");
                std::process::exit(2);
            }
        }
        Commands::Ast { file, imports } => {
            let result = (|| -> Result<ast::Program, String> {
                let mut program = parse_file(&file)?;
                if imports {
                    let mut visited = std::collections::HashSet::new();
                    visited.insert(file.canonicalize().unwrap_or_else(|_| file.clone()));
                    resolve_modules(&mut program, file.as_path(), &mut visited, &[])?;
                }
                Ok(program)
            })();
            match result {
                Ok(program) => { println!("{program:#?}"); diagnosen_abschliessen(); }
                Err(e) => mit_diagnosen(Err(e)),
            }
        }
        Commands::Layout { file, target, c_header } => {
            let target = target.as_deref().unwrap_or("native");
            let result = if c_header { layout_c_header(&file, target) } else { layout_diagnostic(&file, target) };
            match result {
                Ok(header) if c_header => { print!("{header}"); diagnosen_abschliessen(); }
                Ok(json) => { println!("{json}"); diagnosen_abschliessen(); }
                Err(e) => mit_diagnosen(Err(e)),
            }
        }
        Commands::Compile { file, output, emit_ir, target, lib_paths, no_stdlib, linker_script, entry, profile, erlaube_kern, kernel, emit, board } => {
            if let Err(e) = compile(&file, output.as_deref(), emit_ir, target.as_deref(), no_stdlib, linker_script.as_deref(), entry.as_deref(), profile, erlaube_kern, kernel, &emit, board.as_deref(), &lib_paths) {
                mit_diagnosen(Err(e));
            } else {
                diagnosen_abschliessen();
            }
        }
        Commands::Run { file } => {
            mit_diagnosen(run(&file));
        }
        Commands::Pruefe { file, target, lib_paths, no_stdlib, erlaube_kern } => {
            let ergebnis = pruefe(&file, target.as_deref(), &lib_paths, no_stdlib, erlaube_kern);
            let ok = ergebnis.is_ok();
            mit_diagnosen(ergebnis);
            if ok && !matches!(diagnose_format(&cli.diagnose), diagnose::Format::Json) {
                println!("✓ Geprüft: {} (keine Fehler)", file.display());
            }
        }
        Commands::Erklaere { code } => {
            mit_diagnosen(erklaere(code.as_deref()));
        }
        Commands::Builtins => {
            println!("{}", builtin_aritaet::als_json());
        }
        Commands::LlvmVersion => {
            // Tatsaechlich geladene Bibliothek (statisch oder dynamisch).
            let (major, minor, patch) = inkwell::support::get_llvm_version();
            println!("LLVM {major}.{minor}.{patch}");
        }
        Commands::Repl => {
            repl();
        }
        Commands::Targets => {
            println!("Verfuegbare Targets fuer moo Cross-Compilation:\n");
            for (name, desc) in codegen::CodeGen::list_targets() {
                println!("  {:<12} {}", name, desc);
            }
            println!("\nBoard-Profile (Kernel-Pipeline, --kernel --board <name>):\n");
            for b in boards::BOARDS {
                println!("  {:<18} {} [target {}]", b.name, b.beschreibung, b.target);
            }
            println!("\nVerwendung: moo-compiler compile datei.moos --target <name>");
        }
        Commands::Paket { action } => {
            if let Err(e) = handle_paket(action) {
                eprintln!("Fehler: {e}");
                std::process::exit(1);
            }
        }
    }
}

// JSON-Stringkodierung: gemeinsamer Encoder in diagnose.rs (P2-T97).
use diagnose::json_string as layout_json_string;

/// P2-T97: Diagnosen des Kommandos einmal ausgeben; bei Fehlern RC 1.
fn diagnosen_abschliessen() {
    if diagnose::abschliessen() {
        std::process::exit(1);
    }
}

/// P2-T97: Ergebnis eines Kommandos mit Diagnosen abschliessen. Ein Err-Text
/// ohne strukturierte Diagnose wird als M-W000 gemeldet.
fn mit_diagnosen(ergebnis: Result<(), String>) {
    if let Err(e) = ergebnis {
        diagnose::fehler_aus_text(e);
    }
    diagnosen_abschliessen();
}

fn diagnose_format(wert: &str) -> diagnose::Format {
    diagnose::Format::parse(wert).unwrap_or_default()
}

// Gemeinsame Quelle für `layout` (JSON) und `layout --c-header`.
fn layout_program(file: &PathBuf) -> Result<ast::Program, String> {
    let mut program = parse_file(file)?;
    let mut visited = std::collections::HashSet::new();
    visited.insert(file.canonicalize().unwrap_or_else(|_| file.clone()));
    resolve_modules(&mut program, file.as_path(), &mut visited, &[])?;
    Ok(program)
}

/// P2-T13/T48 (Plan a71947f7 E1): dieselben registrierten Ziel-Layouts als
/// C11-Header, den der C-Compiler des Ziels per _Static_assert prüft.
fn layout_c_header(file: &PathBuf, target: &str) -> Result<String, String> {
    let program = layout_program(file)?;
    let context = inkwell::context::Context::create();
    let name = file.file_stem().and_then(|s| s.to_str()).unwrap_or("layout");
    let mut compiler = codegen::CodeGen::new(&context, name, target)?;
    let guard: String = name.chars()
        .map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_uppercase() } else { '_' })
        .collect();
    compiler.diagnostic_c_header(&program, &codegen::CodeGen::resolve_triple(target),
        &format!("MOO_LAYOUT_{guard}_H"))
}

fn layout_diagnostic(file: &PathBuf, target: &str) -> Result<String, String> {
    let program = layout_program(file)?;
    let context = inkwell::context::Context::create();
    let name = file.file_stem().and_then(|s| s.to_str()).unwrap_or("layout");
    let mut compiler = codegen::CodeGen::new(&context, name, target)?;
    let layouts = compiler.diagnostic_layouts(&program)?;
    let triple = codegen::CodeGen::resolve_triple(target);
    let mut structs = Vec::with_capacity(layouts.len());
    for (name, size, alignment, fields) in layouts {
        let fields = fields.into_iter().map(|(field, offset)|
            format!("{{\"name\":{},\"offset\":{offset}}}", layout_json_string(&field))
        ).collect::<Vec<_>>().join(",");
        structs.push(format!("{{\"name\":{},\"size\":{size},\"alignment\":{alignment},\"fields\":[{fields}]}}",
            layout_json_string(&name)));
    }
    Ok(format!("{{\"schema\":1,\"target_triple\":{},\"structs\":[{}]}}",
        layout_json_string(&triple), structs.join(",")))
}

/// Parst eine .moos-Datei zu einem AST
fn parse_file(file: &PathBuf) -> Result<ast::Program, String> {
    let datei = file.display().to_string();
    let source = std::fs::read_to_string(file).map_err(|e| diagnose::melde_fehler(
        diagnose::Diagnose::fehler("M-I003", format!("Datei '{datei}' lesen fehlgeschlagen: {e}")).datei(datei.clone())
    ))?;
    // P2-T97: alle Lexer- bzw. Syntaxfehler der Datei strukturiert melden;
    // Dateiname an jeder Diagnose (importierte Module bleiben zuordenbar).
    let (tokens, lex_fehler) = lexer::Lexer::new().tokenize_alle(&source);
    // Lexer-Hinweise (M-H007) entstehen ohne Datei; hier nachtragen.
    diagnose::datei_nachtragen(&datei);
    if let Some(erster) = lex_fehler.first() {
        let text = format!("{datei}: {erster}");
        for e in &lex_fehler {
            diagnose::melden(e.diagnose().datei(datei.clone()));
        }
        return Err(text);
    }
    let mut par = parser::Parser::new(tokens);
    par.parse_alle().map(|mut program| {
        // Methoden bleiben bis zur Auflösung beim Strukturtyp; ihre Herkunft
        // muss auch nach dem späteren nativen Absenken erhalten bleiben.
        for stmt in &program.statements {
            if let ast::Stmt::StructDef { name, methods, .. } = stmt {
                for method in methods {
                    if let ast::Stmt::FunctionDef { name: method_name, .. } = method {
                        program.funktion_quellen.insert(ast::native_method_name(name, method_name), datei.clone());
                    }
                }
            }
        }
        program
    }).map_err(|fehler| {
        let text = format!("{datei}: {}", fehler[0]);
        for e in &fehler {
            diagnose::melden(e.diagnose().datei(datei.clone()));
        }
        text
    })
}

/// Findet die Quelldatei fuer ein Modul (relativ zum Import-Ort oder in packages).
/// Endung: bevorzugt .moos (MooScript); .moo bleibt als Fallback lesbar.
fn module_file(dir: &std::path::Path, name: &str) -> Option<PathBuf> {
    for ext in ["moos", "moo"] {
        let candidate = dir.join(format!("{name}.{ext}"));
        if candidate.exists() { return Some(candidate); }
    }
    None
}

/// Pfad-Import (`importiere "../lib/welt"`): enthaelt einen Pfadtrenner oder
/// beginnt mit '.', und wird relativ zur importierenden Datei aufgeloest.
fn is_path_import(name: &str) -> bool {
    name.contains('/') || name.contains('\\') || name.starts_with('.')
}

/// Orte, an denen find_module sucht — fuer die Fehlermeldung bei fehlendem Modul.
fn module_search_locations(name: &str, dir: &std::path::Path, lib_paths: &[PathBuf]) -> Vec<String> {
    if is_path_import(name) {
        return vec![dir.join(name).display().to_string()];
    }
    let mut orte = vec![dir.display().to_string()];
    orte.extend(lib_paths.iter().map(|p| p.display().to_string()));
    orte.push(dir.join("stdlib").display().to_string());
    if let Some(exe_dir) = std::env::current_exe().ok().and_then(|e| e.parent().map(|p| p.to_path_buf())) {
        orte.push(exe_dir.join("stdlib").display().to_string());
    }
    orte.push(std::env::current_dir().unwrap_or_default().join("stdlib").display().to_string());
    orte.push(packages_dir().join(name).display().to_string());
    orte
}

fn find_module(name: &str, dir: &std::path::Path, lib_paths: &[PathBuf]) -> Option<PathBuf> {
    if is_path_import(name) {
        let path = dir.join(name);
        if path.extension().is_some_and(|e| e == "moos" || e == "moo") {
            return path.exists().then_some(path);
        }
        let file_name = path.file_name()?.to_str()?.to_string();
        return module_file(path.parent()?, &file_name);
    }
    // Relativ zum aktuellen Verzeichnis
    if let Some(p) = module_file(dir, name) { return Some(p); }
    // P2-T81: --lib-pfad (erzeugte Moo-Schnittstellen liegen neben Importbibliothek/DLL)
    for pfad in lib_paths {
        if let Some(p) = module_file(pfad, name) { return Some(p); }
    }
    // In stdlib/ relativ zum aktuellen Verzeichnis
    if let Some(p) = module_file(&dir.join("stdlib"), name) { return Some(p); }
    // In stdlib/ relativ zum Compiler-Binary (fuer installierte stdlib)
    if let Ok(exe) = std::env::current_exe() {
        if let Some(exe_dir) = exe.parent() {
            if let Some(p) = module_file(&exe_dir.join("stdlib"), name) { return Some(p); }
        }
    }
    // In stdlib/ relativ zum cwd (K4 Fix)
    let cwd = std::env::current_dir().unwrap_or_default();
    if let Some(p) = module_file(&cwd.join("stdlib"), name) { return Some(p); }
    // In ~/.moo/packages/<name>/<name>.moos (bzw. .moo)
    if let Some(p) = module_file(&packages_dir().join(name), name) { return Some(p); }
    None
}

/// Wickelt einen `exportiere X`-Wrapper (Stmt::Export) zu seinem inneren Statement aus.
/// Das Modulsystem ist inline-basiert; `exportiere` ist nur ein Sichtbarkeits-Marker und
/// darf die Definition nicht vor codegen verstecken (codegen behandelt Stmt::Export nicht,
/// wuerde die umschlossene Funktion/Klasse also stillschweigend verwerfen).
fn strip_export(stmt: ast::Stmt) -> ast::Stmt {
    match stmt {
        ast::Stmt::Export(inner, _) => *inner,
        other => other,
    }
}

/// P2-T47 (Plan b1f51d65 C2/C3/C5): Wissen ueber bereits geparste Module,
/// auch fuer Rautenimporte (Modul schon von anderer Stelle eingefuegt).
#[derive(Default)]
struct ModulKontext {
    /// Kanonischer Pfad -> Namen, die das Modul SELBST auf oberster Ebene
    /// definiert (ohne die seiner Importe; Plan E2, Review 49a04c50).
    namen: std::collections::HashMap<PathBuf, std::rc::Rc<std::collections::HashSet<String>>>,
    /// Strukturname -> Datei der ersten Definition (Kollision mit beiden Pfaden).
    strukturen: std::collections::HashMap<String, PathBuf>,
    /// P2-T99: Top-Level-Name -> alle Vorkommen (erstes je Datei und Art;
    /// Herkunft fuer M-I004/M-I005, Review 9502d95c).
    definitionen: std::collections::HashMap<String, Vec<Definition>>,
    /// P2-T99: kanonischer Pfad der Hauptdatei (Benutzerseite einer Kollision).
    hauptdatei: Option<PathBuf>,
    /// P2-T99: Zahl der gemeldeten M-I004 (Abbruch nach dem Aufloesen).
    kollisionen: usize,
    /// P2-T98: Zahl der gemeldeten M-I006 (Verschattung unsicherer Builtins).
    verschattungen: usize,
    /// P2-T99 (Review 2be97766): kanonischer Pfad -> nur SELBST definierte Namen.
    definiert: std::collections::HashMap<PathBuf, std::collections::HashSet<String>>,
    /// P2-T99 (Review 2be97766): kanonischer Pfad -> per `aus x importiere a`
    /// weitergegebene Namen mit den kanonischen Quellmodulen.
    weitergaben: std::collections::HashMap<PathBuf, std::collections::HashMap<String, Vec<PathBuf>>>,
    /// P2-T99 (Review 2be97766): selektive Importe, die nur ueber eine
    /// Weitergabe gedeckt sind; nach dem Aufloesen bis zu einer echten
    /// Definition verfolgt, sonst M-I002.
    offene_weitergaben: Vec<OffeneWeitergabe>,
}

/// P2-T99 (Review 2be97766): `aus modul importiere name`, der nur ueber eine
/// Weitergabe des Moduls gedeckt ist.
struct OffeneWeitergabe {
    datei: String,
    zeile: usize,
    modul: String,
    name: String,
    modul_datei: String,
    kanonisch: PathBuf,
}

/// P2-T99: Herkunft einer Top-Level-Definition.
#[derive(Clone)]
struct Definition {
    /// Anzeigepfad (relativ zum Arbeitsverzeichnis, wenn moeglich).
    datei: String,
    kanonisch: PathBuf,
    zeile: usize,
    art: &'static str,
}

/// Qualifizierer `q` aus `importiere m` bzw. `importiere m als q`; gilt nur
/// in der importierenden Datei.
struct Qualifizierer {
    modul: String,
    alias: bool,
    namen: std::rc::Rc<std::collections::HashSet<String>>,
}

/// Namen, die eine Datei selbst auf oberster Ebene definiert.
fn eigene_namen(stmts: &[ast::Stmt]) -> std::collections::HashSet<String> {
    let mut namen = std::collections::HashSet::new();
    for stmt in stmts {
        let stmt = if let ast::Stmt::Export(inner, _) = stmt { inner.as_ref() } else { stmt };
        match stmt {
            ast::Stmt::FunctionDef { name, .. }
            | ast::Stmt::ExternC { name, .. }
            | ast::Stmt::RawDataU32 { name, .. }
            | ast::Stmt::NativeConst { name, .. }
            | ast::Stmt::StructDef { name, .. }
            | ast::Stmt::ClassDef { name, .. }
            | ast::Stmt::InterfaceDef { name, .. }
            | ast::Stmt::DataClassDef { name, .. }
            | ast::Stmt::Assignment { name, .. }
            | ast::Stmt::ConstAssignment { name, .. } => { namen.insert(name.clone()); }
            _ => {}
        }
    }
    namen
}

/// P2-T99 (Review 9502d95c): Namen, die eine Datei ausdruecklich mit
/// `aus x importiere a, b` holt; sie gelten fuer M-I002 und `q.name` als ihre
/// eigenen (Weitergabe). Ein voller `importiere x` gibt nichts weiter (E2).
/// Review 2be97766: je Name die kanonischen Quellmodule, damit eine Kette
/// (auch ein Zyklus) bis zu einer echten Definition verfolgt werden kann.
fn weitergegebene_namen(
    stmts: &[ast::Stmt],
    dir: &std::path::Path,
    lib_paths: &[PathBuf],
) -> std::collections::HashMap<String, Vec<PathBuf>> {
    let mut weitergaben: std::collections::HashMap<String, Vec<PathBuf>> = std::collections::HashMap::new();
    for stmt in stmts {
        let ast::Stmt::Import { module, names, .. } = stmt else { continue };
        if names.is_empty() { continue; }
        // Nicht gefundene Module meldet die Rekursion als M-I001.
        let Some(pfad) = find_module(module, dir, lib_paths) else { continue };
        let kanonisch = pfad.canonicalize().unwrap_or(pfad);
        for name in names {
            weitergaben.entry(name.clone()).or_default().push(kanonisch.clone());
        }
    }
    weitergaben
}

/// P2-T99 (Review 2be97766): Ist `name` im Modul `kanonisch` selbst definiert
/// oder ueber eine Weitergabekette bis zu einer echten Definition erreichbar?
/// `besucht` bricht Zyklen ab; ein Zyklus ohne Definition liefert false.
fn weitergabe_definiert(
    kontext: &ModulKontext,
    kanonisch: &std::path::Path,
    name: &str,
    besucht: &mut std::collections::HashSet<PathBuf>,
) -> bool {
    if !besucht.insert(kanonisch.to_path_buf()) { return false; }
    if kontext.definiert.get(kanonisch).is_some_and(|n| n.contains(name)) { return true; }
    kontext.weitergaben.get(kanonisch)
        .and_then(|w| w.get(name))
        .is_some_and(|quellen| quellen.iter().any(|q| weitergabe_definiert(kontext, q, name, besucht)))
}

/// Strukturen einer Datei gegen das Programm pruefen: im globalen Namensraum
/// ist ein Strukturname programmweit eindeutig (E4), die Meldung nennt beide Dateien.
fn strukturen_registrieren(
    stmts: &[ast::Stmt],
    datei: &std::path::Path,
    kontext: &mut ModulKontext,
) -> Result<(), String> {
    let kanonisch = datei.canonicalize().unwrap_or_else(|_| datei.to_path_buf());
    for stmt in stmts {
        let stmt = if let ast::Stmt::Export(inner, _) = stmt { inner.as_ref() } else { stmt };
        if let ast::Stmt::StructDef { name, .. } = stmt {
            match kontext.strukturen.get(name) {
                Some(erste) if *erste != kanonisch => {
                    return Err(format!(
                        "Struktur '{name}' ist bereits definiert: {} und {}",
                        erste.display(), kanonisch.display()
                    ));
                }
                Some(_) => {}
                None => { kontext.strukturen.insert(name.clone(), kanonisch.clone()); }
            }
        }
    }
    Ok(())
}

/// P2-T99: Art einer Top-Level-Definition fuer die Kollisionspruefung.
/// Struktur gegen Struktur prueft strukturen_registrieren, extern gegen extern
/// ist dasselbe C-Symbol; gemischt (z. B. Struktur gegen Funktion) zaehlt.
fn definitions_art(stmt: &ast::Stmt) -> Option<(&str, &'static str)> {
    use ast::Stmt as S;
    Some(match stmt {
        S::FunctionDef { name, .. } => (name.as_str(), "Funktion"),
        S::ClassDef { name, .. } => (name.as_str(), "Klasse"),
        S::DataClassDef { name, .. } => (name.as_str(), "Datenklasse"),
        S::InterfaceDef { name, .. } => (name.as_str(), "Schnittstelle"),
        S::ConstAssignment { name, .. } | S::NativeConst { name, .. } | S::RawDataU32 { name, .. } => {
            (name.as_str(), "Konstante")
        }
        S::StructDef { name, .. } => (name.as_str(), "Struktur"),
        S::ExternC { name, .. } => (name.as_str(), "Extern"),
        S::Assignment { name, .. } => (name.as_str(), "Variable"),
        _ => return None,
    })
}

/// P2-T99: Top-Level-Definitionen einer Datei gegen alle bisher bekannten
/// pruefen. Im Inline-Modell gibt es je Name genau eine globale Definition;
/// die zuerst eingefuegte gewinnt still (codegen Duplikat-Guard), auch fuer
/// Aufrufe IM Modul. Darum ist derselbe Name aus zwei Dateien ein Fehler
/// (M-I004), mit einer Variablen beteiligt ein Hinweis (M-I005: gemeinsamer
/// globaler Zustand). Ort ist die Benutzerdefinition, sonst die spaetere
/// Moduldefinition. Dieselbe Datei (Rautenimport, Neuzuweisung) bleibt still.
fn definitionen_registrieren(stmts: &[ast::Stmt], datei: &std::path::Path, kontext: &mut ModulKontext) {
    let kanonisch = datei.canonicalize().unwrap_or_else(|_| datei.to_path_buf());
    let cwd = std::env::current_dir().unwrap_or_default();
    let anzeige = kanonisch.strip_prefix(&cwd).unwrap_or(datei).display().to_string();
    let hauptdatei = kontext.hauptdatei.clone();
    for stmt in stmts {
        let stmt = if let ast::Stmt::Export(inner, _) = stmt { inner.as_ref() } else { stmt };
        let Some((name, art)) = definitions_art(stmt) else { continue };
        let neu = Definition { datei: anzeige.clone(), kanonisch: kanonisch.clone(), zeile: stmt.zeile(), art };
        // Review 9502d95c: ALLE Vorkommen je Name halten (erstes je Datei und
        // Art), sonst verdeckt eine zuerst gesehene Variable die spaetere
        // Funktions-Kollision zweier Module.
        let bisher = kontext.definitionen.get(name).cloned().unwrap_or_default();
        if bisher.iter().any(|e| e.kanonisch == neu.kanonisch && e.art == neu.art) {
            continue;
        }
        kontext.definitionen.entry(name.to_string()).or_default().push(neu.clone());
        // P2-T98 (Entscheid Koordinator, c5622262): Funktion/extern darf kein
        // unsicheres Builtin verschatten, sonst ist die unsicher:-Pruefung umgehbar.
        if matches!(art, "Funktion" | "Extern") && native_system::ist_unsicheres_builtin(name) {
            kontext.verschattungen += 1;
            diagnose::melden(diagnose::Diagnose::fehler("M-I006", format!(
                "{art} '{name}' verschattet das unsichere Builtin '{name}'; ob ein Aufruf die eigene Definition oder den Rohzugriff meint, waere nicht eindeutig und die unsicher:-Pruefung umgehbar"))
                .vorschlag(format!("Die eigene {art} umbenennen, z. B. '{name}_eigen'"))
                .datei(neu.datei.clone()).ort(neu.zeile, 0).gefunden(name.to_string()));
        }
        let fremd = || bisher.iter().filter(|e| e.kanonisch != neu.kanonisch);
        let vertraeglich = |a: &str, b: &str| a == b && (a == "Struktur" || a == "Extern");
        let hart = fremd().find(|e| e.art != "Variable" && neu.art != "Variable" && !vertraeglich(e.art, neu.art));
        let weich = fremd().find(|e| e.art == "Variable" || neu.art == "Variable");
        let Some(alt) = hart.or(weich).cloned() else { continue };
        let (hier, dort) = if hauptdatei.as_ref() == Some(&alt.kanonisch) { (&alt, &neu) } else { (&neu, &alt) };
        let benutzer = hauptdatei.as_ref() == Some(&hier.kanonisch);
        let d = if hier.art == "Variable" || dort.art == "Variable" {
            diagnose::Diagnose::hinweis("M-I005", format!(
                "{} '{name}' gibt es auch in {}:{} ({}); beide teilen denselben globalen Namen",
                hier.art, dort.datei, dort.zeile, dort.art))
                .vorschlag("Umbenennen, falls kein gemeinsamer Zustand gewollt ist")
        } else {
            kontext.kollisionen += 1;
            let (meldung, vorschlag) = if benutzer {
                (format!(
                    "{} '{name}' ist schon im Modul {}:{} definiert ({}); Module werden in einen gemeinsamen Namensraum eingefügt, eine Definition würde die andere still ersetzen, auch für Aufrufe im Modul",
                    hier.art, dort.datei, dort.zeile, dort.art),
                 format!("Eigene {} umbenennen; auch `aus … importiere …` fügt alle Definitionen des Moduls ein, auch interne Helfer", hier.art))
            } else {
                (format!(
                    "{} '{name}' ist auch in {}:{} definiert ({}); im gemeinsamen Namensraum gilt nur eine, die andere wird still ersetzt, auch für Aufrufe im Modul",
                    hier.art, dort.datei, dort.zeile, dort.art),
                 "Eine der Definitionen umbenennen oder sie aus dem anderen Modul importieren statt sie zu kopieren".to_string())
            };
            diagnose::Diagnose::fehler("M-I004", meldung).vorschlag(vorschlag)
        };
        diagnose::melden(d.datei(hier.datei.clone()).ort(hier.zeile, 0).gefunden(format!("{}:{}", dort.datei, dort.zeile)));
    }
}

/// Löst Imports AST-basiert auf: parst jedes Modul genau einmal (visited) und
/// fügt seine Definitionen inline in EINEN globalen Namensraum vor dem
/// importierenden Programm ein (kein Präfix, keine Alias-Funktionen).
/// Qualifizierte Schreibweisen `q.name(...)`, `q.NAME`, `q.Typ` der Datei
/// werden danach geprueft auf den einfachen Namen zurueckgefuehrt (E2/E3).
fn resolve_modules(
    program: &mut ast::Program,
    datei: &std::path::Path,
    visited: &mut std::collections::HashSet<PathBuf>,
    lib_paths: &[PathBuf],
) -> Result<(), String> {
    let mut kontext = ModulKontext::default();
    // P2-T99: eigene Definitionen der Hauptdatei zuerst (Benutzerseite).
    kontext.hauptdatei = Some(datei.canonicalize().unwrap_or_else(|_| datei.to_path_buf()));
    definitionen_registrieren(&program.statements, datei, &mut kontext);
    resolve_modules_mit(program, datei, visited, lib_paths, &mut kontext)?;
    // P2-T99 (Review 2be97766): ein nur weitergegebener Name muss irgendwo in
    // der Kette echt definiert sein; ein Zyklus ohne Definition ist M-I002.
    for offen in &kontext.offene_weitergaben {
        let mut besucht = std::collections::HashSet::new();
        if !weitergabe_definiert(&kontext, &offen.kanonisch, &offen.name, &mut besucht) {
            return Err(diagnose::melde_fehler(diagnose::Diagnose::fehler("M-I002", format!(
                "Modul '{}' definiert '{}' nicht, es reicht den Namen nur weiter und in der Importkette gibt es keine Definition (aus {} importiere {}; Modul {})",
                offen.modul, offen.name, offen.modul, offen.name, offen.modul_datei
            )).datei(offen.datei.clone()).ort(offen.zeile, 0)));
        }
    }
    match (kontext.kollisionen, kontext.verschattungen) {
        (0, 0) => Ok(()),
        (0, n) => Err(format!("{n} Definition(en) verschatten unsichere Builtins (M-I006)")),
        (n, _) => Err(format!("{n} doppelte Definition(en) aus verschiedenen Dateien (M-I004)")),
    }
}

/// Loest die Importe einer Datei auf (rekursiv fuer jedes neu besuchte Modul).
fn resolve_modules_mit(
    program: &mut ast::Program,
    datei: &std::path::Path,
    visited: &mut std::collections::HashSet<PathBuf>,
    lib_paths: &[PathBuf],
    kontext: &mut ModulKontext,
) -> Result<(), String> {
    let file_dir = datei.parent().unwrap_or(std::path::Path::new("."));
    let mut imported_stmts: Vec<ast::Stmt> = Vec::new();
    let mut remaining_stmts: Vec<ast::Stmt> = Vec::new();
    let mut qualifizierer: std::collections::HashMap<String, Qualifizierer> = std::collections::HashMap::new();

    for stmt in program.statements.drain(..) {
        match &stmt {
            ast::Stmt::Import { module, names, alias, line } => {
                if let Some(mod_path) = find_module(module, file_dir, lib_paths) {
                    let canonical = mod_path.canonicalize().unwrap_or(mod_path.clone());
                    let mut neues_modul = None;
                    if !visited.contains(&canonical) {
                        visited.insert(canonical.clone());
                        // Modul parsen; eigene Namen VOR der Rekursion merken,
                        // damit auch ein Zyklus/Rautenimport sie findet. E2: nur
                        // die SELBST definierten Namen, nicht die seiner Importe.
                        let mut mod_program = parse_file(&mod_path)?;
                        // P2-T29: Modul vor dem Inlining pruefen (Meldung mit Moduldatei).
                        typpruefung::pruefe_datei(&mod_program, &mod_path.display().to_string());
                        let definiert = eigene_namen(&mod_program.statements);
                        // Review 9502d95c: ausdruecklich per `aus x importiere a`
                        // geholte Namen gibt das Modul weiter (primzahl -> mathe).
                        // Review 2be97766: getrennt gemerkt; gedeckt erst durch
                        // eine echte Definition irgendwo in der Kette.
                        let mod_dir = mod_path.parent().unwrap_or(std::path::Path::new("."));
                        let weitergaben = weitergegebene_namen(&mod_program.statements, mod_dir, lib_paths);
                        let mut eigene = definiert.clone();
                        eigene.extend(weitergaben.keys().cloned());
                        strukturen_registrieren(&mod_program.statements, &mod_path, kontext)?;
                        definitionen_registrieren(&mod_program.statements, &mod_path, kontext);
                        kontext.namen.insert(canonical.clone(), std::rc::Rc::new(eigene));
                        kontext.definiert.insert(canonical.clone(), definiert);
                        kontext.weitergaben.insert(canonical.clone(), weitergaben);
                        // Rekursiv Imports im Modul aufloesen
                        resolve_modules_mit(&mut mod_program, &mod_path, visited, lib_paths, kontext)?;
                        neues_modul = Some(mod_program);
                    }
                    let modul_namen = kontext.namen.get(&canonical).cloned().unwrap_or_default();
                    if names.is_empty() {
                        let q = match alias {
                            Some(a) => a.clone(),
                            None => std::path::Path::new(module).file_stem()
                                .map(|s| s.to_string_lossy().into_owned())
                                .unwrap_or_else(|| module.clone()),
                        };
                        qualifizierer.insert(q, Qualifizierer {
                            modul: module.clone(),
                            alias: alias.is_some(),
                            namen: modul_namen.clone(),
                        });
                    } else {
                        // O2: unbekannter Name in `aus m importiere x` ist ein Fehler mit Herkunft.
                        for name in names {
                            // Review 2be97766: nur weitergegeben -> nach dem Aufloesen
                            // bis zu einer echten Definition verfolgen.
                            let selbst = kontext.definiert.get(&canonical).is_some_and(|n| n.contains(name));
                            if !selbst && modul_namen.contains(name) {
                                kontext.offene_weitergaben.push(OffeneWeitergabe {
                                    datei: datei.display().to_string(),
                                    zeile: *line,
                                    modul: module.clone(),
                                    name: name.clone(),
                                    modul_datei: mod_path.display().to_string(),
                                    kanonisch: canonical.clone(),
                                });
                                continue;
                            }
                            if !modul_namen.contains(name) {
                                return Err(diagnose::melde_fehler(diagnose::Diagnose::fehler("M-I002", format!(
                                    "Modul '{module}' definiert '{name}' nicht (aus {module} importiere {name}; Modul {})",
                                    mod_path.display()
                                )).datei(datei.display().to_string()).ort(*line, 0)));
                            }
                        }
                    }
                    let Some(mod_program) = neues_modul else { continue };
                    // P2-T95: Quelldatei je Modulfunktion fuer Fehlerhinweise (Datei:Zeile).
                    for (funktion, quelle) in &mod_program.funktion_quellen {
                        program.funktion_quellen.entry(funktion.clone()).or_insert_with(|| quelle.clone());
                    }
                    for mod_stmt in &mod_program.statements {
                        let definition = match mod_stmt {
                            ast::Stmt::Export(inner, _) => inner.as_ref(),
                            other => other,
                        };
                        // P2-T96: auch Klassen (Datei ihrer Methoden fuer M-F001).
                        if let ast::Stmt::FunctionDef { name, .. } | ast::Stmt::ClassDef { name, .. } = definition {
                            program.funktion_quellen.entry(name.clone())
                                .or_insert_with(|| mod_path.display().to_string());
                        }
                    }

                    if names.is_empty() {
                        // "importiere mathe" → ALLE Statements direkt einfuegen (kein Prefix!)
                        // exportiere-Wrapper auswickeln, damit die Definition codegen erreicht.
                        imported_stmts.extend(mod_program.statements.into_iter().map(strip_export));
                    } else {
                        // "aus mathe importiere fakultaet" → alle DEFINITIONEN des Moduls
                        // (Funktionen, Klassen, Interfaces, DataClasses; exportiere ausgewickelt) plus alle
                        // Variablen/Konstanten und Rohdaten. Nur ausfuehrbare Top-Level-Statements werden
                        // verworfen, damit ein Import keine Seiteneffekte ausloest. Im inline-
                        // Modell bleibt jede Definition des Moduls erhalten (Helfer, Klassen);
                        // die Namensliste wird gegen die eigenen Namen des Moduls geprueft.
                        for mod_stmt in mod_program.statements {
                            match strip_export(mod_stmt) {
                                s @ (ast::Stmt::FunctionDef { .. }
                                    | ast::Stmt::StructDef { .. }
                                    | ast::Stmt::ExternC { .. }
                                    | ast::Stmt::ClassDef { .. }
                                    | ast::Stmt::InterfaceDef { .. }
                                    | ast::Stmt::DataClassDef { .. }
                                    | ast::Stmt::Assignment { .. }
                                    | ast::Stmt::ConstAssignment { .. }
                                    | ast::Stmt::NativeConst { .. }
                                    | ast::Stmt::RawDataU32 { .. }) => {
                                    imported_stmts.push(s);
                                }
                                _ => {}
                            }
                        }
                    }
                } else {
                    // Frueher still verworfen -> spaeter nur 'Funktion nicht gefunden'.
                    return Err(diagnose::melde_fehler(diagnose::Diagnose::fehler("M-I001", format!(
                        "Modul '{module}' nicht gefunden (gesucht in: {})",
                        module_search_locations(module, file_dir, lib_paths).join(", ")
                    )).datei(datei.display().to_string()).ort(*line, 0)));
                }
            }
            // Eigene Statements der Datei (inkl. same-file `exportiere funktion/klasse`)
            _ => remaining_stmts.push(strip_export(stmt)),
        }
    }

    // Eigene Strukturen gegen die importierten pruefen (beide Pfade in der Meldung).
    strukturen_registrieren(&remaining_stmts, datei, kontext)?;

    // Qualifizierte Schreibweisen der EIGENEN Anweisungen aufloesen (E2/E3).
    // Variablenvorrang lexikalisch: globale Bindungen = Top-Level-Variablen der
    // Datei und globale Variablen der importierten Module; Parameter/Lokale
    // gelten nur in ihrem Geltungsbereich (Scope-Stack im Laeufer).
    let mut global = std::collections::HashSet::new();
    for stmt in &imported_stmts {
        if let ast::Stmt::Assignment { name, .. } | ast::Stmt::ConstAssignment { name, .. } = stmt {
            global.insert(name.clone());
        }
    }
    sammle_bindungen(&remaining_stmts, &mut global);
    let mut laeufer = QualiLaeufer {
        tabelle: &qualifizierer,
        global,
        scopes: Vec::new(),
        gewarnt: std::collections::HashSet::new(),
        datei,
    };
    laeufer.stmts(&mut remaining_stmts)?;

    // Importierte Statements VOR dem Hauptprogramm einfuegen
    imported_stmts.extend(remaining_stmts);
    program.statements = imported_stmts;
    Ok(())
}

/// Namen, die Anweisungen in IHREM Geltungsbereich binden (Zuweisungen,
/// Schleifen-, catch- und Match-Binder). Steigt in Bloecke ab, aber nicht in
/// Funktionen, Klassen, Tests oder Lambdas (eigene Geltungsbereiche).
fn sammle_bindungen(stmts: &[ast::Stmt], out: &mut std::collections::HashSet<String>) {
    use ast::Stmt as S;
    for s in stmts {
        match s {
            S::Assignment { name, .. } | S::ConstAssignment { name, .. }
            | S::CompoundAssignment { name, .. } => { out.insert(name.clone()); }
            S::For { var_name, body, .. } | S::ParallelFor { var_name, body, .. } => {
                out.insert(var_name.clone());
                sammle_bindungen(body, out);
            }
            S::If { body, else_body, .. } => {
                sammle_bindungen(body, out);
                sammle_bindungen(else_body, out);
            }
            S::While { body, .. } | S::UnsafeBlock { body, .. } | S::Guard { else_body: body, .. } => {
                sammle_bindungen(body, out);
            }
            S::TryCatch { try_body, catch_var, catch_body, .. } => {
                if let Some(v) = catch_var { out.insert(v.clone()); }
                sammle_bindungen(try_body, out);
                sammle_bindungen(catch_body, out);
            }
            S::Match { cases, .. } => {
                for (muster, waechter, body) in cases {
                    match muster {
                        Some(ast::Expr::Identifier(n)) if waechter.is_some() => { out.insert(n.clone()); }
                        Some(ast::Expr::Dict(paare)) => {
                            for (_, wert) in paare {
                                if let ast::Expr::Identifier(n) = wert { out.insert(n.clone()); }
                            }
                        }
                        _ => {}
                    }
                    sammle_bindungen(body, out);
                }
            }
            S::Export(inner, _) => sammle_bindungen(std::slice::from_ref(inner.as_ref()), out),
            // Eigene Geltungsbereiche oder ohne Bindung.
            _ => {}
        }
    }
}

/// P2-T47 (Plan b1f51d65 C2): AST-Laeufer fuer qualifizierte Namen. Vollstaendige
/// Matches ohne Sammelarm: ein neuer AST-Knoten erzwingt hier eine Entscheidung.
/// Variablenvorrang lexikalisch (Review 49a04c50): `global` plus Scope-Stack.
struct QualiLaeufer<'a> {
    tabelle: &'a std::collections::HashMap<String, Qualifizierer>,
    global: std::collections::HashSet<String>,
    scopes: Vec<std::collections::HashSet<String>>,
    gewarnt: std::collections::HashSet<String>,
    datei: &'a std::path::Path,
}

impl QualiLaeufer<'_> {
    fn gebunden(&self, q: &str) -> bool {
        self.global.contains(q) || self.scopes.iter().any(|s| s.contains(q))
    }

    /// Ob `q.name` auf die Moduldefinition `name` zurueckgefuehrt wird. Eine an
    /// dieser Stelle sichtbare Variable `q` hat Vorrang (Warnung bei Alias). Ohne
    /// Alias bleibt ein unbekanntes Mitglied beim Bestandsverhalten (Variable
    /// bzw. eingebauter Namensraum gleichen Namens); mit Alias ist es ein Fehler.
    fn ziel(&mut self, q: &str, name: &str) -> Result<bool, String> {
        let Some(ql) = self.tabelle.get(q) else { return Ok(false) };
        if self.gebunden(q) {
            if ql.alias && self.gewarnt.insert(q.to_string()) {
                diagnose::melden(diagnose::Diagnose::hinweis("M-H003", format!(
                    "Modulalias '{q}' (Modul '{}') wird von einer Variable gleichen Namens verdeckt; '{q}.{name}' greift auf die Variable zu",
                    ql.modul
                )).datei(self.datei.display().to_string()));
            }
            return Ok(false);
        }
        if ql.namen.contains(name) { return Ok(true); }
        if ql.alias {
            return Err(diagnose::melde_fehler(diagnose::Diagnose::fehler("M-I002", format!(
                "Modul '{}' (Alias '{q}') definiert '{name}' nicht", ql.modul
            )).datei(self.datei.display().to_string())));
        }
        Ok(false)
    }

    /// Punktname `q.Name` (Typ oder `neu q.Klasse`) auf `Name` zurueckfuehren.
    /// `zeile` 0 = unbekannt (P2-T97: Diagnose mit Datei/Zeile).
    fn punktname(&self, name: &mut String, zeile: usize) -> Result<(), String> {
        if let Some((q, glied)) = name.split_once('.') {
            let datei = self.datei.display().to_string();
            let Some(ql) = self.tabelle.get(q) else {
                return Err(diagnose::melde_fehler(diagnose::Diagnose::fehler("M-I000", format!(
                    "Unbekannter Modulqualifizierer '{q}' in '{name}'"
                )).datei(datei).ort(zeile, 0)));
            };
            if !ql.namen.contains(glied) {
                return Err(diagnose::melde_fehler(diagnose::Diagnose::fehler("M-I002", format!(
                    "Modul '{}' definiert '{glied}' nicht", ql.modul
                )).datei(datei).ort(zeile, 0)));
            }
            *name = glied.to_string();
        }
        Ok(())
    }

    fn typ(&mut self, ty: &mut ast::TypeRef) -> Result<(), String> {
        match &mut ty.kind {
            ast::TypeKind::Name(name) => {
                self.punktname(name, ty.line)?;
            }
            ast::TypeKind::Generic { args, .. } => {
                for arg in args { self.typ(arg)?; }
            }
            ast::TypeKind::Array { element, length } => {
                self.typ(element)?;
                self.expr(length)?;
            }
            ast::TypeKind::Function { params, return_type, .. } => {
                for p in params { self.typ(p)?; }
                self.typ(return_type)?;
            }
        }
        Ok(())
    }

    fn typ_opt(&mut self, ty: &mut Option<ast::TypeRef>) -> Result<(), String> {
        if let Some(t) = ty { self.typ(t)?; }
        Ok(())
    }

    fn expr_opt(&mut self, e: &mut Option<Box<ast::Expr>>) -> Result<(), String> {
        if let Some(e) = e { self.expr(e)?; }
        Ok(())
    }

    fn expr(&mut self, e: &mut ast::Expr) -> Result<(), String> {
        use ast::Expr as E;
        match e {
            E::Number(_) | E::NativeNumber { .. } | E::String(_) | E::Boolean(_) | E::None
            | E::Identifier(_) | E::This => {}
            E::BinaryOp { left, right, .. }
            | E::NullishCoalesce { left, right }
            | E::Pipe { left, right } => { self.expr(left)?; self.expr(right)?; }
            E::UnaryOp { operand, .. } => self.expr(operand)?,
            E::Spread(inner) => self.expr(inner)?,
            E::FunctionCall { args, .. } | E::List(args) => {
                for a in args { self.expr(a)?; }
            }
            E::New { class_name, args } => {
                self.punktname(class_name, 0)?;
                for a in args { self.expr(a)?; }
            }
            E::MethodCall { object, method, args } => {
                for a in args.iter_mut() { self.expr(a)?; }
                let q = if let E::Identifier(q) = object.as_ref() { Some(q.clone()) } else { None };
                if let Some(q) = q {
                    if self.ziel(&q, method)? {
                        let neu = E::FunctionCall { name: method.clone(), type_args: Vec::new(), args: std::mem::take(args), zeile: 0, spalten: (0, 0) };
                        *e = neu;
                        return Ok(());
                    }
                }
                self.expr(object)?;
            }
            E::PropertyAccess { object, property } => {
                let q = if let E::Identifier(q) = object.as_ref() { Some(q.clone()) } else { None };
                if let Some(q) = q {
                    if self.ziel(&q, property)? {
                        let neu = E::Identifier(property.clone());
                        *e = neu;
                        return Ok(());
                    }
                }
                self.expr(object)?;
            }
            E::OptionalChain { object, .. } => self.expr(object)?,
            E::IndexAccess { object, index } => { self.expr(object)?; self.expr(index)?; }
            E::Range { start, end } => { self.expr(start)?; self.expr(end)?; }
            E::ListComprehension { expr, var_name, iterable, condition } => {
                self.expr(iterable)?;
                self.scopes.push(std::iter::once(var_name.clone()).collect());
                let r = self.expr(expr).and_then(|_| self.expr_opt(condition));
                self.scopes.pop();
                r?;
            }
            E::Dict(pairs) => {
                for (k, v) in pairs { self.expr(k)?; self.expr(v)?; }
            }
            E::Lambda { params, param_types, body } => {
                for t in param_types.iter_mut() { self.typ_opt(t)?; }
                self.scopes.push(params.iter().cloned().collect());
                let r = self.expr(body);
                self.scopes.pop();
                r?;
            }
            E::Ternary { condition, then_val, else_val } => {
                self.expr(condition)?;
                self.expr(then_val)?;
                self.expr(else_val)?;
            }
            E::MatchExpr { value, cases } => {
                self.expr(value)?;
                for (muster, waechter, ergebnis) in cases {
                    if let Some(m) = muster { self.expr(m)?; }
                    if let Some(w) = waechter { self.expr(w)?; }
                    self.expr(ergebnis)?;
                }
            }
            E::QueryExpr { var_name, source, where_cond, order_expr, select_expr } => {
                self.expr(source)?;
                self.scopes.push(std::iter::once(var_name.clone()).collect());
                let r = self.expr_opt(where_cond)
                    .and_then(|_| self.expr_opt(order_expr))
                    .and_then(|_| self.expr(select_expr));
                self.scopes.pop();
                r?;
            }
        }
        Ok(())
    }

    fn stmts(&mut self, stmts: &mut [ast::Stmt]) -> Result<(), String> {
        for s in stmts { self.stmt(s)?; }
        Ok(())
    }

    /// Eigener Geltungsbereich (Funktion, Test): Parameter + lokale Binder.
    fn im_scope(&mut self, mut namen: std::collections::HashSet<String>, body: &mut [ast::Stmt]) -> Result<(), String> {
        sammle_bindungen(body, &mut namen);
        self.scopes.push(namen);
        let r = self.stmts(body);
        self.scopes.pop();
        r
    }

    fn stmt(&mut self, s: &mut ast::Stmt) -> Result<(), String> {
        use ast::Stmt as S;
        match s {
            S::Assignment { ty, value, .. } | S::ConstAssignment { ty, value, .. } => {
                self.typ_opt(ty)?;
                self.expr(value)?;
            }
            S::CompoundAssignment { value, .. } => self.expr(value)?,
            S::PropertyAssignment { object, value, .. } => { self.expr(object)?; self.expr(value)?; }
            S::IndexAssignment { object, index, value, .. } => {
                self.expr(object)?;
                self.expr(index)?;
                self.expr(value)?;
            }
            S::Show(e, _) | S::Throw(e, _) | S::Expression(e, _) | S::Defer(e, _) | S::Expect(e, _) => self.expr(e)?,
            S::If { condition, body, else_body, .. } => {
                self.expr(condition)?;
                self.stmts(body)?;
                self.stmts(else_body)?;
            }
            S::While { condition, body, .. } => { self.expr(condition)?; self.stmts(body)?; }
            S::For { iterable, body, .. } | S::ParallelFor { iterable, body, .. } => {
                self.expr(iterable)?;
                self.stmts(body)?;
            }
            S::Precondition { condition, .. } | S::Postcondition { condition, .. } => self.expr(condition)?,
            S::FunctionDef { params, param_types, return_type, defaults, body, .. } => {
                for t in param_types.iter_mut() { self.typ_opt(t)?; }
                self.typ_opt(return_type)?;
                for d in defaults.iter_mut().flatten() { self.expr(d)?; }
                let namen = params.iter().cloned().collect();
                self.im_scope(namen, body)?;
            }
            S::ExternC { params, result, .. } => {
                for (_, t) in params { self.typ(t)?; }
                self.typ(result)?;
            }
            S::RawDataU32 { ty, value, .. } => { self.typ_opt(ty)?; self.expr(value)?; }
            S::NativeConst { ty, value, .. } => {
                self.typ(ty)?;
                if let Some(value) = value { self.expr(value)?; }
            }
            S::Return(value, _) => { if let Some(v) = value { self.expr(v)?; } }
            S::Break(_) | S::Continue(_) | S::Import { .. } | S::Global(_, _) => {}
            S::StructDef { fields, methods, .. } => {
                for (_, t) in fields { self.typ(t)?; }
                self.stmts(methods)?;
            }
            S::ClassDef { body, .. } => self.stmts(body)?,
            S::InterfaceDef { signatures, .. } => {
                for sig in signatures.iter_mut().flatten() {
                    for t in sig.param_types.iter_mut() { self.typ_opt(t)?; }
                    for d in sig.defaults.iter_mut().flatten() { self.expr(d)?; }
                    self.typ_opt(&mut sig.return_type)?;
                }
            }
            S::TryCatch { try_body, catch_body, .. } => {
                self.stmts(try_body)?;
                self.stmts(catch_body)?;
            }
            S::Export(inner, _) => self.stmt(inner)?,
            S::Match { value, cases, .. } => {
                self.expr(value)?;
                for (muster, waechter, body) in cases {
                    if let Some(m) = muster { self.expr(m)?; }
                    if let Some(w) = waechter { self.expr(w)?; }
                    self.stmts(body)?;
                }
            }
            S::Guard { condition, else_body, .. } => { self.expr(condition)?; self.stmts(else_body)?; }
            S::DataClassDef { field_types, .. } => {
                for t in field_types.iter_mut() { self.typ_opt(t)?; }
            }
            S::UnsafeBlock { body, .. } => self.stmts(body)?,
            S::TestDef { body, .. } => self.im_scope(std::collections::HashSet::new(), body)?,
        }
        Ok(())
    }
}

/// Findet die Wurzel der Compiler-Quellen (enthaelt runtime/moo_bare.c).
/// (1) CARGO_MANIFEST_DIR (compile-zeit, Dev-Maschine = Normalfall),
/// (2) exe-relativ: target/release/moo-compiler -> ../../ = compiler/.
fn kernel_source_root() -> Option<PathBuf> {
    let manifest = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    if manifest.join("runtime").join("moo_bare.c").exists() {
        return Some(manifest);
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent().and_then(|p| p.parent()).and_then(|p| p.parent()) {
            if dir.join("runtime").join("moo_bare.c").exists() {
                return Some(dir.to_path_buf());
            }
        }
    }
    None
}

// Gemeinsame freestanding Wertkerne für Kernel- und Benutzerlaufzeit.
const NATIVE_VALUE_SOURCES: &[&str] = &[
    "boot/uefi_rt.c", "moo_value.c", "moo_memory.c", "moo_string.c",
    "moo_list.c", "moo_dict.c", "moo_stdlib.c", "moo_print.c", "moo_frame.c",
    "moo_object.c", "moo_buffer.c", "moo_buffer_index.c", "moo_struct_box.c",
    "moo_struct_dyn.c", "moo_handle.c", "moo_defer_stack.c", "moo_defer_error.c",
];

/// One-Shot-Kernel-Pipeline (Plan-010 C2): erzeugt aus einer .moos-Datei ein
/// bootbares Multiboot2-ELF. Reproduzierbar: FESTE Quell-Liste + FESTE
/// Flag-Liste, keine Umgebungs-Magie. MOO_VERBOSE=1 zeigt alle Kommandos.
fn build_kernel(
    file: &PathBuf,
    compiler: &codegen::CodeGen,
    output: Option<&std::path::Path>,
    target_str: &str,
    linker_script: Option<&std::path::Path>,
    emit: &str,
    board: Option<&'static boards::BoardProfile>,
    libraries: &[String],
    lib_paths: &[PathBuf],
) -> Result<(), String> {
    let resolved = codegen::CodeGen::resolve_triple(target_str);

    // P012-B4: Toolchain-Abstraktion pro resolved Triple statt hartem
    // x86_64-only-Guard. x86_64 = voller Boot-Pfad (Bestand, kein Regress);
    // aarch64-unknown-none = PoC bis moo-Objekt + C-Runtime-Objekte
    // (Entry/Linker-Script folgen mit P012-D5/E1); alles andere = klare
    // Diagnose mit Target-Nennung, KEIN stiller Success.
    enum KernelArch { X86_64, Aarch64 }
    let arch = if resolved.starts_with("x86_64") {
        KernelArch::X86_64
    } else if resolved == "aarch64-unknown-none" {
        KernelArch::Aarch64
    } else {
        return Err(format!(
            "--kernel: Target '{resolved}' hat noch keine Kernel-Toolchain. \
             Unterstuetzt: x86_64-bare (voller Multiboot2-Boot-Pfad) und \
             aarch64-bare (P012-B4-PoC bis C-Runtime-Objekte). Weitere \
             Targets: Plan P012-D/E."
        ));
    };

    // C-Compiler pro Arch: x86_64 nativ 'cc' (Host-Toolchain, Bestand);
    // aarch64 bevorzugt clang --target=aarch64-unknown-none (ein Binary,
    // alle Targets), dokumentierte GNU-Cross-Fallbacks. Fehlt alles:
    // Diagnose nennt Target UND fehlende Tools.
    fn tool_ok(cmd: &str) -> bool {
        Command::new(cmd).arg("--version").output()
            .map(|o| o.status.success()).unwrap_or(false)
    }
    let (cc_prog, cc_target_args, arch_cflags): (&str, Vec<String>, Vec<&str>) = match arch {
        KernelArch::X86_64 => (
            "cc",
            vec![],
            // x86-spezifisch: kein Red-Zone-Verlass in ISRs, Kernel-Code-Model.
            vec!["-mno-red-zone", "-mcmodel=kernel"],
        ),
        KernelArch::Aarch64 => {
            if tool_ok("clang") {
                ("clang", vec![format!("--target={resolved}")], vec![])
            } else if tool_ok("aarch64-linux-gnu-gcc") {
                ("aarch64-linux-gnu-gcc", vec![], vec![])
            } else if tool_ok("aarch64-elf-gcc") {
                ("aarch64-elf-gcc", vec![], vec![])
            } else {
                return Err(format!(
                    "--kernel ({resolved}): kein C-Compiler fuer aarch64 gefunden. \
                     Benoetigt: clang (bevorzugt, --target={resolved}) ODER \
                     aarch64-linux-gnu-gcc ODER aarch64-elf-gcc. \
                     Arch/CachyOS: sudo pacman -S clang"
                ));
            }
        }
    };

    // P012-C1: Board-Parameter -> generierte Defines fuer die C-Runtime
    // (P012-D1: PL011-Konsole liest MOO_BOARD_UART_BASE). Ohne --board:
    // leerer Vec, Bestandsverhalten byte-identisch.
    let board_defines: Vec<String> = match board {
        Some(b) => {
            // P012-H1: DRAFT-Boards (C2-Konvention) bauen, aber warnen laut.
            if b.beschreibung.starts_with("DRAFT") {
                eprintln!("[moo-kernel] WARNUNG: Board '{}' ist ein DRAFT — unverifizierte Werte, KEIN Gate, kein finaler Beweis.", b.name);
            }
            println!(
                "[moo-kernel] Board '{}': target={} load_addr={:#x} uart={}@{:#x} timer={} intc={}",
                b.name, b.target, b.load_addr, b.uart_kind, b.uart_base, b.timer_kind, b.intc_kind
            );
            let mut v = vec![format!("-DMOO_BOARD_UART_BASE={:#x}", b.uart_base)];
            v.push(match b.uart_kind {
                "pl011" => "-DMOO_BOARD_UART_PL011=1".to_string(),
                _ => "-DMOO_BOARD_UART_16550=1".to_string(),
            });
            // P012-D4: GIC-Basen als Defines (qemu-virt: GICv2).
            if let (Some(d), Some(c)) = (b.gic_dist_base, b.gic_cpu_base) {
                v.push(format!("-DMOO_BOARD_GIC_DIST_BASE={d:#x}"));
                v.push(format!("-DMOO_BOARD_GIC_CPU_BASE={c:#x}"));
            }
            v
        }
        None => Vec::new(),
    };
    // P2-T28 (Plan 607b20ba B2; Bootloader-Bezug P1-T118): liefert das Programm
    // eine native Konstante in .multiboot2, laesst moo_bare_boot.c den C-Header
    // aus. Ohne solche Konstante bleibt alles bytegleich (GRUB-Referenz).
    let mut board_defines = board_defines;
    if compiler.hat_sektion(".multiboot2") {
        println!("[moo-kernel] Multiboot2-Kopf aus Moo (.multiboot2), C-Header g_mb2_header entfaellt");
        board_defines.push("-DMOO_BARE_MB2_AUS_MOO=1".to_string());
    }

    let src_root = kernel_source_root().ok_or_else(|| {
        "Kernel-Runtime nicht gefunden: weder CARGO_MANIFEST_DIR noch exe-relativ \
         existiert runtime/moo_bare.c. Der moo-Quellbaum wird gebraucht.".to_string()
    })?;
    let runtime_dir = src_root.join("runtime");

    let verbose = std::env::var("MOO_VERBOSE").is_ok();
    let tmp = std::env::temp_dir().join(format!("moo-kernel-{}", std::process::id()));
    std::fs::create_dir_all(&tmp).map_err(|e| format!("tmp-Verzeichnis {}: {e}", tmp.display()))?;

    // (1) moo -> .o
    let moo_obj = tmp.join("moo_program.o");
    compiler.write_object(&moo_obj, target_str)?;
    let mut objects: Vec<PathBuf> = vec![moo_obj];

    // (2) Bare-Runtime uebersetzen. WICHTIG: KEIN -mgeneral-regs-only global —
    // die kern_*-MooValue-Wrapper nutzen double (SSE); die ISRs in
    // moo_bare_idt.c tragen __attribute__((interrupt, target("general-regs-only")))
    // bereits per Funktion (Plan-010 K4).
    // Surface- und Compositor-Kerne sind freestanding und werden in Hosted-
    // und Bare-Build identisch verwendet. Ein Hardware-Presenter (DRM/Display)
    // bleibt davon getrennt; dieses Gate startet weder UI noch QEMU.
    const KERNEL_SOURCES: [&str; 20] = [
        "moo_bare.c",
        "moo_freestanding_mem.c",
        "moo_native_math.c", // P2-T58: Float-%/** nativer Funktionen ohne libm
        "moo_surface_core.c",
        "moo_compositor_core.c",
        "moo_compositor_effects_state.c",
        "moo_compositor_effects_math.c",
        "moo_compositor_effects_damage.c",
        "moo_compositor_effects_cpu.c",
        "moo_compositor_animation.c",
        "moo_compositor_raster.c",
        "moo_bare_console.c",
        "moo_bare_alloc.c",
        "moo_bare_idt.c",
        "moo_bare_boot.c",
        "moo_bare_stackprot.c",
        "moo_bare_entry_arm64.c",
        "moo_bare_timer_arm64.c",
        "moo_bare_mmu_arm64.c",
        "moo_bare_gic_arm64.c",
    ];
    // Der native Laufvertrag schaltet diese freestanding Byte-APIs ein.
    // Andere Bare-Kerne (einschliesslich Bootsektor und ARM) behalten ihre Quellen.
    const KERNEL_KRYPTO_SOURCES: [&str; 7] = [
        "moo_krypto.c",
        "moo_krypto_chacha.c",
        "moo_krypto_x25519.c",
        "moo_bare_krypto_test.c",
        "mlkem-native/mlkem_native.c",
        "monocypher/monocypher.c",
        "monocypher/monocypher-ed25519.c",
    ];
    let crypto_sources: &[&str] = if compiler.hat_sektion(".data.moo_krypto") {
        if !matches!(arch, KernelArch::X86_64) {
            return Err("Bare-Krypto mit RDSEED/RDRAND braucht x86_64-bare".to_string());
        }
        &KERNEL_KRYPTO_SOURCES
    } else {
        &[]
    };
    let mono_font_sources: &[&str] = if compiler.hat_sektion(".data.moo_font_aa") {
        &["moo_surface.c"]
    } else {
        &[]
    };
    let thread_runtime = compiler.hat_sektion(crate::runtime_provider::SECTION);
    let runtime_sources: &[&str] = if thread_runtime {
        NATIVE_VALUE_SOURCES
    } else { &[] };
    const KERNEL_CFLAGS: [&str; 10] = [
        "-c", "-ffreestanding", "-fno-pic", "-fno-pie",
        "-O2", "-nostdlib",
        "-Wall", "-Wextra", "-std=c11", "-DMOO_BARE",
    ];

    // P012-A4: Freestanding Stack-Protector. Der Canary MUSS aus dem
    // globalen Symbol __stack_chk_guard kommen (moo_bare_stackprot.c) —
    // der x86_64-TLS-Default (%fs:0x28) waere im Kernel Garbage (kein FS).
    // Probe, ob die GEWAEHLTE Toolchain -mstack-protector-guard=global
    // traegt (gcc>=8 / clang, arch-abhaengig); sonst ehrlicher Fallback.
    let ssp_flags: &[&str] = {
        let mut probe_cmd = Command::new(cc_prog);
        probe_cmd.args(&cc_target_args);
        let probe = probe_cmd
            .args([
                "-fstack-protector-strong", "-mstack-protector-guard=global",
                "-x", "c", "-c", "-o", "/dev/null", "/dev/null",
            ])
            .output();
        match probe {
            Ok(o) if o.status.success() => {
                if verbose {
                    eprintln!("[moo-kernel] SSP aktiv: -fstack-protector-strong -mstack-protector-guard=global");
                }
                &["-fstack-protector-strong", "-mstack-protector-guard=global"]
            }
            _ => {
                eprintln!("[moo-kernel] SSP: {cc_prog} ({resolved}) traegt -mstack-protector-guard=global nicht — Kernel ohne Stack-Protector");
                &["-fno-stack-protector"]
            }
        }
    };

    for src in KERNEL_SOURCES.iter().chain(crypto_sources).chain(mono_font_sources).chain(runtime_sources) {
        // Das Vollprofil benutzt die gemeinsame Wertausgabe; die rohe Early-
        // Console bleibt nur im unveränderten Minimalprofil.
        if thread_runtime && *src == "moo_bare_console.c" { continue; }
        let src_path = runtime_dir.join(src);
        if !src_path.exists() {
            return Err(format!("Kernel-Runtime-Quelle fehlt: {}", src_path.display()));
        }
        let obj = tmp.join(format!("{}.o", src.replace('/', "_")));
        let mut cmd = Command::new(cc_prog);
        if thread_runtime {
            cmd.args(["-DMOO_BARE_THREADS", "-DMOO_UEFI_COLLECTIONS", "-DMOO_HAS_SURFACE", "-D_GNU_SOURCE"]);
            if *src == "boot/uefi_rt.c" { cmd.arg("-DMOO_UEFI_USE_BARE_RUNTIME"); }
        }
        if mono_font_sources.contains(src) {
            cmd.arg("-DMOO_BARE_FONT_MONO");
        }
        if crypto_sources.contains(src) {
            cmd.arg("-mgeneral-regs-only")
                .arg("-I")
                .arg(runtime_dir.join("mlkem-native"));
        }
        if emit == "elf" {
            cmd.args(["-ffunction-sections", "-fdata-sections"]);
        }
        cmd.args(&cc_target_args)
            .args(KERNEL_CFLAGS)
            .args(&arch_cflags)
            .args(ssp_flags)
            .args(&board_defines)
            .arg("-I").arg(&runtime_dir)
            .arg(&src_path)
            .arg("-o").arg(&obj);
        if verbose {
            eprintln!("[moo-kernel] {cmd:?}");
        }
        let out = cmd.output().map_err(|e| {
            format!("{cc_prog} nicht ausfuehrbar ({e}) — C-Compiler fuer {resolved} installieren")
        })?;
        if !out.status.success() {
            return Err(format!(
                "{cc_prog} ({resolved}) fehlgeschlagen fuer {src}:\n{}",
                String::from_utf8_lossy(&out.stderr)
            ));
        }
        objects.push(obj);
    }

    // P012-D5: Linker pro Arch. x86_64: GNU ld (Host-binutils, Bestand).
    // aarch64: ld.lld bevorzugt (multi-target), dokumentierte GNU-Cross-
    // Fallbacks; fehlt alles -> Diagnose nennt Target + Kandidaten.
    let ld_prog: &str = match arch {
        KernelArch::X86_64 => "ld",
        KernelArch::Aarch64 => {
            if tool_ok("ld.lld") {
                "ld.lld"
            } else if tool_ok("aarch64-linux-gnu-ld") {
                "aarch64-linux-gnu-ld"
            } else if tool_ok("aarch64-elf-ld") {
                "aarch64-elf-ld"
            } else {
                return Err(format!(
                    "--kernel ({resolved}): kein Linker fuer aarch64 gefunden. \
                     Benoetigt: ld.lld (bevorzugt) ODER aarch64-linux-gnu-ld \
                     ODER aarch64-elf-ld. Arch/CachyOS: sudo pacman -S lld"
                ));
            }
        }
    };

    // P011-C2: Boot-asm-Templates (Stage1 bleibt toolchain-verwaltetes asm).
    // Deklaratives Routing ueber Linker-Scripts: kernel/linker.ld DISCARDed
    // .boot_s1, beispiele/bootloader/stage1.ld KEEPt ausschliesslich .boot_s1.
    let boot_dir = runtime_dir.join("boot");
    if matches!(arch, KernelArch::X86_64) && boot_dir.is_dir() {
        let mut entries: Vec<PathBuf> = std::fs::read_dir(&boot_dir)
            .map_err(|e| format!("boot/-Verzeichnis lesen: {e}"))?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .filter(|p| p.extension().and_then(|s| s.to_str()) == Some("S"))
            .collect();
        entries.sort();
        for p in entries {
            let obj = tmp.join(format!(
                "{}.o",
                p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
            ));
            let mut cmd = Command::new("cc");
            cmd.arg("-c").arg(&p).arg("-o").arg(&obj);
            if verbose {
                eprintln!("[moo-kernel] {cmd:?}");
            }
            let out = cmd.output().map_err(|e| {
                format!("cc (asm) nicht ausfuehrbar ({e}) — C-Compiler installieren")
            })?;
            if !out.status.success() {
                return Err(format!(
                    "cc fehlgeschlagen fuer {}:\n{}",
                    p.display(),
                    String::from_utf8_lossy(&out.stderr)
                ));
            }
            objects.push(obj);
        }
    }

    // (3) Linker-Script: explizit (--linker_script) oder Template
    // beispiele/kernel/linker.ld aus dem Quellbaum.
    let script = match linker_script {
        Some(s) => s.to_path_buf(),
        // P012-C1: Board-Profil darf ein eigenes Linker-Script vorgeben.
        None => match board.and_then(|b| b.linker_script) {
            Some(rel) => src_root.join("..").join(rel),
            None => src_root.join("..").join("beispiele").join("kernel").join("linker.ld"),
        },
    };
    if !script.exists() {
        return Err(format!(
            "Linker-Script nicht gefunden: {} (explizit via --linker_script angeben)",
            script.display()
        ));
    }

    // P011-C1: Endartefakt vs Zwischen-ELF. Bei flat/sector ist das ELF nur
    // Zwischenprodukt (tmp), -o bezeichnet das finale Binary/Image.
    let final_path = output.map(|p| p.to_path_buf()).unwrap_or_else(|| {
        file.with_extension(match emit {
            "flat" => "bin",
            "sector" => "img",
            _ => "elf",
        })
    });
    let elf_path = if emit == "elf" {
        final_path.clone()
    } else {
        tmp.join("kernel.elf")
    };
    let mut ld = Command::new(ld_prog);
    ld.arg("-n");
    if emit == "elf" {
        ld.arg("--gc-sections");
    }
    ld.arg("-T").arg(&script).arg("-o").arg(&elf_path);
    for o in &objects {
        ld.arg(o);
    }
    if !libraries.is_empty() {
        // Bare akzeptiert nur Archive; Suchpfade und Namen bleiben getrennte argv.
        ld.arg("-Bstatic");
        for path in lib_paths { ld.arg("-L").arg(path); }
        for name in libraries { ld.arg(format!("-l{name}")); }
    }
    if verbose {
        eprintln!("[moo-kernel] {ld:?}");
    }
    let out = ld.output().map_err(|e| {
        format!("{ld_prog} nicht ausfuehrbar ({e}) — binutils/lld installieren")
    })?;
    if !out.status.success() {
        return Err(format!("{ld_prog} ({resolved}) fehlgeschlagen:\n{}", String::from_utf8_lossy(&out.stderr)));
    }
    let ld_warn = String::from_utf8_lossy(&out.stderr);
    if !ld_warn.trim().is_empty() {
        // Linker-Warnungen sichtbar machen statt verschlucken
        eprintln!("{ld_warn}");
    }

    // P011-C1: Ausgabeformat.
    if emit == "elf" {
        println!("✓ Kernel gebaut ({}): {}", resolved, elf_path.display());
        match arch {
            KernelArch::X86_64 => println!("  Boot-Test: scripts/kernel-smoke.sh (GRUB-ISO + QEMU). Hinweis: qemu -kernel kann nur Multiboot1 — Multiboot2 braucht GRUB (Testbruecke, kein Lock-in — eigener moo-Bootloader: Backlog P011)."),
            KernelArch::Aarch64 => println!("  Boot-Test: scripts/kernel-smoke-arm64.sh (qemu-system-aarch64 -M virt -kernel — KEIN GRUB/Multiboot noetig)."),
        }
        return Ok(());
    }

    // flat|sector: objcopy-Wahl fest (P011-C1): binutils objcopy bevorzugt
    // (Toolset hier ist binutils: ld/nm/readelf), llvm-objcopy als Fallback.
    let objcopy = if Command::new("objcopy").arg("--version").output().map(|o| o.status.success()).unwrap_or(false) {
        "objcopy"
    } else if Command::new("llvm-objcopy").arg("--version").output().map(|o| o.status.success()).unwrap_or(false) {
        "llvm-objcopy"
    } else {
        return Err("weder objcopy noch llvm-objcopy gefunden — binutils (oder llvm) installieren; --emit flat|sector braucht objcopy".to_string());
    };
    let mut oc = Command::new(objcopy);
    oc.arg("-O").arg("binary").arg(&elf_path).arg(&final_path);
    if verbose {
        eprintln!("[moo-kernel] {oc:?}");
    }
    let out = oc.output().map_err(|e| format!("{objcopy} nicht ausfuehrbar: {e}"))?;
    if !out.status.success() {
        return Err(format!("{objcopy} fehlgeschlagen:\n{}", String::from_utf8_lossy(&out.stderr)));
    }

    if emit == "sector" {
        // HARTES Gate: Boot-Sektor = exakt 512 Bytes, Nutzcode <= 510,
        // Signatur 0x55AA an Offset 510/511.
        let mut data = std::fs::read(&final_path)
            .map_err(|e| format!("Sector-Gate: {} lesen: {e}", final_path.display()))?;
        if data.len() > 510 {
            return Err(format!(
                "Sector-Gate VERLETZT: Nutzcode ist {} Bytes, erlaubt sind max. 510 \
                 (Boot-Sektor = 512 Bytes inkl. Signatur 0x55AA an 510/511). Abbruch.",
                data.len()
            ));
        }
        let code_len = data.len();
        data.resize(512, 0);
        data[510] = 0x55;
        data[511] = 0xAA;
        std::fs::write(&final_path, &data)
            .map_err(|e| format!("Sector-Image schreiben: {e}"))?;
        println!(
            "✓ Sector-Image ({code_len}/510 Bytes Code, 512 Bytes total, Signatur 0x55AA): {}",
            final_path.display()
        );
        println!("  Boot-Test ohne GRUB: qemu-system-x86_64 -drive format=raw,file={}", final_path.display());
    } else {
        let n = std::fs::metadata(&final_path).map(|m| m.len()).unwrap_or(0);
        println!("✓ Flat-Binary ({n} Bytes, {resolved}): {}", final_path.display());
    }
    Ok(())
}

/// P2-T75: Ziel-Triple der MinGW-Umgebung (x86_64-pc-windows-gnu, x86_64-w64-mingw32).
fn is_windows_gnu_triple(triple: &str) -> bool {
    triple.starts_with("x86_64-") && (triple.ends_with("-windows-gnu") || triple.contains("mingw32"))
}

/// P2-T75: MinGW-Build der Moo-Runtime (skripte/p2/windows_minipc/runtime_mingw.sh).
/// Override > lib/x86_64-pc-windows-gnu/libmoo_runtime.a neben dem Compiler.
fn windows_gnu_runtime_lib() -> Result<PathBuf, String> {
    if let Some(pfad) = std::env::var_os("MOO_RUNTIME_LIB_WINDOWS_GNU") {
        let pfad = PathBuf::from(pfad);
        return if pfad.is_file() {
            Ok(pfad)
        } else {
            Err(format!("MOO_RUNTIME_LIB_WINDOWS_GNU zeigt auf keine Datei: {}", pfad.display()))
        };
    }
    let gebuendelt = std::env::current_exe().ok().and_then(|p| p.parent().map(|d|
        d.join("lib").join("x86_64-pc-windows-gnu").join("libmoo_runtime.a")));
    if let Some(pfad) = gebuendelt.filter(|p| p.is_file()) {
        return Ok(pfad);
    }
    // P2-T76: beim Compiler-Build erzeugtes Archiv (build.rs, OUT_DIR); sonst
    // nennt die Diagnose den Grund, warum build.rs es nicht gebaut hat.
    let gebaut = env!("MOO_WINDOWS_GNU_RUNTIME");
    if !gebaut.is_empty() && std::path::Path::new(gebaut).is_file() {
        return Ok(PathBuf::from(gebaut));
    }
    let grund = env!("MOO_WINDOWS_GNU_RUNTIME_GRUND");
    let grund = if grund.is_empty() { String::new() } else { format!(" (Compiler-Build: {grund})") };
    Err(format!(
        "Windows-Runtime (MinGW) nicht gefunden{grund}: MOO_RUNTIME_LIB_WINDOWS_GNU setzen, \
         lib/x86_64-pc-windows-gnu/libmoo_runtime.a neben moo-compiler ablegen oder den \
         Compiler mit x86_64-w64-mingw32-gcc im PATH neu bauen"
    ))
}

/// P2-T75: argv fuer den MinGW-Link, ohne Shell. Reihenfolge: Objekt, Suchpfade,
/// @bibliothek-Bibliotheken, Runtime, Windows-Systembibliotheken (wie build.rs fuer
/// target_windows). -static: keine libwinpthread-1.dll auf dem Ziel; eine
/// DLL-Importbibliothek muss daher libNAME.a heissen (der Import bleibt dynamisch).
/// P2-T79: `dll` = (Moduldefinitionsdatei, Importbibliothek) -> `-shared`; die .def
/// legt die Exporte fest (GNU ld exportiert dann nichts automatisch, auch keine Runtime).
fn windows_gnu_link_args(
    obj: &std::path::Path,
    runtime: &std::path::Path,
    exe: &std::path::Path,
    libraries: &[String],
    lib_paths: &[PathBuf],
    dll: Option<(&std::path::Path, &std::path::Path)>,
) -> Result<Vec<String>, String> {
    let text = |p: &std::path::Path| p.to_str().map(str::to_owned)
        .ok_or_else(|| format!("Pfad ist kein gueltiges UTF-8: {}", p.display()));
    let mut args = vec![text(obj)?];
    if let Some((def, implib)) = dll {
        let implib = text(implib)?;
        if implib.contains(',') {
            return Err(format!("Importbibliothek-Pfad darf kein Komma enthalten (-Wl-Liste): {implib}"));
        }
        args.push("-shared".to_string());
        args.push(text(def)?);
        args.push(format!("-Wl,--out-implib,{implib}"));
    }
    for pfad in lib_paths {
        args.push("-L".to_string());
        args.push(text(pfad)?);
    }
    for name in libraries {
        args.push(format!("-l{name}"));
    }
    args.push(text(runtime)?);
    for arg in ["-static", "-static-libgcc", "-lws2_32", "-lbcrypt", "-lcrypt32", "-lsecur32", "-o"] {
        args.push(arg.to_string());
    }
    args.push(text(exe)?);
    Ok(args)
}

/// P2-T79: C-Exporte einer Moo-DLL. Dieselbe Auswahl wie codegen.rs (@nativ +
/// @einstieg auf oberster Ebene, Export unter dem Moo-Namen), Reihenfolge der Quelle.
fn einstieg_exporte(stmts: &[ast::Stmt]) -> Vec<String> {
    stmts.iter().filter_map(|s| match s {
        ast::Stmt::FunctionDef { name, decorators, .. } if ist_einstieg(decorators) => Some(name.clone()),
        _ => None,
    }).collect()
}

fn ist_einstieg(decorators: &[ast::Decorator]) -> bool {
    decorators.iter().any(|d| d.name == "nativ" || d.name == "native")
        && decorators.iter().any(|d| d.name == "einstieg" || d.name == "entry")
}

/// P2-T81: erste Zeile jeder erzeugten Schnittstelle; nur solche Dateien werden ersetzt.
const SCHNITTSTELLE_MARKE: &str = "# moo-schnittstelle (erzeugt von moo compile -o *.dll, P2-T81)";

/// P2-T81: Typ als extern-"C"-Quelltext. Dieselbe Freigabe wie ffi_types.rs value_type/
/// pointee_supported (Skalare, ptr/zeiger auf void/Skalar/Pointer; void nur als Ziel).
fn schnittstellen_typ(ty: &ast::TypeRef, als_ziel: bool) -> Result<String, String> {
    const SKALARE: [&str; 10] = ["i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64"];
    match &ty.kind {
        ast::TypeKind::Name(n) if SKALARE.contains(&n.as_str()) || (als_ziel && n == "void") => Ok(n.clone()),
        ast::TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
            Ok(format!("{name}[{}]", schnittstellen_typ(&args[0], true)?)),
        _ => Err(format!("Typ in Zeile {} ist fuer extern \"C\" nicht freigegeben", ty.line)),
    }
}

/// P2-T81: Moo-Schnittstelle einer Moo-DLL: je @nativ @einstieg eine extern-"C"-
/// Deklaration mit @bibliothek("<NAME>") (Importbibliothek lib<NAME>.a, P2-T79).
fn dll_schnittstelle(bibliothek: &str, stmts: &[ast::Stmt]) -> Result<String, String> {
    let mut text = format!("{SCHNITTSTELLE_MARKE}\n# Nicht von Hand aendern: jeder DLL-Build schreibt sie neu.\n");
    for stmt in stmts {
        let ast::Stmt::FunctionDef { name, params, param_types, return_type, decorators, .. } = stmt else { continue };
        if !ist_einstieg(decorators) { continue; }
        if let Some(d) = decorators.iter().find(|d|
            !matches!(d.name.as_str(), "nativ" | "native" | "einstieg" | "entry" | "benutzt" | "used")) {
            return Err(format!("'{name}': @{} wird in der Schnittstelle nicht abgebildet", d.name));
        }
        let mut signatur = Vec::new();
        for (param, ty) in params.iter().zip(param_types) {
            let ty = ty.as_ref().ok_or_else(|| format!("'{name}': Parameter '{param}' ohne Typ"))?;
            signatur.push(format!("{param}: {}", schnittstellen_typ(ty, false).map_err(|e| format!("'{name}': {e}"))?));
        }
        let ergebnis = match return_type {
            Some(ty) => schnittstellen_typ(ty, true).map_err(|e| format!("'{name}': {e}"))?,
            None => "void".to_string(),
        };
        text.push_str(&format!("@bibliothek(\"{bibliothek}\")\nextern \"C\" funktion {name}({}) -> {ergebnis}\n",
            signatur.join(", ")));
    }
    Ok(text)
}

/// P2-T81: schreibt <NAME>_schnittstelle.moos neben die DLL (Import per --lib-pfad).
/// Fremde Dateien ohne Marke werden nie ueberschrieben; ist eine Signatur nicht
/// abbildbar, entfaellt eine alte erzeugte Schnittstelle (kein veralteter Stand).
fn schnittstelle_aktualisieren(dll: &std::path::Path, stmts: &[ast::Stmt]) -> Result<(), String> {
    let stem = dll.file_stem().and_then(|s| s.to_str())
        .ok_or_else(|| format!("DLL-Name ist kein gueltiges UTF-8: {}", dll.display()))?;
    let pfad = dll.with_file_name(format!("{stem}_schnittstelle.moos"));
    if pfad.exists() && !std::fs::read_to_string(&pfad).is_ok_and(|t| t.starts_with(SCHNITTSTELLE_MARKE)) {
        eprintln!("Warnung: {} ist keine erzeugte Moo-Schnittstelle und bleibt unveraendert", pfad.display());
        return Ok(());
    }
    match dll_schnittstelle(stem, stmts) {
        Ok(text) => {
            std::fs::write(&pfad, text)
                .map_err(|e| format!("Moo-Schnittstelle {} nicht schreibbar: {e}", pfad.display()))?;
            println!("  Moo-Schnittstelle: {} (importiere \"{stem}_schnittstelle\" mit --lib-pfad)", pfad.display());
        }
        Err(grund) => {
            let _ = std::fs::remove_file(&pfad);
            eprintln!("Warnung: keine Moo-Schnittstelle fuer {} ({grund}); extern \"C\" selbst deklarieren", dll.display());
        }
    }
    Ok(())
}

/// P2-T79: Moduldefinitionsdatei fuer GNU ld: genau diese Exporte.
fn windows_def_inhalt(exporte: &[String]) -> String {
    let mut text = String::from("EXPORTS\n");
    for name in exporte {
        text.push_str("    ");
        text.push_str(name);
        text.push('\n');
    }
    text
}

/// P2-T79: Windows-DLL per MinGW. Exportiert nur die @einstieg-Funktionen; die
/// Importbibliothek heisst lib<NAME>.a neben der DLL (unter -static von ld als
/// -l<NAME> gefunden, vgl. P2-T43). Liefert den Pfad der Importbibliothek.
fn link_windows_gnu_dll(
    obj: &std::path::Path,
    dll: &std::path::Path,
    exporte: &[String],
    libraries: &[String],
    lib_paths: &[PathBuf],
) -> Result<PathBuf, String> {
    if exporte.is_empty() {
        return Err("Ausgabe .dll braucht mindestens eine exportierte Funktion (@nativ @einstieg)".to_string());
    }
    let stem = dll.file_stem().and_then(|s| s.to_str())
        .ok_or_else(|| format!("DLL-Name ist kein gueltiges UTF-8: {}", dll.display()))?;
    let implib = dll.with_file_name(format!("lib{stem}.a"));
    let def = dll.with_extension("def");
    std::fs::write(&def, windows_def_inhalt(exporte))
        .map_err(|e| format!("Moduldefinitionsdatei {} nicht schreibbar: {e}", def.display()))?;
    let ergebnis = link_windows_gnu(obj, dll, libraries, lib_paths, Some((def.as_path(), implib.as_path())));
    let _ = std::fs::remove_file(&def);
    ergebnis.map(|()| implib)
}

/// P2-T75: Windows-EXE (bzw. mit `dll` DLL, P2-T79) per MinGW linken
/// (Linker: MOO_MINGW_CC oder x86_64-w64-mingw32-gcc).
fn link_windows_gnu(
    obj: &std::path::Path,
    exe: &std::path::Path,
    libraries: &[String],
    lib_paths: &[PathBuf],
    dll: Option<(&std::path::Path, &std::path::Path)>,
) -> Result<(), String> {
    let runtime = windows_gnu_runtime_lib()?;
    let linker = std::env::var("MOO_MINGW_CC").unwrap_or_else(|_| "x86_64-w64-mingw32-gcc".to_string());
    let args = windows_gnu_link_args(obj, &runtime, exe, libraries, lib_paths, dll)?;
    let status = std::process::Command::new(&linker).args(&args).status().map_err(|e|
        format!("Windows-Link: '{linker}' nicht startbar ({e}); MinGW installieren oder MOO_MINGW_CC setzen"))?;
    if !status.success() {
        return Err(format!("Windows-Link mit '{linker}' fehlgeschlagen ({status})"));
    }
    Ok(())
}

#[cfg(test)]
mod windows_gnu_cross_tests {
    use super::*;

    #[test]
    fn gnu_triple_und_link_reihenfolge() {
        assert!(is_windows_gnu_triple("x86_64-pc-windows-gnu"));
        assert!(is_windows_gnu_triple("x86_64-w64-mingw32"));
        assert!(!is_windows_gnu_triple("x86_64-pc-windows-msvc"));
        assert!(!is_windows_gnu_triple("x86_64-unknown-linux-gnu"));
        assert!(!is_windows_gnu_triple("aarch64-pc-windows-gnu"));
        let args = windows_gnu_link_args(
            std::path::Path::new("a.obj"), std::path::Path::new("rt.a"), std::path::Path::new("a.exe"),
            &["p2_win_dll".to_string()], &[PathBuf::from("libs")], None,
        ).unwrap();
        assert_eq!(args, ["a.obj", "-L", "libs", "-lp2_win_dll", "rt.a", "-static", "-static-libgcc",
            "-lws2_32", "-lbcrypt", "-lcrypt32", "-lsecur32", "-o", "a.exe"]);
    }

    #[test]
    fn gnu_dll_link_args_und_def() {
        let p = std::path::Path::new;
        let args = windows_gnu_link_args(p("a.obj"), p("rt.a"), p("a.dll"), &[], &[],
            Some((p("a.def"), p("liba.a")))).unwrap();
        assert_eq!(args, ["a.obj", "-shared", "a.def", "-Wl,--out-implib,liba.a", "rt.a", "-static",
            "-static-libgcc", "-lws2_32", "-lbcrypt", "-lcrypt32", "-lsecur32", "-o", "a.dll"]);
        assert!(windows_gnu_link_args(p("a.obj"), p("rt.a"), p("a.dll"), &[], &[],
            Some((p("a.def"), p("x,y/liba.a")))).is_err());
        assert_eq!(windows_def_inhalt(&["f".to_string(), "g".to_string()]), "EXPORTS\n    f\n    g\n");
        assert!(link_windows_gnu_dll(p("a.obj"), p("a.dll"), &[], &[], &[]).is_err());
    }

    fn programm(quelle: &str) -> ast::Program {
        let tokens = lexer::Lexer::new().tokenize(quelle).expect("lexer");
        parser::Parser::new(tokens).parse().expect("parser")
    }

    #[test]
    fn dll_schnittstelle_ist_wieder_parsbar() {
        let quelle = "@nativ\n@einstieg\nfunktion f(a: i32, b: ptr[u8]) -> u32:\n    gib_zurück 1\n\n\
            @nativ\n@einstieg\nfunktion g():\n    gib_zurück\n\n\
            @nativ\nfunktion intern(a: i32) -> i32:\n    gib_zurück a\n";
        let text = dll_schnittstelle("bib", &programm(quelle).statements).unwrap();
        assert!(text.starts_with(SCHNITTSTELLE_MARKE));
        let externs: Vec<(String, usize, Vec<String>)> = programm(&text).statements.iter().filter_map(|s| match s {
            ast::Stmt::ExternC { name, params, decorators, .. } => Some((name.clone(), params.len(),
                decorators.iter().map(|d| d.name.clone()).collect())),
            _ => None,
        }).collect();
        assert_eq!(externs, [("f".to_string(), 2, vec!["bibliothek".to_string()]),
            ("g".to_string(), 0, vec!["bibliothek".to_string()])]);
        assert!(text.contains("extern \"C\" funktion f(a: i32, b: ptr[u8]) -> u32\n"));
        assert!(text.contains("extern \"C\" funktion g() -> void\n"));
        let bool_quelle = "@nativ\n@einstieg\nfunktion h(a: bool) -> i32:\n    gib_zurück 1\n";
        assert!(dll_schnittstelle("bib", &programm(bool_quelle).statements).is_err());
    }
}

fn resolve_windows_linker_candidates(
    env_clang: Option<(PathBuf, bool)>,
    env_lld: Option<(PathBuf, bool)>,
    sibling_clang: Option<(PathBuf, bool)>,
    sibling_lld: Option<(PathBuf, bool)>,
    path_clang: Option<(PathBuf, bool)>,
    path_lld: Option<(PathBuf, bool)>,
) -> Result<(PathBuf, PathBuf), String> {
    for (tier, clang, lld) in [
        ("MOO_CLANG/MOO_LLD", env_clang, env_lld),
        ("gebündelte Toolchain", sibling_clang, sibling_lld),
        ("PATH", path_clang, path_lld),
    ] {
        match (clang, lld) {
            (None, None) => continue,
            (Some(_), None) | (None, Some(_)) => {
                return Err(format!(
                    "{tier}: clang.exe und lld-link.exe müssen als vollständiges Paar vorhanden sein"
                ));
            }
            (Some((clang, clang_executable)), Some((lld, lld_executable))) => {
                if !clang_executable || !lld_executable {
                    return Err(format!(
                        "{tier}: clang.exe und lld-link.exe müssen ausführbare Dateien sein"
                    ));
                }
                let clang_identity_ok = clang
                    .file_name()
                    .and_then(|name| name.to_str())
                    .map(|name| name.eq_ignore_ascii_case("clang.exe"))
                    .unwrap_or(false);
                let lld_identity_ok = lld
                    .file_name()
                    .and_then(|name| name.to_str())
                    .map(|name| name.eq_ignore_ascii_case("lld-link.exe"))
                    .unwrap_or(false);
                if !clang_identity_ok || !lld_identity_ok {
                    return Err(format!(
                        "{tier}: erwartete Tool-Identitäten clang.exe und lld-link.exe"
                    ));
                }
                return Ok((clang, lld));
            }
        }
    }

    Err(
        "Windows-Linker nicht gefunden: MOO_CLANG/MOO_LLD, gebündelte Toolchain und PATH enthalten kein vollständiges Paar"
            .to_string(),
    )
}

fn append_windows_linker_args(
    args: &mut Vec<String>,
    lld_path: &Path,
) -> Result<(), String> {
    if !lld_path.is_absolute() {
        return Err("lld-link-Pfad muss absolut sein".to_string());
    }
    let lld_dir = lld_path
        .parent()
        .ok_or_else(|| "lld-link-Pfad hat kein übergeordnetes Verzeichnis".to_string())?
        .to_str()
        .ok_or_else(|| "lld-link-Verzeichnis ist kein gültiger Unicode-Pfad".to_string())?;
    const WINDOWS_LIBS: [&str; 4] = [
        "-lmsvcrt",
        "-lvcruntime",
        "-lucrt",
        "-lkernel32",
    ];

    let mut normalized = Vec::with_capacity(args.len() + 6);
    let mut skip_option_value = false;
    for arg in args.drain(..) {
        if skip_option_value {
            skip_option_value = false;
            continue;
        }
        if arg == "--ld-path" || arg == "-B" {
            skip_option_value = true;
            continue;
        }
        if arg.starts_with("--ld-path=")
            || arg.starts_with("-fuse-ld=")
            || (arg.starts_with("-B") && arg.len() > 2)
            || WINDOWS_LIBS.contains(&arg.as_str())
        {
            continue;
        }
        normalized.push(arg);
    }
    normalized.push("-fuse-ld=lld".to_string());
    normalized.push(format!("-B{lld_dir}"));
    normalized.extend(WINDOWS_LIBS.into_iter().map(str::to_string));
    *args = normalized;
    Ok(())
}

fn windows_compile_allows_run(
    started: bool,
    timed_out: bool,
    return_code: Option<i32>,
    output_exists: bool,
) -> bool {
    started && !timed_out && return_code == Some(0) && output_exists
}

/// MoOS-Benutzerprogramme sind statische ELF64-Dateien mit eigenem SysV-Start.
fn build_moos_benutzer(
    file: &std::path::Path,
    compiler: &codegen::CodeGen,
    output: Option<&std::path::Path>,
    linker_script: Option<&std::path::Path>,
    entry_point: Option<&str>,
    libraries: &[String],
    lib_paths: &[PathBuf],
) -> Result<(), String> {
    let elf = output.map(|pfad| pfad.to_path_buf()).unwrap_or_else(|| file.with_extension("elf"));
    let object = elf.with_extension(format!("moo-{}.o", std::process::id()));
    let temporary = elf.with_extension(format!("moo-{}.tmp", std::process::id()));
    let ergebnis = (|| {
        compiler.write_object(&object, codegen::MOOS_USER_TARGET)?;
        let mut ld = Command::new("ld");
        // AR_ERSTE_VA in kern_adressraum.moos. Text getrennt von R/O und R/W.
        ld.args([
            "-m", "elf_x86_64", "-static", "--gc-sections", "--fatal-warnings",
            "-z", "separate-code", "-z", "noexecstack", "-z", "max-page-size=0x1000",
            "-Ttext-segment=0x8000000000",
            "-e", entry_point.unwrap_or("_start"),
        ]);
        if let Some(skript) = linker_script {
            ld.arg("-T").arg(skript);
        }
        ld.arg("-o").arg(&elf).arg(&object);
        if compiler.hat_sektion(crate::runtime_provider::SECTION) {
            if !compiler.hat_sektion(".data.moo_benutzer_laufzeit") {
                return Err("MoOS-Threadlaufzeit benötigt den Benutzer-Träger moos_laufzeit.moos".into());
            }
            let runtime = kernel_source_root().ok_or("MoOS-Wertlaufzeit: Compiler-Quellen fehlen")?.join("runtime");
            std::fs::create_dir_all(&temporary).map_err(|e| format!("MoOS-Wertlaufzeit: {e}"))?;
            // Dieselben freestanding Wertkerne wie im Kernel; Thread-, Kanal-,
            // Fehler- und Besitzlogik kommt aus der gemeinsamen Moo-Laufzeit.
            let user_sources = ["moo_bare.c", "moo_bare_alloc.c", "moo_freestanding_mem.c",
                "moo_native_math.c", "moo_surface.c", "moo_surface_core.c"];
            for src in user_sources.iter().chain(NATIVE_VALUE_SOURCES) {
                let obj = temporary.join(format!("{}.o", src.replace('/', "_")));
                let mut cc = Command::new("cc");
                cc.args(["-c", "-std=c11", "-O2", "-ffreestanding", "-fPIC",
                    "-fno-stack-protector", "-mno-red-zone", "-ffunction-sections", "-fdata-sections",
                    "-DMOO_BARE", "-DMOO_BARE_THREADS", "-DMOO_UEFI_COLLECTIONS",
                    "-DMOO_HAS_SURFACE", "-D_GNU_SOURCE"]);
                if *src == "boot/uefi_rt.c" { cc.arg("-DMOO_UEFI_USE_BARE_RUNTIME"); }
                let built = cc.arg("-I").arg(&runtime).arg(runtime.join(src)).arg("-o").arg(&obj)
                    .output().map_err(|e| format!("MoOS-Wertlaufzeit {src}: {e}"))?;
                if !built.status.success() {
                    return Err(format!("MoOS-Wertlaufzeit {src}: {}", String::from_utf8_lossy(&built.stderr)));
                }
                ld.arg(obj);
            }
        }
        for pfad in lib_paths {
            ld.arg("-L").arg(pfad);
        }
        for name in libraries {
            ld.arg(format!("-l{name}"));
        }
        let ausgabe = ld.output().map_err(|e| format!("MoOS-Benutzer-ELF: GNU ld benötigt ({e})"))?;
        if !ausgabe.status.success() {
            return Err(format!("MoOS-Benutzer-ELF: {}", String::from_utf8_lossy(&ausgabe.stderr)));
        }
        Ok(())
    })();
    let _ = std::fs::remove_file(&object);
    let _ = std::fs::remove_dir_all(&temporary);
    ergebnis?;
    println!("✓ MoOS-Benutzer-ELF gelinkt: {}", elf.display());
    Ok(())
}

fn compile(file: &PathBuf, output: Option<&std::path::Path>, emit_ir: bool, target: Option<&str>, no_stdlib: bool, linker_script: Option<&std::path::Path>, entry_point: Option<&str>, profile: bool, erlaube_kern: bool, kernel: bool, emit: &str, board: Option<&str>, lib_paths: &[PathBuf]) -> Result<(), String> {
    let moos_benutzer = target == Some(codegen::MOOS_USER_TARGET);
    if moos_benutzer && (kernel || emit != "elf") {
        return Err("x86_64-moos erzeugt ein Benutzer-ELF, --kernel und --emit flat|sector sind Kernel-Optionen".into());
    }
    // P011-C1: --emit frueh validieren (vor jeder Kompilierung).
    if !matches!(emit, "elf" | "flat" | "sector") {
        return Err(format!("--emit '{emit}' unbekannt — erlaubt: elf | flat | sector"));
    }

    // P012-C1: Board-Profil aufloesen (nur Kernel-Pipeline). Das Board
    // setzt das Target; ein explizit ABWEICHENDES --target ist ein Fehler.
    // Unbekanntes Board: Fehlermeldung LISTET alle bekannten Boards.
    let board_profile: Option<&'static boards::BoardProfile> = match board {
        Some(name) => {
            if !kernel {
                return Err("--board ist nur mit --kernel gueltig (Board-Profile steuern die Bare-Kernel-Pipeline).".to_string());
            }
            match boards::find_board(name) {
                Some(b) => Some(b),
                None => return Err(format!(
                    "Unbekanntes Board '{name}'. Bekannte Boards: {}",
                    boards::board_names().join(", ")
                )),
            }
        }
        None => None,
    };
    let target: Option<&str> = match (board_profile, target) {
        (Some(b), Some(t)) if t != b.target => return Err(format!(
            "--target '{t}' widerspricht --board '{}' (Board erwartet '{}'). --target weglassen oder passend setzen.",
            b.name, b.target
        )),
        (Some(b), _) => Some(b.target),
        (None, t) => t,
    };
    // P10-T14: --kernel mit Standardbibliothek wählt den nativen Thread-Träger.
    // --no-stdlib behält den bisherigen Minimalpfad.
    let kernel_runtime = kernel && !no_stdlib;
    if kernel_runtime && codegen::CodeGen::resolve_triple(target.unwrap_or("native")) != "x86_64-unknown-none" {
        return Err("Kernel-Standardruntime benötigt --target x86_64-bare".to_string());
    }
    // Haupt-Datei parsen
    let mut program = parse_file(file)?;
    // P2-T29: Typpruefung je Datei (Module in resolve_modules), Abbruch vor dem Codegen.
    typpruefung_einschalten_falls_gewuenscht();
    typpruefung::pruefe_datei(&program, &file.display().to_string());
    // P2-T96: Top-Level der Hauptdatei vor dem Inlining (Fehlerpfade mit richtiger Datei).
    let haupt_statements = program.statements.clone();

    // Module AST-basiert auflösen
    let mut visited = std::collections::HashSet::new();
    let canonical = file.canonicalize().unwrap_or(file.clone());
    visited.insert(canonical);
    resolve_modules(&mut program, file.as_path(), &mut visited, lib_paths)?;
    if kernel_runtime {
        let root = kernel_source_root().ok_or("Kernel-Standardruntime: Compiler-Quellen fehlen")?
            .join("..").join("beispiele").join("kernel").join("kern_laufzeit.moos");
        let mut carrier = parse_file(&root)?;
        resolve_modules(&mut carrier, &root, &mut visited, lib_paths)?;
        program.funktion_quellen.extend(carrier.funktion_quellen);
        carrier.statements.append(&mut program.statements);
        program.statements = carrier.statements;
    }
    fehlerpfade::pruefe(&haupt_statements, &program, &file.display().to_string());
    typpruefung::ergebnis()?;
    fehlerpfade::ergebnis()?;
    // P2-T114 Phase C: Globale gehoeren dem Hauptthread (Effektpruefung).
    let global_pruefplan = typpruefung::globale_pruefen(&program, &file.display().to_string())?;
    let link_triple = codegen::CodeGen::resolve_triple(target.unwrap_or("native"));
    let libraries = ffi_link::libraries(&program.statements, &link_triple)?;

    // LLVM Codegen
    let context = inkwell::context::Context::create();
    let module_name = file.file_stem().unwrap().to_str().unwrap();
    let mut compiler = codegen::CodeGen::new(&context, module_name, target.unwrap_or("native"))?;
    compiler.set_quelldatei(&file.display().to_string());
    if profile {
        compiler.set_profiling(true);
    }
    // UEFI-Apps mit Standard-Runtime verwenden den Bare-Systemzugriff und
    // behalten das Ownership der dynamischen Werte. --no-stdlib wählt weiter
    // das Minimalprofil für Loader und Kernel.
    let uefi_runtime = link_triple == "x86_64-unknown-uefi" && !no_stdlib && !kernel;
    let is_bare_target = target.map(|t| t.contains("-bare")).unwrap_or(false);
    compiler.set_bare_mode(no_stdlib || is_bare_target || erlaube_kern || uefi_runtime || moos_benutzer);
    compiler.set_bare_runtime_refcounts(uefi_runtime || kernel_runtime);
    compiler.set_global_pruefplan(global_pruefplan);
    compiler.compile_program(&program)?;
    // P2-T43: MSVC-COFF traegt @bibliothek selbst (.drectve /DEFAULTLIB).
    compiler.embed_link_libraries(&libraries, &link_triple)?;

    if emit_ir {
        let ir_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("ll"));
        compiler.write_ir(&ir_path)?;
        println!("✓ LLVM IR geschrieben: {}", ir_path.display());
        return Ok(());
    }

    if moos_benutzer {
        return build_moos_benutzer(file, &compiler, output, linker_script, entry_point, &libraries, lib_paths);
    }

    let target_str = target.unwrap_or("native");

    // P011-C1: flat/sector existieren nur in der Kernel-/Loader-Pipeline
    // (objcopy-Pfad haengt an build_kernel).
    if emit != "elf" && !(no_stdlib && target_str.contains("-bare") && (kernel || linker_script.is_some())) {
        return Err("--emit flat|sector wirkt nur in der Kernel-/Loader-Pipeline: --no-stdlib + *-bare-Target + --kernel (oder --linker-script)".to_string());
    }

    // Bare-Metal: One-Shot-Kernel-Pipeline oder nur Object-File
    if no_stdlib || kernel_runtime {
        // Plan-010 C2: --kernel (oder explizites --linker_script) bei einem
        // *-bare-Target baut moo->.o + moo_bare*.c -> ld -T script -> kernel.elf.
        let is_bare_target = target_str.contains("-bare");
        if is_bare_target && (kernel || linker_script.is_some()) {
            let runtime_script = if kernel_runtime && linker_script.is_none() {
                Some(kernel_source_root().ok_or("Kernel-Standardruntime: Quellen fehlen")?
                    .join("..").join("beispiele").join("kernel").join("linker-uefi.ld"))
            } else { None };
            return build_kernel(file, &compiler, output, target_str, linker_script.or(runtime_script.as_deref()), emit, board_profile, &libraries, lib_paths);
        }
        let obj_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("o"));
        compiler.write_object(&obj_path, target_str)?;
        let resolved = codegen::CodeGen::resolve_triple(target_str);
        println!("✓ Bare-Metal kompiliert ({}): {}", resolved, obj_path.display());
        println!("  Ohne Runtime. Linken mit: cc {0} moo_bare.o -nostdlib -o firmware", obj_path.display());
        if !libraries.is_empty() || !lib_paths.is_empty() {
            eprintln!("  Link-Metadaten (nur statische Archive): Bibliotheken {:?}, Suchpfade {:?}", libraries, lib_paths);
        }
        return Ok(());
    }

    // Cross-Compilation: WASM als Sonderfall (kein Linking)
    let resolved = codegen::CodeGen::resolve_triple(target_str);
    if resolved.starts_with("wasm") {
        let wasm_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("wasm"));
        compiler.write_object(&wasm_path, target_str)?;
        println!("✓ WASM geschrieben: {}", wasm_path.display());
        println!("  Hinweis: .wasm Objektdatei ohne Runtime. Fuer volle Programme braucht es wasi-sdk/emscripten.");
        return Ok(());
    }

    // Cross-Compilation: Object-File fuer Ziel-Architektur (kein Linking)
    if target_str != "native" && !target_str.is_empty() {
        // P2-T75/T79: Windows-GNU mit Ausgabe *.exe bzw. *.dll -> fertig gelinkt
        // (MinGW auf dem Build-Host). Sonst bleibt es beim Objekt (Bestand).
        let endung = output.and_then(|p| p.extension()).and_then(|e| e.to_str()).map(str::to_ascii_lowercase);
        let wants_dll = endung.as_deref() == Some("dll");
        let wants_exe = endung.as_deref() == Some("exe");
        if let (true, Some(ziel)) = (wants_exe || wants_dll, output) {
            if !is_windows_gnu_triple(&resolved) {
                return Err(format!(
                    "Ausgabe .exe/.dll wird aus einem Cross-Build nur fuer x86_64-pc-windows-gnu (MinGW) gelinkt, nicht fuer '{resolved}'. Ohne .exe/.dll entsteht ein Objekt."
                ));
            }
            let exporte = if wants_dll { einstieg_exporte(&program.statements) } else { Vec::new() };
            if wants_dll && exporte.is_empty() {
                return Err("Ausgabe .dll braucht mindestens eine exportierte Funktion (@nativ @einstieg)".to_string());
            }
            let obj_path = ziel.with_extension("obj");
            compiler.write_object(&obj_path, target_str)?;
            if wants_dll {
                let implib = link_windows_gnu_dll(&obj_path, ziel, &exporte, &libraries, lib_paths)?;
                let _ = std::fs::remove_file(&obj_path);
                println!("✓ Windows-DLL gelinkt ({}): {} (Importbibliothek {}, Exporte: {})",
                    resolved, ziel.display(), implib.display(), exporte.join(", "));
                schnittstelle_aktualisieren(ziel, &program.statements)?;
            } else {
                link_windows_gnu(&obj_path, ziel, &libraries, lib_paths, None)?;
                let _ = std::fs::remove_file(&obj_path);
                println!("✓ Windows-EXE gelinkt ({}): {}", resolved, ziel.display());
            }
            return Ok(());
        }
        let obj_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("o"));
        compiler.write_object(&obj_path, target_str)?;
        println!("✓ Cross-kompiliert fuer {}: {}", resolved, obj_path.display());
        println!("  Hinweis: Object-File fuer {}. Linken muss auf dem Zielsystem erfolgen.", resolved);
        if !libraries.is_empty() || !lib_paths.is_empty() {
            eprintln!("  Link-Metadaten: Bibliotheken {:?}, Suchpfade {:?}", libraries, lib_paths);
        }
        return Ok(());
    }

    // Native: Object-File erzeugen + Linken
    let obj_path = file.with_extension("o");
    compiler.write_object(&obj_path, "native")?;

    // Mit cc/gcc linken
    let output_path = output
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| {
            let stem = file.file_stem().unwrap().to_str().unwrap();
            PathBuf::from(stem)
        });

    // Runtime-Archiv: installierte/bundled Compiler muessen relocatable sein.
    // Override > lib/ neben der EXE > build.rs-OUT_DIR fuer Dev-Builds.
    #[cfg(target_os = "windows")]
    let runtime_name = "moo_runtime.lib";
    #[cfg(not(target_os = "windows"))]
    let runtime_name = "libmoo_runtime.a";
    let embedded_runtime = PathBuf::from(env!("OUT_DIR")).join(runtime_name);
    let bundled_runtime = std::env::current_exe().ok()
        .and_then(|p| p.parent().map(|d| d.join("lib").join(runtime_name)));
    let runtime_lib = std::env::var_os("MOO_RUNTIME_LIB")
        .map(PathBuf::from)
        .or_else(|| bundled_runtime.filter(|p| p.exists()))
        .unwrap_or(embedded_runtime);
    if !runtime_lib.exists() {
        return Err(format!(
            "Moo-Runtime nicht gefunden: {} (MOO_RUNTIME_LIB setzen oder lib/{} neben moo-compiler ablegen)",
            runtime_lib.display(), runtime_name
        ));
    }

    let mut link_args = vec![
        obj_path.to_str().unwrap().to_string(),
        runtime_lib.to_str().unwrap().to_string(),
        "-o".to_string(),
        output_path.to_str().unwrap().to_string(),
    ];

    // Bibliotheken vor System-Libs: statische Bibliotheken koennen diese nutzen.
    // Command::args uebergibt alle Werte direkt, ohne Shell-Interpolation.
    for path in lib_paths {
        let path = path.to_str().ok_or("Bibliothekssuchpfad ist kein gueltiges UTF-8")?;
        link_args.push("-L".to_string());
        link_args.push(path.to_string());
    }
    for name in &libraries {
        link_args.push(format!("-l{name}"));
    }

    #[cfg(target_os = "windows")]
    link_args.extend([
        // cc-rs baut die Runtime mit MSVC /MD. Der finale clang-Link muss
        // dasselbe DLL-CRT-Modell verwenden, sonst entstehen __imp_*-Luecken
        // und LNK4098-Konflikte zwischen MSVCRT und statischer UCRT.
        "-fms-runtime-lib=dll".to_string(),
        // Clang 18s GNU-Treiber fuegt auf windows-msvc trotzdem libcmt als
        // Default ein. /MD-Objekte referenzieren bereits msvcrt; libcmt muss
        // daher explizit ausgeschlossen werden, um CRT-Mischung zu vermeiden.
        "-Wl,/NODEFAULTLIB:libcmt".to_string(),
        "-lsqlite3".to_string(),
        "-lws2_32".to_string(), "-lbcrypt".to_string(),
        "-lcrypt32".to_string(), "-lsecur32".to_string(),
    ]);
    #[cfg(not(target_os = "windows"))]
    link_args.extend([
        "-lm".to_string(), "-lpthread".to_string(),
        "-lsqlite3".to_string(),
        "-lSDL2".to_string(), "-lSDL2_image".to_string(),
    ]);
    // TLS-Backend-Libs per Build-Flag MOO_TLS_BACKEND.
    // Windows nutzt SChannel oder vendored mbedTLS; Nicht-Windows OpenSSL oder mbedTLS.
    #[cfg(all(not(target_os = "windows"), not(moo_tls_mbedtls)))]
    link_args.extend(["-lssl".to_string(), "-lcrypto".to_string()]);
    // Vendored mbedTLS (compiler/runtime/mbedtls/) ist in moo_runtime einkompiliert
    // (build.rs unter MOO_TLS_BACKEND=mbedtls) — KEINE System-Libs mehr noetig.
    // Nativer AES-Backend (moo_aes_native.c) braucht libcrypto (EVP/HMAC/SHA/RAND).
    #[cfg(all(not(target_os = "windows"), moo_aes_native))]
    link_args.extend(["-lcrypto".to_string()]);

    // Linux-only UI-Libs (GTK3 + libappindicator3 + Co.) — nur wenn das
    // UI-Modul wirklich mitgebaut wurde. 3D-only Builds auf Linux duerfen
    // beim finalen Linken eines .moos-Programms keine GTK/AppIndicator-Libs
    // verlangen.
    #[cfg(all(target_os = "linux", feature = "moo_ui"))]
    {
        link_args.extend([
            "-lappindicator3".to_string(),
            "-ldbusmenu-glib".to_string(),
            "-lgtk-3".to_string(),
            "-lgdk-3".to_string(),
            "-lgio-2.0".to_string(),
            "-lgobject-2.0".to_string(),
            "-lglib-2.0".to_string(),
            "-lcairo".to_string(),
            "-lgdk_pixbuf-2.0".to_string(),
        ]);
    }

    // Natives Wayland-Backend (NATIVE-UI-1): beim finalen Link eines
    // kompilierten .moos-Programms wayland-client + xkbcommon mitgeben.
    #[cfg(all(target_os = "linux", feature = "wayland_ui"))]
    {
        link_args.extend([
            "-lwayland-client".to_string(),
            "-lxkbcommon".to_string(),
        ]);
    }

    // Natives X11/XCB-Backend (NATIVE-UI-2): xcb-Libs beim finalen Link
    // eines kompilierten .moos-Programms mitgeben.
    #[cfg(all(target_os = "linux", feature = "x11_ui"))]
    {
        link_args.extend([
            "-lxcb".to_string(),
            "-lxcb-shm".to_string(),
            "-lxcb-keysyms".to_string(),
        ]);
    }

    // macOS UI-Libs/Frameworks — der Compiler-Build linkt diese bereits via
    // build.rs fuer das moo-compiler Binary. Beim spaeteren finalen Link eines
    // kompilierten .moos-Programms muessen sie aber erneut an cc/clang uebergeben
    // werden, sonst fehlen AppKit/Foundation/CoreGraphics/Objective-C-Symbole
    // aus moo_ui_cocoa.o.
    #[cfg(target_os = "macos")]
    {
        link_args.extend([
            "-framework".to_string(),
            "AppKit".to_string(),
            "-framework".to_string(),
            "Foundation".to_string(),
            "-framework".to_string(),
            "CoreGraphics".to_string(),
            "-lobjc".to_string(),
        ]);
    }

    // Win32-UI-Systembibliotheken fuer die finale Link-Stufe eines kompilierten
    // Moo-Programms (analog zu build.rs::link_ui_windows).
    #[cfg(all(target_os = "windows", feature = "moo_ui"))]
    link_args.extend([
        "-luser32".to_string(), "-lgdi32".to_string(),
        "-lcomctl32".to_string(), "-lcomdlg32".to_string(),
        "-lshell32".to_string(), "-lole32".to_string(),
        "-luxtheme".to_string(),
    ]);

    // 3D-Backend-Libs — nur wenn ein 3D-Feature aktiv ist (analog build.rs).
    // moo_ui-only Build (ohne gl33/gl21/vulkan) linkt kein GL/Vulkan/GLFW.
    #[cfg(all(target_os = "windows", any(feature = "gl21", feature = "gl33", feature = "vulkan")))]
    link_args.extend(["-lSDL2".to_string(), "-lSDL2_image".to_string()]);
    #[cfg(any(feature = "gl21", feature = "gl33"))]
    {
        if cfg!(target_os = "windows") {
            link_args.push("-lopengl32".to_string());
        } else if cfg!(target_os = "macos") {
            link_args.extend(["-framework".to_string(), "OpenGL".to_string()]);
        } else {
            link_args.push("-lGL".to_string());
        }
        link_args.push(if cfg!(target_os = "windows") {
            "-lglfw3dll".to_string()
        } else {
            "-lglfw".to_string()
        });
    }
    #[cfg(feature = "vulkan")]
    {
        link_args.push(if cfg!(target_os = "windows") {
            "-lvulkan-1".to_string()
        } else {
            "-lvulkan".to_string()
        });
        #[cfg(not(any(feature = "gl21", feature = "gl33")))]
        link_args.push(if cfg!(target_os = "windows") {
            "-lglfw3dll".to_string()
        } else {
            "-lglfw".to_string()
        });
    }

    // Capture-Backend-Libs fuer die separate finale Link-Stufe.
    #[cfg(moo_has_v4l2)]
    {
        link_args.push("-lv4l2".to_string());
        link_args.push("-lv4lconvert".to_string());
    }
    #[cfg(moo_has_alsa)]
    link_args.push("-lasound".to_string());
    #[cfg(moo_has_windows_capture)]
    link_args.extend([
        "-lmfplat".to_string(), "-lmf".to_string(), "-lmfreadwrite".to_string(),
        "-lmfuuid".to_string(), "-lole32".to_string(), "-loleaut32".to_string(),
        "-luuid".to_string(),
    ]);
    #[cfg(moo_has_macos_capture)]
    {
        for framework in ["AVFoundation", "CoreMedia", "CoreVideo", "CoreAudio", "AudioToolbox", "Foundation"] {
            link_args.push("-framework".to_string());
            link_args.push(framework.to_string());
        }
        link_args.push("-lobjc".to_string());
        link_args.push(format!("-Wl,-sectcreate,__TEXT,__info_plist,{}/runtime/moo_capture_macos_info.plist", env!("CARGO_MANIFEST_DIR")));
    }

    // Linker-Script: -T kernel.ld
    if let Some(script) = linker_script {
        link_args.push("-T".to_string());
        link_args.push(script.to_str().unwrap().to_string());
        link_args.push("-nostdlib".to_string()); // Kein Standard-Startup bei eigenem Linker-Script
    }

    // Entry-Point: -e _start
    if let Some(entry) = entry_point {
        link_args.push("-e".to_string());
        link_args.push(entry.to_string());
    }

    #[cfg(target_os = "windows")]
    let (linker_program, windows_lld_program) = {
        let candidate = |path: PathBuf| {
            let path = if path.is_absolute() {
                path
            } else {
                match std::env::current_dir() {
                    Ok(dir) => dir.join(path),
                    Err(_) => path,
                }
            };
            let executable = path.is_absolute()
                && path.is_file()
                && path
                    .extension()
                    .and_then(|extension| extension.to_str())
                    .map(|extension| extension.eq_ignore_ascii_case("exe"))
                    .unwrap_or(false);
            (path, executable)
        };

        let env_clang = std::env::var_os("MOO_CLANG")
            .map(PathBuf::from)
            .map(|path| candidate(path));
        let env_lld = std::env::var_os("MOO_LLD")
            .map(PathBuf::from)
            .map(|path| candidate(path));

        let bundled_bin = std::env::current_exe().ok().and_then(|path| {
            path.parent()
                .map(|dir| dir.join("toolchain").join("bin"))
        });
        let sibling_clang = bundled_bin.as_ref().and_then(|dir| {
            let path = dir.join("clang.exe");
            path.exists().then(|| candidate(path))
        });
        let sibling_lld = bundled_bin.as_ref().and_then(|dir| {
            let path = dir.join("lld-link.exe");
            path.exists().then(|| candidate(path))
        });

        let first_path_candidate = |name: &str| {
            std::env::var_os("PATH").and_then(|path_value| {
                std::env::split_paths(&path_value)
                    .map(|dir| dir.join(name))
                    .find(|path| path.exists())
                    .map(|path| candidate(path))
            })
        };
        let path_clang = first_path_candidate("clang.exe");
        let path_lld = first_path_candidate("lld-link.exe");

        resolve_windows_linker_candidates(
            env_clang,
            env_lld,
            sibling_clang,
            sibling_lld,
            path_clang,
            path_lld,
        )?
    };
    #[cfg(not(target_os = "windows"))]
    let linker_program = PathBuf::from("cc");
    #[cfg(target_os = "windows")]
    {
        // Bindet clang an das validierte lld-link-Paar statt an Visual Studios link.exe.
        append_windows_linker_args(&mut link_args, &windows_lld_program)?;
        if let Ok(exe) = std::env::current_exe() {
            if let Some(dir) = exe.parent() {
                let bundled_lib = dir.join("lib");
                if bundled_lib.exists() {
                    link_args.push(format!("-L{}", bundled_lib.display()));
                }
            }
        }
    }
    let link_status = Command::new(&linker_program)
        .args(&link_args)
        .status()
        .map_err(|e| format!("Linker starten fehlgeschlagen: {e}"))?;

    // Object-File aufräumen
    let _ = std::fs::remove_file(&obj_path);

    #[cfg(target_os = "windows")]
    if !windows_compile_allows_run(
        true,
        false,
        link_status.code(),
        output_path.exists(),
    ) {
        return Err("Linken fehlgeschlagen oder Ausgabedatei fehlt".to_string());
    }
    #[cfg(not(target_os = "windows"))]
    if !link_status.success() {
        return Err("Linken fehlgeschlagen".to_string());
    }

    if std::env::var("MOO_QUIET").is_err() {
        println!("✓ Kompiliert: {} → {}", file.display(), output_path.display());
    }
    Ok(())
}

/// P2-T97: `pruefe` – wie compile bis einschliesslich Codegen und LLVM-Verifikation,
/// aber ohne Objektdatei/Linken. Schnelle Rueckmeldung fuer Agenten.
fn pruefe(file: &PathBuf, target: Option<&str>, lib_paths: &[PathBuf], no_stdlib: bool, erlaube_kern: bool) -> Result<(), String> {
    let mut program = parse_file(file)?;
    // P2-T29: Typpruefung je Datei (Module in resolve_modules), Abbruch vor dem Codegen.
    typpruefung_einschalten_falls_gewuenscht();
    typpruefung::pruefe_datei(&program, &file.display().to_string());
    let haupt_statements = program.statements.clone();
    let mut visited = std::collections::HashSet::new();
    visited.insert(file.canonicalize().unwrap_or(file.clone()));
    resolve_modules(&mut program, file.as_path(), &mut visited, lib_paths)?;
    fehlerpfade::pruefe(&haupt_statements, &program, &file.display().to_string());
    typpruefung::ergebnis()?;
    fehlerpfade::ergebnis()?;
    let global_pruefplan = typpruefung::globale_pruefen(&program, &file.display().to_string())?;
    let link_triple = codegen::CodeGen::resolve_triple(target.unwrap_or("native"));
    ffi_link::libraries(&program.statements, &link_triple)?;
    let context = inkwell::context::Context::create();
    let module_name = file.file_stem().and_then(|s| s.to_str()).unwrap_or("pruefe");
    let mut compiler = codegen::CodeGen::new(&context, module_name, target.unwrap_or("native"))?;
    compiler.set_quelldatei(&file.display().to_string());
    let is_bare_target = target.map(|t| t.contains("-bare")).unwrap_or(false);
    compiler.set_bare_mode(no_stdlib || is_bare_target || erlaube_kern || target == Some(codegen::MOOS_USER_TARGET));
    compiler.set_global_pruefplan(global_pruefplan);
    compiler.compile_program(&program)
}

/// P2-T97: `erklaere [CODE]` – Katalogeintrag bzw. ganzer Katalog.
fn erklaere(code: Option<&str>) -> Result<(), String> {
    match code {
        None => {
            for e in diagnose::KATALOG {
                println!("{:<7} {}", e.code, e.titel);
            }
            Ok(())
        }
        Some(code) => {
            let e = diagnose::eintrag(code).ok_or_else(|| format!(
                "Unbekannter Diagnosecode '{code}' (Liste: moo-compiler erklaere)"
            ))?;
            println!("{} – {}\n\n{}", e.code, e.titel, e.erklaerung);
            Ok(())
        }
    }
}

fn run(file: &PathBuf) -> Result<(), String> {
    let tmp_dir = std::env::temp_dir();
    #[cfg(target_os = "windows")]
    let tmp_name = format!("moo_tmp_binary_{}.exe", std::process::id());
    #[cfg(not(target_os = "windows"))]
    let tmp_name = format!("moo_tmp_binary_{}", std::process::id());
    let tmp_output = tmp_dir.join(tmp_name);

    compile(file, Some(&tmp_output), false, None, false, None, None, false, false, false, "elf", None, &[])?;
    // P2-T97: Hinweise vor der Ausfuehrung zeigen (run endet per exit).
    diagnosen_abschliessen();

    let status = Command::new(&tmp_output)
        .status()
        .map_err(|e| format!("Ausführung fehlgeschlagen: {e}"))?;

    let _ = std::fs::remove_file(&tmp_output);

    std::process::exit(status.code().unwrap_or(1));
}

fn repl() {
    println!("moo REPL v0.1.0 (nativ) — Tippe 'quit' zum Beenden");
    println!("Zweisprachig: Deutsch & Englisch\n");

    let mut history: Vec<String> = Vec::new();
    let mut buffer: Vec<String> = Vec::new();
    let mut indent_level = 0u32;

    loop {
        let prompt = if indent_level > 0 { "... " } else { "moo> " };
        eprint!("{prompt}");

        let mut line = String::new();
        if std::io::stdin().read_line(&mut line).unwrap_or(0) == 0 {
            eprintln!("\nTschüss!");
            break;
        }
        let line = line.trim_end_matches('\n').to_string();

        if line.trim() == "quit" || line.trim() == "exit" || line.trim() == "ende" {
            eprintln!("Tschüss!");
            break;
        }

        buffer.push(line.clone());

        let stripped = line.trim();
        if stripped.ends_with(':') {
            indent_level += 1;
            continue;
        } else if indent_level > 0 && stripped.is_empty() {
            indent_level = 0;
        } else if indent_level > 0 && !line.starts_with(' ') && !line.starts_with('\t') {
            indent_level = 0;
        } else if indent_level > 0 {
            continue;
        }

        let new_code = buffer.join("\n");
        buffer.clear();
        indent_level = 0;

        if new_code.trim().is_empty() {
            continue;
        }

        // Bisherige History + neuer Code = komplettes Programm
        let mut full_source = history.join("\n");
        if !full_source.is_empty() {
            full_source.push('\n');
        }
        full_source.push_str(&new_code);

        // Kompilieren und ausführen
        let tmp_dir = std::env::temp_dir();
        let tmp_src = tmp_dir.join("moo_repl.moos");
        let tmp_bin = tmp_dir.join("moo_repl_bin");

        if std::fs::write(&tmp_src, &full_source).is_err() {
            eprintln!("Fehler: Temporäre Datei schreiben fehlgeschlagen");
            continue;
        }

        unsafe { std::env::set_var("MOO_QUIET", "1"); }
        match compile(&tmp_src, Some(&tmp_bin), false, None, false, None, None, false, false, false, "elf", None, &[]) {
            Ok(()) => {
                diagnose::abschliessen();
                // Erfolgreich kompiliert — ausfuehren (ohne den "Kompiliert" Output)
                let _ = Command::new(&tmp_bin).status();
                history.push(new_code);
            }
            Err(e) => {
                // P2-T97: Diagnosen je Eingabe ausgeben und Sammler leeren.
                diagnose::fehler_aus_text(e);
                diagnose::abschliessen();
            }
        }

        let _ = std::fs::remove_file(&tmp_src);
        let _ = std::fs::remove_file(&tmp_bin);
    }
}

/// Paketverzeichnis: ~/.moo/packages/
fn packages_dir() -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| ".".to_string());
    PathBuf::from(home).join(".moo").join("packages")
}

fn handle_paket(action: PaketAction) -> Result<(), String> {
    let pkg_dir = packages_dir();

    match action {
        PaketAction::Install { name } => {
            let target = pkg_dir.join(&name);
            if target.exists() {
                return Err(format!("Paket '{name}' ist bereits installiert"));
            }
            std::fs::create_dir_all(&pkg_dir)
                .map_err(|e| format!("Verzeichnis erstellen fehlgeschlagen: {e}"))?;

            let url = format!("https://github.com/moo-packages/{name}.git");
            println!("Installiere {name} von {url} ...");
            let status = Command::new("git")
                .args(["clone", "--depth", "1", &url, target.to_str().unwrap()])
                .status()
                .map_err(|e| format!("git clone fehlgeschlagen: {e}"))?;

            if !status.success() {
                return Err(format!("Paket '{name}' konnte nicht installiert werden (git clone fehlgeschlagen)"));
            }
            println!("✓ Paket '{name}' installiert nach {}", target.display());
            Ok(())
        }
        PaketAction::List => {
            if !pkg_dir.exists() {
                println!("Keine Pakete installiert.");
                return Ok(());
            }
            let entries = std::fs::read_dir(&pkg_dir)
                .map_err(|e| format!("Verzeichnis lesen fehlgeschlagen: {e}"))?;
            let mut count = 0;
            for entry in entries {
                if let Ok(entry) = entry {
                    if entry.path().is_dir() {
                        println!("  - {}", entry.file_name().to_string_lossy());
                        count += 1;
                    }
                }
            }
            if count == 0 {
                println!("Keine Pakete installiert.");
            } else {
                println!("{count} Paket(e) installiert.");
            }
            Ok(())
        }
        PaketAction::Remove { name } => {
            let target = pkg_dir.join(&name);
            if !target.exists() {
                return Err(format!("Paket '{name}' ist nicht installiert"));
            }
            std::fs::remove_dir_all(&target)
                .map_err(|e| format!("Paket loeschen fehlgeschlagen: {e}"))?;
            println!("✓ Paket '{name}' entfernt.");
            Ok(())
        }
    }
}



#[cfg(test)]
mod windows_linker_contract_tests {
    use super::{
        append_windows_linker_args, resolve_windows_linker_candidates,
        windows_compile_allows_run,
    };
    use std::path::PathBuf;

    fn c(path: &str, executable: bool) -> Option<(PathBuf, bool)> {
        Some((PathBuf::from(path), executable))
    }

    #[test]
    fn resolver_precedence_is_exact_and_fail_closed() {
        let plan = resolve_windows_linker_candidates(
            c("/env/clang.exe", true), c("/env/lld-link.exe", true),
            c("/sibling/clang.exe", true), c("/sibling/lld-link.exe", true),
            c("/path/clang.exe", true), c("/path/lld-link.exe", true),
        ).unwrap();
        assert_eq!(
            plan,
            (
                PathBuf::from("/env/clang.exe"),
                PathBuf::from("/env/lld-link.exe"),
            )
        );

        let sibling = resolve_windows_linker_candidates(
            None, None,
            c("/sibling/clang.exe", true), c("/sibling/lld-link.exe", true),
            c("/path/clang.exe", true), c("/path/lld-link.exe", true),
        ).unwrap();
        assert_eq!(
            sibling,
            (
                PathBuf::from("/sibling/clang.exe"),
                PathBuf::from("/sibling/lld-link.exe"),
            )
        );

        let path = resolve_windows_linker_candidates(
            None, None, None, None,
            c("/path/clang.exe", true), c("/path/lld-link.exe", true),
        ).unwrap();
        assert_eq!(
            path,
            (
                PathBuf::from("/path/clang.exe"),
                PathBuf::from("/path/lld-link.exe"),
            )
        );

        assert!(resolve_windows_linker_candidates(
            c("/bad/clang.exe", false), c("/bad/lld-link.exe", false),
            c("/sibling/clang.exe", true), c("/sibling/lld-link.exe", true),
            None, None,
        ).is_err());
        assert!(resolve_windows_linker_candidates(
            c("/env/clang.exe", true), None, None, None, None, None,
        ).is_err());
    }

    #[test]
    fn linker_args_bind_lld_search_dir_and_canonical_crt_once() {
        #[cfg(target_os = "windows")]
        let lld_path = PathBuf::from(r"C:\toolchain\lld-link.exe");
        #[cfg(target_os = "windows")]
        let expected_search_dir = r"-BC:\toolchain";
        #[cfg(not(target_os = "windows"))]
        let lld_path = PathBuf::from("/toolchain/lld-link.exe");
        #[cfg(not(target_os = "windows"))]
        let expected_search_dir = "-B/toolchain";

        let mut args: Vec<String> = vec![
            "--ld-path=/stale/lld-link.exe".to_string(),
            "-fuse-ld=lld-link".to_string(),
            "-B/stale".to_string(),
        ];
        append_windows_linker_args(&mut args, &lld_path).unwrap();
        append_windows_linker_args(&mut args, &lld_path).unwrap();

        assert_eq!(
            args.iter().filter(|a| *a == "-fuse-ld=lld").count(),
            1
        );
        assert_eq!(
            args.iter().filter(|a| *a == expected_search_dir).count(),
            1
        );
        assert_eq!(
            args.iter().filter(|a| a.starts_with("--ld-path")).count(),
            0
        );
        for lib in ["-lmsvcrt", "-lvcruntime", "-lucrt", "-lkernel32"] {
            assert_eq!(
                args.iter().filter(|a| a.as_str() == lib).count(),
                1,
                "{lib}"
            );
        }

        let mut empty: Vec<String> = Vec::new();
        assert!(append_windows_linker_args(
            &mut empty,
            &PathBuf::from("lld-link.exe"),
        ).is_err());
    }

    #[test]
    fn compile_gate_allows_run_only_after_complete_success() {
        for case in [
            (false, false, None, false),   // StartException/start failure
            (true, true, None, false),     // timeout
            (true, false, Some(7), false), // nonzero rc
            (true, false, None, false),    // missing rc
            (true, false, Some(0), false), // rc0, no EXE
        ] {
            assert!(!windows_compile_allows_run(
                case.0, case.1, case.2, case.3,
            ));
        }
        assert!(windows_compile_allows_run(true, false, Some(0), true));
    }

    #[test]
    fn resolver_rejects_swapped_or_aliased_tool_identities() {
        let resolve_env = |clang: &str, lld: &str| {
            resolve_windows_linker_candidates(
                c(clang, true), c(lld, true), None, None, None, None,
            )
        };

        assert!(resolve_env("/toolchain/clang.exe", "/toolchain/lld-link.exe").is_ok());
        assert!(resolve_env("/toolchain/ClAnG.ExE", "/toolchain/LLD-LINK.EXE").is_ok());
        for (clang, lld) in [
            ("/toolchain/lld-link.exe", "/toolchain/clang.exe"),
            ("/toolchain/clang.exe", "/toolchain/clang.exe"),
            ("/toolchain/clang.exe", "/toolchain/lld.exe"),
        ] {
            assert!(
                resolve_env(clang, lld).is_err(),
                "unexpectedly accepted clang={clang} lld={lld}"
            );
        }
    }
}