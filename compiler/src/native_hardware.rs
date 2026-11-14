//! P7-T4 (Teil 6A, dazu Intrinsics aus 5B): typisierte Hardwarezugriffe in
//! `@nativ`-Funktionen. Nur innerhalb von `unsicher`, nur im Bare-Profil und
//! nur auf freigegebenen Zielen (x86_64-unknown-none/-uefi, aarch64-unknown-none).
//!
//! - `volatile_lesen(p)` / `volatile_lesen(p, index)` (EN `volatile_read`):
//!   volatiler Load mit exakt dem Zeigerzieltyp von `p: ptr[T]`, T ein
//!   Integer-Registertyp (u8..u64, i8..i64). Index wie beim Zeigerlesen:
//!   nichtnegatives Literal oder u32-Parameter (64-Bit-Ziel).
//! - `volatile_schreiben(p, wert)` / `volatile_schreiben(p, index, wert)`
//!   (EN `volatile_write`): `wert` muss exakt den Typ T haben.
//!   Vertrag des Aufrufers: Adresse natuerlich ausgerichtet (ABI-Alignment
//!   von T), gueltig und fuer die Dauer des Zugriffs gemappt.
//! - Barrieren: `speicherbarriere`/`memory_barrier` (x86_64 mfence, AArch64
//!   dmb sy), `lesebarriere`/`read_barrier` (lfence, dmb ld),
//!   `schreibbarriere`/`write_barrier` (sfence, dmb st), `compilerbarriere`/
//!   `compiler_barrier` (nur Compiler, keine Instruktion).
//! - Port-I/O (nur x86_64, Namen wie die dynamischen Builtins):
//!   `port_lese`/`port_in` (u8), `port_lese16`/`port_in16`, `port_lese32`/
//!   `port_in32`, `port_schreibe`/`port_out`, `port_schreibe16`/`port_out16`,
//!   `port_schreibe32`/`port_out32`; Port ist u16, Wert exakt u8/u16/u32.
//! Volatile ersetzt weder Barriere noch Atomics: die Reihenfolge zwischen
//! Geraeteregistern und DMA-Speicher legt der Aufrufer mit Barrieren fest.
//! - P7-T4 (5A) Inline-Assembler, nur mit benannten Argumenten:
//!   `assembler(ziel="x86_64", code="...", ausgang="=r", eingaenge={"r": a},
//!   clobber=["memory"])` (EN target/output/inputs). Pflicht: ziel (muss zum
//!   Modulziel passen), code, clobber (Liste, darf leer sein). ausgang (ein
//!   Register: "=r", "=&r", "={reg}") nur mit `gib_zurueck`, Typ = Rueckgabetyp.
//!   Eingaenge: Constraint "r", "{reg}" oder Ziffer (an Ausgang gebunden) ->
//!   typisierter Integer-/Zeigerparameter. Operanden im Code: `$N`/`${N:mod}`,
//!   ein woertliches `$` als `$$`. Immer sideeffect (volatile).
//!   Freie Strings ohne Constraints/Clobber sind ein Compilefehler.

use inkwell::context::Context;
use inkwell::targets::TargetData;
use inkwell::types::{BasicMetadataTypeEnum, BasicType};
use inkwell::values::{BasicMetadataValueEnum, BasicValueEnum, FunctionValue, IntValue, PointerValue};

use crate::ast::{Expr, Stmt, TypeKind, TypeRef};
use crate::codegen_native::{NType, NValue, NativeParameter, NativePointerSignature};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Barrier { Full, Read, Write, Compiler }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Intrinsic {
    VolatileRead,
    VolatileWrite,
    Barrier(Barrier),
    PortRead(u32),
    PortWrite(u32),
    Asm,
}

