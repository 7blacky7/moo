//! Scalar LLVM building blocks for native f32 vectors and 4x4 matrices.
//!
//! This module does not decide Moo syntax, boxing, ABI, or physical GPU layout.
//! Matrix slices below use an explicit column-major adapter. Operations use a
//! fixed scalar evaluation order and do not attach fast-math flags or request
//! FMA contraction. They are independent of the still-evolving native value
//! representation and must only be called by the opt-in native code path.

use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::values::FloatValue;

type Result<T> = std::result::Result<T, String>;

fn validate_f32<'ctx>(context: &'ctx Context, values: &[FloatValue<'ctx>]) -> Result<()> {
    if values.iter().any(|value| value.get_type() != context.f32_type()) {
        return Err("vec/mat: f32-Komponenten erwartet".to_string());
    }
    Ok(())
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum VecOp {
    Add,
    Sub,
    Mul,
}

/// Componentwise vector operation. Only vec2/3/4 of matching dimension.
pub(crate) fn vec_binary<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    op: VecOp,
    left: &[FloatValue<'ctx>],
    right: &[FloatValue<'ctx>],
) -> Result<Vec<FloatValue<'ctx>>> {
    if !(2..=4).contains(&left.len()) || left.len() != right.len() {
        return Err("vec: Dimension muss 2, 3 oder 4 und auf beiden Seiten gleich sein".into());
    }
    validate_f32(context, left)?;
    validate_f32(context, right)?;
    left.iter().zip(right).map(|(&a, &b)| {
        match op {
            VecOp::Add => builder.build_float_add(a, b, "vec.add"),
            VecOp::Sub => builder.build_float_sub(a, b, "vec.sub"),
            VecOp::Mul => builder.build_float_mul(a, b, "vec.mul"),
        }.map_err(|e| format!("vec: {e}"))
    }).collect()
}

/// Multiply a vector by an f32 scalar, preserving component order.
pub(crate) fn vec_scale<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    vector: &[FloatValue<'ctx>],
    scalar: FloatValue<'ctx>,
) -> Result<Vec<FloatValue<'ctx>>> {
    if !(2..=4).contains(&vector.len()) {
        return Err("vec: Dimension muss 2, 3 oder 4 sein".into());
    }
    validate_f32(context, vector)?;
    validate_f32(context, &[scalar])?;
    vector.iter().map(|&v| {
        builder.build_float_mul(v, scalar, "vec.scale").map_err(|e| e.to_string())
    }).collect()
}

/// Three-dimensional cross product; never reassociate or contract operations.
pub(crate) fn vec_cross<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    a: &[FloatValue<'ctx>],
    b: &[FloatValue<'ctx>],
) -> Result<Vec<FloatValue<'ctx>>> {
    if a.len() != 3 || b.len() != 3 {
        return Err("cross: zwei vec3 erwartet".into());
    }
    validate_f32(context, a)?;
    validate_f32(context, b)?;
    [(1, 2), (2, 0), (0, 1)].iter().map(|&(i, j)| {
        let left = builder.build_float_mul(a[i], b[j], "cross.mul").map_err(|e| e.to_string())?;
        let right = builder.build_float_mul(a[j], b[i], "cross.mul").map_err(|e| e.to_string())?;
        builder.build_float_sub(left, right, "cross.sub").map_err(|e| e.to_string())
    }).collect()
}

/// Dot product with left-to-right summation, without an implicit FMA.
pub(crate) fn vec_dot<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    left: &[FloatValue<'ctx>],
    right: &[FloatValue<'ctx>],
) -> Result<FloatValue<'ctx>> {
    if !(2..=4).contains(&left.len()) || left.len() != right.len() {
        return Err("dot: Dimension muss 2, 3 oder 4 und auf beiden Seiten gleich sein".into());
    }
    validate_f32(context, left)?;
    validate_f32(context, right)?;
    let mut products = left.iter().zip(right);
    let (&a, &b) = products.next().ok_or_else(|| "dot: leerer Vektor".to_string())?;
    let mut sum = builder.build_float_mul(a, b, "dot.mul").map_err(|e| e.to_string())?;
    for (&a, &b) in products {
        let product = builder.build_float_mul(a, b, "dot.mul").map_err(|e| e.to_string())?;
        sum = builder.build_float_add(sum, product, "dot.add").map_err(|e| e.to_string())?;
    }
    Ok(sum)
}

/// Column-major adapter: out[row] = sum(matrix[col*4+row] * vector[col]).
pub(crate) fn mat4_vec4<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    matrix: &[FloatValue<'ctx>],
    vector: &[FloatValue<'ctx>],
) -> Result<Vec<FloatValue<'ctx>>> {
    if matrix.len() != 16 || vector.len() != 4 {
        return Err("mat4*vec4: 16 Matrix- und 4 Vektorkomponenten erwartet".into());
    }
    validate_f32(context, matrix)?;
    validate_f32(context, vector)?;
    (0..4).map(|row| {
        let row_values = [matrix[row], matrix[4 + row], matrix[8 + row], matrix[12 + row]];
        vec_dot(context, builder, &row_values, vector)
    }).collect()
}

/// Column-major adapter: out[col,row] = sum(a[k,row] * b[col,k]).
pub(crate) fn mat4_mul<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    a: &[FloatValue<'ctx>],
    b: &[FloatValue<'ctx>],
) -> Result<Vec<FloatValue<'ctx>>> {
    if a.len() != 16 || b.len() != 16 {
        return Err("mat4*mat4: jeweils 16 Komponenten erwartet".into());
    }
    validate_f32(context, a)?;
    validate_f32(context, b)?;
    let mut out = Vec::with_capacity(16);
    for col in 0..4 {
        for row in 0..4 {
            let a_row = [a[row], a[4 + row], a[8 + row], a[12 + row]];
            let b_col = [b[col*4], b[col*4 + 1], b[col*4 + 2], b[col*4 + 3]];
            out.push(vec_dot(context, builder, &a_row, &b_col)?);
        }
    }
    Ok(out)
}
