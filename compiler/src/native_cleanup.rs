//! P2-T126: native Cleanup-Frames liegen im aufrufenden Stack, nicht auf dem Heap.
//! Die Kette ist hosted thread-lokal. Bare verwendet wie die bestehende
//! Fehler-/Kernel-ABI eine Kette je gelinktem Ausfuehrungskontext.
use crate::ast::Stmt;
use inkwell::{AddressSpace, IntPredicate, ThreadLocalMode};
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::{Linkage, Module};
use inkwell::types::StructType;
use inkwell::values::{FunctionValue, GlobalValue, PointerValue};

const TOP: &str = "__moo_native.cleanup.top";
const DRAIN: &str = "__moo_native.cleanup.drain";

pub(crate) fn contains(body: &[Stmt]) -> bool {
    body.iter().any(|s| match s {
        Stmt::Defer(..) => true,
        Stmt::If { body, else_body, .. } => contains(body) || contains(else_body),
        Stmt::While { body, .. } | Stmt::For { body, .. }
        | Stmt::UnsafeBlock { body, .. } => contains(body),
        _ => false,
    })
}

pub(crate) fn enabled(module: &Module<'_>) -> bool { module.get_global(TOP).is_some() }

pub(crate) fn frame_type<'ctx>(context: &'ctx Context) -> StructType<'ctx> {
    let p = context.ptr_type(AddressSpace::default());
    context.struct_type(&[p.into(), p.into(), p.into()], false)
}

pub(crate) fn top<'ctx>(module: &Module<'ctx>) -> Result<GlobalValue<'ctx>, String> {
    module.get_global(TOP).ok_or("Nativer Cleanup-Kontext fehlt".into())
}

/// Erst vor der nativen Deklarations-/Emissionsrunde aktivieren. Module ohne
/// native aufraeumen-Anweisung erhalten weder Globals noch neue Fehlerpfade.
pub(crate) fn prepare<'ctx>(
    context: &'ctx Context, module: &Module<'ctx>, statements: &[Stmt], bare: bool,
) -> Result<(), String> {
    let needed = statements.iter().any(|s| matches!(s,
        Stmt::FunctionDef { decorators, body, .. }
        if decorators.iter().any(|d| matches!(d.name.as_str(), "nativ" | "native")) && contains(body)));
    if !needed || enabled(module) { return Ok(()); }
    let p = context.ptr_type(AddressSpace::default());
    let top = module.add_global(p, None, TOP);
    top.set_linkage(Linkage::Internal);
    top.set_initializer(&p.const_null());
    if !bare { top.set_thread_local_mode(Some(ThreadLocalMode::GeneralDynamicTLSModel)); }
    let f = module.add_function(DRAIN,
        context.void_type().fn_type(&[p.into()], false), Some(Linkage::Internal));
    let b = context.create_builder();
    let entry = context.append_basic_block(f, "entry");
    let test = context.append_basic_block(f, "test");
    let action = context.append_basic_block(f, "action");
    let done = context.append_basic_block(f, "done");
    b.position_at_end(entry);
    // MooValue hat zwei 64-bit-Worte. Nur Zeiger gehen ueber die C-Grenze;
    // damit bleibt der Error-Transfer auch auf Win64/Darwin ABI-neutral.
    let saved = if bare { None } else {
        let saved = b.build_alloca(context.i64_type().array_type(2), "first.error").map_err(e)?;
        b.build_call(error_helper(context, module, "moo_native_cleanup_enter")?,
            &[saved.into()], "").map_err(e)?;
        Some(saved)
    };
    b.build_unconditional_branch(test).map_err(e)?;
    b.position_at_end(test);
    let current = b.build_load(p, top.as_pointer_value(), "top").map_err(e)?.into_pointer_value();
    let end = f.get_first_param().ok_or("Cleanup-Marker fehlt")?.into_pointer_value();
    let equal = b.build_int_compare(IntPredicate::EQ, current, end, "done").map_err(e)?;
    b.build_conditional_branch(equal, done, action).map_err(e)?;
    b.position_at_end(action);
    let frame = frame_type(context);
    let field = |index| b.build_struct_gep(frame, current, index, "frame.field").map_err(e);
    let prev = b.build_load(p, field(0)?, "prev").map_err(e)?;
    let callback = b.build_load(p, field(1)?, "callback").map_err(e)?.into_pointer_value();
    let captures = b.build_load(p, field(2)?, "captures").map_err(e)?;
    // Vor dem Aufruf entfernen: ein Fehler im Cleanup darf dieselbe Aktion
    // nicht erneut ausfuehren und die restliche Kette nicht ueberspringen.
    b.build_store(top.as_pointer_value(), prev).map_err(e)?;
    b.build_indirect_call(context.void_type().fn_type(&[p.into()], false),
        callback, &[captures.into()], "").map_err(e)?;
    if let Some(saved) = saved {
        b.build_call(error_helper(context, module, "moo_native_cleanup_step")?,
            &[saved.into()], "").map_err(e)?;
    }
    b.build_unconditional_branch(test).map_err(e)?;
    b.position_at_end(done);
    if let Some(saved) = saved {
        b.build_call(error_helper(context, module, "moo_native_cleanup_leave")?,
            &[saved.into()], "").map_err(e)?;
    }
    b.build_return(None).map_err(e)?;
    Ok(())
}

/// Schwache No-op-Fassung fuer einzeln gelinkte native Objekte. Die starke
/// Hosted-Runtime bewahrt Handler-Tiefe und owning Fehlerwert. Bare ruft diese
/// Helfer nie auf und braucht keinerlei Moo-Runtime.
pub(crate) fn error_helper<'ctx>(
    context: &'ctx Context, module: &Module<'ctx>, name: &str,
) -> Result<FunctionValue<'ctx>, String> {
    if let Some(f) = module.get_function(name) { return Ok(f); }
    let p = context.ptr_type(AddressSpace::default());
    let f = module.add_function(name, context.void_type().fn_type(&[p.into()], false),
        Some(Linkage::WeakAny));
    let b = context.create_builder();
    b.position_at_end(context.append_basic_block(f, "entry"));
    b.build_return(None).map_err(e)?;
    Ok(f)
}

pub(crate) fn drain<'ctx>(
    builder: &Builder<'ctx>, module: &Module<'ctx>, marker: PointerValue<'ctx>,
) -> Result<(), String> {
    let f = module.get_function(DRAIN).ok_or("Nativer Cleanup-Drain fehlt")?;
    builder.build_call(f, &[marker.into()], "").map_err(e)?;
    Ok(())
}

/// Alle aktiven nativen Caller-Frames vor einem compiler-generierten
/// terminalen Fehler abarbeiten. CPU-Faults/fremdes abort sind kein Sprachfehler.
pub(crate) fn before_abort<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, module: &Module<'ctx>,
) -> Result<(), String> {
    if enabled(module) { drain(builder, module, context.ptr_type(AddressSpace::default()).const_null())?; }
    Ok(())
}

fn e(error: impl ToString) -> String { error.to_string() }
