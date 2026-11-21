//! Ganzzahlvektoren: nominale Speicherwerte im vorhandenen Struktur-ABI.
//! ISA-Auswahl und Kernelzustand bleiben getrennt von den Werteoperationen.
use crate::ast::{BinOp, Expr, TypeKind, TypeRef};
use crate::codegen_native::NType;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::types::VectorType;
use inkwell::values::{IntValue, VectorValue};
use inkwell::IntPredicate;

pub(crate) const NAMEN: [&str; 3] = ["u8x16", "u8x32", "u64x4"];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum SimdArt { U8x16, U8x32, U64x4 }

impl SimdArt {
    pub(crate) fn name(self) -> &'static str {
        match self { Self::U8x16 => "u8x16", Self::U8x32 => "u8x32", Self::U64x4 => "u64x4" }
    }
    pub(crate) fn bahnen(self) -> u32 {
        match self { Self::U8x16 => 16, Self::U8x32 => 32, Self::U64x4 => 4 }
    }
    pub(crate) fn element(self) -> NType {
        match self { Self::U8x16 | Self::U8x32 => NType::U8, Self::U64x4 => NType::U64 }
    }
    pub(crate) fn bytes(self) -> u64 {
        match self { Self::U8x16 => 16, Self::U8x32 | Self::U64x4 => 32 }
    }
    pub(crate) fn llvm<'ctx>(self, context: &'ctx Context) -> VectorType<'ctx> {
        self.element().llvm_type(context).into_int_type().vec_type(self.bahnen())
    }
}

pub(crate) fn art(name: &str) -> Option<SimdArt> {
    match name { "u8x16" => Some(SimdArt::U8x16), "u8x32" => Some(SimdArt::U8x32),
        "u64x4" => Some(SimdArt::U64x4), _ => None }
}

/// Speicherlayout ist ein Array nativer unsigned Elemente. Die SSA-Vektorform
/// wird an der Speichergrenze zerlegt, nicht als neue C-Vektor-ABI ausgegeben.
pub(crate) fn felder(name: &str) -> Option<Vec<(String, TypeRef)>> {
    let art = art(name)?;
    let named = |n: &str| TypeRef { kind: TypeKind::Name(n.into()), line: 0,
        column: 0, end_line: 0, end_column: 0 };
    let element = named(if art.element() == NType::U8 { "u8" } else { "u64" });
    let array = TypeRef { kind: TypeKind::Array { element: Box::new(element),
        length: Box::new(Expr::NativeNumber { legacy: f64::from(art.bahnen()),
            text: art.bahnen().to_string(), line: 0, column: 0 }) }, ..named("") };
    Some(vec![("werte".into(), array)])
}

pub(crate) fn binaer<'ctx>(builder: &Builder<'ctx>, op: &BinOp,
    a: VectorValue<'ctx>, b: VectorValue<'ctx>) -> Result<VectorValue<'ctx>, String>
{
    if a.get_type() != b.get_type() || !a.get_type().get_element_type().is_int_type() {
        return Err("SIMD: beide Operanden benötigen denselben Ganzzahlvektortyp".into());
    }
    // Ohne nsw/nuw: dieselbe Wrapping-Semantik wie unsigned native Skalare.
    match op {
        BinOp::Add => builder.build_int_add(a, b, "simd.add"),
        BinOp::Sub => builder.build_int_sub(a, b, "simd.sub"),
        BinOp::Mul => builder.build_int_mul(a, b, "simd.mul"),
        BinOp::BitAnd => builder.build_and(a, b, "simd.and"),
        BinOp::BitOr => builder.build_or(a, b, "simd.or"),
        BinOp::BitXor => builder.build_xor(a, b, "simd.xor"),
        _ => return Err(format!("SIMD: Operator {op:?} ist für Ganzzahlvektoren nicht definiert")),
    }.map_err(|e| e.to_string())
}