fn classify(name: &str) -> Option<Intrinsic> {
    Some(match name {
        "volatile_lesen" | "volatile_read" => Intrinsic::VolatileRead,
        "volatile_schreiben" | "volatile_write" => Intrinsic::VolatileWrite,
        "speicherbarriere" | "memory_barrier" => Intrinsic::Barrier(Barrier::Full),
        "lesebarriere" | "read_barrier" => Intrinsic::Barrier(Barrier::Read),
        "schreibbarriere" | "write_barrier" => Intrinsic::Barrier(Barrier::Write),
        "compilerbarriere" | "compiler_barrier" => Intrinsic::Barrier(Barrier::Compiler),
        "port_lese" | "port_in" => Intrinsic::PortRead(8),
        "port_lese16" | "port_in16" => Intrinsic::PortRead(16),
        "port_lese32" | "port_in32" => Intrinsic::PortRead(32),
        "port_schreibe" | "port_out" => Intrinsic::PortWrite(8),
        "port_schreibe16" | "port_out16" => Intrinsic::PortWrite(16),
        "port_schreibe32" | "port_out32" => Intrinsic::PortWrite(32),
        "assembler" => Intrinsic::Asm,
        _ => return None,
    })
}

fn is_read(kind: Intrinsic) -> bool {
    matches!(kind, Intrinsic::VolatileRead | Intrinsic::PortRead(_) | Intrinsic::Asm)
}

fn intrinsic_stmt(stmt: &Stmt) -> Option<(Intrinsic, &[Expr], bool)> {
    match stmt {
        Stmt::Expression(Expr::FunctionCall { name, args }) => classify(name).map(|k| (k, args.as_slice(), false)),
        Stmt::Return(Some(Expr::FunctionCall { name, args })) => classify(name).map(|k| (k, args.as_slice(), true)),
        _ => None,
    }
}

/// Ergebnis der Koerper-Erkennung fuer `@nativ`-Funktionen.
pub(crate) enum BodyKind {
    /// Kein Hardware-Intrinsic: bestehende Native-Pfade entscheiden.
    Other,
    /// `unsicher:` mit ausschliesslich Hardware-Intrinsics.
    Hardware,
    /// Hardware-Intrinsic ausserhalb von `unsicher`.
    MissingUnsafe,
}

pub(crate) fn classify_body(body: &[Stmt]) -> BodyKind {
    if let [Stmt::UnsafeBlock { body: inner }] = body {
        if !inner.is_empty() && inner.iter().all(|stmt| intrinsic_stmt(stmt).is_some()) {
            return BodyKind::Hardware;
        }
        return BodyKind::Other;
    }
    if body.iter().any(|stmt| intrinsic_stmt(stmt).is_some()) {
        return BodyKind::MissingUnsafe;
    }
    BodyKind::Other
}

/// Signatur: Zeiger (Adressraum 0) und skalare Parameter; Ergebnis skalar
/// oder void. Mit Zeiger wird die bestehende Zeigersignatur verwendet.
pub(crate) fn signature(params: &[Option<TypeRef>], result: Option<&TypeRef>) -> Result<NativePointerSignature, String> {
    signature_with_nominals(params, result, &crate::native_type_id::NominalTypes::new())
}

/// Bestehendes Signatur-Lowering mit lokal aufgeloesten nativen Strukturnamen.
pub(crate) fn signature_with_nominals(
    params: &[Option<TypeRef>], result: Option<&TypeRef>,
    nominal: &crate::native_type_id::NominalTypes,
) -> Result<NativePointerSignature, String> {
    let has_pointer = params.iter().flatten().any(|ty| matches!(
        &ty.kind, TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger"
    ));
    if has_pointer {
        return NativePointerSignature::from_annotations(params, result, nominal);
    }
    let mut native = Vec::with_capacity(params.len());
    for (index, param) in params.iter().enumerate() {
        let ty = param.as_ref().ok_or_else(|| format!("Typ des nativen Parameters {} fehlt", index + 1))?;
        native.push(NativeParameter::Scalar(NType::from_type_ref(ty)?));
    }
    let result = result.ok_or("Rueckgabetyp der nativen Funktion fehlt")?;
    let result = match &result.kind {
        TypeKind::Name(name) if name == "void" => None,
        _ => Some(NType::from_type_ref(result)?),
    };
    Ok(NativePointerSignature { params: native, result })
}

fn is_integer(ty: NType) -> bool {
    matches!(ty, NType::I8 | NType::I16 | NType::I32 | NType::I64 | NType::U8 | NType::U16 | NType::U32 | NType::U64)
}

