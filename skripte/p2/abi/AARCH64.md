# AArch64 P2-T56 – Clang-belegter ABI-Slice

Die bestehenden Strukturtypen aus
`skripte/p7/lader/system_ffi_struct.moos` werden durch den normalen
Moo-Compiler fuer `aarch64-bare` erzeugt. Die C-Referenz stammt aus
`skripte/p2/abi/struct_by_value.c` und wird unabhaengig von Clang
fuer `aarch64-unknown-none` kompiliert.

Die derzeit freigegebenen Einzel-Roundtrips haben jeweils genau
**einen** gleichnamigen Strukturparameter und -Rueckgabetyp:

| Layout | Clang/AAPCS64 Argument | Rueckgabe |
| --- | --- | --- |
| 4 Byte (1 u32) | i64 (8-Byte-Registerslot) | i32 |
| 8 Byte (2 u32) | i64 | i64 |
| 12/16 Byte (3/4 u32) | [2 x i64] | [2 x i64] |
| 24 Byte (6 u32) | ptr (kein byval) | ptr sret align 4 |

12-Byte-Quellwerte werden vor dem 16-Byte-Register-Load in einem
groesseren temporären Slot auf null gepolstert. Der Rueckgabeslot
wird separat behandelt, weil seine ABI-Klasse nicht zwangsläufig
mit dem Eingabeslot identisch ist.

## Reproduzierbarer Cross-Test

```bash
bash skripte/p2/abi/aarch64_struct_slice.sh
```

Der Runner vergleicht Moo-LLVM-Deklarationen mit einer getrennten
Clang-C-AArch64-Referenz, verifiziert LLVM, erzeugt AArch64-Objekte
und linkt sie relocatable (ELF). **Er fuehrt keinen AArch64-Code
auf echter ARM-Hardware aus.** Das ist noch kein Laufzeitbeweis.

Noch nicht freigegeben: komplexe HFA-Floats, gepackte 5B-ABI,
mehrere Strukturargumente, u64-Praefixe/Registerdruck,
variadische ABI-Aufrufe und pauschale AArch64 Aggregate.
