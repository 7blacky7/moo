//! Native skalare Werte und Slots. Kein MooValue-Ownership in dieser Ebene.
//! Dynamische Werte bleiben im bestehenden Codegen-Pfad; Uebergaenge erfolgen
//! ausschliesslich ueber explizite, noch zu implementierende Adapter.

use std::collections::HashMap;

use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::types::{BasicMetadataTypeEnum, BasicType, BasicTypeEnum, FunctionType};
use inkwell::values::{BasicValueEnum, FunctionValue, PointerValue};

use crate::ast::{BinOp, Expr, Stmt, TypeKind, TypeRef, UnaryOpKind};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum NType {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64, Bool,
}

impl NType {
    pub(crate) fn from_name(name: &str) -> Option<Self> {
        Some(match name {
            "i8" => Self::I8, "i16" => Self::I16,
            "i32" => Self::I32, "i64" => Self::I64,
            "u8" => Self::U8, "u16" => Self::U16,
            "u32" => Self::U32, "u64" => Self::U64,
            "f32" => Self::F32, "f64" => Self::F64,
            "bool" => Self::Bool,
            _ => return None,
        })
    }

    /// Erkennt im ersten Slice nur explizit unterstuetzte skalare Typen.
    pub(crate) fn from_type_ref(ty: &TypeRef) -> Result<Self, String> {
        let value = match &ty.kind {
            TypeKind::Name(name) => Self::from_name(name),
            _ => None,
        };
        value.ok_or_else(|| format!(
            "Nicht unterstuetzter nativer Skalertyp bei {}:{}", ty.line, ty.column
        ))
    }

    pub(crate) fn llvm_type<'ctx>(self, context: &'ctx Context) -> BasicTypeEnum<'ctx> {
        match self {
            Self::I8 | Self::U8 => context.i8_type().into(),
            Self::I16 | Self::U16 => context.i16_type().into(),
            Self::I32 | Self::U32 => context.i32_type().into(),
            Self::I64 | Self::U64 => context.i64_type().into(),
            Self::F32 => context.f32_type().into(),
            Self::F64 => context.f64_type().into(),
            Self::Bool => context.bool_type().into(),
        }
    }

    /// Nullen und Vorzeichen werden auf mathematischem Wert geprueft;
    /// bei negiertem Literal ist auch MIN darstellbar.
    pub(crate) fn integer_literal<'ctx>(
        self,
        context: &'ctx Context,
        text: &str,
        negative: bool,
    ) -> Result<NValue<'ctx>, String> {
        let width: u32 = match self {
            Self::I8 | Self::U8 => 8,
            Self::I16 | Self::U16 => 16,
            Self::I32 | Self::U32 => 32,
            Self::I64 | Self::U64 => 64,
            _ => return Err(format!("Ganzzahlliteral fuer {self:?} unzulaessig")),
        };
        let (digits, radix) = if let Some(s) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
            (s, 16)
        } else if let Some(s) = text.strip_prefix("0b").or_else(|| text.strip_prefix("0B")) {
            (s, 2)
        } else if let Some(s) = text.strip_prefix("0o").or_else(|| text.strip_prefix("0O")) {
            (s, 8)
        } else {
            (text, 10)
        };
        if digits.is_empty() || !digits.chars().all(|c| c.to_digit(radix).is_some()) {
            return Err(format!("Ungueltiges Ganzzahlliteral '{text}'"));
        }
        let magnitude = u128::from_str_radix(digits, radix)
            .map_err(|_| format!("Ganzzahlliteral '{text}' ist zu gross"))?;
        let signed = self.is_signed_integer();
        let limit = if signed {
            if negative { 1u128 << (width - 1) } else { (1u128 << (width - 1)) - 1 }
        } else {
            if negative && magnitude != 0 {
                return Err(format!("Negatives Literal fuer {self:?}"));
            }
            (1u128 << width) - 1
        };
        if magnitude > limit {
            return Err(format!("Ganzzahlliteral '{text}' passt nicht in {self:?}"));
        }
        let bits = if negative { 0u64.wrapping_sub(magnitude as u64) } else { magnitude as u64 };
        let llvm_type = self.llvm_type(context).into_int_type();
        NValue::checked(context, self, llvm_type.const_int(bits, false).into())
    }

    /// Rundet direkt zur Zielbreite. Das zweite Rueckgabefeld meldet ein
    /// mathematisch von Null verschiedenes Literal, das auf Null rundete.
    /// Der Aufrufer muss dafuer die vorgeschriebene Diagnose ausgeben.
    pub(crate) fn float_literal<'ctx>(
        self,
        context: &'ctx Context,
        text: &str,
        negative: bool,
    ) -> Result<(NValue<'ctx>, bool), String> {
        if !text.as_bytes().first().is_some_and(u8::is_ascii_digit)
            || !text.bytes().all(|c| c.is_ascii_digit() || matches!(c, b'.' | b'e' | b'E' | b'+' | b'-'))
        {
            return Err(format!("Ungueltiges Floatliteral '{text}'"));
        }
        let mantissa = text.split(['e', 'E']).next().unwrap_or("");
        let nonzero = mantissa.bytes().any(|c| matches!(c, b'1'..=b'9'));
        let (value, underflow) = match self {
            Self::F32 => {
                let value: f32 = text.parse().map_err(|_| format!("Ungueltiges Floatliteral '{text}'"))?;
                if !value.is_finite() { return Err(format!("Floatliteral '{text}' passt nicht in f32")); }
                let value = if negative { -value } else { value };
                (context.f32_type().const_float(f64::from(value)).into(), nonzero && value == 0.0)
            }
            Self::F64 => {
                let value: f64 = text.parse().map_err(|_| format!("Ungueltiges Floatliteral '{text}'"))?;
                if !value.is_finite() { return Err(format!("Floatliteral '{text}' passt nicht in f64")); }
                let value = if negative { -value } else { value };
                (context.f64_type().const_float(value).into(), nonzero && value == 0.0)
            }
            _ => return Err(format!("Floatliteral fuer {self:?} unzulaessig")),
        };
        Ok((NValue::checked(context, self, value)?, underflow))
    }

    pub(crate) fn is_signed_integer(self) -> bool {
        matches!(self, Self::I8 | Self::I16 | Self::I32 | Self::I64)
    }
}

/// Signatur eines nativen Koerpers. None bedeutet void als Rueckgabe,
/// niemals als Parameter oder gespeicherter Wert.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct NSignature {
    pub(crate) params: Vec<NType>,
    pub(crate) result: Option<NType>,
}

impl NSignature {
    /// Die native Ebene benoetigt fuer jeden Parameter und die Rueckgabe
    /// eine Annotation. Dynamische Funktionen verwenden diese Pruefung nicht.
    pub(crate) fn from_annotations(
        params: &[Option<TypeRef>],
        result: Option<&TypeRef>,
    ) -> Result<Self, String> {
        let params = params.iter().enumerate().map(|(index, ty)| {
            let ty = ty.as_ref().ok_or_else(|| format!(
                "Typ des nativen Parameters {} fehlt", index + 1
            ))?;
            NType::from_type_ref(ty)
        }).collect::<Result<Vec<_>, _>>()?;
        let result = result.ok_or("Rueckgabetyp der nativen Funktion fehlt")?;
        let result = match &result.kind {
            TypeKind::Name(name) if name == "void" => None,
            _ => Some(NType::from_type_ref(result)?),
        };
        Ok(Self { params, result })
    }

    pub(crate) fn llvm_type<'ctx>(&self, context: &'ctx Context) -> FunctionType<'ctx> {
        let params: Vec<BasicMetadataTypeEnum<'ctx>> = self.params.iter()
            .map(|ty| ty.llvm_type(context).into())
            .collect();
        match self.result {
            Some(ty) => ty.llvm_type(context).fn_type(&params, false),
            None => context.void_type().fn_type(&params, false),
        }
    }
}

/// Native pointer parameters retain source identity even though LLVM pointers
/// are opaque. This signature does not itself enable calls or dereferences.
#[derive(Clone, Debug)]
pub(crate) enum NativeParameter {
    Scalar(NType),
    Pointer {
        identity: crate::native_type_id::NativeTypeId,
        source: TypeRef,
    },
}

#[derive(Clone, Debug)]
pub(crate) struct NativePointerSignature {
    pub(crate) params: Vec<NativeParameter>,
    pub(crate) result: Option<NType>,
}

impl NativePointerSignature {
    pub(crate) fn from_annotations(
        params: &[Option<TypeRef>], result: Option<&TypeRef>,
        nominal: &crate::native_type_id::NominalTypes,
    ) -> Result<Self, String> {
        let mut has_pointer = false;
        let mut native_params = Vec::with_capacity(params.len());
        for (index, param) in params.iter().enumerate() {
            let ty = param.as_ref().ok_or_else(|| format!(
                "Typ des nativen Parameters {} fehlt", index + 1
            ))?;
            match &ty.kind {
                TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger" => {
                    let identity = crate::native_type_id::resolve_type(ty, nominal)?;
                    if !matches!(&identity, crate::native_type_id::NativeTypeId::Pointer { address_space: 0, .. }) {
                        return Err("Nativer Zeigerparameter hat einen unzulässigen Adressraum".into());
                    }
                    has_pointer = true;
                    native_params.push(NativeParameter::Pointer { identity, source: ty.clone() });
                }
                _ => native_params.push(NativeParameter::Scalar(NType::from_type_ref(ty)?)),
            }
        }
        if !has_pointer {
            return Err("Pointer-Signatur benötigt mindestens einen Zeigerparameter".into());
        }
        let result = result.ok_or("Rueckgabetyp der nativen Funktion fehlt")?;
        let result = match &result.kind {
            TypeKind::Name(name) if name == "void" => None,
            _ => Some(NType::from_type_ref(result)?),
        };
        Ok(Self { params: native_params, result })
    }

    pub(crate) fn llvm_type<'ctx>(&self, context: &'ctx Context) -> FunctionType<'ctx> {
        let params: Vec<BasicMetadataTypeEnum<'ctx>> = self.params.iter().map(|param| match param {
            NativeParameter::Scalar(ty) => ty.llvm_type(context).into(),
            NativeParameter::Pointer { .. } => context.ptr_type(inkwell::AddressSpace::default()).into(),
        }).collect();
        match self.result {
            Some(ty) => ty.llvm_type(context).fn_type(&params, false),
            None => context.void_type().fn_type(&params, false),
        }
    }
}