pub(crate) fn unaer<'ctx>(builder: &Builder<'ctx>, op: &crate::ast::UnaryOpKind,
    value: VectorValue<'ctx>) -> Result<VectorValue<'ctx>, String>
{
    if !value.get_type().get_element_type().is_int_type() { return Err("SIMD: Ganzzahlvektor erwartet".into()); }
    match op {
        crate::ast::UnaryOpKind::Neg => builder.build_int_neg(value, "simd.neg"),
        crate::ast::UnaryOpKind::BitNot => builder.build_not(value, "simd.not"),
        _ => return Err("SIMD: nur arithmetisches Minus oder bitweises Nicht".into()),
    }.map_err(|e|e.to_string())
}


fn bytevektoren(a: VectorValue<'_>, b: VectorValue<'_>) -> Result<(), String> {
    if a.get_type() != b.get_type()
        || !a.get_type().get_element_type().is_int_type()
        || a.get_type().get_element_type().into_int_type().get_bit_width() != 8
        || !matches!(a.get_type().get_size(), 16 | 32) {
        return Err("SIMD: zwei identische u8x16- oder u8x32-Werte erwartet".into());
    }
    Ok(())
}

fn splat_byte<'ctx>(context: &'ctx Context, builder: &Builder<'ctx>,
    typ: VectorType<'ctx>, wert: u8) -> Result<VectorValue<'ctx>, String>
{
    let mut vector = typ.get_undef();
    for i in 0..typ.get_size() {
        vector = builder.build_insert_element(vector, context.i8_type().const_int(u64::from(wert), false),
            context.i32_type().const_int(u64::from(i), false), "simd.byte.konst").map_err(|e| e.to_string())?;
    }
    Ok(vector)
}

/// PSHUFB-Semantik: Bit 7 nullt, Bits 0..3 wählen innerhalb einer
/// 128-Bit-Hälfte. Der maskierte Index bleibt auch beim Null-Ergebnis gültig.
pub(crate) fn mischen_skalar<'ctx>(context: &'ctx Context, builder: &Builder<'ctx>,
    a: VectorValue<'ctx>, maske: VectorValue<'ctx>) -> Result<VectorValue<'ctx>, String>
{
    bytevektoren(a, maske)?;
    let mut result = a.get_type().get_undef();
    for i in 0..a.get_type().get_size() {
        let position = context.i32_type().const_int(u64::from(i), false);
        let m = builder.build_extract_element(maske, position, "simd.shuffle.maske")
            .map_err(|e| e.to_string())?.into_int_value();
        let low = builder.build_and(m, context.i8_type().const_int(15, false),
            "simd.shuffle.low").map_err(|e| e.to_string())?;
        let index = builder.build_or(low, context.i8_type().const_int(u64::from(i & !15), false),
            "simd.shuffle.index").map_err(|e| e.to_string())?;
        let byte = builder.build_extract_element(a, index, "simd.shuffle.byte")
            .map_err(|e| e.to_string())?.into_int_value();
        let high = builder.build_int_compare(IntPredicate::ULT, m,
            context.i8_type().const_int(128, false), "simd.shuffle.aktiv").map_err(|e| e.to_string())?;
        let value = builder.build_select(high, byte, context.i8_type().const_zero(),
            "simd.shuffle.wert").map_err(|e| e.to_string())?;
        result = builder.build_insert_element(result, value, position,
            "simd.shuffle.ergebnis").map_err(|e| e.to_string())?;
    }
    Ok(result)
}

