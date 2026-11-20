//! Ziel-ABI-Layout für native Strukturen. Kein MooValue- oder Legacy-Pfad.
use inkwell::context::Context;
use inkwell::AddressSpace;
use inkwell::targets::TargetData;
use inkwell::types::{BasicType, BasicTypeEnum, StructType};
use std::collections::{HashMap, HashSet};
use crate::ast::{Decorator, DecoratorArg, Expr, TypeKind, TypeRef};
use crate::codegen_native::NType;


/// Obergrenze für @ausrichten(n) an Strukturen (Seitengröße).
pub(crate) const MAX_STRUKTUR_AUSRICHTUNG: u32 = 4096;

/// P2-T49 (Plan 38c59c64 E3): Feldarten der Laufzeitbeschreibung v2
/// (compiler/runtime/moo_struct_typed.h, MOO_FELD_*).
pub(crate) const FELD_SKALAR: u8 = 1;
pub(crate) const FELD_BOOL: u8 = 2;
pub(crate) const FELD_STRUKTUR: u8 = 3;
pub(crate) const FELD_ARRAY: u8 = 4;
/// P2-T49 E5: MOO_MAX_ALIGN der Runtime (malloc-Ausrichtung hosted). Boxdaten
/// kommen aus calloc; strenger ausgerichtete Strukturen sind nicht boxbar.
pub(crate) const MAX_BOX_AUSRICHTUNG: u32 = 16;

/// P2-T49: deklariertes Feld oberster Ebene der Laufzeitbeschreibung v2.
/// Arrays flach: anzahl Elemente der Art element (Stride groesse / anzahl).
#[derive(Clone, Debug)]
pub(crate) struct LaufzeitFeld {
    pub name: String,
    pub versatz: u64,
    pub groesse: u64,
    pub art: u8,
    pub element: u8,
    pub skalar: u8,
    pub anzahl: u64,
    pub struktur: Option<String>,
}

/// MooBufferScalarType-Code (compiler/runtime/moo_buffer.h); bool hat keinen.
fn skalar_code(t: NType) -> u8 {
    match t {
        NType::I8 => 1, NType::U8 => 2, NType::I16 => 3, NType::U16 => 4, NType::I32 => 5,
        NType::U32 => 6, NType::I64 => 7, NType::U64 => 8, NType::F32 => 9, NType::F64 => 10,
        NType::Bool => 0,
    }
}

#[derive(Clone)]
pub(crate) struct NativeStructLayout<'ctx> {
    pub llvm_type: StructType<'ctx>,
    pub size: u64,
    pub alignment: u32,
    pub offsets: Vec<u64>,
    /// Original fields are retained; canonical identities are resolved later.
    pub fields: Vec<(String, TypeRef)>,
    pub bitfelder: Vec<crate::ast::BitfeldBereich>,
}

/// Layout wird ausschließlich aus den Daten der Ziel-TargetMachine ermittelt.
/// P2-T13 (Plan a71947f7 C): @ausrichten(n) oberhalb der natürlichen Ausrichtung
/// hängt ein leeres Endelement `[0 x <n x i8>]` an. Feldindizes und -versätze
/// bleiben unverändert (wie C `_Alignas` an der Struktur); LLVM liefert die
/// Ausrichtung n und die auf n gerundete Größe, damit stimmen Einbettung in
/// andere Strukturen, Arraystride und Slots ohne Sonderpfade. Ein bloßes
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
    let natural = context.struct_type(fields, packed);
    if !natural.is_sized() {
        return Err("Struktur enthält einen Typ ohne feste Größe".into());
    }
    let natural_alignment = target.get_abi_alignment(&natural);
    let llvm_type = match explicit_alignment {
        Some(a) if a < natural_alignment => return Err(format!(
            "@ausrichten({a}) liegt unter der natürlichen Ausrichtung {natural_alignment} der Struktur"
        )),
        Some(a) if a > MAX_STRUKTUR_AUSRICHTUNG =>
            return Err(format!("@ausrichten({a}): höchstens {MAX_STRUKTUR_AUSRICHTUNG} wird unterstützt")),
        Some(a) if a > natural_alignment => {
            let mut elements = fields.to_vec();
            elements.push(context.i8_type().vec_type(a).array_type(0).into());
            let aligned = context.struct_type(&elements, false);
            let expected = target.get_abi_size(&natural).div_ceil(u64::from(a)) * u64::from(a);
            if target.get_abi_alignment(&aligned) != a || target.get_abi_size(&aligned) != expected {
                return Err(format!("@ausrichten({a}) ist für dieses Ziel nicht darstellbar"));
            }
            aligned
        }
        _ => natural,
    };
    let size = target.get_abi_size(&llvm_type);
    let alignment = target.get_abi_alignment(&llvm_type);
    let mut offsets = Vec::with_capacity(fields.len());
    for i in 0..count {
        let offset = target.offset_of_element(&llvm_type, i)
            .ok_or_else(|| format!("Offset für Strukturfeld {i} nicht bestimmbar"))?;
        // Selbstprüfung: die Strukturausrichtung verschiebt kein Feld.
        if Some(offset) != target.offset_of_element(&natural, i) {
            return Err(format!("interner Fehler: @ausrichten verschiebt Strukturfeld {i}"));
        }
        offsets.push(offset);
    }
    Ok(NativeStructLayout { llvm_type, size, alignment, offsets, fields: Vec::new(), bitfelder: Vec::new() })
}

/// Registry owned by one compiler/module. A name becomes usable only after its
/// full definition has passed validation; forward/recursive by-value fields
/// are deliberately rejected in the first slice.
#[derive(Clone)]
pub(crate) struct NativeStructRegistry<'ctx> {
    layouts: HashMap<String, NativeStructLayout<'ctx>>,
    visible_names: HashSet<String>,
    /// P2-T23/T53 (Plan 36aca1f2 E2): `@opak struktur Name` - nur Zeigerziel,
    /// ohne Layout (kein Wert, kein Feld, keine Groesse).
    opake: HashSet<String>,
    /// P2-T16 (Plan 3f03704a E1/E2): registrierte eingebaute vec2/vec3/vec4/
    /// mat4 (nicht: gleichnamige eigene Strukturen).
    eingebaut: HashSet<String>,
}

