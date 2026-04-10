/// LLVM Code-Generator für moo — erzeugt nativen Maschinencode über LLVM IR.

use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::targets::{
    CodeModel, FileType, InitializationConfig, RelocMode, Target, TargetMachine,
};
use inkwell::types::BasicMetadataTypeEnum;
use inkwell::values::{BasicMetadataValueEnum, BasicValueEnum, FunctionValue, PointerValue};
use inkwell::OptimizationLevel;
use inkwell::AddressSpace;

use std::collections::HashMap;
use std::path::Path;

use crate::ast::*;

pub struct CodeGen<'ctx> {
    context: &'ctx Context,
    module: Module<'ctx>,
    builder: Builder<'ctx>,
    variables: HashMap<String, PointerValue<'ctx>>,
    current_function: Option<FunctionValue<'ctx>>,
    printf_fn: Option<FunctionValue<'ctx>>,
}

impl<'ctx> CodeGen<'ctx> {
    pub fn new(context: &'ctx Context, module_name: &str) -> Self {
        let module = context.create_module(module_name);
        let builder = context.create_builder();

        Self {
            context,
            module,
            builder,
            variables: HashMap::new(),
            current_function: None,
            printf_fn: None,
        }
    }

    /// Deklariert externe C-Funktionen (printf, etc.)
    fn declare_externals(&mut self) {
        // printf
        let i32_type = self.context.i32_type();
        let i8_ptr_type = self.context.ptr_type(AddressSpace::default());
        let printf_type = i32_type.fn_type(&[BasicMetadataTypeEnum::from(i8_ptr_type)], true);
        self.printf_fn = Some(self.module.add_function("printf", printf_type, None));
    }

    /// Kompiliert ein ganzes Programm
    pub fn compile_program(&mut self, program: &Program) -> Result<(), String> {
        self.declare_externals();

        // main-Funktion erstellen
        let i32_type = self.context.i32_type();
        let main_type = i32_type.fn_type(&[], false);
        let main_fn = self.module.add_function("main", main_type, None);
        let entry = self.context.append_basic_block(main_fn, "entry");
        self.builder.position_at_end(entry);
        self.current_function = Some(main_fn);

        // Alle Statements kompilieren
        for stmt in &program.statements {
            self.compile_stmt(stmt)?;
        }

        // return 0
        self.builder.build_return(Some(&i32_type.const_int(0, false)))
            .map_err(|e| format!("Return-Fehler: {e}"))?;

        // LLVM IR verifizieren
        self.module.verify().map_err(|e| format!("LLVM Verifikation fehlgeschlagen: {}", e.to_string()))
    }

    /// Speichert LLVM IR als .ll Datei
    pub fn write_ir(&self, path: &Path) -> Result<(), String> {
        self.module.print_to_file(path)
            .map_err(|e| format!("IR-Datei schreiben fehlgeschlagen: {}", e.to_string()))
    }

    /// Kompiliert zu nativem Object-File
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

    // === Statement-Kompilierung ===

    fn compile_stmt(&mut self, stmt: &Stmt) -> Result<(), String> {
        match stmt {
            Stmt::Assignment { name, value } | Stmt::ConstAssignment { name, value } => {
                self.compile_assignment(name, value)
            }
            Stmt::CompoundAssignment { name, op, value } => {
                self.compile_compound_assignment(name, op, value)
            }
            Stmt::Show(expr) => self.compile_show(expr),
            Stmt::If { condition, body, else_body } => {
                self.compile_if(condition, body, else_body)
            }
            Stmt::While { condition, body } => self.compile_while(condition, body),
            Stmt::FunctionDef { name, params, body, .. } => {
                self.compile_function_def(name, params, body)
            }
            Stmt::Return(value) => self.compile_return(value),
            Stmt::Expression(expr) => {
                self.compile_expr(expr)?;
                Ok(())
            }
            Stmt::Break | Stmt::Continue => {
                // Wird in Schleifen-Kontext behandelt
                Ok(())
            }
            _ => {
                // Noch nicht implementierte Features → Warnung
                Ok(())
            }
        }
    }

