//! Zielgebundene Validierung und LLVM-Absenkung der ersten extern-C-Typmenge.
//! Der angebundene skalare Call-Pfad ist eng begrenzt; Pointer, Callbacks und
//! weitere ABI-Varianten bleiben bis zum jeweiligen Vertrag ausgeschlossen.

use crate::ast::{TypeKind, TypeRef};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum FfiType {
    I8, I16, I32, I64,
    U8, U16, U32, U64,
    F32, F64,
    /// C `_Bool` (P2-T19/T56 Batch C): i1; zeroext auf x86_64 (SysV und MS),
    /// ohne Attribut auf AAPCS64 (Clang-belegt).
    Bool,
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
        "bool" => FfiType::Bool,
        _ => return None,
    })
}

fn pointee_supported(ty: &TypeRef) -> bool {
    match &ty.kind {
        // bool ist als C-Wert erlaubt, als Zeigerziel nicht: Speicherbild von
        // _Bool (nur 0/1 gueltig) ist kein freigegebener Moo-Vertrag.
        TypeKind::Name(name) => name == "void" || (name != "bool" && primitive(name).is_some()),
        TypeKind::Generic { name, args }
            if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
                pointee_supported(&args[0]),
        // P2-T21/T57 A (Plan f4655d20 A1/A2): C-Funktionszeiger ptr[fn(...) -> R].
        TypeKind::Function { params, return_type, .. } =>
            params.iter().all(rueckruf_wert_zulaessig)
                && (matches!(&return_type.kind, TypeKind::Name(n) if n == "void")
                    || rueckruf_wert_zulaessig(return_type)),
        _ => false,
    }
}

/// P2-T21/T57 A1: Parameter-/Ergebnistyp einer C-Rueckruf-Signatur: Skalar
/// (i8..u64, bool, f32, f64), Zeiger (auch ptr[Struktur], ptr[fn]) oder
/// Strukturwert. Namen werden spaeter ueber die nominale Registry aufgeloest
/// (unbekannte Namen scheitern dort); kein Puffer, kein Array, kein fn by value.
pub(crate) fn rueckruf_wert_zulaessig(ty: &TypeRef) -> bool {
    match &ty.kind {
        TypeKind::Name(name) => name != "void",
        // P2-T97 (34119): ptr[bool] auch in Rueckruf-Signaturen nicht.
        TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
            matches!(&args[0].kind, TypeKind::Name(n) if n != "bool") || pointee_supported(&args[0]),
        _ => false,
    }
}

fn value_type(ty: &TypeRef) -> Result<FfiType, String> {
    value_type_in(ty, &|_| false)
}

/// Wie value_type; zusaetzlich ptr[Struktur] (opaker C-Zeiger) fuer Namen, die
/// `ist_struktur` als bekannte native Struktur bestaetigt (P2-T56).
fn value_type_in(ty: &TypeRef, ist_struktur: &dyn Fn(&str) -> bool) -> Result<FfiType, String> {
    match &ty.kind {
        TypeKind::Name(name) => primitive(name).ok_or_else(||
            error(ty, "unbekannter oder nicht unterstuetzter C-Werttyp")),
        TypeKind::Generic { name, args } if name == "ptr" || name == "zeiger" => {
            let struktur_ziel = args.len() == 1
                && matches!(&args[0].kind, TypeKind::Name(n) if ist_struktur(n));
            if crate::native_type_id::hat_bool_zeigerziel(ty) {
                return Err(error(ty, &format!("Pointer braucht genau einen unterstuetzten Zieltyp: {}",
                    crate::native_type_id::BOOL_ZEIGERZIEL)));
            }
            if args.len() != 1 || !(pointee_supported(&args[0]) || struktur_ziel) {
                return Err(error(ty, "Pointer braucht genau einen unterstuetzten Zieltyp"));
            }
            Ok(FfiType::Pointer)
        }
        _ => Err(error(ty, "Aggregate und Funktionstypen by value sind nicht freigegeben")),
    }
}


pub(crate) fn darwin_aarch64(target: &str) -> bool {
    target.starts_with("arm64-apple-darwin") || target.starts_with("aarch64-apple-darwin")
}

/// Attributklasse fuer skalare C-Werte; klassifiziert keine Strukturwerte.
fn skalar_abi(target: &str, convention: u32) -> crate::ffi_abi::Abi {
    use crate::ffi_abi::Abi;
    // Nur die skalaren Extension-Attribute: Darwin verlangt hier wie SysV
    // signext/zeroext (Clang-Orakel ffi_klein_roundtrip.c), nicht die Struktur-ABI.
    if darwin_aarch64(target) { return Abi::SysV; }
    if target.starts_with("aarch64-") { return Abi::Aapcs64; }
    match convention {
        CC_X86_64_SYSV => Abi::SysV,
        CC_WIN64 => Abi::Ms,
        _ if target.contains("-windows-") || target.contains("-uefi") => Abi::Ms,
        _ => Abi::SysV,
    }
}

