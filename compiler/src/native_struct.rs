//! Ziel-ABI-Layout für native Strukturen. Kein MooValue- oder Legacy-Pfad.
use inkwell::context::Context;
use inkwell::AddressSpace;
use inkwell::targets::TargetData;
use inkwell::types::{BasicType, BasicTypeEnum, StructType};
use std::collections::{HashMap, HashSet};
use crate::ast::{Decorator, DecoratorArg, Expr, TypeKind, TypeRef};
use crate::codegen_native::NType;


#[derive(Clone)]
pub(crate) struct NativeStructLayout<'ctx> {
    pub llvm_type: StructType<'ctx>,
    pub size: u64,
    pub alignment: u32,
    pub offsets: Vec<u64>,
    /// Original fields are retained; canonical identities are resolved later.
    pub fields: Vec<(String, TypeRef)>,
}

/// Layout wird ausschließlich aus den Daten der Ziel-TargetMachine ermittelt.
/// Höheres explizites Alignment ist vorerst nicht darstellbar: ein bloßes
/// Alloca-Alignment würde verschachtelte Strukturen und Arraystride verfälschen.
pub(crate) fn native_struct_layout<'ctx>(
    context: &'ctx Context,
    target: &TargetData,
    fields: &[BasicTypeEnum<'ctx>],
    packed: bool,
    explicit_alignment: Option<u32>,
) -> Result<NativeStructLayout<'ctx>, String> {
    if let Some(a) = explicit_alignment {
        if !a.is_power_of_two() {
            return Err("Struktur-Alignment muss eine positive Zweierpotenz sein".into());
        }
        if packed && a != 1 {
            return Err("gepackte Struktur mit höherem Alignment ist nicht erlaubt".into());
        }
    }
    let count = u32::try_from(fields.len())
        .map_err(|_| "zu viele Strukturfelder".to_string())?;
    let llvm_type = context.struct_type(fields, packed);
    if !llvm_type.is_sized() {
        return Err("Struktur enthält einen Typ ohne feste Größe".into());
    }
    let size = target.get_abi_size(&llvm_type);
    let alignment = target.get_abi_alignment(&llvm_type);
    if let Some(a) = explicit_alignment {
        if a != alignment {
            return Err("explizites Struktur-Alignment ist noch nicht unterstützt".into());
        }
    }
    let mut offsets = Vec::with_capacity(fields.len());
    for i in 0..count {
        offsets.push(target.offset_of_element(&llvm_type, i)
            .ok_or_else(|| format!("Offset für Strukturfeld {i} nicht bestimmbar"))?);
    }
    Ok(NativeStructLayout { llvm_type, size, alignment, offsets, fields: Vec::new() })
}

/// Registry owned by one compiler/module. A name becomes usable only after its
/// full definition has passed validation; forward/recursive by-value fields
/// are deliberately rejected in the first slice.
#[derive(Clone)]
pub(crate) struct NativeStructRegistry<'ctx> {
    layouts: HashMap<String, NativeStructLayout<'ctx>>,
    visible_names: HashSet<String>,
}

/// Pointer fields are opaque in LLVM; validate their *source* shape separately
/// and retain it for later canonical type resolution before any use/call.
fn validate_pointer_target(ty: &TypeRef, names: &HashSet<String>) -> Result<(), String> {
    match &ty.kind {
        TypeKind::Name(name) if name == "void" || NType::from_name(name).is_some() || names.contains(name) => Ok(()),
        TypeKind::Name(name) => Err(format!("Unbekannter Pointer-Zieltyp '{name}'")),
        TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
            validate_pointer_target(&args[0], names),
        TypeKind::Function { params, return_type, .. } => {
            for param in params {
                validate_function_value(param, names, false)?;
            }
            validate_function_value(return_type, names, true)
        }
        _ => Err("Nicht unterstützter Pointer-Zieltyp".into()),
    }
}

fn validate_function_value(ty: &TypeRef, names: &HashSet<String>, allow_void: bool) -> Result<(), String> {
    match &ty.kind {
        TypeKind::Name(name) if name == "void" => {
            if allow_void { Ok(()) } else { Err("void als Funktionsparameter ist nicht erlaubt".into()) }
        }
        TypeKind::Name(name) if NType::from_name(name).is_some() || names.contains(name) => Ok(()),
        TypeKind::Name(name) => Err(format!("Unbekannter Funktionstyp '{name}'")),
        TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
            validate_pointer_target(&args[0], names),
        _ => Err("Nicht unterstützter Funktionswerttyp".into()),
    }
}