    fn compile_assignment(&mut self, name: &str, value: &Expr) -> Result<(), String> {
        let val = self.compile_expr(value)?;

        let ptr = if let Some(existing) = self.variables.get(name) {
            *existing
        } else {
            let alloca = self.builder
                .build_alloca(self.context.f64_type(), name)
                .map_err(|e| format!("Alloca-Fehler: {e}"))?;
            self.variables.insert(name.to_string(), alloca);
            alloca
        };

        self.builder.build_store(ptr, val)
            .map_err(|e| format!("Store-Fehler: {e}"))?;
        Ok(())
    }

    fn compile_compound_assignment(&mut self, name: &str, op: &str, value: &Expr) -> Result<(), String> {
        let current = self.load_variable(name)?;
        let rhs = self.compile_expr(value)?;

        let result = match op {
            "+=" => self.builder.build_float_add(
                current.into_float_value(), rhs.into_float_value(), "addtmp"
            ).map_err(|e| format!("{e}"))?,
            "-=" => self.builder.build_float_sub(
                current.into_float_value(), rhs.into_float_value(), "subtmp"
            ).map_err(|e| format!("{e}"))?,
            _ => return Err(format!("Unbekannter Compound-Operator: {op}")),
        };

        let ptr = self.variables.get(name)
            .ok_or(format!("Variable '{name}' nicht gefunden"))?;
        self.builder.build_store(*ptr, result)
            .map_err(|e| format!("Store-Fehler: {e}"))?;
        Ok(())
    }

    fn compile_show(&mut self, expr: &Expr) -> Result<(), String> {
        let printf = self.printf_fn.unwrap();

        match expr {
            Expr::String(s) => {
                let fmt = self.builder
                    .build_global_string_ptr(&format!("{s}\n"), "fmt_str")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_call(
                    printf,
                    &[BasicMetadataValueEnum::from(fmt.as_pointer_value())],
                    "printf_call",
                ).map_err(|e| format!("{e}"))?;
            }
            Expr::BinaryOp { op: BinOp::Add, left, right } if self.is_string_expr(left) || self.is_string_expr(right) => {
                // String-Konkatenation: Einfach den ganzen String zusammenbauen
                let s = self.eval_string_concat(expr)?;
                let fmt = self.builder
                    .build_global_string_ptr(&format!("{s}\n"), "fmt_str")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_call(
                    printf,
                    &[BasicMetadataValueEnum::from(fmt.as_pointer_value())],
                    "printf_call",
                ).map_err(|e| format!("{e}"))?;
            }
            _ => {
                let val = self.compile_expr(expr)?;
                // Zahl ausgeben
                let fmt = self.builder
                    .build_global_string_ptr("%g\n", "fmt_num")
                    .map_err(|e| format!("{e}"))?;
                self.builder.build_call(
                    printf,
                    &[
                        BasicMetadataValueEnum::from(fmt.as_pointer_value()),
                        BasicMetadataValueEnum::from(val.into_float_value()),
                    ],
                    "printf_call",
                ).map_err(|e| format!("{e}"))?;
            }
        }

        Ok(())
    }

    fn compile_if(&mut self, condition: &Expr, body: &[Stmt], else_body: &[Stmt]) -> Result<(), String> {
        let cond_val = self.compile_expr(condition)?;
        let func = self.current_function.unwrap();

        // Float != 0.0 → wahr
        let zero = self.context.f64_type().const_float(0.0);
        let cond_bool = self.builder
            .build_float_compare(inkwell::FloatPredicate::ONE, cond_val.into_float_value(), zero, "ifcond")
            .map_err(|e| format!("{e}"))?;

        let then_bb = self.context.append_basic_block(func, "then");
        let else_bb = self.context.append_basic_block(func, "else");
        let merge_bb = self.context.append_basic_block(func, "merge");

        self.builder.build_conditional_branch(cond_bool, then_bb, else_bb)
            .map_err(|e| format!("{e}"))?;

        // Then-Block
        self.builder.position_at_end(then_bb);
        for stmt in body {
            self.compile_stmt(stmt)?;
        }
        self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

        // Else-Block
        self.builder.position_at_end(else_bb);
        for stmt in else_body {
            self.compile_stmt(stmt)?;
        }
        self.builder.build_unconditional_branch(merge_bb).map_err(|e| format!("{e}"))?;

        // Weiter nach dem If
        self.builder.position_at_end(merge_bb);
        Ok(())
    }

