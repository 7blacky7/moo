//! Zielgebundene Validierung und LLVM-Absenkung der ersten extern-C-Typmenge.
//! Der angebundene skalare Call-Pfad ist eng begrenzt; Pointer, Callbacks und
//! weitere ABI-Varianten bleiben bis zum jeweiligen Vertrag ausgeschlossen.

use crate::ast::{TypeKind, TypeRef};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum FfiType {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    Pointer,
}

#[derive(Clone, Debug)]
pub(crate) struct FfiSignature {
    params: Vec<FfiType>,
    result: Option<FfiType>,
    target: String,
    // Vollstaendige Quelltypen bleiben getrennt vom opaken LLVM-Pointer erhalten.
    source_params: Vec<TypeRef>,
    source_result: Option<TypeRef>,
    semantic_params: Vec<crate::native_type_id::NativeTypeId>,
    semantic_result: crate::native_type_id::NativeTypeId,
    // P7-T4 (7A): LLVM-CallingConv (0 = Zieldefault) und Quellname der Konvention.
    convention: u32,
    semantic_convention: Option<String>,
}

fn error(ty: &TypeRef, message: &str) -> String {
    format!("extern C, Zeile {} Spalte {}: {}", ty.line, ty.column, message)
}

/// P7-T4 (7A): benannte Aufrufkonventionen fuer `@aufrufkonvention("...")`
/// (EN `@calling_convention`) und Funktionszeigertypen `fn[...](...) -> T`.
pub(crate) const CALL_CONVENTIONS: [&str; 3] = ["sysv64", "win64", "uefi"];

const CC_C: u32 = 0;
const CC_X86_64_SYSV: u32 = 78;
const CC_WIN64: u32 = 79;

/// Name + Zieltriple -> LLVM-CallingConv. "uefi" ist ein zielabhaengiger
/// Alias: x86_64 -> win64 (MS x64), aarch64 -> AAPCS64 (= C). Entspricht die
/// Konvention dem Zieldefault, bleibt sie 0, damit annotierte und
/// unannotierte Aufrufe gleich abgesenkt werden. Unbekannte Paare: Fehler.
pub(crate) fn llvm_call_convention(name: &str, triple: &str) -> Result<u32, String> {
    let arch = triple.split('-').next().unwrap_or("");
    let requested = match (arch, name) {
        ("x86_64", "sysv64") => CC_X86_64_SYSV,
        ("x86_64", "win64" | "uefi") => CC_WIN64,
        ("aarch64", "uefi") => CC_C,
        _ if !CALL_CONVENTIONS.contains(&name) =>
            return Err(format!("Unbekannte Aufrufkonvention '{name}' (erlaubt: sysv64, win64, uefi)")),
        _ => return Err(format!("Aufrufkonvention '{name}' ist für Ziel '{triple}' nicht freigegeben")),
    };
    let default = match arch {
        "x86_64" if triple.contains("windows") || triple.contains("uefi") || triple.contains("mingw") => CC_WIN64,
        "x86_64" => CC_X86_64_SYSV,
        _ => CC_C,
    };
    Ok(if requested == default { CC_C } else { requested })
}

pub(crate) fn is_call_convention_decorator(decorator: &crate::ast::Decorator) -> bool {
    decorator.name == "aufrufkonvention" || decorator.name == "calling_convention"
}

/// `@aufrufkonvention("...")` aus einer Decoratorliste (hoechstens einmal).
pub(crate) fn decorator_call_convention(decorators: &[crate::ast::Decorator], triple: &str) -> Result<u32, String> {
    let mut found = None;
    for decorator in decorators.iter().filter(|d| is_call_convention_decorator(d)) {
        if found.is_some() {
            return Err("@aufrufkonvention darf nur einmal angegeben werden".into());
        }
        match decorator.args.as_slice() {
            [crate::ast::DecoratorArg::Text(name)] => found = Some(llvm_call_convention(name, triple)?),
            _ => return Err(format!(
                "@{} erwartet genau ein Text-Argument, z.B. @aufrufkonvention(\"win64\")", decorator.name
            )),
        }
    }
    Ok(found.unwrap_or(CC_C))
}

/// Direkte Aufrufe uebernehmen die explizite Konvention ihres Ziels (sysv64/
/// win64). Andere Konventionen (z. B. Interrupt) bleiben unberuehrt; eine
/// widerspruechliche, bereits gesetzte Aufrufkonvention ist ein Fehler.
pub(crate) fn sync_direct_call_conventions(module: &inkwell::module::Module<'_>) -> Result<(), String> {
    use inkwell::values::{CallSiteValue, InstructionOpcode};
    for function in module.get_functions() {
        for block in function.get_basic_blocks() {
            let mut instruction = block.get_first_instruction();
            while let Some(current) = instruction {
                instruction = current.get_next_instruction();
                if current.get_opcode() != InstructionOpcode::Call { continue; }
                let Ok(call) = CallSiteValue::try_from(current) else { continue };
                let Some(callee) = call.get_called_fn_value() else { continue };
                let wanted = callee.get_call_conventions();
                if !matches!(wanted, CC_X86_64_SYSV | CC_WIN64) || call.get_call_convention() == wanted { continue; }
                if call.get_call_convention() != CC_C {
                    return Err("Aufrufkonvention der Aufrufstelle widerspricht dem Ziel".into());
                }
                call.set_call_convention(wanted);
            }
        }
    }
    Ok(())
}

fn primitive(name: &str) -> Option<FfiType> {
    Some(match name {
        "i8" => FfiType::I8, "i16" => FfiType::I16,
        "i32" => FfiType::I32, "i64" => FfiType::I64,
        "u8" => FfiType::U8, "u16" => FfiType::U16,
        "u32" => FfiType::U32, "u64" => FfiType::U64,
        "f32" => FfiType::F32, "f64" => FfiType::F64,
        _ => return None,
    })
}

fn pointee_supported(ty: &TypeRef) -> bool {
    match &ty.kind {
        TypeKind::Name(name) => name == "void" || primitive(name).is_some(),
        TypeKind::Generic { name, args }
            if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
                pointee_supported(&args[0]),
        _ => false,
    }
}

fn value_type(ty: &TypeRef) -> Result<FfiType, String> {
    match &ty.kind {
        TypeKind::Name(name) if name == "bool" =>
            Err(error(ty, "C-bool ist bis zum ABI-Nachweis nicht freigegeben")),
        TypeKind::Name(name) => primitive(name).ok_or_else(||
            error(ty, "unbekannter oder nicht unterstuetzter C-Werttyp")),
        TypeKind::Generic { name, args } if name == "ptr" || name == "zeiger" => {
            if args.len() != 1 || !pointee_supported(&args[0]) {
                return Err(error(ty, "Pointer braucht genau einen unterstuetzten Zieltyp"));
            }
            Ok(FfiType::Pointer)
        }
        _ => Err(error(ty, "Aggregate und Funktionstypen by value sind nicht freigegeben")),
    }
}


