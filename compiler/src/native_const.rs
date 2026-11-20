//! P7-T4 (1A): typisierte native Konstanten und statische Layoutabfragen.
//!
//! - `@nativ konstante NAME: T = wert` legt ein konstantes Datenglobal mit
//!   exakt dem Ziel-ABI-Layout von T an (kein MooValue). T ist ein
//!   Integertyp (u8..u64, i8..i64), eine native `struktur` (auch `@gepackt`),
//!   ein festes Array `[T; N]` oder ein Zeiger `ptr[...]`.
//!   Werte: Integerliterale (exakt, bereichsgeprueft), seit P2-T48 auch f32/f64
//!   (Zielrundung) und bool (wahr/falsch), `Struktur(f1, f2, ...)`
//!   mit allen Feldern in Deklarationsreihenfolge, `[a, b, ...]` mit genau N
//!   Elementen, `nichts` (Nullzeiger) bzw. ein Symbolname als symbolische
//!   Linkadresse (Zeigerfeld oder Integer in Zeigerbreite).
//! - `groesse_von(T)` / `size_of(T)`, `ausrichtung_von(T)` / `align_of(T)`,
//!   `versatz_von(T, feld)` / `offset_of(T, feld)` werden in `@nativ`-Koerpern
//!   und nativen Konstanten zu Integerliteralen gefaltet (Ziel-TargetData).
//! - Skalare native Konstanten werden in `@nativ`-Koerpern als Literal
//!   eingesetzt, sofern kein Parameter oder Local gleichen Namens existiert.

use std::collections::{HashMap, HashSet};

use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::targets::TargetData;
use inkwell::types::BasicTypeEnum;
use inkwell::values::{BasicValueEnum, FunctionValue, PointerValue};

use crate::ast::{BinOp, Decorator, Expr, Program, Stmt, TypeKind, TypeRef, UnaryOpKind};
use crate::codegen_native::NType;
use crate::native_struct::NativeStructRegistry;

#[derive(Clone, Copy, PartialEq, Eq)]
enum Query { Size, Align, Offset }

fn query(name: &str) -> Option<Query> {
    match name {
        "groesse_von" | "größe_von" | "size_of" => Some(Query::Size),
        "ausrichtung_von" | "align_of" => Some(Query::Align),
        "versatz_von" | "offset_of" => Some(Query::Offset),
        _ => None,
    }
}

fn is_native(decorators: &[Decorator]) -> bool {
    decorators.iter().any(|d| d.name == "nativ" || d.name == "native")
}

fn integer_type(ty: &TypeRef) -> Option<NType> {
    match &ty.kind {
        TypeKind::Name(name) => NType::from_name(name)
            .filter(|t| !matches!(t, NType::F32 | NType::F64 | NType::Bool)),
        _ => None,
    }
}

fn number(value: i128) -> Expr {
    let magnitude = value.unsigned_abs();
    let literal = Expr::NativeNumber { legacy: magnitude as f64, text: magnitude.to_string(), line: 0, column: 0 };
    if value < 0 {
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand: Box::new(literal) }
    } else {
        literal
    }
}

/// P2-T48 B1: Float-/bool-Typ einer nativen Konstante (Skalar).
fn float_oder_bool(ty: &TypeRef) -> Option<NType> {
    match &ty.kind {
        TypeKind::Name(name) => NType::from_name(name)
            .filter(|t| matches!(t, NType::F32 | NType::F64 | NType::Bool)),
        _ => None,
    }
}

/// Text und Vorzeichen eines (ggf. negierten) Zahlliterals.
fn zahl_text(expr: &Expr) -> Result<(&str, bool), String> {
    match expr {
        Expr::NativeNumber { text, .. } => Ok((text, false)),
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
            Expr::NativeNumber { text, .. } => Ok((text, true)),
            _ => Err("Native Konstante: Zahlliteral erwartet".into()),
        },
        _ => Err("Native Konstante: Zahlliteral erwartet".into()),
    }
}

/// P2-T48 B1: Initialisierer einer f32/f64/bool-Konstante. Floats werden
/// direkt aus dem Literal auf die Zielbreite gerundet (NType::float_literal,
/// ADR §1); ein von Null verschiedenes Literal, das auf 0 rundet, ist ein Fehler.
fn float_bool_wert<'ctx>(context: &'ctx Context, expr: &Expr, ty: NType) -> Result<BasicValueEnum<'ctx>, String> {
    if ty == NType::Bool {
        return match expr {
            Expr::Boolean(flag) => Ok(context.bool_type().const_int(u64::from(*flag), false).into()),
            _ => Err("bool-Konstante erwartet wahr oder falsch".into()),
        };
    }
    let (text, negative) = zahl_text(expr)?;
    let (value, unterlauf) = ty.float_literal(context, text, negative)?;
    if unterlauf {
        return Err(format!("Floatliteral '{text}' rundet in {} auf 0", format!("{ty:?}").to_lowercase()));
    }
    Ok(value.value)
}

