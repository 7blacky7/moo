//! LLVM-Bausteine fuer native Integer-Arithmetik (P2-T11).
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
    /// P2-T58 (Plan f40b5980 §2.1): a ** b, Wrap modulo 2^N wie *, 0 ** 0 = 1;
    /// negativer Exponent (nur signed) ist ein Fehler vor der Rechnung.
    Pow,
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
        // P2-T58: nur signed kann fehlschlagen (negativer Exponent).
        IntOp::Pow if signed => {
            Some(builder.build_int_compare(
                IntPredicate::SLT, rhs, rhs.get_type().const_zero(), "native.pow.negativ",
            ).map_err(|e| e.to_string())?)
        }
        _ => None,
    })
}

/// P2-T58 (Plan f40b5980 §3.1): a ** b als Quadrieren-und-Multiplizieren-Schleife
/// inline (kein Runtime-Symbol, bare-faehig). Exponent ist hier >= 0 bzw.
/// unsigned; mul ohne nsw/nuw = Wrap modulo 2^N wie IntOp::Mul.
fn int_pow<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    function: FunctionValue<'ctx>,
    basis: IntValue<'ctx>,
    exponent: IntValue<'ctx>,
) -> Result<IntValue<'ctx>, String> {
    let e = |e: inkwell::builder::BuilderError| e.to_string();
    let ty = basis.get_type();
    let start = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
    let kopf = context.append_basic_block(function, "native.pow.kopf");
    let koerper = context.append_basic_block(function, "native.pow.koerper");
    let ende = context.append_basic_block(function, "native.pow.ende");
    builder.build_unconditional_branch(kopf).map_err(e)?;
    builder.position_at_end(kopf);
    let ergebnis = builder.build_phi(ty, "native.pow.ergebnis").map_err(e)?;
    let faktor = builder.build_phi(ty, "native.pow.basis").map_err(e)?;
    let rest = builder.build_phi(ty, "native.pow.exponent").map_err(e)?;
    let rest_wert = rest.as_basic_value().into_int_value();
    let fertig = builder.build_int_compare(IntPredicate::EQ, rest_wert, ty.const_zero(), "native.pow.fertig").map_err(e)?;
    builder.build_conditional_branch(fertig, ende, koerper).map_err(e)?;
    builder.position_at_end(koerper);
    let ergebnis_wert = ergebnis.as_basic_value().into_int_value();
    let faktor_wert = faktor.as_basic_value().into_int_value();
    let bit = builder.build_and(rest_wert, ty.const_int(1, false), "native.pow.bit").map_err(e)?;
    let ungerade = builder.build_int_compare(IntPredicate::NE, bit, ty.const_zero(), "native.pow.ungerade").map_err(e)?;
    let mal = builder.build_int_mul(ergebnis_wert, faktor_wert, "native.pow.mal").map_err(e)?;
    let neues_ergebnis = builder.build_select(ungerade, mal, ergebnis_wert, "native.pow.neu").map_err(e)?.into_int_value();
    let neuer_faktor = builder.build_int_mul(faktor_wert, faktor_wert, "native.pow.quadrat").map_err(e)?;
    let neuer_rest = builder.build_right_shift(rest_wert, ty.const_int(1, false), false, "native.pow.halb").map_err(e)?;
    builder.build_unconditional_branch(kopf).map_err(e)?;
    ergebnis.add_incoming(&[(&ty.const_int(1, false), start), (&neues_ergebnis, koerper)]);
    faktor.add_incoming(&[(&basis, start), (&neuer_faktor, koerper)]);
    rest.add_incoming(&[(&exponent, start), (&neuer_rest, koerper)]);
    builder.position_at_end(ende);
    Ok(ergebnis_wert)
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
        IntOp::Pow => {
            let function = builder.get_insert_block().and_then(|b| b.get_parent()).ok_or("LLVM-Block ohne Funktion")?;
            let result = int_pow(context, builder, function, lhs, rhs)?;
            return NValue::checked(context, left.ty, result.into());
        }
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
        // P2-T58: potenzierbar(a, b) = Exponent >= 0 (unsigned immer wahr).
        "potenzierbar" | "can_pow" => Some(IntOp::Pow),
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
    let invalid = match int_invalid(
        builder, op, left.value.into_int_value(), right.value.into_int_value(),
        left.ty.is_signed_integer(), width,
    )? {
        Some(invalid) => invalid,
        // P2-T58: unsigned ** kann nicht fehlschlagen -> potenzierbar ist wahr.
        None if matches!(op, IntOp::Pow) => context.bool_type().const_zero(),
        None => return Err("Operation ohne Fehlerfall braucht keine Vorpruefung".into()),
    };
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
    /// P2-T58 (Plan f40b5980 §2.2/§2.3): Float-% wie C-fmod, Float-** wie
    /// C99-pow. Beide laufen ueber float_bibliothek, nicht float_binary.
    Rem, Pow,
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
        FloatOp::Rem | FloatOp::Pow => {
            return Err("Float-% und Float-** laufen ueber float_bibliothek".into());
        }
    }.map_err(|e| e.to_string())?;
    NValue::checked(context, left.ty, result.into())
}

