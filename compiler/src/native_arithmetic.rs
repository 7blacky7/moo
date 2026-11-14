//! LLVM-Bausteine fuer native Integer-Arithmetik (P2-T11).
//! Noch nicht an den nativen Ausdrucks- oder Fehlerpfad angebunden.
//! Der Aufrufer muss fuer Div/Rem/Shift einen Fehlerblock derselben Funktion
//! uebergeben. Dieser Block muss den jeweiligen Fehlervertrag umsetzen und darf
//! nicht zur ungueltigen LLVM-Operation zurueckfallen.

use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::values::{BasicValue, FunctionValue, IntValue};
use inkwell::IntPredicate;

use crate::codegen_native::{NType, NValue};

#[derive(Clone, Copy, Debug)]
pub(crate) enum IntOp {
    Add, Sub, Mul, Div, Rem, Shl, Shr, BitAnd, BitOr, BitXor,
}

fn int_width(ty: NType) -> Option<u32> {
    match ty {
        NType::I8 | NType::U8 => Some(8),
        NType::I16 | NType::U16 => Some(16),
        NType::I32 | NType::U32 => Some(32),
        NType::I64 | NType::U64 => Some(64),
        _ => None,
    }
}

/// Ungueltigkeitsbedingung (i1) fehlerfaehiger Operationen: Div/Rem durch 0
/// oder signed MIN / -1; Shift-Count ausserhalb 0..Breite-1 (unsigned
/// verglichen, negative Counts zaehlen als gross). None ohne Fehlerfall.
/// Gemeinsam fuer int_binary (Guard) und pruefung (Status, ADR §13).
fn int_invalid<'ctx>(
    builder: &Builder<'ctx>,
    op: IntOp,
    lhs: IntValue<'ctx>,
    rhs: IntValue<'ctx>,
    signed: bool,
    width: u32,
) -> Result<Option<IntValue<'ctx>>, String> {
    Ok(match op {
        IntOp::Div | IntOp::Rem => {
            let zero = builder.build_int_compare(
                IntPredicate::EQ, rhs, rhs.get_type().const_zero(), "native.div.zero",
            ).map_err(|e| e.to_string())?;
            if signed {
                let min = lhs.get_type().const_int(1_u64 << (width - 1), false);
                let minus_one = rhs.get_type().const_all_ones();
                let is_min = builder.build_int_compare(IntPredicate::EQ, lhs, min, "native.div.min")
                    .map_err(|e| e.to_string())?;
                let is_minus_one = builder.build_int_compare(
                    IntPredicate::EQ, rhs, minus_one, "native.div.minus_one",
                ).map_err(|e| e.to_string())?;
                let overflow = builder.build_and(is_min, is_minus_one, "native.div.overflow")
                    .map_err(|e| e.to_string())?;
                Some(builder.build_or(zero, overflow, "native.div.invalid")
                    .map_err(|e| e.to_string())?)
            } else {
                Some(zero)
            }
        }
        IntOp::Shl | IntOp::Shr => {
            Some(builder.build_int_compare(
                IntPredicate::UGE, rhs, rhs.get_type().const_int(width as u64, false),
                "native.shift.invalid",
            ).map_err(|e| e.to_string())?)
        }
        _ => None,
    })
}