/// P2-T19 Batch E: C-Exporte (@einstieg, umbenannte Funktion mit 1:1-Parametern)
/// tragen dieselben Extension-Attribute wie eine Clang-Definition (Befund
/// abi-matrix: 'zeroext i1 f(i16 zeroext)'), sonst lesen C-Aufrufer nicht
/// erweiterte bool/i8/i16-Rueckgaben. Nur FFI-Skalartypen; Rest unveraendert.
pub(crate) fn apply_export_extensions<'ctx>(
    context: &'ctx inkwell::context::Context,
    function: inkwell::values::FunctionValue<'ctx>,
    triple: &str,
    convention: u32,
    inputs: &[Option<TypeRef>],
    result: Option<&TypeRef>,
) {
    use inkwell::attributes::{Attribute, AttributeLoc};
    let abi = skalar_abi(triple, convention);
    let ext = |ty: &TypeRef| match &ty.kind {
        TypeKind::Name(name) => primitive(name).and_then(|skalar| skalar.extension_abi(abi)),
        _ => None,
    };
    for (index, ty) in inputs.iter().enumerate() {
        if let Some(name) = ty.as_ref().and_then(ext) {
            function.add_attribute(AttributeLoc::Param(index as u32),
                context.create_enum_attribute(Attribute::get_named_enum_kind_id(name), 0));
        }
    }
    if let Some(name) = result.and_then(ext) {
        function.add_attribute(AttributeLoc::Return,
            context.create_enum_attribute(Attribute::get_named_enum_kind_id(name), 0));
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
            Self::Bool => context.bool_type().into(),
            Self::Pointer => context.ptr_type(AddressSpace::default()).into(),
        }
    }

    /// P2-T19/T56 (Review db841fb9, Batch C): Extension-Attribute nach der
    /// effektiv gewaehlten ABI (Zieltriple + @aufrufkonvention), Clang-belegt:
    /// C-bool zeroext auf SysV und MS x64; schmale Ganzzahlen signext/zeroext
    /// nur auf SysV; AAPCS64 ohne Attribute.
    fn extension_abi(self, abi: crate::ffi_abi::Abi) -> Option<&'static str> {
        use crate::ffi_abi::Abi;
        match (self, abi) {
            (Self::Bool, Abi::SysV | Abi::Ms) => Some("zeroext"),
            (Self::I8 | Self::I16, Abi::SysV) => Some("signext"),
            (Self::U8 | Self::U16, Abi::SysV) => Some("zeroext"),
            _ => None,
        }
    }

    /// Skalare Pfade: dieselbe Regel, ABI aus Zieltriple und LLVM-Konvention.
    fn extension(self, target: &str, convention: u32) -> Option<&'static str> {
        self.extension_abi(skalar_abi(target, convention))
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
            if let Some(name) = ty.extension(&self.target, self.convention) {
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
            Self::Bool => NType::Bool,
            Self::Pointer => return None,
        })
    }
}

/// Modul-lokale Bindungen, deren LLVM-Deklaration und ABI-Signatur zusammenbleiben.
#[derive(Default)]
pub(crate) struct ExternRegistry<'ctx> {
    bindings: std::collections::HashMap<String, ExternBinding<'ctx>>,
    /// P2-T19/T56 B: Signaturen nach dem allgemeinen Klassifizierer (ffi_abi).
    abi_bindings: std::collections::HashMap<String, AbiBinding<'ctx>>,
    /// P2-T98 S4/S5: Besitzvertraege (@verbraucht/@behaelt) je extern C.
    vertraege: std::collections::HashMap<String, ExternVertrag>,
}

/// P2-T98 S4/S5 (Entscheid #38292): Besitzvertrag eines extern C, 0-basiert.
/// `frei`: @verbraucht(n)/@consumes(n) wie an @nativ (P2-T23/T53), der Aufruf
/// gibt das Objekt hinter Parameter n frei; danach gehoert es nicht mehr dem
/// Aufrufer (weitere Nutzung M-M003). `behaelt`: @behaelt(n)/@behält/@retains,
/// C speichert den Wert aus Parameter n ueber den Aufruf hinaus (ein geliehener
/// Zeiger dort ist M-M004). Der Parametertyp ist frei (Zeiger oder Handle-Zahl).
#[derive(Clone, Debug, Default)]
pub(crate) struct ExternVertrag {
    pub(crate) frei: Vec<usize>,
    pub(crate) behaelt: Vec<usize>,
}

/// Liest den Besitzvertrag aus den Decorators eines extern C (n ab 1).
pub(crate) fn extern_vertrag(name: &str, parameter: usize, decorators: &[crate::ast::Decorator]) -> Result<ExternVertrag, String> {
    let mut vertrag = ExternVertrag::default();
    for d in decorators {
        let ziel = match d.name.as_str() {
            "verbraucht" | "consumes" => &mut vertrag.frei,
            "behaelt" | "behält" | "retains" => &mut vertrag.behaelt,
            _ => continue,
        };
        let n = match d.args.as_slice() {
            [crate::ast::DecoratorArg::Zahl(n)] if n.fract() == 0.0 && *n >= 1.0 && *n <= parameter as f64 => *n as usize - 1,
            _ => return Err(format!(
                "extern C '{name}': @{} erwartet genau eine Parameternummer 1..{parameter}, z.B. @{}(1)", d.name, d.name)),
        };
        if ziel.contains(&n) {
            return Err(format!("extern C '{name}': @{}({}) ist doppelt angegeben", d.name, n + 1));
        }
        ziel.push(n);
    }
    Ok(vertrag)
}

/// P2-T19/T56 B (Plan 91033131): extern-C-Signatur mit Strukturwerten nach dem
/// allgemeinen Klassifizierer (crate::ffi_abi); einziger Weg fuer Strukturwerte
/// an C (die fruehere Roundtrip-Whitelist ist seit B4 entfernt).
pub(crate) struct AbiBinding<'ctx> {
    function: inkwell::values::FunctionValue<'ctx>,
    params: Vec<TypeRef>,
    result: TypeRef,
    plan: crate::ffi_abi::Signaturplan,
    /// Je Strukturparameter (Layouttyp, Ausrichtung, Zwischenspeicher-Bytes).
    param_structs: Vec<Option<(inkwell::types::StructType<'ctx>, u32, u32)>>,
    result_struct: Option<(inkwell::types::StructType<'ctx>, u32, u32)>,
    param_ext: Vec<Option<&'static str>>,
    result_ext: Option<&'static str>,
}

/// LLVM-Registerform eines Strukturteils (Argument bzw. Rueckgabe).
fn abi_teil_typ<'ctx>(
    context: &'ctx inkwell::context::Context, teil: crate::ffi_abi::Teil, rueckgabe: bool,
) -> Result<inkwell::types::BasicTypeEnum<'ctx>, String> {
    use crate::ffi_abi::Teil;
    use inkwell::types::BasicType;
    Ok(match teil {
        Teil::Ganz(bits) => {
            let bits = std::num::NonZeroU32::new(bits).ok_or("C-ABI: Ganzzahlteil ohne Breite")?;
            context.custom_width_int_type(bits)?.into()
        }
        Teil::Zeiger => context.ptr_type(inkwell::AddressSpace::default()).into(),
        Teil::F32 => context.f32_type().into(),
        Teil::F64 => context.f64_type().into(),
        Teil::F32x2 => context.f32_type().vec_type(2).into(),
        Teil::I64x2 => context.i64_type().array_type(2).into(),
        Teil::Hfa { doppelt, n } => {
            let element: inkwell::types::BasicTypeEnum<'ctx> =
                if doppelt { context.f64_type().into() } else { context.f32_type().into() };
            if rueckgabe {
                context.struct_type(&vec![element; n as usize], false).into()
            } else {
                element.array_type(n).into()
            }
        }
    })
}