/// Only by-value fields require an already materialized layout; pointer edges
/// are checked for names/types, but do not participate in layout cycles.
fn collect_value_dependencies(
    ty: &TypeRef, names: &HashSet<String>, dependencies: &mut HashSet<String>,
) -> Result<(), String> {
    match &ty.kind {
        TypeKind::Name(name) if NType::from_name(name).is_some() => Ok(()),
        TypeKind::Name(name) if names.contains(name) => { dependencies.insert(name.clone()); Ok(()) },
        TypeKind::Name(name) => Err(format!("Unbekannter Strukturfeldtyp '{name}'")),
        TypeKind::Array { element, .. } => collect_value_dependencies(element, names, dependencies),
        TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
            validate_pointer_target(&args[0], names),
        _ => Err("Strukturfeldtyp wird noch nicht unterstützt".into()),
    }
}

pub(crate) type LayoutSnapshot = Vec<(String, u64, u32, Vec<(String, u64)>)>;

impl<'ctx> NativeStructRegistry<'ctx> {
    pub(crate) fn new() -> Self { Self { layouts: HashMap::new(), visible_names: HashSet::new() } }

    /// P7-T4 (1A): fertiges Layout einer registrierten Struktur.
    pub(crate) fn layout(&self, name: &str) -> Option<&NativeStructLayout<'ctx>> {
        self.layouts.get(name)
    }

    pub(crate) fn snapshot(&self) -> Result<LayoutSnapshot, String> {
        let mut result = Vec::with_capacity(self.layouts.len());
        for (name, layout) in &self.layouts {
            if layout.fields.len() != layout.offsets.len() {
                return Err(format!("Struktur '{name}': Feld-/Offsetanzahl widerspricht sich"));
            }
            let fields = layout.fields.iter().zip(&layout.offsets)
                .map(|((field_name, _), offset)| (field_name.clone(), *offset))
                .collect();
            result.push((name.clone(), layout.size, layout.alignment, fields));
        }
        result.sort_by(|a, b| a.0.cmp(&b.0));
        Ok(result)
    }

    pub(crate) fn field_type(
        &self, context: &'ctx Context, target: &TargetData, ty: &TypeRef,
    ) -> Result<BasicTypeEnum<'ctx>, String> {
        match &ty.kind {
            TypeKind::Name(name) => {
                if let Some(scalar) = NType::from_name(name) {
                    return Ok(scalar.llvm_type(context));
                }
                self.layouts.get(name).map(|layout| layout.llvm_type.into())
                    .ok_or_else(|| format!("Unbekannter Strukturfeldtyp '{name}'"))
            }
            TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 => {
                validate_pointer_target(&args[0], &self.visible_names)?;
                Ok(context.ptr_type(AddressSpace::default()).into())
            }
            TypeKind::Array { element, length } => {
                let n = match &**length {
                    Expr::NativeNumber { text, .. } => {
                        let (digits, radix) = if let Some(x) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
                            (x, 16)
                        } else if let Some(x) = text.strip_prefix("0b").or_else(|| text.strip_prefix("0B")) {
                            (x, 2)
                        } else if let Some(x) = text.strip_prefix("0o").or_else(|| text.strip_prefix("0O")) {
                            (x, 8)
                        } else { (text.as_str(), 10) };
                        u32::from_str_radix(digits, radix)
                            .map_err(|_| "Arraylänge muss eine darstellbare konstante Ganzzahl sein".to_string())?
                    }
                    _ => return Err("Arraylänge muss ein konstantes Ganzzahlliteral sein".into()),
                };
                if n == 0 {
                    return Err("C-ABI-Strukturarrays erfordern eine positive Länge".into());
                }
                let item = self.field_type(context, target, element)?;
                target.get_abi_size(&item).checked_mul(u64::from(n))
                    .ok_or_else(|| "Arraygröße läuft über".to_string())?;
                Ok(item.array_type(n).into())
            }
            _ => Err("Strukturfeldtyp wird noch nicht unterstützt".into()),
        }
    }


    /// Collect names first, reject duplicate/unknown types and by-value cycles,
    /// then publish all computed layouts atomically. Pointer edges need no
    /// pointee layout; they retain their TypeRef for later semantic resolution.
    pub(crate) fn register_all(
        &mut self,
        context: &'ctx Context,
        target: &TargetData,
        definitions: &[(&str, &[(String, TypeRef)], &[Decorator])],
    ) -> Result<(), String> {
        let mut names: HashSet<String> = self.layouts.keys().cloned().collect();
        for (name, _, _) in definitions {
            // Preserve existing primitive-first type lookup for legacy names.
            if !names.insert((*name).to_string()) {
                return Err(format!("Struktur '{name}' ist bereits definiert"));
            }
        }
        let mut staged = self.clone();
        staged.visible_names = names.clone();
        let mut pending = Vec::with_capacity(definitions.len());
        for (index, (_, fields, _)) in definitions.iter().enumerate() {
            let mut dependencies = HashSet::new();
            for (_, ty) in *fields {
                collect_value_dependencies(ty, &names, &mut dependencies)?;
            }
            pending.push((index, dependencies));
        }
        while !pending.is_empty() {
            let before = pending.len();
            let mut remaining = Vec::new();
            for (index, dependencies) in pending {
                if dependencies.iter().all(|name| staged.layouts.contains_key(name)) {
                    let (name, fields, decorators) = definitions[index];
                    staged.register(context, target, name, fields, decorators)?;
                } else {
                    remaining.push((index, dependencies));
                }
            }
            if remaining.len() == before {
                return Err("Rekursiver Strukturwert-Zyklus ist nicht erlaubt".into());
            }
            pending = remaining;
        }
        *self = staged;
        Ok(())
    }

    pub(crate) fn register(
        &mut self,
        context: &'ctx Context,
        target: &TargetData,
        name: &str,
        fields: &[(String, TypeRef)],
        decorators: &[Decorator],
    ) -> Result<&NativeStructLayout<'ctx>, String> {
        if self.layouts.contains_key(name) {
            return Err(format!("Struktur '{name}' ist bereits definiert"));
        }
        if fields.is_empty() {
            return Err("Leere C-ABI-Strukturen werden nicht unterstützt".into());
        }
        let mut packed = false;
        let mut alignment = None;
        for decorator in decorators {
            match (decorator.name.as_str(), decorator.args.as_slice()) {
                ("gepackt" | "packed", []) if !packed => packed = true,
                ("ausrichten" | "align", [DecoratorArg::Zahl(n)]) if alignment.is_none()
                    && n.is_finite() && *n > 0.0 && *n <= u32::MAX as f64
                    && n.fract() == 0.0 => alignment = Some(*n as u32),
                _ => return Err(format!("Ungültiger oder wiederholter Struktur-Decorator @{}", decorator.name)),
            }
        }
        let mut seen = HashSet::new();
        let mut llvm_fields = Vec::with_capacity(fields.len());
        let mut checked_size = 0u64;
        let mut checked_align = 1u64;
        for (field_name, ty) in fields {
            if !seen.insert(field_name.as_str()) {
                return Err(format!("Struktur '{name}': Feld '{field_name}' ist doppelt"));
            }
            let field = self.field_type(context, target, ty)?;
            let align = if packed { 1 } else { u64::from(target.get_abi_alignment(&field)) };
            let size = target.get_abi_size(&field);
            checked_size = checked_size.checked_add(align - 1)
                .map(|n| n / align * align)
                .and_then(|n| n.checked_add(size))
                .ok_or_else(|| "Strukturgröße läuft über".to_string())?;
            checked_align = checked_align.max(align);
            llvm_fields.push(field);
        }
        checked_size.checked_add(checked_align - 1)
            .ok_or_else(|| "Strukturgröße läuft über".to_string())?;
        let mut layout = native_struct_layout(context, target, &llvm_fields, packed, alignment)?;
        layout.fields = fields.to_vec();
        self.layouts.insert(name.to_string(), layout);
        self.visible_names.insert(name.to_string());
        Ok(self.layouts.get(name).expect("eben eingefügte Struktur"))
    }
}