/// P2-T16 (Plan 3f03704a E1): eingebaute f32-Werttypen. vecN = N f32-
/// Komponenten (x, y, z, w), mat4 = vier vec4-Spalten s0..s3 (bytegleich zu
/// float[16] spaltenweise wie compiler/runtime/moo_3d_math.c). C-Layout:
/// Groesse/Ausrichtung 8/4, 12/4, 16/4, 64/4.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
/// P2-T89 (Freigabe #38178 O1): f64-Gegenstuecke dvec2/dvec3/dvec4/dmat4
/// (GLSL/glm-Konvention), C-Layout 16/8, 24/8, 32/8, 128/8; dmat4 = vier
/// dvec4-Spalten (bytegleich zu double[16] spaltenweise).
pub(crate) enum VecMatArt {
    Vec(u32),
    Mat4,
    DVec(u32),
    DMat4,
}

impl VecMatArt {
    pub(crate) fn name(self) -> &'static str {
        match self {
            VecMatArt::Vec(2) => "vec2",
            VecMatArt::Vec(3) => "vec3",
            VecMatArt::Vec(_) => "vec4",
            VecMatArt::Mat4 => "mat4",
            VecMatArt::DVec(2) => "dvec2",
            VecMatArt::DVec(3) => "dvec3",
            VecMatArt::DVec(_) => "dvec4",
            VecMatArt::DMat4 => "dmat4",
        }
    }

    /// f64-Variante (dvecN/dmat4).
    pub(crate) fn doppelt(self) -> bool {
        matches!(self, VecMatArt::DVec(_) | VecMatArt::DMat4)
    }

    pub(crate) fn ist_matrix(self) -> bool {
        matches!(self, VecMatArt::Mat4 | VecMatArt::DMat4)
    }

    /// Bahnen je Spalte (Vektor: Dimension, Matrix: 4).
    pub(crate) fn bahnen(self) -> u32 {
        match self {
            VecMatArt::Vec(n) | VecMatArt::DVec(n) => n,
            VecMatArt::Mat4 | VecMatArt::DMat4 => 4,
        }
    }

    /// Vektor gleicher Elementbreite mit n Bahnen (Spaltentyp: vektor(4)).
    pub(crate) fn vektor(self, n: u32) -> VecMatArt {
        if self.doppelt() { VecMatArt::DVec(n) } else { VecMatArt::Vec(n) }
    }

    /// Komponententyp f32 bzw. f64.
    pub(crate) fn element(self) -> NType {
        if self.doppelt() { NType::F64 } else { NType::F32 }
    }
}

pub(crate) const VECMAT_NAMEN: [&str; 8] = ["vec2", "vec3", "vec4", "mat4", "dvec2", "dvec3", "dvec4", "dmat4"];

pub(crate) fn vecmat_art_name(name: &str) -> Option<VecMatArt> {
    match name {
        "vec2" => Some(VecMatArt::Vec(2)),
        "vec3" => Some(VecMatArt::Vec(3)),
        "vec4" => Some(VecMatArt::Vec(4)),
        "mat4" => Some(VecMatArt::Mat4),
        "dvec2" => Some(VecMatArt::DVec(2)),
        "dvec3" => Some(VecMatArt::DVec(3)),
        "dvec4" => Some(VecMatArt::DVec(4)),
        "dmat4" => Some(VecMatArt::DMat4),
        _ => None,
    }
}

/// Felder der eingebauten Definition (Quellort 0: nicht aus dem Programm).
pub(crate) fn vecmat_felder(name: &str) -> Option<Vec<(String, TypeRef)>> {
    let typ = |n: &str| TypeRef { kind: TypeKind::Name(n.to_string()), line: 0, column: 0, end_line: 0, end_column: 0 };
    let art = vecmat_art_name(name)?;
    let skalar = if art.doppelt() { "f64" } else { "f32" };
    let (felder, feldtyp): (&[&str], &str) = match art {
        VecMatArt::Mat4 => (&["s0", "s1", "s2", "s3"], "vec4"),
        VecMatArt::DMat4 => (&["s0", "s1", "s2", "s3"], "dvec4"),
        _ => match art.bahnen() {
            2 => (&["x", "y"], skalar),
            3 => (&["x", "y", "z"], skalar),
            _ => (&["x", "y", "z", "w"], skalar),
        },
    };
    Some(felder.iter().map(|f| (f.to_string(), typ(feldtyp))).collect())
}