/// Ausgerichteter Byte-Zwischenspeicher im Eintrittsblock der aktuellen Funktion
/// (wie Clangs Koerzierungs-Temporaries; SROA/O2 entfernt ihn).
fn abi_entry_puffer<'ctx>(
    context: &'ctx inkwell::context::Context, builder: &inkwell::builder::Builder<'ctx>, bytes: u32, align: u32,
) -> Result<inkwell::values::PointerValue<'ctx>, String> {
    let block = builder.get_insert_block().ok_or("C-ABI: kein aktiver Block")?;
    let function = block.get_parent().ok_or("C-ABI: Block ohne Funktion")?;
    let entry = function.get_first_basic_block().ok_or("C-ABI: Funktion ohne Eintrittsblock")?;
    let lokal = context.create_builder();
    match entry.get_first_instruction() {
        Some(first) => lokal.position_before(&first),
        None => lokal.position_at_end(entry),
    }
    let ptr = lokal.build_alloca(context.i8_type().array_type(bytes), "abi.puffer").map_err(|e| e.to_string())?;
    ptr.as_instruction().ok_or("C-ABI: Alloca fehlt")?.set_alignment(align).map_err(|e| e.to_string())?;
    Ok(ptr)
}

impl<'ctx> AbiBinding<'ctx> {
    pub(crate) fn param_types(&self) -> &[TypeRef] { &self.params }
    pub(crate) fn result_type(&self) -> &TypeRef { &self.result }

    /// Ruft die C-Funktion. Strukturargumente kommen als Aggregatwert im
    /// Layouttyp, eine Strukturrueckgabe wird als solcher geliefert; dazwischen
    /// wird ueber Entry-Zwischenspeicher umgeformt (keine LLVM-Aggregate an C).
    pub(crate) fn emit_call(
        &self,
        context: &'ctx inkwell::context::Context,
        builder: &inkwell::builder::Builder<'ctx>,
        args: &[inkwell::values::BasicValueEnum<'ctx>],
    ) -> Result<Option<inkwell::values::BasicValueEnum<'ctx>>, String> {
        use crate::ffi_abi::Uebergabe;
        use inkwell::attributes::{Attribute, AttributeLoc};
        use inkwell::values::{BasicMetadataValueEnum, BasicValue};
        let e = |err: inkwell::builder::BuilderError| err.to_string();
        let ea = |err: inkwell::values::InstructionValueError| err.to_string();
        if args.len() != self.params.len() {
            return Err("C-ABI: falsche Argumentzahl".into());
        }
        let i8t = context.i8_type();
        let i64t = context.i64_type();
        let mut call_args: Vec<BasicMetadataValueEnum<'ctx>> = Vec::new();
        let mut attrs: Vec<(u32, Attribute)> = Vec::new();
        let mut sret = None;
        if let (Some(Uebergabe::Sret), Some((typ, align, bytes))) = (&self.plan.rueckgabe, self.result_struct) {
            let slot = abi_entry_puffer(context, builder, bytes, align.max(8))?;
            attrs.push((0, context.create_type_attribute(Attribute::get_named_enum_kind_id("sret"), typ.into())));
            attrs.push((0, context.create_enum_attribute(Attribute::get_named_enum_kind_id("align"), u64::from(align))));
            call_args.push(slot.into());
            sret = Some(slot);
        }
        for (index, value) in args.iter().enumerate() {
            match (&self.plan.params[index], self.param_structs[index]) {
                (None, None) => {
                    if let Some(ext) = self.param_ext[index] {
                        attrs.push((call_args.len() as u32,
                            context.create_enum_attribute(Attribute::get_named_enum_kind_id(ext), 0)));
                    }
                    call_args.push((*value).into());
                }
                (Some(uebergabe), Some((typ, align, bytes))) => {
                    let puffer_align = align.max(8);
                    let slot = abi_entry_puffer(context, builder, bytes, puffer_align)?;
                    builder.build_memset(slot, puffer_align, i8t.const_zero(), i64t.const_int(u64::from(bytes), false)).map_err(e)?;
                    builder.build_store(slot, *value).map_err(e)?.set_alignment(puffer_align).map_err(ea)?;
                    match uebergabe {
                        Uebergabe::Register(teile) => {
                            for (teil_index, teil) in teile.iter().enumerate() {
                                let ptr = unsafe {
                                    builder.build_gep(i8t, slot, &[i64t.const_int(8 * teil_index as u64, false)], "abi.teil.ptr")
                                }.map_err(e)?;
                                let teil_typ = abi_teil_typ(context, *teil, false)?;
                                let geladen = builder.build_load(teil_typ, ptr, "abi.teil").map_err(e)?;
                                geladen.as_instruction_value().ok_or("C-ABI: Load fehlt")?.set_alignment(8).map_err(ea)?;
                                if matches!(teil, crate::ffi_abi::Teil::Hfa { .. }) {
                                    attrs.push((call_args.len() as u32,
                                        context.create_enum_attribute(Attribute::get_named_enum_kind_id("alignstack"), 8)));
                                }
                                call_args.push(geladen.into());
                            }
                        }
                        Uebergabe::Byval { ausrichtung } => {
                            let position = call_args.len() as u32;
                            attrs.push((position, context.create_type_attribute(Attribute::get_named_enum_kind_id("byval"), typ.into())));
                            attrs.push((position, context.create_enum_attribute(Attribute::get_named_enum_kind_id("align"), *ausrichtung)));
                            call_args.push(slot.into());
                        }
                        Uebergabe::Indirekt => call_args.push(slot.into()),
                        Uebergabe::Sret => return Err("C-ABI: sret als Argument".into()),
                    }
                }
                _ => return Err("C-ABI: Signaturplan und Parameter widersprechen sich".into()),
            }
        }
        let call = builder.build_call(self.function, &call_args, "abi.aufruf").map_err(e)?;
        call.set_call_convention(self.function.get_call_conventions());
        for (index, attr) in attrs {
            call.add_attribute(AttributeLoc::Param(index), attr);
        }
        if let Some(ext) = self.result_ext {
            call.add_attribute(AttributeLoc::Return, context.create_enum_attribute(Attribute::get_named_enum_kind_id(ext), 0));
        }
        let roh = match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(value) => Some(value),
            _ => None,
        };
        match (&self.plan.rueckgabe, self.result_struct) {
            (Some(Uebergabe::Sret), Some((typ, align, _))) => {
                let slot = sret.ok_or("C-ABI: sret-Puffer fehlt")?;
                let wert = builder.build_load(typ, slot, "abi.sret.wert").map_err(e)?;
                wert.as_instruction_value().ok_or("C-ABI: Load fehlt")?.set_alignment(align.max(8)).map_err(ea)?;
                Ok(Some(wert))
            }
            (Some(Uebergabe::Register(teile)), Some((typ, align, bytes))) => {
                let roh = roh.ok_or("C-ABI: Registerrueckgabe ohne Wert")?;
                let puffer_align = align.max(8);
                let slot = abi_entry_puffer(context, builder, bytes, puffer_align)?;
                builder.build_memset(slot, puffer_align, i8t.const_zero(), i64t.const_int(u64::from(bytes), false)).map_err(e)?;
                if teile.len() == 1 {
                    builder.build_store(slot, roh).map_err(e)?.set_alignment(puffer_align).map_err(ea)?;
                } else {
                    for teil_index in 0..teile.len() {
                        let teil = builder.build_extract_value(roh.into_struct_value(), teil_index as u32, "abi.rueck.teil").map_err(e)?;
                        let ptr = unsafe {
                            builder.build_gep(i8t, slot, &[i64t.const_int(8 * teil_index as u64, false)], "abi.rueck.ptr")
                        }.map_err(e)?;
                        builder.build_store(ptr, teil).map_err(e)?.set_alignment(8).map_err(ea)?;
                    }
                }
                let wert = builder.build_load(typ, slot, "abi.rueck.wert").map_err(e)?;
                wert.as_instruction_value().ok_or("C-ABI: Load fehlt")?.set_alignment(puffer_align).map_err(ea)?;
                Ok(Some(wert))
            }
            (None, None) => Ok(roh),
            _ => Err("C-ABI: Rueckgabeplan und Rueckgabetyp widersprechen sich".into()),
        }
    }
}

