//! P11-T27: CPU-Auswahl gilt nur innerhalb eines gebundenen, aufgeraeumten Bereichs.
use crate::ast::{Decorator, Stmt};
use inkwell::{AddressSpace, IntPredicate};
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::memory_buffer::MemoryBuffer;
use inkwell::module::{Linkage, Module};
use inkwell::values::{FunctionValue, IntValue, PointerValue};

const FEATURES: &str = "__moo_native.cpu_scope.features";
const ENTER: &str = "__moo_native.cpu_scope.enter";
const LEAVE: &str = "__moo_native.cpu_scope.leave";
const DETECT: &str = "__moo_native.cpu_scope.detect";
const ABORT: &str = "__moo_native.cpu_scope.abort";
pub(crate) const VALID: u64 = 256;
pub(crate) const SSE42: u64 = 512;
fn e(x: impl ToString) -> String { x.to_string() }

pub(crate) fn marker(decorators: &[Decorator]) -> Result<bool, String> {
    let matches: Vec<_> = decorators.iter().filter(|d| d.name == "cpu_bereich").collect();
    if matches.is_empty() { return Ok(false); }
    if matches.len() != 1 || !matches[0].args.is_empty() {
        return Err("@cpu_bereich erwartet keine Argumente und darf nur einmal vorkommen".into());
    }
    if decorators.iter().any(|d| matches!(d.name.as_str(), "simd" | "nackt" | "naked" | "unterbrechung" | "interrupt")) {
        return Err("@cpu_bereich braucht einen normalen nativen Koerper; geschuetzte @simd-Funktionen darin aufrufen".into());
    }
    Ok(true)
}

pub(crate) fn gebraucht(statements: &[Stmt]) -> bool {
    statements.iter().any(|s| matches!(s, Stmt::FunctionDef { decorators, .. }
        if decorators.iter().any(|d| d.name == "cpu_bereich")))
}

pub(crate) fn ist_helfer(f: FunctionValue<'_>) -> bool {
    matches!(f.get_name().to_str(), Ok(ENTER | LEAVE | DETECT | ABORT))
}

pub(crate) fn merkmale<'ctx>(c: &'ctx Context, b: &Builder<'ctx>, m: &Module<'ctx>) -> Result<IntValue<'ctx>, String> {
    match m.get_global(FEATURES) {
        Some(g) => Ok(b.build_load(c.i64_type(), g.as_pointer_value(), "cpu.bereich.merkmale").map_err(e)?.into_int_value()),
        None => Ok(c.i64_type().const_zero()),
    }
}