/// P2-T48 B1: skalare Float-/bool-Konstante als Ausdruck fuer @nativ-Koerper.
/// f32 wird einmal korrekt gerundet und als exakt darstellbares f64-Literal
/// (kuerzeste Rundreise-Darstellung) in als(f32, ...) eingesetzt; der Cast ist
/// damit exakt und es gibt keine Doppelrundung. f64 bleibt beim Quelltext.
fn konstanten_ausdruck(context: &Context, ty: &TypeRef, value: &Expr) -> Result<Option<Expr>, String> {
    let Some(scalar) = float_oder_bool(ty) else { return Ok(None) };
    float_bool_wert(context, value, scalar)?;
    if scalar == NType::Bool {
        return Ok(Some(value.clone()));
    }
    let (text, negative) = zahl_text(value)?;
    let text = if scalar == NType::F32 {
        let rund: f32 = text.parse().map_err(|_| format!("Ungueltiges Floatliteral '{text}'"))?;
        format!("{:?}", f64::from(rund))
    } else {
        text.to_string()
    };
    let literal = Expr::NativeNumber { legacy: text.parse().unwrap_or(0.0), text, line: 0, column: 0 };
    let literal = if negative {
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand: Box::new(literal) }
    } else {
        literal
    };
    let typ = if scalar == NType::F32 { "f32" } else { "f64" };
    Ok(Some(Expr::FunctionCall { type_args: Vec::new(), name: "als".into(), args: vec![Expr::Identifier(typ.into()), literal], zeile: 0, spalten: (0, 0) }))
}

/// Exakter Wert eines (ggf. negierten) Integerliterals fuer den Typ `ty`.
/// P2-T58 (Plan f40b5980 §3.1, Review 06d10060 #2): `a ** b` aus solchen
/// Werten wird mit derselben Regel wie der Lowerer gefaltet: b-fache
/// Multiplikation mit Wrap modulo 2^N, 0 ** 0 = 1, negativer Exponent
/// (signed) ist ein Compilefehler.
fn literal_value(context: &Context, expr: &Expr, ty: NType) -> Result<i128, String> {
    ganz_ausdruck(context, expr, ty, false)
}

fn ganz_grenzen(context: &Context, ty: NType) -> (u32, i128, i128) {
    let bits = ty.llvm_type(context).into_int_type().get_bit_width();
    if ty.is_signed_integer() {
        (bits, -(1i128 << (bits - 1)), (1i128 << (bits - 1)) - 1)
    } else {
        (bits, 0, (1i128 << bits) - 1)
    }
}

fn ganz_umlauf(context: &Context, wert: i128, ty: NType) -> i128 {
    let (bits, _, _) = ganz_grenzen(context, ty);
    let maske = (1i128 << bits) - 1;
    let r = wert & maske;
    if ty.is_signed_integer() && (r >> (bits - 1)) & 1 == 1 { r - (1i128 << bits) } else { r }
}

/// P2-T28 (Plan 607b20ba A1, Koordinator-O1 #38150): Ganzzahl-Konstantenausdruck
/// aus Literalen, groesse_von/ausrichtung_von/versatz_von (vorher gefaltet) und
/// + - * sowie unaerem Minus. Jedes Zwischenergebnis muss in den Zieltyp passen
/// (kein stiller Umlauf, wie in @nativ-Koerpern); modulo 2^N nur innerhalb von
/// `als_wrap(T, ausdruck)` (z. B. Multiboot2-Pruefsumme).
fn ganz_ausdruck(context: &Context, expr: &Expr, ty: NType, umlauf: bool) -> Result<i128, String> {
    let pruefe = |wert: Option<i128>| -> Result<i128, String> {
        let wert = wert.ok_or_else(|| "Native Konstante: Ganzzahlueberlauf im Konstantenausdruck".to_string())?;
        let (_, min, max) = ganz_grenzen(context, ty);
        if !umlauf && (wert < min || wert > max) {
            return Err(format!(
                "Native Konstante: Ergebnis {wert} passt nicht in {ty:?}; Umlauf nur ausdruecklich mit als_wrap({}, ...)",
                format!("{ty:?}").to_lowercase()
            ));
        }
        Ok(wert)
    };
    match expr {
        Expr::BinaryOp { op: op @ (BinOp::Add | BinOp::Sub | BinOp::Mul), left, right } => {
            let l = ganz_ausdruck(context, left, ty, umlauf)?;
            let r = ganz_ausdruck(context, right, ty, umlauf)?;
            if umlauf {
                // Review acbff6e0: innerhalb als_wrap jedes Zwischenergebnis modulo
                // 2^N (Operanden <= 64 Bit; i128-wrapping ist modulo 2^128 und damit
                // kongruent modulo 2^N), z. B. als_wrap(u64, MAX * MAX) = 1.
                let (l, r) = (ganz_umlauf(context, l, ty), ganz_umlauf(context, r, ty));
                let roh = match op {
                    BinOp::Add => l.wrapping_add(r),
                    BinOp::Sub => l.wrapping_sub(r),
                    _ => l.wrapping_mul(r),
                };
                return Ok(ganz_umlauf(context, roh, ty));
            }
            pruefe(match op {
                BinOp::Add => l.checked_add(r),
                BinOp::Sub => l.checked_sub(r),
                _ => l.checked_mul(r),
            })
        }
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } if !matches!(operand.as_ref(), Expr::NativeNumber { .. }) => {
            let v = ganz_ausdruck(context, operand, ty, umlauf)?;
            if umlauf {
                return Ok(ganz_umlauf(context, ganz_umlauf(context, v, ty).wrapping_neg(), ty));
            }
            pruefe(v.checked_neg())
        }
        Expr::FunctionCall { name, args, .. } if name == "als_wrap" => {
            let [Expr::Identifier(ziel), inner] = args.as_slice() else {
                return Err("als_wrap(T, ausdruck) erwartet einen Ganzzahltyp und einen Ausdruck".into());
            };
            let ziel_ty = NType::from_name(ziel)
                .filter(|t| t.is_signed_integer() || matches!(t, NType::U8 | NType::U16 | NType::U32 | NType::U64))
                .ok_or_else(|| format!("als_wrap: '{ziel}' ist kein Ganzzahltyp"))?;
            let wert = ganz_ausdruck(context, inner, ziel_ty, true)?;
            let gekuerzt = ganz_umlauf(context, wert, ziel_ty);
            pruefe(Some(gekuerzt))
        }
        _ => {
            // Review 4571f610: innerhalb als_wrap gilt ein Literal exakt (auch
            // ausserhalb des Zieltyps, z. B. als_wrap(u8, 256)); gekuerzt wird
            // erst am Ende von als_wrap.
            if umlauf {
                if let Some(wert) = literal_roh(expr) { return wert; }
            }
            literal_kern(context, expr, ty)
        }
    }
}

