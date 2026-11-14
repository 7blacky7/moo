# Native Moo: language reference (draft)

**Status:** 31 October 2026. This reference describes the approved P2 language contract, not functionality already available. All snippets are illustrative drafts and have not been compiled. The authoritative decision package is Synapse thought `f67bfcbd-8090-479e-9546-f2713f00f59e`. Constructor and helper syntax will be finalized during implementation.

## Scope and opt-in

Dynamic Moo remains the default. Type annotations alone do not change its behavior. A native function explicitly uses `@native` or `@nativ` and has a complete signature including its return type. Native does not mean unsafe.

```moos
@native
func add(a: i32, b: i32) -> i32:
    return a + b
```

This is target syntax. Existing dynamic semantics, `as_u8` through `as_u64`, classes, data classes and the MooValue layout retain their existing behavior.

## Types and literals

Numeric types are `i8`, `i16`, `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32` and `f64`, plus `bool`. `usize` and `isize` have the target pointer width and are not portable wire types. `void` denotes no return value or an opaque pointer target, not an ordinary stored value. These names are reserved only in type contexts.

A literal directly takes its expected type from a parameter, field, assignment, return or compatible typed neighboring operand. With no context, integer literals default to `i64` and float literals to `f64`. Only literals adapt automatically; conflicting contexts and out-of-range integer literals are compile errors.

Integer source digits and radix are preserved exactly: decimal, `0x`, `0b` and `0o`. A leading zero alone is decimal. The complete negative value is range-checked, so `-9223372036854775808` fits `i64`. Optional suffixes such as `5u8` are planned for a later increment.

Float literals round directly to the target type, nearest with ties to even. Ordinary rounding of `0.1` is allowed; a finite literal that rounds to infinity is an error. Subnormals are allowed. A mathematically nonzero literal that rounds to signed zero emits a warning by default.

## Arithmetic and conversions

Integer `+`, `-`, `*` and unary minus wrap modulo 2 to the N in both debug and release, for signed and unsigned types. Size, offset and allocation arithmetic is checked separately.

Integer division rounds toward zero and the remainder has the dividend's sign. Division or remainder by zero, `MIN / -1`, `MIN % -1`, and shift counts outside `0..N-1` are errors. Statically known errors are compile errors; hosted runtime errors are catchable, with a defined abort when unhandled. A non-returning target hook handles fatal bare-metal errors. Signed right shift is arithmetic; unsigned right shift is logical.

The planned standard cast `als(T, x)` is checked. An integer outside the target range, or a fractional, NaN, infinite or out-of-range float converted to integer, is an error. Intentional wrapping, saturation and rounding are explicit; their concrete helper names remain to be specified. Integer-to-float and float-to-float conversions allow documented ordinary rounding but reject finite-to-infinity overflow; a separate exact conversion is planned.

The `f32`/`f64` baseline follows IEEE semantics including NaN, infinities, signed zero and gradual underflow. Reassociation, implicit FMA contraction, flush-to-zero and fast-math are off by default; changed optimization semantics require explicit opt-in.

## Composite types and layout

Planned forms include fixed arrays `[T; N]`, `buffer[T]`/`puffer[T]`, `ptr[T]`/`zeiger[T]` and function types `fn(T, ...) -> R`.

```moos
struct Vertex:
    position: [f32; 3]
    color: [u8; 4]
```

A `struct`/`struktur` has value semantics and layout derived from the selected target ABI, including field order, size, alignment and offsets. Copying a structure copies its values; a buffer handle within it still shares storage. This layout is not a universal file format: byte order and serialization are separate contracts.

For the first increment, `@packed`/`@gepackt` means alignment 1 without implicit field padding. Conflicting higher alignment is rejected. Unaligned fields require suitable access; a normally aligned reference must not be invented. Padding is neither comparison content nor unchecked serialized payload.

## Buffers, pointers and unsafe operations

A `buffer[T]` manages contiguous storage, checks bounds and retains its owner. Views keep the owner alive. Length changes and reallocation are prohibited while views or exported pins are active. Shared handles expose shared element mutations; deep copying is explicit. Mutable buffers are thread-bound in the first safe increment.

Declaring pointer types, copying pointer values and comparing them with null are safe. Converting integer addresses to pointers, dereferencing, free pointer arithmetic, unchecked access, raw FFI calls and MMIO require an explicit `unsafe`/`unsicher` scope. Volatile does not replace atomics or hardware barriers. A raw pointer must be valid, suitably aligned and live for its contractual duration.

## C interop and callbacks

```moos
@library("GL")
extern "C" func glClear(mask: u32) -> void

@native
func clear() -> void:
    unsafe:
        glClear(0x00004000)
```

The first planned FFI increment covers fixed scalar integer/float arguments, pointers and `void`. Variadics and aggregates by value are rejected until target ABI lowering is demonstrated. C `_Bool`, C++ `bool` and Win32 `BOOL` require separate handling. Target triple, calling convention and data layout are selected before IR generation.

Each binding specifies borrowing or transfer, lifetime, nullability, mutability, pinning, deallocator and error handling. Foreign code may retain a buffer only for the agreed duration. C++ integration starts with an `extern "C"` adapter and opaque handle; the adapter catches C++ exceptions.

Callbacks require a C ABI thunk with the exact signature. Captures use an explicit `user_data` contract. Registration and deregistration account for in-flight and later calls. Errors must not escape uncontrolled through foreign C frames; `void` callbacks require an out-of-band error channel. Foreign threads and reentrancy are either supported or explicitly rejected.

## Vectors and matrices

`vec2`, `vec3`, `vec4` and `mat4` are mathematical `f32` value types; explicit `f64` variants are possible. A matrix transforms a column vector as matrix times vector. Matrix times matrix is matrix multiplication, vector times vector is componentwise, and dot product is a named operation. No fixed SIMD or GPU layout is assumed. Row-/column-major and std140/std430 boundaries require explicit adapters.

## Dynamic boundary and migration

A dynamic call to native code uses an adapter that checks argument count and types, value ranges and ownership. Native-to-native calls do not require unnecessary boxing. An integer can cross the existing dynamic Number path only when that particular value is exactly representable as `f64`; otherwise a boundary error results. Some values above 2^53 are exactly representable. An exact integer box is a later additive step subject to its own runtime gate.

When moving a C function, first specify its target ABI, signature, ownership, errors and tests; then design the native function and explicit adapters. For a C library, declare scalar signatures and link names and verify them with C counterparts on each supported target. Do not directly transfer a C++ class or foreign resource merely because its layout appears compatible.

## Verification and status

A reproducible snapshot and passing legacy gate precede implementation. P2-T33 investigates the currently non-reproducible IR baseline. Only after implementation can the reference snippets become compile-tested examples; Linux, Windows, bare-metal and ABI claims each require their corresponding native evidence.