/// Laenge eines festen Arrays `[T; N]`: konstantes, positives Ganzzahlliteral
/// (dezimal, 0x, 0b, 0o). Gemeinsam fuer Strukturfelder und (P2-T48) lokale
/// Arrays im Systemkoerper (native_system.rs STy::Array).
pub(crate) fn array_laenge(length: &Expr) -> Result<u32, String> {
    let n = match length {
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
    Ok(n)
}

/// Pointer fields are opaque in LLVM; validate their *source* shape separately
/// and retain it for later canonical type resolution before any use/call.
fn validate_pointer_target(ty: &TypeRef, names: &HashSet<String>) -> Result<(), String> {
    match &ty.kind {
        // P2-T97 (34119): kein Bool-Zeigerziel, auch nicht in Strukturfeldern.
        TypeKind::Name(name) if name == "bool" => Err(crate::native_type_id::BOOL_ZEIGERZIEL.into()),
        TypeKind::Name(name) if name == "void" || NType::from_name(name).is_some() || names.contains(name) => Ok(()),
        TypeKind::Name(name) => Err(format!("Unbekannter Pointer-Zieltyp '{name}'")),
        TypeKind::Generic { name, .. } if crate::native_type_id::ist_atom_typ(name) =>
            crate::native_type_id::atom_element(ty).map(|_| ()),
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

/// Endianbezeichnungen sind Speicherfeldtypen, keine numerischen Casts.
pub(crate) fn endian_feld(ty: &TypeRef) -> Option<(NType, bool)> {
    let TypeKind::Name(name) = &ty.kind else { return None; };
    Some(match name.as_str() {
        "u16be" => (NType::U16, true), "u16le" => (NType::U16, false),
        "u32be" => (NType::U32, true), "u32le" => (NType::U32, false),
        "u64be" => (NType::U64, true), "u64le" => (NType::U64, false),
        _ => return None,
    })
}

/// Only by-value fields require an already materialized layout; pointer edges
/// are checked for names/types, but do not participate in layout cycles.
fn collect_value_dependencies(
    ty: &TypeRef, names: &HashSet<String>, dependencies: &mut HashSet<String>,
) -> Result<(), String> {
    if endian_feld(ty).is_some() { return Ok(()); }
    match &ty.kind {
        TypeKind::Array { element, .. } if endian_feld(element).is_some() =>
            Err("Endian-Feldtypen gehören an benannte Strukturfelder, nicht unmittelbar in ein Array".into()),
        TypeKind::Name(name) if NType::from_name(name).is_some() => Ok(()),
        TypeKind::Name(name) if names.contains(name) => { dependencies.insert(name.clone()); Ok(()) },
        TypeKind::Name(name) => Err(format!("Unbekannter Strukturfeldtyp '{name}'")),
        TypeKind::Array { element, .. } => collect_value_dependencies(element, names, dependencies),
        TypeKind::Generic { name, .. } if crate::native_type_id::ist_atom_typ(name) =>
            crate::native_type_id::atom_element(ty).map(|_| ()),
        TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 =>
            validate_pointer_target(&args[0], names),
        _ => Err("Strukturfeldtyp wird noch nicht unterstützt".into()),
    }
}

/// P2-T13/T48 (Plan a71947f7 E1): fester C-Typ je nativem Skalar.
fn c_skalar(scalar: NType) -> &'static str {
    match scalar {
        NType::I8 => "int8_t", NType::I16 => "int16_t",
        NType::I32 => "int32_t", NType::I64 => "int64_t",
        NType::U8 => "uint8_t", NType::U16 => "uint16_t",
        NType::U32 => "uint32_t", NType::U64 => "uint64_t",
        NType::F32 => "float", NType::F64 => "double",
        // 1 Byte wie uint8_t; der Header belegt nur das Layout, die Übergabe
        // als Argument regelt die C-ABI-Klassifizierung (P2-T56).
        NType::Bool => "_Bool",
    }
}

/// C11-Schlüsselwörter und Namen, die der erzeugte Header selbst benutzt.
const C_RESERVIERT: &[&str] = &[
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
    "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
    "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
    "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Alignas",
    "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic", "_Imaginary", "_Noreturn",
    "_Static_assert", "_Thread_local", "offsetof", "size_t", "ptrdiff_t", "NULL",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
];

/// Moo-Namen gehen unverändert in den Header; ein in C nicht darstellbarer
/// Name ist ein Fehler statt einer stillen Umbenennung (Felder müssen passen).
fn c_bezeichner(name: &str) -> Result<&str, String> {
    let mut zeichen = name.chars();
    let start = zeichen.next().is_some_and(|c| c == '_' || c.is_alphabetic());
    if !start || !zeichen.all(|c| c == '_' || c.is_alphanumeric()) {
        return Err(format!("'{name}' ist kein gültiger C-Bezeichner"));
    }
    if C_RESERVIERT.contains(&name) {
        return Err(format!("'{name}' ist in C reserviert und kann im Layout-Header nicht stehen"));
    }
    Ok(name)
}

/// Kanonische Typschreibweise fuer struktur_id (ohne Quellpositionen, Aliasse vereinheitlicht).
fn typ_text(ty: &TypeRef) -> String {
    let liste = |tys: &[TypeRef]| tys.iter().map(typ_text).collect::<Vec<_>>().join(",");
    match &ty.kind {
        TypeKind::Name(name) => name.clone(),
        TypeKind::Generic { name, args } => {
            let name = match name.as_str() { "zeiger" => "ptr", "buffer" => "puffer", other => other };
            format!("{name}[{}]", liste(args))
        }
        TypeKind::Array { element, length } => format!("[{};{}]", typ_text(element),
            array_laenge(length).map_or_else(|_| "?".to_string(), |n| n.to_string())),
        TypeKind::Function { params, return_type, convention } => format!("fn[{}]({})->{}",
            convention.as_deref().unwrap_or(""), liste(params), typ_text(return_type)),
    }
}

fn c_verbinden(basis: &str, deklarator: &str) -> String {
    if deklarator.is_empty() { basis.to_string() } else { format!("{basis} {deklarator}") }
}

pub(crate) type LayoutSnapshot = Vec<(String, u64, u32, Vec<(String, u64)>)>;

impl<'ctx> NativeStructRegistry<'ctx> {
    pub(crate) fn new() -> Self {
        Self { layouts: HashMap::new(), visible_names: HashSet::new(), opake: HashSet::new(), eingebaut: HashSet::new() }
    }

    /// P2-T23/T53: opaker Typ (nur als Zeigerziel).
    pub(crate) fn ist_opak(&self, name: &str) -> bool {
        self.opake.contains(name)
    }

    /// P2-T16: Art eines eingebauten Vektor-/Matrixtyps; None fuer jede andere
    /// (auch eine gleichnamige eigene) Struktur.
    pub(crate) fn vecmat_art(&self, name: &str) -> Option<VecMatArt> {
        if self.eingebaut.contains(name) { vecmat_art_name(name) } else { None }
    }

    /// Eigene gleichnamige Strukturen behalten ihre normalen Strukturregeln.
    pub(crate) fn simd_art(&self, name: &str) -> Option<crate::native_simd::SimdArt> {
        if self.eingebaut.contains(name) { crate::native_simd::art(name) } else { None }
    }

    /// P2-T16: nach register_all die eingebauten Definitionen markieren.
    pub(crate) fn markiere_eingebaut(&mut self, name: &str) {
        self.eingebaut.insert(name.to_string());
    }

    /// P7-T4 (1A): fertiges Layout einer registrierten Struktur.
    pub(crate) fn layout(&self, name: &str) -> Option<&NativeStructLayout<'ctx>> {
        self.layouts.get(name)
    }

    /// Liefert kanonische lokale Strukturidentitaeten fuer ptr[Struktur].
    /// Importierte Module sind Teil derselben kompilierten Einheit und eines
    /// globalen Namensraums (P2-T47, Plan b1f51d65 E1/E4): ein Strukturname ist
    /// programmweit eindeutig, gleichnamige Strukturen aus zwei Dateien lehnt
    /// der Resolver (main.rs strukturen_registrieren) mit beiden Pfaden ab.
    pub(crate) fn nominal_types(&self, module_name: &str) -> crate::native_type_id::NominalTypes {
        self.layouts.keys().chain(self.opake.iter()).map(|name| (
            name.clone(), (module_name.to_string(), name.clone()),
        )).collect()
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

    /// Prueft eingebetteten Speicher. Zeiger sind keine eingebetteten Werte.
    pub(crate) fn hat_atom_speicher(&self, ty: &TypeRef) -> bool {
        self.findet_atom_element(ty, &|_| true)
    }

    fn findet_atom_element(&self, ty: &TypeRef, pruefe: &impl Fn(&TypeRef) -> bool) -> bool {
        match &ty.kind {
            TypeKind::Generic { name, .. } if crate::native_type_id::ist_atom_typ(name) =>
                crate::native_type_id::atom_element(ty).is_ok_and(pruefe),
            TypeKind::Name(name) if NType::from_name(name).is_none() =>
                self.layout(name).is_some_and(|layout| layout.fields.iter()
                    .any(|(_, field)| self.findet_atom_element(field, pruefe))),
            TypeKind::Array { element, .. } => self.findet_atom_element(element, pruefe),
            _ => false,
        }
    }

    pub(crate) fn field_type(
        &self, context: &'ctx Context, target: &TargetData, ty: &TypeRef,
    ) -> Result<BasicTypeEnum<'ctx>, String> {
        if let Some((scalar, _)) = endian_feld(ty) { return Ok(scalar.llvm_type(context)); }
        match &ty.kind {
            TypeKind::Name(name) => {
                if let Some(scalar) = NType::from_name(name) {
                    return Ok(scalar.llvm_type(context));
                }
                self.layouts.get(name).map(|layout| layout.llvm_type.into())
                    .ok_or_else(|| format!("Unbekannter Strukturfeldtyp '{name}'"))
            }
            TypeKind::Generic { name, .. } if crate::native_type_id::ist_atom_typ(name) =>
                self.field_type(context, target, crate::native_type_id::atom_element(ty)?),
            TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 => {
                validate_pointer_target(&args[0], &self.visible_names)?;
                Ok(context.ptr_type(AddressSpace::default()).into())
            }
            TypeKind::Array { element, length } => {
                let n = array_laenge(length)?;
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
        // P2-T23/T53: opake Typen zuerst (kein Layout, nur Name).
        for (name, fields, decorators) in definitions {
            if decorators.iter().any(|d| d.name == "opak" || d.name == "opaque") {
                if !fields.is_empty() || decorators.len() != 1 || !decorators[0].args.is_empty() {
                    return Err(format!("@opak struktur '{name}': opake Typen haben keine Felder und keine weiteren Decorators"));
                }
                staged.opake.insert((*name).to_string());
            }
        }
        let mut pending = Vec::with_capacity(definitions.len());
        for (index, (name, fields, _)) in definitions.iter().enumerate() {
            if staged.opake.contains(*name) { continue; }
            let mut dependencies = HashSet::new();
            for (_, ty) in *fields {
                collect_value_dependencies(ty, &names, &mut dependencies)?;
            }
            if let Some(opak) = dependencies.iter().find(|d| staged.opake.contains(*d)) {
                return Err(format!("Struktur '{name}': opaker Typ '{opak}' ist nur als Zeigerziel ptr[{opak}] erlaubt"));
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

    /// Eine geprüfte Byteansicht darf keine ungültigen Bitmuster oder Besitz-
    /// bzw. Zeigerwerte aus beliebigen Eingabebytes erzeugen.
    pub(crate) fn sicht_zulaessig(&self, name: &str) -> Result<(), String> {
        let layout = self.layout(name).ok_or_else(|| format!("sicht: unbekannte Struktur '{name}'"))?;
        if self.ist_opak(name) || !layout.bitfelder.is_empty() {
            return Err(format!("sicht: '{name}' ist kein vollständiger On-Disk-Strukturtyp"));
        }
        fn feld(reg: &NativeStructRegistry<'_>, ty: &TypeRef) -> Result<(), String> {
            if endian_feld(ty).is_some() { return Ok(()); }
            match &ty.kind {
                TypeKind::Name(n) if matches!(n.as_str(), "u8" | "i8") => Ok(()),
                TypeKind::Name(n) if reg.layout(n).is_some() => reg.sicht_zulaessig(n),
                TypeKind::Array { element, .. } => feld(reg, element),
                _ => Err("sicht erlaubt Bytefelder, Endian-Felder und eingebettete On-Disk-Strukturen; keine Zeiger, bool, atom[T] oder hostabhängigen Wortfelder".into()),
            }
        }
        for (_, ty) in &layout.fields { feld(self, ty)?; }
        Ok(())
    }

    pub(crate) fn register_bitfelder(&mut self, name: &str, bereiche: &[crate::ast::BitfeldBereich]) -> Result<(), String> {
        if bereiche.is_empty() { return Ok(()); }
        let layout = self.layouts.get_mut(name).ok_or("Bitfeld-Strukturlayout fehlt")?;
        if layout.fields.len() != 1 || layout.fields[0].0 != "__bits" {
            return Err(format!("Bitfeld '{name}': internes Basiswort fehlt"));
        }
        let bits = layout.llvm_type.get_field_types()[0].into_int_type().get_bit_width();
        let mut belegt = 0u64;
        let mut namen = HashSet::new();
        for feld in bereiche {
            if feld.breite == 0 || feld.breite > 64 || feld.anfang + feld.breite > bits {
                return Err(crate::diagnose::vormerken("M-C000", feld.zeile,
                    format!("Bitfeld '{name}.{}': Bereich ueberschreitet die Basisbreite {bits}", feld.name)));
            }
            if !namen.insert(&feld.name) {
                return Err(format!("Bitfeld '{name}': Feld '{}' ist doppelt", feld.name));
            }
            let maske = feld.maximum() << feld.anfang;
            if belegt & maske != 0 {
                return Err(crate::diagnose::vormerken("M-C000", feld.zeile,
                    format!("Bitfeld '{name}.{}': Bereich ueberlappt einen anderen Bereich", feld.name)));
            }
            belegt |= maske;
        }
        layout.bitfelder = bereiche.to_vec();
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
            if packed && self.findet_atom_element(ty, &|element| {
                NType::from_type_ref(element).is_ok_and(|t| target.get_abi_alignment(&t.llvm_type(context)) > 1)
            }) {
                return Err(format!("Struktur '{name}.{field_name}': atom[T] braucht natuerliche Ausrichtung; @gepackt ist hier unzulaessig"));
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

    /// P2-T56 A1 (Plan 91033131): flache Feldliste einer Struktur fuer die
    /// C-ABI-Klassifizierung (crate::ffi_abi): eingebettete Strukturen und
    /// [T; N] rekursiv mit Layoutversatz/Stride; Zeiger und bool als Ganzzahlklasse.
    pub(crate) fn flach(&self, context: &'ctx Context, target: &TargetData, name: &str)
        -> Result<crate::ffi_abi::StrukturForm, String>
    {
        let layout = self.layouts.get(name).ok_or_else(|| format!("Unbekannte Struktur '{name}'"))?;
        let mut felder = Vec::new();
        for ((_, ty), versatz) in layout.fields.iter().zip(&layout.offsets) {
            self.flach_typ(context, target, ty, *versatz, &mut felder)?;
        }
        Ok(crate::ffi_abi::StrukturForm { groesse: layout.size, ausrichtung: u64::from(layout.alignment), felder })
    }

    fn flach_typ(
        &self, context: &'ctx Context, target: &TargetData, ty: &TypeRef, basis: u64,
        out: &mut Vec<crate::ffi_abi::FlachFeld>,
    ) -> Result<(), String> {
        use crate::ffi_abi::{Art, FlachFeld};
        match &ty.kind {
            TypeKind::Name(name) => {
                if let Some(scalar) = NType::from_name(name) {
                    let art = match scalar { NType::F32 => Art::F32, NType::F64 => Art::F64, _ => Art::Ganzzahl };
                    let groesse = target.get_abi_size(&scalar.llvm_type(context));
                    out.push(FlachFeld { versatz: basis, groesse, art });
                    return Ok(());
                }
                let layout = self.layouts.get(name).ok_or_else(|| format!("Unbekannte Struktur '{name}'"))?;
                for ((_, feld), versatz) in layout.fields.iter().zip(&layout.offsets) {
                    self.flach_typ(context, target, feld, basis + versatz, out)?;
                }
                Ok(())
            }
            TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 => {
                out.push(FlachFeld { versatz: basis, groesse: u64::from(target.get_pointer_byte_size(None)), art: Art::Zeiger });
                Ok(())
            }
            TypeKind::Array { element, length } => {
                let n = array_laenge(length)?;
                let stride = target.get_abi_size(&self.field_type(context, target, element)?);
                for i in 0..u64::from(n) {
                    self.flach_typ(context, target, element, basis + i * stride, out)?;
                }
                Ok(())
            }
            _ => Err("Strukturfeldtyp hat keine C-ABI-Klasse".into()),
        }
    }

    /// P2-T48 D (Plan a71947f7 E4): Laufzeit-Identitaet fuer puffer[Struktur] =
    /// 64-Bit-FNV-1a ueber "Name|Groesse|Ausrichtung|feld:typ@versatz;...".
    /// Gleiche Definition -> gleiche ID; anderer Name, andere Felder oder anderes
    /// Layout -> andere ID (Grenzfehler statt stiller Fehlinterpretation).
    /// Importierte Strukturen (P2-T47, E4): Typidentitaet bleibt Name + Layout;
    /// der Name ist im globalen Namensraum programmweit eindeutig, daher
    /// erhaelt eine Struktur bei Rautenimport dieselbe ID (Gate frontend_module matrix).
    pub(crate) fn struktur_id(&self, name: &str) -> Option<u64> {
        // P2-T23/T53: opake Typen: Identitaet nur ueber den Namen.
        let text = if self.opake.contains(name) {
            format!("opak|{name}")
        } else {
            let layout = self.layouts.get(name)?;
            let mut text = format!("{name}|{}|{}|", layout.size, layout.alignment);
            for ((feld, ty), versatz) in layout.fields.iter().zip(&layout.offsets) {
                text.push_str(&format!("{feld}:{}@{versatz};", typ_text(ty)));
            }
            text
        };
        let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
        for byte in text.bytes() {
            hash ^= u64::from(byte);
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
        Some(hash.max(1))
    }

    /// P2-T49 (Plan 38c59c64 E5): darf ein Wert dieser Struktur in dynamisches
    /// Moo (MOO_STRUCT-Box)? Nicht: opak, Ausrichtung > MAX_BOX_AUSRICHTUNG,
    /// Zeiger-/Funktionszeigerfelder (auch verschachtelt; Lebensdauer in der
    /// dynamischen Ebene nicht pruefbar).
    pub(crate) fn boxbar(&self, name: &str) -> Result<(), String> {
        self.boxbar_tiefe(name, 0)
    }

    fn boxbar_tiefe(&self, name: &str, tiefe: u32) -> Result<(), String> {
        if name.starts_with("ergebnis[") {
            return Err("ergebnis[T, F] ist ein nativer Must-use-Wert und darf nicht implizit als MooValue geboxed werden".into());
        }
        if self.opake.contains(name) {
            return Err(format!("'{name}' ist opak (nur als Zeigerziel ptr[{name}])"));
        }
        let layout = self.layouts.get(name).ok_or_else(|| format!("unbekannte Struktur '{name}'"))?;
        if layout.alignment > MAX_BOX_AUSRICHTUNG {
            return Err(format!("Ausrichtung {} von '{name}' liegt ueber {MAX_BOX_AUSRICHTUNG}", layout.alignment));
        }
        if tiefe > 32 {
            return Err(format!("'{name}' ist zu tief verschachtelt"));
        }
        for (feld, ty) in &layout.fields {
            let (_, _, _, innen) = self.element_art(ty).map_err(|g| format!("Feld '{name}.{feld}': {g}"))?;
            if let Some(innen) = innen { self.boxbar_tiefe(&innen, tiefe + 1)?; }
        }
        Ok(())
    }

    /// (Elementart, Skalarcode, Elementanzahl, Elementstruktur); Arrays (auch
    /// verschachtelt) flach, da ihre Elemente ohne Zwischenraum liegen.
    fn element_art(&self, ty: &TypeRef) -> Result<(u8, u8, u64, Option<String>), String> {
        match &ty.kind {
            TypeKind::Name(name) => {
                if let Some(s) = NType::from_name(name) {
                    return Ok(if s == NType::Bool { (FELD_BOOL, 0, 1, None) } else { (FELD_SKALAR, skalar_code(s), 1, None) });
                }
                if self.layouts.contains_key(name) {
                    Ok((FELD_STRUKTUR, 0, 1, Some(name.clone())))
                } else {
                    Err(format!("Typ '{name}' hat keine Laufzeitbeschreibung"))
                }
            }
            TypeKind::Array { element, length } => {
                let n = u64::from(array_laenge(length)?);
                let (art, skalar, anzahl, struktur) = self.element_art(element)?;
                let anzahl = anzahl.checked_mul(n).ok_or_else(|| "Arraygroesse laeuft ueber".to_string())?;
                Ok((art, skalar, anzahl, struktur))
            }
            TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger" =>
                Err("Zeigerfelder sind in dynamischem Moo nicht erlaubt (Lebensdauer nicht pruefbar)".into()),
            _ => Err("Feldtyp hat keine Laufzeitbeschreibung".into()),
        }
    }

    /// P2-T49 B1: deklarierte Felder oberster Ebene (Name, Ausgabe, Gleichheit).
    pub(crate) fn laufzeit_felder(&self, context: &'ctx Context, target: &TargetData, name: &str)
        -> Result<Vec<LaufzeitFeld>, String>
    {
        let layout = self.layouts.get(name).ok_or_else(|| format!("Unbekannte Struktur '{name}'"))?;
        let mut felder = Vec::with_capacity(layout.fields.len());
        for ((feld, ty), versatz) in layout.fields.iter().zip(&layout.offsets) {
            let groesse = target.get_abi_size(&self.field_type(context, target, ty)?);
            let (element, skalar, anzahl, struktur) = self.element_art(ty)?;
            let art = if matches!(ty.kind, TypeKind::Array { .. }) { FELD_ARRAY } else { element };
            felder.push(LaufzeitFeld { name: feld.clone(), versatz: *versatz, groesse, art, element, skalar, anzahl, struktur });
        }
        Ok(felder)
    }

    /// P2-T49 B1: flache Blaetter (Pfadname wie "a.b"/"a[2]", Versatz, Groesse)
    /// fuer Kopie mit Padding 0. Lueckenlos aufeinanderfolgende Blaetter werden
    /// zusammengefasst (Arrays, padding-freie Folgen): weniger Laufzeitpruefung.
    pub(crate) fn laufzeit_blaetter(&self, context: &'ctx Context, target: &TargetData, name: &str)
        -> Result<Vec<(String, u64, u64)>, String>
    {
        let layout = self.layouts.get(name).ok_or_else(|| format!("Unbekannte Struktur '{name}'"))?;
        let mut roh = Vec::new();
        for ((feld, ty), versatz) in layout.fields.iter().zip(&layout.offsets) {
            self.blaetter_typ(context, target, ty, feld.clone(), *versatz, &mut roh)?;
        }
        let mut blaetter: Vec<(String, u64, u64)> = Vec::with_capacity(roh.len());
        for (pfad, versatz, groesse) in roh {
            if let Some(letztes) = blaetter.last_mut() {
                if letztes.1 + letztes.2 == versatz {
                    letztes.2 += groesse;
                    continue;
                }
            }
            blaetter.push((pfad, versatz, groesse));
        }
        Ok(blaetter)
    }

    fn blaetter_typ(
        &self, context: &'ctx Context, target: &TargetData, ty: &TypeRef, pfad: String, basis: u64,
        out: &mut Vec<(String, u64, u64)>,
    ) -> Result<(), String> {
        match &ty.kind {
            TypeKind::Name(name) => {
                if let Some(s) = NType::from_name(name) {
                    out.push((pfad, basis, target.get_abi_size(&s.llvm_type(context))));
                    return Ok(());
                }
                let layout = self.layouts.get(name).ok_or_else(|| format!("Unbekannte Struktur '{name}'"))?;
                for ((feld, innen), versatz) in layout.fields.iter().zip(&layout.offsets) {
                    self.blaetter_typ(context, target, innen, format!("{pfad}.{feld}"), basis + versatz, out)?;
                }
                Ok(())
            }
            TypeKind::Array { element, length } => {
                let n = array_laenge(length)?;
                let stride = target.get_abi_size(&self.field_type(context, target, element)?);
                for i in 0..u64::from(n) {
                    self.blaetter_typ(context, target, element, format!("{pfad}[{i}]"), basis + i * stride, out)?;
                }
                Ok(())
            }
            _ => Err("Feldtyp hat keine Laufzeitbeschreibung".into()),
        }
    }

    /// Rendert `ty` als C-Deklarator um `innen` (Feldname oder leer).
    fn c_deklarator(&self, ty: &TypeRef, innen: String) -> Result<String, String> {
        match &ty.kind {
            TypeKind::Name(name) if name == "void" => Ok(c_verbinden("void", &innen)),
            TypeKind::Name(name) => {
                let basis = match NType::from_name(name) {
                    Some(scalar) => c_skalar(scalar),
                    None if self.visible_names.contains(name) => c_bezeichner(name)?,
                    None => return Err(format!("Unbekannter Strukturfeldtyp '{name}'")),
                };
                Ok(c_verbinden(basis, &innen))
            }
            TypeKind::Generic { name, args } if (name == "ptr" || name == "zeiger") && args.len() == 1 => {
                let innen = match &args[0].kind {
                    TypeKind::Array { .. } | TypeKind::Function { .. } => format!("(*{innen})"),
                    _ => format!("*{innen}"),
                };
                self.c_deklarator(&args[0], innen)
            }
            TypeKind::Array { element, length } =>
                self.c_deklarator(element, format!("{innen}[{}]", array_laenge(length)?)),
            TypeKind::Function { params, return_type, .. } => {
                let params = if params.is_empty() { "void".to_string() } else {
                    params.iter().map(|p| self.c_deklarator(p, String::new()))
                        .collect::<Result<Vec<_>, _>>()?.join(", ")
                };
                self.c_deklarator(return_type, format!("{innen}({params})"))
            }
            _ => Err("Strukturfeldtyp hat keine C-Darstellung".into()),
        }
    }

    /// P2-T13/T48 (Plan a71947f7 E1): die registrierten Ziel-Layouts als
    /// C11-Header. Der C-Compiler des Ziels prüft per _Static_assert Größe,
    /// Ausrichtung, Feldversätze, Feldgrößen und den Stride jeder Arraydimension;
    /// weicht das C-Layout ab, scheitert schon die Übersetzung (auch cross).
    /// @gepackt wird als `#pragma pack(push, 1)` geschrieben (GCC, Clang, MSVC).
    pub(crate) fn c_header(
        &self, context: &'ctx Context, target: &TargetData, triple: &str, guard: &str,
    ) -> Result<String, String> {
        use std::fmt::Write;
        let mut namen: Vec<&String> = self.layouts.keys().collect();
        namen.sort();
        let mut out = String::new();
        let _ = writeln!(out, "/* Erzeugt von moo-compiler layout --c-header, Ziel {triple}. Nicht von Hand aendern. */");
        let _ = writeln!(out, "/* bool-Felder sind _Bool (1 Byte wie uint8_t); belegt ist nur das Layout, nicht die Aufrufkonvention. */");
        let _ = writeln!(out, "#ifndef {guard}\n#define {guard}\n#include <stddef.h>\n#include <stdint.h>\n");
        let zeiger = target.get_pointer_byte_size(None);
        let _ = writeln!(out, "_Static_assert(sizeof(void *) == {zeiger}, \"Ziel {triple}: sizeof(void *)\");");
        for name in &namen {
            let ident = c_bezeichner(name)?;
            let _ = writeln!(out, "typedef struct {ident} {ident};");
        }
        // Wertfelder brauchen die vollständige Definition vorher; Zeigerfelder
        // kommen mit den typedef-Vorwärtsdeklarationen aus.
        let mut fertig: HashSet<&str> = HashSet::new();
        let mut offen = namen;
        while !offen.is_empty() {
            let vorher = offen.len();
            let mut rest = Vec::new();
            for name in offen {
                let layout = &self.layouts[name.as_str()];
                let mut abhaengig = HashSet::new();
                for (_, ty) in &layout.fields {
                    collect_value_dependencies(ty, &self.visible_names, &mut abhaengig)?;
                }
                if abhaengig.iter().all(|d| fertig.contains(d.as_str())) {
                    self.c_struktur(context, target, name, layout, &mut out)?;
                    fertig.insert(name.as_str());
                } else {
                    rest.push(name);
                }
            }
            if rest.len() == vorher {
                return Err("Rekursiver Strukturwert-Zyklus ist nicht erlaubt".into());
            }
            offen = rest;
        }
        let _ = writeln!(out, "\n#endif /* {guard} */");
        Ok(out)
    }

    fn c_struktur(
        &self, context: &'ctx Context, target: &TargetData, name: &str,
        layout: &NativeStructLayout<'ctx>, out: &mut String,
    ) -> Result<(), String> {
        use std::fmt::Write;
        if layout.fields.len() != layout.offsets.len() {
            return Err(format!("Struktur '{name}': Feld-/Offsetanzahl widerspricht sich"));
        }
        let ident = c_bezeichner(name)?;
        let gepackt = layout.llvm_type.is_packed();
        // @ausrichten(n) > natürlich: Endelement vorhanden -> C11 _Alignas am
        // ersten Feld (hebt die Strukturausrichtung, Versatz 0 bleibt).
        let ausgerichtet = layout.llvm_type.count_fields() as usize > layout.fields.len();
        out.push('\n');
        if gepackt { out.push_str("#pragma pack(push, 1)\n"); }
        let _ = writeln!(out, "struct {ident} {{");
        for (index, (feld, ty)) in layout.fields.iter().enumerate() {
            let praefix = if ausgerichtet && index == 0 { format!("_Alignas({}) ", layout.alignment) } else { String::new() };
            let _ = writeln!(out, "    {praefix}{};", self.c_deklarator(ty, c_bezeichner(feld)?.to_string())?);
        }
        out.push_str("};\n");
        if gepackt { out.push_str("#pragma pack(pop)\n"); }
        let _ = writeln!(out, "_Static_assert(sizeof({ident}) == {}, \"{ident}: sizeof\");", layout.size);
        let _ = writeln!(out, "_Static_assert(_Alignof({ident}) == {}, \"{ident}: _Alignof\");", layout.alignment);
        for ((feld, ty), versatz) in layout.fields.iter().zip(&layout.offsets) {
            let _ = writeln!(out, "_Static_assert(offsetof({ident}, {feld}) == {versatz}, \"{ident}.{feld}: offsetof\");");
            // Feldgröße, dann Stride jeder Arraydimension ([0], [0][0], ...).
            let mut stufe = ty;
            let mut dims = String::new();
            loop {
                let groesse = target.get_abi_size(&self.field_type(context, target, stufe)?);
                let _ = writeln!(out, "_Static_assert(sizeof((({ident} *)0)->{feld}{dims}) == {groesse}, \"{ident}.{feld}{dims}: sizeof\");");
                match &stufe.kind {
                    TypeKind::Array { element, .. } => { dims.push_str("[0]"); stufe = element; }
                    _ => break,
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod flach_tests {
    //! P2-T56 A1 (Review 76c030bf): flach() ueber eingebettete Strukturen,
    //! [T; N] mit Layout-Stride und Zeigerfelder, mit exakten Versaetzen.
    use super::*;
    use crate::ffi_abi::{klassifiziere, Abi, Art, FlachFeld, Teil, Uebergabe};

    fn ty(name: &str) -> TypeRef {
        TypeRef { kind: TypeKind::Name(name.into()), line: 1, column: 1, end_line: 1, end_column: 1 }
    }
    fn arr(element: TypeRef, n: u32) -> TypeRef {
        let length = Expr::NativeNumber { legacy: f64::from(n), text: n.to_string(), line: 1, column: 1 };
        TypeRef { kind: TypeKind::Array { element: Box::new(element), length: Box::new(length) },
            line: 1, column: 1, end_line: 1, end_column: 1 }
    }
    fn zeiger(ziel: &str) -> TypeRef {
        TypeRef { kind: TypeKind::Generic { name: "ptr".into(), args: vec![ty(ziel)] },
            line: 1, column: 1, end_line: 1, end_column: 1 }
    }
    fn feld(name: &str, t: TypeRef) -> (String, TypeRef) { (name.into(), t) }
    fn ff(versatz: u64, groesse: u64, art: Art) -> FlachFeld { FlachFeld { versatz, groesse, art } }

    /// x86_64-SysV-Datenlayout (LLVM, x86_64-unknown-linux-gnu).
    fn x86_64() -> TargetData {
        TargetData::create("e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128")
    }

    #[test]
    fn verschachtelt_array_zeiger() {
        let context = Context::create();
        let target = x86_64();
        let mut reg = NativeStructRegistry::new();
        // Innen { u8 tag; f32 wert } = 8 Byte; Aussen { u16 kopf; Innen innen[2]; ptr[u8] p } = 32 Byte.
        let innen = [feld("tag", ty("u8")), feld("wert", ty("f32"))];
        let aussen = [feld("kopf", ty("u16")), feld("innen", arr(ty("Innen"), 2)), feld("p", zeiger("u8"))];
        reg.register_all(&context, &target, &[("Innen", &innen, &[]), ("Aussen", &aussen, &[])]).unwrap();
        let form = reg.flach(&context, &target, "Aussen").unwrap();
        assert_eq!((form.groesse, form.ausrichtung), (32, 8));
        assert_eq!(form.felder, vec![
            ff(0, 2, Art::Ganzzahl),
            ff(4, 1, Art::Ganzzahl), ff(8, 4, Art::F32),
            ff(12, 1, Art::Ganzzahl), ff(16, 4, Art::F32),
            ff(24, 8, Art::Zeiger),
        ]);
        // > 16 Byte: SysV Speicherklasse, AAPCS64 indirekt.
        assert_eq!(klassifiziere(Abi::SysV, &form, false), Ok(Uebergabe::Byval { ausrichtung: 8 }));
        assert_eq!(klassifiziere(Abi::Aapcs64, &form, false), Ok(Uebergabe::Indirekt));
    }

    #[test]
    fn verschachteltes_float_array_als_hfa() {
        let context = Context::create();
        let target = x86_64();
        let mut reg = NativeStructRegistry::new();
        // V2 { f32 x; f32 y }; Seg { V2 ecken[2] } = 16 Byte, vier f32 nach Abflachung.
        let v2 = [feld("x", ty("f32")), feld("y", ty("f32"))];
        let seg = [feld("ecken", arr(ty("V2"), 2))];
        reg.register_all(&context, &target, &[("V2", &v2, &[]), ("Seg", &seg, &[])]).unwrap();
        let form = reg.flach(&context, &target, "Seg").unwrap();
        assert_eq!((form.groesse, form.ausrichtung), (16, 4));
        assert_eq!(form.felder, vec![ff(0, 4, Art::F32), ff(4, 4, Art::F32), ff(8, 4, Art::F32), ff(12, 4, Art::F32)]);
        assert_eq!(klassifiziere(Abi::SysV, &form, false), Ok(Uebergabe::Register(vec![Teil::F32x2, Teil::F32x2])));
        assert_eq!(klassifiziere(Abi::Aapcs64, &form, false), Ok(Uebergabe::Register(vec![Teil::Hfa { doppelt: false, n: 4 }])));
        assert_eq!(klassifiziere(Abi::Ms, &form, false), Ok(Uebergabe::Indirekt));
    }
}