/// Exakter Wert eines (ggf. negierten) Ganzzahlliterals ohne Bereichspruefung
/// (nur im Umlaufmodus von als_wrap). None = kein Ganzzahlliteral.
fn literal_roh(expr: &Expr) -> Option<Result<i128, String>> {
    let (text, negativ) = match expr {
        Expr::NativeNumber { text, .. } => (text, false),
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
            Expr::NativeNumber { text, .. } => (text, true),
            _ => return None,
        },
        _ => return None,
    };
    let (ziffern, basis) = if let Some(s) = text.strip_prefix("0x").or_else(|| text.strip_prefix("0X")) {
        (s, 16)
    } else if let Some(s) = text.strip_prefix("0b").or_else(|| text.strip_prefix("0B")) {
        (s, 2)
    } else if let Some(s) = text.strip_prefix("0o").or_else(|| text.strip_prefix("0O")) {
        (s, 8)
    } else {
        (text.as_str(), 10)
    };
    if ziffern.is_empty() || !ziffern.chars().all(|c| c.is_digit(basis)) {
        return None;
    }
    Some(u128::from_str_radix(ziffern, basis).ok()
        .and_then(|v| i128::try_from(v).ok())
        .map(|v| if negativ { -v } else { v })
        .ok_or_else(|| format!("als_wrap: Literal '{text}' ist zu gross")))
}

fn literal_kern(context: &Context, expr: &Expr, ty: NType) -> Result<i128, String> {
    if let Expr::BinaryOp { op: BinOp::Pow, left, right } = expr {
        let basis = literal_value(context, left, ty)?;
        let exponent = literal_value(context, right, ty)?;
        if exponent < 0 {
            return Err("Native Potenz mit konstant negativem Exponenten".into());
        }
        let bits = ty.llvm_type(context).into_int_type().get_bit_width();
        let maske = if bits >= 64 { u64::MAX } else { (1u64 << bits) - 1 };
        // Quadrieren und Multiplizieren modulo 2^64 (Zweierkomplement-Bitmuster),
        // danach auf N Bit gekuerzt.
        let (mut b, mut e, mut r) = (basis as u64, exponent as u64, 1u64);
        while e != 0 {
            if e & 1 == 1 { r = r.wrapping_mul(b); }
            b = b.wrapping_mul(b);
            e >>= 1;
        }
        let r = r & maske;
        let negativ = ty.is_signed_integer() && (r >> (bits - 1)) & 1 == 1;
        return Ok(if negativ { i128::from(r) - (1i128 << bits) } else { i128::from(r) });
    }
    let (text, negative) = match expr {
        Expr::NativeNumber { text, .. } => (text, false),
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
            Expr::NativeNumber { text, .. } => (text, true),
            _ => return Err("Native Konstante: Integerliteral erwartet".into()),
        },
        _ => return Err("Native Konstante: Integerliteral erwartet".into()),
    };
    let value = ty.integer_literal(context, text, negative)?.value.into_int_value();
    if ty.is_signed_integer() {
        value.get_sign_extended_constant().map(i128::from)
    } else {
        value.get_zero_extended_constant().map(i128::from)
    }.ok_or_else(|| "Native Konstante: Literal nicht konstant".to_string())
}

pub(crate) struct Folder<'a, 'ctx> {
    context: &'ctx Context,
    target: &'a TargetData,
    registry: &'a NativeStructRegistry<'ctx>,
    scalars: HashMap<String, i128>,
    /// P2-T48 B1: skalare Float-/bool-Konstanten als typisierter Ausdruck
    /// (als(T, Literal) bzw. wahr/falsch) fuer @nativ-Koerper.
    ausdruecke: HashMap<String, Expr>,
}