impl<'ctx> ExternRegistry<'ctx> {
    /// P2-T98 S4/S5: Besitzvertrag eines extern C ablegen bzw. abfragen.
    pub(crate) fn setze_vertrag(&mut self, name: &str, vertrag: ExternVertrag) {
        self.vertraege.insert(name.to_string(), vertrag);
    }

    pub(crate) fn gibt_frei(&self, name: &str, parameter: usize) -> bool {
        self.vertraege.get(name).is_some_and(|v| v.frei.contains(&parameter))
    }

    pub(crate) fn behaelt(&self, name: &str, parameter: usize) -> bool {
        self.vertraege.get(name).is_some_and(|v| v.behaelt.contains(&parameter))
    }

    pub(crate) fn get_abi(&self, name: &str) -> Option<&AbiBinding<'ctx>> {
        self.abi_bindings.get(name)
    }

    /// P2-T19/T56 B1 (Plan 91033131): Deklaration nach dem allgemeinen
    /// Klassifizierer: beliebige Mischung aus Skalaren, Zeigern und (auch
    /// verschiedenen) Strukturwerten; Rueckgabe void/Skalar/Struktur. Die ABI
    /// folgt aus Zieltriple bzw. @aufrufkonvention (convention = LLVM-CC).
    #[allow(clippy::too_many_arguments)]
    pub(crate) fn declare_abi(
        &mut self,
        context: &'ctx inkwell::context::Context,
        module: &inkwell::module::Module<'ctx>,
        target: &inkwell::targets::TargetData,
        name: &str,
        inputs: &[TypeRef],
        result: &TypeRef,
        layouts: &crate::native_struct::NativeStructRegistry<'ctx>,
        convention: u32,
    ) -> Result<(), String> {
        use inkwell::attributes::{Attribute, AttributeLoc};
        if self.bindings.contains_key(name) || self.abi_bindings.contains_key(name)
            || module.get_function(name).is_some()
        {
            return Err(format!("extern C: Symbolkollision bei '{name}'"));
        }
        let form = abi_form(context, module, target, name, inputs, result, layouts, convention)?;
        let function = module.add_function(name, form.function_type, None);
        if convention != CC_C {
            function.set_call_conventions(convention);
        }
        for (index, attr) in &form.attrs {
            function.add_attribute(AttributeLoc::Param(*index), *attr);
        }
        if let Some(ext) = form.result_ext {
            function.add_attribute(AttributeLoc::Return, context.create_enum_attribute(Attribute::get_named_enum_kind_id(ext), 0));
        }
        self.abi_bindings.insert(name.to_owned(), AbiBinding {
            function, params: inputs.to_vec(), result: result.clone(), plan: form.plan,
            param_structs: form.param_structs, result_struct: form.result_struct,
            param_ext: form.param_ext, result_ext: form.result_ext,
        });
        Ok(())
    }
}

