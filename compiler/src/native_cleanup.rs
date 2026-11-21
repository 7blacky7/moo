//! P2-T126: native Cleanup-Frames liegen im aufrufenden Stack, nicht auf dem Heap.
//! Hosted liegt die Kette in TLS. Bare kann einen nativen Slottraeger
//! bereitstellen; ohne ihn bleibt der lokale Einzelkontext erhalten.
use crate::ast::Stmt;
use inkwell::{AddressSpace, IntPredicate, ThreadLocalMode};
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::{Linkage, Module};
use inkwell::types::StructType;
use inkwell::values::{FunctionValue, PointerValue};

const TOP: &str = "__moo_native.cleanup.top";
const DRAIN: &str = "__moo_native.cleanup.drain";
const CONTEXT: &str = "__moo_native.cleanup.context";
const PROVIDER: &str = "kern_native_cleanup_ort";

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

pub(crate) fn top<'ctx>(
    builder: &Builder<'ctx>, module: &Module<'ctx>,
) -> Result<PointerValue<'ctx>, String> {
    if let Some(f) = module.get_function(CONTEXT) {
        return builder.build_call(f, &[], "cleanup.context").map_err(e)?
            .try_as_basic_value().basic().map(|v| v.into_pointer_value())
            .ok_or("Nativer Cleanup-Slot fehlt".into());
    }
    module.get_global(TOP).map(|g| g.as_pointer_value())
        .ok_or("Nativer Cleanup-Kontext fehlt".into())
}

/// Nach den nativen Deklarationen binden, ohne C-Export oder MooValue-ABI.
/// Der Traeger darf weder Cleanup registrieren noch den Faden wechseln.
pub(crate) fn bind<'ctx>(
    context: &'ctx Context, module: &Module<'ctx>,
    native: &std::collections::HashMap<String, FunctionValue<'ctx>>,
) -> Result<(), String> {
    let Some(outer) = module.get_function(CONTEXT) else { return Ok(()); };
    let inner = *native.get(PROVIDER).ok_or("Nativer Cleanup-Traeger muss @nativ sein")?;
    if inner.get_type() != outer.get_type() || inner.get_call_conventions() != 0 {
        return Err("Nativer Cleanup-Traeger braucht () -> ptr[ptr[void]]".into());
    }
    let b = context.create_builder();
    b.position_at_end(context.append_basic_block(outer, "entry"));
    let slot = b.build_call(inner, &[], "slot").map_err(e)?
        .try_as_basic_value().basic().ok_or("Nativer Cleanup-Slot fehlt")?;
    b.build_return(Some(&slot)).map_err(e)?;
    Ok(())
}

/// Erst vor der nativen Deklarations-/Emissionsrunde aktivieren. Module ohne
/// native aufraeumen-Anweisung erhalten weder Globals noch neue Fehlerpfade.
pub(crate) fn prepare<'ctx>(
    context: &'ctx Context, module: &Module<'ctx>, statements: &[Stmt], bare: bool,
) -> Result<(), String> {
    let needed = statements.iter().any(|s| matches!(s,
        Stmt::FunctionDef { decorators, body, .. }
        if decorators.iter().any(|d| matches!(d.name.as_str(), "nativ" | "native")) && contains(body))) || crate::native_cpu_scope::gebraucht(statements);
    if !needed { return Ok(()); }
    if enabled(module) { return Ok(()); }
    let p = context.ptr_type(AddressSpace::default());
    let top = module.add_global(p, None, TOP);
    top.set_linkage(Linkage::Internal);
    top.set_initializer(&p.const_null());
    // --no-stdlib benutzt ebenfalls bare_mode, auch beim Linux-Test-/FFI-Modul.
    // Ein Linux-CPU-Bereich ist trotzdem threadlokal und braucht dieselbe
    // Isolation fuer seine Cleanup-Frames wie fuer den CPU-Merkmalsbeweis.
    let triple = module.get_triple();
    let linux_cpu_scope = crate::native_cpu_scope::gebraucht(statements)
        && matches!(triple.as_str().to_str(), Ok("x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"));
    if !bare || linux_cpu_scope {
        top.set_thread_local_mode(Some(ThreadLocalMode::GeneralDynamicTLSModel));
    }
    if bare {
        if let Some(Stmt::FunctionDef { param_types, return_type, decorators, body, .. }) =
            statements.iter().find(|s| matches!(s, Stmt::FunctionDef { name, .. } if name == PROVIDER))
        {
            // Opaque LLVM-Pointer ersetzen nicht den semantischen Pointee-Vertrag.
            if !param_types.is_empty() || !return_type.as_ref().is_some_and(slot_type)
                || !decorators.iter().any(|d| matches!(d.name.as_str(), "nativ" | "native"))
                || contains(body)
            {
                return Err("Nativer Cleanup-Traeger braucht @nativ () -> ptr[ptr[void]] ohne aufraeumen".into());
            }
            module.add_function(CONTEXT, p.fn_type(&[], false), Some(Linkage::Internal));
        }
    }
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
    let slot = self::top(&b, module)?;
    let current = b.build_load(p, slot, "top").map_err(e)?.into_pointer_value();
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
    b.build_store(slot, prev).map_err(e)?;
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

fn slot_type(ty: &crate::ast::TypeRef) -> bool {
    let mut inner = ty;
    for _ in 0..2 {
        match &inner.kind {
            crate::ast::TypeKind::Generic { name, args }
                if matches!(name.as_str(), "ptr" | "zeiger") && args.len() == 1 => inner = &args[0],
            _ => return false,
        }
    }
    matches!(&inner.kind, crate::ast::TypeKind::Name(n) if n == "void")
}

fn e(error: impl ToString) -> String { error.to_string() }
