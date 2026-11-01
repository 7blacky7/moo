//! Compile-time Katalog der Defer-Quellstellen pro Aktivierung.
//! Die Adressen sind nur waehrend der Compilation Identitaeten; sie werden
//! weder dereferenziert noch in das erzeugte Programm geschrieben.
use std::collections::HashMap;
use crate::ast::{Expr, Stmt};
use inkwell::{AddressSpace, context::Context, module::Module, types::FunctionType, values::FunctionValue};

/// Skalare C-ABI des noch anzubindenden Defer-Runtimestapels.
/// Deklarationen entstehen erst beim expliziten Aufruf und sind typgeprueft.
#[allow(dead_code)]
pub(super) struct DeferAbi<'ctx> {
    pub(super) new: FunctionValue<'ctx>,
    pub(super) push: FunctionValue<'ctx>,
    pub(super) pop: FunctionValue<'ctx>,
    pub(super) free: FunctionValue<'ctx>,
    pub(super) oom_value: FunctionValue<'ctx>,
    pub(super) status_value: FunctionValue<'ctx>,
}

#[allow(dead_code)]
impl<'ctx> DeferAbi<'ctx> {
    pub(super) fn declare(context: &'ctx Context, module: &Module<'ctx>) -> Result<Self, String> {
        fn declare_one<'ctx>(
            module: &Module<'ctx>, name: &str, expected: FunctionType<'ctx>,
        ) -> Result<FunctionValue<'ctx>, String> {
            if let Some(existing) = module.get_function(name) {
                if existing.get_type() != expected {
                    return Err(format!("Inkompatible Defer-Runtime-Deklaration: {name}"));
                }
                Ok(existing)
            } else {
                Ok(module.add_function(name, expected, None))
            }
        }
        let ptr = context.ptr_type(AddressSpace::default());
        let i32_ty = context.i32_type();
        Ok(Self {
            new: declare_one(module, "moo_defer_stack_new", i32_ty.fn_type(&[ptr.into()], false))?,
            push: declare_one(module, "moo_defer_stack_push", i32_ty.fn_type(&[ptr.into(), i32_ty.into()], false))?,
            pop: declare_one(module, "moo_defer_stack_pop", i32_ty.fn_type(&[ptr.into(), ptr.into()], false))?,
            free: declare_one(module, "moo_defer_stack_free", context.void_type().fn_type(&[ptr.into()], false))?,
            oom_value: declare_one(module, "moo_defer_oom_value", i32_ty.fn_type(&[ptr.into()], false))?,
            status_value: declare_one(module, "moo_defer_status_value", i32_ty.fn_type(&[i32_ty.into(), ptr.into()], false))?,
        })
    }

    /// Rueckgabestatus wird dem aufrufenden Kontrollfluss zur Pruefung ueberlassen.
    fn status(
        builder: &inkwell::builder::Builder<'ctx>,
        function: FunctionValue<'ctx>,
        args: &[inkwell::values::BasicMetadataValueEnum<'ctx>],
        name: &str,
    ) -> Result<inkwell::values::IntValue<'ctx>, String> {
        let call = builder.build_call(function, args, name).map_err(|e| e.to_string())?;
        match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(inkwell::values::BasicValueEnum::IntValue(value)) => Ok(value),
            _ => Err(format!("Defer-Runtime liefert keinen Status: {name}")),
        }
    }

    pub(super) fn emit_new(&self, builder: &inkwell::builder::Builder<'ctx>, out: inkwell::values::PointerValue<'ctx>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        Self::status(builder, self.new, &[out.into()], "defer.stack.new")
    }

    pub(super) fn emit_push(&self, builder: &inkwell::builder::Builder<'ctx>, stack: inkwell::values::PointerValue<'ctx>, id: inkwell::values::IntValue<'ctx>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        if id.get_type().get_bit_width() != 32 {
            return Err("Defer-Aktions-ID muss i32 sein".into());
        }
        Self::status(builder, self.push, &[stack.into(), id.into()], "defer.stack.push")
    }

    /// Registriert eine ID; slot muss beim Eintritt null oder ein gueltiger
    /// Stack sein. Bei new-/push-Fehler bleibt die Behandlung beim Aufrufer.
    pub(super) fn emit_register(
        &self,
        context: &'ctx Context,
        builder: &inkwell::builder::Builder<'ctx>,
        function: FunctionValue<'ctx>,
        slot: inkwell::values::PointerValue<'ctx>,
        id: inkwell::values::IntValue<'ctx>,
    ) -> Result<inkwell::values::IntValue<'ctx>, String> {
        if id.get_type().get_bit_width() != 32 {
            return Err("Defer-Aktions-ID muss i32 sein".into());
        }
        let current = builder.get_insert_block().ok_or("Defer-Registrierung ohne Block")?;
        if current.get_parent() != Some(function) || current.get_terminator().is_some() {
            return Err("Defer-Registrierung ausserhalb eines offenen Funktionsblocks".into());
        }
        let ptr = context.ptr_type(AddressSpace::default());
        let stack = builder.build_load(ptr, slot, "defer.stack.current")
            .map_err(|e| e.to_string())?.into_pointer_value();
        let empty = builder.build_is_null(stack, "defer.stack.unallocated")
            .map_err(|e| e.to_string())?;
        let allocate = context.append_basic_block(function, "defer.stack.allocate");
        let ready = context.append_basic_block(function, "defer.stack.ready");
        let push = context.append_basic_block(function, "defer.stack.register");
        let failed = context.append_basic_block(function, "defer.stack.new_failed");
        let done = context.append_basic_block(function, "defer.stack.register_done");
        builder.build_conditional_branch(empty, allocate, ready).map_err(|e| e.to_string())?;
        builder.position_at_end(allocate);
        let new_status = self.emit_new(builder, slot)?;
        let created = builder.build_int_compare(
            inkwell::IntPredicate::EQ, new_status, context.i32_type().const_zero(), "defer.stack.created",
        ).map_err(|e| e.to_string())?;
        builder.build_conditional_branch(created, push, failed).map_err(|e| e.to_string())?;
        builder.position_at_end(ready);
        builder.build_unconditional_branch(push).map_err(|e| e.to_string())?;
        builder.position_at_end(failed);
        builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        builder.position_at_end(push);
        let stack = builder.build_load(ptr, slot, "defer.stack.for_push")
            .map_err(|e| e.to_string())?.into_pointer_value();
        let push_status = self.emit_push(builder, stack, id)?;
        builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        builder.position_at_end(done);
        let status = builder.build_phi(context.i32_type(), "defer.stack.status")
            .map_err(|e| e.to_string())?;
        status.add_incoming(&[(&new_status, failed), (&push_status, push)]);
        Ok(status.as_basic_value().into_int_value())
    }

    pub(super) fn emit_pop(&self, builder: &inkwell::builder::Builder<'ctx>, stack: inkwell::values::PointerValue<'ctx>, out: inkwell::values::PointerValue<'ctx>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        Self::status(builder, self.pop, &[stack.into(), out.into()], "defer.stack.pop")
    }

    /// Ein nie angelegter Stack entspricht einem leeren Stack; out bleibt
    /// dabei unveraendert. out muss ein gueltiger, ausgerichteter i32-Slot sein.
    /// Der Status muss vom Dispatcher ausgewertet werden.
    pub(super) fn emit_pop_or_empty(
        &self,
        context: &'ctx Context,
        builder: &inkwell::builder::Builder<'ctx>,
        function: FunctionValue<'ctx>,
        slot: inkwell::values::PointerValue<'ctx>,
        out: inkwell::values::PointerValue<'ctx>,
    ) -> Result<inkwell::values::IntValue<'ctx>, String> {
        let current = builder.get_insert_block().ok_or("Defer-Pop ohne Block")?;
        if current.get_parent() != Some(function) || current.get_terminator().is_some() {
            return Err("Defer-Pop ausserhalb eines offenen Funktionsblocks".into());
        }
        let ptr = context.ptr_type(AddressSpace::default());
        let stack = builder.build_load(ptr, slot, "defer.stack.for_pop")
            .map_err(|e| e.to_string())?.into_pointer_value();
        let empty = builder.build_is_null(stack, "defer.stack.is_null").map_err(|e| e.to_string())?;
        let empty_bb = context.append_basic_block(function, "defer.stack.empty");
        let pop_bb = context.append_basic_block(function, "defer.stack.pop");
        let done = context.append_basic_block(function, "defer.stack.pop_done");
        builder.build_conditional_branch(empty, empty_bb, pop_bb).map_err(|e| e.to_string())?;
        builder.position_at_end(empty_bb);
        // MOO_DEFER_EMPTY aus moo_defer_stack.h.
        let empty_status = context.i32_type().const_int(1, false);
        builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        builder.position_at_end(pop_bb);
        let pop_status = self.emit_pop(builder, stack, out)?;
        builder.build_unconditional_branch(done).map_err(|e| e.to_string())?;
        builder.position_at_end(done);
        let status = builder.build_phi(context.i32_type(), "defer.stack.pop_status")
            .map_err(|e| e.to_string())?;
        status.add_incoming(&[(&empty_status, empty_bb), (&pop_status, pop_bb)]);
        Ok(status.as_basic_value().into_int_value())
    }

    /// Nur beim tatsaechlichen Frame-Ende: freigeben und Slot entwerten.
    pub(super) fn emit_release_slot(
        &self,
        context: &'ctx Context,
        builder: &inkwell::builder::Builder<'ctx>,
        slot: inkwell::values::PointerValue<'ctx>,
    ) -> Result<(), String> {
        let ptr = context.ptr_type(AddressSpace::default());
        let stack = builder.build_load(ptr, slot, "defer.stack.for_free")
            .map_err(|e| e.to_string())?.into_pointer_value();
        self.emit_free(builder, stack)?;
        builder.build_store(slot, ptr.const_null()).map_err(|e| e.to_string())?;
        Ok(())
    }

    pub(super) fn emit_free(&self, builder: &inkwell::builder::Builder<'ctx>, stack: inkwell::values::PointerValue<'ctx>) -> Result<(), String> {
        builder.build_call(self.free, &[stack.into()], "").map_err(|e| e.to_string())?;
        Ok(())
    }

    pub(super) fn emit_oom_value(&self, builder: &inkwell::builder::Builder<'ctx>, out: inkwell::values::PointerValue<'ctx>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        Self::status(builder, self.oom_value, &[out.into()], "defer.oom.value")
    }

    /// out muss ein gueltiger MooValue-Slot ohne owning Inhalt sein.
    /// OK/EMPTY sind keine Fehlerwerte; der Caller prueft beide Statuswerte.
    pub(super) fn emit_status_value(&self, builder: &inkwell::builder::Builder<'ctx>, status: inkwell::values::IntValue<'ctx>, out: inkwell::values::PointerValue<'ctx>) -> Result<inkwell::values::IntValue<'ctx>, String> {
        if status.get_type().get_bit_width() != 32 {
            return Err("Defer-Status muss i32 sein".into());
        }
        Self::status(builder, self.status_value, &[status.into(), out.into()], "defer.status.value")
    }
}