impl<'a, 'ctx> Folder<'a, 'ctx> {
    fn type_layout(&self, name: &str) -> Result<(u64, u64), String> {
        if let Some(scalar) = NType::from_name(name) {
            let llvm = scalar.llvm_type(self.context);
            return Ok((self.target.get_abi_size(&llvm), u64::from(self.target.get_abi_alignment(&llvm))));
        }
        let layout = self.registry.layout(name)
            .ok_or_else(|| format!("Layoutabfrage: unbekannter Typ '{name}'"))?;
        Ok((layout.size, u64::from(layout.alignment)))
    }

    fn query(&self, kind: Query, name: &str, args: &[Expr]) -> Result<u64, String> {
        match (kind, args) {
            (Query::Size, [Expr::Identifier(ty)]) => Ok(self.type_layout(ty)?.0),
            (Query::Align, [Expr::Identifier(ty)]) => Ok(self.type_layout(ty)?.1),
            (Query::Offset, [Expr::Identifier(ty), Expr::Identifier(field)]) => {
                let layout = self.registry.layout(ty)
                    .ok_or_else(|| format!("{name}: '{ty}' ist keine native Struktur"))?;
                if !layout.bitfelder.is_empty() {
                    return Err(format!("{name}: '{ty}.{field}' ist ein Bitbereich ohne eigene Byteadresse"));
                }
                let index = layout.fields.iter().position(|(candidate, _)| candidate == field)
                    .ok_or_else(|| format!("{name}: Struktur '{ty}' hat kein Feld '{field}'"))?;
                layout.offsets.get(index).copied()
                    .ok_or_else(|| format!("{name}: Offset von '{ty}.{field}' fehlt"))
            }
            (Query::Offset, _) => Err(format!("{name} erwartet (Strukturname, Feldname)")),
            _ => Err(format!("{name} erwartet genau einen Typnamen")),
        }
    }

    fn expr(&self, expr: &Expr, shadow: &HashSet<String>) -> Result<Expr, String> {
        let fold = |e: &Expr| self.expr(e, shadow);
        let boxed = |e: &Expr| self.expr(e, shadow).map(Box::new);
        Ok(match expr {
            // P7-T5: adresse_von(NAME) nimmt die Adresse, nicht den gefalteten Wert.
            Expr::FunctionCall { name, .. } if name == "adresse_von" || name == "address_of" => expr.clone(),
            Expr::FunctionCall { name, args, zeile, spalten, type_args } => match query(name) {
                Some(kind) => number(i128::from(self.query(kind, name, args)?)),
                // Die Quellzeile bleibt erhalten (Diagnosen in nativen Koerpern).
                None => Expr::FunctionCall { name: name.clone(), type_args: type_args.clone(), args: args.iter().map(fold).collect::<Result<_, _>>()?, zeile: *zeile, spalten: *spalten },
            },
            Expr::Identifier(name) if !shadow.contains(name) => match self.scalars.get(name) {
                Some(value) => number(*value),
                None => self.ausdruecke.get(name).cloned().unwrap_or_else(|| expr.clone()),
            },
            Expr::BinaryOp { left, op, right } =>
                Expr::BinaryOp { left: boxed(left)?, op: op.clone(), right: boxed(right)? },
            Expr::UnaryOp { op, operand } => Expr::UnaryOp { op: op.clone(), operand: boxed(operand)? },
            Expr::List(items) => Expr::List(items.iter().map(fold).collect::<Result<_, _>>()?),
            Expr::IndexAccess { object, index } => Expr::IndexAccess { object: boxed(object)?, index: boxed(index)? },
            Expr::MethodCall { object, method, args } => Expr::MethodCall {
                object: boxed(object)?, method: method.clone(),
                args: args.iter().map(fold).collect::<Result<_, _>>()?,
            },
            Expr::Ternary { condition, then_val, else_val } => Expr::Ternary {
                condition: boxed(condition)?, then_val: boxed(then_val)?, else_val: boxed(else_val)?,
            },
            _ => expr.clone(),
        })
    }

    fn stmts(&self, body: &[Stmt], shadow: &HashSet<String>) -> Result<Vec<Stmt>, String> {
        body.iter().map(|stmt| Ok(match stmt {
            Stmt::Return(Some(value), zeile) => Stmt::Return(Some(self.expr(value, shadow)?), *zeile),
            Stmt::Expression(value, zeile) => Stmt::Expression(self.expr(value, shadow)?, *zeile),
            Stmt::UnsafeBlock { zeile, body } => Stmt::UnsafeBlock { zeile: *zeile, body: self.stmts(body, shadow)? },
            Stmt::If { zeile, condition, body, else_body } => Stmt::If {
                zeile: *zeile,
                condition: self.expr(condition, shadow)?,
                body: self.stmts(body, shadow)?,
                else_body: self.stmts(else_body, shadow)?,
            },
            Stmt::While { zeile, condition, body } => Stmt::While {
                zeile: *zeile, condition: self.expr(condition, shadow)?, body: self.stmts(body, shadow)?,
            },
            Stmt::Assignment { zeile, name, ty, value } => Stmt::Assignment {
                zeile: *zeile, name: name.clone(), ty: ty.clone(), value: self.expr(value, shadow)?,
            },
            Stmt::ConstAssignment { zeile, name, ty, value, geteilt } => Stmt::ConstAssignment {
                zeile: *zeile, name: name.clone(), ty: ty.clone(), value: self.expr(value, shadow)?,
                geteilt: *geteilt,
            },
            // P2-T48: auch Ziel und Wert von p[i] = …, p.feld = …, x += … und zeige
            // falten; sonst bleibt z. B. `q[0] = KONST` ein unbekannter Bezeichner.
            Stmt::CompoundAssignment { zeile, name, op, value } => Stmt::CompoundAssignment {
                zeile: *zeile, name: name.clone(), op: op.clone(), value: self.expr(value, shadow)?,
            },
            Stmt::PropertyAssignment { zeile, object, property, value } => Stmt::PropertyAssignment {
                zeile: *zeile, object: self.expr(object, shadow)?, property: property.clone(), value: self.expr(value, shadow)?,
            },
            Stmt::IndexAssignment { zeile, object, index, value } => Stmt::IndexAssignment {
                zeile: *zeile, object: self.expr(object, shadow)?, index: self.expr(index, shadow)?, value: self.expr(value, shadow)?,
            },
            Stmt::Show(value, zeile) => Stmt::Show(self.expr(value, shadow)?, *zeile),
            other => other.clone(),
        })).collect()
    }
}