/// P2-T19/T56 D: C-ABI-Form einer Signatur nach ffi_abi, gemeinsam fuer
/// extern-C-Deklarationen (declare_abi) und @einstieg-Export-Thunks.
struct AbiForm<'ctx> {
    function_type: inkwell::types::FunctionType<'ctx>,
    attrs: Vec<(u32, inkwell::attributes::Attribute)>,
    result_ext: Option<&'static str>,
    plan: crate::ffi_abi::Signaturplan,
    /// Je Strukturparameter (Layouttyp, Ausrichtung, Zwischenspeicher-Bytes).
    param_structs: Vec<Option<(inkwell::types::StructType<'ctx>, u32, u32)>>,
    result_struct: Option<(inkwell::types::StructType<'ctx>, u32, u32)>,
    param_ext: Vec<Option<&'static str>>,
}

#[allow(clippy::too_many_arguments)]
fn abi_form<'ctx>(
    context: &'ctx inkwell::context::Context,
    module: &inkwell::module::Module<'ctx>,
    target: &inkwell::targets::TargetData,
    name: &str,
    inputs: &[TypeRef],
    result: &TypeRef,
    layouts: &crate::native_struct::NativeStructRegistry<'ctx>,
    convention: u32,
) -> Result<AbiForm<'ctx>, String> {
        use crate::ffi_abi::{Abi, ParamForm, Skalar, StrukturForm, Uebergabe};
        use inkwell::attributes::Attribute;
        use inkwell::types::{BasicMetadataTypeEnum, BasicType};
        let triple = module.get_triple();
        let triple = triple.as_str().to_str().map_err(|e| e.to_string())?.to_string();
        let konvention = match convention {
            CC_C => None,
            CC_X86_64_SYSV => Some("sysv64"),
            CC_WIN64 => Some("win64"),
            other => return Err(format!("C-ABI: Aufrufkonvention {other} fuer Strukturwerte nicht unterstuetzt")),
        };
        let abi = Abi::aus_triple(&triple, konvention)?;
        let struktur = |ty: &TypeRef| match &ty.kind {
            TypeKind::Name(n) if layouts.layout(n).is_some() => Some(n.clone()),
            _ => None,
        };
        let mut formen: Vec<Option<StrukturForm>> = Vec::new();
        let mut skalare: Vec<Option<FfiType>> = Vec::new();
        let mut param_structs = Vec::new();
        for input in inputs {
            if let Some(n) = struktur(input) {
                let layout = layouts.layout(&n).ok_or("C-ABI: Layout fehlt")?;
                formen.push(Some(layouts.flach(context, target, &n)?));
                skalare.push(None);
                param_structs.push(Some((layout.llvm_type, layout.alignment, layout.size.max(16) as u32)));
            } else {
                formen.push(None);
                skalare.push(Some(value_type_in(input, &|n| layouts.layout(n).is_some())?));
                param_structs.push(None);
            }
        }
        let param_formen: Vec<ParamForm<'_>> = formen.iter().zip(&skalare).map(|(form, skalar)| match (form, skalar) {
            (Some(form), _) => ParamForm::Struktur(form),
            (None, Some(FfiType::F32 | FfiType::F64)) => ParamForm::Skalar(Skalar::Sse),
            _ => ParamForm::Skalar(Skalar::Gp),
        }).collect();
        let void = matches!(&result.kind, TypeKind::Name(n) if n == "void");
        let (result_form, result_struct, result_skalar) = match struktur(result) {
            Some(n) => {
                let layout = layouts.layout(&n).ok_or("C-ABI: Layout fehlt")?;
                (Some(layouts.flach(context, target, &n)?), Some((layout.llvm_type, layout.alignment, layout.size.max(16) as u32)), None)
            }
            None if void => (None, None, None),
            None => (None, None, Some(value_type_in(result, &|n| layouts.layout(n).is_some())?)),
        };
        let plan = crate::ffi_abi::plane(abi, &param_formen, result_form.as_ref())?;
        let ptr = context.ptr_type(inkwell::AddressSpace::default());
        let mut llvm_params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::new();
        let mut attrs: Vec<(u32, Attribute)> = Vec::new();
        let mut param_ext = Vec::new();
        if let (Some(Uebergabe::Sret), Some((typ, align, _))) = (&plan.rueckgabe, result_struct) {
            attrs.push((0, context.create_type_attribute(Attribute::get_named_enum_kind_id("sret"), typ.into())));
            attrs.push((0, context.create_enum_attribute(Attribute::get_named_enum_kind_id("align"), u64::from(align))));
            attrs.push((0, context.create_enum_attribute(Attribute::get_named_enum_kind_id("noalias"), 0)));
            llvm_params.push(ptr.into());
        }
        for index in 0..inputs.len() {
            match (&plan.params[index], skalare[index], param_structs[index]) {
                (None, Some(skalar), None) => {
                    let ext = skalar.extension_abi(abi);
                    if let Some(ext) = ext {
                        attrs.push((llvm_params.len() as u32, context.create_enum_attribute(Attribute::get_named_enum_kind_id(ext), 0)));
                    }
                    param_ext.push(ext);
                    llvm_params.push(skalar.llvm_type(context).into());
                }
                (Some(uebergabe), None, Some((typ, _, _))) => {
                    param_ext.push(None);
                    match uebergabe {
                        Uebergabe::Register(teile) => for teil in teile {
                            if matches!(teil, crate::ffi_abi::Teil::Hfa { .. }) {
                                attrs.push((llvm_params.len() as u32, context.create_enum_attribute(Attribute::get_named_enum_kind_id("alignstack"), 8)));
                            }
                            llvm_params.push(abi_teil_typ(context, *teil, false)?.into());
                        },
                        Uebergabe::Byval { ausrichtung } => {
                            let position = llvm_params.len() as u32;
                            attrs.push((position, context.create_type_attribute(Attribute::get_named_enum_kind_id("byval"), typ.into())));
                            attrs.push((position, context.create_enum_attribute(Attribute::get_named_enum_kind_id("align"), *ausrichtung)));
                            llvm_params.push(ptr.into());
                        }
                        Uebergabe::Indirekt => llvm_params.push(ptr.into()),
                        Uebergabe::Sret => return Err(format!("C-ABI '{name}': sret als Argument")),
                    }
                }
                _ => return Err(format!("C-ABI '{name}': Parameter {} nicht klassifizierbar", index + 1)),
            }
        }
        let result_ext = result_skalar.and_then(|skalar| skalar.extension_abi(abi));
        let function_type = match (&plan.rueckgabe, result_skalar) {
            (Some(Uebergabe::Sret), _) => context.void_type().fn_type(&llvm_params, false),
            (Some(Uebergabe::Register(teile)), _) if teile.len() == 1 =>
                abi_teil_typ(context, teile[0], true)?.fn_type(&llvm_params, false),
            (Some(Uebergabe::Register(teile)), _) => {
                let teil_typen = teile.iter().map(|teil| abi_teil_typ(context, *teil, false)).collect::<Result<Vec<_>, String>>()?;
                context.struct_type(&teil_typen, false).fn_type(&llvm_params, false)
            }
            (None, Some(skalar)) => skalar.llvm_type(context).fn_type(&llvm_params, false),
            (None, None) => context.void_type().fn_type(&llvm_params, false),
            _ => return Err(format!("C-ABI '{name}': Rueckgabe nicht klassifizierbar")),
        };
        Ok(AbiForm { function_type, attrs, result_ext, plan, param_structs, result_struct, param_ext })
}

