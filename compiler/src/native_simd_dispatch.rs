//! P11-T19: x86-64-Varianten mit GPR-/Speicher-ABI vor der ISA-Grenze.
//! Der Wrapper bleibt ohne FP/SIMD und wird nicht in seine Aufrufer eingebettet.
use crate::ast::{Decorator, DecoratorArg};
use inkwell::attributes::{Attribute, AttributeLoc};
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::{Linkage, Module};
use inkwell::types::BasicType;
use inkwell::values::{BasicMetadataValueEnum, BasicValue, FunctionValue, IntValue, PointerValue};
use inkwell::{AddressSpace, IntPredicate};

fn e(x: impl ToString) -> String { x.to_string() }

/// Native ISA-Körper und ihre Cleanup-Thunks gehören auch hosted zum O2-Pfad.
pub(crate) fn ist_helfer(f: FunctionValue<'_>) -> bool {
    let name = f.get_name().to_string_lossy();
    name.starts_with("__moo_native.__simd.")
        || matches!(name.as_ref(), "__moo_simd_cpu" | "__moo_simd_zustand")
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Profil { Skalar, Ssse3, Avx2, Gfni128, Gfni256 }
impl Profil {
    pub(crate) fn name(self) -> &'static str {
        match self { Self::Skalar => "skalar", Self::Ssse3 => "ssse3", Self::Avx2 => "avx2",
            Self::Gfni128 => "gfni128", Self::Gfni256 => "gfni256" }
    }
    fn id(self) -> u64 { match self { Self::Skalar=>0, Self::Ssse3=>1, Self::Avx2=>2, Self::Gfni128=>3, Self::Gfni256=>4 } }
    pub(crate) fn avx(self) -> bool { matches!(self, Self::Avx2 | Self::Gfni256) }
    pub(crate) fn gfni(self) -> bool { matches!(self, Self::Gfni128 | Self::Gfni256) }
    pub(crate) fn von_funktion(f: FunctionValue<'_>) -> Self {
        let n = f.get_name().to_string_lossy();
        for p in [Self::Gfni256, Self::Gfni128, Self::Avx2, Self::Ssse3] {
            if n.starts_with(&format!("__moo_native.__simd.{}.", p.name())) { return p; }
        }
        Self::Skalar
    }
}

/// Ein bereits gewähltes Profil darf denselben geschützten Unterhelfer direkt
/// aufrufen. Bei fehlender Variante (z. B. engerem ISA-Deckel) bleibt der Guard.
pub(crate) fn aufruf_ziel<'ctx>(
    module: &Module<'ctx>, caller: FunctionValue<'ctx>, callee: FunctionValue<'ctx>,
) -> FunctionValue<'ctx> {
    let profile = Profil::von_funktion(caller);
    let caller_name = caller.get_name().to_string_lossy();
    if !caller_name.starts_with(&format!("__moo_native.__simd.{}.", profile.name())) {
        return callee;
    }
    let name = callee.get_name().to_string_lossy();
    let source = name.strip_prefix("__moo_native.").unwrap_or(&name);
    let variant = format!("__moo_native.__simd.{}.{source}", profile.name());
    module.get_function(&variant)
        .filter(|f| f.get_type() == callee.get_type()
            && f.get_call_conventions() == callee.get_call_conventions())
        .unwrap_or(callee)
}

/// Optionaler ISA-Deckel ist eine Funktionsanforderung, keine CPUID-Umgehung.
/// Auch @simd("gfni") fällt auf einer CPU ohne GFNI sicher zurück.
pub(crate) fn marker(decorators: &[Decorator]) -> Result<Option<u64>, String> {
    let found: Vec<_> = decorators.iter().filter(|d| d.name == "simd").collect();
    let d = match found.as_slice() {
        [] => return Ok(None), [d] => d, _ => return Err("@simd ist doppelt angegeben".into()),
    };
    let limit = match d.args.as_slice() {
        [] => 4,
        [DecoratorArg::Text(s)] => match s.as_str() {
            "skalar" => 0, "ssse3" => 1, "avx2" => 2, "gfni128" => 3, "gfni" => 4,
            _ => return Err("@simd erwartet optional skalar, ssse3, avx2, gfni128 oder gfni".into()),
        },
        _ => return Err("@simd erwartet höchstens ein Textargument".into()),
    };
    if decorators.iter().any(|d| matches!(d.name.as_str(), "nackt" | "naked" | "unterbrechung" | "interrupt")) {
        return Err("@simd benötigt einen normalen nativen Funktionskörper; der IRQ-/Nackt-Rahmen ruft ihn auf".into());
    }
    Ok(Some(limit))
}