fn port_type(bits: u32) -> NType {
    match bits { 8 => NType::U8, 16 => NType::U16, _ => NType::U32 }
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Arch { X86_64, AArch64 }

/// `{reg}` mit Kleinbuchstaben/Ziffern (z. B. {eax}, {dx}, {x0}).
pub(crate) fn register_constraint(constraint: &str) -> bool {
    constraint.len() > 2 && constraint.starts_with('{') && constraint.ends_with('}')
        && constraint[1..constraint.len() - 1].bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit())
}

/// Jeder Operandenverweis `$N` bzw. `${N...}` muss deklariert sein; `$$` ist ein `$`.
pub(crate) fn check_operands(code: &str, count: usize) -> Result<(), String> {
    let bytes = code.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] != b'$' { i += 1; continue; }
        let start = match bytes.get(i + 1) {
            Some(b'$') => { i += 2; continue; }
            Some(b'{') => i + 2,
            _ => i + 1,
        };
        let digits = bytes[start..].iter().take_while(|b| b.is_ascii_digit()).count();
        if digits == 0 {
            return Err("assembler: '$' ohne Operandennummer (wörtliches $ als $$ schreiben)".into());
        }
        let index: usize = code[start..start + digits].parse().map_err(|_| "assembler: Operandennummer zu groß".to_string())?;
        if index >= count {
            return Err(format!("assembler: Operand ${index} ist nicht deklariert ({count} Operanden)"));
        }
        i = start + digits;
    }
    Ok(())
}

pub(crate) struct AsmSpec<'e> {
    pub(crate) target: String,
    pub(crate) code: String,
    pub(crate) output: Option<String>,
    pub(crate) inputs: Vec<(String, &'e Expr)>,
    pub(crate) clobbers: Vec<String>,
}

impl<'e> AsmSpec<'e> {
    pub(crate) fn parse(args: &'e [Expr]) -> Result<Self, String> {
        let pairs = match args {
            [Expr::Dict(pairs)] => pairs,
            _ => return Err("assembler erwartet benannte Argumente (ziel=, code=, clobber=, optional ausgang=, eingaenge=); freie Assembler-Strings ohne Constraints sind nicht erlaubt".into()),
        };
        let text = |key: &str, value: &Expr| match value {
            Expr::String(s) => Ok(s.clone()),
            _ => Err(format!("assembler: {key}= erwartet ein Text-Literal")),
        };
        let (mut target, mut code, mut output, mut inputs, mut clobbers) = (None, None, None, Vec::new(), None);
        let mut seen = std::collections::HashSet::new();
        for (key, value) in pairs {
            let key = match key { Expr::String(key) => key.as_str(), _ => return Err("assembler: ungültiger Argumentname".into()) };
            if !seen.insert(key) {
                return Err(format!("assembler: {key}= doppelt angegeben"));
            }
            match key {
                "ziel" | "target" => target = Some(text(key, value)?),
                "code" => code = Some(text(key, value)?),
                "ausgang" | "output" => output = Some(text(key, value)?),
                "eingaenge" | "eingänge" | "inputs" => match value {
                    Expr::Dict(items) => for (constraint, operand) in items {
                        inputs.push((text(key, constraint)?, operand));
                    },
                    _ => return Err("assembler: eingaenge= erwartet {\"constraint\": parameter, ...}".into()),
                },
                "clobber" | "clobbers" => match value {
                    Expr::List(items) => clobbers = Some(items.iter().map(|item| text(key, item)).collect::<Result<Vec<_>, _>>()?),
                    _ => return Err("assembler: clobber= erwartet eine Liste von Text-Literalen".into()),
                },
                other => return Err(format!("assembler: unbekanntes Argument {other}=")),
            }
        }
        Ok(Self {
            target: target.ok_or("assembler: ziel= fehlt")?,
            code: code.ok_or("assembler: code= fehlt")?,
            output,
            inputs,
            clobbers: clobbers.ok_or("assembler: clobber= fehlt (leere Liste [] erlaubt, muss aber angegeben werden)")?,
        })
    }
}

