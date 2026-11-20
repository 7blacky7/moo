//! P2-T125: eng begrenzte native Krypto-Primitive, ohne Runtime-/Provider-Ersatz.
//! Laengen und CPU-Merkmale sind oeffentlich. Geheimnisse beeinflussen weder
//! Schleifengrenzen noch Adressen; fluechtige Zugriffe und opake Register-ORs
//! verhindern Wegoptimieren bzw. Umformen in frueh abbrechende Vergleiche.
use inkwell::context::Context;
use inkwell::module::{Linkage, Module};
use inkwell::values::{BasicValue, FunctionValue, IntValue};
use inkwell::{AddressSpace, IntPredicate};

fn e(error: impl std::fmt::Display) -> String { error.to_string() }

pub(crate) fn wipe_name(name: &str) -> bool {
    matches!(name, "loeschen_sicher" | "secure_zero")
}

pub(crate) fn rng_name(name: &str) -> Option<(bool, bool)> {
    match name {
        "rdrand64" => Some((false, false)),
        "rdseed64" => Some((true, false)),
        "rdrand_verfuegbar" | "rdrand_available" => Some((false, true)),
        "rdseed_verfuegbar" | "rdseed_available" => Some((true, true)),
        _ => None,
    }
}

fn target<'a>(module: &'a Module<'_>) -> Result<bool, String> {
    let triple = module.get_triple();
    let triple = triple.as_str().to_str().map_err(e)?;
    if triple.starts_with("x86_64") { Ok(true) }
    else if triple.starts_with("aarch64") || triple.starts_with("arm64") { Ok(false) }
    else { Err("Konstantzeit-Primitive unterstuetzen x86_64 und AArch64".into()) }
}