/// Ein SSA-Skalarwert. Signedness bleibt erhalten, obwohl LLVM sie im
/// Integer-Typ selbst nicht kodiert. Dieser Wert besitzt keine MooValue-Referenz.
#[derive(Clone, Copy, Debug)]
pub(crate) struct NValue<'ctx> {
    pub(crate) ty: NType,
    pub(crate) value: BasicValueEnum<'ctx>,
}

impl<'ctx> NValue<'ctx> {
    pub(crate) fn checked(
        context: &'ctx Context,
        ty: NType,
        value: BasicValueEnum<'ctx>,
    ) -> Result<Self, String> {
        if value.get_type() != ty.llvm_type(context) {
            return Err(format!("Nativer LLVM-Wert passt nicht zu {ty:?}"));
        }
        Ok(Self { ty, value })
    }
}

/// P2-T34: Fehlervertrag fehlerfaehiger nativer Operationen (ADR f67bfcbd §3,
/// Thought 788e6459). Hosted: `moo_native_fehler(text)` erzeugt einen fangbaren
/// Moo-Fehler (versuche: prueft nach jedem Statement das Fehlerflag), danach
/// kehrt die native Funktion mit einem Nullwert zurueck; ohne aktiven Handler
/// beendet moo_throw den Prozess. Bare/WASM (`trap`): llvm.trap, keine Rueckkehr.
#[derive(Clone, Copy)]
pub(crate) struct NativeFehler<'m, 'ctx> {
    pub(crate) module: &'m inkwell::module::Module<'ctx>,
    pub(crate) trap: bool,
    /// P2-T84: hosted C-Export (@einstieg): statt nacktem Trap moo_native_abbruch
    /// (Runtime: Meldung + RC 1); nur zusammen mit trap.
    pub(crate) abbruch: bool,
    /// Name der nativen Funktion fuer die Fehlermeldung.
    pub(crate) funktion: &'m str,
}

/// Legt den Fehlerblock einer Operation in der aktuellen Funktion an; der
/// Builder steht danach wieder im Ausgangsblock. `moo_native_fehler` wird erst
/// beim ersten Bedarf deklariert, damit Module ohne fehlerfaehige Operationen
/// ihr bisheriges IR behalten (Legacy-IR-Gate).
fn fehlerblock<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    fehler: NativeFehler<'_, 'ctx>,
    kennung: &str,
    text: &str,
) -> Result<BasicBlock<'ctx>, String> {
    let current = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
    let function = current.get_parent().ok_or("LLVM-Block ohne Funktion")?;
    let block = context.append_basic_block(function, &format!("native.fehler.{kennung}"));
    builder.position_at_end(block);
    if fehler.trap {
        crate::native_cleanup::before_abort(context, builder, fehler.module)?;
    }
    if fehler.trap && fehler.abbruch {
        emit_abbruch(context, builder, fehler.module, fehler.funktion, text)?;
    } else if fehler.trap {
        let intrinsic = inkwell::intrinsics::Intrinsic::find("llvm.trap").ok_or("llvm.trap fehlt")?;
        let trap = intrinsic.get_declaration(fehler.module, &[]).ok_or("llvm.trap-Deklaration fehlt")?;
        builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
        builder.build_unreachable().map_err(|e| e.to_string())?;
    } else {
        let hook = native_fehler_hook(context, fehler.module)?;
        let message = builder.build_global_string_ptr(text, "native.fehler.text")
            .map_err(|e| e.to_string())?;
        builder.build_call(hook, &[message.as_pointer_value().into()], "")
            .map_err(|e| e.to_string())?;
        match function.get_type().get_return_type() {
            Some(ty) => { builder.build_return(Some(&ty.const_zero())).map_err(|e| e.to_string())?; }
            None => { builder.build_return(None).map_err(|e| e.to_string())?; }
        }
    }
    builder.position_at_end(current);
    Ok(block)
}

/// P2-T84 (Plan 91033131 §6): `moo_native_abbruch(text)` (noreturn) fuer Fehler
/// in hosted C-Exporten (@einstieg). Ein Moo-Fehler kann nicht ueber einen
/// fremden C-Frame weitergegeben werden. SCHWACHE Moduldefinition mit
/// llvm.trap: ein ohne Runtime gelinktes Objekt traegt kein offenes Symbol und
/// trappt wie bisher; die Moo-Runtime (moo_error.c) gibt Meldung und den
/// anstehenden Moo-Fehler aus und beendet mit RC 1.
pub(crate) fn native_abbruch_hook<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
) -> Result<FunctionValue<'ctx>, String> {
    if let Some(hook) = module.get_function("moo_native_abbruch") {
        return Ok(hook);
    }
    let ptr = context.ptr_type(inkwell::AddressSpace::default());
    let hook = module.add_function(
        "moo_native_abbruch",
        context.void_type().fn_type(&[ptr.into()], false),
        Some(inkwell::module::Linkage::WeakAny),
    );
    hook.add_attribute(
        inkwell::attributes::AttributeLoc::Function,
        context.create_enum_attribute(inkwell::attributes::Attribute::get_named_enum_kind_id("noreturn"), 0),
    );
    let entry = context.append_basic_block(hook, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    crate::native_cleanup::before_abort(context, &builder, module)?;
    let intrinsic = inkwell::intrinsics::Intrinsic::find("llvm.trap").ok_or("llvm.trap fehlt")?;
    let trap = intrinsic.get_declaration(module, &[]).ok_or("llvm.trap-Deklaration fehlt")?;
    builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
    builder.build_unreachable().map_err(|e| e.to_string())?;
    Ok(hook)
}

/// P2-T21/T57 B3 (Plan f4655d20 E5): `moo_native_rueckruf_thread(name)` am
/// Eintritt fehlbarer hosted Rueckruf-Thunks. SCHWACHE Moduldefinition (kehrt
/// sofort zurueck): runtime-freie Objekte bleiben linkbar; die starke Fassung in
/// compiler/runtime/moo_error.c bricht auf Nicht-Moo-Threads (weder Hauptthread
/// noch moo_thread_spawn) mit Diagnose + abort() ab. Erst bei Bedarf angelegt.
pub(crate) fn native_rueckruf_thread_hook<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
) -> Result<FunctionValue<'ctx>, String> {
    if let Some(hook) = module.get_function("moo_native_rueckruf_thread") {
        return Ok(hook);
    }
    let ptr = context.ptr_type(inkwell::AddressSpace::default());
    let hook = module.add_function(
        "moo_native_rueckruf_thread",
        context.void_type().fn_type(&[ptr.into()], false),
        Some(inkwell::module::Linkage::WeakAny),
    );
    let entry = context.append_basic_block(hook, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    builder.build_return(None).map_err(|e| e.to_string())?;
    Ok(hook)
}

/// P2-T84: Abbruch eines hosted C-Exports an der aktuellen Builder-Position
/// (Pure-Pfad und Systemkoerper teilen Meldung und Hook).
pub(crate) fn emit_abbruch<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    module: &inkwell::module::Module<'ctx>,
    funktion: &str,
    grund: &str,
) -> Result<(), String> {
    crate::native_cleanup::before_abort(context, builder, module)?;
    let hook = native_abbruch_hook(context, module)?;
    let text = format!(
        "Fehler in exportierter Funktion '{funktion}': {grund} - kann nicht ueber einen C-Aufrufer weitergegeben werden"
    );
    let message = builder.build_global_string_ptr(&text, "native.abbruch.text").map_err(|e| e.to_string())?;
    builder.build_call(hook, &[message.as_pointer_value().into()], "").map_err(|e| e.to_string())?;
    builder.build_unreachable().map_err(|e| e.to_string())?;
    Ok(())
}

/// P2-T84: im hosted Export nach einem Aufruf einer Moo-internen Funktion
/// (deren Fehler setzt nur das Flag und liefert 0) nicht weiterrechnen.
fn abbruch_nach_aufruf<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    fehler: NativeFehler<'_, 'ctx>,
    callee: &str,
) -> Result<(), String> {
    let hook = native_fehler_offen_hook(context, fehler.module)?;
    let flag = match builder.build_call(hook, &[], "native.fehler.offen").map_err(|e| e.to_string())?.try_as_basic_value() {
        inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
        _ => return Err("moo_native_fehler_offen liefert keinen Wert".into()),
    };
    let offen = builder.build_int_compare(
        inkwell::IntPredicate::NE, flag, context.i32_type().const_zero(), "native.fehler.gesetzt",
    ).map_err(|e| e.to_string())?;
    let abbruch = fehlerblock(context, builder, fehler, "aufruf", &format!("Fehler aus '{callee}'"))?;
    let function = builder.get_insert_block().and_then(|block| block.get_parent())
        .ok_or("Nativer Aufruf ohne Funktion")?;
    let ok = context.append_basic_block(function, "native.aufruf.ok");
    builder.build_conditional_branch(offen, abbruch, ok).map_err(|e| e.to_string())?;
    builder.position_at_end(ok);
    Ok(())
}

/// P2-T58 C2 (Plan 4500717b): `moo_native_fehler_offen() -> i32` als SCHWACHE
/// Definition (liefert 0) neben native_fehler_hook; die Moo-Runtime
/// (moo_error.c) liefert die starke Fassung mit moo_error_flag. Ohne Runtime
/// gelinkte Objekte tragen kein offenes Symbol. Erst bei Bedarf angelegt.
pub(crate) fn native_fehler_offen_hook<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
) -> Result<FunctionValue<'ctx>, String> {
    if let Some(hook) = module.get_function("moo_native_fehler_offen") {
        return Ok(hook);
    }
    let hook = module.add_function(
        "moo_native_fehler_offen",
        context.i32_type().fn_type(&[], false),
        Some(inkwell::module::Linkage::WeakAny),
    );
    let entry = context.append_basic_block(hook, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    builder.build_return(Some(&context.i32_type().const_zero())).map_err(|e| e.to_string())?;
    Ok(hook)
}