pub(crate) fn attribute<'ctx>(context: &'ctx Context, f: FunctionValue<'ctx>, p: Profil) {
    let prefix = "-mmx,-avx512f,-avx512vl,-avx512bw,-avx512dq,-avx512vbmi,-avx512vbmi2";
    let features = match p {
        Profil::Skalar => "-sse,-sse2,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2,-gfni,-x87",
        Profil::Ssse3 => "+sse,+sse2,+sse3,+ssse3,-sse4.1,-sse4.2,-avx,-avx2,-gfni,-x87",
        Profil::Avx2 => "+sse,+sse2,+sse3,+ssse3,+sse4.1,+sse4.2,+avx,+avx2,-gfni,-x87",
        Profil::Gfni128 => "+sse,+sse2,+sse3,+ssse3,-sse4.1,-sse4.2,-avx,-avx2,+gfni,-x87",
        Profil::Gfni256 => "+sse,+sse2,+sse3,+ssse3,+sse4.1,+sse4.2,+avx,+avx2,+gfni,-x87",
    };
    for (name, value) in [("target-cpu", "x86-64"), ("target-features", &format!("{prefix},{features}"))] {
        f.add_attribute(AttributeLoc::Function, context.create_string_attribute(name, value));
    }
    f.add_attribute(AttributeLoc::Function,
        context.create_enum_attribute(Attribute::get_named_enum_kind_id("noinline"), 0));
}

fn bit<'ctx>(_c: &'ctx Context, b: &Builder<'ctx>, value: IntValue<'ctx>, mask: u64) -> Result<IntValue<'ctx>, String> {
    let masked = b.build_and(value, value.get_type().const_int(mask, false), "simd.cpu.bit").map_err(e)?;
    b.build_int_compare(IntPredicate::NE, masked, value.get_type().const_zero(), "simd.cpu.hat").map_err(e)
}

fn xcr0<'ctx>(c: &'ctx Context, b: &Builder<'ctx>) -> Result<IntValue<'ctx>, String> {
    let t = c.i32_type();
    let pair = c.struct_type(&[t.into(), t.into()], false);
    let ty = pair.fn_type(&[t.into()], false);
    let asm = c.create_inline_asm(ty, "xgetbv".into(), "={ax},={dx},{cx},~{flags}".into(), true, false, None, false);
    let result = b.build_indirect_call(ty, asm, &[t.const_zero().into()], "simd.xcr0").map_err(e)?
        .try_as_basic_value().basic().ok_or("XGETBV-Ergebnis fehlt")?.into_struct_value();
    let lo = b.build_extract_value(result, 0, "simd.xcr0.low").map_err(e)?.into_int_value();
    let hi = b.build_extract_value(result, 1, "simd.xcr0.high").map_err(e)?.into_int_value();
    let lo = b.build_int_z_extend(lo, c.i64_type(), "simd.xcr0.lo64").map_err(e)?;
    let hi = b.build_int_z_extend(hi, c.i64_type(), "simd.xcr0.hi64").map_err(e)?;
    let hi = b.build_left_shift(hi, c.i64_type().const_int(32, false), "simd.xcr0.shift").map_err(e)?;
    b.build_or(lo, hi, "simd.xcr0.maske").map_err(e)
}

