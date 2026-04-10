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

pub struct CodeGen<'ctx> {
    context: &'ctx Context,
    module: Module<'ctx>,
    builder: Builder<'ctx>,
    rt: RuntimeBindings<'ctx>,
    variables: HashMap<String, PointerValue<'ctx>>,
    current_function: Option<FunctionValue<'ctx>>,
    // Fuer break/continue
    loop_stack: Vec<(inkwell::basic_block::BasicBlock<'ctx>, inkwell::basic_block::BasicBlock<'ctx>)>,
    // Klassen-Konstruktoren: class_name -> FunctionValue
    class_constructors: HashMap<String, FunctionValue<'ctx>>,
    // Klassen-Methoden: (class_name, method_name) -> FunctionValue
    class_methods: HashMap<(String, String), FunctionValue<'ctx>>,
    // Lambda-Mapping: variable_name -> LLVM function name
    lambda_names: HashMap<String, String>,
    // Lambda-Counter
    lambda_counter: usize,
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
            current_function: None,
            loop_stack: Vec::new(),
            class_constructors: HashMap::new(),
            class_methods: HashMap::new(),
            lambda_names: HashMap::new(),
            lambda_counter: 0,
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

        for stmt in &program.statements {
            self.compile_stmt(stmt)?;
        }

        // Sicherstellen dass main terminiert ist
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_return(Some(&i32_type.const_int(0, false)))
                .map_err(|e| format!("Return-Fehler: {e}"))?;
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
                _ => {}
            }
        }
        Ok(())
    }

    pub fn write_ir(&self, path: &Path) -> Result<(), String> {
        self.module.print_to_file(path)
            .map_err(|e| format!("IR-Datei schreiben fehlgeschlagen: {}", e.to_string()))
    }

    pub fn write_object(&self, path: &Path) -> Result<(), String> {
        Target::initialize_native(&InitializationConfig::default())
            .map_err(|e| format!("Target-Init fehlgeschlagen: {e}"))?;

        let triple = TargetMachine::get_default_triple();
        let target = Target::from_triple(&triple)
            .map_err(|e| format!("Target-Fehler: {}", e.to_string()))?;

        let cpu = TargetMachine::get_host_cpu_name();
        let features = TargetMachine::get_host_cpu_features();

        let machine = target
            .create_target_machine(
                &triple,
                cpu.to_str().unwrap(),
                features.to_str().unwrap(),
                OptimizationLevel::Default,
                RelocMode::PIC,
                CodeModel::Default,
            )
            .ok_or("Konnte TargetMachine nicht erstellen")?;

        machine
            .write_to_file(&self.module, FileType::Object, path)
            .map_err(|e| format!("Object-File schreiben fehlgeschlagen: {}", e.to_string()))
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
            *existing
        } else {
            let alloca = self.builder.build_alloca(self.mv_type(), name)
                .map_err(|e| format!("{e}"))?;
            self.variables.insert(name.to_string(), alloca);
            alloca
        };
        self.builder.build_store(ptr, val).map_err(|e| format!("{e}"))?;
        Ok(())
    }

    fn load_var(&self, name: &str) -> Result<StructValue<'ctx>, String> {
        let ptr = self.variables.get(name)
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
                // Koennte Liste oder Dict sein — beides geht ueber list_set/dict_set
                // Wir verwenden list_set fuer Zahlen-Indices, dict_set fuer String-Indices
                // Einfachster Ansatz: immer dict_set (Strings) oder list_set
                // Da wir zur Compile-Zeit den Typ nicht kennen, nutzen wir eine Heuristik:
                // Index ist Number-Literal -> list_set, sonst dict_set
                match index {
                    Expr::Number(_) => {
                        self.call_rt_void(self.rt.moo_list_set,
                            &[obj.into(), idx.into(), val.into()], "list_set")
                    }
                    _ => {
                        self.call_rt_void(self.rt.moo_dict_set,
                            &[obj.into(), idx.into(), val.into()], "dict_set")
                    }
                }
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
            Stmt::FunctionDef { name, params, defaults, body } => {
                self.compile_function_def(name, params, defaults, body)
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
            Stmt::ClassDef { name, parent, body } => {
                self.compile_class_def(name, parent, body)
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
            Stmt::Expression(expr) => {
                self.compile_expr(expr)?;
                Ok(())
            }
            _ => Ok(()),
        }
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
            self.compile_stmt(stmt)?;
        }
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(else_bb);
        for stmt in else_body {
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

        // Body
        self.loop_stack.push((cond_bb, after_bb));
        self.builder.position_at_end(body_bb);

        // Element holen: list_iter_get(list, idx)
        let list_loaded = self.builder.build_load(self.mv_type(), list_ptr, "list")
            .map_err(|e| format!("{e}"))?.into_struct_value();
        let idx_loaded = self.builder.build_load(i32_type, idx_ptr, "idx")
            .map_err(|e| format!("{e}"))?.into_int_value();
        let element = self.call_rt(self.rt.moo_list_iter_get,
            &[list_loaded.into(), idx_loaded.into()], "elem")?;
        self.store_var(var_name, element)?;

        for stmt in body {
            self.compile_stmt(stmt)?;
        }

        // idx++
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            let idx_now = self.builder.build_load(i32_type, idx_ptr, "idx")
                .map_err(|e| format!("{e}"))?.into_int_value();
            let idx_next = self.builder.build_int_add(idx_now, i32_type.const_int(1, false), "idx_next")
                .map_err(|e| format!("{e}"))?;
            self.builder.build_store(idx_ptr, idx_next).map_err(|e| format!("{e}"))?;
            self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;
        }
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

        for stmt in body {
            self.compile_stmt(stmt)?;
        }

        // Default return none
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

        Ok(())
    }

    fn compile_return(&mut self, value: &Option<Expr>) -> Result<(), String> {
        let val = if let Some(expr) = value {
            self.compile_expr(expr)?
        } else {
            self.call_rt(self.rt.moo_none, &[], "none")?
        };
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

    fn compile_match(&mut self, value: &Expr, cases: &[(Option<Expr>, Vec<Stmt>)]) -> Result<(), String> {
        let func = self.current_function.unwrap();
        let val = self.compile_expr(value)?;
        let merge_bb = self.context.append_basic_block(func, "match_end");

        for (pattern, body) in cases {
            if let Some(pat_expr) = pattern {
                let pat_val = self.compile_expr(pat_expr)?;
                let eq = self.call_rt(self.rt.moo_eq, &[val.into(), pat_val.into()], "match_eq")?;
                let is_true_result = self.builder.build_call(self.rt.moo_is_truthy,
                    &[eq.into()], "truthy")
                    .map_err(|e| format!("{e}"))?;
                let is_true = match is_true_result.try_as_basic_value() {
                    inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
                    _ => return Err("truthy fehlgeschlagen".to_string()),
                };

                let case_bb = self.context.append_basic_block(func, "case");
                let next_bb = self.context.append_basic_block(func, "next_case");
                self.builder.build_conditional_branch(is_true, case_bb, next_bb)
                    .map_err(|e| format!("{e}"))?;

                self.builder.position_at_end(case_bb);
                for stmt in body {
                    self.compile_stmt(stmt)?;
                }
                if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
                    self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;
                }

                self.builder.position_at_end(next_bb);
            } else {
                // Default case
                for stmt in body {
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

        // moo_try_begin() returns 0 = normal, 1 = caught error
        let result = self.builder.build_call(self.rt.moo_try_begin, &[], "try_result")
            .map_err(|e| format!("{e}"))?;
        let try_val = match result.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("moo_try_begin fehlgeschlagen".to_string()),
        };

        let is_catch = self.builder.build_int_compare(
            inkwell::IntPredicate::NE,
            try_val,
            self.context.i32_type().const_int(0, false),
            "is_catch",
        ).map_err(|e| format!("{e}"))?;

        let try_bb = self.context.append_basic_block(func, "try_body");
        let catch_bb = self.context.append_basic_block(func, "catch_body");
        let after_bb = self.context.append_basic_block(func, "after_try");

        self.builder.build_conditional_branch(is_catch, catch_bb, try_bb)
            .map_err(|e| format!("{e}"))?;

        // Try-Block
        self.builder.position_at_end(try_bb);
        for s in try_body {
            self.compile_stmt(s)?;
        }
        // try_end() aufrufen wenn wir normal durchgekommen sind
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.call_rt_void(self.rt.moo_try_end, &[], "try_end")?;
            self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
        }

        // Catch-Block
        self.builder.position_at_end(catch_bb);
        if let Some(var_name) = catch_var {
            let error_val = self.call_rt(self.rt.moo_get_error, &[], "error")?;
            // Fehler zu String konvertieren fuer den User
            let error_str = self.call_rt(self.rt.moo_to_string, &[error_val.into()], "err_str")?;
            self.store_var(var_name, error_str)?;
        }
        for s in catch_body {
            self.compile_stmt(s)?;
        }
        if self.builder.get_insert_block().unwrap().get_terminator().is_none() {
            self.builder.build_unconditional_branch(after_bb).map_err(|e| format!("{e}"))?;
        }

        self.builder.position_at_end(after_bb);
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
                let lhs = self.compile_expr(left)?;
                let rhs = self.compile_expr(right)?;
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
                };
                self.call_rt(func, &[lhs.into(), rhs.into()], "op")
            }
            Expr::UnaryOp { op, operand } => {
                let val = self.compile_expr(operand)?;
                let func = match op {
                    UnaryOpKind::Neg => self.rt.moo_neg,
                    UnaryOpKind::Not => self.rt.moo_not,
                };
                self.call_rt(func, &[val.into()], "unary")
            }
            Expr::FunctionCall { name, args } => {
                // Builtin-Funktionen pruefen
                match name.as_str() {
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
                    "eingabe" | "input" => {
                        let arg = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_input, &[arg.into()], "input");
                    }
                    _ => {}
                }

                // Lambda-Lookup: wenn name ein Lambda-Alias ist, den echten Namen nutzen
                let actual_name = self.lambda_names.get(name)
                    .cloned()
                    .unwrap_or(name.clone());
                let function = self.module.get_function(&actual_name)
                    .ok_or(format!("Funktion '{name}' nicht gefunden"))?;
                let mut compiled_args: Vec<BasicMetadataValueEnum> = Vec::new();
                for a in args {
                    let v = self.compile_expr(a)?;
                    compiled_args.push(v.into());
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
                        return self.call_rt(self.rt.moo_string_length, &[obj.into()], "upper");
                        // TODO: moo_string_upper binding
                    }
                    "split" | "teilen" => {
                        let delim = self.compile_expr(&args[0])?;
                        return self.call_rt(self.rt.moo_string_index,
                            &[obj.into(), delim.into()], "split");
                        // TODO: moo_string_split binding
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
                        return Err(format!("Methode '{method}' nicht gefunden"));
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
            Expr::IndexAccess { object, index } => {
                let obj = self.compile_expr(object)?;
                let idx = self.compile_expr(index)?;
                // Koennte Liste oder Dict sein
                match index.as_ref() {
                    Expr::Number(_) => {
                        self.call_rt(self.rt.moo_list_get,
                            &[obj.into(), idx.into()], "idx_get")
                    }
                    _ => {
                        self.call_rt(self.rt.moo_dict_get,
                            &[obj.into(), idx.into()], "dict_get")
                    }
                }
            }
            Expr::List(elements) => {
                let count = elements.len() as i32;
                let list = self.call_rt(self.rt.moo_list_new,
                    &[self.context.i32_type().const_int(count as u64, false).into()],
                    "list")?;
                // Liste in Variable speichern damit wir sie fuer append verwenden koennen
                let list_ptr = self.builder.build_alloca(self.mv_type(), "list_tmp")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(list_ptr, list).map_err(|e| format!("{e}"))?;

                for elem in elements {
                    let val = self.compile_expr(elem)?;
                    let current_list = self.builder.build_load(self.mv_type(), list_ptr, "list")
                        .map_err(|e| format!("{e}"))?.into_struct_value();
                    self.call_rt_void(self.rt.moo_list_append,
                        &[current_list.into(), val.into()], "append")?;
                }

                let final_list = self.builder.build_load(self.mv_type(), list_ptr, "list")
                    .map_err(|e| format!("{e}"))?.into_struct_value();
                Ok(final_list)
            }
            Expr::Dict(pairs) => {
                let dict = self.call_rt(self.rt.moo_dict_new, &[], "dict")?;
                let dict_ptr = self.builder.build_alloca(self.mv_type(), "dict_tmp")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_store(dict_ptr, dict).map_err(|e| format!("{e}"))?;

                for (key, val) in pairs {
                    let k = self.compile_expr(key)?;
                    let v = self.compile_expr(val)?;
                    let current_dict = self.builder.build_load(self.mv_type(), dict_ptr, "dict")
                        .map_err(|e| format!("{e}"))?.into_struct_value();
                    self.call_rt_void(self.rt.moo_dict_set,
                        &[current_dict.into(), k.into(), v.into()], "dict_set")?;
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
                // Lambdas als benannte Funktionen kompilieren
                let lambda_name = format!("__lambda_{}", self.lambda_counter);
                self.lambda_counter += 1;
                let mv = self.mv_type();
                let param_types: Vec<BasicMetadataTypeEnum> = params.iter()
                    .map(|_| BasicMetadataTypeEnum::from(mv))
                    .collect();
                let fn_type = mv.fn_type(&param_types, false);
                let function = self.module.add_function(&lambda_name, fn_type, None);

                let entry = self.context.append_basic_block(function, "entry");
                let prev_fn = self.current_function;
                let prev_vars = self.variables.clone();
                let prev_block = self.builder.get_insert_block();

                self.current_function = Some(function);
                self.builder.position_at_end(entry);
                self.variables.clear();

                for (i, param_name) in params.iter().enumerate() {
                    let alloca = self.builder.build_alloca(mv, param_name)
                        .map_err(|e| format!("{e}"))?;
                    self.builder.build_store(alloca, function.get_nth_param(i as u32).unwrap())
                        .map_err(|e| format!("{e}"))?;
                    self.variables.insert(param_name.clone(), alloca);
                }

                let result = self.compile_expr(body)?;
                self.builder.build_return(Some(&result)).map_err(|e| format!("{e}"))?;

                self.current_function = prev_fn;
                self.variables = prev_vars;
                if let Some(bb) = prev_block {
                    self.builder.position_at_end(bb);
                }

                // Lambda als MooValue zurueckgeben - fuer jetzt einfach none
                // (Lambdas werden ueber ihren Namen aufgerufen)
                self.call_rt(self.rt.moo_none, &[], "lambda_placeholder")
            }
        }
    }
}
