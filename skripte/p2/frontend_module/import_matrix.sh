#!/usr/bin/env bash
# P2-T47 (Plan b1f51d65 E1-E3): Importmatrix, Rautenimport, qualifizierte
# Namen und Negativfaelle als echtes Gate.
set -u
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-frontend-module/matrix
mkdir -p "$out"
fehler=0
t=compiler/tests/p2

lauf() { # name expected [warnung]
  local name="$1" expected="$2" warnung="${3:-}"
  if ! timeout 120s "$compiler" compile "$t/$name.moos" -o "$out/$name" >"$out/$name.compile.log" 2>&1; then
    echo "FAIL $name: Compile"; cat "$out/$name.compile.log"; fehler=1; return
  fi
  if [ -n "$warnung" ] && ! rg -q -F "$warnung" "$out/$name.compile.log"; then
    echo "FAIL $name: Warnung fehlt: $warnung"; fehler=1; return
  fi
  if ! timeout 30s "$out/$name" >"$out/$name.out" 2>"$out/$name.err"; then
    echo "FAIL $name: Laufzeit-Exit"; cat "$out/$name.err"; fehler=1; return
  fi
  if ! cmp -s "$expected" "$out/$name.out"; then
    echo "FAIL $name: Ausgabe"; diff "$expected" "$out/$name.out"; fehler=1; return
  fi
  echo "PASS $name"
}

negativ() { # name
  local name="$1"
  timeout 120s "$compiler" compile "$t/$name.moos" --emit-ir -o "$out/$name.ll" >"$out/$name.log" 2>&1
  local rc=$?
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
    echo "FAIL $name: unerwarteter Exit $rc"; fehler=1; return
  fi
  if ! rg -q -F -f "$t/$name.expected_error" "$out/$name.log"; then
    echo "FAIL $name: erwartete Diagnose fehlt"; cat "$out/$name.log"; fehler=1; return
  fi
  echo "PASS $name (Diagnose)"
}

# E1: vier Importformen, gleiche Ausgabe; IR-Signaturen der importierten nativen Funktionen.
for form in voll benannt pfad alias; do
  lauf "import_matrix_$form" "$t/import_matrix.expected"
  if timeout 120s "$compiler" compile "$t/import_matrix_$form.moos" --emit-ir -o "$out/import_matrix_$form.ll" >/dev/null 2>&1 \
    && timeout 30s mise exec -- opt -passes=verify -disable-output "$out/import_matrix_$form.ll" \
    && rg -q 'define internal double @__moo_native[.]m_skalar\(i32 [^,]*, double' "$out/import_matrix_$form.ll" \
    && rg -q '@__moo_native[.]m_verschiebe\(' "$out/import_matrix_$form.ll" \
    && rg -q 'declare [^@]*i32 @abs\(i32' "$out/import_matrix_$form.ll"; then
    echo "PASS import_matrix_$form: IR-Signaturen"
  else
    echo "FAIL import_matrix_$form: IR-Signaturen"; fehler=1
  fi
  if timeout 30s "$compiler" ast "$t/import_matrix_$form.moos" --imports >"$out/import_matrix_$form.ast" 2>&1 \
    && rg -q 'name: "MSchnitt"' "$out/import_matrix_$form.ast" \
    && ! rg -q '"[A-Za-z_]+[.][A-Za-z_]+"' "$out/import_matrix_$form.ast"; then
    echo "PASS import_matrix_$form: Schnittstelle erhalten, kein Punktname nach dem Resolver"
  else
    echo "FAIL import_matrix_$form: AST nach --imports"; fehler=1
  fi
done

# Bestandsfaelle aus den frueheren Diagnose-Tasks, jetzt mit Laufzeit.
printf '42\n' >"$out/zweiundvierzig.expected"
lauf import_kern_alias "$out/zweiundvierzig.expected"
printf '1\n' >"$out/eins.expected"
lauf import_kern_typ_pfad "$out/eins.expected"
lauf import_kern_typ_einfach "$out/eins.expected"

# E2: Rautenimport.
lauf import_raute "$t/import_raute.expected"

# E3: Negativfaelle und Verdeckung.
for name in import_neg_mitglied import_neg_typ import_neg_qualifizierer import_kern_unbekannt import_neg_transitiv_alias import_neg_transitiv_benannt; do
  negativ "$name"
done
timeout 120s "$compiler" compile "$t/import_qualifiziert_kollision.moos" --emit-ir -o "$out/kollision.ll" >"$out/kollision.log" 2>&1
rc=$?
if [ "$rc" -ne 0 ] && [ "$rc" -lt 124 ] \
  && rg -q -F "Struktur 'P2Gleichname' ist bereits definiert: " "$out/kollision.log" \
  && rg -q -F 'modul_qualifiziert_a.moos und ' "$out/kollision.log" \
  && rg -q -F 'modul_qualifiziert_b.moos' "$out/kollision.log"; then
  echo "PASS import_qualifiziert_kollision (beide Pfade)"
else
  echo "FAIL import_qualifiziert_kollision (Exit $rc)"; cat "$out/kollision.log"; fehler=1
fi
lauf import_schatten_lexikal "$t/import_schatten_lexikal.expected" "Modulalias 'm' (Modul 'modul_import_matrix') wird von einer Variable gleichen Namens verdeckt; 'm.MGRENZE'"
lauf import_verdeckt "$t/import_verdeckt.expected" "Modulalias 'm' (Modul 'modul_import_matrix') wird von einer Variable gleichen Namens verdeckt"

exit "$fehler"