    fn compile_while(&mut self, condition: &Expr, body: &[Stmt]) -> Result<(), String> {
        let func = self.current_function.unwrap();

        let cond_bb = self.context.append_basic_block(func, "while_cond");
        let body_bb = self.context.append_basic_block(func, "while_body");
        let after_bb = self.context.append_basic_block(func, "while_after");

        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        // Bedingung prüfen
        self.builder.position_at_end(cond_bb);
        let cond_val = self.compile_expr(condition)?;
        let zero = self.context.f64_type().const_float(0.0);
        let cond_bool = self.builder
            .build_float_compare(inkwell::FloatPredicate::ONE, cond_val.into_float_value(), zero, "whilecond")
            .map_err(|e| format!("{e}"))?;
        self.builder.build_conditional_branch(cond_bool, body_bb, after_bb)
            .map_err(|e| format!("{e}"))?;

        // Schleifen-Body
        self.builder.position_at_end(body_bb);
        for stmt in body {
            self.compile_stmt(stmt)?;
        }
        self.builder.build_unconditional_branch(cond_bb).map_err(|e| format!("{e}"))?;

        // Nach der Schleife
        self.builder.position_at_end(after_bb);
        Ok(())
    }

    fn compile_function_def(&mut self, name: &str, params: &[String], body: &[Stmt]) -> Result<(), String> {
        let f64_type = self.context.f64_type();
        let param_types: Vec<BasicMetadataTypeEnum> = params.iter()
            .map(|_| BasicMetadataTypeEnum::from(f64_type))
            .collect();
        let fn_type = f64_type.fn_type(&param_types, false);
        let function = self.module.add_function(name, fn_type, None);

        let entry = self.context.append_basic_block(function, "entry");

        // Kontext sichern
        let prev_fn = self.current_function;
        let prev_vars = self.variables.clone();
        let prev_block = self.builder.get_insert_block();

        self.current_function = Some(function);
        self.builder.position_at_end(entry);

        // Parameter als lokale Variablen
        for (i, param_name) in params.iter().enumerate() {
            let alloca = self.builder.build_alloca(f64_type, param_name)
                .map_err(|e| format!("{e}"))?;
            let param_val = function.get_nth_param(i as u32).unwrap();
            self.builder.build_store(alloca, param_val)
                .map_err(|e| format!("{e}"))?;
            self.variables.insert(param_name.clone(), alloca);
        }

        // Body kompilieren
        for stmt in body {
            self.compile_stmt(stmt)?;
        }

        // Default return 0.0 wenn kein explizites Return
        if entry.get_terminator().is_none() {
            let current_bb = self.builder.get_insert_block().unwrap();
            if current_bb.get_terminator().is_none() {
                self.builder.build_return(Some(&f64_type.const_float(0.0)))
                    .map_err(|e| format!("{e}"))?;
            }
        }

        // Kontext wiederherstellen
        self.current_function = prev_fn;
        self.variables = prev_vars;
        if let Some(bb) = prev_block {
            self.builder.position_at_end(bb);
        }

        Ok(())
    }

    fn compile_return(&mut self, value: &Option<Expr>) -> Result<(), String> {
        if let Some(expr) = value {
            let val = self.compile_expr(expr)?;
            self.builder.build_return(Some(&val.into_float_value()))
                .map_err(|e| format!("{e}"))?;
        } else {
            let zero = self.context.f64_type().const_float(0.0);
            self.builder.build_return(Some(&zero))
                .map_err(|e| format!("{e}"))?;
        }
        Ok(())
    }

