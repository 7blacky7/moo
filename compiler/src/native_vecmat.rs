//! P2-T16 (Plan 3f03704a E3/E4): LLVM-Bausteine fuer die eingebauten f32-
//! Werttypen vec2/vec3/vec4/mat4 auf SSA-Vektoren <N x float>.
//!
//! Vertrag: jede Bahn rechnet dieselbe Operationsfolge wie die skalare
//! C-Referenz (compiler/runtime/moo_3d_math.c, skripte/p2/grafik/mat_ref.c).
//! Keine Fast-Math-Flags, kein fmuladd, keine Reassoziation. Matrizen sind
//! vier Spaltenvektoren (m[spalte*4+zeile]); Summen laufen in fester
//! Reihenfolge links nach rechts. Laden/Speichern geschieht beim Aufrufer
//! komponentenweise (nie ein Laden eines LLVM-Vektortyps); dieses Modul sieht
//! nur SSA-Werte.

use std::collections::BTreeSet;

use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::intrinsics::Intrinsic;
use inkwell::module::Module;
use inkwell::types::VectorType;
use inkwell::values::{FloatValue, IntValue, ValueKind, VectorValue};
use inkwell::FloatPredicate;

use crate::ast::{Expr, Stmt, TypeKind, TypeRef};
use crate::native_struct::VECMAT_NAMEN;

type Result<T> = std::result::Result<T, String>;

fn e(err: impl ToString) -> String { err.to_string() }

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum VecOp {
    Add,
    Sub,
    Mul,
    Div,
}

fn index<'ctx>(context: &'ctx Context, bahn: u32) -> IntValue<'ctx> {
    context.i32_type().const_int(u64::from(bahn), false)
}

fn maske<'ctx>(context: &'ctx Context, bahnen: &[u32]) -> VectorValue<'ctx> {
    let werte: Vec<IntValue<'ctx>> = bahnen.iter().map(|&b| index(context, b)).collect();
    VectorType::const_vector(&werte)
}

fn bahn<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, v: VectorValue<'ctx>, i: u32,
) -> Result<FloatValue<'ctx>> {
    Ok(builder.build_extract_element(v, index(context, i), "vm.bahn").map_err(e)?.into_float_value())
}

fn dimension(v: VectorValue<'_>) -> u32 { v.get_type().get_size() }

/// Komponentenweise Operation zweier gleich breiter Vektoren.
pub(crate) fn vec_binary<'ctx>(
    builder: &Builder<'ctx>, op: VecOp, a: VectorValue<'ctx>, b: VectorValue<'ctx>,
) -> Result<VectorValue<'ctx>> {
    if a.get_type() != b.get_type() {
        return Err("vec: beide Seiten brauchen dieselbe Dimension".into());
    }
    match op {
        VecOp::Add => builder.build_float_add(a, b, "vm.add"),
        VecOp::Sub => builder.build_float_sub(a, b, "vm.sub"),
        VecOp::Mul => builder.build_float_mul(a, b, "vm.mul"),
        VecOp::Div => builder.build_float_div(a, b, "vm.div"),
    }.map_err(e)
}

/// Skalar auf alle Bahnen (insertelement + shufflevector).
pub(crate) fn splat<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, scalar: FloatValue<'ctx>, bahnen: u32,
) -> Result<VectorValue<'ctx>> {
    let ty = scalar.get_type().vec_type(bahnen);
    let einzeln = builder.build_insert_element(ty.get_undef(), scalar, index(context, 0), "vm.splat.ein")
        .map_err(e)?;
    let null = vec![0u32; bahnen as usize];
    builder.build_shuffle_vector(einzeln, ty.get_undef(), maske(context, &null), "vm.splat").map_err(e)
}

/// vecN * s bzw. vecN / s: der Skalar wird auf alle Bahnen verteilt; je Bahn
/// genau eine IEEE-Multiplikation bzw. -Division wie `x * s` / `x / s` in C.
pub(crate) fn vec_scale<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, op: VecOp, v: VectorValue<'ctx>, s: FloatValue<'ctx>,
) -> Result<VectorValue<'ctx>> {
    if !matches!(op, VecOp::Mul | VecOp::Div) {
        return Err("vec: mit einem Skalar nur * und /".into());
    }
    let s = splat(context, builder, s, dimension(v))?;
    vec_binary(builder, op, v, s)
}

pub(crate) fn vec_neg<'ctx>(builder: &Builder<'ctx>, v: VectorValue<'ctx>) -> Result<VectorValue<'ctx>> {
    builder.build_float_neg(v, "vm.neg").map_err(e)
}

