//! P7-T4 (1A): typisierte native Konstanten und statische Layoutabfragen.
//!
//! - `@nativ konstante NAME: T = wert` legt ein konstantes Datenglobal mit
//!   exakt dem Ziel-ABI-Layout von T an (kein MooValue). T ist ein
//!   Integertyp (u8..u64, i8..i64), eine native `struktur` (auch `@gepackt`),
//!   ein festes Array `[T; N]` oder ein Zeiger `ptr[...]`.
//!   Werte: Integerliterale (exakt, bereichsgeprueft), `Struktur(f1, f2, ...)`
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

use crate::ast::{Decorator, Expr, Program, Stmt, TypeKind, TypeRef, UnaryOpKind};
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

/// Exakter Wert eines (ggf. negierten) Integerliterals fuer den Typ `ty`.
fn literal_value(context: &Context, expr: &Expr, ty: NType) -> Result<i128, String> {
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
            Expr::FunctionCall { name, args } => match query(name) {
                Some(kind) => number(i128::from(self.query(kind, name, args)?)),
                None => Expr::FunctionCall { name: name.clone(), args: args.iter().map(fold).collect::<Result<_, _>>()? },
            },
            Expr::Identifier(name) if !shadow.contains(name) => match self.scalars.get(name) {
                Some(value) => number(*value),
                None => expr.clone(),
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
            Stmt::Return(Some(value)) => Stmt::Return(Some(self.expr(value, shadow)?)),
            Stmt::Expression(value) => Stmt::Expression(self.expr(value, shadow)?),
            Stmt::UnsafeBlock { body } => Stmt::UnsafeBlock { body: self.stmts(body, shadow)? },
            Stmt::If { condition, body, else_body } => Stmt::If {
                condition: self.expr(condition, shadow)?,
                body: self.stmts(body, shadow)?,
                else_body: self.stmts(else_body, shadow)?,
            },
            Stmt::While { condition, body } => Stmt::While {
                condition: self.expr(condition, shadow)?, body: self.stmts(body, shadow)?,
            },
            Stmt::Assignment { name, ty, value } => Stmt::Assignment {
                name: name.clone(), ty: ty.clone(), value: self.expr(value, shadow)?,
            },
            Stmt::ConstAssignment { name, ty, value } => Stmt::ConstAssignment {
                name: name.clone(), ty: ty.clone(), value: self.expr(value, shadow)?,
            },
            other => other.clone(),
        })).collect()
    }
}