#[derive(Default)]
pub(super) struct DeferCatalog {
    ids: HashMap<usize, u32>,
    actions: Vec<Expr>,
}

impl DeferCatalog {
    pub(super) fn collect(stmts: &[Stmt]) -> Result<Self, String> {
        let mut result = Self::default();
        result.visit(stmts)?;
        Ok(result)
    }

    pub(super) fn id_for(&self, expr: &Expr) -> Option<u32> {
        self.ids.get(&(expr as *const Expr as usize)).copied()
    }

    /// Die Position in dieser Liste ist die stabile Aktions-ID des Frames.
    pub(super) fn actions(&self) -> &[Expr] {
        &self.actions
    }

    pub(super) fn action(&self, id: u32) -> Option<&Expr> {
        self.actions().get(id as usize)
    }

    fn visit(&mut self, stmts: &[Stmt]) -> Result<(), String> {
        for stmt in stmts {
            match stmt {
                Stmt::Defer(expr) => {
                    let id = u32::try_from(self.actions.len())
                        .map_err(|_| "Zu viele Defer-Aktionsstellen in einer Funktion".to_string())?;
                    self.ids.insert(expr as *const Expr as usize, id);
                    self.actions.push(expr.clone());
                }
                Stmt::If { body, else_body, .. } => {
                    self.visit(body)?;
                    self.visit(else_body)?;
                }
                Stmt::While { body, .. } | Stmt::For { body, .. }
                | Stmt::ParallelFor { body, .. } | Stmt::UnsafeBlock { body } => self.visit(body)?,
                Stmt::Guard { else_body, .. } => self.visit(else_body)?,
                Stmt::TryCatch { try_body, catch_body, .. } => {
                    self.visit(try_body)?;
                    self.visit(catch_body)?;
                }
                Stmt::Match { cases, .. } => {
                    for (_, _, body) in cases { self.visit(body)?; }
                }
                // Eigene Aktivierungen werden an ihrem Erzeugungspunkt katalogisiert.
                Stmt::FunctionDef { .. } | Stmt::ClassDef { .. } | Stmt::TestDef { .. } => {}
                // Diese Anweisungen enthalten keine im aktuellen Frame ausgefuehrten
                // Statement-Bloecke; Export wird im aktuellen Codegen ignoriert.
                Stmt::Assignment { .. } | Stmt::ConstAssignment { .. }
                | Stmt::CompoundAssignment { .. } | Stmt::PropertyAssignment { .. }
                | Stmt::IndexAssignment { .. } | Stmt::Show(_)
                | Stmt::Precondition { .. } | Stmt::Postcondition { .. }
                | Stmt::ExternC { .. } | Stmt::RawDataU32 { .. }
                | Stmt::Return(_) | Stmt::Break | Stmt::Continue
                | Stmt::StructDef { .. } | Stmt::InterfaceDef { .. }
                | Stmt::Throw(_) | Stmt::Import { .. } | Stmt::Export(_)
                | Stmt::Global(_) | Stmt::Expression(_) | Stmt::DataClassDef { .. }
                | Stmt::Expect(_) => {}
            }
        }
        Ok(())
    }
}