/// Untrennbare Bindung einer validierten C-Signatur an genau deren Deklaration.
pub(crate) struct ExternBinding<'ctx> {
    signature: FfiSignature,
    function: inkwell::values::FunctionValue<'ctx>,
}

impl FfiType {
    fn llvm_type<'ctx>(self, context: &'ctx inkwell::context::Context) -> inkwell::types::BasicTypeEnum<'ctx> {
        use inkwell::AddressSpace;
        match self {
            Self::I8 | Self::U8 => context.i8_type().into(),
            Self::I16 | Self::U16 => context.i16_type().into(),
            Self::I32 | Self::U32 => context.i32_type().into(),
            Self::I64 | Self::U64 => context.i64_type().into(),
            Self::F32 => context.f32_type().into(),
            Self::F64 => context.f64_type().into(),
            Self::Pointer => context.ptr_type(AddressSpace::default()).into(),
        }
    }

    fn extension(self, target: &str) -> Option<&'static str> {
        // Win64 und AArch64-Linux verwenden hier keine LLVM-Extension-Attribute.
        if target.contains("-windows-") || target == "x86_64-unknown-uefi"
            || target == "aarch64-unknown-linux-gnu"
            || target == "aarch64-unknown-none" { return None; }
        match self {
            Self::I8 | Self::I16 => Some("signext"),
            Self::U8 | Self::U16 => Some("zeroext"),
            _ => None,
        }
    }
}

impl FfiSignature {
    /// Zielabhaengige C-Deklaration. Attribute muessen auch spaeter an jedem
    /// erzeugten Call-Site uebernommen und dort gesondert getestet werden.
    pub(crate) fn declare<'ctx>(
        &self,
        context: &'ctx inkwell::context::Context,
        module: &inkwell::module::Module<'ctx>,
        name: &str,
    ) -> Result<ExternBinding<'ctx>, String> {
        use inkwell::attributes::{Attribute, AttributeLoc};
        use inkwell::types::{BasicMetadataTypeEnum, BasicType};
        let module_triple = module.get_triple();
        let module_triple = module_triple.as_str().to_str().map_err(|e| format!("Ungueltiges Ziel-Triple: {e}"))?;
        if module_triple != self.target {
            return Err("extern C: Modul und Signatur haben unterschiedliche Ziele".to_string());
        }
        if self.source_result.is_none() {
            return Err("Indirekte Signatur darf kein externes Symbol deklarieren".into());
        }
        if module.get_function(name).is_some() {
            return Err(format!("extern C: Symbolkollision bei '{name}'"));
        }
        let params: Vec<BasicMetadataTypeEnum<'ctx>> =
            self.params.iter().map(|ty| ty.llvm_type(context).into()).collect();
        let function_type = match self.result {
            Some(ty) => ty.llvm_type(context).fn_type(&params, false),
            None => context.void_type().fn_type(&params, false),
        };
        let function = module.add_function(name, function_type, None);
        if self.convention != CC_C {
            function.set_call_conventions(self.convention);
        }
        let apply = |location, ty: FfiType| {
            if let Some(name) = ty.extension(&self.target) {
                let kind = Attribute::get_named_enum_kind_id(name);
                function.add_attribute(location, context.create_enum_attribute(kind, 0));
            }
        };
        for (index, ty) in self.params.iter().enumerate() {
            apply(AttributeLoc::Param(index as u32), *ty);
        }
        if let Some(ty) = self.result { apply(AttributeLoc::Return, ty); }
        Ok(ExternBinding { signature: self.clone(), function })
    }
}


impl FfiType {
    fn native_scalar(self) -> Option<crate::codegen_native::NType> {
        use crate::codegen_native::NType;
        Some(match self {
            Self::I8 => NType::I8, Self::I16 => NType::I16,
            Self::I32 => NType::I32, Self::I64 => NType::I64,
            Self::U8 => NType::U8, Self::U16 => NType::U16,
            Self::U32 => NType::U32, Self::U64 => NType::U64,
            Self::F32 => NType::F32, Self::F64 => NType::F64,
            Self::Pointer => return None,
        })
    }
}

/// Enger, durch getrennte C-Clang-Referenz belegter x86_64-ABI-Vertrag:
/// reine u32-Werte (1..4/6), f64+u32, vier f32 sowie packed u8+u32.
/// SysV klassifiziert Integer-/SSE-Register, Win64 Aggregate >8 Bytes
/// indirekt. Keine allgemeine Aggregate-/Variadic-Klassifizierung.
/// Alle anderen Aggregate bleiben bis zum vollständigen ABI-Classifier gesperrt.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum StructAbiParam { U64, Struct }

/// Konkrete AAPCS64-C-Formen aus der Clang-aarch64-Referenz.
/// Dies ist absichtlich kein allgemeiner Aggregate-Classifier.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum AArch64AggregateAbi {
    /// C-Argument i64 (8-Byte-Slot), C-Rueckgabe i32.
    U32,
    /// C-Argument/Rueckgabe i64.
    U64,
    /// C-Argument/Rueckgabe [2 x i64] fuer 12/16 Byte.
    Pair64,
    /// C-Rueckgabe via sret, Argument via normalen Zeiger.
    Indirect,
}

/// SysV x86_64: ein mehrteiliges Aggregat wird GANZ als byval-
/// Speicherargument uebergeben, wenn nicht alle benoetigten INTEGER-
/// bzw. SSE-Register frei sind. Kleine einzelne Registerteile werden
/// weiterhin vom LLVM-Backend korrekt im Stack-Fall abgelegt.
fn classify_struct_argument_storage<'ctx>(
    params: &[StructAbiParam],
    registers: &[inkwell::types::BasicTypeEnum<'ctx>],
    sysv: bool,
) -> Vec<bool> {
    use inkwell::types::BasicTypeEnum;
    let mut gp_free: usize = if sysv && registers.is_empty() { 5 } else { 6 };
    let mut sse_free: usize = 8;
    let gp_need = registers.iter()
        .filter(|reg| matches!(reg, BasicTypeEnum::IntType(_))).count();
    let sse_need = registers.iter().filter(|reg|
        matches!(reg, BasicTypeEnum::FloatType(_) | BasicTypeEnum::VectorType(_))
    ).count();
    let mut result = Vec::with_capacity(params.len());
    for param in params {
        match param {
            StructAbiParam::U64 => {
                if sysv { gp_free = gp_free.saturating_sub(1); }
                result.push(false);
            }
            StructAbiParam::Struct => {
                let spill = registers.is_empty() || (sysv && registers.len() > 1
                    && (gp_need > gp_free || sse_need > sse_free));
                if sysv && !spill {
                    gp_free = gp_free.saturating_sub(gp_need);
                    sse_free = sse_free.saturating_sub(sse_need);
                }
                result.push(spill);
            }
        }
    }
    result
}