fn collect_locals(body: &[Stmt], names: &mut HashSet<String>) {
    for stmt in body {
        match stmt {
            Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. } => { names.insert(name.clone()); }
            Stmt::UnsafeBlock { body } | Stmt::While { body, .. } => collect_locals(body, names),
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
    let mut folder = Folder { context, target, registry, scalars: HashMap::new() };
    let empty = HashSet::new();
    let mut names = HashSet::new();
    for stmt in &program.statements {
        if let Stmt::NativeConst { name, ty, value, .. } = stmt {
            if !names.insert(name.clone()) {
                return Err(format!("Native Konstante '{name}' ist doppelt definiert"));
            }
            if let Some(scalar) = integer_type(ty) {
                let folded = folder.expr(value, &empty)
                    .map_err(|e| format!("Native Konstante '{name}': {e}"))?;
                // Symbolische Linkadressen sind keine Compilezeitwerte.
                if matches!(folded, Expr::Identifier(_)) { continue; }
                let value = literal_value(context, &folded, scalar)
                    .map_err(|e| format!("Native Konstante '{name}': {e}"))?;
                folder.scalars.insert(name.clone(), value);
            }
        }
    }
    let statements = program.statements.iter().map(|stmt| Ok(match stmt {
        Stmt::FunctionDef { name, params, param_types, return_type, defaults, body, decorators }
            if is_native(decorators) =>
        {
            let mut shadow: HashSet<String> = params.iter().cloned().collect();
            collect_locals(body, &mut shadow);
            Stmt::FunctionDef {
                name: name.clone(), params: params.clone(), param_types: param_types.clone(),
                return_type: return_type.clone(), defaults: defaults.clone(),
                body: folder.stmts(body, &shadow).map_err(|e| format!("Native Funktion '{name}': {e}"))?,
                decorators: decorators.clone(),
            }
        }
        Stmt::NativeConst { name, ty, value, decorators } => Stmt::NativeConst {
            name: name.clone(), ty: text_length(ty, value),
            value: folder.expr(value, &empty).map_err(|e| format!("Native Konstante '{name}': {e}"))?,
            decorators: decorators.clone(),
        },
        other => other.clone(),
    })).collect::<Result<Vec<_>, String>>()?;
    Ok(Program { statements })
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
        let llvm = self.registry.field_type(self.context, self.target, ty)?;
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
            (TypeKind::Name(name), Expr::FunctionCall { name: ctor, args }) if name == ctor => {
                let layout = self.registry.layout(name)
                    .ok_or_else(|| format!("'{name}' ist keine native Struktur"))?;
                if args.len() != layout.fields.len() {
                    return Err(format!("Struktur '{name}' erwartet {} Felder, bekommen {}", layout.fields.len(), args.len()));
                }
                let values = layout.fields.iter().zip(args)
                    .map(|((field, field_ty), arg)| self.value(field_ty, arg)
                        .map_err(|e| format!("{name}.{field}: {e}")))
                    .collect::<Result<Vec<_>, _>>()?;
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
        BasicTypeEnum::StructType(t) => t.const_array(&values.iter().map(|v| v.into_struct_value()).collect::<Vec<_>>()).into(),
        BasicTypeEnum::ArrayType(t) => t.const_array(&values.iter().map(|v| v.into_array_value()).collect::<Vec<_>>()).into(),
        BasicTypeEnum::PointerType(t) => t.const_array(&values.iter().map(|v| v.into_pointer_value()).collect::<Vec<_>>()).into(),
        _ => return Err("Arrayelementtyp fuer native Konstanten nicht unterstuetzt".into()),
    })
}

/// Legt alle nativen Konstanten als konstante Globals an (erst Deklaration,
/// dann Initialisierer, damit sich Konstanten gegenseitig referenzieren koennen).
pub(crate) fn emit_constants<'ctx>(
    program: &Program,
    context: &'ctx Context,
    module: &Module<'ctx>,
    target: Option<&TargetData>,
    registry: &NativeStructRegistry<'ctx>,
    native_functions: &HashMap<String, FunctionValue<'ctx>>,
) -> Result<(), String> {
    let constants: Vec<(&String, &TypeRef, &Expr, &Vec<Decorator>)> = program.statements.iter()
        .filter_map(|stmt| match stmt {
            Stmt::NativeConst { name, ty, value, decorators } => Some((name, ty, value, decorators)),
            _ => None,
        })
        .collect();
    if constants.is_empty() {
        return Ok(());
    }
    let target = target.ok_or("Ziel-Datenlayout fuer native Konstanten fehlt")?;
    let emitter = Emitter { context, module, target, registry, native_functions };
    let mut globals = Vec::with_capacity(constants.len());
    for (name, ty, _, decorators) in &constants {
        for decorator in decorators.iter() {
            // Layout-Decorators (2A) wertet codegen::apply_native_layout_decorators aus.
            let known = (matches!(decorator.name.as_str(), "nativ" | "native") && decorator.args.is_empty())
                || matches!(decorator.name.as_str(), "sektion" | "section" | "ausrichten" | "align" | "benutzt" | "used");
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
        let global = module.add_global(llvm, None, name);
        global.set_constant(true);
        globals.push(global);
    }
    for ((name, ty, value, _), global) in constants.iter().zip(globals) {
        let init = emitter.value(ty, value).map_err(|e| format!("Native Konstante '{name}': {e}"))?;
        global.set_initializer(&init);
    }
    Ok(())
}