fn collect_locals(body: &[Stmt], names: &mut HashSet<String>) {
    for stmt in body {
        match stmt {
            Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. } => { names.insert(name.clone()); }
            Stmt::UnsafeBlock { body, .. } | Stmt::While { body, .. } => collect_locals(body, names),
            Stmt::If { body, else_body, .. } => { collect_locals(body, names); collect_locals(else_body, names); }
            _ => {}
        }
    }
}

/// P7-T5: `[u8; _]` bzw. `[u16; _]` mit Textliteral: Laenge = Einheiten + 1
/// (abschliessende 0), damit Texte nicht von Hand abgezaehlt werden muessen.
fn text_length(ty: &TypeRef, value: &Expr) -> TypeRef {
    if let (TypeKind::Array { element, length }, Expr::String(text)) = (&ty.kind, value) {
        if matches!(length.as_ref(), Expr::Identifier(n) if n == "_") {
            let units = match &element.kind {
                TypeKind::Name(n) if n == "u8" => Some(text.len()),
                TypeKind::Name(n) if n == "u16" => Some(text.encode_utf16().count()),
                _ => None,
            };
            if let Some(units) = units {
                let n = units + 1;
                let mut out = ty.clone();
                out.kind = TypeKind::Array {
                    element: element.clone(),
                    length: Box::new(Expr::NativeNumber { legacy: n as f64, text: n.to_string(), line: ty.line, column: ty.column }),
                };
                return out;
            }
        }
    }
    ty.clone()
}

/// P7-T6: `@nativ statisch` (Parser setzt das interne Decorator "statisch").
fn decorators_statisch(stmt: &Stmt) -> bool {
    matches!(stmt, Stmt::NativeConst { decorators, .. } if decorators.iter().any(|d| d.name == "statisch"))
}

/// Faltet Layoutabfragen und skalare native Konstanten in `@nativ`-Koerpern
/// und nativen Konstanten. Dynamische Moo-Funktionen bleiben unveraendert.
pub(crate) fn fold_program<'ctx>(
    program: &Program,
    context: &'ctx Context,
    target: Option<&TargetData>,
    registry: &NativeStructRegistry<'ctx>,
) -> Result<Program, String> {
    let has_native = program.statements.iter().any(|stmt| match stmt {
        Stmt::NativeConst { .. } => true,
        Stmt::FunctionDef { decorators, .. } => is_native(decorators),
        _ => false,
    });
    let target = match target {
        Some(target) if has_native => target,
        _ => return Ok(program.clone()),
    };
    let mut folder = Folder { context, target, registry, scalars: HashMap::new(), ausdruecke: HashMap::new() };
    let empty = HashSet::new();
    let mut names = HashSet::new();
    for stmt in &program.statements {
        if let Stmt::NativeConst { name, ty, value, .. } = stmt {
            if !names.insert(name.clone()) {
                return Err(format!("Native Konstante '{name}' ist doppelt definiert"));
            }
            let Some(value) = value else { continue; };
            if decorators_statisch(stmt) {
                // P7-T6: beschreibbar, also nie falten; Zugriff ueber adresse_von.
                if !matches!(stmt, Stmt::NativeConst { decorators, .. } if decorators.iter().any(|d| d.name == "pro_kern"))
                    && (integer_type(ty).is_some() || float_oder_bool(ty).is_some()) {
                    return Err(format!("Statischer Speicher '{name}': braucht einen Array- oder Strukturtyp (Zugriff ueber adresse_von/speicher_*)"));
                }
                continue;
            }
            if let Some(scalar) = integer_type(ty) {
                let folded = folder.expr(value, &empty)
                    .map_err(|e| format!("Native Konstante '{name}': {e}"))?;
                // Symbolische Linkadressen sind keine Compilezeitwerte.
                if matches!(folded, Expr::Identifier(_)) { continue; }
                let value = literal_value(context, &folded, scalar)
                    .map_err(|e| format!("Native Konstante '{name}': {e}"))?;
                folder.scalars.insert(name.clone(), value);
            } else if let Some(ausdruck) = konstanten_ausdruck(context, ty, value)
                .map_err(|e| format!("Native Konstante '{name}': {e}"))?
            {
                folder.ausdruecke.insert(name.clone(), ausdruck);
            }
        }
    }
    let statements = program.statements.iter().map(|stmt| Ok(match stmt {
        Stmt::FunctionDef { zeile, name, params, param_types, return_type, defaults, body, decorators }
            if is_native(decorators) =>
        {
            let mut shadow: HashSet<String> = params.iter().cloned().collect();
            collect_locals(body, &mut shadow);
            Stmt::FunctionDef {
                zeile: *zeile,
                name: name.clone(), params: params.clone(), param_types: param_types.clone(),
                return_type: return_type.clone(), defaults: defaults.clone(),
                body: folder.stmts(body, &shadow).map_err(|e| format!("Native Funktion '{name}': {e}"))?,
                decorators: decorators.clone(),
            }
        }
        Stmt::NativeConst { zeile, name, ty, value: Some(value), decorators } => Stmt::NativeConst {
            zeile: *zeile,
            name: name.clone(), ty: text_length(ty, value),
            value: Some(folder.expr(value, &empty).map_err(|e| format!("Native Konstante '{name}': {e}"))?),
            decorators: decorators.clone(),
        },
        other => other.clone(),
    })).collect::<Result<Vec<_>, String>>()?;
    Ok(Program { statements, funktion_quellen: Default::default() })
}