/// `moo_native_fehler(text)` beim ersten Bedarf als SCHWACHE Definition mit
/// Trap-Koerper anlegen: Die Moo-Runtime (compiler/runtime/moo_error.c) liefert
/// die starke, fangbare Fassung und gewinnt beim Link; ein ohne Runtime
/// gelinktes natives Objekt (C-Export, llvm-extract) traegt kein offenes Symbol
/// und trappt wie zuvor. Module ohne fehlerfaehige Operation bleiben unveraendert.
pub(crate) fn native_fehler_hook<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
) -> Result<FunctionValue<'ctx>, String> {
    if let Some(hook) = module.get_function("moo_native_fehler") {
        return Ok(hook);
    }
    let ptr = context.ptr_type(inkwell::AddressSpace::default());
    let hook = module.add_function(
        "moo_native_fehler",
        context.void_type().fn_type(&[ptr.into()], false),
        Some(inkwell::module::Linkage::WeakAny),
    );
    let entry = context.append_basic_block(hook, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    crate::native_cleanup::before_abort(context, &builder, module)?;
    let intrinsic = inkwell::intrinsics::Intrinsic::find("llvm.trap").ok_or("llvm.trap fehlt")?;
    let trap = intrinsic.get_declaration(module, &[]).ok_or("llvm.trap-Deklaration fehlt")?;
    builder.build_call(trap, &[], "").map_err(|e| e.to_string())?;
    builder.build_unreachable().map_err(|e| e.to_string())?;
    Ok(hook)
}

/// ADR §3: statisch nachweisbare Fehler sind Compilefehler: konstante Null als
/// Divisor/Modul, konstantes MIN/-1 bei signed Division/Rest, konstanter
/// Shiftzaehler ausserhalb 0..Breite-1 (negative Zaehler sind als unsigned gross).
fn pruefe_konstante_operanden<'ctx>(
    op: crate::native_arithmetic::IntOp, left: &NValue<'ctx>, right: &NValue<'ctx>,
) -> Result<(), String> {
    use crate::native_arithmetic::IntOp;
    let width: u32 = match left.ty {
        NType::I8 | NType::U8 => 8, NType::I16 | NType::U16 => 16,
        NType::I32 | NType::U32 => 32, NType::I64 | NType::U64 => 64,
        _ => return Ok(()),
    };
    let mask = if width == 64 { u64::MAX } else { (1u64 << width) - 1 };
    let Some(r) = right.value.into_int_value().get_zero_extended_constant() else { return Ok(()) };
    let r = r & mask;
    match op {
        IntOp::Div | IntOp::Rem => {
            if r == 0 {
                return Err("Native Division/Rest durch konstante Null".into());
            }
            if left.ty.is_signed_integer() && r == mask {
                if let Some(l) = left.value.into_int_value().get_zero_extended_constant() {
                    if l & mask == 1u64 << (width - 1) {
                        return Err("Native Division/Rest von MIN durch -1 ist konstant ungueltig".into());
                    }
                }
            }
        }
        IntOp::Shl | IntOp::Shr => {
            if r >= u64::from(width) {
                return Err(format!("Nativer Shiftzaehler {r} liegt ausserhalb 0..{}", width - 1));
            }
        }
        // P2-T58 (Plan f40b5980 §2.1): konstant negativer Exponent ist ein Compilefehler.
        IntOp::Pow => {
            if left.ty.is_signed_integer() && r & (1u64 << (width - 1)) != 0 {
                return Err("Native Potenz mit konstant negativem Exponenten".into());
            }
        }
        _ => {}
    }
    Ok(())
}