pub(crate) struct StructRoundtripBinding<'ctx> {
    function: inkwell::values::FunctionValue<'ctx>,
    struct_name: String,
    param_kinds: Vec<StructAbiParam>,
    llvm_struct: inkwell::types::StructType<'ctx>,
    /// Exakte Rueckgabe-Registerklasse (INTEGER/SSE).
    /// Leere Liste bedeutet sret; fuer Argumente entscheidet
    /// unabhaengig davon indirect_args nach dem verfuegbaren Registerbudget.
    registers: Vec<inkwell::types::BasicTypeEnum<'ctx>>,
    /// Eingabeparameter, die anstelle von 1-2 Registern als Zeiger/byval
    /// uebergeben werden, unabhaengig von der Rueckgabe-Klassifizierung.
    indirect_args: Vec<bool>,
    /// AArch64 nutzt eigene Eingabe- und Rueckgabeformen (AAPCS64).
    aarch64: Option<AArch64AggregateAbi>,
    /// SysV markiert jeden indirekten Struct-Parameter mit byval;
    /// Win64 uebergibt einen normalen Zeiger.
    memory_byval: bool,
    sret_alignment: u64,
}

impl<'ctx> StructRoundtripBinding<'ctx> {
    pub(crate) fn struct_name(&self) -> &str { &self.struct_name }

    /// Erwartete Eingabewerte in der Moo-Quellreihenfolge: erst optionale
    /// u64-Praefixe, dann ein oder zwei nominal gleiche Strukturwerte.
    pub(crate) fn param_kinds(&self) -> &[StructAbiParam] { &self.param_kinds }

    /// ABI-Lowering fuer mehrere C-Argumente mit einem Struktur-Ergebnis.
    /// Allocas bleiben im Entry; Zieltriple bestimmt Register und byval.
    pub(crate) fn emit_roundtrip(
        &self, context: &'ctx inkwell::context::Context,
        builder: &inkwell::builder::Builder<'ctx>,
        values: &[inkwell::values::BasicValueEnum<'ctx>],
    ) -> Result<inkwell::values::BasicValueEnum<'ctx>, String> {
        use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, ValueKind};
        if values.len() != self.param_kinds.len() {
            return Err(format!("C-ABI '{}': falsche Argumentzahl", self.struct_name));
        }
        let entry = builder.get_insert_block()
            .and_then(|block| block.get_parent())
            .and_then(|function| function.get_first_basic_block())
            .ok_or("C-ABI: kein Funktions-Entry fuer Struct-Aufruf")?;
        let alloc_builder = context.create_builder();
        if let Some(first) = entry.get_first_instruction() {
            alloc_builder.position_before(&first);
        } else {
            alloc_builder.position_at_end(entry);
        }
        let dest = alloc_builder.build_alloca(self.llvm_struct, "ffi.struct.dst")
            .map_err(|e| e.to_string())?;
        let mut args: Vec<BasicMetadataValueEnum<'ctx>> = Vec::new();
        let mut byval_indices = Vec::new();
        if self.registers.is_empty() { args.push(dest.into()); }