/// Punktprodukt: Bahnprodukte als Vektor, dann skalare Summe in der
/// Reihenfolge x, y, z, w (((x*x' + y*y') + z*z') + w*w').
pub(crate) fn vec_dot<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, a: VectorValue<'ctx>, b: VectorValue<'ctx>,
) -> Result<FloatValue<'ctx>> {
    let produkte = vec_binary(builder, VecOp::Mul, a, b)?;
    let mut summe = bahn(context, builder, produkte, 0)?;
    for i in 1..dimension(produkte) {
        let x = bahn(context, builder, produkte, i)?;
        summe = builder.build_float_add(summe, x, "vm.dot.add").map_err(e)?;
    }
    Ok(summe)
}

/// Kreuzprodukt (nur vec3): a.yzx * b.zxy - a.zxy * b.yzx, je Bahn
/// (a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x).
pub(crate) fn vec_cross<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, a: VectorValue<'ctx>, b: VectorValue<'ctx>,
) -> Result<VectorValue<'ctx>> {
    if dimension(a) != 3 || a.get_type() != b.get_type() {
        return Err("kreuz: zwei vec3 erwartet".into());
    }
    let yzx = maske(context, &[1, 2, 0]);
    let zxy = maske(context, &[2, 0, 1]);
    let a1 = builder.build_shuffle_vector(a, a, yzx, "vm.kreuz.a1").map_err(e)?;
    let b1 = builder.build_shuffle_vector(b, b, zxy, "vm.kreuz.b1").map_err(e)?;
    let a2 = builder.build_shuffle_vector(a, a, zxy, "vm.kreuz.a2").map_err(e)?;
    let b2 = builder.build_shuffle_vector(b, b, yzx, "vm.kreuz.b2").map_err(e)?;
    let links = vec_binary(builder, VecOp::Mul, a1, b1)?;
    let rechts = vec_binary(builder, VecOp::Mul, a2, b2)?;
    vec_binary(builder, VecOp::Sub, links, rechts)
}

fn pruefe_mat4(m: &[VectorValue<'_>]) -> Result<()> {
    if m.len() != 4 || m.iter().any(|s| dimension(*s) != 4) {
        return Err("mat4: vier vec4-Spalten erwartet".into());
    }
    Ok(())
}

/// mat4 * vec4 = ((s0*v.x + s1*v.y) + s2*v.z) + s3*v.w: vier
/// Vektormultiplikationen, drei Vektoradditionen. Je Bahn (Zeile) dieselbe
/// Folge wie p2_mat4_vec4 bzw. mat4_multiply (moo_3d_math.c:15-27).
pub(crate) fn mat4_vec4<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, m: &[VectorValue<'ctx>], v: VectorValue<'ctx>,
) -> Result<VectorValue<'ctx>> {
    pruefe_mat4(m)?;
    if dimension(v) != 4 {
        return Err("mat4 * vec4: vec4 erwartet".into());
    }
    let mut summe = None;
    for (k, spalte) in m.iter().enumerate() {
        let s = splat(context, builder, bahn(context, builder, v, k as u32)?, 4)?;
        let produkt = vec_binary(builder, VecOp::Mul, *spalte, s)?;
        summe = Some(match summe {
            None => produkt,
            Some(bisher) => vec_binary(builder, VecOp::Add, bisher, produkt)?,
        });
    }
    summe.ok_or_else(|| "mat4 * vec4: leere Matrix".to_string())
}

/// mat4 * mat4: Ergebnisspalte j = a * b.sj (wie mat4_multiply).
pub(crate) fn mat4_mul<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, a: &[VectorValue<'ctx>], b: &[VectorValue<'ctx>],
) -> Result<Vec<VectorValue<'ctx>>> {
    pruefe_mat4(b)?;
    b.iter().map(|&spalte| mat4_vec4(context, builder, a, spalte)).collect()
}

/// Betrag (euklidische Norm): llvm.sqrt.f32(punkt(v, v)), wie sqrtf(x*x + ...).
pub(crate) fn vec_betrag<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, module: &Module<'ctx>, v: VectorValue<'ctx>,
) -> Result<FloatValue<'ctx>> {
    let quadrat = vec_dot(context, builder, v, v)?;
    let sqrt = Intrinsic::find("llvm.sqrt")
        .and_then(|i| i.get_declaration(module, &[quadrat.get_type().into()]))
        .ok_or("betrag: llvm.sqrt nicht verfuegbar")?;
    match builder.build_call(sqrt, &[quadrat.into()], "vm.betrag").map_err(e)?.try_as_basic_value() {
        ValueKind::Basic(wert) => Ok(wert.into_float_value()),
        _ => Err("betrag: llvm.sqrt liefert keinen Wert".into()),
    }
}