/// Arithmetik verlangt gleiche Integer-Typen; Shift-Counts duerfen einen
/// anderen Integer-Typ haben und werden vor jeder Breitenkonversion geprueft.
/// Bei Div/Rem/Shift springt ein ungueltiger Eingabewert vor der Operation nach
/// failure. Der Builder steht anschliessend im gueltigen Fortsetzungsblock.
pub(crate) fn int_binary<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    op: IntOp,
    left: NValue<'ctx>,
    right: NValue<'ctx>,
    failure: BasicBlock<'ctx>,
) -> Result<NValue<'ctx>, String> {
    let width = int_width(left.ty).ok_or("Ganzzahltyp erwartet")?;
    let right_width = int_width(right.ty).ok_or("Ganzzahltyp fuer rechten Operanden erwartet")?;
    if !matches!(op, IntOp::Shl | IntOp::Shr) && left.ty != right.ty {
        return Err("Gemischte native Integer-Typen brauchen eine explizite Konversion".into());
    }
    if left.value.get_type() != left.ty.llvm_type(context)
        || right.value.get_type() != right.ty.llvm_type(context)
    {
        return Err("Nativer LLVM-Wert passt nicht zum Integer-Typ".into());
    }
    let current = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
    let function = current.get_parent().ok_or("LLVM-Block ohne Funktion")?;
    if failure.get_parent() != Some(function) {
        return Err("Fehlerblock gehoert zu einer anderen Funktion".into());
    }
    let lhs = left.value.into_int_value();
    let rhs = right.value.into_int_value();
    let i1 = context.bool_type();
    let invalid = int_invalid(builder, op, lhs, rhs, left.ty.is_signed_integer(), width)?;
    if let Some(invalid) = invalid {
        if invalid.get_type() != i1 {
            return Err("Ungueltige LLVM-Fehlerbedingung".into());
        }
        let valid = context.append_basic_block(function, "native.arith.valid");
        builder.build_conditional_branch(invalid, failure, valid)
            .map_err(|e| e.to_string())?;
        builder.position_at_end(valid);
    }
    // Erst nach dem Guard darf der Count fuer LLVM auf die Breite des linken
    // Operanden gebracht werden; vorheriges Truncieren koennte Fehler verdecken.
    let shift_rhs = if matches!(op, IntOp::Shl | IntOp::Shr) && right_width != width {
        if right_width < width {
            builder.build_int_z_extend(rhs, lhs.get_type(), "native.shift.extend")
        } else {
            builder.build_int_truncate(rhs, lhs.get_type(), "native.shift.truncate")
        }.map_err(|e| e.to_string())?
    } else {
        rhs
    };
    // Die generischen LLVM-Integeroperationen erhalten keine nsw/nuw-Flags.
    let result = match op {
        IntOp::Add => builder.build_int_add(lhs, rhs, "native.add"),
        IntOp::Sub => builder.build_int_sub(lhs, rhs, "native.sub"),
        IntOp::Mul => builder.build_int_mul(lhs, rhs, "native.mul"),
        IntOp::BitAnd => builder.build_and(lhs, rhs, "native.and"),
        IntOp::BitOr => builder.build_or(lhs, rhs, "native.or"),
        IntOp::BitXor => builder.build_xor(lhs, rhs, "native.xor"),
        IntOp::Div if left.ty.is_signed_integer() => builder.build_int_signed_div(lhs, rhs, "native.sdiv"),
        IntOp::Div => builder.build_int_unsigned_div(lhs, rhs, "native.udiv"),
        IntOp::Rem if left.ty.is_signed_integer() => builder.build_int_signed_rem(lhs, rhs, "native.srem"),
        IntOp::Rem => builder.build_int_unsigned_rem(lhs, rhs, "native.urem"),
        IntOp::Shl => builder.build_left_shift(lhs, shift_rhs, "native.shl"),
        IntOp::Shr => builder.build_right_shift(lhs, shift_rhs, left.ty.is_signed_integer(), "native.shr"),
    }.map_err(|e| e.to_string())?;
    NValue::checked(context, left.ty, result.into())
}

/// ADR §3/§13: Vorpruefung mit Status statt Trap/Fehler. teilbar(a, b):
/// Division und Rest sind definiert (b != 0, nicht signed MIN / -1);
/// verschiebbar(a, n): 0 <= n < Breite von a. Englisch divisible/shiftable.
pub(crate) fn pruef_art(name: &str) -> Option<IntOp> {
    match name {
        "teilbar" | "divisible" => Some(IntOp::Div),
        "verschiebbar" | "shiftable" => Some(IntOp::Shl),
        _ => None,
    }
}

/// bool-Status der Vorpruefung: wahr, wenn die Operation fehlerfrei laeuft.
/// Typregeln wie int_binary: Division gleiche Typen, Shift-Count beliebig ganz.
pub(crate) fn pruefung<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    op: IntOp,
    left: NValue<'ctx>,
    right: NValue<'ctx>,
) -> Result<NValue<'ctx>, String> {
    let width = int_width(left.ty).ok_or("Ganzzahltyp erwartet")?;
    int_width(right.ty).ok_or("Ganzzahltyp fuer rechten Operanden erwartet")?;
    if !matches!(op, IntOp::Shl | IntOp::Shr) && left.ty != right.ty {
        return Err("Gemischte native Integer-Typen brauchen eine explizite Konversion".into());
    }
    if left.value.get_type() != left.ty.llvm_type(context)
        || right.value.get_type() != right.ty.llvm_type(context)
    {
        return Err("Nativer LLVM-Wert passt nicht zum Integer-Typ".into());
    }
    let invalid = int_invalid(
        builder, op, left.value.into_int_value(), right.value.into_int_value(),
        left.ty.is_signed_integer(), width,
    )?.ok_or("Operation ohne Fehlerfall braucht keine Vorpruefung")?;
    let ok = builder.build_not(invalid, "native.pruefung").map_err(|e| e.to_string())?;
    NValue::checked(context, NType::Bool, ok.into())
}