/// Teilausdrucks-Lowering fuer den ersten sicheren Slice. Casts (P2-T34,
/// ADR §4) laufen ueber native_arithmetic::cast mit eigenem Fehlerblock;
/// Vergleiche, und/oder (Kurzschluss) und nicht liefern bool. 'failure' gehoert zur
/// aktuellen Funktion und wird vom Arithmetikbaustein validiert; die nicht
/// fehlerfaehigen Operationen springen nie dorthin. Div/Rem/Shift (P2-T34)
/// bekommen je Operation einen eigenen Fehlerblock nach `fehler`.
pub(crate) fn lower_nontrapping_expr<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    expr: &Expr,
    expected: NType,
    slots: &impl ScalarBindings<'ctx>,
    failure: BasicBlock<'ctx>,
    fehler: NativeFehler<'_, 'ctx>,
    pure_functions: &HashMap<String, (FunctionValue<'ctx>, NSignature)>,
) -> Result<NValue<'ctx>, String> {
    if slots.integer_only() && matches!(expected, NType::F32 | NType::F64) {
        return Err(format!("Hardwarezugriff: {expected:?} ist kein Ganzzahltyp"));
    }
    match expr {
        Expr::NativeNumber { text, .. } if matches!(expected, NType::F32 | NType::F64) => {
            let (value, underflow) = expected.float_literal(context, text, false)?;
            if underflow {
                crate::diagnose::melden(crate::diagnose::Diagnose::hinweis("M-H004", format!("Floatliteral '{text}' rundet auf null")));
            }
            Ok(value)
        },
        Expr::NativeNumber { text, .. } => expected.integer_literal(context, text, false),
        Expr::Boolean(_) if slots.integer_only() && expected != NType::Bool => {
            Err(format!("Hardwarezugriff: Boolliteral hat Typ Bool, erwartet {expected:?}"))
        }
        Expr::Boolean(value) if expected == NType::Bool => NValue::checked(
            context,
            expected,
            context.bool_type().const_int(u64::from(*value), false).into(),
        ),
        Expr::Identifier(name) => {
            let binding = slots.scalar_binding(name)
                .ok_or_else(|| format!("Nativer Name '{name}' nicht gefunden"))?;
            let actual = binding.ty();
            if actual != expected {
                let message = match binding {
                    ScalarBinding::Slot(_) => format!("Nativer Name '{name}' hat Typ {actual:?}, erwartet {expected:?}"),
                    ScalarBinding::Argument(_) => format!("Hardwarezugriff: '{name}' hat nicht den Typ {expected:?}, erhalten {actual:?}"),
                };
                return Err(message);
            }
            match binding {
                ScalarBinding::Slot(slot) => slot.load(context, builder, name),
                ScalarBinding::Argument(arg) => NValue::checked(context, actual, arg.value),
            }
        }
        // P2-T34: nicht liefert bool.
        Expr::UnaryOp { op: UnaryOpKind::Not, operand } => {
            if expected != NType::Bool {
                return Err(format!("Natives 'nicht' liefert bool, hier wird {expected:?} erwartet"));
            }
            let value = lower_nontrapping_expr(context, builder, operand, NType::Bool, slots, failure, fehler, pure_functions)?;
            let result = builder.build_not(value.value.into_int_value(), "native.nicht").map_err(|e| e.to_string())?;
            NValue::checked(context, NType::Bool, result.into())
        }
        // P2-T34: Bit-Negation auf Ganzzahlen (Bitmuster der Breite).
        Expr::UnaryOp { op: UnaryOpKind::BitNot, operand } => {
            if matches!(expected, NType::F32 | NType::F64 | NType::Bool) {
                return Err(format!("Native Bit-Negation ist fuer {expected:?} nicht definiert"));
            }
            let value = lower_nontrapping_expr(context, builder, operand, expected, slots, failure, fehler, pure_functions)?;
            let result = builder.build_not(value.value.into_int_value(), "native.bitnicht").map_err(|e| e.to_string())?;
            NValue::checked(context, expected, result.into())
        }
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => {
            if let Expr::NativeNumber { text, .. } = operand.as_ref() {
                if matches!(expected, NType::F32 | NType::F64) {
                    let (value, underflow) = expected.float_literal(context, text, true)?;
                    if underflow {
                        crate::diagnose::melden(crate::diagnose::Diagnose::hinweis("M-H004", format!("Floatliteral '-{text}' rundet auf null")));
                    }
                    return Ok(value);
                }
                return expected.integer_literal(context, text, true);
            }
            let value = lower_nontrapping_expr(context, builder, operand, expected, slots, failure, fehler, pure_functions)?;
            if matches!(expected, NType::F32 | NType::F64) {
                let result = builder.build_float_neg(value.value.into_float_value(), "native.fneg")
                    .map_err(|e| e.to_string())?;
                NValue::checked(context, expected, result.into())
            } else {
                crate::native_arithmetic::int_neg(context, builder, value)
            }
        }
        Expr::BinaryOp { left, op, right } => {
            use crate::native_arithmetic::CompareOp;
            // P2-T34: Vergleiche liefern bool; Operandentyp aus den Operanden,
            // ein Literal nimmt den Typ des typisierten Nachbarn an (ADR §2).
            let compare = match op {
                BinOp::Eq => Some(CompareOp::Eq), BinOp::NotEq => Some(CompareOp::Ne),
                BinOp::Less => Some(CompareOp::Lt), BinOp::LessEq => Some(CompareOp::Le),
                BinOp::Greater => Some(CompareOp::Gt), BinOp::GreaterEq => Some(CompareOp::Ge),
                _ => None,
            };
            if let Some(cmp) = compare {
                if expected != NType::Bool {
                    return Err(format!("Nativer Vergleich liefert bool, hier wird {expected:?} erwartet"));
                }
                let ty = operand_type(left, right, slots, pure_functions)
                    .ok_or("Nativer Vergleich: Typ der Operanden ist nicht bestimmbar")?;
                let lhs = lower_nontrapping_expr(context, builder, left, ty, slots, failure, fehler, pure_functions)?;
                let rhs = lower_nontrapping_expr(context, builder, right, ty, slots, failure, fehler, pure_functions)?;
                return crate::native_arithmetic::compare(context, builder, cmp, lhs, rhs);
            }
            if matches!(op, BinOp::And | BinOp::Or) {
                if expected != NType::Bool {
                    return Err(format!("Native Logik (und/oder) liefert bool, hier wird {expected:?} erwartet"));
                }
                // Kurzschluss: der rechte Operand samt seinen Fehlerpfaden laeuft
                // nur, wenn der linke das Ergebnis nicht schon festlegt.
                let lhs = lower_nontrapping_expr(context, builder, left, NType::Bool, slots, failure, fehler, pure_functions)?;
                let left_end = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
                let function = left_end.get_parent().ok_or("LLVM-Block ohne Funktion")?;
                let rhs_block = context.append_basic_block(function, "native.logik.rechts");
                let merge = context.append_basic_block(function, "native.logik.ende");
                let l = lhs.value.into_int_value();
                if matches!(op, BinOp::And) {
                    builder.build_conditional_branch(l, rhs_block, merge)
                } else {
                    builder.build_conditional_branch(l, merge, rhs_block)
                }.map_err(|e| e.to_string())?;
                builder.position_at_end(rhs_block);
                let rhs = lower_nontrapping_expr(context, builder, right, NType::Bool, slots, failure, fehler, pure_functions)?;
                let right_end = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
                builder.build_unconditional_branch(merge).map_err(|e| e.to_string())?;
                builder.position_at_end(merge);
                let phi = builder.build_phi(context.bool_type(), "native.logik").map_err(|e| e.to_string())?;
                let short = context.bool_type().const_int(u64::from(matches!(op, BinOp::Or)), false);
                let r = rhs.value.into_int_value();
                phi.add_incoming(&[(&short, left_end), (&r, right_end)]);
                return NValue::checked(context, NType::Bool, phi.as_basic_value());
            }
            let left = lower_nontrapping_expr(context, builder, left, expected, slots, failure, fehler, pure_functions)?;
            let right = lower_nontrapping_expr(context, builder, right, expected, slots, failure, fehler, pure_functions)?;
            if matches!(expected, NType::F32 | NType::F64) {
                let op = match op {
                    BinOp::Add => crate::native_arithmetic::FloatOp::Add,
                    BinOp::Sub => crate::native_arithmetic::FloatOp::Sub,
                    BinOp::Mul => crate::native_arithmetic::FloatOp::Mul,
                    BinOp::Div => crate::native_arithmetic::FloatOp::Div,
                    BinOp::Mod => crate::native_arithmetic::FloatOp::Rem,
                    BinOp::Pow => crate::native_arithmetic::FloatOp::Pow,
                    _ => return Err("Nativer Float-Operator noch nicht unterstützt".into()),
                };
                if matches!(op, crate::native_arithmetic::FloatOp::Rem | crate::native_arithmetic::FloatOp::Pow) {
                    crate::native_arithmetic::float_bibliothek(context, builder, fehler.module, op, left, right)
                } else {
                    crate::native_arithmetic::float_binary(context, builder, op, left, right)
                }
            } else {
                use crate::native_arithmetic::IntOp;
                let op = match op {
                    BinOp::Add => IntOp::Add,
                    BinOp::Sub => IntOp::Sub,
                    BinOp::Mul => IntOp::Mul,
                    BinOp::BitAnd => IntOp::BitAnd,
                    BinOp::BitOr => IntOp::BitOr,
                    BinOp::BitXor => IntOp::BitXor,
                    // P2-T34: fehlerfaehige Operationen mit eigenem Fehlerblock.
                    BinOp::Div => IntOp::Div,
                    BinOp::Mod => IntOp::Rem,
                    BinOp::LShift => IntOp::Shl,
                    BinOp::RShift => IntOp::Shr,
                    BinOp::Pow => IntOp::Pow,
                    _ => return Err("Nicht unterstützter nativer Integer-Operator".into()),
                };
                let pow_signed = matches!(op, IntOp::Pow) && left.ty.is_signed_integer();
                if matches!(op, IntOp::Pow) && !pow_signed {
                    // unsigned **: kein Fehlerfall, kein Fehlerblock im IR.
                    return crate::native_arithmetic::int_binary(context, builder, op, left, right, failure);
                }
                if matches!(op, IntOp::Div | IntOp::Rem | IntOp::Shl | IntOp::Shr) || pow_signed {
                    pruefe_konstante_operanden(op, &left, &right)?;
                    let (kennung, text) = match op {
                        IntOp::Div => ("div", format!("Native Division durch Null oder MIN/-1 in '{}'", fehler.funktion)),
                        IntOp::Rem => ("rem", format!("Nativer Rest durch Null oder MIN/-1 in '{}'", fehler.funktion)),
                        IntOp::Pow => ("pow", format!("Native Potenz mit negativem Exponenten in '{}'", fehler.funktion)),
                        _ => ("shift", format!("Nativer Shiftzaehler ausserhalb 0..Breite-1 in '{}'", fehler.funktion)),
                    };
                    let block = fehlerblock(context, builder, fehler, kennung, &text)?;
                    return crate::native_arithmetic::int_binary(context, builder, op, left, right, block);
                }
                crate::native_arithmetic::int_binary(context, builder, op, left, right, failure)
            }
        }
        Expr::FunctionCall { name, args, .. } if !pure_functions.contains_key(name)
            && crate::native_arithmetic::bit_art(name).is_some() =>
        {
            let art = crate::native_arithmetic::bit_art(name).ok_or("Bitoperationsart fehlt")?;
            if args.len() != art.aritaet() {
                return Err(format!("{name}(...) erwartet {} Argumente", art.aritaet()));
            }
            let ty = if art.liefert_bool() {
                if expected != NType::Bool { return Err(format!("{name}(...) liefert bool")); }
                operand_type(&args[0], &args[1], slots, pure_functions)
                    .ok_or_else(|| format!("{name}: Operandentyp fehlt"))?
            } else { expected };
            let mut values = Vec::with_capacity(args.len());
            for (i, arg) in args.iter().enumerate() {
                let target = if art == crate::native_arithmetic::BitArt::Auswahl && i == 0 {
                    NType::Bool
                } else if matches!(art, crate::native_arithmetic::BitArt::Links | crate::native_arithmetic::BitArt::Rechts) && i == 1 {
                    pure_infer(arg, slots, pure_functions).unwrap_or(ty)
                } else { ty };
                values.push(lower_nontrapping_expr(context, builder, arg, target, slots, failure, fehler, pure_functions)?);
            }
            crate::native_arithmetic::bit_operation(context, builder, fehler.module, art, &values)
        }
        // P2-T34 / ADR §13: Vorpruefung mit Status: teilbar(a, b), verschiebbar(a, n) -> bool.
        Expr::FunctionCall { name, args, .. } if crate::native_arithmetic::pruef_art(name).is_some() => {
            let op = crate::native_arithmetic::pruef_art(name).ok_or("Pruefart fehlt")?;
            if expected != NType::Bool {
                return Err(format!("{name}(...) liefert bool, hier wird {expected:?} erwartet"));
            }
            let [left, right] = args.as_slice() else {
                return Err(format!("{name}(a, b) erwartet genau zwei Argumente"));
            };
            let shift = matches!(op, crate::native_arithmetic::IntOp::Shl);
            let left_ty = if shift {
                pure_infer(left, slots, pure_functions)
            } else {
                operand_type(left, right, slots, pure_functions)
            }.ok_or_else(|| format!("{name}: Typ der Operanden ist nicht bestimmbar"))?;
            let right_ty = if shift { pure_infer(right, slots, pure_functions).unwrap_or(left_ty) } else { left_ty };
            let lhs = lower_nontrapping_expr(context, builder, left, left_ty, slots, failure, fehler, pure_functions)?;
            let rhs = lower_nontrapping_expr(context, builder, right, right_ty, slots, failure, fehler, pure_functions)?;
            crate::native_arithmetic::pruefung(context, builder, op, lhs, rhs)
        }
        // P7-T22: Bei Hardwareoperanden gibt T(x) wie bisher den Ganzzahltyp vor.
        Expr::FunctionCall { name, args, .. } if slots.integer_constructor(name).is_some() => {
            let ty = slots.integer_constructor(name).ok_or("Hardwaretyp fehlt")?;
            if ty != expected {
                return Err(format!("Hardwarezugriff: {name}(...) liefert {ty:?}, erwartet ist {expected:?}"));
            }
            let [operand] = args.as_slice() else {
                return Err(format!("Hardwarezugriff: {name}(...) erwartet genau ein Argument"));
            };
            lower_nontrapping_expr(context, builder, operand, ty, slots, failure, fehler, pure_functions)
        }
        // P2-T34 / ADR §4: als(T, x) gepruefter Standardcast, als_wrap(T, x),
        // als_saettigend(T, x). Der Operand wird mit seinem eigenen Typ gesenkt.
        Expr::FunctionCall { name, args, .. } if crate::native_arithmetic::cast_art(name).is_some() => {
            let art = crate::native_arithmetic::cast_art(name).ok_or("Cast-Art fehlt")?;
            let [ziel, operand] = args.as_slice() else {
                return Err(format!("{name}(Typ, Wert) erwartet genau zwei Argumente"));
            };
            let target = match ziel {
                Expr::Identifier(ty) => NType::from_name(ty)
                    .ok_or_else(|| format!("{name}: unbekannter Zieltyp '{ty}'"))?,
                _ => return Err(format!("{name}: erstes Argument muss ein Typname sein, z. B. {name}(u8, x)")),
            };
            if target != expected {
                return Err(format!("{name}({target:?}, ...) liefert {target:?}, hier wird {expected:?} erwartet"));
            }
            let source = pure_infer(operand, slots, pure_functions)
                .ok_or_else(|| format!("{name}: Typ des Operanden ist nicht bestimmbar"))?;
            let value = lower_nontrapping_expr(context, builder, operand, source, slots, failure, fehler, pure_functions)?;
            crate::native_arithmetic::pruefe_konstanten_cast(art, &value, target)?;
            let block = if !crate::native_arithmetic::cast_kann_fehlen(art, value.ty, target) {
                failure
            } else {
                let text = format!("Native Konversion nach {target:?} ausserhalb des Zielbereichs in '{}'", fehler.funktion);
                fehlerblock(context, builder, fehler, "cast", &text)?
            };
            crate::native_arithmetic::cast(context, builder, fehler.module, art, value, target, block)
        }
        // P2-T89: eingebaute Winkelfunktionen (eigene @nativ-Funktion gleichen
        // Namens gewinnt); Argument im Ergebnistyp, Aufruf moo_native_math.c.
        Expr::FunctionCall { name, args, .. } if !pure_functions.contains_key(name)
            && crate::native_arithmetic::trig_art(name).is_some() =>
        {
            let art = crate::native_arithmetic::trig_art(name).ok_or("Winkelfunktion fehlt")?;
            let [arg] = args.as_slice() else {
                return Err(format!("{name}(x) erwartet genau ein Argument"));
            };
            if !matches!(expected, NType::F32 | NType::F64) {
                return Err(format!("{name}(x) liefert f32/f64, hier wird {expected:?} erwartet"));
            }
            let value = lower_nontrapping_expr(context, builder, arg, expected, slots, failure, fehler, pure_functions)?;
            crate::native_arithmetic::float_trig(context, builder, fehler.module, art, value)
        }
        Expr::FunctionCall { name, args, zeile, .. } => {
            let (function, signature) = pure_functions.get(name)
                .ok_or_else(|| format!("Nicht-reines natives Aufrufziel '{name}'"))?;
            if args.len() != signature.params.len() {
                return Err(crate::diagnose::vormerken("M-N004", *zeile,
                    aritaet_meldung(name, signature.params.len(), args.len())));
            }
            if signature.result != Some(expected)
                || function.get_type() != signature.llvm_type(context)
            {
                return Err(format!("Native Funktion '{name}': falscher Typ oder falsche Argumentzahl"));
            }
            let mut call_args = Vec::with_capacity(args.len());
            for (arg, ty) in args.iter().zip(&signature.params) {
                let value = lower_nontrapping_expr(context, builder, arg, *ty, slots, failure, fehler, pure_functions)?;
                call_args.push(value.value.into());
            }
            let result = builder.build_call(*function, &call_args, "native.direct")
                .map_err(|e| e.to_string())?.try_as_basic_value();
            if fehler.trap && fehler.abbruch
                && function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."))
            {
                abbruch_nach_aufruf(context, builder, fehler, name)?;
            }
            match result {
                inkwell::values::ValueKind::Basic(value) => NValue::checked(context, expected, value),
                _ => Err("Nativer Aufruf liefert keinen Wert".into()),
            }
        }
        _ => Err("Nativer Ausdruck noch nicht unterstützt".into()),
    }
}