/// GF(256) mit Reduktionspolynom x^8+x^4+x^3+x^2+1 (0x11D).
/// Acht feste Schritte; keine datenabhängigen Sprünge oder Tabellenzugriffe.
/// GFNI-Multiplikation allein verwendet 0x11B und benötigt eine Basisabbildung.
pub(crate) fn gf256_skalar<'ctx>(context: &'ctx Context, builder: &Builder<'ctx>,
    mut a: VectorValue<'ctx>, mut b: VectorValue<'ctx>) -> Result<VectorValue<'ctx>, String>
{
    bytevektoren(a, b)?;
    let typ = a.get_type();
    let zero = typ.const_zero();
    let one = splat_byte(context, builder, typ, 1)?;
    let seven = splat_byte(context, builder, typ, 7)?;
    let reduction = splat_byte(context, builder, typ, 0x1d)?;
    let mut result = zero;
    for _ in 0..8 {
        let bit = builder.build_and(b, one, "simd.gf.bit").map_err(|e| e.to_string())?;
        let mask = builder.build_int_sub(zero, bit, "simd.gf.maske").map_err(|e| e.to_string())?;
        let term = builder.build_and(a, mask, "simd.gf.term").map_err(|e| e.to_string())?;
        result = builder.build_xor(result, term, "simd.gf.summe").map_err(|e| e.to_string())?;
        let carry = builder.build_right_shift(a, seven, false, "simd.gf.carry").map_err(|e| e.to_string())?;
        let carry = builder.build_int_sub(zero, carry, "simd.gf.carrymaske").map_err(|e| e.to_string())?;
        let carry = builder.build_and(carry, reduction, "simd.gf.reduktion").map_err(|e| e.to_string())?;
        let shifted = builder.build_left_shift(a, one, "simd.gf.doppelt").map_err(|e| e.to_string())?;
        a = builder.build_xor(shifted, carry, "simd.gf.naechstes").map_err(|e| e.to_string())?;
        b = builder.build_right_shift(b, one, false, "simd.gf.rest").map_err(|e| e.to_string())?;
    }
    Ok(result)
}

pub(crate) fn byte_operation(name: &str) -> bool {
    matches!(name, "simd_mischen" | "simd_gf256_mal" | "simd_gf256_mal_faktor" | "simd_laden")
}



fn gf_konst(mut a: u8, mut b: u8) -> u8 {
    let mut r = 0;
    for _ in 0..8 {
        if b & 1 != 0 { r ^= a; }
        a = a.wrapping_shl(1) ^ if a & 0x80 != 0 { 0x1d } else { 0 };
        b >>= 1;
    }
    r
}