/// normalisiere(v) = v / betrag(v) nur bei betrag > 0 (wie mat4_lookat
/// "if (len > 0)"); sonst (0 oder NaN) bleibt v unveraendert.
pub(crate) fn vec_normalisiere<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, module: &Module<'ctx>, v: VectorValue<'ctx>,
) -> Result<VectorValue<'ctx>> {
    let betrag = vec_betrag(context, builder, module, v)?;
    let geteilt = vec_scale(context, builder, VecOp::Div, v, betrag)?;
    let positiv = builder.build_float_compare(
        FloatPredicate::OGT, betrag, betrag.get_type().const_zero(), "vm.norm.positiv",
    ).map_err(e)?;
    Ok(builder.build_select(positiv, geteilt, v, "vm.norm").map_err(e)?.into_vector_value())
}

/// == bzw. != ueber alle Komponenten (IEEE: NaN ist ungleich). Gleich: alle
/// Bahnen oeq; ungleich: mindestens eine Bahn une.
pub(crate) fn vecmat_gleich<'ctx>(
    context: &'ctx Context, builder: &Builder<'ctx>, a: &[VectorValue<'ctx>], b: &[VectorValue<'ctx>], gleich: bool,
) -> Result<IntValue<'ctx>> {
    if a.len() != b.len() || a.iter().zip(b).any(|(x, y)| x.get_type() != y.get_type()) {
        return Err("Vergleich: beide Seiten brauchen denselben Vektor-/Matrixtyp".into());
    }
    let praedikat = if gleich { FloatPredicate::OEQ } else { FloatPredicate::UNE };
    let mut ergebnis: Option<IntValue<'ctx>> = None;
    for (x, y) in a.iter().zip(b) {
        for i in 0..dimension(*x) {
            let l = bahn(context, builder, *x, i)?;
            let r = bahn(context, builder, *y, i)?;
            let c = builder.build_float_compare(praedikat, l, r, "vm.cmp").map_err(e)?;
            ergebnis = Some(match ergebnis {
                None => c,
                Some(bisher) if gleich => builder.build_and(bisher, c, "vm.cmp.und").map_err(e)?,
                Some(bisher) => builder.build_or(bisher, c, "vm.cmp.oder").map_err(e)?,
            });
        }
    }
    ergebnis.ok_or_else(|| "Vergleich: leerer Vektor".to_string())
}

/// P2-T16 (Plan 3f03704a E2): eingebaute Typen, die das Programm braucht.
/// Bedarf = Name in einer beliebigen Typposition oder Aufrufname (Konstruktor)
/// in einem @nativ-Koerper. mat4 zieht vec4 nach. Ohne Bedarf wird nichts
/// registriert; dynamische Programme behalten ihr IR.
pub(crate) fn bedarf(stmts: &[Stmt]) -> BTreeSet<&'static str> {
    let mut scan = Bedarf::default();
    scan.stmts(stmts);
    if scan.gefunden.contains("mat4") {
        scan.gefunden.insert("vec4");
    }
    if scan.gefunden.contains("dmat4") {
        scan.gefunden.insert("dvec4");
    }
    scan.gefunden
}

#[derive(Default)]
struct Bedarf {
    gefunden: BTreeSet<&'static str>,
    nativ: bool,
}

impl Bedarf {
    fn name(&mut self, name: &str) {
        if let Some(eingebaut) = VECMAT_NAMEN.iter().find(|n| **n == name) {
            self.gefunden.insert(eingebaut);
        }
    }

    fn typ(&mut self, ty: &TypeRef) {
        match &ty.kind {
            TypeKind::Name(name) => self.name(name),
            TypeKind::Generic { args, .. } => args.iter().for_each(|a| self.typ(a)),
            TypeKind::Array { element, .. } => self.typ(element),
            TypeKind::Function { params, return_type, .. } => {
                params.iter().for_each(|p| self.typ(p));
                self.typ(return_type);
            }
        }
    }

    fn typen(&mut self, typen: &[Option<TypeRef>]) {
        typen.iter().flatten().for_each(|t| self.typ(t));
    }

