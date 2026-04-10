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
    }
}

fn compile(file: &PathBuf, output: Option<&std::path::Path>, emit_ir: bool) -> Result<(), String> {
    let source = std::fs::read_to_string(file)
        .map_err(|e| format!("Datei lesen fehlgeschlagen: {e}"))?;

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

    println!("✓ Kompiliert: {} → {}", file.display(), output_path.display());
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