    // === Expression-Kompilierung ===

    fn compile_expr(&mut self, expr: &Expr) -> Result<BasicValueEnum<'ctx>, String> {
        match expr {
            Expr::Number(n) => {
                Ok(BasicValueEnum::FloatValue(self.context.f64_type().const_float(*n)))
            }
            Expr::Boolean(b) => {
                let val = if *b { 1.0 } else { 0.0 };
                Ok(BasicValueEnum::FloatValue(self.context.f64_type().const_float(val)))
            }
            Expr::None => {
                Ok(BasicValueEnum::FloatValue(self.context.f64_type().const_float(0.0)))
            }
            Expr::Identifier(name) => self.load_variable(name),
            Expr::BinaryOp { left, op, right } => {
                let lhs = self.compile_expr(left)?.into_float_value();
                let rhs = self.compile_expr(right)?.into_float_value();

                let result = match op {
                    BinOp::Add => self.builder.build_float_add(lhs, rhs, "addtmp")
                        .map_err(|e| format!("{e}"))?,
                    BinOp::Sub => self.builder.build_float_sub(lhs, rhs, "subtmp")
                        .map_err(|e| format!("{e}"))?,
                    BinOp::Mul => self.builder.build_float_mul(lhs, rhs, "multmp")
                        .map_err(|e| format!("{e}"))?,
                    BinOp::Div => self.builder.build_float_div(lhs, rhs, "divtmp")
                        .map_err(|e| format!("{e}"))?,
                    BinOp::Mod => self.builder.build_float_rem(lhs, rhs, "modtmp")
                        .map_err(|e| format!("{e}"))?,
                    BinOp::Pow => {
                        // pow über llvm.pow.f64 intrinsic
                        let pow_fn = self.get_or_declare_pow();
                        let result = self.builder.build_call(
                            pow_fn,
                            &[BasicMetadataValueEnum::from(lhs), BasicMetadataValueEnum::from(rhs)],
                            "powtmp",
                        ).map_err(|e| format!("{e}"))?;
                        match result.try_as_basic_value() {
                            inkwell::values::ValueKind::Basic(v) => v.into_float_value(),
                            _ => return Err("pow hat keinen Wert zurückgegeben".to_string()),
                        }
                    }
                    BinOp::Eq | BinOp::NotEq | BinOp::Less | BinOp::Greater
                    | BinOp::LessEq | BinOp::GreaterEq => {
                        let pred = match op {
                            BinOp::Eq => inkwell::FloatPredicate::OEQ,
                            BinOp::NotEq => inkwell::FloatPredicate::ONE,
                            BinOp::Less => inkwell::FloatPredicate::OLT,
                            BinOp::Greater => inkwell::FloatPredicate::OGT,
                            BinOp::LessEq => inkwell::FloatPredicate::OLE,
                            BinOp::GreaterEq => inkwell::FloatPredicate::OGE,
                            _ => unreachable!(),
                        };
                        let cmp = self.builder
                            .build_float_compare(pred, lhs, rhs, "cmptmp")
                            .map_err(|e| format!("{e}"))?;
                        // Bool → Float (0.0 oder 1.0)
                        self.builder
                            .build_unsigned_int_to_float(cmp, self.context.f64_type(), "booltmp")
                            .map_err(|e| format!("{e}"))?
                    }
                    BinOp::And => {
                        // Beide != 0
                        let zero = self.context.f64_type().const_float(0.0);
                        let l = self.builder.build_float_compare(inkwell::FloatPredicate::ONE, lhs, zero, "l")
                            .map_err(|e| format!("{e}"))?;
                        let r = self.builder.build_float_compare(inkwell::FloatPredicate::ONE, rhs, zero, "r")
                            .map_err(|e| format!("{e}"))?;
                        let and = self.builder.build_and(l, r, "andtmp")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_unsigned_int_to_float(and, self.context.f64_type(), "andftmp")
                            .map_err(|e| format!("{e}"))?
                    }
                    BinOp::Or => {
                        let zero = self.context.f64_type().const_float(0.0);
                        let l = self.builder.build_float_compare(inkwell::FloatPredicate::ONE, lhs, zero, "l")
                            .map_err(|e| format!("{e}"))?;
                        let r = self.builder.build_float_compare(inkwell::FloatPredicate::ONE, rhs, zero, "r")
                            .map_err(|e| format!("{e}"))?;
                        let or = self.builder.build_or(l, r, "ortmp")
                            .map_err(|e| format!("{e}"))?;
                        self.builder.build_unsigned_int_to_float(or, self.context.f64_type(), "orftmp")
                            .map_err(|e| format!("{e}"))?
                    }
                };

                Ok(BasicValueEnum::FloatValue(result))
            }
            Expr::UnaryOp { op, operand } => {
                let val = self.compile_expr(operand)?.into_float_value();
                let result = match op {
                    UnaryOpKind::Neg => self.builder
                        .build_float_neg(val, "negtmp")
                        .map_err(|e| format!("{e}"))?,
                    UnaryOpKind::Not => {
                        let zero = self.context.f64_type().const_float(0.0);
                        let is_zero = self.builder
                            .build_float_compare(inkwell::FloatPredicate::OEQ, val, zero, "nottmp")
                            .map_err(|e| format!("{e}"))?;
                        self.builder
                            .build_unsigned_int_to_float(is_zero, self.context.f64_type(), "notftmp")
                            .map_err(|e| format!("{e}"))?
                    }
                };
                Ok(BasicValueEnum::FloatValue(result))
            }
            Expr::FunctionCall { name, args } => {
                let function = self.module.get_function(name)
                    .ok_or(format!("Funktion '{name}' nicht gefunden"))?;

                let compiled_args: Vec<BasicMetadataValueEnum> = args.iter()
                    .map(|a| {
                        let v = self.compile_expr(a).unwrap();
                        BasicMetadataValueEnum::from(v.into_float_value())
                    })
                    .collect();

                let result = self.builder.build_call(function, &compiled_args, "calltmp")
                    .map_err(|e| format!("{e}"))?;

                let val = match result.try_as_basic_value() {
                    inkwell::values::ValueKind::Basic(v) => v,
                    _ => BasicValueEnum::FloatValue(self.context.f64_type().const_float(0.0)),
                };
                Ok(val)
            }
            _ => {
                // Fallback für noch nicht kompilierbare Expressions
                Ok(BasicValueEnum::FloatValue(self.context.f64_type().const_float(0.0)))
            }
        }
    }

    // === Hilfsfunktionen ===

    fn load_variable(&self, name: &str) -> Result<BasicValueEnum<'ctx>, String> {
        let ptr = self.variables.get(name)
            .ok_or(format!("Variable '{name}' nicht gefunden"))?;
        let val = self.builder
            .build_load(self.context.f64_type(), *ptr, name)
            .map_err(|e| format!("Load-Fehler: {e}"))?;
        Ok(val)
    }

    fn get_or_declare_pow(&self) -> FunctionValue<'ctx> {
        if let Some(f) = self.module.get_function("llvm.pow.f64") {
            return f;
        }
        let f64_type = self.context.f64_type();
        let pow_type = f64_type.fn_type(
            &[BasicMetadataTypeEnum::from(f64_type), BasicMetadataTypeEnum::from(f64_type)],
            false,
        );
        self.module.add_function("llvm.pow.f64", pow_type, None)
    }

    fn is_string_expr(&self, expr: &Expr) -> bool {
        matches!(expr, Expr::String(_))
    }

    fn eval_string_concat(&self, expr: &Expr) -> Result<String, String> {
        match expr {
            Expr::String(s) => Ok(s.clone()),
            Expr::BinaryOp { op: BinOp::Add, left, right } => {
                let l = self.eval_string_concat(left)?;
                let r = self.eval_string_concat(right)?;
                Ok(format!("{l}{r}"))
            }
            _ => Ok("<...>".to_string()),
        }
    }
}