    fn stmts(&mut self, stmts: &[Stmt]) {
        stmts.iter().for_each(|s| self.stmt(s));
    }

    fn exprs(&mut self, exprs: &[Expr]) {
        exprs.iter().for_each(|x| self.expr(x));
    }

    fn optional(&mut self, expr: &Option<Expr>) {
        if let Some(x) = expr { self.expr(x); }
    }

    fn stmt(&mut self, stmt: &Stmt) {
        match stmt {
            Stmt::Assignment { ty, value, .. } | Stmt::ConstAssignment { ty, value, .. }
            | Stmt::RawDataU32 { ty, value, .. } => {
                if let Some(t) = ty { self.typ(t); }
                self.expr(value);
            }
            Stmt::NativeConst { ty, value, .. } => {
                self.typ(ty);
                self.optional(value);
            }
            Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _) | Stmt::Throw(value, _)
            | Stmt::Expression(value, _) | Stmt::Defer(value, _) | Stmt::Expect(value, _)
            | Stmt::Precondition { condition: value, .. }
            | Stmt::Postcondition { condition: value, .. } => self.expr(value),
            Stmt::Return(value, _) => self.optional(value),
            Stmt::PropertyAssignment { object, value, .. } => {
                self.expr(object);
                self.expr(value);
            }
            Stmt::IndexAssignment { object, index, value, .. } => {
                self.expr(object);
                self.expr(index);
                self.expr(value);
            }
            Stmt::If { condition, body, else_body, .. } => {
                self.expr(condition);
                self.stmts(body);
                self.stmts(else_body);
            }
            Stmt::While { condition, body, .. } => {
                self.expr(condition);
                self.stmts(body);
            }
            Stmt::For { iterable, body, .. } | Stmt::ParallelFor { iterable, body, .. } => {
                self.expr(iterable);
                self.stmts(body);
            }
            Stmt::Guard { condition, else_body, .. } => {
                self.expr(condition);
                self.stmts(else_body);
            }
            Stmt::FunctionDef { param_types, return_type, defaults, body, decorators, .. } => {
                self.typen(param_types);
                if let Some(t) = return_type { self.typ(t); }
                defaults.iter().for_each(|d| self.optional(d));
                let vorher = self.nativ;
                self.nativ |= decorators.iter().any(|d| d.name == "nativ" || d.name == "native");
                self.stmts(body);
                self.nativ = vorher;
            }
            Stmt::ExternC { params, result, .. } => {
                params.iter().for_each(|(_, t)| self.typ(t));
                self.typ(result);
            }
            Stmt::StructDef { fields, .. } => fields.iter().for_each(|(_, t)| self.typ(t)),
            Stmt::ClassDef { body, .. } | Stmt::UnsafeBlock { body, .. } | Stmt::TestDef { body, .. } =>
                self.stmts(body),
            Stmt::InterfaceDef { signatures, .. } => {
                for signatur in signatures.iter().flatten() {
                    self.typen(&signatur.param_types);
                    if let Some(t) = &signatur.return_type { self.typ(t); }
                }
            }
            Stmt::TryCatch { try_body, catch_body, .. } => {
                self.stmts(try_body);
                self.stmts(catch_body);
            }
            Stmt::Export(inner, _) => self.stmt(inner),
            Stmt::Match { value, cases, .. } => {
                self.expr(value);
                for (muster, waechter, body) in cases {
                    self.optional(muster);
                    self.optional(waechter);
                    self.stmts(body);
                }
            }
            Stmt::DataClassDef { field_types, .. } => self.typen(field_types),
            Stmt::Break(_) | Stmt::Continue(_) | Stmt::Import { .. } | Stmt::Global(_, _) => {}
        }
    }

    fn expr(&mut self, expr: &Expr) {
        match expr {
            Expr::FunctionCall { name, args, .. } => {
                if self.nativ { self.name(name); }
                self.exprs(args);
            }
            Expr::MethodCall { object, args, .. } => {
                self.expr(object);
                self.exprs(args);
            }
            Expr::New { args, .. } | Expr::List(args) => self.exprs(args),
            Expr::BinaryOp { left, right, .. } | Expr::NullishCoalesce { left, right }
            | Expr::Pipe { left, right } | Expr::Range { start: left, end: right }
            | Expr::IndexAccess { object: left, index: right } => {
                self.expr(left);
                self.expr(right);
            }
            Expr::UnaryOp { operand, .. } | Expr::Spread(operand)
            | Expr::PropertyAccess { object: operand, .. }
            | Expr::OptionalChain { object: operand, .. } => self.expr(operand),
            Expr::ListComprehension { expr, iterable, condition, .. } => {
                self.expr(expr);
                self.expr(iterable);
                if let Some(c) = condition { self.expr(c); }
            }
            Expr::Dict(paare) => {
                for (k, v) in paare {
                    self.expr(k);
                    self.expr(v);
                }
            }
            Expr::Lambda { param_types, body, .. } => {
                self.typen(param_types);
                self.expr(body);
            }
            Expr::Ternary { condition, then_val, else_val } => {
                self.expr(condition);
                self.expr(then_val);
                self.expr(else_val);
            }
            Expr::MatchExpr { value, cases } => {
                self.expr(value);
                for (muster, waechter, ergebnis) in cases {
                    self.optional(muster);
                    self.optional(waechter);
                    self.expr(ergebnis);
                }
            }
            Expr::QueryExpr { source, where_cond, order_expr, select_expr, .. } => {
                self.expr(source);
                if let Some(w) = where_cond { self.expr(w); }
                if let Some(o) = order_expr { self.expr(o); }
                self.expr(select_expr);
            }
            Expr::Number(_) | Expr::NativeNumber { .. } | Expr::String(_) | Expr::Boolean(_)
            | Expr::None | Expr::Identifier(_) | Expr::This => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// B1: alle Bausteine erzeugen gueltiges IR mit <N x float>-Operationen und
    /// ohne Fast-Math/FMA/Kontraktion.
    #[test]
    fn ssa_vektoren_ohne_fast_math() {
        let context = Context::create();
        let module = context.create_module("vm");
        let builder = context.create_builder();
        let v4 = context.f32_type().vec_type(4);
        let v3 = context.f32_type().vec_type(3);
        let fn_type = v4.fn_type(&[v4.into(), v4.into(), v3.into(), v3.into()], false);
        let function = module.add_function("probe", fn_type, None);
        builder.position_at_end(context.append_basic_block(function, "start"));
        let p = |i| function.get_nth_param(i).unwrap().into_vector_value();
        let (a, b, c, d) = (p(0), p(1), p(2), p(3));
        let m = [a, b, a, b];
        let r = mat4_vec4(&context, &builder, &m, b).unwrap();
        let mm = mat4_mul(&context, &builder, &m, &m).unwrap();
        let s = vec_dot(&context, &builder, r, mm[3]).unwrap();
        let k = vec_cross(&context, &builder, c, d).unwrap();
        let n = vec_normalisiere(&context, &builder, &module, k).unwrap();
        let g = vecmat_gleich(&context, &builder, &[n], &[c], true).unwrap();
        let skaliert = vec_scale(&context, &builder, VecOp::Mul, r, s).unwrap();
        let negiert = vec_neg(&builder, skaliert).unwrap();
        let wahl = builder.build_select(g, negiert, r, "wahl").unwrap().into_vector_value();
        builder.build_return(Some(&wahl)).unwrap();
        module.verify().unwrap();
        let ir = module.print_to_string().to_string();
        assert!(ir.contains("fmul <4 x float>"), "{ir}");
        assert!(ir.contains("fadd <4 x float>"), "{ir}");
        assert!(ir.contains("shufflevector <3 x float>"), "{ir}");
        assert!(ir.contains("llvm.sqrt.f32"), "{ir}");
        for verboten in ["fast", "contract", "reassoc", "fmuladd", "llvm.fma", "nnan", "ninf"] {
            assert!(!ir.contains(verboten), "{verboten} im IR:\n{ir}");
        }
    }

    #[test]
    fn falsche_dimensionen_werden_abgelehnt() {
        let context = Context::create();
        let module = context.create_module("vm");
        let builder = context.create_builder();
        let v4 = context.f32_type().vec_type(4);
        let v3 = context.f32_type().vec_type(3);
        let function = module.add_function("probe", context.void_type().fn_type(&[v4.into(), v3.into()], false), None);
        builder.position_at_end(context.append_basic_block(function, "start"));
        let a = function.get_nth_param(0).unwrap().into_vector_value();
        let c = function.get_nth_param(1).unwrap().into_vector_value();
        assert!(vec_binary(&builder, VecOp::Add, a, c).is_err());
        assert!(vec_cross(&context, &builder, a, a).is_err());
        assert!(mat4_vec4(&context, &builder, &[a, a, a], a).is_err());
        assert!(vec_scale(&context, &builder, VecOp::Add, a, context.f32_type().const_zero()).is_err());
    }
}