/// P2-T58 D2c (Plan f40b5980 §3.2): Float-% und Float-** als Aufruf der
/// freistehenden Moo-Runtime compiler/runtime/moo_native_math.c
/// (moo_native_fmod/fmodf, moo_native_pow/powf). Bewusst kein LLVM-frem und
/// kein llvm.pow: beide werden zu fmod/pow aus libm abgesenkt, die es im
/// Bare-Ziel nicht gibt. Die Deklaration entsteht erst beim ersten Bedarf
/// (nounwind, willreturn, memory(none)); Module ohne Float-%/** bleiben
/// unveraendert. Kein Moo-Fehlerpfad: Sonderfaelle liefern IEEE-Werte.
pub(crate) fn float_bibliothek<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    module: &inkwell::module::Module<'ctx>,
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
    let einfach = left.ty == NType::F32;
    let name = match (op, einfach) {
        (FloatOp::Rem, false) => "moo_native_fmod",
        (FloatOp::Rem, true) => "moo_native_fmodf",
        (FloatOp::Pow, false) => "moo_native_pow",
        (FloatOp::Pow, true) => "moo_native_powf",
        _ => return Err("float_bibliothek: nur Float-% und Float-**".into()),
    };
    let float_t = left.ty.llvm_type(context).into_float_type();
    let signatur = float_t.fn_type(&[float_t.into(), float_t.into()], false);
    let funktion = bibliothek_funktion(context, module, name, signatur)?;
    let lhs = left.value.into_float_value();
    let rhs = right.value.into_float_value();
    let aufruf = builder.build_call(funktion, &[lhs.into(), rhs.into()], "native.fbib")
        .map_err(|e| e.to_string())?;
    let wert = match aufruf.try_as_basic_value() {
        inkwell::values::ValueKind::Basic(wert) => wert,
        _ => return Err(format!("{name} liefert keinen Wert")),
    };
    NValue::checked(context, left.ty, wert)
}

/// Deklaration einer reinen moo_native_math-Funktion beim ersten Bedarf
/// (nounwind, willreturn, memory(none)); Module ohne Bedarf bleiben unveraendert.
fn bibliothek_funktion<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
    name: &str,
    signatur: inkwell::types::FunctionType<'ctx>,
) -> Result<inkwell::values::FunctionValue<'ctx>, String> {
    match module.get_function(name) {
        Some(f) if f.get_type() == signatur => Ok(f),
        Some(_) => Err(format!("{name} ist im Modul mit anderer Signatur deklariert")),
        None => {
            use inkwell::attributes::{Attribute, AttributeLoc};
            let f = module.add_function(name, signatur, None);
            for art in ["nounwind", "willreturn"] {
                f.add_attribute(AttributeLoc::Function,
                    context.create_enum_attribute(Attribute::get_named_enum_kind_id(art), 0));
            }
            // memory(none): reine Funktion ohne Speicherzugriff (LLVM >= 16).
            f.add_attribute(AttributeLoc::Function,
                context.create_enum_attribute(Attribute::get_named_enum_kind_id("memory"), 0));
            Ok(f)
        }
    }
}

/// P2-T89 (Freigabe #38178 O2): eingebaute Winkelfunktionen im @nativ-Koerper.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum TrigArt {
    Sin,
    Cos,
    Tan,
}

/// Gleiche Namen wie die dynamische Ebene (codegen.rs sinus/sin, cosinus/cos,
/// tan) plus die korrekte deutsche Schreibweise kosinus/tangens.
pub(crate) fn trig_art(name: &str) -> Option<TrigArt> {
    match name {
        "sinus" | "sin" => Some(TrigArt::Sin),
        "kosinus" | "cosinus" | "cos" => Some(TrigArt::Cos),
        "tangens" | "tan" => Some(TrigArt::Tan),
        _ => None,
    }
}