/// Literal ohne Typkontext (ADR §2): Ganzzahl i64, Float f64. Basisliterale
/// (0x/0b/0o) sind immer Ganzzahlen, auch wenn sie ein 'e' enthalten.
pub(crate) fn literal_default_type(text: &str) -> NType {
    let basis = text.len() > 1 && text.starts_with('0')
        && matches!(text.as_bytes()[1], b'x' | b'X' | b'b' | b'B' | b'o' | b'O');
    if !basis && text.bytes().any(|c| matches!(c, b'.' | b'e' | b'E')) { NType::F64 } else { NType::I64 }
}

/// Zahlenliteral, ggf. negiert: nimmt den Typ des anderen Operanden an.
fn is_literal(expr: &Expr) -> bool {
    match expr {
        Expr::NativeNumber { .. } => true,
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => matches!(operand.as_ref(), Expr::NativeNumber { .. }),
        _ => false,
    }
}

/// Gemeinsamer Operandentyp zweier Teilausdruecke (Arithmetik, Vergleich).
fn operand_type<'ctx>(
    left: &Expr,
    right: &Expr,
    slots: &impl ScalarBindings<'ctx>,
    pure_functions: &HashMap<String, (FunctionValue<'ctx>, NSignature)>,
) -> Option<NType> {
    match (pure_infer(left, slots, pure_functions), pure_infer(right, slots, pure_functions)) {
        // P2-T9 (Review 9b847d8e): zwei untypisierte Literale mit verschiedenem
        // Standardtyp (i64/f64) -> f64, unabhaengig von der Reihenfolge. Ein
        // Ganzzahlliteral darf den Float-Typ annehmen (ADR §2), umgekehrt nicht.
        // Dieselbe Regel nutzt der Systemkoerper (native_system literal_paar).
        (Some(l), Some(r)) if l != r && is_literal(left) && is_literal(right) => Some(literal_paar_typ(l, r)),
        (Some(l), Some(r)) if l != r && is_literal(left) => Some(r),
        (l, r) => l.or(r),
    }
}

/// Gemeinsamer Typ zweier untypisierter Literale (Standardtypen l, r):
/// f64, sobald eines ein Float-Literal ist, sonst der gemeinsame Typ.
pub(crate) fn literal_paar_typ(l: NType, r: NType) -> NType {
    if l == r { l } else if matches!(l, NType::F64 | NType::F32) || matches!(r, NType::F64 | NType::F32) { NType::F64 } else { l }
}

/// Statischer Typ eines Teilausdrucks im Pure-Pfad; gebraucht fuer Cast-
/// und Vergleichsoperanden, deren Typ nicht vom Zielkontext kommt.
/// Ein Literal neben einem typisierten Operanden nimmt dessen Typ an.
fn pure_infer<'ctx>(
    expr: &Expr,
    slots: &impl ScalarBindings<'ctx>,
    pure_functions: &HashMap<String, (FunctionValue<'ctx>, NSignature)>,
) -> Option<NType> {
    match expr {
        Expr::NativeNumber { text, .. } => Some(literal_default_type(text)),
        Expr::Boolean(_) => Some(NType::Bool),
        Expr::Identifier(name) => slots.scalar_binding(name).map(|binding| binding.ty()),
        Expr::UnaryOp { op: UnaryOpKind::Not, .. } => Some(NType::Bool),
        Expr::UnaryOp { operand, .. } => pure_infer(operand, slots, pure_functions),
        Expr::BinaryOp { op, left, right } => match op {
            BinOp::Eq | BinOp::NotEq | BinOp::Less | BinOp::LessEq | BinOp::Greater
            | BinOp::GreaterEq | BinOp::And | BinOp::Or => Some(NType::Bool),
            BinOp::LShift | BinOp::RShift => pure_infer(left, slots, pure_functions),
            _ => operand_type(left, right, slots, pure_functions),
        },
        Expr::FunctionCall { name, args, .. } => {
            if !pure_functions.contains_key(name) {
                if let Some(art) = crate::native_arithmetic::bit_art(name) {
                    return if art.liefert_bool() { Some(NType::Bool) }
                        else { args.get(art.daten_index()).and_then(|a| pure_infer(a, slots, pure_functions)) };
                }
            }
            if crate::native_arithmetic::pruef_art(name).is_some() {
                return Some(NType::Bool);
            }
            if crate::native_arithmetic::cast_art(name).is_some() {
                return match args.first() {
                    Some(Expr::Identifier(ty)) => NType::from_name(ty),
                    _ => None,
                };
            }
            if !pure_functions.contains_key(name) && crate::native_arithmetic::trig_art(name).is_some() {
                return args.first().and_then(|a| pure_infer(a, slots, pure_functions))
                    .filter(|ty| matches!(ty, NType::F32 | NType::F64));
            }
            slots.integer_constructor(name)
                .or_else(|| pure_functions.get(name).and_then(|(_, signature)| signature.result))
        }
        _ => None,
    }
}

pub(crate) enum ScalarBinding<'ctx> {
    Slot(NSlot<'ctx>),
    Argument(NValue<'ctx>),
}

impl ScalarBinding<'_> {
    fn ty(&self) -> NType {
        match self {
            Self::Slot(slot) => slot.ty,
            Self::Argument(arg) => arg.ty,
        }
    }
}

/// Der Skalarpfad liest lokale Slots oder direkt uebergebene SSA-Argumente.
pub(crate) trait ScalarBindings<'ctx> {
    fn scalar_binding(&self, name: &str) -> Option<ScalarBinding<'ctx>>;

    fn integer_constructor(&self, _name: &str) -> Option<NType> {
        None
    }

    fn integer_only(&self) -> bool {
        false
    }
}

impl<'ctx> ScalarBindings<'ctx> for HashMap<String, NSlot<'ctx>> {
    fn scalar_binding(&self, name: &str) -> Option<ScalarBinding<'ctx>> {
        self.get(name).copied().map(ScalarBinding::Slot)
    }
}

/// Ein bereits im Entry-Block angelegter, typisierter Skalarslot.
/// Initialisierung und Gueltigkeit des Alloca-Pointers liegen beim Aufrufer.
#[derive(Clone, Copy, Debug)]
pub(crate) struct NSlot<'ctx> {
    pub(crate) ty: NType,
    pub(crate) ptr: PointerValue<'ctx>,
}

impl<'ctx> NSlot<'ctx> {
    pub(crate) fn load(
        self,
        context: &'ctx Context,
        builder: &Builder<'ctx>,
        name: &str,
    ) -> Result<NValue<'ctx>, String> {
        let value = builder.build_load(self.ty.llvm_type(context), self.ptr, name)
            .map_err(|e| e.to_string())?;
        NValue::checked(context, self.ty, value)
    }

    pub(crate) fn store(
        self,
        builder: &Builder<'ctx>,
        value: NValue<'ctx>,
    ) -> Result<(), String> {
        if self.ty != value.ty {
            return Err(format!("Nativer Slot erwartet {:?}, erhalten {:?}", self.ty, value.ty));
        }
        builder.build_store(self.ptr, value.value).map_err(|e| e.to_string())?;
        Ok(())
    }
}


/// Emittiert nur einen isolierten, nichttrappenden skalaren Funktionskoerper.
/// Bei Fehler muss der Aufrufer das gesamte Modul verwerfen: ein teilweise
/// gefuellter LLVM-Koerper darf nicht weiterverwendet werden. Dieser Helfer
/// erzeugt weder dynamische Thunks noch aktiviert er den @nativ-Einstieg.
/// Prueft den gesamten reinen Aufrufgraphen vor Emission auf Zyklen und
/// unbekannte bzw. nicht reine Ziele; damit bleiben alle Calls nichttrappend.
/// Meldung fuer eine falsche Argumentzahl beim Aufruf einer nativen Funktion.
pub(crate) fn aritaet_meldung(name: &str, erwartet: usize, bekommen: usize) -> String {
    let wort = if erwartet == 1 { "Argument" } else { "Argumente" };
    format!("Native Funktion '{name}' erwartet {erwartet} {wort}, bekommen {bekommen}")
}