/// Nach Aufbau des Cleanup-Drains, vor den nativen Koerpern.
pub(crate) fn prepare<'ctx>(c: &'ctx Context, m: &Module<'ctx>, statements: &[Stmt]) -> Result<(), String> {
    if !gebraucht(statements) || m.get_function(ENTER).is_some() { return Ok(()); }
    for s in statements {
        if let Stmt::FunctionDef { decorators, .. } = s {
            if marker(decorators)? && !decorators.iter().any(|d| matches!(d.name.as_str(), "nativ" | "native")) {
                return Err("@cpu_bereich braucht @nativ".into());
            }
        }
    }
    let triple = m.get_triple();
    let linux = matches!(triple.as_str().to_str(), Ok("x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"));
    let p = c.ptr_type(AddressSpace::default());
    if !linux {
        // Ohne garantierten Bindungsanbieter wird nichts gecacht. Insbesondere
        // bleiben Bare-/UEFI-CPUID, XCR0 und XSAVE unveraendert aktiv.
        for (name, parameters) in [(ENTER, vec![p.into(), p.into(), p.into()]), (LEAVE, vec![p.into()])] {
            let f = m.add_function(name, c.void_type().fn_type(&parameters, false), Some(Linkage::Internal));
            let b = c.create_builder();
            b.position_at_end(c.append_basic_block(f, "entry"));
            b.build_return(None).map_err(e)?;
        }
        return Ok(());
    }
    let i32t = c.i32_type();
    let affinity = i32t.fn_type(&[i32t.into(), c.i64_type().into(), p.into()], false);
    let getcpu = i32t.fn_type(&[], false);
    for (name, signature) in [("sched_getaffinity", affinity), ("sched_setaffinity", affinity), ("sched_getcpu", getcpu)] {
        if let Some(f) = m.get_function(name) {
            if f.get_type() != signature || f.get_call_conventions() != 0 {
                return Err(format!("@cpu_bereich: extern C '{name}' hat eine unpassende Linux-ABI-Signatur"));
            }
        }
    }
    let mut ir = String::from(IR);
    ir.push('\0');
    let memory = MemoryBuffer::create_from_memory_range_copy(ir.as_bytes(), "moo-cpu-bereich");
    m.link_in_module(c.create_module_from_ir(memory).map_err(e)?).map_err(e)?;
    for name in [ENTER, LEAVE] {
        m.get_function(name).ok_or("CPU-Bereich-Helfer fehlt")?.set_linkage(Linkage::Internal);
    }
    let abort = m.add_function(ABORT, c.void_type().fn_type(&[], false), Some(Linkage::Internal));
    let b = c.create_builder();
    b.position_at_end(c.append_basic_block(abort, "entry"));
    crate::codegen_native::emit_abbruch(c, &b, m, "cpu_bereich", "Thread-Affinitaet konnte nicht wiederhergestellt werden")?;
    Ok(())
}

fn detect<'ctx>(c: &'ctx Context, m: &Module<'ctx>) -> Result<FunctionValue<'ctx>, String> {
    if let Some(f) = m.get_function(DETECT) { return Ok(f); }
    let f = m.add_function(DETECT, c.i64_type().fn_type(&[], false), Some(Linkage::Internal));
    crate::native_simd_dispatch::attribute(c, f, crate::native_simd_dispatch::Profil::Skalar);
    let b = c.create_builder();
    let entry = c.append_basic_block(f, "entry");
    let leaf = c.append_basic_block(f, "sse42");
    let no = c.append_basic_block(f, "ohne.sse42");
    b.position_at_end(entry);
    let profile = b.build_call(crate::native_simd_dispatch::cpu(c, m)?, &[], "profil").map_err(e)?
        .try_as_basic_value().basic().ok_or("CPU-Profil fehlt")?.into_int_value();
    let base = b.build_or(profile, c.i64_type().const_int(VALID, false), "bereich.gueltig").map_err(e)?;
    let max = crate::native_crypto::cpuid(c, &b, 0, 0)?;
    let exists = b.build_int_compare(IntPredicate::UGE, max, c.i32_type().const_int(1, false), "leaf1").map_err(e)?;
    b.build_conditional_branch(exists, leaf, no).map_err(e)?;
    b.position_at_end(leaf);
    let bits = crate::native_crypto::cpuid(c, &b, 1, 2)?;
    let bit = b.build_and(bits, c.i32_type().const_int(1 << 20, false), "sse42.bit").map_err(e)?;
    let bit = b.build_int_compare(IntPredicate::NE, bit, c.i32_type().const_zero(), "sse42.verfuegbar").map_err(e)?;
    let flag = b.build_select(bit, c.i64_type().const_int(SSE42, false), c.i64_type().const_zero(), "crc.freigabe").map_err(e)?.into_int_value();
    let flags = b.build_or(base, flag, "cpu.freigaben").map_err(e)?;
    b.build_return(Some(&flags)).map_err(e)?;
    b.position_at_end(no);
    b.build_return(Some(&base)).map_err(e)?;
    Ok(f)
}