struct Emitter<'a, 'ctx> {
    context: &'ctx Context,
    target: &'a TargetData,
    arch: Arch,
    function: FunctionValue<'ctx>,
    signature: &'a NativePointerSignature,
    param_names: &'a [String],
    builder: inkwell::builder::Builder<'ctx>,
}

impl<'a, 'ctx> Emitter<'a, 'ctx> {
    fn param(&self, name: &str) -> Result<(usize, &'a NativeParameter), String> {
        let index = self.param_names.iter().position(|candidate| candidate == name)
            .ok_or_else(|| format!("Hardwarezugriff: Parameter '{name}' nicht gefunden"))?;
        Ok((index, &self.signature.params[index]))
    }

    fn argument(&self, index: usize) -> Result<BasicValueEnum<'ctx>, String> {
        self.function.get_nth_param(index as u32).ok_or_else(|| "Nativer Parameter fehlt".to_string())
    }

    /// Skalarer Operand mit exakt erwartetem Typ: Parameter oder Literal.
    fn scalar(&self, expr: &Expr, expected: NType) -> Result<IntValue<'ctx>, String> {
        match expr {
            Expr::Identifier(name) => {
                let (index, param) = self.param(name)?;
                if !matches!(param, NativeParameter::Scalar(ty) if *ty == expected) {
                    return Err(format!("Hardwarezugriff: '{name}' hat nicht den Typ {expected:?}"));
                }
                match self.argument(index)? {
                    BasicValueEnum::IntValue(value) => Ok(value),
                    _ => Err("Hardwarezugriff: Parameter ist keine Ganzzahl".into()),
                }
            }
            Expr::NativeNumber { text, .. } => Ok(expected.integer_literal(self.context, text, false)?.value.into_int_value()),
            _ => Err("Hardwarezugriff: Operand muss ein typisierter Parameter oder ein Integerliteral sein".into()),
        }
    }

    /// Zeiger plus optionaler Index -> (Elementzeiger, Zieltyp, Quelltyp).
    fn element(&self, pointer: &Expr, index: Option<&Expr>) -> Result<(PointerValue<'ctx>, NType, TypeRef), String> {
        let name = match pointer {
            Expr::Identifier(name) => name,
            _ => return Err("Hardwarezugriff: erster Operand muss ein ptr[T]-Parameter sein".into()),
        };
        let (param_index, param) = self.param(name)?;
        let source = match param {
            NativeParameter::Pointer { source, .. } => source,
            _ => return Err(format!("Hardwarezugriff: '{name}' ist kein Zeigerparameter")),
        };
        let pointee = match &source.kind {
            TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 => &args[0],
            _ => return Err("Zeigerzieltyp fehlt".into()),
        };
        let ty = NType::from_type_ref(pointee)?;
        if !is_integer(ty) {
            return Err("Volatile-Zugriff erfordert einen Integer-Registertyp (u8..u64, i8..i64)".into());
        }
        let base = match self.argument(param_index)? {
            BasicValueEnum::PointerValue(value) => value,
            _ => return Err("LLVM-Argument ist kein Zeiger".into()),
        };
        let index = match index {
            None => return Ok((base, ty, pointee.clone())),
            Some(index) => index,
        };
        let index_type = crate::native_pointer::target_pointer_int(self.context, self.target)?;
        let element_size = self.target.get_abi_size(&ty.llvm_type(self.context));
        let index_value = match index {
            Expr::NativeNumber { text, .. } => {
                let number_type = if index_type.get_bit_width() == 32 { NType::U32 } else { NType::U64 };
                let value = number_type.integer_literal(self.context, text, false)?.value.into_int_value();
                let number = value.get_zero_extended_constant().ok_or("Hardwareindex ist nicht konstant")?;
                let max_offset = if index_type.get_bit_width() == 32 { i32::MAX as u64 } else { i64::MAX as u64 };
                if number.checked_mul(element_size).filter(|offset| *offset <= max_offset).is_none() {
                    return Err("Hardwareindex überschreitet den Zieladressraum".into());
                }
                value
            }
            Expr::Identifier(_) => {
                if index_type.get_bit_width() != 64 {
                    return Err("Variabler Hardwareindex benötigt ein 64-Bit-Ziel".into());
                }
                let value = self.scalar(index, NType::U32)?;
                self.builder.build_int_z_extend(value, index_type, "hw.index").map_err(|e| e.to_string())?
            }
            _ => return Err("Hardwareindex benötigt ein nichtnegatives Integerliteral oder einen u32-Parameter".into()),
        };
        // Nicht inbounds: Gueltigkeit und Bereich liegen beim unsicheren Aufrufer.
        let element = unsafe {
            self.builder.build_gep(ty.llvm_type(self.context), base, &[index_value], "hw.element")
        }.map_err(|e| e.to_string())?;
        Ok((element, ty, pointee.clone()))
    }

    fn native_pointer(&self, element: PointerValue<'ctx>, ty: NType, source: TypeRef) -> Result<crate::native_pointer::NativePointer<'ctx>, String> {
        let llvm = ty.llvm_type(self.context);
        // Vertrag von volatile_*: die Adresse ist natuerlich ausgerichtet.
        let alignment = self.target.get_abi_alignment(&llvm);
        crate::native_pointer::NativePointer::new(element, llvm, source, alignment)
    }

    fn asm_call(&self, asm: &str, constraints: &str, result: Option<NType>, args: &[BasicMetadataValueEnum<'ctx>]) -> Result<Option<BasicValueEnum<'ctx>>, String> {
        let arg_types: Vec<BasicMetadataTypeEnum<'ctx>> = args.iter().map(|arg| match arg {
            BasicMetadataValueEnum::IntValue(value) => Ok(value.get_type().into()),
            _ => Err("Inline-Assembler-Operand ist keine Ganzzahl".to_string()),
        }).collect::<Result<_, _>>()?;
        let fn_type = match result {
            Some(ty) => ty.llvm_type(self.context).fn_type(&arg_types, false),
            None => self.context.void_type().fn_type(&arg_types, false),
        };
        let asm = self.context.create_inline_asm(fn_type, asm.to_string(), constraints.to_string(), true, false, None, false);
        let call = self.builder.build_indirect_call(fn_type, asm, args, "hw.asm").map_err(|e| e.to_string())?;
        Ok(match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(value) => Some(value),
            _ => None,
        })
    }

    /// P7-T4 (5A): `assembler(...)` mit deklarierten Ein-/Ausgaengen und Clobbers.
    fn inline_asm(&self, args: &[Expr], result: Option<NType>) -> Result<Option<NValue<'ctx>>, String> {
        let spec = AsmSpec::parse(args)?;
        let arch = match self.arch { Arch::X86_64 => "x86_64", Arch::AArch64 => "aarch64" };
        if spec.target != arch {
            return Err(format!("assembler: ziel=\"{}\" passt nicht zum Modulziel {arch}", spec.target));
        }
        let mut constraints = Vec::new();
        let outputs = match (&spec.output, result) {
            (Some(output), Some(ty)) => {
                let register = output.strip_prefix("=&").or_else(|| output.strip_prefix('='));
                if !register.is_some_and(|r| r == "r" || register_constraint(r)) {
                    return Err(format!("assembler: ungültige Ausgangs-Constraint '{output}' (=r, =&r, ={{reg}})"));
                }
                if !is_integer(ty) {
                    return Err("assembler: ausgang verlangt einen Integer-Rückgabetyp".into());
                }
                constraints.push(output.clone());
                1
            }
            (Some(_), None) => return Err("assembler: ausgang= ist nur mit gib_zurück erlaubt".into()),
            (None, Some(_)) => return Err("assembler: gib_zurück verlangt ausgang=".into()),
            (None, None) => 0,
        };
        let mut values: Vec<BasicMetadataValueEnum<'ctx>> = Vec::new();
        let mut types: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
        for (constraint, expr) in &spec.inputs {
            let tied = constraint.len() == 1 && constraint.chars().all(|c| c.is_ascii_digit())
                && constraint.parse::<usize>().is_ok_and(|n| n < outputs);
            if !(constraint == "r" || register_constraint(constraint) || tied) {
                return Err(format!("assembler: ungültige Eingangs-Constraint '{constraint}' (r, {{reg}}, Ausgangsnummer)"));
            }
            let name = match expr {
                Expr::Identifier(name) => name,
                _ => return Err("assembler: Eingänge müssen typisierte Parameter sein".into()),
            };
            let (index, param) = self.param(name)?;
            if !matches!(param, NativeParameter::Pointer { .. })
                && !matches!(param, NativeParameter::Scalar(ty) if is_integer(*ty))
            {
                return Err(format!("assembler: Eingang '{name}' muss Integer oder Zeiger sein"));
            }
            let value = self.argument(index)?;
            types.push(value.get_type().into());
            values.push(value.into());
            constraints.push(constraint.clone());
        }
        for clobber in &spec.clobbers {
            if clobber.is_empty() || !clobber.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit()) {
                return Err(format!("assembler: ungültiger Clobber '{clobber}'"));
            }
            constraints.push(format!("~{{{clobber}}}"));
        }
        check_operands(&spec.code, outputs + spec.inputs.len())?;
        let fn_type = match result {
            Some(ty) => ty.llvm_type(self.context).fn_type(&types, false),
            None => self.context.void_type().fn_type(&types, false),
        };
        let asm = self.context.create_inline_asm(fn_type, spec.code.clone(), constraints.join(","), true, false, None, false);
        let call = self.builder.build_indirect_call(fn_type, asm, &values, "hw.asm").map_err(|e| e.to_string())?;
        match (result, call.try_as_basic_value()) {
            (Some(ty), inkwell::values::ValueKind::Basic(value)) => Ok(Some(NValue::checked(self.context, ty, value)?)),
            (None, _) => Ok(None),
            _ => Err("assembler: Ausgang liefert keinen Wert".into()),
        }
    }

    /// Ein Intrinsic ausgeben; Lesezugriffe liefern ihren typisierten Wert.
    fn intrinsic(&self, kind: Intrinsic, args: &[Expr], returns: bool) -> Result<Option<NValue<'ctx>>, String> {
        match kind {
            Intrinsic::Asm => self.inline_asm(args, if returns { self.signature.result } else { None }),
            Intrinsic::VolatileRead => {
                let (element, ty, source) = match args {
                    [pointer] => self.element(pointer, None)?,
                    [pointer, index] => self.element(pointer, Some(index))?,
                    _ => return Err("volatile_lesen erwartet (zeiger) oder (zeiger, index)".into()),
                };
                let _ = ty;
                let pointer = self.native_pointer(element, ty, source)?;
                Ok(Some(pointer.load_scalar(self.context, &self.builder, self.target, true, true, "hw.volatile.lesen")?))
            }
            Intrinsic::VolatileWrite => {
                let ((element, ty, source), value) = match args {
                    [pointer, value] => (self.element(pointer, None)?, value),
                    [pointer, index, value] => (self.element(pointer, Some(index))?, value),
                    _ => return Err("volatile_schreiben erwartet (zeiger, wert) oder (zeiger, index, wert)".into()),
                };
                let value = self.scalar(value, ty)?;
                let value = NValue::checked(self.context, ty, value.into())?;
                let pointer = self.native_pointer(element, ty, source)?;
                pointer.store_scalar(self.context, &self.builder, self.target, true, true, value)?;
                Ok(None)
            }
            Intrinsic::Barrier(barrier) => {
                if !args.is_empty() {
                    return Err("Barrieren erwarten keine Argumente".into());
                }
                let asm = match (self.arch, barrier) {
                    (_, Barrier::Compiler) => "",
                    (Arch::X86_64, Barrier::Full) => "mfence",
                    (Arch::X86_64, Barrier::Read) => "lfence",
                    (Arch::X86_64, Barrier::Write) => "sfence",
                    (Arch::AArch64, Barrier::Full) => "dmb sy",
                    (Arch::AArch64, Barrier::Read) => "dmb ld",
                    (Arch::AArch64, Barrier::Write) => "dmb st",
                };
                self.asm_call(asm, "~{memory}", None, &[])?;
                Ok(None)
            }
            Intrinsic::PortRead(bits) => {
                if self.arch != Arch::X86_64 {
                    return Err("Port-I/O gibt es nur auf x86_64".into());
                }
                let port = match args {
                    [port] => self.scalar(port, NType::U16)?,
                    _ => return Err("port_lese* erwartet (port: u16)".into()),
                };
                let (asm, constraints) = match bits {
                    8 => ("inb $1, $0", "={al},{dx},~{memory}"),
                    16 => ("inw $1, $0", "={ax},{dx},~{memory}"),
                    _ => ("inl $1, $0", "={eax},{dx},~{memory}"),
                };
                let ty = port_type(bits);
                let value = self.asm_call(asm, constraints, Some(ty), &[port.into()])?
                    .ok_or("Port-Lesen liefert keinen Wert")?;
                Ok(Some(NValue::checked(self.context, ty, value)?))
            }
            Intrinsic::PortWrite(bits) => {
                if self.arch != Arch::X86_64 {
                    return Err("Port-I/O gibt es nur auf x86_64".into());
                }
                let ty = port_type(bits);
                let (port, value) = match args {
                    [port, value] => (self.scalar(port, NType::U16)?, self.scalar(value, ty)?),
                    _ => return Err("port_schreibe* erwartet (port: u16, wert)".into()),
                };
                let (asm, constraints) = match bits {
                    8 => ("outb $0, $1", "{al},{dx},~{memory}"),
                    16 => ("outw $0, $1", "{ax},{dx},~{memory}"),
                    _ => ("outl $0, $1", "{eax},{dx},~{memory}"),
                };
                self.asm_call(asm, constraints, None, &[value.into(), port.into()])?;
                Ok(None)
            }
        }
    }
}