/// Interner Helfer mit explizitem (ptr,len)-ABI, kein MooValue und keine FFI-Leihe.
pub(crate) fn bytes<'ctx>(context: &'ctx Context, module: &Module<'ctx>, wipe: bool) -> Result<FunctionValue<'ctx>, String> {
    let x86 = target(module)?;
    let name = if wipe { "__moo_secure_zero" } else { "__moo_ct_bytes" };
    if let Some(function) = module.get_function(name) { return Ok(function); }
    let i8t = context.i8_type();
    let i32t = context.i32_type();
    let i64t = context.i64_type();
    let ptr = context.ptr_type(AddressSpace::default());
    let params = if wipe { vec![ptr.into(), i64t.into()] }
        else { vec![ptr.into(), i64t.into(), ptr.into(), i64t.into()] };
    let ty = if wipe { context.void_type().fn_type(&params, false) }
        else { context.bool_type().fn_type(&params, false) };
    let f = module.add_function(name, ty, Some(Linkage::Internal));
    let b = context.create_builder();
    let entry = context.append_basic_block(f, "entry");
    let setup = context.append_basic_block(f, "public.lengths");
    let head = context.append_basic_block(f, "public.loop");
    let body = context.append_basic_block(f, "secret.bytes");
    let done = context.append_basic_block(f, "done");
    let a = f.get_nth_param(0).ok_or("Pufferzeiger fehlt")?.into_pointer_value();
    let len = f.get_nth_param(1).ok_or("Pufferlaenge fehlt")?.into_int_value();
    b.position_at_end(entry);
    let other = if wipe { None } else {
        let p = f.get_nth_param(2).ok_or("Vergleichszeiger fehlt")?.into_pointer_value();
        let n = f.get_nth_param(3).ok_or("Vergleichslaenge fehlt")?.into_int_value();
        let equal = b.build_int_compare(IntPredicate::EQ, len, n, "public.same_length").map_err(e)?;
        b.build_conditional_branch(equal, setup, done).map_err(e)?;
        Some(p)
    };
    if wipe { b.build_unconditional_branch(setup).map_err(e)?; }
    b.position_at_end(setup);
    b.build_unconditional_branch(head).map_err(e)?;
    b.position_at_end(head);
    let index = b.build_phi(i64t, "public.index").map_err(e)?;
    index.add_incoming(&[(&i64t.const_zero(), setup)]);
    let acc = if wipe { None } else {
        let phi = b.build_phi(i32t, "secret.accumulator").map_err(e)?;
        phi.add_incoming(&[(&i32t.const_zero(), setup)]);
        Some(phi)
    };
    let more = b.build_int_compare(IntPredicate::ULT, index.as_basic_value().into_int_value(), len, "public.more").map_err(e)?;
    b.build_conditional_branch(more, body, done).map_err(e)?;
    b.position_at_end(body);
    let p = unsafe { b.build_gep(i8t, a, &[index.as_basic_value().into_int_value()], "byte.a") }.map_err(e)?;
    if let Some(other) = other {
        let q = unsafe { b.build_gep(i8t, other, &[index.as_basic_value().into_int_value()], "byte.b") }.map_err(e)?;
        let av = b.build_load(i8t, p, "secret.a").map_err(e)?;
        let bv = b.build_load(i8t, q, "secret.b").map_err(e)?;
        for v in [av, bv] {
            let inst = v.as_instruction_value().ok_or("Byte-Load fehlt")?;
            inst.set_alignment(1).map_err(e)?;
            inst.set_volatile(true).map_err(e)?;
        }
        let xor = b.build_xor(av.into_int_value(), bv.into_int_value(), "secret.diff").map_err(e)?;
        let diff = b.build_int_z_extend(xor, i32t, "secret.ext").map_err(e)?;
        let aty = i32t.fn_type(&[i32t.into(), i32t.into()], false);
        let (code, constraints) = if x86 { ("or $2,$0", "=r,0,r,~{flags}") }
            else { ("orr $0,$1,$2", "=r,r,r") };
        let asm = context.create_inline_asm(aty, code.into(), constraints.into(), true, false, None, false);
        let value = b.build_indirect_call(aty, asm, &[acc.ok_or("Akkumulator fehlt")?.as_basic_value().into(), diff.into()], "secret.or").map_err(e)?
            .try_as_basic_value().basic().ok_or("Register-OR fehlt")?.into_int_value();
        acc.ok_or("Akkumulator fehlt")?.add_incoming(&[(&value, body)]);
    } else {
        let store = b.build_store(p, i8t.const_zero()).map_err(e)?;
        store.set_alignment(1).map_err(e)?;
        store.set_volatile(true).map_err(e)?;
    }
    let next = b.build_int_add(index.as_basic_value().into_int_value(), i64t.const_int(1, false), "public.next").map_err(e)?;
    index.add_incoming(&[(&next, body)]);
    b.build_unconditional_branch(head).map_err(e)?;
    b.position_at_end(done);
    if let Some(acc) = acc {
        // Die einzige payloadabhaengige Entscheidung ist ein Ergebnisbit, kein Sprung.
        // acc wird im Laengenfehlerpfad nicht dominiert: Ergebnis in head berechnen.
        let phi = b.build_phi(context.bool_type(), "result").map_err(e)?;
        // Vergleich in head einfuegen, bevor dessen Terminator steht.
        b.position_before(&head.get_terminator().ok_or("Schleifenterminator fehlt")?);
        let equal_head = b.build_int_compare(IntPredicate::EQ, acc.as_basic_value().into_int_value(), i32t.const_zero(), "secret.equal").map_err(e)?;
        phi.add_incoming(&[(&context.bool_type().const_zero(), entry), (&equal_head, head)]);
        b.position_at_end(done);
        b.build_return(Some(&phi.as_basic_value())).map_err(e)?;
    } else { b.build_return(None).map_err(e)?; }
    Ok(f)
}

pub(crate) fn cpuid<'ctx>(context: &'ctx Context, b: &inkwell::builder::Builder<'ctx>, leaf: u64, reg: u32) -> Result<IntValue<'ctx>, String> {
    let t = context.i32_type();
    let result = context.struct_type(&[t.into(), t.into(), t.into(), t.into()], false);
    let ty = result.fn_type(&[t.into(), t.into()], false);
    let asm = context.create_inline_asm(ty, "cpuid".into(),
        "={ax},={bx},={cx},={dx},0,2,~{flags}".into(), true, false, None, false);
    let call = b.build_indirect_call(ty, asm, &[t.const_int(leaf, false).into(), t.const_zero().into()], "public.cpuid").map_err(e)?;
    let value = call.try_as_basic_value().basic().ok_or("CPUID-Ergebnis fehlt")?.into_struct_value();
    Ok(b.build_extract_value(value, reg, "public.feature").map_err(e)?.into_int_value())
}

