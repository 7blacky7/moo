#!/usr/bin/env bash
# P2-T9/T11: strukturelles LLVM-Golden; kein Nachweis der Guard-Dominanz.
set -euo pipefail
cd "$(dirname "$0")/../../.."
check_ir() {
    awk '
    function fail(s) { print "FAIL: " s > "/dev/stderr"; errors++ }
    function finish() {
        if (name == "" || !(name in expected)) return
        if (seen[name]++) fail(name ": doppelte Definition")
        if (count != 1) fail(name ": Operation fehlt oder mehrfach")
        if (bad) fail(name ": unzulaessige Flags oder dynamische Arithmetik")
        if (guard[name] && (!icmp || !branch)) fail(name ": Vergleich/Branch fehlt")
        if ((name == "p2_div" || name == "p2_rem") && (!zero || !minus || !min))
            fail(name ": Grenze 0, -1 oder i64-MIN fehlt")
        if ((name == "p2_shl" || name == "p2_ashr" || name == "p2_lshr") && !limit)
            fail(name ": Shiftgrenze 63/64 fehlt")
    }
    BEGIN {
        expected["p2_add"]="add"; expected["p2_sub"]="sub"; expected["p2_mul"]="mul"
        expected["p2_neg"]="sub"; expected["p2_div"]="sdiv"; expected["p2_rem"]="srem"
        expected["p2_shl"]="shl"; expected["p2_ashr"]="ashr"; expected["p2_lshr"]="lshr"
        guard["p2_div"]=guard["p2_rem"]=guard["p2_shl"]=guard["p2_ashr"]=guard["p2_lshr"]=1
    }
    /^define[[:space:]]/ && match($0, /@"?p2_[A-Za-z0-9_]+/) {
        name=substr($0,RSTART+1,RLENGTH-1); gsub(/"/,"",name)
        count=bad=icmp=branch=zero=minus=min=limit=0; next
    }
    name != "" && /^}/ { finish(); name=""; next }
    name != "" && (name in expected) {
        op=expected[name]; width=(name ~ /^p2_(add|sub|mul|neg)$/ ? "i8" : "i64")
        if ($0 ~ ("=[[:space:]]*" op "[[:space:]]+((nsw|nuw|exact|disjoint)[[:space:]]+)*" width "([^[:alnum:]_]|$)")) {
            count++
            if ($0 ~ /[[:space:]](nsw|nuw|exact|disjoint)[[:space:]]/) bad=1
        }
        if ($0 ~ /@moo_(add|sub|mul|div|mod|lshift|rshift)\(/) bad=1
        if ($0 ~ /[[:space:]]icmp[[:space:]]/) {
            icmp=1
            if ($0 ~ /,[[:space:]]*0([,[:space:]]|$)/) zero=1
            if ($0 ~ /,[[:space:]]*-1([,[:space:]]|$)/) minus=1
            if ($0 ~ /-9223372036854775808([^0-9]|$)/) min=1
            if ($0 ~ /,[[:space:]]*(63|64)([,[:space:]]|$)/) limit=1
        }
        if ($0 ~ /br[[:space:]]+i1[[:space:]]/) branch=1
    }
    END {
        if (name != "") fail(name ": Funktion nicht abgeschlossen")
        for (n in expected) if (!seen[n]) fail(n ": native Definition fehlt")
        if (errors) exit 1
        print "IR-Golden strukturell bestanden; LLVM-Verify und Laufzeitfehler bleiben Pflicht"
    }' "$1"
}
self_test() {
    mkdir -p build/p2-ir
    local dir name op width
    dir=$(mktemp -d build/p2-ir/golden-check.XXXXXXXX)
    trap 'rm -f -- "$dir/good.ll" "$dir/bad.ll"; rmdir -- "$dir"' RETURN
    for name in p2_add p2_sub p2_mul p2_neg p2_div p2_rem p2_shl p2_ashr p2_lshr; do
        case "$name" in
            p2_add) op=add; width=i8;; p2_sub|p2_neg) op=sub; width=i8;;
            p2_mul) op=mul; width=i8;; p2_div) op=sdiv; width=i64;;
            p2_rem) op=srem; width=i64;; p2_shl) op=shl; width=i64;;
            p2_ashr) op=ashr; width=i64;; p2_lshr) op=lshr; width=i64;;
        esac
        printf 'define %s @%s(%s %%a, %s %%b) {\nentry:\n' "$width" "$name" "$width" "$width" >> "$dir/good.ll"
        case "$name" in
            p2_div|p2_rem)
                printf '  %%z = icmp eq i64 %%b, 0\n  %%m = icmp eq i64 %%a, -9223372036854775808\n  %%n = icmp eq i64 %%b, -1\n  br i1 %%z, label %%fail, label %%ok\nok:\n' >> "$dir/good.ll";;
            p2_shl|p2_ashr|p2_lshr)
                printf '  %%z = icmp ult i64 %%b, 64\n  br i1 %%z, label %%fail, label %%ok\nok:\n' >> "$dir/good.ll";;
        esac
        printf '  %%r = %s %s %%a, %%b\n  ret %s %%r\n}\n' "$op" "$width" "$width" >> "$dir/good.ll"
    done
    check_ir "$dir/good.ll" >/dev/null
    sed 's/= add i8/= add nsw i8/' "$dir/good.ll" > "$dir/bad.ll"
    if check_ir "$dir/bad.ll" >/dev/null 2>&1; then echo "FAIL: nsw nicht erkannt" >&2; return 1; fi
    sed 's/icmp eq i64 %b, -1/icmp eq i64 %b, 1/g' "$dir/good.ll" > "$dir/bad.ll"
    if check_ir "$dir/bad.ll" >/dev/null 2>&1; then echo "FAIL: MIN/-1 nicht erkannt" >&2; return 1; fi
    sed 's/icmp ult i64 %b, 64/icmp ult i64 %b, 65/g' "$dir/good.ll" > "$dir/bad.ll"
    if check_ir "$dir/bad.ll" >/dev/null 2>&1; then echo "FAIL: Shiftgrenze nicht erkannt" >&2; return 1; fi
    echo "IR-Checker-Selbsttest: 4 Faelle bestanden (synthetisch, kein Compiler-Gate)"
}
case "${1:-}" in
    --self-test) test "$#" -eq 1; self_test ;;
    --check) test "$#" -eq 2; check_ir "$2" ;;
    "")
        mkdir -p build/p2-ir
        ir=$(mktemp build/p2-ir/native-golden.XXXXXXXX.ll)
        trap 'rm -f -- "$ir"' EXIT
        compiler/target/release/moo-compiler compile compiler/tests/p2/ir_native_arithmetik.moos --emit-ir -o "$ir"
        check_ir "$ir" ;;
    *) echo "Aufruf: $0 [--self-test | --check datei.ll]" >&2; exit 2 ;;
esac
