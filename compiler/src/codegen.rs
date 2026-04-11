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
    // Klassen-Konstruktoren: class_name -> FunctionValue
    class_constructors: HashMap<String, FunctionValue<'ctx>>,
    // Klassen-Methoden: (class_name, method_name) -> FunctionValue
    class_methods: HashMap<(String, String), FunctionValue<'ctx>>,
    // Lambda-Mapping: variable_name -> LLVM function name
    lambda_names: HashMap<String, String>,
    // Lambda-Captures: lambda_fn_name -> [captured var names]
    lambda_captures: HashMap<String, Vec<String>>,
    // Lambda-Counter
    lambda_counter: usize,
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
            class_constructors: HashMap::new(),
            class_methods: HashMap::new(),
            lambda_names: HashMap::new(),
            lambda_captures: HashMap::new(),
            lambda_counter: 0,
            defer_stack: Vec::new(),
            variable_types: HashMap::new(),
            warnings: Vec::new(),
            unsafe_context: false,
            test_names: Vec::new(),
            profiling: false,
        }
    }

    pub fn set_profiling(&mut self, enabled: bool) {
        self.profiling = enabled;
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
        let none_init = self.context.i64_type().const_int(3, false);
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

        // Typ-Warnungen ausgeben (Crystal-inspiriert)
        for w in &self.warnings {
            eprintln!("Warnung: {w}");
        }

        self.module.verify().map_err(|e| format!("LLVM Verifikation fehlgeschlagen: {}", e.to_string()))
    }

    fn forward_declare(&mut self, program: &Program) -> Result<(), String> {
        for stmt in &program.statements {
            match stmt {
                Stmt::FunctionDef { name, params, .. } => {
                    let mv = self.mv_type();
                    let param_types: Vec<BasicMetadataTypeEnum> = params.iter()
                        .map(|_| BasicMetadataTypeEnum::from(mv))
                        .collect();
                    let fn_type = mv.fn_type(&param_types, false);
                    self.module.add_function(name, fn_type, None);
                }
                Stmt::ClassDef { name, body, .. } => {
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
            "x86" | "i686" | "i386" => "i686-unknown-linux-gnu".to_string(),
            "arm" | "armv7" => "arm-unknown-linux-gnueabihf".to_string(),
            "aarch64" | "arm64" => "aarch64-unknown-linux-gnu".to_string(),
            "riscv64" => "riscv64-unknown-linux-gnu".to_string(),
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
            ("x86", "32-bit x86 (Linux)"),
            ("arm", "ARMv7 (Linux, z.B. Raspberry Pi)"),
            ("aarch64", "ARM 64-bit (Linux, z.B. Apple M-Serie)"),
            ("riscv64", "RISC-V 64-bit (Linux)"),
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

        let machine = target_obj
            .create_target_machine(
                &triple,
                &cpu_str,
                &feat_str,
                OptimizationLevel::Default,
                RelocMode::PIC,
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
            let none_init = self.context.i64_type().const_int(3, false); // MOO_NONE tag
            let zero_data = self.context.i64_type().const_int(0, false);
            let none_val = self.mv_type().const_named_struct(&[none_init.into(), zero_data.into()]);
            self.builder.build_store(alloca, none_val).map_err(|e| format!("{e}"))?;

            self.builder.position_at_end(current_block);
            self.variables.insert(name.to_string(), alloca);
            alloca
        };
        // Retain neuen Wert
        self.call_rt_void(self.rt.moo_retain, &[val.into()], "retain_new")?;
        self.builder.build_store(ptr, val).map_err(|e| format!("{e}"))?;
        Ok(())
    }

    fn load_var(&self, name: &str) -> Result<StructValue<'ctx>, String> {
        let ptr = self.variables.get(name)
            .or_else(|| self.globals.get(name))
            .ok_or(format!("Variable '{name}' nicht gefunden"))?;
        let val = self.builder.build_load(self.mv_type(), *ptr, name)
            .map_err(|e| format!("{e}"))?;
        Ok(val.into_struct_value())
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
                    &[obj.into(), prop_str.into(), val.into()], "obj_set")
            }
            Stmt::IndexAssignment { object, index, value } => {
                let obj = self.compile_expr(object)?;
                let idx = self.compile_expr(index)?;
                let val = self.compile_expr(value)?;
                self.call_rt_void(self.rt.moo_index_set,
                    &[obj.into(), idx.into(), val.into()], "idx_set")
            }
            Stmt::Show(expr) => {
                let val = self.compile_expr(expr)?;
                self.call_rt_void(self.rt.moo_print, &[val.into()], "print")
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
                self.compile_function_def(name, params, defaults, body)?;
                // Decorators: func = decorator(func)
                for dec_name in decorators.iter().rev() {
                    if let Some(dec_fn) = self.module.get_function(dec_name) {
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
                self.call_rt_void(self.rt.moo_throw, &[val.into()], "throw")
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
                self.compile_expr(expr)?;
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
        let idx_ptr = self.builder.build_alloca(i32_type, "for_idx")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(idx_ptr, i32_type.const_int(0, false))
            .map_err(|e| format!("{e}"))?;

        // Wir brauchen die Liste gespeichert, damit wir sie im Loop-Body laden koennen
        let list_ptr = self.builder.build_alloca(self.mv_type(), "for_list")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_store(list_ptr, list_val).map_err(|e| format!("{e}"))?;

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
        Ok(())
    }

    fn compile_function_def(&mut self, name: &str, params: &[String], defaults: &[Option<Expr>], body: &[Stmt]) -> Result<(), String> {
        let function = self.module.get_function(name)
            .ok_or(format!("Funktion '{name}' nicht forward-declared"))?;

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
                    self.context.i64_type().const_int(3, false), // MOO_NONE = 3
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
            let none_val = self.call_rt(self.rt.moo_none, &[], "none")?;
            self.builder.build_return(Some(&none_val)).map_err(|e| format!("{e}"))?;
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

    fn compile_return(&mut self, value: &Option<Expr>) -> Result<(), String> {
        let val = if let Some(expr) = value {
            self.compile_expr(expr)?
        } else {
            self.call_rt(self.rt.moo_none, &[], "none")?
        };
        // Defer-Stack vor Return ausführen (Go-Semantik)
        self.emit_defers()?;
        self.builder.build_return(Some(&val)).map_err(|e| format!("{e}"))?;
        Ok(())
    }

    fn compile_class_def(&mut self, name: &str, parent: &Option<String>, body: &[Stmt]) -> Result<(), String> {
        // Methoden kompilieren
        for stmt in body {
            if let Stmt::FunctionDef { name: method_name, params, body: method_body, .. } = stmt {
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

                // Regulaere Parameter
                for (i, param_name) in params.iter().enumerate() {
                    let alloca = self.builder.build_alloca(self.mv_type(), param_name)
                        .map_err(|e| format!("{e}"))?;
                    let param_val = func.get_nth_param((i + 1) as u32).unwrap();
                    self.builder.build_store(alloca, param_val).map_err(|e| format!("{e}"))?;
                    self.variables.insert(param_name.clone(), alloca);
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
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }
            }
        }

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

        // moo_try_enter() — markiert dass wir in einem try-Block sind
        self.call_rt_void(self.rt.moo_try_enter, &[], "try_enter")?;

        // Try-Body ausfuehren
        for s in try_body {
            self.compile_stmt(s)?;
        }

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

        let catch_bb = self.context.append_basic_block(func, "catch_body");
        let after_bb = self.context.append_basic_block(func, "after_try");

        self.builder.build_conditional_branch(is_error, catch_bb, after_bb)
            .map_err(|e| format!("{e}"))?;

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
            Expr::Identifier(name) => self.load_var(name),
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
                        self.context.i64_type().const_int(7, false), // MOO_OBJECT = 7
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
                        // Bit-Operatoren: TODO — nutzen vorerst die logischen Ops als Fallback
                        BinOp::BitAnd => self.rt.moo_bitand,
                        BinOp::BitOr => self.rt.moo_bitor,
                        BinOp::BitXor => self.rt.moo_bitxor,
                        BinOp::LShift => self.rt.moo_lshift,
                        BinOp::RShift => self.rt.moo_rshift,
                    };
                    let normal_result = self.call_rt(rt_func, &[lhs.into(), rhs.into()], "op_normal")?;
                    self.builder.build_store(result_ptr, normal_result).map_err(|e| format!("{e}"))?;
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
                        BinOp::RShift => self.rt.moo_rshift, // TODO: echte Bit-Ops
                    };
                    self.call_rt(func, &[lhs.into(), rhs.into()], "op")
                }
            }
            Expr::UnaryOp { op, operand } => {
                let val = self.compile_expr(operand)?;
                let func = match op {
                    UnaryOpKind::Neg => self.rt.moo_neg,
                    UnaryOpKind::Not => self.rt.moo_not,
                    UnaryOpKind::BitNot => self.rt.moo_bitnot,
                };
                self.call_rt(func, &[val.into()], "unary")
            }
            Expr::FunctionCall { name, args } => {
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
                    "länge" | "len" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_length, &[arg.into()], "len");
                    }
                    "text" | "str" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_to_string, &[arg.into()], "str");
                    }
                    "zahl" | "num" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_to_number, &[arg.into()], "to_num");
                        // TODO: echte moo_to_number Funktion
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
                    // Freeze/Immutable
                    "zeit" | "time" => {
                        return self.call_rt(self.rt.moo_time, &[], "time");
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
                    _ => {}
                }

                // Lambda-Lookup: wenn name ein Lambda-Alias ist, den echten Namen nutzen
                let actual_name = self.lambda_names.get(name)
                    .cloned()
                    .unwrap_or(name.clone());
                let function = self.module.get_function(&actual_name)
                    .ok_or_else(|| {
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
                        if suggestion.is_empty() {
                            format!("Funktion '{name}' nicht gefunden.")
                        } else {
                            format!("Funktion '{name}' nicht gefunden. Meintest du '{suggestion}'?")
                        }
                    })?;
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
                while compiled_args.len() < expected_params {
                    let none_val = self.call_rt(self.rt.moo_none, &[], "none_arg")?;
                    compiled_args.push(none_val.into());
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

                let obj = self.compile_expr(object)?;
                // Eingebaute Methoden
                match method.as_str() {
                    "append" | "hinzufügen" => {
                        let arg = self.compile_expr(&args[0])?;
                        self.call_rt_void(self.rt.moo_list_append,
                            &[obj.into(), arg.into()], "append")?;
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
                        return self.call_rt(self.rt.moo_list_join,
                            &[obj.into(), delim.into()], "join");
                    }
                    "contains" | "enthält" => {
                        let item = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_list_contains,
                            &[obj.into(), item.into()], "contains");
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
                    "split" | "teilen" => {
                        let delim = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_string_split,
                            &[obj.into(), delim.into()], "split");
                    }
                    "replace" | "ersetzen" => {
                        let old_s = self.compile_expr(&args[0])?;
                        let new_s = self.compile_expr(&args[1])?;
                        return self.call_rt(self.rt.moo_string_replace,
                            &[obj.into(), old_s.into(), new_s.into()], "replace");
                    }
                    "str_contains" | "text_enthält" => {
                        let needle = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_string_contains,
                            &[obj.into(), needle.into()], "str_contains");
                    }
                    "warten" | "wait" => {
                        return self.call_rt(self.rt.moo_thread_wait, &[obj.into()], "thread_wait");
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
                        self.call_rt_void(self.rt.moo_channel_close,
                            &[obj.into()], "chan_close")?;
                        return self.call_rt(self.rt.moo_none, &[], "none");
                    }
                    // Webserver-Methoden
                    "web_annehmen" | "web_accept" => {
                        return self.call_rt(self.rt.moo_web_accept, &[obj.into()], "web_accept");
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

                        // Apply lambda: store element in param var, compile body
                        if let Expr::Lambda { params, body } = lambda {
                            if let Some(param) = params.first() {
                                self.store_var(param, element)?;
                            }
                            let mapped_val = self.compile_expr(body)?;
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

                        // Apply lambda: store element in param var, compile body, check truthy
                        if let Expr::Lambda { params, body } = lambda {
                            if let Some(param) = params.first() {
                                self.store_var(param, element)?;
                            }
                            let test_val = self.compile_expr(body)?;
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
                        // Klassen-Methode aufrufen: suche nach class__method
                        // Wir muessen den Klassennamen herausfinden — schwierig zur Compile-Zeit
                        // Fuer jetzt: obj als erstes Argument + user-defined function suchen
                        let mut call_args: Vec<BasicMetadataValueEnum> = vec![obj.into()];
                        for a in args {
                            call_args.push(self.compile_expr(a)?.into());
                        }
                        // Versuche alle bekannten Klassen-Methoden
                        for ((_, mname), func) in &self.class_methods {
                            if mname == method {
                                return self.call_rt(*func, &call_args, "method_call");
                            }
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
                if let Expr::Range { start, end } = index.as_ref() {
                    let s = self.compile_expr(start)?;
                    let e = self.compile_expr(end)?;
                    self.call_rt(self.rt.moo_string_slice,
                        &[obj.into(), s.into(), e.into()], "str_slice")
                } else {
                    let idx = self.compile_expr(index)?;
                    self.call_rt(self.rt.moo_index_get,
                        &[obj.into(), idx.into()], "idx_get")
                }
            }
            Expr::Range { start, end } => {
                let s = self.compile_expr(start)?;
                let e = self.compile_expr(end)?;
                self.call_rt(self.rt.moo_range, &[s.into(), e.into()], "range")
            }
            Expr::List(elements) => {
                let list = self.call_rt(self.rt.moo_list_new,
                    &[self.context.i32_type().const_int(0, false).into()],
                    "list")?;
                let list_ptr = self.builder.build_alloca(self.mv_type(), "list_tmp")
                    .map_err(|e| format!("{e}"))?;
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
                let result_ptr = self.builder.build_alloca(self.mv_type(), "comp_list_ptr")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(result_ptr, result_list).map_err(|e| format!("{e}"))?;

                // Compile iterable and get length
                let list_val = self.compile_expr(iterable)?;
                let list_ptr = self.builder.build_alloca(self.mv_type(), "comp_iter_ptr")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(list_ptr, list_val).map_err(|e| format!("{e}"))?;

                let len_result = self.builder.build_call(self.rt.moo_list_iter_len,
                    &[list_val.into()], "len")
                    .map_err(|e| format!("{e}"))?;
                let len = match len_result.try_as_basic_value() {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("iter_len fehlgeschlagen".to_string()),
                };

                let i32_type = self.context.i32_type();
                let idx_ptr = self.builder.build_alloca(i32_type, "comp_idx")
                    .map_err(|e| format!("{e}"))?;
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
                let dict_ptr = self.builder.build_alloca(self.mv_type(), "dict_tmp")
                    .map_err(|e| format!("{e}"))?;
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
                self.builder.build_return(Some(&result)).map_err(|e| format!("{e}"))?;

                self.current_function = prev_fn;
                self.variables = prev_vars;
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }

                self.call_rt(self.rt.moo_none, &[], "lambda_placeholder")
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
                    Expr::MethodCall { object: _, method, args } => {
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
