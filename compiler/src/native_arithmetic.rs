//! LLVM-Bausteine fuer native Integer-Arithmetik (P2-T11).
//! Noch nicht an den nativen Ausdrucks- oder Fehlerpfad angebunden.
//! Der Aufrufer muss fuer Div/Rem/Shift einen Fehlerblock derselben Funktion
//! uebergeben. Dieser Block muss den jeweiligen Fehlervertrag umsetzen und darf
//! nicht zur ungueltigen LLVM-Operation zurueckfallen.

use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::values::{BasicValue, IntValue};
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
    let invalid: Option<IntValue<'ctx>> = match op {
        IntOp::Div | IntOp::Rem => {
            let zero = builder.build_int_compare(
                IntPredicate::EQ, rhs, rhs.get_type().const_zero(), "native.div.zero",
            ).map_err(|e| e.to_string())?;
            if left.ty.is_signed_integer() {
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
            // Unsigned-Vergleich erkennt auch negative signed Counts als gross.
            Some(builder.build_int_compare(
                IntPredicate::UGE, rhs, rhs.get_type().const_int(width as u64, false),
                "native.shift.invalid",
            ).map_err(|e| e.to_string())?)
        }
        _ => None,
    };
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