pub(crate) fn int_neg<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    value: NValue<'ctx>,
) -> Result<NValue<'ctx>, String> {
    int_width(value.ty).ok_or("Ganzzahltyp erwartet")?;
    if value.value.get_type() != value.ty.llvm_type(context) {
        return Err("Nativer LLVM-Wert passt nicht zum Integer-Typ".into());
    }
    let result = builder.build_int_neg(value.value.into_int_value(), "native.neg")
        .map_err(|e| e.to_string())?;
    NValue::checked(context, value.ty, result.into())
}

#[derive(Clone, Copy, Debug)]
pub(crate) enum FloatOp {
    Add, Sub, Mul, Div,
}

/// IEEE-Basisoperationen auf gleicher f32/f64-Breite. Es werden keine
/// Fast-Math-Flags oder FMA-Kontraktion angefordert; /0 bleibt IEEE.
pub(crate) fn float_binary<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    op: FloatOp,
    left: NValue<'ctx>,
    right: NValue<'ctx>,
) -> Result<NValue<'ctx>, String> {
    if !matches!(left.ty, NType::F32 | NType::F64) || left.ty != right.ty {
        return Err("Gleiche native Float-Typen erwartet".into());
    }
    if left.value.get_type() != left.ty.llvm_type(context)
        || right.value.get_type() != right.ty.llvm_type(context)
    {
        return Err("Nativer LLVM-Wert passt nicht zum Float-Typ".into());
    }
    let lhs = left.value.into_float_value();
    let rhs = right.value.into_float_value();
    let result = match op {
        FloatOp::Add => builder.build_float_add(lhs, rhs, "native.fadd"),
        FloatOp::Sub => builder.build_float_sub(lhs, rhs, "native.fsub"),
        FloatOp::Mul => builder.build_float_mul(lhs, rhs, "native.fmul"),
        FloatOp::Div => builder.build_float_div(lhs, rhs, "native.fdiv"),
    }.map_err(|e| e.to_string())?;
    NValue::checked(context, left.ty, result.into())
}

#[derive(Clone, Copy, Debug)]
pub(crate) enum CompareOp {
    Eq, Ne, Lt, Le, Gt, Ge,
}

/// Vergleiche setzen identische operandseitige Typen voraus. IEEE-NaN ist
/// ungleich jedem Wert; alle geordneten Relationen sind bei NaN falsch.
pub(crate) fn compare<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    op: CompareOp,
    left: NValue<'ctx>,
    right: NValue<'ctx>,
) -> Result<NValue<'ctx>, String> {
    if left.ty != right.ty || left.value.get_type() != left.ty.llvm_type(context)
        || right.value.get_type() != right.ty.llvm_type(context) {
        return Err("Vergleich braucht zwei Werte desselben nativen Typs".into());
    }
    let result = if int_width(left.ty).is_some() {
        let signed = left.ty.is_signed_integer();
        let predicate = match op {
            CompareOp::Eq => IntPredicate::EQ,
            CompareOp::Ne => IntPredicate::NE,
            CompareOp::Lt if signed => IntPredicate::SLT,
            CompareOp::Le if signed => IntPredicate::SLE,
            CompareOp::Gt if signed => IntPredicate::SGT,
            CompareOp::Ge if signed => IntPredicate::SGE,
            CompareOp::Lt => IntPredicate::ULT,
            CompareOp::Le => IntPredicate::ULE,
            CompareOp::Gt => IntPredicate::UGT,
            CompareOp::Ge => IntPredicate::UGE,
        };
        builder.build_int_compare(predicate, left.value.into_int_value(),
            right.value.into_int_value(), "native.icmp")
    } else if matches!(left.ty, NType::F32 | NType::F64) {
        let predicate = match op {
            CompareOp::Eq => inkwell::FloatPredicate::OEQ,
            CompareOp::Ne => inkwell::FloatPredicate::UNE,
            CompareOp::Lt => inkwell::FloatPredicate::OLT,
            CompareOp::Le => inkwell::FloatPredicate::OLE,
            CompareOp::Gt => inkwell::FloatPredicate::OGT,
            CompareOp::Ge => inkwell::FloatPredicate::OGE,
        };
        builder.build_float_compare(predicate, left.value.into_float_value(),
            right.value.into_float_value(), "native.fcmp")
    } else if left.ty == NType::Bool && matches!(op, CompareOp::Eq | CompareOp::Ne) {
        let predicate = if matches!(op, CompareOp::Eq) { IntPredicate::EQ } else { IntPredicate::NE };
        builder.build_int_compare(predicate, left.value.into_int_value(),
            right.value.into_int_value(), "native.bcmp")
    } else {
        return Err("Vergleich ist fuer diesen Typ nicht definiert".into());
    }.map_err(|e| e.to_string())?;
    NValue::checked(context, NType::Bool, result.into())
}




