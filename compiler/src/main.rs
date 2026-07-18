mod ast;
mod boards;
mod codegen;
mod lexer;
mod parser;
mod runtime_bindings;
mod tokens;

use clap::{Parser as ClapParser, Subcommand};
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(ClapParser)]
#[command(name = "moo", about = "moo — die universelle Programmiersprache (nativer Compiler)")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
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

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Compile { file, output, emit_ir, target, no_stdlib, linker_script, entry, profile, erlaube_kern, kernel, emit, board } => {
            if let Err(e) = compile(&file, output.as_deref(), emit_ir, target.as_deref(), no_stdlib, linker_script.as_deref(), entry.as_deref(), profile, erlaube_kern, kernel, &emit, board.as_deref()) {
                eprintln!("Fehler: {e}");
                std::process::exit(1);
            }
        }
        Commands::Run { file } => {
            if let Err(e) = run(&file) {
                eprintln!("Fehler: {e}");
                std::process::exit(1);
            }
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

/// Parst eine .moos-Datei zu einem AST
fn parse_file(file: &PathBuf) -> Result<ast::Program, String> {
    let source = std::fs::read_to_string(file)
        .map_err(|e| format!("Datei '{}' lesen fehlgeschlagen: {e}", file.display()))?;
    let mut lex = lexer::Lexer::new();
    let tokens = lex.tokenize(&source).map_err(|e| e.to_string())?;
    let mut par = parser::Parser::new(tokens);
    par.parse().map_err(|e| e.to_string())
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

fn find_module(name: &str, dir: &std::path::Path) -> Option<PathBuf> {
    // Relativ zum aktuellen Verzeichnis
    if let Some(p) = module_file(dir, name) { return Some(p); }
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

/// Sammelt alle Funktionsnamen aus einem AST
fn collect_function_names(stmts: &[ast::Stmt]) -> std::collections::HashSet<String> {
    stmts.iter().filter_map(|s| {
        if let ast::Stmt::FunctionDef { name, .. } = s { Some(name.clone()) } else { None }
    }).collect()
}

/// Prefixed FunctionCall-Namen in einer Expression — ALLE Varianten abgedeckt
fn prefix_expr(expr: &ast::Expr, prefix: &str, names: &std::collections::HashSet<String>) -> ast::Expr {
    let pe = |e: &ast::Expr| -> ast::Expr { prefix_expr(e, prefix, names) };
    let pe_box = |e: &ast::Expr| -> Box<ast::Expr> { Box::new(pe(e)) };
    let pe_vec = |v: &[ast::Expr]| -> Vec<ast::Expr> { v.iter().map(|e| pe(e)).collect() };
    let pe_opt = |o: &Option<Box<ast::Expr>>| -> Option<Box<ast::Expr>> {
        o.as_ref().map(|e| pe_box(e))
    };

    match expr {
        ast::Expr::FunctionCall { name, args } => {
            let new_name = if names.contains(name) {
                format!("{prefix}__{name}")
            } else {
                name.clone()
            };
            ast::Expr::FunctionCall { name: new_name, args: pe_vec(args) }
        }
        ast::Expr::MethodCall { object, method, args } => ast::Expr::MethodCall {
            object: pe_box(object), method: method.clone(), args: pe_vec(args),
        },
        ast::Expr::BinaryOp { left, op, right } => ast::Expr::BinaryOp {
            left: pe_box(left), op: op.clone(), right: pe_box(right),
        },
        ast::Expr::UnaryOp { op, operand } => ast::Expr::UnaryOp {
            op: op.clone(), operand: pe_box(operand),
        },
        ast::Expr::Lambda { params, body } => ast::Expr::Lambda {
            params: params.clone(), body: pe_box(body),
        },
        ast::Expr::List(elems) => ast::Expr::List(pe_vec(elems)),
        ast::Expr::Dict(pairs) => ast::Expr::Dict(
            pairs.iter().map(|(k, v)| (pe(k), pe(v))).collect()
        ),
        ast::Expr::ListComprehension { expr, var_name, iterable, condition } => {
            ast::Expr::ListComprehension {
                expr: pe_box(expr), var_name: var_name.clone(),
                iterable: pe_box(iterable), condition: pe_opt(condition),
            }
        }
        ast::Expr::IndexAccess { object, index } => ast::Expr::IndexAccess {
            object: pe_box(object), index: pe_box(index),
        },
        ast::Expr::PropertyAccess { object, property } => ast::Expr::PropertyAccess {
            object: pe_box(object), property: property.clone(),
        },
        ast::Expr::Pipe { left, right } => ast::Expr::Pipe {
            left: pe_box(left), right: pe_box(right),
        },
        ast::Expr::Spread(inner) => ast::Expr::Spread(pe_box(inner)),
        ast::Expr::NullishCoalesce { left, right } => ast::Expr::NullishCoalesce {
            left: pe_box(left), right: pe_box(right),
        },
        ast::Expr::OptionalChain { object, property } => ast::Expr::OptionalChain {
            object: pe_box(object), property: property.clone(),
        },
        ast::Expr::Range { start, end } => ast::Expr::Range {
            start: pe_box(start), end: pe_box(end),
        },
        ast::Expr::New { class_name, args } => ast::Expr::New {
            class_name: class_name.clone(), args: pe_vec(args),
        },
        // Literale + einfache Knoten: keine Sub-Expressions
        other => other.clone(),
    }
}

/// Prefixed FunctionCall-Namen in Statements
fn prefix_stmts(stmts: &[ast::Stmt], prefix: &str, names: &std::collections::HashSet<String>) -> Vec<ast::Stmt> {
    stmts.iter().map(|s| prefix_stmt(s, prefix, names)).collect()
}

fn prefix_stmt(stmt: &ast::Stmt, prefix: &str, names: &std::collections::HashSet<String>) -> ast::Stmt {
    match stmt {
        ast::Stmt::Assignment { name, value } => ast::Stmt::Assignment {
            name: name.clone(), value: prefix_expr(value, prefix, names),
        },
        ast::Stmt::ConstAssignment { name, value } => ast::Stmt::ConstAssignment {
            name: name.clone(), value: prefix_expr(value, prefix, names),
        },
        ast::Stmt::Show(e) => ast::Stmt::Show(prefix_expr(e, prefix, names)),
        ast::Stmt::Return(Some(e)) => ast::Stmt::Return(Some(prefix_expr(e, prefix, names))),
        ast::Stmt::If { condition, body, else_body } => ast::Stmt::If {
            condition: prefix_expr(condition, prefix, names),
            body: prefix_stmts(body, prefix, names),
            else_body: prefix_stmts(else_body, prefix, names),
        },
        ast::Stmt::While { condition, body } => ast::Stmt::While {
            condition: prefix_expr(condition, prefix, names),
            body: prefix_stmts(body, prefix, names),
        },
        ast::Stmt::For { var_name, iterable, body } => ast::Stmt::For {
            var_name: var_name.clone(),
            iterable: prefix_expr(iterable, prefix, names),
            body: prefix_stmts(body, prefix, names),
        },
        ast::Stmt::Expression(e) => ast::Stmt::Expression(prefix_expr(e, prefix, names)),
        other => other.clone(),
    }
}

/// Prefixed alle FunctionDef-Namen UND interne Aufrufe in einem AST mit "modul__"
fn prefix_functions(stmts: &[ast::Stmt], prefix: &str) -> Vec<ast::Stmt> {
    let func_names = collect_function_names(stmts);
    stmts.iter().map(|stmt| {
        match stmt {
            ast::Stmt::FunctionDef { name, params, defaults, body, decorators } => {
                ast::Stmt::FunctionDef {
                    name: format!("{prefix}__{name}"),
                    params: params.clone(),
                    defaults: defaults.clone(),
                    body: prefix_stmts(body, prefix, &func_names),
                    decorators: decorators.clone(),
                }
            }
            // Export: FunctionDef darin prefixen (KEIN rekursiver prefix_functions Aufruf!)
            ast::Stmt::Export(inner) => {
                if let ast::Stmt::FunctionDef { name, params, defaults, body, decorators } = inner.as_ref() {
                    ast::Stmt::Export(Box::new(ast::Stmt::FunctionDef {
                        name: format!("{prefix}__{name}"),
                        params: params.clone(),
                        defaults: defaults.clone(),
                        body: prefix_stmts(body, prefix, &func_names),
                        decorators: decorators.clone(),
                    }))
                } else {
                    stmt.clone()
                }
            }
            // Nicht-Funktionen (z.B. Variablen-Initialisierung) auch mit reinnehmen
            other => other.clone(),
        }
    }).collect()
}

/// Erzeugt Alias-Funktionen: func(args) → modul__func(args)
/// Fuer "aus mathe importiere sin, cos" → sin ruft mathe__sin auf
fn create_alias_functions(stmts: &[ast::Stmt], prefix: &str, names: &[String]) -> Vec<ast::Stmt> {
    let mut aliases = Vec::new();
    for stmt in stmts {
        if let ast::Stmt::FunctionDef { name, params, .. } = stmt {
            // Nur wenn der Name (ohne Prefix) in der Import-Liste ist
            let bare_name = name.strip_prefix(&format!("{prefix}__")).unwrap_or(name);
            if names.contains(&bare_name.to_string()) {
                // Alias: funktion sin(a): gib_zurück mathe__sin(a)
                let call = ast::Expr::FunctionCall {
                    name: name.clone(), // prefixed name
                    args: params.iter().map(|p| ast::Expr::Identifier(p.clone())).collect(),
                };
                aliases.push(ast::Stmt::FunctionDef {
                    name: bare_name.to_string(),
                    params: params.clone(),
                    defaults: params.iter().map(|_| None).collect(),
                    body: vec![ast::Stmt::Return(Some(call))],
                    decorators: vec![],
                });
            }
        }
    }
    aliases
}

/// Wickelt einen `exportiere X`-Wrapper (Stmt::Export) zu seinem inneren Statement aus.
/// Das Modulsystem ist inline-basiert; `exportiere` ist nur ein Sichtbarkeits-Marker und
/// darf die Definition nicht vor codegen verstecken (codegen behandelt Stmt::Export nicht,
/// wuerde die umschlossene Funktion/Klasse also stillschweigend verwerfen).
fn strip_export(stmt: ast::Stmt) -> ast::Stmt {
    match stmt {
        ast::Stmt::Export(inner) => *inner,
        other => other,
    }
}

/// Löst Imports AST-basiert auf: parst Module, prefixed Funktionen, erzeugt Aliases
fn resolve_modules(
    program: &mut ast::Program,
    file_dir: &std::path::Path,
    visited: &mut std::collections::HashSet<PathBuf>,
) -> Result<(), String> {
    let mut imported_stmts: Vec<ast::Stmt> = Vec::new();
    let mut remaining_stmts: Vec<ast::Stmt> = Vec::new();

    for stmt in program.statements.drain(..) {
        match &stmt {
            ast::Stmt::Import { module, names, .. } => {
                if let Some(mod_path) = find_module(module, file_dir) {
                    let canonical = mod_path.canonicalize().unwrap_or(mod_path.clone());
                    if visited.contains(&canonical) {
                        continue; // Zirkulaeren Import vermeiden
                    }
                    visited.insert(canonical);

                    // Modul parsen
                    let mut mod_program = parse_file(&mod_path)?;

                    // Rekursiv Imports im Modul aufloesen
                    let mod_dir = mod_path.parent().unwrap_or(std::path::Path::new("."));
                    resolve_modules(&mut mod_program, mod_dir, visited)?;

                    if names.is_empty() {
                        // "importiere mathe" → ALLE Statements direkt einfuegen (kein Prefix!)
                        // exportiere-Wrapper auswickeln, damit die Definition codegen erreicht.
                        imported_stmts.extend(mod_program.statements.into_iter().map(strip_export));
                    } else {
                        // "aus mathe importiere fakultaet" → alle DEFINITIONEN des Moduls
                        // (Funktionen, Klassen, DataClasses; exportiere ausgewickelt) plus alle
                        // Variablen/Konstanten. Nur ausfuehrbare Top-Level-Statements werden
                        // verworfen, damit ein Import keine Seiteneffekte ausloest. Im inline-
                        // Modell ist das `names`-Filter rein informativ: sonst muessten Klassen
                        // und transitiv genutzte Helfer eines Moduls einzeln importiert werden.
                        for mod_stmt in mod_program.statements {
                            match strip_export(mod_stmt) {
                                s @ (ast::Stmt::FunctionDef { .. }
                                    | ast::Stmt::ClassDef { .. }
                                    | ast::Stmt::DataClassDef { .. }
                                    | ast::Stmt::Assignment { .. }
                                    | ast::Stmt::ConstAssignment { .. }) => {
                                    imported_stmts.push(s);
                                }
                                _ => {}
                            }
                        }
                    }
                }
                // Modul nicht gefunden → Import-Statement droppen
            }
            // Eigene Statements der Datei (inkl. same-file `exportiere funktion/klasse`)
            _ => remaining_stmts.push(strip_export(stmt)),
        }
    }

    // Importierte Statements VOR dem Hauptprogramm einfuegen
    imported_stmts.extend(remaining_stmts);
    program.statements = imported_stmts;
    Ok(())
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
    const KERNEL_SOURCES: [&str; 13] = [
        "moo_bare.c",
        "moo_surface_core.c",
        "moo_compositor_core.c",
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

    for src in KERNEL_SOURCES {
        let src_path = runtime_dir.join(src);
        if !src_path.exists() {
            return Err(format!("Kernel-Runtime-Quelle fehlt: {}", src_path.display()));
        }
        let obj = tmp.join(format!("{src}.o"));
        let mut cmd = Command::new(cc_prog);
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
    ld.arg("-n").arg("-T").arg(&script).arg("-o").arg(&elf_path);
    for o in &objects {
        ld.arg(o);
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
    let lld_path = lld_path
        .to_str()
        .ok_or_else(|| "lld-link-Pfad ist kein gültiger Unicode-Pfad".to_string())?;
    const WINDOWS_LIBS: [&str; 4] = [
        "-lmsvcrt",
        "-lvcruntime",
        "-lucrt",
        "-lkernel32",
    ];

    let mut normalized = Vec::with_capacity(args.len() + 5);
    let mut skip_ld_path_value = false;
    for arg in args.drain(..) {
        if skip_ld_path_value {
            skip_ld_path_value = false;
            continue;
        }
        if arg == "--ld-path" {
            skip_ld_path_value = true;
            continue;
        }
        if arg == "-fuse-ld=lld"
            || arg.starts_with("--ld-path=")
            || WINDOWS_LIBS.contains(&arg.as_str())
        {
            continue;
        }
        normalized.push(arg);
    }
    normalized.push(format!("--ld-path={lld_path}"));
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

fn compile(file: &PathBuf, output: Option<&std::path::Path>, emit_ir: bool, target: Option<&str>, no_stdlib: bool, linker_script: Option<&std::path::Path>, entry_point: Option<&str>, profile: bool, erlaube_kern: bool, kernel: bool, emit: &str, board: Option<&str>) -> Result<(), String> {
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
    // Haupt-Datei parsen
    let mut program = parse_file(file)?;

    // Module AST-basiert auflösen
    let file_dir = file.parent().unwrap_or(std::path::Path::new("."));
    let mut visited = std::collections::HashSet::new();
    let canonical = file.canonicalize().unwrap_or(file.clone());
    visited.insert(canonical);
    resolve_modules(&mut program, file_dir, &mut visited)?;

    // LLVM Codegen
    let context = inkwell::context::Context::create();
    let module_name = file.file_stem().unwrap().to_str().unwrap();
    let mut compiler = codegen::CodeGen::new(&context, module_name);
    if profile {
        compiler.set_profiling(true);
    }
    // Bare-Metal-Kernel-Modus (Plan-010): kern_*-Builtins nur im
    // --no-stdlib/*-bare-Build, oder mit explizitem --erlaube-kern.
    // MUSS vor compile_program() gesetzt sein (Guard greift im Codegen).
    let is_bare_target = target.map(|t| t.contains("-bare")).unwrap_or(false);
    compiler.set_bare_mode(no_stdlib || is_bare_target || erlaube_kern);
    compiler.compile_program(&program)?;

    if emit_ir {
        let ir_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("ll"));
        compiler.write_ir(&ir_path)?;
        println!("✓ LLVM IR geschrieben: {}", ir_path.display());
        return Ok(());
    }

    let target_str = target.unwrap_or("native");

    // P011-C1: flat/sector existieren nur in der Kernel-/Loader-Pipeline
    // (objcopy-Pfad haengt an build_kernel).
    if emit != "elf" && !(no_stdlib && target_str.contains("-bare") && (kernel || linker_script.is_some())) {
        return Err("--emit flat|sector wirkt nur in der Kernel-/Loader-Pipeline: --no-stdlib + *-bare-Target + --kernel (oder --linker-script)".to_string());
    }

    // Bare-Metal: One-Shot-Kernel-Pipeline oder nur Object-File
    if no_stdlib {
        // Plan-010 C2: --kernel (oder explizites --linker_script) bei einem
        // *-bare-Target baut moo->.o + moo_bare*.c -> ld -T script -> kernel.elf.
        let is_bare_target = target_str.contains("-bare");
        if is_bare_target && (kernel || linker_script.is_some()) {
            return build_kernel(file, &compiler, output, target_str, linker_script, emit, board_profile);
        }
        let obj_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("o"));
        compiler.write_object(&obj_path, target_str)?;
        let resolved = codegen::CodeGen::resolve_triple(target_str);
        println!("✓ Bare-Metal kompiliert ({}): {}", resolved, obj_path.display());
        println!("  Ohne Runtime. Linken mit: cc {0} moo_bare.o -nostdlib -o firmware", obj_path.display());
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
        let obj_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("o"));
        compiler.write_object(&obj_path, target_str)?;
        println!("✓ Cross-kompiliert fuer {}: {}", resolved, obj_path.display());
        println!("  Hinweis: Object-File fuer {}. Linken muss auf dem Zielsystem erfolgen.", resolved);
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
        "-llibcurl".to_string(), "-lsqlite3".to_string(),
        "-lws2_32".to_string(), "-lbcrypt".to_string(),
    ]);
    #[cfg(not(target_os = "windows"))]
    link_args.extend([
        "-lm".to_string(), "-lpthread".to_string(),
        "-lcurl".to_string(), "-lsqlite3".to_string(),
        "-lSDL2".to_string(), "-lSDL2_image".to_string(),
    ]);
    // TLS-Backend-Libs per Build-Flag MOO_TLS_BACKEND (build.rs -> cfg moo_tls_mbedtls).
    // Vendored mbedTLS braucht spaeter gar keine System-Libs mehr.
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
    #[cfg(any(feature = "gl21", feature = "gl33"))]
    {
        link_args.push("-lGL".to_string());
        link_args.push("-lglfw".to_string());
    }
    #[cfg(feature = "vulkan")]
    {
        link_args.push("-lvulkan".to_string());
        #[cfg(not(any(feature = "gl21", feature = "gl33")))]
        link_args.push("-lglfw".to_string());
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

fn run(file: &PathBuf) -> Result<(), String> {
    let tmp_dir = std::env::temp_dir();
    #[cfg(target_os = "windows")]
    let tmp_name = format!("moo_tmp_binary_{}.exe", std::process::id());
    #[cfg(not(target_os = "windows"))]
    let tmp_name = format!("moo_tmp_binary_{}", std::process::id());
    let tmp_output = tmp_dir.join(tmp_name);

    compile(file, Some(&tmp_output), false, None, false, None, None, false, false, false, "elf", None)?;

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
        match compile(&tmp_src, Some(&tmp_bin), false, None, false, None, None, false, false, false, "elf", None) {
            Ok(()) => {
                // Erfolgreich kompiliert — ausfuehren (ohne den "Kompiliert" Output)
                let _ = Command::new(&tmp_bin).status();
                history.push(new_code);
            }
            Err(e) => {
                eprintln!("Fehler: {e}");
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
    fn linker_args_bind_absolute_lld_and_canonical_crt_once() {
        let mut args: Vec<String> = vec!["-fuse-ld=lld".to_string()];
        append_windows_linker_args(
            &mut args,
            &PathBuf::from("/toolchain/lld-link.exe"),
        ).unwrap();
        append_windows_linker_args(
            &mut args,
            &PathBuf::from("/toolchain/lld-link.exe"),
        ).unwrap();

        assert_eq!(
            args.iter()
                .filter(|a| *a == "--ld-path=/toolchain/lld-link.exe")
                .count(),
            1
        );
        assert_eq!(
            args.iter().filter(|a| *a == "-fuse-ld=lld").count(),
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