struct Emitter<'a, 'ctx> {
    context: &'ctx Context,
    module: &'a Module<'ctx>,
    target: &'a TargetData,
    registry: &'a NativeStructRegistry<'ctx>,
    native_functions: &'a HashMap<String, FunctionValue<'ctx>>,
}

impl<'a, 'ctx> Emitter<'a, 'ctx> {
    /// Adresse eines Symbols: native/dynamische Funktion oder native Konstante.
    fn symbol(&self, name: &str) -> Option<(PointerValue<'ctx>, bool)> {
        if let Some(function) = self.native_functions.get(name) {
            return Some((function.as_global_value().as_pointer_value(), true));
        }
        if let Some(function) = self.module.get_function(name) {
            return Some((function.as_global_value().as_pointer_value(), true));
        }
        self.module.get_global(name).map(|global| (global.as_pointer_value(), false))
    }

    fn value(&self, ty: &TypeRef, expr: &Expr) -> Result<BasicValueEnum<'ctx>, String> {
        if matches!(&ty.kind, TypeKind::Generic { name, .. } if crate::native_type_id::ist_atom_typ(name)) {
            return self.value(crate::native_type_id::atom_element(ty)?, expr);
        }
        let llvm = self.registry.field_type(self.context, self.target, ty)?;
        // P2-T48 B1: f32/f64/bool als Skalar, Array- oder Strukturfeld.
        if let Some(scalar) = float_oder_bool(ty) {
            return float_bool_wert(self.context, expr, scalar);
        }
        if let Some(scalar) = integer_type(ty) {
            if let Expr::Identifier(name) = expr {
                // Symbolische Linkadresse in einem Integer exakt in Zeigerbreite.
                let (pointer, _) = self.symbol(name)
                    .ok_or_else(|| format!("Unbekanntes Symbol '{name}'"))?;
                let width = llvm.into_int_type().get_bit_width();
                if scalar.is_signed_integer() || u64::from(width) != u64::from(self.target.get_pointer_byte_size(None)) * 8 {
                    return Err(format!("Linkadresse '{name}' braucht einen vorzeichenlosen Integer in Zeigerbreite"));
                }
                return Ok(pointer.const_to_int(llvm.into_int_type()).into());
            }
            let value = literal_value(self.context, expr, scalar)?;
            return Ok(llvm.into_int_type().const_int(value as u64, false).into());
        }
        match (&ty.kind, expr) {
            (TypeKind::Name(name), Expr::FunctionCall { name: ctor, args, .. }) if name == ctor => {
                let layout = self.registry.layout(name)
                    .ok_or_else(|| format!("'{name}' ist keine native Struktur"))?;
                if args.is_empty() {
                    return Ok(layout.llvm_type.const_zero().into());
                }
                if !layout.bitfelder.is_empty() {
                    if args.len() != layout.bitfelder.len() {
                        return Err(format!("Bitfeld '{name}' erwartet {} Bereiche, bekommen {}", layout.bitfelder.len(), args.len()));
                    }
                    let basis = integer_type(&layout.fields[0].1).ok_or("Bitfeld-Basistyp fehlt")?;
                    let mut wort = 0u64;
                    for (feld, arg) in layout.bitfelder.iter().zip(args) {
                        let wert = literal_value(self.context, arg, basis)? as u64;
                        if wert > feld.maximum() {
                            return Err(format!("Bitfeld '{name}.{}': Wert {wert} passt nicht in {} Bit", feld.name, feld.breite));
                        }
                        wort |= wert << feld.anfang;
                    }
                    let wert = layout.llvm_type.get_field_types()[0].into_int_type().const_int(wort, false);
                    let mut values = vec![wert.into()];
                    values.extend(layout.llvm_type.get_field_types().iter().skip(1).map(|t| t.const_zero()));
                    return Ok(layout.llvm_type.const_named_struct(&values).into());
                }
                if args.len() != layout.fields.len() {
                    return Err(format!("Struktur '{name}' erwartet {} Felder, bekommen {}", layout.fields.len(), args.len()));
                }
                let mut values = layout.fields.iter().zip(args)
                    .map(|((field, field_ty), arg)| self.value(field_ty, arg)
                        .map_err(|e| format!("{name}.{field}: {e}")))
                    .collect::<Result<Vec<_>, _>>()?;
                // P2-T13 C: das leere Ausrichtungselement von @ausrichten(n) bekommt den Nullwert.
                for extra in layout.llvm_type.get_field_types().into_iter().skip(values.len()) {
                    values.push(extra.const_zero());
                }
                Ok(layout.llvm_type.const_named_struct(&values).into())
            }
            (TypeKind::Name(name), _) if self.registry.layout(name).is_some() =>
                Err(format!("Struktur '{name}' erwartet einen Wert der Form {name}(feld1, ...)")),
            (TypeKind::Array { element, .. }, Expr::List(items)) => {
                let array = llvm.into_array_type();
                if items.len() != array.len() as usize {
                    return Err(format!("Array erwartet {} Elemente, bekommen {}", array.len(), items.len()));
                }
                let values = items.iter().map(|item| self.value(element, item)).collect::<Result<Vec<_>, _>>()?;
                const_array(array.get_element_type(), &values)
            }
            // P7-T5: Textliteral fuer [u8; N] (UTF-8-Bytes) bzw. [u16; N] (UTF-16,
            // z. B. UEFI-Pfade); N = Laenge oder Laenge + 1 (abschliessende 0).
            (TypeKind::Array { element, .. }, Expr::String(text)) => {
                let array = llvm.into_array_type();
                let units: Vec<u64> = match &element.kind {
                    TypeKind::Name(n) if n == "u8" => text.bytes().map(u64::from).collect(),
                    TypeKind::Name(n) if n == "u16" => text.encode_utf16().map(u64::from).collect(),
                    _ => return Err("Textliteral nur fuer [u8; N] (UTF-8) oder [u16; N] (UTF-16)".into()),
                };
                let n = array.len() as usize;
                if units.len() != n && units.len() + 1 != n {
                    return Err(format!("Textliteral hat {} Einheiten, das Array {n} (gleich oder +1 fuer die abschliessende 0)", units.len()));
                }
                let int = array.get_element_type().into_int_type();
                let mut values: Vec<_> = units.iter().map(|unit| int.const_int(*unit, false)).collect();
                values.resize(n, int.const_zero());
                Ok(int.const_array(&values).into())
            }
            (TypeKind::Array { .. }, _) => Err("Array erwartet ein Listenliteral [..] oder ein Textliteral".into()),
            (TypeKind::Generic { name, args }, _) if (name == "ptr" || name == "zeiger") && args.len() == 1 => {
                let pointer_type = llvm.into_pointer_type();
                match expr {
                    Expr::None => Ok(pointer_type.const_null().into()),
                    Expr::Identifier(symbol) => {
                        let (pointer, is_function) = self.symbol(symbol)
                            .ok_or_else(|| format!("Unbekanntes Symbol '{symbol}'"))?;
                        let wants_function = matches!(args[0].kind, TypeKind::Function { .. });
                        let byte_address = matches!(&args[0].kind, TypeKind::Name(n) if n == "u8" || n == "void");
                        if wants_function != is_function && !byte_address {
                            return Err(format!("Symbol '{symbol}' passt nicht zum Zeigerziel (Funktion/Daten)"));
                        }
                        Ok(pointer.into())
                    }
                    _ => Err("Zeiger erwartet nichts oder einen Symbolnamen".into()),
                }
            }
            _ => Err("Typ fuer native Konstanten nicht unterstuetzt".into()),
        }
    }
}