/// P2-T19/T56 D1 (Plan 91033131): externer C-ABI-Thunk `name` vor der internen
/// Moo-Funktion `internal` (Strukturparameter als Zeiger auf eine exklusive
/// Kopie, Strukturrueckgabe ueber sret, native_system::SystemSignature).
/// Umkehrung von AbiBinding::emit_call: Registerteile werden in einen
/// ausgerichteten Entry-Puffer zusammengesetzt; byval-/indirekte Zeiger zeigen
/// bereits auf eine dem Aufgerufenen gehoerende Kopie des Aufrufers.
#[allow(clippy::too_many_arguments)]
pub(crate) fn emit_abi_export<'ctx>(
    context: &'ctx inkwell::context::Context,
    module: &inkwell::module::Module<'ctx>,
    target: &inkwell::targets::TargetData,
    name: &str,
    internal: inkwell::values::FunctionValue<'ctx>,
    inputs: &[TypeRef],
    result: &TypeRef,
    layouts: &crate::native_struct::NativeStructRegistry<'ctx>,
    convention: u32,
    linkage: inkwell::module::Linkage,
    rueckruf: Option<RueckrufVertrag<'ctx>>,
) -> Result<inkwell::values::FunctionValue<'ctx>, String> {
    use crate::ffi_abi::Uebergabe;
    use inkwell::attributes::{Attribute, AttributeLoc};
    use inkwell::values::{BasicMetadataValueEnum, BasicValue};
    let e = |err: inkwell::builder::BuilderError| err.to_string();
    let ea = |err: inkwell::values::InstructionValueError| err.to_string();
    if module.get_function(name).is_some() || module.get_global(name).is_some() {
        return Err(format!("Native Funktion '{name}': @einstieg-Symbolkollision"));
    }
    let form = abi_form(context, module, target, name, inputs, result, layouts, convention)?;
    let thunk = module.add_function(name, form.function_type, Some(linkage));
    if convention != CC_C {
        thunk.set_call_conventions(convention);
    }
    for (index, attr) in &form.attrs {
        thunk.add_attribute(AttributeLoc::Param(*index), *attr);
    }
    if let Some(ext) = form.result_ext {
        thunk.add_attribute(AttributeLoc::Return, context.create_enum_attribute(Attribute::get_named_enum_kind_id(ext), 0));
    }
    let builder = context.create_builder();
    builder.position_at_end(context.append_basic_block(thunk, "entry"));
    // P2-T21/T57 B1 (Plan f4655d20 E4d/E5): fehlbarer hosted Rueckruf -
    // Thread-Pruefung und kein Koerperlauf bei bereits anstehendem Moo-Fehler.
    let rueckruf_fehler = match &rueckruf {
        Some(vertrag) => {
            let thread = crate::codegen_native::native_rueckruf_thread_hook(context, module)?;
            let text = builder.build_global_string_ptr(&vertrag.name, "rueckruf.name").map_err(e)?;
            builder.build_call(thread, &[text.as_pointer_value().into()], "").map_err(e)?;
            let fehler = context.append_basic_block(thunk, "rueckruf.fehler");
            rueckruf_flag_verzweigen(context, &builder, module, thunk, fehler, "rueckruf.start")?;
            Some(fehler)
        }
        None => None,
    };
    let i8t = context.i8_type();
    let i64t = context.i64_type();
    let param = |index: u32| thunk.get_nth_param(index)
        .ok_or_else(|| format!("C-ABI-Export '{name}': Parameter {index} fehlt"));
    let mut next = 0u32;
    let mut args: Vec<BasicMetadataValueEnum<'ctx>> = Vec::new();
    let mut ergebnis = None;
    match (&form.plan.rueckgabe, form.result_struct) {
        (Some(Uebergabe::Sret), Some(_)) => {
            args.push(param(0)?.into());
            next = 1;
        }
        (Some(Uebergabe::Register(_)), Some((_, align, bytes))) => {
            let slot = abi_entry_puffer(context, &builder, bytes, align.max(8))?;
            args.push(slot.into());
            ergebnis = Some(slot);
        }
        (None, None) => {}
        _ => return Err(format!("C-ABI-Export '{name}': Rueckgabeplan widerspricht dem Rueckgabetyp")),
    }
    for index in 0..inputs.len() {
        match (&form.plan.params[index], form.param_structs[index]) {
            (None, None) => {
                args.push(param(next)?.into());
                next += 1;
            }
            (Some(Uebergabe::Register(teile)), Some((_, align, bytes))) => {
                let slot = abi_entry_puffer(context, &builder, bytes, align.max(8))?;
                for teil_index in 0..teile.len() {
                    let ptr = unsafe {
                        builder.build_gep(i8t, slot, &[i64t.const_int(8 * teil_index as u64, false)], "export.teil.ptr")
                    }.map_err(e)?;
                    builder.build_store(ptr, param(next)?).map_err(e)?.set_alignment(8).map_err(ea)?;
                    next += 1;
                }
                args.push(slot.into());
            }
            (Some(Uebergabe::Byval { .. } | Uebergabe::Indirekt), Some(_)) => {
                args.push(param(next)?.into());
                next += 1;
            }
            _ => return Err(format!("C-ABI-Export '{name}': Parameter {} nicht klassifizierbar", index + 1)),
        }
    }
    if next != thunk.count_params() {
        return Err(format!("C-ABI-Export '{name}': Parameterzahl widerspricht dem Signaturplan"));
    }
    let call = builder.build_call(internal, &args, "export.innen").map_err(e)?;
    call.set_call_convention(internal.get_call_conventions());
    if let Some(fehler) = rueckruf_fehler {
        rueckruf_flag_verzweigen(context, &builder, module, thunk, fehler, "rueckruf.ok")?;
    }
    match (&form.plan.rueckgabe, ergebnis) {
        (Some(Uebergabe::Register(teile)), Some(slot)) => {
            let wert = if teile.len() == 1 {
                let wert = builder.build_load(abi_teil_typ(context, teile[0], true)?, slot, "export.rueck").map_err(e)?;
                wert.as_instruction_value().ok_or("C-ABI: Load fehlt")?.set_alignment(8).map_err(ea)?;
                wert
            } else {
                let typen = teile.iter().map(|teil| abi_teil_typ(context, *teil, false)).collect::<Result<Vec<_>, String>>()?;
                let mut aggregat = context.struct_type(&typen, false).get_undef();
                for (teil_index, typ) in typen.iter().enumerate() {
                    let ptr = unsafe {
                        builder.build_gep(i8t, slot, &[i64t.const_int(8 * teil_index as u64, false)], "export.rueck.ptr")
                    }.map_err(e)?;
                    let teil = builder.build_load(*typ, ptr, "export.rueck.teil").map_err(e)?;
                    teil.as_instruction_value().ok_or("C-ABI: Load fehlt")?.set_alignment(8).map_err(ea)?;
                    aggregat = builder.build_insert_value(aggregat, teil, teil_index as u32, "export.rueck.agg")
                        .map_err(e)?.into_struct_value();
                }
                aggregat.as_basic_value_enum()
            };
            builder.build_return(Some(&wert)).map_err(e)?;
        }
        (Some(Uebergabe::Sret), _) => {
            builder.build_return(None).map_err(e)?;
        }
        _ => match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(wert) => { builder.build_return(Some(&wert)).map_err(e)?; }
            _ => { builder.build_return(None).map_err(e)?; }
        },
    }
    if let (Some(fehler), Some(vertrag)) = (rueckruf_fehler, &rueckruf) {
        builder.position_at_end(fehler);
        match vertrag.fehlerwert {
            Some(wert) => { builder.build_return(Some(&wert)).map_err(e)?; }
            None => { builder.build_return(None).map_err(e)?; }
        }
    }
    Ok(thunk)
}