fn faktor_isa<'ctx>(c: &'ctx Context, m: &inkwell::module::Module<'ctx>, b: &Builder<'ctx>,
    profile: crate::native_simd_dispatch::Profil, a: VectorValue<'ctx>, factor: IntValue<'ctx>)
    -> Result<VectorValue<'ctx>, String>
{
    use inkwell::module::Linkage;
    let index = b.build_int_z_extend(factor, c.i64_type(), "simd.gf.faktor.index").map_err(|e|e.to_string())?;
    let zero = c.i32_type().const_zero();
    if profile.gfni() {
        let name = "__moo_gf256_factor_matrix";
        let global = if let Some(g) = m.get_global(name) { g } else {
            let values = (0..256).map(|factor| {
                let mut matrix = 0u64;
                for row in 0..8 {
                    let mut bits = 0u64;
                    for col in 0..8 {
                        if gf_konst(1 << col, factor as u8) & (1 << row) != 0 { bits |= 1 << col; }
                    }
                    matrix |= bits << ((7-row)*8);
                }
                c.i64_type().const_int(matrix, false)
            }).collect::<Vec<_>>();
            let value = c.i64_type().const_array(&values);
            let g = m.add_global(value.get_type(), None, name);
            g.set_linkage(Linkage::Private);
            g.set_constant(true);
            g.set_initializer(&value);
            g.set_alignment(8);
            g
        };
        let ptr = unsafe { b.build_gep(c.i64_type().array_type(256), global.as_pointer_value(), &[zero, index], "simd.gf.matrix.ptr") }.map_err(|e|e.to_string())?;
        let word = b.build_load(c.i64_type(), ptr, "simd.gf.matrix").map_err(|e|e.to_string())?.into_int_value();
        let mut matrix = c.i64_type().vec_type(a.get_type().get_size()/8).get_undef();
        for i in 0..a.get_type().get_size()/8 {
            matrix = b.build_insert_element(matrix, word, c.i32_type().const_int(u64::from(i), false), "simd.gf.matrix.splat").map_err(|e|e.to_string())?;
        }
        let matrix = b.build_bit_cast(matrix, a.get_type(), "simd.gf.matrix.bytes").map_err(|e|e.to_string())?.into_vector_value();
        return intrinsic(c, m, b, if a.get_type().get_size()==32 { "llvm.x86.vgf2p8affineqb.256" }
            else { "llvm.x86.vgf2p8affineqb.128" }, a, matrix, true);
    }
    let name = "__moo_gf256_factor_nibbles";
    let global = if let Some(g) = m.get_global(name) { g } else {
        let rows = (0..256).map(|factor| {
            let entries = (0..32).map(|n| {
                let v = if n < 16 { n } else { (n-16)*16 };
                c.i8_type().const_int(u64::from(gf_konst(v as u8, factor as u8)), false)
            }).collect::<Vec<_>>();
            c.i8_type().const_array(&entries)
        }).collect::<Vec<_>>();
        let value = c.i8_type().array_type(32).const_array(&rows);
        let g = m.add_global(value.get_type(), None, name);
        g.set_linkage(Linkage::Private);
        g.set_constant(true);
        g.set_initializer(&value);
        g.set_alignment(32);
        g
    };
    let table_ty = c.i8_type().array_type(32).array_type(256);
    let mut tables = Vec::new();
    for offset in [0u64,16] {
        let ptr = unsafe { b.build_gep(table_ty, global.as_pointer_value(), &[zero,index,c.i32_type().const_int(offset,false)], "simd.gf.nibble.ptr") }.map_err(|e|e.to_string())?;
        let low = b.build_load(c.i8_type().vec_type(16), ptr, "simd.gf.nibble.table").map_err(|e|e.to_string())?.into_vector_value();
        let table = if a.get_type().get_size()==16 { low } else {
            let mut wide = a.get_type().get_undef();
            for i in 0u32..32 {
                let byte = b.build_extract_element(low,c.i32_type().const_int(u64::from(i%16),false),"simd.gf.nibble.byte").map_err(|e|e.to_string())?;
                wide = b.build_insert_element(wide,byte,c.i32_type().const_int(u64::from(i),false),"simd.gf.nibble.repeat").map_err(|e|e.to_string())?;
            }
            wide
        };
        tables.push(table);
    }
    let mask = splat_byte(c,b,a.get_type(),15)?;
    let shift = splat_byte(c,b,a.get_type(),4)?;
    let lo = b.build_and(a,mask,"simd.gf.low.nibble").map_err(|e|e.to_string())?;
    let hi = b.build_right_shift(a,shift,false,"simd.gf.high.nibble").map_err(|e|e.to_string())?;
    let op = if a.get_type().get_size()==32 { "llvm.x86.avx2.pshuf.b" } else { "llvm.x86.ssse3.pshuf.b.128" };
    let lo = intrinsic(c,m,b,op,tables[0],lo,false)?;
    let hi = intrinsic(c,m,b,op,tables[1],hi,false)?;
    b.build_xor(lo,hi,"simd.gf.nibble.product").map_err(|e|e.to_string())
}