/// Genau EIN Instruktionsversuch. CPUID sperrt #UD, CF=false veraendert das Ziel
/// nicht; weder Retry noch DRBG/Entropieversprechen sind in diesem Primitive versteckt.
pub(crate) fn rng<'ctx>(context: &'ctx Context, module: &Module<'ctx>, seed: bool, feature_only: bool) -> Result<FunctionValue<'ctx>, String> {
    if !target(module)? { return Err("RDRAND/RDSEED benoetigen ein x86_64-Ziel".into()); }
    let name = format!("__moo_{}_{}", if seed { "rdseed" } else { "rdrand" }, if feature_only { "available" } else { "step64" });
    if let Some(f) = module.get_function(&name) { return Ok(f); }
    let params = if feature_only { vec![] } else { vec![context.ptr_type(AddressSpace::default()).into()] };
    let f = module.add_function(&name, context.bool_type().fn_type(&params, false), Some(Linkage::Internal));
    let b = context.create_builder();
    let entry = context.append_basic_block(f, "entry");
    let features = context.append_basic_block(f, "public.features");
    let supported = context.append_basic_block(f, "public.supported");
    let valid = context.append_basic_block(f, "cf.valid");
    let no = context.append_basic_block(f, "unavailable");
    b.position_at_end(entry);
    let max = cpuid(context, &b, 0, 0)?;
    let leaf = if seed { 7 } else { 1 };
    let exists = b.build_int_compare(IntPredicate::UGE, max, context.i32_type().const_int(leaf, false), "public.leaf_exists").map_err(e)?;
    b.build_conditional_branch(exists, features, no).map_err(e)?;
    b.position_at_end(features);
    let bits = cpuid(context, &b, leaf, if seed { 1 } else { 2 })?;
    let bit = b.build_and(bits, context.i32_type().const_int(1 << if seed { 18 } else { 30 }, false), "public.feature_bit").map_err(e)?;
    let has = b.build_int_compare(IntPredicate::NE, bit, context.i32_type().const_zero(), "public.has_feature").map_err(e)?;
    b.build_conditional_branch(has, supported, no).map_err(e)?;
    b.position_at_end(supported);
    if feature_only {
        b.build_return(Some(&context.bool_type().const_int(1, false))).map_err(e)?;
        b.position_at_end(valid);
        b.build_unreachable().map_err(e)?;
    } else {
        // Kein globales +rdrnd/+rdseed: auch auf CPUs ohne Merkmal muss das
        // Modul ausfuehrbar bleiben. Der oeffentliche CPUID-Guard schuetzt die
        // eng begrenzte Instruktion; SETC liefert den tatsaechlichen CF.
        let result = context.struct_type(&[context.i64_type().into(), context.i8_type().into()], false);
        let ty = result.fn_type(&[], false);
        let code = format!("{} $0\nsetc $1", if seed { "rdseed" } else { "rdrand" });
        let asm = context.create_inline_asm(ty, code, "=&r,=q,~{flags}".into(), true, false, None, false);
        let pair = b.build_indirect_call(ty, asm, &[], "rng.attempt").map_err(e)?
            .try_as_basic_value().basic().ok_or("RNG-Ergebnis fehlt")?.into_struct_value();
        let word = b.build_extract_value(pair, 0, "rng.word").map_err(e)?.into_int_value();
        let cf = b.build_extract_value(pair, 1, "rng.cf").map_err(e)?.into_int_value();
        let ok = b.build_int_compare(IntPredicate::NE, cf, context.i8_type().const_zero(), "rng.valid").map_err(e)?;
        b.build_conditional_branch(ok, valid, no).map_err(e)?;
        b.position_at_end(valid);
        let p = f.get_nth_param(0).ok_or("RNG-Ziel fehlt")?.into_pointer_value();
        b.build_store(p, word).map_err(e)?.set_alignment(1).map_err(e)?;
        b.build_return(Some(&context.bool_type().const_int(1, false))).map_err(e)?;
    }
    b.position_at_end(no);
    b.build_return(Some(&context.bool_type().const_zero())).map_err(e)?;
    Ok(f)
}