/// sin/cos/tan (Bogenmass) ueber moo_native_math.c auf jedem Ziel (hosted,
/// Bare, UEFI): eine Semantik, gleiche Bits auf allen Plattformen, keine libm.
/// Kein Fehlerpfad: Sonderfaelle liefern IEEE-Werte (Inf/NaN -> NaN).
pub(crate) fn float_trig<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    module: &inkwell::module::Module<'ctx>,
    art: TrigArt,
    wert: NValue<'ctx>,
) -> Result<NValue<'ctx>, String> {
    if !matches!(wert.ty, NType::F32 | NType::F64) || wert.value.get_type() != wert.ty.llvm_type(context) {
        return Err("Winkelfunktion erwartet einen nativen Float-Wert (f32/f64)".into());
    }
    let einfach = wert.ty == NType::F32;
    let name = match (art, einfach) {
        (TrigArt::Sin, false) => "moo_native_sin",
        (TrigArt::Sin, true) => "moo_native_sinf",
        (TrigArt::Cos, false) => "moo_native_cos",
        (TrigArt::Cos, true) => "moo_native_cosf",
        (TrigArt::Tan, false) => "moo_native_tan",
        (TrigArt::Tan, true) => "moo_native_tanf",
    };
    let float_t = wert.ty.llvm_type(context).into_float_type();
    let funktion = bibliothek_funktion(context, module, name, float_t.fn_type(&[float_t.into()], false))?;
    let aufruf = builder.build_call(funktion, &[wert.value.into_float_value().into()], "native.trig")
        .map_err(|e| e.to_string())?;
    let ergebnis = match aufruf.try_as_basic_value() {
        inkwell::values::ValueKind::Basic(ergebnis) => ergebnis,
        _ => return Err(format!("{name} liefert keinen Wert")),
    };
    NValue::checked(context, wert.ty, ergebnis)
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


/// P2-T125: Bitoperationen mit totaler Zaehler-/Modulo-Semantik. Die
/// bestehenden +,-,* und als_wrap bleiben unveraendert.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum BitArt { Links, Rechts, MulHoch, AddUebertrag, Gleich, Auswahl }

impl BitArt {
    pub(crate) fn daten_index(self) -> usize { usize::from(self == Self::Auswahl) }
    pub(crate) fn liefert_bool(self) -> bool { matches!(self, Self::AddUebertrag | Self::Gleich) }
    pub(crate) fn aritaet(self) -> usize { if self == Self::Auswahl { 3 } else { 2 } }
}

pub(crate) fn bit_art(name: &str) -> Option<BitArt> {
    Some(match name {
        "rotieren_links" | "rotate_left" => BitArt::Links,
        "rotieren_rechts" | "rotate_right" => BitArt::Rechts,
        "mul_hoch" | "mul_high" => BitArt::MulHoch,
        "add_uebertrag" | "add_carry" => BitArt::AddUebertrag,
        "konstantzeit_gleich" | "ct_equal" => BitArt::Gleich,
        "konstantzeit_auswahl" | "ct_select" => BitArt::Auswahl,
        _ => return None,
    })
}