/// P2-T21/T57 E4 (Plan f4655d20): Fehlervertrag eines fehlbaren hosted
/// Rueckrufs. fehlerwert = vereinbarter Rueckgabewert an C (None bei void);
/// der Moo-Fehler bleibt als Flag anstehen (Fehlerkanal zum Moo-Aufrufer).
pub(crate) struct RueckrufVertrag<'ctx> {
    pub(crate) name: String,
    pub(crate) fehlerwert: Option<inkwell::values::BasicValueEnum<'ctx>>,
}

/// Steht ein Moo-Fehler an (moo_native_fehler_offen != 0) -> `fehler`, sonst
/// weiter in einem neuen Block `weiter`.
fn rueckruf_flag_verzweigen<'ctx>(
    context: &'ctx inkwell::context::Context,
    builder: &inkwell::builder::Builder<'ctx>,
    module: &inkwell::module::Module<'ctx>,
    thunk: inkwell::values::FunctionValue<'ctx>,
    fehler: inkwell::basic_block::BasicBlock<'ctx>,
    weiter: &str,
) -> Result<(), String> {
    let hook = crate::codegen_native::native_fehler_offen_hook(context, module)?;
    let flag = match builder.build_call(hook, &[], "rueckruf.fehler.offen").map_err(|e| e.to_string())?.try_as_basic_value() {
        inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
        _ => return Err("moo_native_fehler_offen liefert keinen Wert".into()),
    };
    let offen = builder.build_int_compare(
        inkwell::IntPredicate::NE, flag, context.i32_type().const_zero(), "rueckruf.fehler.gesetzt",
    ).map_err(|e| e.to_string())?;
    let naechster = context.append_basic_block(thunk, weiter);
    builder.build_conditional_branch(offen, fehler, naechster).map_err(|e| e.to_string())?;
    builder.position_at_end(naechster);
    Ok(())
}