/// GF(0x11D) mit einem gemeinsamen, explizit typisierten u8-Faktor.
pub(crate) fn gf256_faktor<'ctx>(c: &'ctx Context, m: &inkwell::module::Module<'ctx>, b: &Builder<'ctx>,
    profile: crate::native_simd_dispatch::Profil, a: VectorValue<'ctx>, factor: IntValue<'ctx>)
    -> Result<VectorValue<'ctx>, String>
{
    if factor.get_type().get_bit_width()!=8 || !matches!(a.get_type().get_size(),16|32)
        || !a.get_type().get_element_type().is_int_type()
        || a.get_type().get_element_type().into_int_type().get_bit_width()!=8 {
        return Err("simd_gf256_mal_faktor erwartet (u8x16 oder u8x32, u8)".into());
    }
    if profile == crate::native_simd_dispatch::Profil::Skalar {
        let mut second = a.get_type().get_undef();
        for i in 0..a.get_type().get_size() {
            second = b.build_insert_element(second,factor,c.i32_type().const_int(u64::from(i),false),"simd.gf.scalar.factor").map_err(|e|e.to_string())?;
        }
        return gf256_skalar(c,b,a,second);
    }
    if a.get_type().get_size()==16 || profile.avx() { return faktor_isa(c,m,b,profile,a,factor); }
    let mut result = a.get_type().get_undef();
    for start in [0,16] {
        let half = halb(c,b,a,start)?;
        let value = faktor_isa(c,m,b,profile,half,factor)?;
        for i in 0..16 {
            let lane = b.build_extract_element(value,c.i32_type().const_int(u64::from(i),false),"simd.gf.factor.lane").map_err(|e|e.to_string())?;
            result = b.build_insert_element(result,lane,c.i32_type().const_int(u64::from(start+i),false),"simd.gf.factor.join").map_err(|e|e.to_string())?;
        }
    }
    Ok(result)
}

fn halb<'ctx>(c: &'ctx Context, b: &Builder<'ctx>, value: VectorValue<'ctx>, start: u32)
    -> Result<VectorValue<'ctx>, String>
{
    let mut result = c.i8_type().vec_type(16).get_undef();
    for i in 0..16 {
        let lane = b.build_extract_element(value, c.i32_type().const_int(u64::from(start+i), false), "simd.halb.hole").map_err(|e|e.to_string())?;
        result = b.build_insert_element(result, lane, c.i32_type().const_int(u64::from(i), false), "simd.halb.setze").map_err(|e|e.to_string())?;
    }
    Ok(result)
}

fn intrinsic<'ctx>(c: &'ctx Context, m: &inkwell::module::Module<'ctx>, b: &Builder<'ctx>,
    name: &str, a: VectorValue<'ctx>, second: VectorValue<'ctx>, affine: bool)
    -> Result<VectorValue<'ctx>, String>
{
    let intrinsic = inkwell::intrinsics::Intrinsic::find(name).ok_or_else(|| format!("LLVM-Intrinsic {name} fehlt"))?;
    let function = intrinsic.get_declaration(m, &[]).ok_or_else(|| format!("LLVM-Deklaration {name} fehlt"))?;
    let mut args: Vec<inkwell::values::BasicMetadataValueEnum<'ctx>> = vec![a.into(), second.into()];
    if affine { args.push(c.i8_type().const_zero().into()); }
    b.build_call(function, &args, "simd.isa").map_err(|e|e.to_string())?
        .try_as_basic_value().basic().map(|v|v.into_vector_value()).ok_or("SIMD-Intrinsic liefert keinen Wert".into())
}

/// Basiswechsel GF(0x11D) -> GF(0x11B): alpha=3 ist eine Nullstelle von
/// x^8+x^4+x^3+x^2+1 im AES-Körper. Spalten alpha^0..7 sind
/// 01,03,05,0f,11,33,55,ff. Diese GF(2)-Matrix ist ihre eigene Inverse.
/// GFNI kodiert Ergebnisbit 0 im höchstwertigen Matrixbyte.
fn gf_basis<'ctx>(c: &'ctx Context, b: &Builder<'ctx>, typ: VectorType<'ctx>)
    -> Result<VectorValue<'ctx>, String>
{
    const MATRIX: u64 = 0xffaacc88f0a0c080;
    let mut result = typ.get_undef();
    for i in 0..typ.get_size() {
        let byte = (MATRIX >> ((i % 8) * 8)) & 255;
        result = b.build_insert_element(result, c.i8_type().const_int(byte, false),
            c.i32_type().const_int(u64::from(i), false), "simd.gf.basis").map_err(|e|e.to_string())?;
    }
    Ok(result)
}