fn cpu<'ctx>(c: &'ctx Context, m: &Module<'ctx>) -> Result<FunctionValue<'ctx>, String> {
    let name = "__moo_simd_cpu";
    if let Some(f) = m.get_function(name) { return Ok(f); }
    let f = m.add_function(name, c.i64_type().fn_type(&[], false), Some(Linkage::Internal));
    attribute(c, f, Profil::Skalar);
    let b = c.create_builder();
    let entry = c.append_basic_block(f, "entry");
    let extended = c.append_basic_block(f, "leaf7");
    let avx = c.append_basic_block(f, "avx.os");
    let choose = c.append_basic_block(f, "auswahl");
    let fallback = c.append_basic_block(f, "skalar");
    b.position_at_end(entry);
    let max = crate::native_crypto::cpuid(c, &b, 0, 0)?;
    let has1 = b.build_int_compare(IntPredicate::UGE, max, c.i32_type().const_int(1, false), "simd.leaf1").map_err(e)?;
    let leaf1 = c.append_basic_block(f, "leaf1");
    b.build_conditional_branch(has1, leaf1, fallback).map_err(e)?;
    b.position_at_end(leaf1);
    let flags = crate::native_crypto::cpuid(c, &b, 1, 2)?;
    let ssse3 = bit(c, &b, flags, 1 << 9)?;
    let has7 = b.build_int_compare(IntPredicate::UGE, max, c.i32_type().const_int(7, false), "simd.leaf7").map_err(e)?;
    b.build_conditional_branch(has7, extended, choose).map_err(e)?;
    b.position_at_end(extended);
    let ebx = crate::native_crypto::cpuid(c, &b, 7, 1)?;
    let ecx = crate::native_crypto::cpuid(c, &b, 7, 2)?;
    let gfni = bit(c, &b, ecx, 1 << 8)?;
    let avx2 = bit(c, &b, ebx, 1 << 5)?;
    let osmask = (1 << 26) | (1 << 27) | (1 << 28);
    let osbits = b.build_and(flags, c.i32_type().const_int(osmask, false), "simd.os.bits").map_err(e)?;
    let os = b.build_int_compare(IntPredicate::EQ, osbits, c.i32_type().const_int(osmask, false), "simd.os.avx").map_err(e)?;
    let possible = b.build_and(os, avx2, "simd.avx.moeglich").map_err(e)?;
    b.build_conditional_branch(possible, avx, choose).map_err(e)?;
    b.position_at_end(avx);
    let enabled = xcr0(c, &b)?;
    let enabled = b.build_and(enabled, c.i64_type().const_int(7, false), "simd.os.xcr0").map_err(e)?;
    let enabled = b.build_int_compare(IntPredicate::EQ, enabled, c.i64_type().const_int(7, false), "simd.os.ymm").map_err(e)?;
    b.build_unconditional_branch(choose).map_err(e)?;
    b.position_at_end(choose);
    let has_gfni = b.build_phi(c.bool_type(), "simd.gfni").map_err(e)?;
    has_gfni.add_incoming(&[(&c.bool_type().const_zero(), leaf1), (&gfni, extended), (&gfni, avx)]);
    let has_avx = b.build_phi(c.bool_type(), "simd.avx2").map_err(e)?;
    has_avx.add_incoming(&[(&c.bool_type().const_zero(), leaf1), (&c.bool_type().const_zero(), extended), (&enabled, avx)]);
    let i64t = c.i64_type();
    let normal = b.build_select(has_avx.as_basic_value().into_int_value(), i64t.const_int(2, false), i64t.const_int(1, false), "simd.normal").map_err(e)?.into_int_value();
    let gf = b.build_select(has_avx.as_basic_value().into_int_value(), i64t.const_int(4, false), i64t.const_int(3, false), "simd.gf").map_err(e)?.into_int_value();
    let selected = b.build_select(has_gfni.as_basic_value().into_int_value(), gf, normal, "simd.profil").map_err(e)?.into_int_value();
    let selected = b.build_select(ssse3, selected, i64t.const_zero(), "simd.ssse3.freigabe").map_err(e)?;
    b.build_return(Some(&selected)).map_err(e)?;
    b.position_at_end(fallback);
    b.build_return(Some(&i64t.const_zero())).map_err(e)?;
    Ok(f)
}