pub(crate) fn bit_operation<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>,
    module: &inkwell::module::Module<'ctx>, art: BitArt, args: &[NValue<'ctx>],
) -> Result<NValue<'ctx>, String> {
    if args.len() != art.aritaet() {
        return Err(format!("Native Bitoperation erwartet {} Argumente", art.aritaet()));
    }
    let data = &args[art.daten_index()];
    let width = int_width(data.ty).ok_or("Native Bitoperation braucht einen Ganzzahltyp i8..u64")?;
    if args.iter().any(|v| v.value.get_type() != v.ty.llvm_type(context)) {
        return Err("Bitoperation: LLVM-Wert passt nicht zum Quelltyp".into());
    }
    let a = data.value.into_int_value();
    if art == BitArt::Auswahl {
        if args[0].ty != NType::Bool || args[2].ty != data.ty {
            return Err("konstantzeit_auswahl(bedingung, a, b) braucht bool und zwei gleiche Ganzzahltypen".into());
        }
        let i64t = context.i64_type();
        let extend = |v| if width == 64 { Ok(v) } else {
            builder.build_int_z_extend(v, i64t, "ct.extend").map_err(|e| e.to_string())
        };
        let a = extend(a)?;
        let b = extend(args[2].value.into_int_value())?;
        let mask = builder.build_int_s_extend(args[0].value.into_int_value(), i64t, "ct.mask")
            .map_err(|e| e.to_string())?;
        let triple = module.get_triple();
        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?;
        // Die opaque Barriere verhindert auch nach Inlining, dass LLVM die
        // Auswahl in geheimnisabhaengigen Kontrollfluss umformt.
        let (code, constraints) = if triple.starts_with("x86_64") {
            ("xor $2, $0\nand $3, $0\nxor $2, $0", "=&r,0,r,r,~{flags}")
        } else if triple.starts_with("aarch64") || triple.starts_with("arm64") {
            ("eor $0, $1, $2\nand $0, $0, $3\neor $0, $0, $2", "=&r,r,r,r")
        } else {
            return Err(format!("konstantzeit_auswahl: Ziel '{triple}' ist noch nicht freigegeben (x86_64/AArch64)"));
        };
        let ty = i64t.fn_type(&[i64t.into(), i64t.into(), i64t.into()], false);
        let asm = context.create_inline_asm(ty, code.into(), constraints.into(), true, false, None, false);
        let call = builder.build_indirect_call(ty, asm, &[a.into(), b.into(), mask.into()], "ct.select")
            .map_err(|e| e.to_string())?;
        let value = match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v.into_int_value(),
            _ => return Err("Konstantzeit-Auswahl ohne Ergebnis".into()),
        };
        let value = if width == 64 { value } else {
            builder.build_int_truncate(value, data.value.into_int_value().get_type(), "ct.result")
                .map_err(|e| e.to_string())?
        };
        return NValue::checked(context, data.ty, value.into());
    }
    let count_width = int_width(args[1].ty).ok_or("Bitoperation braucht ganzzahlige Operanden")?;
    let b = args[1].value.into_int_value();
    if matches!(art, BitArt::Links | BitArt::Rechts) {
        let count = if count_width < width {
            builder.build_int_z_extend(b, a.get_type(), "rotate.count").map_err(|e| e.to_string())?
        } else if count_width > width {
            builder.build_int_truncate(b, a.get_type(), "rotate.count").map_err(|e| e.to_string())?
        } else { b };
        let name = if art == BitArt::Links { "llvm.fshl" } else { "llvm.fshr" };
        let intrinsic = inkwell::intrinsics::Intrinsic::find(name).ok_or("LLVM-Funnelshift fehlt")?;
        let function = intrinsic.get_declaration(module, &[a.get_type().into()])
            .ok_or("LLVM-Rotationssignatur fehlt")?;
        let call = builder.build_call(function, &[a.into(), a.into(), count.into()], "native.rotate")
            .map_err(|e| e.to_string())?;
        let value = match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(v) => v,
            _ => return Err("Rotation ohne Ergebnis".into()),
        };
        return NValue::checked(context, data.ty, value);
    }
    if args[1].ty != data.ty {
        return Err("Native Bitoperation braucht zwei gleiche Ganzzahltypen".into());
    }
    match art {
        BitArt::Gleich => {
            let equal = builder.build_int_compare(IntPredicate::EQ, a, b, "ct.equal").map_err(|e| e.to_string())?;
            NValue::checked(context, NType::Bool, equal.into())
        }
        BitArt::AddUebertrag | BitArt::MulHoch if data.ty.is_signed_integer() =>
            Err("mul_hoch/add_uebertrag erwarten vorzeichenlose Ganzzahlen u8/u16/u32/u64".into()),
        BitArt::AddUebertrag => {
            let sum = builder.build_int_add(a, b, "carry.sum").map_err(|e| e.to_string())?;
            let carry = builder.build_int_compare(IntPredicate::ULT, sum, a, "carry.bit").map_err(|e| e.to_string())?;
            NValue::checked(context, NType::Bool, carry.into())
        }
        BitArt::MulHoch => {
            let wide = context.custom_width_int_type(
                std::num::NonZeroU32::new(width * 2).ok_or("Ungueltige Produktbreite")?,
            )?;
            let a = builder.build_int_z_extend(a, wide, "mul.wide.a").map_err(|e| e.to_string())?;
            let b = builder.build_int_z_extend(b, wide, "mul.wide.b").map_err(|e| e.to_string())?;
            let product = builder.build_int_mul(a, b, "mul.wide").map_err(|e| e.to_string())?;
            let high = builder.build_right_shift(product, wide.const_int(width.into(), false), false, "mul.high")
                .map_err(|e| e.to_string())?;
            let value = builder.build_int_truncate(high, data.value.into_int_value().get_type(), "mul.high.word")
                .map_err(|e| e.to_string())?;
            NValue::checked(context, data.ty, value.into())
        }
        _ => Err("Interne Bitoperationsart".into()),
    }
}
