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
    },
    /// Eine .moo-Datei kompilieren und sofort ausführen
    Run {
        /// Die .moo-Quelldatei
        file: PathBuf,
    },
    /// Interaktiver Modus (REPL)
    Repl,
}

fn main() {
    let cli = Cli::parse();

    match cli.command {
        Commands::Compile { file, output, emit_ir } => {
            if let Err(e) = compile(&file, output.as_deref(), emit_ir) {
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
    }
}

/// Liest eine .moo-Datei und löst Imports rekursiv auf
fn resolve_imports(file: &PathBuf, visited: &mut std::collections::HashSet<PathBuf>) -> Result<String, String> {
    let canonical = file.canonicalize().unwrap_or(file.clone());
    if visited.contains(&canonical) {
        return Ok(String::new()); // Zirkuläre Imports vermeiden
    }
    visited.insert(canonical);

    let source = std::fs::read_to_string(file)
        .map_err(|e| format!("Datei '{}' lesen fehlgeschlagen: {e}", file.display()))?;

    let dir = file.parent().unwrap_or(std::path::Path::new("."));
    let mut result = String::new();

    for line in source.lines() {
        let trimmed = line.trim();
        // "importiere <name>" oder "import <name>" — Datei einbinden
        let import_module = if trimmed.starts_with("importiere ") {
            Some(trimmed.strip_prefix("importiere ").unwrap().trim())
        } else if trimmed.starts_with("import ") && !trimmed.contains(" from ") {
            Some(trimmed.strip_prefix("import ").unwrap().trim())
        } else {
            None
        };

        if let Some(module_name) = import_module {
            // "als" / "as" Alias entfernen
            let name = module_name.split_whitespace().next().unwrap_or(module_name);
            let import_path = dir.join(format!("{name}.moo"));
            if import_path.exists() {
                let imported = resolve_imports(&import_path, visited)?;
                result.push_str(&imported);
                result.push('\n');
                continue;
            }
            // Datei nicht gefunden — Import-Zeile beibehalten (wird vom Parser ignoriert)
        }

        result.push_str(line);
        result.push('\n');
    }

    Ok(result)
}

fn compile(file: &PathBuf, output: Option<&std::path::Path>, emit_ir: bool) -> Result<(), String> {
    // Imports rekursiv auflösen
    let mut visited = std::collections::HashSet::new();
    let source = resolve_imports(file, &mut visited)?;

    // Lexer → Parser → AST
    let mut lex = lexer::Lexer::new();
    let tokens = lex.tokenize(&source).map_err(|e| e.to_string())?;
    let mut par = parser::Parser::new(tokens);
    let program = par.parse().map_err(|e| e.to_string())?;

    // LLVM Codegen
    let context = inkwell::context::Context::create();
    let module_name = file.file_stem().unwrap().to_str().unwrap();
    let mut compiler = codegen::CodeGen::new(&context, module_name);
    compiler.compile_program(&program)?;

    if emit_ir {
        let ir_path = output
            .map(|p| p.to_path_buf())
            .unwrap_or_else(|| file.with_extension("ll"));
        compiler.write_ir(&ir_path)?;
        println!("✓ LLVM IR geschrieben: {}", ir_path.display());
        return Ok(());
    }

    // Object-File erzeugen
    let obj_path = file.with_extension("o");
    compiler.write_object(&obj_path)?;

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

    let link_status = Command::new("cc")
        .args([
            obj_path.to_str().unwrap(),
            runtime_lib.to_str().unwrap(),
            "-o",
            output_path.to_str().unwrap(),
            "-lm",
        ])
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

    compile(file, Some(&tmp_output), false)?;

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
        match compile(&tmp_src, Some(&tmp_bin), false) {
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