/// Liefert Größe und XCR0-Maske. CPUID.0D:EBX umfasst ALLE aktivierten
/// Komponenten: auch Firmware mit AVX-512 darf den lokalen Puffer nicht überlaufen.
fn zustand_info<'ctx>(c: &'ctx Context, m: &Module<'ctx>) -> Result<FunctionValue<'ctx>, String> {
    let name = "__moo_simd_zustand";
    if let Some(f) = m.get_function(name) { return Ok(f); }
    let t = c.i64_type();
    let pair = c.struct_type(&[t.into(), t.into()], false);
    let f = m.add_function(name, pair.fn_type(&[], false), Some(Linkage::Internal));
    attribute(c, f, Profil::Skalar);
    let b = c.create_builder();
    let entry = c.append_basic_block(f, "entry");
    let xsave = c.append_basic_block(f, "xsave");
    let fxsave = c.append_basic_block(f, "fxsave");
    b.position_at_end(entry);
    let flags = crate::native_crypto::cpuid(c, &b, 1, 2)?;
    let os = bit(c, &b, flags, 1 << 27)?;
    b.build_conditional_branch(os, xsave, fxsave).map_err(e)?;
    b.position_at_end(xsave);
    let mask = xcr0(c, &b)?;
    let size = crate::native_crypto::cpuid(c, &b, 13, 1)?;
    let size = b.build_int_z_extend(size, t, "simd.zustand.bytes").map_err(e)?;
    let a = b.build_insert_value(pair.get_undef(), size, 0, "simd.zustand.groesse").map_err(e)?;
    let a = b.build_insert_value(a, mask, 1, "simd.zustand.info").map_err(e)?;
    b.build_return(Some(&a.as_basic_value_enum())).map_err(e)?;
    b.position_at_end(fxsave);
    let a = pair.const_named_struct(&[t.const_int(576, false).into(), t.const_zero().into()]);
    b.build_return(Some(&a)).map_err(e)?;
    Ok(f)
}

fn zustand_asm<'ctx>(c: &'ctx Context, b: &Builder<'ctx>, pointer: PointerValue<'ctx>,
    mask: IntValue<'ctx>, extended: bool, restore: bool) -> Result<(), String>
{
    let pt = c.ptr_type(AddressSpace::default());
    let mut types: Vec<inkwell::types::BasicMetadataTypeEnum<'ctx>> = vec![pt.into()];
    let mut args: Vec<BasicMetadataValueEnum<'ctx>> = vec![pointer.into()];
    let op = match (extended, restore) { (true, false)=>"xsave64", (true,true)=>"xrstor64", (false,false)=>"fxsave64", (false,true)=>"fxrstor64" };
    let constraints = if extended {
        types.push(c.i32_type().into());
        types.push(c.i32_type().into());
        let lo = b.build_int_truncate(mask, c.i32_type(), "simd.maske.low").map_err(e)?;
        let hi = b.build_right_shift(mask, c.i64_type().const_int(32, false), false, "simd.maske.high64").map_err(e)?;
        let hi = b.build_int_truncate(hi, c.i32_type(), "simd.maske.high").map_err(e)?;
        args.push(lo.into());
        args.push(hi.into());
        "r,{ax},{dx},~{memory},~{flags}"
    } else { "r,~{memory},~{flags}" };
    let ty = c.void_type().fn_type(&types, false);
    let asm = c.create_inline_asm(ty, format!("{op} ($0)"), constraints.into(), true, false, None, false);
    b.build_indirect_call(ty, asm, &args, "").map_err(e)?;
    Ok(())
}

