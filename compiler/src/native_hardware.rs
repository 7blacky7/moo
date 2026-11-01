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
        _ => return None,
    })
}

fn is_read(kind: Intrinsic) -> bool {
    matches!(kind, Intrinsic::VolatileRead | Intrinsic::PortRead(_))
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
    let has_pointer = params.iter().flatten().any(|ty| matches!(
        &ty.kind, TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger"
    ));
    if has_pointer {
        let nominal = crate::native_type_id::NominalTypes::new();
        return NativePointerSignature::from_annotations(params, result, &nominal);
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

    /// Ein Intrinsic ausgeben; Lesezugriffe liefern ihren typisierten Wert.
    fn intrinsic(&self, kind: Intrinsic, args: &[Expr]) -> Result<Option<NValue<'ctx>>, String> {
        match kind {
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
            let value = emitter.intrinsic(kind, args)?.ok_or("Lesezugriff liefert keinen Wert")?;
            if signature.result != Some(value.ty) {
                return Err(format!("Rückgabetyp passt nicht zum Lesezugriff ({:?})", value.ty));
            }
            emitter.builder.build_return(Some(&value.value)).map_err(|e| e.to_string())?;
            return Ok(());
        }
        // Lesen als Anweisung ist erlaubt (z. B. Register lesen, um es zu quittieren).
        emitter.intrinsic(kind, args)?;
    }
    if signature.result.is_some() {
        return Err("Hardwarefunktion mit Rückgabetyp endet ohne gib_zurück".into());
    }
    emitter.builder.build_return(None).map_err(|e| e.to_string())?;
    Ok(())
}