pub(crate) fn begin<'ctx>(c: &'ctx Context, b: &Builder<'ctx>, m: &Module<'ctx>, storage: PointerValue<'ctx>) -> Result<FunctionValue<'ctx>, String> {
    let enter = m.get_function(ENTER).ok_or("CPU-Bereich wurde nicht vorbereitet")?;
    let leave = m.get_function(LEAVE).ok_or("CPU-Bereich-Cleanup fehlt")?;
    let (query, abort) = if m.get_global(FEATURES).is_some() {
        (detect(c, m)?.as_global_value().as_pointer_value(),
         m.get_function(ABORT).ok_or("CPU-Bereich-Abbruch fehlt")?.as_global_value().as_pointer_value())
    } else {
        let nil = c.ptr_type(AddressSpace::default()).const_null();
        (nil, nil)
    };
    b.build_call(enter, &[storage.into(), query.into(), abort.into()], "").map_err(e)?;
    Ok(leave)
}

const IR: &str = r#"
@__moo_native.cpu_scope.features = internal thread_local global i64 0, align 8
declare i32 @sched_getaffinity(i32, i64, ptr)
declare i32 @sched_setaffinity(i32, i64, ptr)
declare i32 @sched_getcpu()
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
define void @__moo_native.cpu_scope.enter(ptr %context, ptr %detect, ptr %abort) {
entry:
  %one = alloca [16 x i64], align 8
  %previous = load i64, ptr @__moo_native.cpu_scope.features, align 8
  store i64 %previous, ptr %context, align 8
  %pinned = getelementptr i64, ptr %context, i64 1
  store i64 0, ptr %pinned, align 8
  %abort_slot = getelementptr i64, ptr %context, i64 2
  store ptr %abort, ptr %abort_slot, align 8
  %active = icmp ne i64 %previous, 0
  br i1 %active, label %done, label %query
query:
  %old = getelementptr i64, ptr %context, i64 4
  %got = call i32 @sched_getaffinity(i32 0, i64 128, ptr %old)
  %available = icmp eq i32 %got, 0
  br i1 %available, label %cpu, label %done
cpu:
  %id = call i32 @sched_getcpu()
  %valid = icmp ult i32 %id, 1024
  br i1 %valid, label %allowed, label %done
allowed:
  %word = lshr i32 %id, 6
  %bit = and i32 %id, 63
  %shift = zext i32 %bit to i64
  %mask = shl i64 1, %shift
  %old_word = getelementptr i64, ptr %old, i32 %word
  %old_bits = load i64, ptr %old_word, align 8
  %included = and i64 %old_bits, %mask
  %was_allowed = icmp ne i64 %included, 0
  br i1 %was_allowed, label %bind, label %done
bind:
  call void @llvm.memset.p0.i64(ptr %one, i8 0, i64 128, i1 false)
  %one_word = getelementptr i64, ptr %one, i32 %word
  store i64 %mask, ptr %one_word, align 8
  %set = call i32 @sched_setaffinity(i32 0, i64 128, ptr %one)
  %bound = icmp eq i32 %set, 0
  br i1 %bound, label %select, label %done
select:
  store i64 1, ptr %pinned, align 8
  %features = call i64 %detect()
  store i64 %features, ptr @__moo_native.cpu_scope.features, align 8
  br label %done
done:
  ret void
}
define void @__moo_native.cpu_scope.leave(ptr %context) {
entry:
  %previous = load i64, ptr %context, align 8
  store i64 %previous, ptr @__moo_native.cpu_scope.features, align 8
  %pinned = getelementptr i64, ptr %context, i64 1
  %bound = load i64, ptr %pinned, align 8
  %restore = icmp ne i64 %bound, 0
  br i1 %restore, label %affinity, label %done
affinity:
  %old = getelementptr i64, ptr %context, i64 4
  %set = call i32 @sched_setaffinity(i32 0, i64 128, ptr %old)
  %ok = icmp eq i32 %set, 0
  br i1 %ok, label %done, label %failure
failure:
  %abort_slot = getelementptr i64, ptr %context, i64 2
  %abort = load ptr, ptr %abort_slot, align 8
  call void %abort()
  unreachable
done:
  ret void
}
"#;