/// P7-T4 (4A): x86_64-Interruptvertrag fuer `@nativ @unterbrechung(vektor)`.
/// Vektoren 8, 10-14, 17, 21, 29, 30 legen einen Fehlercode ab:
/// `(rahmen: ptr[u64], fehlercode: u64) -> void`, alle anderen
/// `(rahmen: ptr[u64]) -> void`. rahmen zeigt auf RIP, CS, RFLAGS, RSP, SS.
/// LLVM x86_intrcc sichert alle benutzten Register und kehrt mit iretq
/// zurueck (Fehlercode wird vorher entfernt). Nur Allzweckregister.
/// EOI, Stackwechsel (IST) und Synchronisation bleiben beim Handler bzw. IDT.
pub(crate) fn configure_interrupt<'ctx>(
    context: &'ctx Context,
    function: FunctionValue<'ctx>,
    signature: &NativePointerSignature,
    vector: u8,
    triple: &str,
) -> Result<(), String> {
    use inkwell::attributes::{Attribute, AttributeLoc};
    use inkwell::types::AnyType;
    if !matches!(triple, "x86_64-unknown-none" | "x86_64-unknown-uefi") {
        return Err(format!("@unterbrechung an @nativ: Ziel '{triple}' nicht unterstützt (aarch64: @nackt-Trampolin mit assembler, 4B)"));
    }
    let frame = match signature.params.first() {
        Some(NativeParameter::Pointer { source, .. }) => matches!(&source.kind,
            TypeKind::Generic { args, .. } if matches!(&args[0].kind, TypeKind::Name(n) if n == "u64")),
        _ => false,
    };
    let error_code = matches!(vector, 8 | 10..=14 | 17 | 21 | 29 | 30);
    let shape = match signature.params.as_slice() {
        [_] => frame && !error_code,
        [_, NativeParameter::Scalar(NType::U64)] => frame && error_code,
        _ => false,
    };
    if !shape {
        return Err(if error_code {
            format!("@unterbrechung({vector}): Vektor legt einen Fehlercode ab, Signatur (rahmen: ptr[u64], fehlercode: u64) -> void")
        } else {
            format!("@unterbrechung({vector}): Vektor ohne Fehlercode, Signatur (rahmen: ptr[u64]) -> void")
        });
    }
    if signature.result.is_some() {
        return Err("@unterbrechung: Rückgabetyp muss void sein".into());
    }
    function.set_call_conventions(83); // x86_intrcc
    let byval = Attribute::get_named_enum_kind_id("byval");
    let frame_type = context.i64_type().array_type(5).as_any_type_enum();
    function.add_attribute(AttributeLoc::Param(0), context.create_type_attribute(byval, frame_type));
    function.add_attribute(AttributeLoc::Function,
        context.create_enum_attribute(Attribute::get_named_enum_kind_id("noinline"), 0));
    function.add_attribute(AttributeLoc::Function,
        context.create_string_attribute("target-features", "-mmx,-sse,-sse2,-avx,-x87"));
    Ok(())
}

