# Native Moo: migration guide

**Status:** 14 November 2026. Companion to the language reference (`language-reference-draft-en.md`). Runnable examples live in `beispiele/nativ/`; everything else is a described procedure, not finished tooling.

## 1. Making a function native

1. Choose types: every parameter and the return type get a machine type (`i32`, `u8`, `f64` ...). A native function needs a complete signature.
2. Put `@nativ` above the function. The remaining dynamic functions keep their behavior.
3. Check edge cases that behaved differently in dynamic code: division and remainder round toward zero, `+` and `-` wrap modulo 2^N, shift counts outside `0..N-1` are errors (`beispiele/nativ/02_ganzzahl_semantik.moos`).
4. Write conversions explicitly: `als(T, x)` is checked, `als_wrap` and `als_saettigend` choose wrapping or saturation (`beispiele/nativ/03_casts.moos`).
5. Call sites: native functions call each other without boxing. A call from dynamic code goes through an adapter that checks count, type and value range; an integer crosses the dynamic number path only when it is exactly representable as `f64`.

## 2. Moving a C runtime function to native Moo

1. Record first: signature, ownership, error behavior and a test with the expected output of the C version.
2. Write the function as `@nativ` with the same type widths and compare the output.
3. Check arithmetic deliberately: native integer overflow wraps in a defined way; the C code should not have relied on `-fwrapv`. Where the C version had undefined behavior on overflow, the new behavior is the reference.
4. Keep data in buffers rather than dynamic lists (`puffer_erstellen(i32, n)`) when the function reads or writes many values.
5. Remove the C function only after the test and comparison pass.

## 3. Binding a C library

1. Use only scalar integer/float parameters, pointers and `void`. The compiler currently rejects `bool`, aggregates and function types by value; for `bool`, use an integer (`i32` or `u8`) in a C wrapper instead.
2. Write the declaration: `@bibliothek(linux="name")` and `extern "C" funktion name(a: i8, b: u8) -> i32`. Calls go in a `@nativ` function inside `unsicher:`.
3. Verify with a C counterpart program per target (templates: tests `bibliothek_ziel_*`, `ffi_i64*` in `compiler/tests/p2`).
4. Callbacks: write the Moo function as `@nativ` and `@rückruf` and pass it as `ptr[fn(...) -> R]` (`beispiele/nativ/10_rueckruf.moos`); carry state through a `user_data` pointer and use the error value `@rückruf(value)` for fallible callbacks. Structures may cross the boundary by value (`beispiele/nativ/11_struktur_by_value.moos`); fixed arrays and bare function types by value may not.
5. Bind C++ only through an `extern "C"` adapter with an opaque handle; the adapter catches exceptions. The handle is `@opak struktur Name`, the release function carries `@verbraucht(1)` (`beispiele/nativ/12_opaker_griff.moos`; pattern with an adapter: test `cpp_griff`).
6. Pass data without copying: tensors and frames go as `puffer[f32]` and `puffer[u8]`, read-only parameters with `@liest(n)` (`beispiele/nativ/08_leihgabe_liest.moos`). Use `vec3`/`mat4` instead of number lists for vectors and matrices (`beispiele/nativ/06_vec_mat.moos`); pointer arithmetic only inside `unsicher:` (`beispiele/nativ/07_zeiger.moos`).

## 4. Passing dynamic data across the boundary

Not every number above 2^53 is exactly representable on the dynamic path; the transition then reports a boundary error. An exact integer container is a later step. Structures, in contrast, cross the boundary by value without loss and can be used as boxes in dynamic code (`beispiele/nativ/13_struktur_dynamisch.moos`). Native functions may be called from methods and lambdas; an error there is catchable with `versuche:` (`beispiele/nativ/09_aufruf_aus_methoden.moos`).

## 5. Checklist before merging

- All files in `beispiele/nativ/` run with `moo-compiler run` and print the output noted in their header.
- Existing dynamic programs behave unchanged when no native function is used.
- Every FFI binding has a C counterpart per released target.
