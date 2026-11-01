//! Semantic native type identity. This module does not grant FFI capabilities.
//! Nominal names resolve only through the caller's explicit module/name map.
//! Compare identities only within one target and canonical module namespace;
//! target-dependent usize/isize layouts are not encoded in these IDs.
use std::collections::HashMap;
use crate::ast::{TypeKind, TypeRef};

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub(crate) enum ScalarType {
    I8, I16, I32, I64, U8, U16, U32, U64, F32, F64, Bool, ISize, USize,
}

#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub(crate) enum NativeTypeId {
    Scalar(ScalarType),
    Void,
    Pointer { address_space: u32, pointee: Box<NativeTypeId> },
    Buffer { element: Box<NativeTypeId> },
    // Quelltypidentität inkl. benannter Aufrufkonvention (P7-T4, 7A); die
    // zielabhängige Auflösung (z. B. "uefi") erfolgt erst beim Absenken.
    Function { params: Vec<NativeTypeId>, result: Box<NativeTypeId>, variadic: bool, convention: Option<String> },
    Nominal { module: String, name: String },
}

/// Key: source-visible name; value: canonical (module identity, declaration name).
/// Import resolution and ambiguous-name rejection remain the caller's responsibility.
pub(crate) type NominalTypes = HashMap<String, (String, String)>;

pub(crate) fn resolve_type(ty: &TypeRef, nominal: &NominalTypes) -> Result<NativeTypeId, String> {
    match &ty.kind {
        TypeKind::Name(name) => {
            let scalar = match name.as_str() {
                "i8" => Some(ScalarType::I8), "i16" => Some(ScalarType::I16),
                "i32" => Some(ScalarType::I32), "i64" => Some(ScalarType::I64),
                "u8" => Some(ScalarType::U8), "u16" => Some(ScalarType::U16),
                "u32" => Some(ScalarType::U32), "u64" => Some(ScalarType::U64),
                "f32" => Some(ScalarType::F32), "f64" => Some(ScalarType::F64),
                "bool" => Some(ScalarType::Bool),
                "isize" => Some(ScalarType::ISize), "usize" => Some(ScalarType::USize),
                _ => None,
            };
            if let Some(scalar) = scalar { return Ok(NativeTypeId::Scalar(scalar)); }
            if name == "void" { return Ok(NativeTypeId::Void); }
            let (module, declaration) = nominal.get(name)
                .ok_or_else(|| format!("Unbekannter nativer Typ '{name}'"))?;
            if module.is_empty() || declaration.is_empty() {
                return Err(format!("Typ '{name}' hat keine kanonische Modulidentität"));
            }
            Ok(NativeTypeId::Nominal { module: module.clone(), name: declaration.clone() })
        }
        TypeKind::Generic { name, args } if name == "ptr" || name == "zeiger" => {
            if args.len() != 1 { return Err("Zeiger benötigt genau einen Zieltyp".into()); }
            let pointee = resolve_type(&args[0], nominal)?;
            Ok(NativeTypeId::Pointer { address_space: 0, pointee: Box::new(pointee) })
        }
        TypeKind::Generic { name, args } if name == "puffer" || name == "buffer" => {
            if args.len() != 1 { return Err("Puffer benötigt genau einen Elementtyp".into()); }
            let element = resolve_type(&args[0], nominal)?;
            if element == NativeTypeId::Void { return Err("Puffer-Elementtyp void ist ungültig".into()); }
            Ok(NativeTypeId::Buffer { element: Box::new(element) })
        }
        TypeKind::Function { params, return_type, convention } => {
            if let Some(name) = convention {
                if !crate::ffi_types::CALL_CONVENTIONS.contains(&name.as_str()) {
                    return Err(format!("Unbekannte Aufrufkonvention '{name}' (erlaubt: sysv64, win64, uefi)"));
                }
            }
            let params = params.iter().map(|param| resolve_type(param, nominal))
                .collect::<Result<Vec<_>, _>>()?;
            let result = Box::new(resolve_type(return_type, nominal)?);
            // Das aktuelle TypeKind::Function kann keine Variadizität ausdrücken.
            Ok(NativeTypeId::Function { params, result, variadic: false, convention: convention.clone() })
        }
        _ => Err("Nativer Typ kann noch nicht eindeutig aufgelöst werden".into()),
    }
}