        for (index, (value, kind)) in values.iter().zip(&self.param_kinds).enumerate() {
            match kind {
                StructAbiParam::U64 => {
                    if value.get_type() != context.i64_type().into() {
                        return Err(format!("C-ABI: Argument {} erwartet u64", index + 1));
                    }
                    args.push((*value).into());
                }
                StructAbiParam::Struct => {
                    if value.get_type() != self.llvm_struct.into() {
                        return Err(format!("C-ABI: Argument {} hat falschen Strukturtyp", index + 1));
                    }
                    // AArch64: fuer ein 12-Byte-Aggregat braucht Clang
                    // einen 16-Byte-Register-Slot [2 x i64]. Den Puffer
                    // vollstaendig nullen, BEVOR die echte Struktur hinein
                    // geschrieben wird; sonst wird Padding ueberlesen.
                    let payload = if self.aarch64.is_some() && !self.registers.is_empty() {
                        self.registers[0]
                    } else {
                        self.llvm_struct.into()
                    };
                    let source = alloc_builder.build_alloca(payload, "ffi.struct.src")
                        .map_err(|e| e.to_string())?;
                    source.as_instruction_value().ok_or("C-ABI: Entry-Alloca fehlt")?
                        .set_alignment(8).map_err(|e| e.to_string())?;
                    if let Some(kind) = self.aarch64 {
                        let zero: Option<BasicValueEnum<'ctx>> = match kind {
                            AArch64AggregateAbi::U32 | AArch64AggregateAbi::U64 =>
                                Some(context.i64_type().const_zero().into()),
                            AArch64AggregateAbi::Pair64 =>
                                Some(context.i64_type().array_type(2).const_zero().into()),
                            AArch64AggregateAbi::Indirect => None,
                        };
                        if let Some(zero) = zero {
                            builder.build_store(source, zero).map_err(|e| e.to_string())?;
                        }
                    }
                    builder.build_store(source, *value).map_err(|e| e.to_string())?;
                    if self.indirect_args[index] {
                        if self.memory_byval { byval_indices.push(args.len() as u32); }
                        args.push(source.into());
                    } else if self.aarch64.is_some() {
                        // AAPCS64: ein einzelner, ggf. gepolsterter LLVM-Wert
                        // statt eines SysV-/Win64-Registervektors.
                        let part = builder.build_load(payload, source, "ffi.arm64.arg")
                            .map_err(|e| e.to_string())?;
                        part.as_instruction_value().ok_or("C-ABI: ARM64-Load fehlt")?
                            .set_alignment(1).map_err(|e| e.to_string())?;
                        args.push(part.into());
                    } else {
                        for (part_index, register) in self.registers.iter().enumerate() {
                            let address = if part_index == 0 { source } else {
                                unsafe { builder.build_gep(context.i8_type(), source,
                                    &[context.i64_type().const_int((part_index * 8) as u64, false)],
                                    "ffi.struct.arg.offset") }.map_err(|e| e.to_string())?
                            };
                            let part = builder.build_load(*register, address, "ffi.struct.arg")
                                .map_err(|e| e.to_string())?;
                            part.as_instruction_value().ok_or("C-ABI: Register-Load fehlt")?
                                .set_alignment(1).map_err(|e| e.to_string())?;
                            args.push(part.into());
                        }
                    }
                }
            }
        }
        let call = builder.build_call(self.function, &args,
            if self.registers.is_empty() { "" } else { "ffi.struct.call" })
            .map_err(|e| e.to_string())?;
        call.set_call_convention(self.function.get_call_conventions());
        use inkwell::attributes::{Attribute, AttributeLoc};
        let align = Attribute::get_named_enum_kind_id("align");
        for index in byval_indices {
            let byval = Attribute::get_named_enum_kind_id("byval");
            call.add_attribute(AttributeLoc::Param(index),
                context.create_type_attribute(byval, self.llvm_struct.into()));
            call.add_attribute(AttributeLoc::Param(index),
                context.create_enum_attribute(align, 8));
        }
        if self.registers.is_empty() {
            use inkwell::attributes::{Attribute, AttributeLoc};
            let sret = Attribute::get_named_enum_kind_id("sret");
            let align = Attribute::get_named_enum_kind_id("align");
            call.add_attribute(AttributeLoc::Param(0),
                context.create_type_attribute(sret, self.llvm_struct.into()));
            call.add_attribute(AttributeLoc::Param(0),
                context.create_enum_attribute(align, self.sret_alignment));
            let result = builder.build_load(self.llvm_struct, dest, "ffi.struct.sret")
                .map_err(|e| e.to_string())?;
            result.as_instruction_value().ok_or("C-ABI: sret-Load fehlt")?
                .set_alignment(1).map_err(|e| e.to_string())?;
            return Ok(result);
        }
        let ValueKind::Basic(ret) = call.try_as_basic_value() else {
            return Err("C-ABI: Struktur-Rueckgabe fehlt".into());
        };
        if let Some(kind) = self.aarch64 {
            if kind == AArch64AggregateAbi::Indirect {
                return Err("C-ABI: ARM64-Indirekt-Rueckgabe ohne sret".into());
            }
            // AAPCS64: i32, i64 bzw. [2 x i64] aus dem ABI-Wert
            // zur echten nominalen Moo-Struktur laden. Die groessere
            // 12-Byte-Eingabeform wurde oben vor dem Laden gepolstert.
            let result_slot = alloc_builder.build_alloca(
                self.registers[0], "ffi.arm64.result",
            ).map_err(|e| e.to_string())?;
            builder.build_store(result_slot, ret).map_err(|e| e.to_string())?;
            let result = builder.build_load(self.llvm_struct, result_slot,
                "ffi.arm64.struct").map_err(|e| e.to_string())?;
            result.as_instruction_value().ok_or("C-ABI: ARM64-Resultat-Load fehlt")?
                .set_alignment(1).map_err(|e| e.to_string())?;
            return Ok(result);
        }
        for (index, register) in self.registers.iter().enumerate() {
            let part = if self.registers.len() == 1 { ret } else {
                builder.build_extract_value(ret.into_struct_value(), index as u32,
                    "ffi.struct.result.part").map_err(|e| e.to_string())?
            };
            if part.get_type() != *register {
                return Err("C-ABI: Ergebnisregister hat den falschen Typ".into());
            }
            let address = if index == 0 { dest } else {
                unsafe { builder.build_gep(context.i8_type(), dest,
                    &[context.i64_type().const_int((index * 8) as u64, false)],
                    "ffi.struct.ret.offset") }.map_err(|e| e.to_string())?
            };
            let store = builder.build_store(address, part).map_err(|e| e.to_string())?;
            store.set_alignment(1).map_err(|e| e.to_string())?;
        }
        let result = builder.build_load(self.llvm_struct, dest, "ffi.struct.value")
            .map_err(|e| e.to_string())?;
        result.as_instruction_value().ok_or("C-ABI: Struktur-Load fehlt")?
            .set_alignment(1).map_err(|e| e.to_string())?;
        Ok(result)
    }
}

/// Modul-lokale Bindungen, deren LLVM-Deklaration und ABI-Signatur zusammenbleiben.
#[derive(Default)]
pub(crate) struct ExternRegistry<'ctx> {
    bindings: std::collections::HashMap<String, ExternBinding<'ctx>>,
    struct_bindings: std::collections::HashMap<String, StructRoundtripBinding<'ctx>>,
}