/// @nativ-Funktionen, die auf einem nativen Aufrufzyklus liegen oder (transitiv)
/// eine solche Funktion aufrufen. Die Pure-Form ist zyklusfrei
/// (validate_pure_call_graph); diese Funktionen laufen im System-Emitter.
pub(crate) fn zyklische_native_funktionen(statements: &[Stmt]) -> std::collections::HashSet<String> {
    use std::collections::HashSet;
    // Erschoepfend ueber alle Ausdrucks- und Anweisungsformen (kein `_`-Arm):
    // eine neue AST-Form muss hier bewusst eingeordnet werden. Methodennamen
    // sind keine nativen Aufrufziele; Empfaenger und Argumente zaehlen.
    fn expr_aufrufe(expr: &Expr, aufrufe: &mut HashSet<String>) {
        let mut alle = |es: &[Expr], aufrufe: &mut HashSet<String>| for e in es { expr_aufrufe(e, aufrufe); };
        match expr {
            Expr::Number(_) | Expr::NativeNumber { .. } | Expr::String(_) | Expr::Boolean(_)
            | Expr::None | Expr::Identifier(_) | Expr::This => {}
            Expr::FunctionCall { name, args, .. } => {
                aufrufe.insert(name.clone());
                alle(args, aufrufe);
            }
            Expr::New { args, .. } => alle(args, aufrufe),
            Expr::MethodCall { object, args, .. } => {
                expr_aufrufe(object, aufrufe);
                alle(args, aufrufe);
            }
            Expr::BinaryOp { left, right, .. } | Expr::NullishCoalesce { left, right }
            | Expr::Pipe { left, right } => {
                expr_aufrufe(left, aufrufe);
                expr_aufrufe(right, aufrufe);
            }
            Expr::UnaryOp { operand: inner, .. } | Expr::PropertyAccess { object: inner, .. }
            | Expr::OptionalChain { object: inner, .. } | Expr::Spread(inner)
            | Expr::Lambda { body: inner, .. } => expr_aufrufe(inner, aufrufe),
            Expr::IndexAccess { object: a, index: b } | Expr::Range { start: a, end: b } => {
                expr_aufrufe(a, aufrufe);
                expr_aufrufe(b, aufrufe);
            }
            Expr::List(elemente) => alle(elemente, aufrufe),
            Expr::Dict(paare) => for (k, v) in paare {
                expr_aufrufe(k, aufrufe);
                expr_aufrufe(v, aufrufe);
            },
            Expr::ListComprehension { expr, iterable, condition, .. } => {
                expr_aufrufe(expr, aufrufe);
                expr_aufrufe(iterable, aufrufe);
                if let Some(c) = condition { expr_aufrufe(c, aufrufe); }
            }
            Expr::Ternary { condition, then_val, else_val } => {
                expr_aufrufe(condition, aufrufe);
                expr_aufrufe(then_val, aufrufe);
                expr_aufrufe(else_val, aufrufe);
            }
            Expr::MatchExpr { value, cases } => {
                expr_aufrufe(value, aufrufe);
                for (muster, waechter, ergebnis) in cases {
                    if let Some(m) = muster { expr_aufrufe(m, aufrufe); }
                    if let Some(w) = waechter { expr_aufrufe(w, aufrufe); }
                    expr_aufrufe(ergebnis, aufrufe);
                }
            }
            Expr::QueryExpr { source, where_cond, order_expr, select_expr, .. } => {
                expr_aufrufe(source, aufrufe);
                if let Some(w) = where_cond { expr_aufrufe(w, aufrufe); }
                if let Some(o) = order_expr { expr_aufrufe(o, aufrufe); }
                expr_aufrufe(select_expr, aufrufe);
            }
        }
    }
    fn stmt_aufrufe(body: &[Stmt], aufrufe: &mut HashSet<String>) {
        for stmt in body {
            match stmt {
                Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
                | Stmt::CompoundAssignment { value, .. } | Stmt::RawDataU32 { value, .. }
                | Stmt::NativeConst { value: Some(value), .. } | Stmt::Show(value, _) | Stmt::Throw(value, _)
                | Stmt::Expression(value, _) | Stmt::Defer(value, _) | Stmt::Expect(value, _)
                | Stmt::Return(Some(value), _)
                | Stmt::Precondition { condition: value, .. } | Stmt::Postcondition { condition: value, .. } =>
                    expr_aufrufe(value, aufrufe),
                Stmt::PropertyAssignment { object, value, .. } => {
                    expr_aufrufe(object, aufrufe);
                    expr_aufrufe(value, aufrufe);
                }
                Stmt::IndexAssignment { object, index, value, .. } => {
                    expr_aufrufe(object, aufrufe);
                    expr_aufrufe(index, aufrufe);
                    expr_aufrufe(value, aufrufe);
                }
                Stmt::If { condition, body, else_body, .. } => {
                    expr_aufrufe(condition, aufrufe);
                    stmt_aufrufe(body, aufrufe);
                    stmt_aufrufe(else_body, aufrufe);
                }
                Stmt::While { condition, body, .. } => {
                    expr_aufrufe(condition, aufrufe);
                    stmt_aufrufe(body, aufrufe);
                }
                Stmt::For { iterable, body, .. } | Stmt::ParallelFor { iterable, body, .. } => {
                    expr_aufrufe(iterable, aufrufe);
                    stmt_aufrufe(body, aufrufe);
                }
                Stmt::Guard { condition, else_body, .. } => {
                    expr_aufrufe(condition, aufrufe);
                    stmt_aufrufe(else_body, aufrufe);
                }
                Stmt::Match { value, cases, .. } => {
                    expr_aufrufe(value, aufrufe);
                    for (muster, waechter, rumpf) in cases {
                        if let Some(m) = muster { expr_aufrufe(m, aufrufe); }
                        if let Some(w) = waechter { expr_aufrufe(w, aufrufe); }
                        stmt_aufrufe(rumpf, aufrufe);
                    }
                }
                Stmt::TryCatch { try_body, catch_body, .. } => {
                    stmt_aufrufe(try_body, aufrufe);
                    stmt_aufrufe(catch_body, aufrufe);
                }
                Stmt::FunctionDef { defaults, body, .. } => {
                    for d in defaults.iter().flatten() { expr_aufrufe(d, aufrufe); }
                    stmt_aufrufe(body, aufrufe);
                }
                Stmt::UnsafeBlock { body, .. } | Stmt::ClassDef { body, .. } | Stmt::TestDef { body, .. } =>
                    stmt_aufrufe(body, aufrufe),
                Stmt::Export(inner, _) => stmt_aufrufe(std::slice::from_ref(inner.as_ref()), aufrufe),
                Stmt::Return(None, _) | Stmt::Break(_) | Stmt::Continue(_) | Stmt::ExternC { .. }
                | Stmt::NativeConst { value: None, .. } | Stmt::StructDef { .. } | Stmt::InterfaceDef { .. } | Stmt::Import { .. }
                | Stmt::Global(..) | Stmt::DataClassDef { .. } => {}
            }
        }
    }
    let mut graph: HashMap<String, HashSet<String>> = HashMap::new();
    for stmt in statements {
        if let Stmt::FunctionDef { name, body, decorators, .. } = stmt {
            if decorators.iter().any(|d| d.name == "nativ" || d.name == "native") {
                let mut aufrufe = HashSet::new();
                stmt_aufrufe(body, &mut aufrufe);
                graph.insert(name.clone(), aufrufe);
            }
        }
    }
    // Eine Funktion liegt auf einem Zyklus, wenn sie von sich selbst aus erreichbar ist.
    let mut ergebnis = HashSet::new();
    for (start, kanten) in &graph {
        let mut stapel: Vec<&String> = kanten.iter().collect();
        let mut gesehen = HashSet::new();
        while let Some(knoten) = stapel.pop() {
            if knoten == start {
                ergebnis.insert(start.clone());
                break;
            }
            if !gesehen.insert(knoten.clone()) { continue; }
            if let Some(weiter) = graph.get(knoten) { stapel.extend(weiter.iter()); }
        }
    }
    // Aufrufer zyklischer Funktionen ebenso (die Pure-Form ruft nur reine Ziele).
    loop {
        let neu: Vec<String> = graph.iter()
            .filter(|(n, aufrufe)| !ergebnis.contains(*n) && aufrufe.iter().any(|a| ergebnis.contains(a)))
            .map(|(n, _)| n.clone())
            .collect();
        if neu.is_empty() { break; }
        ergebnis.extend(neu);
    }
    ergebnis
}

pub(crate) fn validate_pure_call_graph(bodies: &HashMap<String, &[Stmt]>) -> Result<(), String> {
    fn expr_calls(expr: &Expr, calls: &mut Vec<String>) -> Result<(), String> {
        match expr {
            Expr::NativeNumber { .. } | Expr::Boolean(_) | Expr::Identifier(_) => {}
            Expr::UnaryOp { operand, .. } => expr_calls(operand, calls)?,
            Expr::BinaryOp { left, right, .. } => {
                expr_calls(left, calls)?;
                expr_calls(right, calls)?;
            }
            Expr::FunctionCall { name, args, .. } => {
                // P2-T34: Casts (als/als_wrap/als_saettigend) sind Operatoren,
                // keine Aufrufziele; ihr erstes Argument ist ein Typname.
                if crate::native_arithmetic::cast_art(name).is_some() {
                    for arg in args.iter().skip(1) { expr_calls(arg, calls)?; }
                } else if crate::native_arithmetic::pruef_art(name).is_some() {
                    for arg in args { expr_calls(arg, calls)?; }
                } else {
                    calls.push(name.clone());
                    for arg in args { expr_calls(arg, calls)?; }
                }
            }
            _ => return Err("Nativer Ausdruck noch nicht unterstützt".into()),
        }
        Ok(())
    }
    fn visit(
        name: &str, graph: &HashMap<String, Vec<String>>,
        visiting: &mut std::collections::HashSet<String>,
        done: &mut std::collections::HashSet<String>,
    ) -> Result<(), String> {
        if done.contains(name) { return Ok(()); }
        if !visiting.insert(name.to_owned()) {
            return Err(format!("Rekursive native Aufrufe sind noch nicht unterstützt: '{name}'"));
        }
        // P2-T89: eingebaute Winkelfunktion ohne gleichnamige reine Funktion
        // ist ein Blatt (moo_native_math.c, nichttrappend).
        let edges = match graph.get(name) {
            Some(edges) => edges,
            None if crate::native_arithmetic::trig_art(name).is_some()
                || crate::native_arithmetic::bit_art(name).is_some() => {
                done.insert(name.to_owned());
                return Ok(());
            }
            None if NType::from_name(name).is_some() => return Err(format!(
                "Numerische Konstruktorform '{name}(...)' ist im reinen nativen Koerper nicht unterstuetzt; Vorschlag: als({name}, wert) fuer eine gepruefte Konversion verwenden"
            )),
            None => return Err(format!("Nicht-reines natives Aufrufziel '{name}'")),
        };
        for callee in edges { visit(callee, graph, visiting, done)?; }
        visiting.remove(name);
        done.insert(name.to_owned());
        Ok(())
    }
    let mut graph = HashMap::new();
    for (name, body) in bodies {
        let mut calls = Vec::new();
        for stmt in *body {
            match stmt {
                Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
                | Stmt::Return(Some(value), _) => expr_calls(value, &mut calls)?,
                Stmt::Return(None, _) => {}
                _ => return Err("Nativer Funktionskoerper wird noch nicht unterstützt".into()),
            }
        }
        graph.insert(name.clone(), calls);
    }
    let mut visiting = std::collections::HashSet::new();
    let mut done = std::collections::HashSet::new();
    for name in graph.keys() { visit(name, &graph, &mut visiting, &mut done)?; }
    Ok(())
}