fn byte_isa<'ctx>(c: &'ctx Context, m: &inkwell::module::Module<'ctx>, b: &Builder<'ctx>,
    operation: &str, a: VectorValue<'ctx>, second: VectorValue<'ctx>)
    -> Result<VectorValue<'ctx>, String>
{
    let wide = a.get_type().get_size() == 32;
    if operation == "simd_mischen" {
        return intrinsic(c, m, b, if wide { "llvm.x86.avx2.pshuf.b" }
            else { "llvm.x86.ssse3.pshuf.b.128" }, a, second, false);
    }
    let affine = if wide { "llvm.x86.vgf2p8affineqb.256" } else { "llvm.x86.vgf2p8affineqb.128" };
    let multiply = if wide { "llvm.x86.vgf2p8mulb.256" } else { "llvm.x86.vgf2p8mulb.128" };
    let matrix = gf_basis(c, b, a.get_type())?;
    let a = intrinsic(c, m, b, affine, a, matrix, true)?;
    let second = intrinsic(c, m, b, affine, second, matrix, true)?;
    let product = intrinsic(c, m, b, multiply, a, second, false)?;
    intrinsic(c, m, b, affine, product, matrix, true)
}

pub(crate) fn byte_rechnung<'ctx>(c: &'ctx Context, m: &inkwell::module::Module<'ctx>, b: &Builder<'ctx>,
    profile: crate::native_simd_dispatch::Profil, operation: &str,
    a: VectorValue<'ctx>, second: VectorValue<'ctx>) -> Result<VectorValue<'ctx>, String>
{
    use crate::native_simd_dispatch::Profil;
    bytevektoren(a, second)?;
    if profile == Profil::Skalar || (operation == "simd_gf256_mal" && !profile.gfni()) {
        return if operation == "simd_mischen" { mischen_skalar(c, b, a, second) }
            else { gf256_skalar(c, b, a, second) };
    }
    if a.get_type().get_size() == 16 || profile.avx() {
        return byte_isa(c, m, b, operation, a, second);
    }
    // SSSE3/GFNI ohne AVX: zwei selbständige XMM-Hälften, kein YMM-Einsatz.
    let mut result = a.get_type().get_undef();
    for start in [0, 16] {
        let left = halb(c, b, a, start)?;
        let right = halb(c, b, second, start)?;
        let value = byte_isa(c, m, b, operation, left, right)?;
        for i in 0..16 {
            let lane = b.build_extract_element(value, c.i32_type().const_int(u64::from(i), false), "simd.isa.hole").map_err(|e|e.to_string())?;
            result = b.build_insert_element(result, lane, c.i32_type().const_int(u64::from(start+i), false), "simd.isa.setze").map_err(|e|e.to_string())?;
        }
    }
    Ok(result)
}

pub(crate) fn gleich<'ctx>(context: &'ctx Context, builder: &Builder<'ctx>,
    a: VectorValue<'ctx>, b: VectorValue<'ctx>) -> Result<IntValue<'ctx>, String>
{
    if a.get_type() != b.get_type() || !a.get_type().get_element_type().is_int_type() {
        return Err("SIMD-Vergleich benötigt identische Ganzzahlvektortypen".into());
    }
    let lanes = builder.build_int_compare(IntPredicate::EQ, a, b, "simd.gleich").map_err(|e|e.to_string())?;
    let mut all = context.bool_type().const_int(1, false);
    for i in 0..a.get_type().get_size() {
        let lane = builder.build_extract_element(lanes, context.i32_type().const_int(u64::from(i), false),
            "simd.gleich.bahn").map_err(|e|e.to_string())?.into_int_value();
        all = builder.build_and(all, lane, "simd.gleich.alle").map_err(|e|e.to_string())?;
    }
    Ok(all)
}