/// P2-T34 / ADR f67bfcbd §4: explizite Konversionen. `als(T, x)` ist der
/// gepruefte Standardcast; Wrap und Saettigung sind eigens benannte
/// Operationen. Bestehende als_uN/as_uN (codegen.rs) bleiben unveraendert.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum CastArt {
    /// als/as: Bereichs-, NaN-/Infinity- und Bruchteilfehler springen vor der
    /// LLVM-Konversion in den Fehlerblock.
    Geprueft,
    /// als_wrap/as_wrap: Ganzzahl -> Ganzzahl modulo 2^N (Bitmuster).
    Wrap,
    /// als_saettigend/as_saturating: auf den Zielbereich geklemmt; Float ->
    /// Ganzzahl schneidet zur Null ab, NaN bleibt ein Fehler.
    Saettigend,
}

pub(crate) fn cast_art(name: &str) -> Option<CastArt> {
    Some(match name {
        "als" | "as" => CastArt::Geprueft,
        "als_wrap" | "as_wrap" => CastArt::Wrap,
        "als_saettigend" | "as_saturating" => CastArt::Saettigend,
        _ => return None,
    })
}

/// Wertebereich eines Integertyps.
fn int_range(ty: NType) -> Option<(i128, i128)> {
    let width = int_width(ty)?;
    Some(if ty.is_signed_integer() {
        (-(1i128 << (width - 1)), (1i128 << (width - 1)) - 1)
    } else {
        (0, (1i128 << width) - 1)
    })
}

/// Konstanter Integerwert im Quelltyp, signed vorzeichenerweitert.
fn int_constant(value: &NValue<'_>) -> Option<i128> {
    int_width(value.ty)?;
    let int = value.value.into_int_value();
    if value.ty.is_signed_integer() {
        int.get_sign_extended_constant().map(i128::from)
    } else {
        int.get_zero_extended_constant().map(i128::from)
    }
}

/// ADR §4: statisch nachweisbar ungueltige gepruefte Konversion ist ein
/// Compilefehler (Konstante ausserhalb des Zielbereichs, nicht endlich,
/// nicht ganzzahlig, Float-Verengung auf Infinity).
pub(crate) fn pruefe_konstanten_cast(art: CastArt, value: &NValue<'_>, target: NType) -> Result<(), String> {
    if art != CastArt::Geprueft { return Ok(()); }
    if let (Some(c), Some((lo, hi))) = (int_constant(value), int_range(target)) {
        if c < lo || c > hi {
            return Err(format!("Konstante {c} passt nicht in {target:?} (gepruefter Cast)"));
        }
    }
    if matches!(value.ty, NType::F32 | NType::F64) {
        if let Some((f, _)) = value.value.into_float_value().get_constant() {
            if let Some((lo, hi)) = int_range(target) {
                if !f.is_finite() {
                    return Err(format!("Konstante {f} ist nicht endlich (gepruefter Cast nach {target:?})"));
                }
                if f.fract() != 0.0 {
                    return Err(format!("Konstante {f} ist nicht ganzzahlig (gepruefter Cast nach {target:?})"));
                }
                if f < lo as f64 || f >= (hi + 1) as f64 {
                    return Err(format!("Konstante {f} passt nicht in {target:?} (gepruefter Cast)"));
                }
            }
            if target == NType::F32 && f.is_finite() && (f as f32).is_infinite() {
                return Err(format!("Konstante {f} passt nicht in f32 (gepruefter Cast)"));
            }
        }
    }
    Ok(())
}