pub(crate) fn emit_nontrapping_function<'ctx>(
    context: &'ctx Context,
    function: FunctionValue<'ctx>,
    signature: &NSignature,
    param_names: &[String],
    body: &[Stmt],
    pure_functions: &HashMap<String, (FunctionValue<'ctx>, NSignature)>,
    fehler: NativeFehler<'_, 'ctx>,
) -> Result<(), String> {
    if function.get_type() != signature.llvm_type(context)
        || param_names.len() != signature.params.len()
        || function.count_basic_blocks() != 0
    {
        return Err("Native Funktionssignatur oder leerer Funktionsrahmen erwartet".into());
    }
    let mut names = std::collections::HashSet::new();
    if param_names.iter().any(|name| !names.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let (result_expr, locals) = match (signature.result, body.split_last()) {
        (Some(_), Some((Stmt::Return(Some(expr), _), locals))) => (Some(expr), locals),
        (None, None) => (None, &[][..]),
        (None, Some((Stmt::Return(None, _), locals))) => (None, locals),
        _ => return Err("Nativer Funktionskoerper wird noch nicht unterstützt".into()),
    };
    for stmt in locals {
        match stmt {
            Stmt::Assignment { name, ty: Some(ty), .. }
            | Stmt::ConstAssignment { name, ty: Some(ty), .. } => {
                // P2-T9 (Plan 4500717b A1): jedes Local traegt seinen eigenen
                // nativen Typ; frueher musste er dem Rueckgabetyp gleichen.
                NType::from_type_ref(ty)?;
                if !names.insert(name) {
                    return Err(format!("Doppelter nativer Name '{name}'"));
                }
            }
            _ => return Err("Native lokale Variable braucht eine Typannotation (setze name: T auf wert)".into()),
        }
    }

    let entry = context.append_basic_block(function, "entry");
    let failure = context.append_basic_block(function, "native.unreachable");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    let mut slots = HashMap::new();
    for (index, (name, ty)) in param_names.iter().zip(&signature.params).enumerate() {
        let argument = function.get_nth_param(index as u32)
            .ok_or("Nativer LLVM-Parameter fehlt")?;
        let value = NValue::checked(context, *ty, argument)?;
        let ptr = builder.build_alloca(ty.llvm_type(context), name)
            .map_err(|e| e.to_string())?;
        let slot = NSlot { ty: *ty, ptr };
        slot.store(&builder, value)?;
        slots.insert(name.clone(), slot);
    }
    for stmt in locals {
        let (name, expr, ty) = match stmt {
            Stmt::Assignment { name, value, ty: Some(ty), .. }
            | Stmt::ConstAssignment { name, value, ty: Some(ty), .. } => (name, value, ty),
            _ => return Err("Ungueltige native lokale Variable".into()),
        };
        // Initialisierung vor Eintrag in die Symboltabelle verhindert
        // versehentliches Lesen eines noch uninitialisierten Slots.
        let local_ty = NType::from_type_ref(ty)?;
        let value = lower_nontrapping_expr(context, &builder, expr, local_ty, &slots, failure, fehler, pure_functions)?;
        // Allocas nur im Eintrittsblock: nach einer Verzweigung (P2-T84-Pruefung
        // nach internem Callee) waere ein Alloca dynamisch und erzwaenge auf
        // Win64 ___chkstk_ms (offenes Symbol im runtime-freien Objekt, ADR §13).
        let ptr = if builder.get_insert_block() == Some(entry) {
            builder.build_alloca(local_ty.llvm_type(context), name).map_err(|e| e.to_string())?
        } else {
            let am_anfang = context.create_builder();
            match entry.get_first_instruction() {
                Some(first) => am_anfang.position_before(&first),
                None => am_anfang.position_at_end(entry),
            }
            am_anfang.build_alloca(local_ty.llvm_type(context), name).map_err(|e| e.to_string())?
        };
        let slot = NSlot { ty: local_ty, ptr };
        slot.store(&builder, value)?;
        slots.insert(name.clone(), slot);
    }
    match (signature.result, result_expr) {
        (Some(ty), Some(expr)) => {
            let value = lower_nontrapping_expr(context, &builder, expr, ty, &slots, failure, fehler, pure_functions)?;
            builder.build_return(Some(&value.value)).map_err(|e| e.to_string())?;
        }
        (None, None) => {
            builder.build_return(None).map_err(|e| e.to_string())?;
        }
        _ => return Err("Inkonsistente native Rueckgabe".into()),
    }
    builder.position_at_end(failure);
    builder.build_unreachable().map_err(|e| e.to_string())?;
    Ok(())
}


/// Emit an unsafe return-call or a void call using a typed C function pointer.
/// Callers must uphold the validity and lifetime of all foreign pointers.
pub(crate) fn emit_indirect_return_function<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
    function: FunctionValue<'ctx>,
    signature: &NativePointerSignature,
    param_names: &[String],
    param_types: &[Option<TypeRef>],
    return_type: &TypeRef,
    body: &[Stmt],
    nominal: &crate::native_type_id::NominalTypes,
) -> Result<(), String> {
    use crate::ffi_types::{FfiSignature, TypedFfiValue};
    use crate::native_type_id::resolve_type;
    if function.get_type() != signature.llvm_type(context)
        || param_names.len() != signature.params.len()
        || param_types.len() != signature.params.len()
        || function.count_basic_blocks() != 0
    { return Err("Indirekter Aufruf: inkonsistente native Signatur".into()); }
    let mut names = std::collections::HashSet::new();
    if param_names.iter().any(|name| !names.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let (callee_name, args, returns_value) = match body {
        [Stmt::UnsafeBlock { body, .. }] => match body.as_slice() {
            [Stmt::Return(Some(Expr::FunctionCall { name, args, .. }), _)] => (name, args, true),
            [Stmt::Expression(Expr::FunctionCall { name, args, .. }, _)] => (name, args, false),
            _ => return Err("Indirekter Aufruf benötigt einen Rückgabe- oder void-Aufruf".into()),
        },
        _ => return Err("Indirekter extern-C-Aufruf erfordert unsicher".into()),
    };
    // Nominal-Tabelle des Moduls statt einer leeren Tabelle, sonst
    // scheitert jede Struktur in der Signatur (ptr[fn(ptr[S]) -> T]) mit
    // "Unbekannter nativer Typ".
    let parameter = |name: &str| -> Result<TypedFfiValue<'ctx>, String> {
        let index = param_names.iter().position(|candidate| candidate == name)
            .ok_or_else(|| format!("Unbekannter nativer Parameter '{name}'"))?;
        let source = param_types[index].as_ref().ok_or("Nativer Parametertyp fehlt")?;
        let identity = resolve_type(source, nominal)?;
        let value = function.get_nth_param(index as u32).ok_or("LLVM-Parameter fehlt")?;
        Ok(TypedFfiValue { value, identity })
    };
    let callee = parameter(callee_name)?;
    let triple = module.get_triple();
    let triple = triple.as_str().to_str().map_err(|e| e.to_string())?;
    let ffi = FfiSignature::from_indirect_identity(triple, &callee.identity, nominal)?;
    let mut values = Vec::with_capacity(args.len());
    for arg in args {
        match arg {
            Expr::Identifier(name) => values.push(parameter(name)?),
            _ => return Err("Indirekter Aufruf erwartet vorerst Parameterargumente".into()),
        }
    }
    let expected_result = resolve_type(return_type, nominal)?;
    let entry = context.append_basic_block(function, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    let result = ffi.emit_indirect_call(context, &builder, module, &callee, &values, true)?;
    match (returns_value, expected_result, result) {
        (true, expected, Some(value)) if value.identity == expected => {
            builder.build_return(Some(&value.value)).map_err(|e| e.to_string())?;
        }
        (false, crate::native_type_id::NativeTypeId::Void, None) => {
            builder.build_return(None).map_err(|e| e.to_string())?;
        }
        _ => return Err("Indirekter Aufruf: Rückgabetyp stimmt nicht".into()),
    }
    Ok(())
}

/// A first native pointer read slice. The pointer is borrowed, not owned.
/// Unsupported bodies fail closed; no dynamic adapter or foreign ABI is implied.
pub(crate) fn emit_pointer_load_function<'ctx>(
    context: &'ctx Context,
    target: &inkwell::targets::TargetData,
    function: FunctionValue<'ctx>,
    signature: &NativePointerSignature,
    param_names: &[String],
    body: &[Stmt],
) -> Result<(), String> {
    if function.get_type() != signature.llvm_type(context)
        || param_names.len() != signature.params.len()
        || function.count_basic_blocks() != 0
    {
        return Err("Native Zeigerfunktion hat eine inkonsistente Signatur".into());
    }
    let mut names = std::collections::HashSet::new();
    if param_names.iter().any(|name| !names.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let inner = match body {
        [Stmt::UnsafeBlock { body, .. }] => body.as_slice(),
        _ => return Err("Zeiger dereferenzieren erfordert unsicher".into()),
    };
    let (object, index) = match inner {
        [Stmt::Return(Some(Expr::IndexAccess { object, index }), _)] =>
            (object.as_ref(), index.as_ref()),
        _ => return Err("Nativer Zeigerfunktionskoerper wird noch nicht unterstützt".into()),
    };
    let pointer_name = match object {
        Expr::Identifier(name) => name,
        _ => return Err("Nativer Zeigerzugriff benötigt einen typisierten Zeigerparameter".into()),
    };
    let parameter_index = param_names.iter().position(|name| name == pointer_name)
        .ok_or("Zeigerparameter nicht gefunden")?;
    let (identity, source) = match &signature.params[parameter_index] {
        NativeParameter::Pointer { identity, source } => (identity, source),
        _ => return Err("Indexzugriff benötigt einen Zeigerparameter".into()),
    };
    let pointee = match &source.kind {
        TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 => &args[0],
        _ => return Err("Zeigerzieltyp fehlt".into()),
    };
    let pointee_type = NType::from_type_ref(pointee)?;
    if signature.result != Some(pointee_type) {
        return Err("Zeigerziel und Rückgabetyp stimmen nicht überein".into());
    }
    let index_type = crate::native_pointer::target_pointer_int(context, target)?;
    let number_type = match index_type.get_bit_width() {
        32 => NType::U32, 64 => NType::U64,
        _ => return Err("Nicht unterstützte Ziel-Pointerbreite".into()),
    };
    let element_size = target.get_abi_size(&pointee_type.llvm_type(context));
    let (constant_index, parameter_index_value) = match index {
        Expr::NativeNumber { text, .. } => {
            let value = number_type.integer_literal(context, text, false)?.value.into_int_value();
            let number = value.get_zero_extended_constant().ok_or("Nativer Zeigerindex ist nicht konstant")?;
            let max_offset = if index_type.get_bit_width() == 32 { i32::MAX as u64 } else { i64::MAX as u64 };
            if number.checked_mul(element_size).filter(|offset| *offset <= max_offset).is_none() {
                return Err("Nativer Zeigerindex überschreitet den Zieladressraum".into());
            }
            (Some(value), None)
        }
        Expr::Identifier(name) => {
            let index = param_names.iter().position(|candidate| candidate == name)
                .ok_or("Nativer Zeigerindexparameter nicht gefunden")?;
            if !matches!(signature.params[index], NativeParameter::Scalar(NType::U32))
                || index_type.get_bit_width() != 64
                || u32::MAX as u64 * element_size > i64::MAX as u64
            {
                return Err("Nativer Index-Parametertyp benötigt u32 und ein 64-Bit-Ziel".into());
            }
            (None, Some(index))
        }
        _ => return Err("Nativer Zeigerindex benötigt ein nichtnegatives Integerliteral oder einen u32-Parameter".into()),
    };

    let entry = context.append_basic_block(function, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    let index_value = if let Some(value) = constant_index {
        value
    } else {
        let argument = function.get_nth_param(parameter_index_value.unwrap() as u32)
            .ok_or("Nativer Zeigerindexparameter fehlt")?;
        let value = match argument {
            BasicValueEnum::IntValue(value) => value,
            _ => return Err("LLVM-Indexparameter ist keine Ganzzahl".into()),
        };
        builder.build_int_z_extend(value, index_type, "native.ptr.index.u32")
            .map_err(|e| e.to_string())?
    };
    let argument = function.get_nth_param(parameter_index as u32)
        .ok_or("Nativer Zeigerparameter fehlt")?;
    let pointer_value = match argument {
        BasicValueEnum::PointerValue(value) => value,
        _ => return Err("LLVM-Argument ist kein Zeiger".into()),
    };
    let slot = crate::native_pointer::NativePointerSlot::new(
        context, &builder, identity.clone(), pointer_value, pointer_name,
    )?;
    let pointer_value = slot.load(context, &builder, pointer_name)?;
    // This is deliberately not inbounds: the unsafe caller owns provenance,
    // range, lifetime and validity. No alignment is inferred from the type.
    let element_pointer = unsafe {
        builder.build_gep(pointee_type.llvm_type(context), pointer_value, &[index_value], "native.ptr.index")
    }.map_err(|e| e.to_string())?;
    let pointer = crate::native_pointer::NativePointer::new(
        element_pointer, pointee_type.llvm_type(context), pointee.clone(), 1,
    )?;
    let result = pointer.load_scalar(context, &builder, target, true, false, "native.ptr.load")?;
    builder.build_return(Some(&result.value)).map_err(|e| e.to_string())?;
    Ok(())
}

/// M1: enger unsicherer C-Return mit skalaren Parametern (FFI-Einzelform).
/// P2-T55: hosted fangbar wie reine @nativ-Koerper (codegen plain_fangbar).
/// `fremd_fehlbar` (fehlbare hosted Rueckrufe im Modul): nach dem C-Aufruf
/// wird das Moo-Fehlerflag geprueft; ist es gesetzt, kehrt der Wrapper mit
/// Nullwert zurueck (Ergebnis verworfen, Erstfehler bleibt), im C-Export
/// (@einstieg) Abbruch. Teilweise emittierte Module muessen bei Fehler
/// verworfen werden.
pub(crate) fn emit_ffi_return_function<'ctx>(
    context: &'ctx Context,
    module: &inkwell::module::Module<'ctx>,
    registry: &crate::ffi_types::ExternRegistry<'ctx>,
    function: FunctionValue<'ctx>,
    signature: &NSignature,
    param_names: &[String],
    body: &[Stmt],
    fehler: NativeFehler<'_, 'ctx>,
    fremd_fehlbar: bool,
) -> Result<(), String> {
    if function.get_type() != signature.llvm_type(context)
        || param_names.len() != signature.params.len()
        || match signature.result {
            // P2-T19 Batch C: C-bool (i1) ist FFI-Werttyp.
            Some(result) => !matches!(result, NType::I64 | NType::U64 | NType::I32 | NType::U32 | NType::I16 | NType::U16 | NType::I8 | NType::U8 | NType::F32 | NType::F64 | NType::Bool)
                || signature.params.iter().any(|ty| !matches!(ty, NType::I64 | NType::U64 | NType::I32 | NType::U32 | NType::I16 | NType::U16 | NType::I8 | NType::U8 | NType::F32 | NType::F64 | NType::Bool)),
            None => signature.params.iter().any(|ty| !matches!(ty, NType::I64 | NType::U64 | NType::I32 | NType::U32 | NType::I16 | NType::U16 | NType::I8 | NType::U8 | NType::F32 | NType::F64 | NType::Bool)),
        }
        || function.count_basic_blocks() != 0
    {
        return Err("Native FFI-Funktion verlangt eine unterstuetzte Signatur und einen leeren Funktionskoerper".into());
    }
    let mut names = std::collections::HashSet::new();
    if param_names.iter().any(|name| !names.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let inner = match body {
        [Stmt::UnsafeBlock { body, .. }] => body.as_slice(),
        _ => return Err("Native FFI-Funktion verlangt einen unsicher-Block".into()),
    };
    let (name, args) = match (signature.result, inner) {
        (Some(_), [Stmt::Return(Some(Expr::FunctionCall { name, args, .. }), _)]) => (name, args),
        (None, [Stmt::Expression(Expr::FunctionCall { name, args, .. }, _)]) => (name, args),
        _ => return Err("Native FFI-Funktion verlangt direkten C-Aufruf mit passender Rueckgabe".into()),
    };
    let binding = registry.get(name).ok_or_else(|| format!("extern C '{name}' nicht deklariert"))?;
    let types = binding.scalar_parameters()?;
    if types.len() != args.len() || binding.scalar_result()? != signature.result {
        return Err("Native FFI-Funktion: Argumentzahl oder Rueckgabetyp unpassend".into());
    }
    let triple = module.get_triple();
    let triple = triple.as_str().to_str().map_err(|e| format!("Ungueltiges Ziel-Triple: {e}"))?;
    let entry = context.append_basic_block(function, "entry");
    let failure = context.append_basic_block(function, "native.unreachable");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    let mut slots = HashMap::new();
    for (index, (name, ty)) in param_names.iter().zip(&signature.params).enumerate() {
        let arg = function.get_nth_param(index as u32).ok_or("Nativer LLVM-Parameter fehlt")?;
        let value = NValue::checked(context, *ty, arg)?;
        let ptr = builder.build_alloca(ty.llvm_type(context), name).map_err(|e| e.to_string())?;
        let slot = NSlot { ty: *ty, ptr };
        slot.store(&builder, value)?;
        slots.insert(name.clone(), slot);
    }
    let mut native_args = Vec::with_capacity(args.len());
    for (expr, ty) in args.iter().zip(types) {
        native_args.push(lower_nontrapping_expr(context, &builder, expr, ty, &slots, failure, fehler, &HashMap::new())?);
    }
    let value = binding.emit_scalar_call(context, &builder, module, triple, &native_args, true)?;
    if fremd_fehlbar && (!fehler.trap || fehler.abbruch) {
        let hook = native_fehler_offen_hook(context, module)?;
        let flag = match builder.build_call(hook, &[], "ffi.fremd.offen").map_err(|e| e.to_string())?.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
            _ => return Err("moo_native_fehler_offen liefert keinen Wert".into()),
        };
        let offen = builder.build_int_compare(
            inkwell::IntPredicate::NE, flag, context.i32_type().const_zero(), "ffi.fremd.gesetzt",
        ).map_err(|e| e.to_string())?;
        let fehler_bb = context.append_basic_block(function, "ffi.fremd.fehler");
        let ok_bb = context.append_basic_block(function, "ffi.fremd.ok");
        builder.build_conditional_branch(offen, fehler_bb, ok_bb).map_err(|e| e.to_string())?;
        builder.position_at_end(fehler_bb);
        if fehler.abbruch {
            emit_abbruch(context, &builder, module, fehler.funktion, "Fehler aus einem Rückruf")?;
        } else {
            match function.get_type().get_return_type() {
                Some(ty) => { builder.build_return(Some(&ty.const_zero())).map_err(|e| e.to_string())?; }
                None => { builder.build_return(None).map_err(|e| e.to_string())?; }
            }
        }
        builder.position_at_end(ok_bb);
    }
    match value {
        Some(value) => { builder.build_return(Some(&value.value)).map_err(|e| e.to_string())?; }
        None => { builder.build_return(None).map_err(|e| e.to_string())?; }
    }
    // Dieser Block ist nur an die nichttrappenden Argument-Operationen gebunden;
    // der Foreign-Call selbst darf ihn nicht als Fehlerfortsetzung verwenden.
    builder.position_at_end(failure);
    builder.build_unreachable().map_err(|e| e.to_string())?;
    Ok(())
}