fn zustand_zweig<'ctx>(c: &'ctx Context, b: &Builder<'ctx>, f: FunctionValue<'ctx>,
    pointer: PointerValue<'ctx>, mask: IntValue<'ctx>, restore: bool) -> Result<(), String>
{
    let x = c.append_basic_block(f, "simd.xstate");
    let fx = c.append_basic_block(f, "simd.fxstate");
    let done = c.append_basic_block(f, "simd.zustand.fertig");
    let active = b.build_int_compare(IntPredicate::NE, mask, c.i64_type().const_zero(), "simd.xsave.aktiv").map_err(e)?;
    b.build_conditional_branch(active, x, fx).map_err(e)?;
    b.position_at_end(x);
    zustand_asm(c, b, pointer, mask, true, restore)?;
    b.build_unconditional_branch(done).map_err(e)?;
    b.position_at_end(fx);
    zustand_asm(c, b, pointer, mask, false, restore)?;
    b.build_unconditional_branch(done).map_err(e)?;
    b.position_at_end(done);
    Ok(())
}

/// Der Wrapper ruft erst nach dem Save genau eine private ISA-Variante auf.
/// Native Fehlerrückgaben kehren ebenfalls hierher zurück und durchlaufen Restore.
pub(crate) fn emit<'ctx>(c: &'ctx Context, m: &Module<'ctx>, f: FunctionValue<'ctx>, limit: u64,
    parameter_attributes: &[(u32, Vec<Attribute>)],
    mut body: impl FnMut(FunctionValue<'ctx>) -> Result<(), String>) -> Result<(), String>
{
    let triple = m.get_triple();
    if !triple.as_str().to_string_lossy().starts_with("x86_64-") {
        return Err("@simd ist für x86_64 definiert".into());
    }
    let name = f.get_name().to_string_lossy().into_owned();
    let source = name.strip_prefix("__moo_native.").unwrap_or(&name);
    let mut variants = Vec::new();
    for p in [Profil::Skalar, Profil::Ssse3, Profil::Avx2, Profil::Gfni128, Profil::Gfni256] {
        if p.id() > limit || (limit == 3 && p == Profil::Avx2) { continue; }
        let variant = m.add_function(&format!("__moo_native.__simd.{}.{source}", p.name()), f.get_type(), Some(Linkage::Internal));
        variant.set_call_conventions(f.get_call_conventions());
        attribute(c, variant, p);
        body(variant)?;
        variants.push((p, variant));
    }
    attribute(c, f, Profil::Skalar);
    let b = c.create_builder();
    b.position_at_end(c.append_basic_block(f, "entry"));
    let bare = matches!(triple.as_str().to_str().map_err(e)?, "x86_64-unknown-none" | "x86_64-unknown-uefi");
    let state = if bare {
        let info = b.build_call(zustand_info(c, m)?, &[], "simd.zustand").map_err(e)?
            .try_as_basic_value().basic().ok_or("SIMD-Zustand fehlt")?.into_struct_value();
        let size = b.build_extract_value(info, 0, "simd.zustand.groesse").map_err(e)?.into_int_value();
        let mask = b.build_extract_value(info, 1, "simd.zustand.maske").map_err(e)?.into_int_value();
        let bytes = b.build_int_add(size, c.i64_type().const_int(63, false), "simd.zustand.reserve").map_err(e)?;
        let raw = b.build_array_alloca(c.i8_type(), bytes, "simd.zustand.roh").map_err(e)?;
        let addr = b.build_ptr_to_int(raw, c.i64_type(), "simd.zustand.adresse").map_err(e)?;
        let addr = b.build_int_add(addr, c.i64_type().const_int(63, false), "simd.zustand.runden").map_err(e)?;
        let addr = b.build_and(addr, c.i64_type().const_int(!63u64, false), "simd.zustand.ausrichten").map_err(e)?;
        let pointer = b.build_int_to_ptr(addr, c.ptr_type(AddressSpace::default()), "simd.zustand.puffer").map_err(e)?;
        // XSAVE schreibt reservierte Headerbytes nicht. Kein SIMD-memset vor Save.
        for offset in (512..576).step_by(8) {
            let ptr = unsafe { b.build_gep(c.i8_type(), pointer, &[c.i64_type().const_int(offset, false)], "simd.zustand.header") }.map_err(e)?;
            b.build_store(ptr, c.i64_type().const_zero()).map_err(e)?.set_alignment(8).map_err(e)?;
        }
        zustand_zweig(c, &b, f, pointer, mask, false)?;
        Some((pointer, mask))
    } else { None };
    let selected = b.build_call(cpu(c, m)?, &[], "simd.cpu.profil").map_err(e)?
        .try_as_basic_value().basic().ok_or("SIMD-Profil fehlt")?.into_int_value();
    let selected = match limit {
        0 => c.i64_type().const_zero(),
        1 => {
            let available = b.build_int_compare(IntPredicate::NE, selected, c.i64_type().const_zero(), "simd.deckel.verfuegbar").map_err(e)?;
            b.build_int_z_extend(available, c.i64_type(), "simd.deckel.ssse3").map_err(e)?
        }
        2 => {
            let low = b.build_and(selected, c.i64_type().const_int(1, false), "simd.deckel.low").map_err(e)?;
            let avx = b.build_int_compare(IntPredicate::EQ, low, c.i64_type().const_zero(), "simd.deckel.avx").map_err(e)?;
            let present = b.build_int_compare(IntPredicate::NE, selected, c.i64_type().const_zero(), "simd.deckel.aktiv").map_err(e)?;
            let capped = b.build_select(avx, c.i64_type().const_int(2, false), c.i64_type().const_int(1, false), "simd.deckel").map_err(e)?.into_int_value();
            b.build_select(present, capped, c.i64_type().const_zero(), "simd.deckel.ohne").map_err(e)?.into_int_value()
        }
        3 => {
            let gfni = b.build_int_compare(IntPredicate::UGE, selected, c.i64_type().const_int(3, false), "simd.deckel.gfni128").map_err(e)?;
            let any = b.build_int_compare(IntPredicate::NE, selected, c.i64_type().const_zero(), "simd.deckel.basis").map_err(e)?;
            let basis = b.build_int_z_extend(any, c.i64_type(), "simd.deckel.basis64").map_err(e)?;
            b.build_select(gfni, c.i64_type().const_int(3, false), basis, "simd.deckel.gfni.ohneavx").map_err(e)?.into_int_value()
        }
        _ => selected,
    };
    let args: Vec<BasicMetadataValueEnum<'ctx>> = f.get_param_iter().map(Into::into).collect();
    for (index, (p, variant)) in variants.iter().enumerate().rev() {
        let block = c.append_basic_block(f, p.name());
        if index != 0 {
            let next = c.append_basic_block(f, "simd.naechstes");
            let valid = b.build_int_compare(IntPredicate::EQ, selected, c.i64_type().const_int(p.id(), false), "simd.cpu.passt").map_err(e)?;
            b.build_conditional_branch(valid, block, next).map_err(e)?;
            b.position_at_end(block);
            let call = b.build_call(*variant, &args, if f.get_type().get_return_type().is_some() { "simd.aufruf" } else { "" }).map_err(e)?;
            call.set_call_convention(variant.get_call_conventions());
            for (position, attributes) in parameter_attributes {
                for attribute in attributes {
                    call.add_attribute(AttributeLoc::Param(*position), *attribute);
                }
            }
            let result = call.try_as_basic_value().basic();
            if let Some((pointer, mask)) = state { zustand_zweig(c, &b, f, pointer, mask, true)?; }
            b.build_return(result.as_ref().map(|r| r as &dyn BasicValue<'ctx>)).map_err(e)?;
            b.position_at_end(next);
        } else {
            b.build_unconditional_branch(block).map_err(e)?;
            b.position_at_end(block);
            let call = b.build_call(*variant, &args, if f.get_type().get_return_type().is_some() { "simd.aufruf" } else { "" }).map_err(e)?;
            call.set_call_convention(variant.get_call_conventions());
            for (position, attributes) in parameter_attributes {
                for attribute in attributes {
                    call.add_attribute(AttributeLoc::Param(*position), *attribute);
                }
            }
            let result = call.try_as_basic_value().basic();
            if let Some((pointer, mask)) = state { zustand_zweig(c, &b, f, pointer, mask, true)?; }
            b.build_return(result.as_ref().map(|r| r as &dyn BasicValue<'ctx>)).map_err(e)?;
        }
    }
    Ok(())
}