/// Ob eine Konversion ueberhaupt einen Fehlerpfad braucht; Aufrufer legen nur
/// dann einen Fehlerblock (moo_native_fehler/Trap) an. Spiegelt die Guards in
/// `cast`: Ganzzahl-Verengung/Vorzeichenwechsel (geprueft), Float -> Ganzzahl
/// (geprueft und saettigend wegen NaN), f64 -> f32 (geprueft).
pub(crate) fn cast_kann_fehlen(art: CastArt, source: NType, target: NType) -> bool {
    if source == target || art == CastArt::Wrap { return false; }
    let source_float = matches!(source, NType::F32 | NType::F64);
    let target_float = matches!(target, NType::F32 | NType::F64);
    match (int_range(source), int_range(target)) {
        (Some((smin, smax)), Some((dmin, dmax))) =>
            art == CastArt::Geprueft && (smin.max(dmin) > smin || smax.min(dmax) < smax),
        _ if source_float && int_range(target).is_some() => true,
        _ if source_float && target_float => art == CastArt::Geprueft && target == NType::F32,
        _ => false,
    }
}

/// Bedingter Sprung: bei `ok` weiter im neuen Block, sonst nach `failure`.
fn guard<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, function: FunctionValue<'ctx>,
    ok: IntValue<'ctx>, failure: BasicBlock<'ctx>, name: &str,
) -> Result<(), String> {
    let valid = context.append_basic_block(function, name);
    builder.build_conditional_branch(ok, valid, failure).map_err(|e| e.to_string())?;
    builder.position_at_end(valid);
    Ok(())
}