impl<'ctx> ExternRegistry<'ctx> {
    /// Fuegt einen eng klassifizierten C-Roundtrip als eigene Alternative zu
    /// den schon etablierten skalaren Bindungen im selben Register ein.
    pub(crate) fn declare_struct_roundtrip(
        &mut self,
        context: &'ctx inkwell::context::Context,
        module: &inkwell::module::Module<'ctx>,
        name: &str,
        inputs: &[TypeRef],
        result: &TypeRef,
        layouts: &crate::native_struct::NativeStructRegistry<'ctx>,
    ) -> Result<(), String> {
        let triple = module.get_triple();
        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?;
        let sysv = triple == "x86_64-unknown-none";
        let win64 = matches!(triple, "x86_64-pc-windows-gnu" | "x86_64-pc-windows-msvc");
        let aarch64 = triple == "aarch64-unknown-none";
        if !sysv && !win64 && !aarch64 {
            return Err(format!(
                "C-ABI: Strukturwert-Aufruf nur fuer x86_64-SysV/Win64 und aarch64-bare freigegeben, nicht '{triple}'"
            ));
        }
        let TypeKind::Name(result_name) = &result.kind else {
            return Err("C-ABI: Roundtrip erwartet einen nativen Struktur-Rueckgabetyp".into());
        };
        let mut param_kinds = Vec::new();
        let mut scalar_count = 0usize;
        let mut struct_count = 0usize;
        for input in inputs {
            match &input.kind {
                TypeKind::Name(name) if name == result_name => {
                    struct_count += 1;
                    param_kinds.push(StructAbiParam::Struct);
                }
                TypeKind::Name(name) if name == "u64" && struct_count == 0 => {
                    scalar_count += 1;
                    param_kinds.push(StructAbiParam::U64);
                }
                _ => return Err(
                    "C-ABI: nur u64-Praefixe und gleichartige Strukturargumente freigegeben".into()
                ),
            }
        }
        if !(1..=2).contains(&struct_count) || scalar_count > 6 {
            return Err("C-ABI: 1..2 Strukturargumente und bis zu 6 u64-Praefixe freigegeben".into());
        }
        let layout = layouts.layout(result_name)
            .ok_or_else(|| format!("C-ABI: unbekannte Struktur '{result_name}'"))?;
        let count = layout.fields.len();
        // Keine Typnamens-Heuristik: genaues Layout inkl. Padding pruefen.
        let scalar = |field: usize, name: &str| layout.fields.get(field)
            .is_some_and(|(_, ty)| matches!(&ty.kind,
                TypeKind::Name(actual) if actual == name));
        let words = ((1..=4).contains(&count) || count == 6)
            && layout.size == count as u64 * 4 && layout.alignment == 4
            && layout.fields.iter().all(|(_, ty)| matches!(&ty.kind,
                TypeKind::Name(name) if name == "u32"));
        let mixed = count == 2 && scalar(0, "f64") && scalar(1, "u32")
            && layout.size == 16 && layout.alignment == 8
            && layout.offsets == [0, 8];
        let float4 = count == 4 && layout.fields.iter().all(|(_, ty)|
                matches!(&ty.kind, TypeKind::Name(name) if name == "f32"))
            && layout.size == 16 && layout.alignment == 4
            && layout.offsets == [0, 4, 8, 12];
        let packed = count == 2 && scalar(0, "u8") && scalar(1, "u32")
            && layout.size == 5 && layout.alignment == 1
            && layout.offsets == [0, 1];
        if !(words || mixed || float4 || packed) {
            return Err(format!(
                "C-ABI: Struktur '{result_name}' ist noch nicht klassifiziert (u32-Worte, f64+u32, f32x4 oder packed u8+u32)"
            ));
        }
        if self.bindings.contains_key(name) || self.struct_bindings.contains_key(name)
            || module.get_function(name).is_some()
        {
            return Err(format!("extern C: Symbolkollision bei '{name}'"));
        }
        use inkwell::types::{BasicMetadataTypeEnum, BasicType, BasicTypeEnum};
        // AArch64: die ersten C-Referenzformen. 20B/HFA/Packed und
        // Registerdruck bekommen eigene nachgelagerte AAPCS64-Gates.
        let arm_kind = if aarch64 {
            if scalar_count != 0 || struct_count != 1 {
                return Err("C-ABI: AArch64 erlaubt vorerst genau einen Strukturparameter ohne u64-Praefix".into());
            }
            if words {
                Some(match count {
                    1 => AArch64AggregateAbi::U32,
                    2 => AArch64AggregateAbi::U64,
                    3 | 4 => AArch64AggregateAbi::Pair64,
                    6 => AArch64AggregateAbi::Indirect,
                    _ => unreachable!("u32-Wortanzahl zuvor geprueft"),
                })
            } else if mixed {
                Some(AArch64AggregateAbi::Pair64)
            } else {
                return Err("C-ABI: AArch64 Struct-by-value noch nicht klassifiziert (nur 1/2/3/4/6 u32 oder f64+u32)".into());
            }
        } else {
            None
        };
        let registers: Vec<BasicTypeEnum<'ctx>> = if let Some(kind) = arm_kind {
            match kind {
                AArch64AggregateAbi::U32 | AArch64AggregateAbi::U64 =>
                    vec![context.i64_type().into()],
                AArch64AggregateAbi::Pair64 =>
                    vec![context.i64_type().array_type(2).into()],
                AArch64AggregateAbi::Indirect => vec![],
            }
        } else if words {
            match count {
                1 => vec![context.i32_type().into()],
                2 => vec![context.i64_type().into()],
                3 if sysv => vec![context.i64_type().into(), context.i32_type().into()],
                4 if sysv => vec![context.i64_type().into(), context.i64_type().into()],
                3 | 4 | 6 => vec![], // Win64: sret + pointer; SysV 24B: byval
                _ => unreachable!("gepruefte Feldanzahl"),
            }
        } else if sysv && mixed {
            vec![context.f64_type().into(), context.i32_type().into()]
        } else if sysv && float4 {
            let xmm_pair = context.f32_type().vec_type(2);
            vec![xmm_pair.into(), xmm_pair.into()]
        } else {
            // Win64 mixed/float4/packed sowie SysV packed ueber Speicher.
            vec![]
        };
        // Das Argument- und Rueckgabe-ABI sind unabhaengig:
        // z.B. {i64,i32}-Resultat im Register, aber ein Struct12 nach
        // 5 u64-Praefixen als byval-Argument im Speicher.
        let indirect_args = if aarch64 {
            vec![arm_kind == Some(AArch64AggregateAbi::Indirect)]
        } else {
            classify_struct_argument_storage(&param_kinds, &registers, sysv)
        };
        let memory_byval = sysv;
        let sret_alignment = u64::from(layout.alignment);
        let pointer = context.ptr_type(inkwell::AddressSpace::default());
        let mut params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
        let mut byval_indices = Vec::new();
        if registers.is_empty() { params.push(pointer.into()); }
        for (index, kind) in param_kinds.iter().enumerate() {
            match kind {
                StructAbiParam::U64 => params.push(context.i64_type().into()),
                StructAbiParam::Struct if indirect_args[index] => {
                    if memory_byval { byval_indices.push(params.len() as u32); }
                    params.push(pointer.into());
                }
                StructAbiParam::Struct => {
                    for register in &registers {
                        params.push((*register).into());
                    }
                }
            }
        }
        let function_type = match arm_kind {
            Some(AArch64AggregateAbi::U32) =>
                context.i32_type().fn_type(&params, false),
            Some(AArch64AggregateAbi::U64) =>
                context.i64_type().fn_type(&params, false),
            Some(AArch64AggregateAbi::Pair64) =>
                context.i64_type().array_type(2).fn_type(&params, false),
            Some(AArch64AggregateAbi::Indirect) =>
                context.void_type().fn_type(&params, false),
            None if registers.is_empty() =>
                context.void_type().fn_type(&params, false),
            None if registers.len() == 1 =>
                registers[0].fn_type(&params, false),
            None => context.struct_type(&registers, false).fn_type(&params, false),
        };
        let function = module.add_function(name, function_type, None);
        if registers.is_empty() {
            use inkwell::attributes::{Attribute, AttributeLoc};
            let sret = Attribute::get_named_enum_kind_id("sret");
            let align = Attribute::get_named_enum_kind_id("align");
            function.add_attribute(AttributeLoc::Param(0),
                context.create_type_attribute(sret, layout.llvm_type.into()));
            function.add_attribute(AttributeLoc::Param(0),
                context.create_enum_attribute(align, sret_alignment));
        }
        if !byval_indices.is_empty() {
            use inkwell::attributes::{Attribute, AttributeLoc};
            let byval = Attribute::get_named_enum_kind_id("byval");
            let align = Attribute::get_named_enum_kind_id("align");
            for index in byval_indices {
                function.add_attribute(AttributeLoc::Param(index),
                    context.create_type_attribute(byval, layout.llvm_type.into()));
                function.add_attribute(AttributeLoc::Param(index),
                    context.create_enum_attribute(align, 8));
            }
        }
        self.struct_bindings.insert(name.to_owned(), StructRoundtripBinding {
            function, struct_name: result_name.clone(), param_kinds,
            llvm_struct: layout.llvm_type, registers, indirect_args,
            aarch64: arm_kind, memory_byval, sret_alignment,
        });
        Ok(())
    }

    pub(crate) fn get_struct(&self, name: &str) -> Option<&StructRoundtripBinding<'ctx>> {
        self.struct_bindings.get(name)
    }

    pub(crate) fn declare(
        &mut self,
        context: &'ctx inkwell::context::Context,
        module: &inkwell::module::Module<'ctx>,
        name: &str,
        signature: &FfiSignature,
    ) -> Result<(), String> {
        if self.bindings.contains_key(name) {
            return Err(format!("extern C: doppelte Deklaration '{name}'"));
        }
        let binding = signature.declare(context, module, name)?;
        self.bindings.insert(name.to_owned(), binding);
        Ok(())
    }

    pub(crate) fn get(&self, name: &str) -> Option<&ExternBinding<'ctx>> {
        self.bindings.get(name)
    }
}

