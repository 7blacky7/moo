# win_regex — POSIX-Regex fuer den Windows-Build (P013)

MinGW/Windows hat kein POSIX `<regex.h>`. Dieses Verzeichnis vendort die
Regex-Implementierung aus **musl libc v1.2.5** (TRE-basiert), damit
`moo_regex.c` unter Windows dieselbe POSIX-ERE-Semantik bekommt wie unter
Linux. **Unter Linux wird weiterhin die System-libc verwendet** — dieses
Verzeichnis ist ausschliesslich im Windows-Build aktiv (build.rs,
`cfg(windows)`).

## Herkunft

- Quelle: https://git.musl-libc.org/cgit/musl/ (Tag `v1.2.5`)
- Dateien: `include/regex.h`, `src/regex/tre.h`, `src/regex/tre-mem.c`,
  `src/regex/regcomp.c`, `src/regex/regexec.c`
- `regerror.c` ist eine eigene kompakte Implementierung (musl's Original
  haengt an musl-internen Locale-Mechanismen); Meldungstexte wie musl.

## Lizenz

- musl: MIT-Lizenz (https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT)
- TRE-Anteile (tre.h, regcomp.c, regexec.c, tre-mem.c):
  Copyright (c) 2001-2009 Ville Laurikari, BSD-2-Clause — die
  Original-Lizenzkoepfe sind in den Dateien unveraendert enthalten.

## Lokale Patches (minimal-invasiv, jeweils mit `P013`-Kommentar markiert)

1. `regex.h`: `#include <features.h>` entfernt (musl-intern);
   `#include <bits/alltypes.h>` + `__NEED_*`-Makros ersetzt durch
   `#include <stddef.h>` und `typedef ptrdiff_t regoff_t;`
   (vorzeichenbehaftet, 64 Bit unter Win64 — traegt rm_so/rm_eo inkl. -1).
2. `tre.h`: `#define hidden` (leer) ergaenzt — musl's Visibility-Attribut,
   beim statischen Vendoring bedeutungslos.
3. `regex.h`: POSIX-Konstanten `RE_DUP_MAX` (255) und `CHARCLASS_NAME_MAX`
   (14) ergaenzt — sie kommen unter POSIX aus `<limits.h>`, das MinGW sie
   nicht definiert; Werte wie musl v1.2.5.
4. Keine weiteren Aenderungen; `regcomp.c`, `regexec.c`, `tre-mem.c` sind
   byte-identisch zu musl v1.2.5.

## Bekannte, bewusste Einschraenkung

`wchar_t` ist unter Windows 16 Bit (UTF-16) statt 32 Bit — Unicode-
Zeichenklassen jenseits der BMP verhalten sich daher ggf. anders als unter
Linux/glibc. Fuer ASCII-Patterns (der praktische Normalfall der moo-stdlib)
ist das Verhalten identisch. Linux-Verhalten ist von alledem unberuehrt.
