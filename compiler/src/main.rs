mod ast;
mod codegen;
mod lexer;
mod parser;
mod runtime_bindings;
mod tokens;

use clap::{Parser as ClapParser, Subcommand};
use std::path::PathBuf;
use std::process::Command;

#[derive(ClapParser)]
#[command(name = "moo", about = "moo — die universelle Programmiersprache (nativer Compiler)")]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// Eine .moo-Datei kompilieren und als native Binary erzeugen
    Compile {
        /// Die .moo-Quelldatei
        file: PathBuf,
        /// Ausgabedatei (Standard: Name ohne .moo)
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
    },
    /// Eine .moo-Datei kompilieren und sofort ausführen
    Run {
        /// Die .moo-Quelldatei
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
        Commands::Compile { file, output, emit_ir, target, no_stdlib, linker_script, entry, profile } => {
            if let Err(e) = compile(&file, output.as_deref(), emit_ir, target.as_deref(), no_stdlib, linker_script.as_deref(), entry.as_deref(), profile) {
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
            println!("\nVerwendung: moo-compiler compile datei.moo --target <name>");
        }
        Commands::Paket { action } => {
            if let Err(e) = handle_paket(action) {
                eprintln!("Fehler: {e}");
                std::process::exit(1);
            }
        }
    }
}

/// Parst eine .moo-Datei zu einem AST
fn parse_file(file: &PathBuf) -> Result<ast::Program, String> {
    let source = std::fs::read_to_string(file)
        .map_err(|e| format!("Datei '{}' lesen fehlgeschlagen: {e}", file.display()))?;
    let mut lex = lexer::Lexer::new();
    let tokens = lex.tokenize(&source).map_err(|e| e.to_string())?;
    let mut par = parser::Parser::new(tokens);
    par.parse().map_err(|e| e.to_string())
}

/// Findet die .moo-Datei fuer ein Modul (relativ zum Import-Ort oder in packages)
fn find_module(name: &str, dir: &std::path::Path) -> Option<PathBuf> {
    // Relativ zum aktuellen Verzeichnis
    let local = dir.join(format!("{name}.moo"));
    if local.exists() { return Some(local); }
    // In stdlib/ relativ zum aktuellen Verzeichnis
    let stdlib = dir.join("stdlib").join(format!("{name}.moo"));
    if stdlib.exists() { return Some(stdlib); }
    // In stdlib/ relativ zum Compiler-Binary (fuer installierte stdlib)
    if let Ok(exe) = std::env::current_exe() {
        if let Some(exe_dir) = exe.parent() {
            let installed_stdlib = exe_dir.join("stdlib").join(format!("{name}.moo"));
            if installed_stdlib.exists() { return Some(installed_stdlib); }
        }
    }
    // In stdlib/ relativ zum cwd (K4 Fix)
    let cwd = std::env::current_dir().unwrap_or_default();
    let cwd_stdlib = cwd.join("stdlib").join(format!("{name}.moo"));
    if cwd_stdlib.exists() { return Some(cwd_stdlib); }
    // In ~/.moo/packages/<name>/<name>.moo
    let pkg = packages_dir().join(name).join(format!("{name}.moo"));
    if pkg.exists() { return Some(pkg); }
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
                        imported_stmts.extend(mod_program.statements);
                    } else {
                        // "aus mathe importiere fakultaet" → gewuenschte Funktionen
                        // + ALLE Variablen/Konstanten (werden von Funktionen gebraucht)
                        for mod_stmt in mod_program.statements {
                            match &mod_stmt {
                                ast::Stmt::FunctionDef { name, .. } => {
                                    if names.contains(name) {
                                        imported_stmts.push(mod_stmt);
                                    }
                                }
                                // Variablen + Konstanten immer importieren
                                // (Modul-Funktionen koennten sie referenzieren)
                                ast::Stmt::Assignment { .. } |
                                ast::Stmt::ConstAssignment { .. } => {
                                    imported_stmts.push(mod_stmt);
                                }
                                _ => {}
                            }
                        }
                    }
                }
                // Modul nicht gefunden → Import-Statement droppen
            }
            _ => remaining_stmts.push(stmt),
        }
    }

    // Importierte Statements VOR dem Hauptprogramm einfuegen
    imported_stmts.extend(remaining_stmts);
    program.statements = imported_stmts;
    Ok(())
}

fn compile(file: &PathBuf, output: Option<&std::path::Path>, emit_ir: bool, target: Option<&str>, no_stdlib: bool, linker_script: Option<&std::path::Path>, entry_point: Option<&str>, profile: bool) -> Result<(), String> {
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

    // Bare-Metal: nur Object-File ohne Runtime-Linking
    if no_stdlib {
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

    // Runtime-Archiv finden (von build.rs kompiliert)
    let runtime_dir = PathBuf::from(env!("OUT_DIR"));
    let runtime_lib = runtime_dir.join("libmoo_runtime.a");

    let mut link_args = vec![
        obj_path.to_str().unwrap().to_string(),
        runtime_lib.to_str().unwrap().to_string(),
        "-o".to_string(),
        output_path.to_str().unwrap().to_string(),
        "-lm".to_string(),
        "-lpthread".to_string(),
        "-lcurl".to_string(),
        "-lsqlite3".to_string(),
        "-lSDL2".to_string(),
        "-lSDL2_image".to_string(),
    ];

    // Linux-only UI-Libs (GTK3 + libappindicator3 + Co.) — analog build.rs
    // gegated, damit macOS/Windows den Linker nicht nach diesen Symbolen
    // fragen. macOS nutzt stattdessen Cocoa (moo_ui_cocoa.m, Framework-Links
    // via build.rs), Windows nutzt Win32 (moo_ui_win32.c).
    #[cfg(target_os = "linux")]
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

    let link_status = Command::new("cc")
        .args(&link_args)
        .status()
        .map_err(|e| format!("Linker starten fehlgeschlagen: {e}"))?;

    // Object-File aufräumen
    let _ = std::fs::remove_file(&obj_path);

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
    let tmp_output = tmp_dir.join("moo_tmp_binary");

    compile(file, Some(&tmp_output), false, None, false, None, None, false)?;

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
        let tmp_src = tmp_dir.join("moo_repl.moo");
        let tmp_bin = tmp_dir.join("moo_repl_bin");

        if std::fs::write(&tmp_src, &full_source).is_err() {
            eprintln!("Fehler: Temporäre Datei schreiben fehlgeschlagen");
            continue;
        }

        unsafe { std::env::set_var("MOO_QUIET", "1"); }
        match compile(&tmp_src, Some(&tmp_bin), false, None, false, None, None, false) {
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