impl<'ctx> ExternBinding<'ctx> {
    /// Quelltypen fuer einen zukuenftigen semantisch geprueften Pointer-Call.
    /// Quellorte sind keine Typidentitaet; die Gleichheit muss separat aufgeloest werden.
    pub(crate) fn source_types(&self) -> Option<(&[TypeRef], &TypeRef)> {
        self.signature.source_result.as_ref().map(|result| (self.signature.source_params.as_slice(), result))
    }

    /// Semantische Identitaeten ohne Quellorte; sie begruenden allein weder
    /// Pointer-Lebensdauer noch Ownership oder eine Call-Freigabe.
    pub(crate) fn semantic_types(&self) -> (&[crate::native_type_id::NativeTypeId], &crate::native_type_id::NativeTypeId) {
        (&self.signature.semantic_params, &self.signature.semantic_result)
    }

    /// P7-T5: Deklaration fuer Systemkoerper (Aufrufstelle uebernimmt deren CC).
    pub(crate) fn function(&self) -> inkwell::values::FunctionValue<'ctx> {
        self.function
    }

    /// Liefert nur Typen, fuer die der skalare Call-Emitter freigegeben ist.
    pub(crate) fn scalar_parameters(&self) -> Result<Vec<crate::codegen_native::NType>, String> {
        self.signature.params.iter().map(|ty| ty.native_scalar()
            .ok_or_else(|| "extern-C-Pointer-Aufrufe sind noch nicht unterstützt".to_string()))
            .collect()
    }

    pub(crate) fn scalar_result(&self) -> Result<Option<crate::codegen_native::NType>, String> {
        self.signature.result.map(|ty| ty.native_scalar()
            .ok_or_else(|| "extern-C-Pointer-Rueckgaben sind noch nicht unterstützt".to_string()))
            .transpose()
    }

    /// P2-T66: Extension-Attribute (signext/zeroext) je Parameter und fuer die
    /// Rueckgabe, zielabhaengig identisch zu Deklaration und skalarem Call
    /// (FfiType::extension). Der Systemkoerper setzt sie an seiner Aufrufstelle,
    /// damit es keine zweite ABI-Regel gibt.
    pub(crate) fn extension_attributes(&self) -> (Vec<Option<&'static str>>, Option<&'static str>) {
        let target = self.signature.target.as_str();
        (
            self.signature.params.iter().map(|ty| ty.extension(target)).collect(),
            self.signature.result.and_then(|ty| ty.extension(target)),
        )
    }

    /// Vorbereiteter skalarer C-Call. Der Aufrufer muss Operanden bereits
    /// links-nach-rechts ausgewertet und den Fehler-/Cleanup-Pfad eingerichtet
    /// haben. Pointer und Callbacks bleiben bis zum eigenen Vertrag gesperrt.
    pub(crate) fn emit_scalar_call(
        &self,
        context: &'ctx inkwell::context::Context,
        builder: &inkwell::builder::Builder<'ctx>,
        module: &inkwell::module::Module<'ctx>,
        triple: &str,
        args: &[crate::codegen_native::NValue<'ctx>],
        unsafe_context: bool,
    ) -> Result<Option<crate::codegen_native::NValue<'ctx>>, String> {
        use inkwell::attributes::{Attribute, AttributeLoc};
        use inkwell::types::{BasicMetadataTypeEnum, BasicType};
        use inkwell::values::{BasicMetadataValueEnum, BasicValue, ValueKind};

        let signature = &self.signature;
        let function = self.function;
        if !unsafe_context {
            return Err("extern-C-Aufruf erfordert unsicher".to_string());
        }
        if triple != signature.target {
            return Err(format!("extern C: Ziel-ABI '{triple}' stimmt nicht mit der Deklaration ueberein"));
        }
        if matches!(triple, "x86_64-unknown-none" | "aarch64-unknown-none") {
            return Err("extern-C-Aufrufe sind im Bare-Profil noch nicht freigegeben".to_string());
        }
        let block = builder.get_insert_block().ok_or("extern-C-Aufruf: kein aktiver Block")?;
        let caller = block.get_parent().ok_or("extern-C-Aufruf: Block ohne Funktion")?;
        if block.get_terminator().is_some() {
            return Err("extern-C-Aufruf: Block ist bereits beendet".to_string());
        }
        let caller_name = caller.get_name().to_str().map_err(|e| format!("Ungueltiger Funktionsname: {e}"))?;
        let callee_name = function.get_name().to_str().map_err(|e| format!("Ungueltiger extern-C-Name: {e}"))?;
        if module.get_function(caller_name) != Some(caller) || module.get_function(callee_name) != Some(function) {
            return Err("extern-C-Aufruf: Caller und Callee gehoeren nicht zum Modul".to_string());
        }
        if args.len() != signature.params.len() {
            return Err("extern-C-Aufruf: falsche Argumentanzahl".to_string());
        }
        for (arg, ty) in args.iter().zip(&signature.params) {
            let expected = ty.native_scalar().ok_or("extern-C-Pointer-Aufrufe sind noch nicht unterstützt")?;
            if arg.ty != expected || arg.value.get_type() != ty.llvm_type(context) {
                return Err("extern-C-Aufruf: Argumenttyp stimmt nicht".to_string());
            }
        }
        let result_type = signature.result.map(|ty| ty.native_scalar()
            .ok_or("extern-C-Pointer-Rueckgaben sind noch nicht unterstützt")).transpose()?;
        let param_types: Vec<BasicMetadataTypeEnum<'ctx>> =
            signature.params.iter().map(|ty| ty.llvm_type(context).into()).collect();
        let expected_type = match signature.result {
            Some(ty) => ty.llvm_type(context).fn_type(&param_types, false),
            None => context.void_type().fn_type(&param_types, false),
        };
        if function.get_type() != expected_type || function.get_call_conventions() != signature.convention {
            return Err("extern-C-Aufruf: LLVM-Signatur oder Calling Convention stimmt nicht".to_string());
        }

        let call_args: Vec<BasicMetadataValueEnum<'ctx>> =
            args.iter().map(|arg| arg.value.into()).collect();
        let call = builder.build_call(function, &call_args, if signature.result.is_some() { "extern_c" } else { "" })
            .map_err(|e| format!("extern-C-Aufruf: {e}"))?;
        if signature.convention != CC_C {
            call.set_call_convention(signature.convention);
        }
        let apply = |location, ty: FfiType| {
            if let Some(name) = ty.extension(&signature.target) {
                let kind = Attribute::get_named_enum_kind_id(name);
                call.add_attribute(location, context.create_enum_attribute(kind, 0));
            }
        };
        for (index, ty) in signature.params.iter().enumerate() {
            apply(AttributeLoc::Param(index as u32), *ty);
        }
        if let Some(ty) = signature.result { apply(AttributeLoc::Return, ty); }

        match result_type {
            None => Ok(None),
            Some(ty) => match call.try_as_basic_value() {
                ValueKind::Basic(value) => crate::codegen_native::NValue::checked(context, ty, value).map(Some),
                _ => Err("extern-C-Aufruf liefert keinen Wert".to_string()),
            },
        }
    }
}