/// Koerper einer Hardwarefunktion ausgeben (Erkennung: classify_body).
pub(crate) fn emit_hardware_function<'ctx>(
    context: &'ctx Context,
    target: &TargetData,
    triple: &str,
    function: FunctionValue<'ctx>,
    signature: &NativePointerSignature,
    param_names: &[String],
    body: &[Stmt],
) -> Result<(), String> {
    let arch = match triple {
        "x86_64-unknown-none" | "x86_64-unknown-uefi" => Arch::X86_64,
        "aarch64-unknown-none" => Arch::AArch64,
        _ => return Err(format!("Hardwarezugriff: Ziel '{triple}' noch nicht freigegeben")),
    };
    if function.get_type() != signature.llvm_type(context)
        || param_names.len() != signature.params.len()
        || function.count_basic_blocks() != 0
    {
        return Err("Native Hardwarefunktion hat eine inkonsistente Signatur".into());
    }
    let mut names = std::collections::HashSet::new();
    if param_names.iter().any(|name| !names.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let inner = match body {
        [Stmt::UnsafeBlock { body }] => body.as_slice(),
        _ => return Err("Hardwarezugriff erfordert unsicher".into()),
    };
    let entry = context.append_basic_block(function, "entry");
    let builder = context.create_builder();
    builder.position_at_end(entry);
    let emitter = Emitter { context, target, arch, function, signature, param_names, builder };
    // P7-T4 (3A): @nackt -> nur assembler ohne Ein-/Ausgang, kein Prolog/Epilog,
    // kein ret: Ruecksprung bzw. Nichtrueckkehr liegt beim Assemblercode.
    let naked_kind = inkwell::attributes::Attribute::get_named_enum_kind_id("naked");
    if function.get_enum_attribute(inkwell::attributes::AttributeLoc::Function, naked_kind).is_some() {
        for stmt in inner {
            match intrinsic_stmt(stmt) {
                Some((Intrinsic::Asm, args, false)) => {
                    let spec = AsmSpec::parse(args)?;
                    if !spec.inputs.is_empty() || spec.output.is_some() {
                        return Err("@nackt: assembler ohne eingaenge=/ausgang= (Parameter liegen in den ABI-Registern)".into());
                    }
                    emitter.intrinsic(Intrinsic::Asm, args, false)?;
                }
                _ => return Err("@nackt erlaubt nur assembler(...)-Anweisungen (kein gib_zurück, keine anderen Intrinsics)".into()),
            }
        }
        emitter.builder.build_unreachable().map_err(|e| e.to_string())?;
        return Ok(());
    }
    let last = inner.len() - 1;
    for (position, stmt) in inner.iter().enumerate() {
        let (kind, args, returns) = intrinsic_stmt(stmt).ok_or("Hardwarefunktion enthält eine nicht unterstützte Anweisung")?;
        if returns {
            if position != last {
                return Err("gib_zurück muss die letzte Anweisung der Hardwarefunktion sein".into());
            }
            if !is_read(kind) {
                return Err("gib_zurück verlangt einen Lesezugriff (volatile_lesen oder port_lese*)".into());
            }
            let value = emitter.intrinsic(kind, args, true)?.ok_or("Lesezugriff liefert keinen Wert")?;
            if signature.result != Some(value.ty) {
                return Err(format!("Rückgabetyp passt nicht zum Lesezugriff ({:?})", value.ty));
            }
            emitter.builder.build_return(Some(&value.value)).map_err(|e| e.to_string())?;
            return Ok(());
        }
        // Lesen als Anweisung ist erlaubt (z. B. Register lesen, um es zu quittieren).
        emitter.intrinsic(kind, args, false)?;
    }
    if signature.result.is_some() {
        return Err("Hardwarefunktion mit Rückgabetyp endet ohne gib_zurück".into());
    }
    emitter.builder.build_return(None).map_err(|e| e.to_string())?;
    Ok(())
}