/// Konversion nach ADR §4. Gepruefte Fehler springen VOR der LLVM-Konversion
/// nach `failure` (Fehlervertrag des Aufrufers: moo_native_fehler oder Trap).
/// Ganzzahl -> Float rundet zum naechsten Wert (ties-to-even), f32 -> f64 ist
/// exakt; f64 -> f32 ist nur fuer endliche Werte, die unendlich wuerden, ein
/// Bereichsfehler. Wrap gibt es nur Ganzzahl -> Ganzzahl; Saettigung fuer
/// Ganzzahl -> Ganzzahl und Float -> Ganzzahl (llvm.fptosi.sat/fptoui.sat).
pub(crate) fn cast<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    module: &inkwell::module::Module<'ctx>,
    art: CastArt,
    value: NValue<'ctx>,
    target: NType,
    failure: BasicBlock<'ctx>,
) -> Result<NValue<'ctx>, String> {
    if value.value.get_type() != value.ty.llvm_type(context) {
        return Err("Nativer LLVM-Wert passt nicht zum Quelltyp".into());
    }
    let current = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
    let function = current.get_parent().ok_or("LLVM-Block ohne Funktion")?;
    if failure.get_parent() != Some(function) {
        return Err("Fehlerblock gehoert zu einer anderen Funktion".into());
    }
    let source = value.ty;
    if source == target {
        return Ok(value);
    }
    let source_int = int_width(source).is_some();
    let target_int = int_width(target).is_some();
    let source_float = matches!(source, NType::F32 | NType::F64);
    let target_float = matches!(target, NType::F32 | NType::F64);
    if source == NType::Bool && target_int {
        if art != CastArt::Geprueft {
            return Err("Wrap-/Saettigungs-Cast ist fuer bool nicht definiert; als(T, x) verwenden".into());
        }
        let result = builder.build_int_z_extend(
            value.value.into_int_value(), target.llvm_type(context).into_int_type(), "native.cast.bool",
        ).map_err(|e| e.to_string())?;
        return NValue::checked(context, target, result.into());
    }
    if source == NType::Bool || target == NType::Bool {
        return Err("Konversion nach bool ist nicht definiert; einen Vergleich verwenden".into());
    }
    if source_int && target_int {
        let int = value.value.into_int_value();
        let dst = target.llvm_type(context).into_int_type();
        let signed = source.is_signed_integer();
        let (smin, smax) = int_range(source).ok_or("Ganzzahl-Quelltyp erwartet")?;
        let (dmin, dmax) = int_range(target).ok_or("Ganzzahl-Zieltyp erwartet")?;
        let lo = smin.max(dmin);
        let hi = smax.min(dmax);
        // Konstanten im Quelltyp: Bitmuster der Quellbreite, ggf. negativ.
        let konst = |x: i128| int.get_type().const_int(x as u64, false);
        let lower = if lo > smin {
            let predicate = if signed { IntPredicate::SGE } else { IntPredicate::UGE };
            Some(builder.build_int_compare(predicate, int, konst(lo), "native.cast.lo").map_err(|e| e.to_string())?)
        } else { None };
        let upper = if hi < smax {
            let predicate = if signed { IntPredicate::SLE } else { IntPredicate::ULE };
            Some(builder.build_int_compare(predicate, int, konst(hi), "native.cast.hi").map_err(|e| e.to_string())?)
        } else { None };
        let clamped = match art {
            CastArt::Wrap => int,
            CastArt::Geprueft => {
                let ok = match (lower, upper) {
                    (Some(lower), Some(upper)) => Some(
                        builder.build_and(lower, upper, "native.cast.bereich").map_err(|e| e.to_string())?
                    ),
                    (Some(single), None) | (None, Some(single)) => Some(single),
                    (None, None) => None,
                };
                if let Some(ok) = ok {
                    guard(context, builder, function, ok, failure, "native.cast.bereich.ok")?;
                }
                int
            }
            CastArt::Saettigend => {
                let mut clamped = int;
                if let Some(lower) = lower {
                    clamped = builder.build_select(lower, clamped, konst(lo), "native.cast.sat.lo")
                        .map_err(|e| e.to_string())?.into_int_value();
                }
                if let Some(upper) = upper {
                    clamped = builder.build_select(upper, clamped, konst(hi), "native.cast.sat.hi")
                        .map_err(|e| e.to_string())?.into_int_value();
                }
                clamped
            }
        };
        let result = builder.build_int_cast_sign_flag(clamped, dst, signed, "native.cast.int")
            .map_err(|e| e.to_string())?;
        return NValue::checked(context, target, result.into());
    }
    if source_float && target_int {
        let float = value.value.into_float_value();
        let float_type = float.get_type();
        let dst = target.llvm_type(context).into_int_type();
        let signed = target.is_signed_integer();
        let (dmin, dmax) = int_range(target).ok_or("Ganzzahl-Zieltyp erwartet")?;
        match art {
            CastArt::Wrap => return Err(
                "Wrap-Cast ist nur Ganzzahl -> Ganzzahl definiert; fuer Float als(T, x) oder als_saettigend(T, x) verwenden".into()
            ),
            CastArt::Saettigend => {
                let ordered = builder.build_float_compare(inkwell::FloatPredicate::ORD, float, float, "native.cast.ord")
                    .map_err(|e| e.to_string())?;
                guard(context, builder, function, ordered, failure, "native.cast.sat.ok")?;
                let name = if signed { "llvm.fptosi.sat" } else { "llvm.fptoui.sat" };
                let intrinsic = inkwell::intrinsics::Intrinsic::find(name).ok_or("llvm.fptosi.sat fehlt")?;
                let declaration = intrinsic.get_declaration(module, &[dst.into(), float_type.into()])
                    .ok_or("Deklaration von llvm.fptosi.sat fehlt")?;
                let call = builder.build_call(declaration, &[float.into()], "native.cast.sat")
                    .map_err(|e| e.to_string())?.try_as_basic_value();
                return match call {
                    inkwell::values::ValueKind::Basic(result) => NValue::checked(context, target, result),
                    _ => Err("Saettigende Konversion liefert keinen Wert".into()),
                };
            }
            CastArt::Geprueft => {
                // -2^(N-1), 0, 2^(N-1) und 2^N sind in f32/f64 exakt darstellbar.
                let minimum = float_type.const_float(dmin as f64);
                let limit = float_type.const_float((dmax + 1) as f64);
                let not_below = builder.build_float_compare(inkwell::FloatPredicate::OGE, float, minimum, "native.cast.ge")
                    .map_err(|e| e.to_string())?;
                let below_limit = builder.build_float_compare(inkwell::FloatPredicate::OLT, float, limit, "native.cast.lt")
                    .map_err(|e| e.to_string())?;
                // NaN scheitert an OGE, +/-Infinity an einer der beiden Grenzen.
                let in_range = builder.build_and(not_below, below_limit, "native.cast.bereich")
                    .map_err(|e| e.to_string())?;
                guard(context, builder, function, in_range, failure, "native.cast.bereich.ok")?;
                let integer = if signed {
                    builder.build_float_to_signed_int(float, dst, "native.cast.fptosi")
                } else {
                    builder.build_float_to_unsigned_int(float, dst, "native.cast.fptoui")
                }.map_err(|e| e.to_string())?;
                let back = if signed {
                    builder.build_signed_int_to_float(integer, float_type, "native.cast.zurueck")
                } else {
                    builder.build_unsigned_int_to_float(integer, float_type, "native.cast.zurueck")
                }.map_err(|e| e.to_string())?;
                let integral = builder.build_float_compare(inkwell::FloatPredicate::OEQ, back, float, "native.cast.ganzzahlig")
                    .map_err(|e| e.to_string())?;
                guard(context, builder, function, integral, failure, "native.cast.ganzzahlig.ok")?;
                return NValue::checked(context, target, integer.into());
            }
        }
    }
    if art != CastArt::Geprueft {
        return Err("Wrap-/Saettigungs-Cast nach Float ist nicht definiert; als(T, x) verwenden".into());
    }
    if source_int && target_float {
        let float_type = target.llvm_type(context).into_float_type();
        let int = value.value.into_int_value();
        let result = if source.is_signed_integer() {
            builder.build_signed_int_to_float(int, float_type, "native.cast.sitofp")
        } else {
            builder.build_unsigned_int_to_float(int, float_type, "native.cast.uitofp")
        }.map_err(|e| e.to_string())?;
        return NValue::checked(context, target, result.into());
    }
    if source_float && target_float {
        let float = value.value.into_float_value();
        let float_type = target.llvm_type(context).into_float_type();
        if target == NType::F64 {
            let result = builder.build_float_ext(float, float_type, "native.cast.fpext").map_err(|e| e.to_string())?;
            return NValue::checked(context, target, result.into());
        }
        let narrowed = builder.build_float_trunc(float, float_type, "native.cast.fptrunc").map_err(|e| e.to_string())?;
        // Nur ein endlicher Wert, dessen korrekt gerundetes Ergebnis unendlich
        // ist, scheitert; NaN und Infinity bleiben erhalten.
        let source_type = float.get_type();
        let finite_hi = builder.build_float_compare(
            inkwell::FloatPredicate::OLT, float, source_type.const_float(f64::INFINITY), "native.cast.endlich.hi",
        ).map_err(|e| e.to_string())?;
        let finite_lo = builder.build_float_compare(
            inkwell::FloatPredicate::OGT, float, source_type.const_float(f64::NEG_INFINITY), "native.cast.endlich.lo",
        ).map_err(|e| e.to_string())?;
        let finite = builder.build_and(finite_hi, finite_lo, "native.cast.endlich").map_err(|e| e.to_string())?;
        let pos = builder.build_float_compare(
            inkwell::FloatPredicate::OEQ, narrowed, float_type.const_float(f64::INFINITY), "native.cast.pos_inf",
        ).map_err(|e| e.to_string())?;
        let neg = builder.build_float_compare(
            inkwell::FloatPredicate::OEQ, narrowed, float_type.const_float(f64::NEG_INFINITY), "native.cast.neg_inf",
        ).map_err(|e| e.to_string())?;
        let infinite = builder.build_or(pos, neg, "native.cast.unendlich").map_err(|e| e.to_string())?;
        let overflow = builder.build_and(finite, infinite, "native.cast.ueberlauf").map_err(|e| e.to_string())?;
        let ok = builder.build_not(overflow, "native.cast.verengung.ok").map_err(|e| e.to_string())?;
        guard(context, builder, function, ok, failure, "native.cast.verengung.weiter")?;
        return NValue::checked(context, target, narrowed.into());
    }
    Err(format!("Konversion {source:?} -> {target:?} ist nicht definiert"))
}