impl FfiSignature {
    /// Dies validiert nur die Typmenge; Calling Convention, signext/zeroext,
    /// Symbolkollisionen, Ownership und Call-Emission sind gesonderte Pflichten.
    pub(crate) fn validate(
        triple: &str,
        params: &[TypeRef],
        result: &TypeRef,
        variadic: bool,
    ) -> Result<Self, String> {
        if !matches!(triple, "x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"
            | "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu"
            | "aarch64-unknown-linux-gnu" | "x86_64-unknown-uefi" | "x86_64-unknown-none"
            | "aarch64-unknown-none") {
            return Err(format!("extern C: Ziel-ABI '{triple}' ist noch nicht freigegeben"));
        }
        if variadic {
            return Err("extern C: variadische Signaturen sind nicht freigegeben".to_string());
        }
        let source_params = params.to_vec();
        let source_result = result.clone();
        let params = params.iter().map(value_type).collect::<Result<Vec<_>, _>>()?;
        let result = match &result.kind {
            TypeKind::Name(name) if name == "void" => None,
            _ => Some(value_type(result)?),
        };
        // Erst nach der FFI-Typmengenpruefung aufloesen: der Resolver darf
        // keine zusaetzlichen Typen oder Pointer-Calls freischalten.
        let nominal = crate::native_type_id::NominalTypes::new();
        let semantic_params = source_params.iter().map(|ty| crate::native_type_id::resolve_type(ty, &nominal))
            .collect::<Result<Vec<_>, _>>()?;
        let semantic_result = crate::native_type_id::resolve_type(&source_result, &nominal)?;
        Ok(Self { params, result, target: triple.to_string(), source_params, source_result: Some(source_result), semantic_params, semantic_result, convention: CC_C, semantic_convention: None })
    }

    /// P7-T4 (7A): aufgeloeste Konvention aus `@aufrufkonvention` (0 = Zieldefault).
    pub(crate) fn with_convention(mut self, convention: u32) -> Self {
        self.convention = convention;
        self
    }
}


/// Ein LLVM-Wert mit erhaltener Moo-Typidentität. Opake LLVM-Pointer allein
/// unterscheiden weder Zieltyp noch Vorzeichen oder Funktionssignatur.
pub(crate) struct TypedFfiValue<'ctx> {
    pub(crate) value: inkwell::values::BasicValueEnum<'ctx>,
    pub(crate) identity: crate::native_type_id::NativeTypeId,
}