fn const_array<'ctx>(element: BasicTypeEnum<'ctx>, values: &[BasicValueEnum<'ctx>]) -> Result<BasicValueEnum<'ctx>, String> {
    Ok(match element {
        BasicTypeEnum::IntType(t) => t.const_array(&values.iter().map(|v| v.into_int_value()).collect::<Vec<_>>()).into(),
        BasicTypeEnum::FloatType(t) => t.const_array(&values.iter().map(|v| v.into_float_value()).collect::<Vec<_>>()).into(),
        BasicTypeEnum::StructType(t) => t.const_array(&values.iter().map(|v| v.into_struct_value()).collect::<Vec<_>>()).into(),
        BasicTypeEnum::ArrayType(t) => t.const_array(&values.iter().map(|v| v.into_array_value()).collect::<Vec<_>>()).into(),
        BasicTypeEnum::PointerType(t) => t.const_array(&values.iter().map(|v| v.into_pointer_value()).collect::<Vec<_>>()).into(),
        _ => return Err("Arrayelementtyp fuer native Konstanten nicht unterstuetzt".into()),
    })
}

/// Legt native Datensymbole vor den Initialisierern an, damit sie einander
/// referenzieren koennen. Externe C-Konstanten bleiben reine Deklarationen.
pub(crate) fn emit_constants<'ctx>(
    program: &Program,
    context: &'ctx Context,
    module: &Module<'ctx>,
    target: Option<&TargetData>,
    registry: &NativeStructRegistry<'ctx>,
    native_functions: &HashMap<String, FunctionValue<'ctx>>,
) -> Result<(), String> {
    let constants: Vec<_> = program.statements.iter()
        .filter_map(|stmt| match stmt {
            Stmt::NativeConst { name, ty, value, decorators, .. } => Some((name, ty, value.as_ref(), decorators)),
            _ => None,
        })
        .collect();
    if constants.is_empty() {
        return Ok(());
    }
    let target = target.ok_or("Ziel-Datenlayout fuer native Konstanten fehlt")?;
    let emitter = Emitter { context, module, target, registry, native_functions };
    let mut globals = Vec::with_capacity(constants.len());
    let mut pro_kern = Vec::new();
    let triple = module.get_triple().as_str().to_string_lossy().into_owned();
    let bare = triple.ends_with("-none") || triple.ends_with("-uefi");
    for (name, ty, value, decorators) in &constants {
        for decorator in decorators.iter() {
            let known = if value.is_none() {
                matches!(decorator.name.as_str(), "bibliothek" | "library")
            } else {
                // Layout-Decorators (2A) wertet codegen::apply_native_layout_decorators aus.
                (matches!(decorator.name.as_str(), "nativ" | "native" | "statisch" | "pro_kern") && decorator.args.is_empty())
                    || matches!(decorator.name.as_str(), "sektion" | "section" | "ausrichten" | "align" | "benutzt" | "used")
            };
            if !known {
                return Err(format!("Native Konstante '{name}': Decorator @{} nicht unterstuetzt", decorator.name));
            }
        }
        if module.get_global(name).is_some() || module.get_function(name).is_some()
            || native_functions.contains_key(name.as_str())
        {
            return Err(format!("Native Konstante '{name}': Symbolkollision"));
        }
        let llvm = registry.field_type(context, target, ty)
            .map_err(|e| format!("Native Konstante '{name}': {e}"))?;
        let statisch = decorators.iter().any(|d| d.name == "statisch");
        if decorators.iter().any(|d| d.name == "pro_kern") {
            if !statisch || value.is_none() || decorators.iter().filter(|d| d.name == "pro_kern").count() != 1 {
                return Err(format!("@pro_kern '{name}' verlangt genau einen argumentlosen Decorator an statisch"));
            }
            if !module.get_triple().as_str().to_string_lossy().starts_with("x86_64") {
                return Err("@pro_kern ist derzeit nur fuer x86_64 implementiert".into());
            }
            if decorators.iter().any(|d| matches!(d.name.as_str(), "sektion" | "section" | "ausrichten" | "align")) {
                return Err(format!("@pro_kern '{name}': Sektion und Ausrichtung ergeben sich aus dem gemeinsamen CPU-lokalen ABI-Layout"));
            }
            pro_kern.push((llvm, *ty, value.unwrap(), *name));
            globals.push(None);
            continue;
        }
        let global = module.add_global(llvm, None, name);
        // P7-T6: @nativ statisch ist beschreibbar (.data/.bss), Konstanten nicht.
        global.set_constant(!statisch);
        if value.is_none() {
            global.set_alignment(target.get_abi_alignment(&llvm));
        }
        globals.push(Some((global, llvm, statisch)));
    }
    for ((name, ty, value, _), global) in constants.iter().zip(globals) {
        let Some((global, llvm, statisch)) = global else { continue; };
        let Some(value) = value else { continue; };
        // P7-T6: `auf 0` nullt statischen Speicher jeden Typs (landet in .bss).
        let null = statisch && match value {
            Expr::NativeNumber { text, .. } => text.trim_start_matches("0x").trim_start_matches('0').is_empty(),
            Expr::Number(n) => *n == 0.0,
            _ => false,
        };
        if null {
            global.set_initializer(&llvm.const_zero());
            continue;
        }
        let init = emitter.value(ty, value).map_err(|e| format!("Native Konstante '{name}': {e}"))?;
        global.set_initializer(&init);
    }
    if !pro_kern.is_empty() {
        let types: Vec<_> = pro_kern.iter().map(|(llvm, ..)| *llvm).collect();
        let layout = context.struct_type(&types, false);
        let values = pro_kern.iter().map(|(llvm, ty, value, name)| {
            let zero = matches!(value, Expr::Number(n) if *n == 0.0)
                || matches!(value, Expr::NativeNumber { text, .. } if text == "0");
            if zero { Ok(llvm.const_zero()) }
            else { emitter.value(ty, value).map_err(|e| format!("@pro_kern '{name}': {e}")) }
        }).collect::<Result<Vec<_>, String>>()?;
        if module.get_global("__moo_prokern_vorlage").is_some() {
            return Err("Reserviertes Symbol __moo_prokern_vorlage ist bereits belegt".into());
        }
        // Ein ABI-Aggregat haelt Reihenfolge, Padding und alle Offsets zusammen.
        // Keine Annahme ueber die Reihenfolge einzelner Linker-Inputsektionen.
        let template = module.add_global(layout, None, "__moo_prokern_vorlage");
        template.set_initializer(&layout.const_named_struct(&values));
        template.set_alignment(target.get_abi_alignment(&layout));
        if bare { template.set_section(Some(".moo_prokern")); }
        else { template.set_thread_local_mode(Some(inkwell::ThreadLocalMode::GeneralDynamicTLSModel)); }
    }
    Ok(())
}