/// P2-T21/T57 E4b: Fehlerwert aus `@rückruf(n)` fuer den Ergebnistyp - Zahlliteral
/// streng im Zielbereich (kein Wrap), bool nur 0/1, Zeiger nur 0 (= nichts).
pub(crate) fn rueckruf_fehlerwert<'ctx>(
    context: &'ctx inkwell::context::Context,
    ty: &TypeRef,
    arg: Option<&crate::ast::DecoratorArg>,
    name: &str,
) -> Result<inkwell::values::BasicValueEnum<'ctx>, String> {
    use inkwell::values::BasicValue;
    let zahl = match arg {
        Some(crate::ast::DecoratorArg::Zahl(zahl)) => *zahl,
        Some(_) => return Err(format!("@rückruf an '{name}': der Fehlerwert muss ein Zahlliteral sein")),
        None => return Err(format!(
            "Rückruf '{name}' kann fehlschlagen; @rückruf(fehlerwert) angeben oder Fehler durch Vorprüfung (teilbar, verschiebbar, als-Prüfung) ausschließen"
        )),
    };
    let skalar = value_type(ty)?;
    let ausserhalb = || format!("@rückruf an '{name}': Fehlerwert {zahl} liegt ausserhalb von {skalar:?}");
    Ok(match skalar {
        FfiType::F32 => context.f32_type().const_float(zahl).as_basic_value_enum(),
        FfiType::F64 => context.f64_type().const_float(zahl).as_basic_value_enum(),
        FfiType::Bool if zahl == 0.0 || zahl == 1.0 =>
            context.bool_type().const_int(zahl as u64, false).as_basic_value_enum(),
        FfiType::Pointer if zahl == 0.0 =>
            context.ptr_type(inkwell::AddressSpace::default()).const_null().as_basic_value_enum(),
        FfiType::Bool | FfiType::Pointer => return Err(ausserhalb()),
        ganz => {
            let (min, max): (i128, i128) = match ganz {
                FfiType::I8 => (i8::MIN.into(), i8::MAX.into()),
                FfiType::I16 => (i16::MIN.into(), i16::MAX.into()),
                FfiType::I32 => (i32::MIN.into(), i32::MAX.into()),
                FfiType::I64 => (i64::MIN.into(), i64::MAX.into()),
                FfiType::U8 => (0, u8::MAX.into()),
                FfiType::U16 => (0, u16::MAX.into()),
                FfiType::U32 => (0, u32::MAX.into()),
                _ => (0, u64::MAX.into()),
            };
            if !zahl.is_finite() || zahl.fract() != 0.0 { return Err(ausserhalb()); }
            let wert = zahl as i128;
            if wert < min || wert > max { return Err(ausserhalb()); }
            ganz.llvm_type(context).into_int_type().const_int(wert as i64 as u64, false).as_basic_value_enum()
        }
    })
}

impl<'ctx> ExternRegistry<'ctx> {
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
            self.signature.params.iter().map(|ty| ty.extension(target, self.signature.convention)).collect(),
            self.signature.result.and_then(|ty| ty.extension(target, self.signature.convention)),
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
            if let Some(name) = ty.extension(&signature.target, signature.convention) {
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
        nominal: &crate::native_type_id::NominalTypes,
    ) -> Result<Self, String> {
        if !matches!(triple, "x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu"
            | "x86_64-pc-windows-msvc" | "x86_64-pc-windows-gnu"
            | "aarch64-unknown-linux-gnu" | "x86_64-unknown-uefi" | "x86_64-unknown-none"
            | "aarch64-unknown-none") && !darwin_aarch64(triple) {
            return Err(format!("extern C: Ziel-ABI '{triple}' ist noch nicht freigegeben"));
        }
        if variadic {
            return Err("extern C: variadische Signaturen sind nicht freigegeben".to_string());
        }
        let source_params = params.to_vec();
        let source_result = result.clone();
        let ist_struktur = |n: &str| nominal.contains_key(n);
        let params = params.iter().map(|ty| value_type_in(ty, &ist_struktur)).collect::<Result<Vec<_>, _>>()?;
        let result = match &result.kind {
            TypeKind::Name(name) if name == "void" => None,
            _ => Some(value_type_in(result, &ist_struktur)?),
        };
        // Erst nach der FFI-Typmengenpruefung aufloesen: der Resolver darf
        // keine zusaetzlichen Typen oder Pointer-Calls freischalten.
        // P2-T21/T57 A2: nominale Registry des Moduls (ptr[Struktur] in
        // Rueckruf-Signaturen); Zusatztypen schaltet sie nicht frei.
        let semantic_params = source_params.iter().map(|ty| crate::native_type_id::resolve_type(ty, nominal))
            .collect::<Result<Vec<_>, _>>()?;
        let semantic_result = crate::native_type_id::resolve_type(&source_result, nominal)?;
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
        // P2-T21/T57 A3: Ganzzahlen, bool, f32/f64, Pointer und void – dieselbe
        // Signaturmenge wie fuer Rueckrufe in Gegenrichtung.
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
            if let Some(name) = ty.extension(&self.target, self.convention) {
                let kind = Attribute::get_named_enum_kind_id(name);
                call.add_attribute(AttributeLoc::Param(index as u32), context.create_enum_attribute(kind, 0));
            }
        }
        if let Some(ty) = self.result {
            if let Some(name) = ty.extension(&self.target, self.convention) {
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
        NativeTypeId::Scalar(ScalarType::Bool) => FfiType::Bool,
        NativeTypeId::Scalar(ScalarType::F32) => FfiType::F32,
        NativeTypeId::Scalar(ScalarType::F64) => FfiType::F64,
        NativeTypeId::Pointer { address_space: 0, pointee } => {
            // Nominale Datenpointee nur aus der expliziten kanonischen Registry.
            // Das verleiht weder Speicher noch Layout oder Lebensdauern Gültigkeit.
            match pointee.as_ref() {
                // P2-T97 (34119): auch bei indirekten Aufrufen kein Bool-Zeigerziel.
                NativeTypeId::Scalar(ScalarType::Bool) =>
                    return Err(format!("Indirekter Aufruf: {}", crate::native_type_id::BOOL_ZEIGERZIEL)),
                NativeTypeId::Void => {},
                // P2-T21/T57 A3: Funktionszeiger als Parameter (z. B. qsort-Vergleicher).
                NativeTypeId::Function { variadic: false, .. } => {},
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