impl FfiSignature {
    /// Niedrige Absenkung für einen synchronen indirekten Fremdaufruf.
    /// Der Aufrufer muss Provenienz, Lebensdauer und Gültigkeit des Code- und
    /// der Datenzeiger im unsicheren Kontext gewährleisten. Kein Pin/Retain.
    /// Erst die Anbindung an Moo und deren Ende-zu-Ende-Gates geben das frei.
    pub(crate) fn emit_indirect_call<'ctx>(
        &self,
        context: &'ctx inkwell::context::Context,
        builder: &inkwell::builder::Builder<'ctx>,
        module: &inkwell::module::Module<'ctx>,
        callee: &TypedFfiValue<'ctx>,
        args: &[TypedFfiValue<'ctx>],
        unsafe_context: bool,
    ) -> Result<Option<TypedFfiValue<'ctx>>, String> {
        use crate::native_type_id::NativeTypeId;
        use inkwell::attributes::{Attribute, AttributeLoc};
        use inkwell::types::{BasicMetadataTypeEnum, BasicType};
        use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, ValueKind};
        use inkwell::AddressSpace;

        if !unsafe_context {
            return Err("Indirekter extern-C-Aufruf erfordert unsicher".into());
        }
        let triple = module.get_triple();
        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?;
        if triple != self.target {
            return Err("Indirekter extern-C-Aufruf: Ziel-ABI stimmt nicht".into());
        }
        let block = builder.get_insert_block().ok_or("Indirekter Aufruf ohne aktiven Block")?;
        let caller = block.get_parent().ok_or("Indirekter Aufruf ohne Funktion")?;
        if block.get_terminator().is_some() {
            return Err("Indirekter Aufruf in beendetem Block".into());
        }
        let caller_name = caller.get_name().to_str().map_err(|e| e.to_string())?;
        if module.get_function(caller_name) != Some(caller) {
            return Err("Indirekter Aufruf: Funktion gehört nicht zum Modul".into());
        }
        let expected_callee = NativeTypeId::Pointer {
            address_space: 0,
            pointee: Box::new(NativeTypeId::Function {
                params: self.semantic_params.clone(),
                result: Box::new(self.semantic_result.clone()),
                variadic: false,
                convention: self.semantic_convention.clone(),
            }),
        };
        let target = match callee.value {
            BasicValueEnum::PointerValue(p)
                if callee.identity == expected_callee
                    && p.get_type() == context.ptr_type(AddressSpace::default()) => p,
            _ => return Err("Indirekter Aufruf: Funktionszeigertyp stimmt nicht".into()),
        };
        if args.len() != self.params.len() {
            return Err("Indirekter Aufruf: falsche Argumentzahl".into());
        }
        // Erste Freigabe beschränkt sich auf Ganzzahlen, Pointer und void.
        if self.params.iter().chain(self.result.iter())
            .any(|ty| matches!(ty, FfiType::F32 | FfiType::F64)) {
            return Err("Indirekter Aufruf: Float-Signatur noch nicht freigegeben".into());
        }
        for ((arg, ty), identity) in args.iter().zip(&self.params).zip(&self.semantic_params) {
            if &arg.identity != identity || arg.value.get_type() != ty.llvm_type(context) {
                return Err("Indirekter Aufruf: Argumenttyp stimmt nicht".into());
            }
        }
        let param_types: Vec<BasicMetadataTypeEnum<'ctx>> =
            self.params.iter().map(|ty| ty.llvm_type(context).into()).collect();
        let function_type = match self.result {
            Some(ty) => ty.llvm_type(context).fn_type(&param_types, false),
            None => context.void_type().fn_type(&param_types, false),
        };
        let call_args: Vec<BasicMetadataValueEnum<'ctx>> =
            args.iter().map(|arg| arg.value.into()).collect();
        let call = builder.build_indirect_call(
            function_type, target, &call_args,
            if self.result.is_some() { "extern_indirect" } else { "" },
        ).map_err(|e| format!("Indirekter extern-C-Aufruf: {e}"))?;
        if self.convention != CC_C {
            call.set_call_convention(self.convention);
        }
        for (index, ty) in self.params.iter().enumerate() {
            if let Some(name) = ty.extension(&self.target) {
                let kind = Attribute::get_named_enum_kind_id(name);
                call.add_attribute(AttributeLoc::Param(index as u32), context.create_enum_attribute(kind, 0));
            }
        }
        if let Some(ty) = self.result {
            if let Some(name) = ty.extension(&self.target) {
                let kind = Attribute::get_named_enum_kind_id(name);
                call.add_attribute(AttributeLoc::Return, context.create_enum_attribute(kind, 0));
            }
        }
        match self.result {
            None => Ok(None),
            Some(_) => match call.try_as_basic_value() {
                ValueKind::Basic(value) => Ok(Some(TypedFfiValue {
                    value,
                    identity: self.semantic_result.clone(),
                })),
                _ => Err("Indirekter Aufruf liefert keinen Wert".into()),
            },
        }
    }
}


fn indirect_value_type(
    ty: &crate::native_type_id::NativeTypeId,
    nominal: &crate::native_type_id::NominalTypes,
) -> Result<FfiType, String> {
    use crate::native_type_id::{NativeTypeId, ScalarType};
    let scalar = match ty {
        NativeTypeId::Scalar(ScalarType::I8) => FfiType::I8,
        NativeTypeId::Scalar(ScalarType::I16) => FfiType::I16,
        NativeTypeId::Scalar(ScalarType::I32) => FfiType::I32,
        NativeTypeId::Scalar(ScalarType::I64) => FfiType::I64,
        NativeTypeId::Scalar(ScalarType::U8) => FfiType::U8,
        NativeTypeId::Scalar(ScalarType::U16) => FfiType::U16,
        NativeTypeId::Scalar(ScalarType::U32) => FfiType::U32,
        NativeTypeId::Scalar(ScalarType::U64) => FfiType::U64,
        NativeTypeId::Pointer { address_space: 0, pointee } => {
            // Nominale Datenpointee nur aus der expliziten kanonischen Registry.
            // Das verleiht weder Speicher noch Layout oder Lebensdauern Gültigkeit.
            match pointee.as_ref() {
                NativeTypeId::Void => {},
                NativeTypeId::Nominal { module, name }
                    if !module.is_empty() && !name.is_empty()
                        && nominal.values().any(|(m, n)| m == module && n == name) => {},
                _ => { indirect_value_type(pointee, nominal)?; },
            }
            FfiType::Pointer
        }
        _ => return Err("Indirekter Aufruf: Typ noch nicht freigegeben".into()),
    };
    Ok(scalar)
}

impl FfiSignature {
    /// Aus einer aufgelösten Funktionszeigeridentität ohne erfundene Quellorte.
    /// Diese Signatur ist nur für den indirekten Aufruf, nicht für declare().
    pub(crate) fn from_indirect_identity(
        triple: &str,
        callee: &crate::native_type_id::NativeTypeId,
        nominal: &crate::native_type_id::NominalTypes,
    ) -> Result<Self, String> {
        use crate::native_type_id::NativeTypeId;
        if !matches!(triple, "x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"
            | "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu"
            | "aarch64-unknown-linux-gnu" | "x86_64-unknown-uefi"
            | "x86_64-unknown-none" | "aarch64-unknown-none") {
            return Err(format!("Indirekter Aufruf: Ziel-ABI '{triple}' nicht freigegeben"));
        }
        let (params, result, semantic_convention) = match callee {
            NativeTypeId::Pointer { address_space: 0, pointee } => match pointee.as_ref() {
                NativeTypeId::Function { params, result, variadic: false, convention } =>
                    (params, result.as_ref(), convention),
                _ => return Err("Indirekter Aufruf: ptr[fn(...)] erwartet".into()),
            },
            _ => return Err("Indirekter Aufruf: ptr[fn(...)] erwartet".into()),
        };
        let lowered = params.iter().map(|ty| indirect_value_type(ty, nominal)).collect::<Result<Vec<_>, _>>()?;
        let lowered_result = if *result == NativeTypeId::Void {
            None
        } else {
            Some(indirect_value_type(result, nominal)?)
        };
        Ok(Self {
            params: lowered,
            result: lowered_result,
            target: triple.to_string(),
            source_params: Vec::new(),
            source_result: None,
            semantic_params: params.clone(),
            semantic_result: result.clone(),
            convention: match semantic_convention {
                Some(name) => llvm_call_convention(name, triple)?,
                None => CC_C,
            },
            semantic_convention: semantic_convention.clone(),
        })
    }
}
