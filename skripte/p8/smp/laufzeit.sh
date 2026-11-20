#!/usr/bin/env bash
# P10-T14: dieselben Quellen/Erwartungsdateien hosted und auf 1/4 CPUs.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_THREAD_OUT:-$root/build/p10-t14/gate}
mkdir -p "$out/hosted"
# Der bisherige Minimalpfad muss weiterhin genau die Atom-Diagnose liefern.
if "$compiler" compile compiler/tests/p2/rc_geteilt/atom_zaehler.moos --no-stdlib \
    --target x86_64-bare --emit-ir -o "$out/minimal.ll" >"$out/minimal.log" 2>&1; then
    echo "FAIL Atom ohne nativen Thread-Träger akzeptiert" >&2
    exit 1
fi
grep -F "'atom' gibt es nur gehostet" "$out/minimal.log"
for fixture in ${MOO_THREAD_FIXTURES:-atom_zaehler sperre_konto warten_mehrfach}; do
    case "$fixture" in atom_zaehler|sperre_konto|warten_mehrfach|spiel_kanal|starte_mehrere|starte_lambda_mehrere) ;; *) exit 2 ;; esac
    src="$root/compiler/tests/p2/rc_geteilt/$fixture.moos"
    expected="$root/compiler/tests/p2/rc_geteilt/$fixture.expected"
    "$compiler" compile "$src" -o "$out/hosted/$fixture" >"$out/hosted/$fixture.compile.log" 2>&1
    timeout 120 "$out/hosted/$fixture" >"$out/hosted/$fixture.out"
    diff -u "$expected" "$out/hosted/$fixture.out"
    echo "PASS $fixture hosted: .expected identisch"
    image=""
    for cpus in ${MOO_THREAD_CPUS:-1 4}; do
        case "$cpus" in 1|4) ;; *) exit 2 ;; esac
        dir="$out/$fixture-$cpus"
        mkdir -p "$dir"
        if [[ -z "$image" ]]; then
            MOO_COMPILER="$compiler" MOO_UEFI_PRUEFUNG=kern MOO_UEFI_KERN="$src" \
                MOO_UEFI_KERN_LAUFZEIT=1 MOO_UEFI_SMP="$cpus" MOO_UEFI_WAIT="${MOO_THREAD_WAIT:-90}" \
                MOO_UEFI_OUT="$dir" bash scripts/uefi-smoke.sh >"$dir/smoke.log" 2>&1 || {
                    tail -60 "$dir/smoke.log" >&2
                    exit 1
                }
            image="$dir"
            serial="$dir/serial.log"
        else
            # Weitere Topologien booten exakt dieselben ELF-/Imagebytes.
            MOO_SMP_OUT="$dir" MOO_SMP_FRIST="$(("${MOO_THREAD_WAIT:-90}" * 10))" \
                MOO_SMP_KVM="${MOO_UEFI_KVM:-0}" bash skripte/p8/smp/ap_start.sh "$image" 1 "$cpus" \
                >"$dir/smoke.log" 2>&1 || { tail -60 "$dir/smoke.log" >&2; exit 1; }
            serial="$dir/lauf-1/serial.log"
        fi
        awk '
            { sub(/\r$/, "") }
            $0 == "MOO-LAUFZEIT-BEGIN" { active=1; begin++; next }
            $0 == "MOO-LAUFZEIT-END" { active=0; finish++; next }
            $0 == "MOO-LAUFZEIT-FAIL" { bad=1; active=0; next }
            active { print }
            END { if (begin != 1 || finish != 1 || bad) exit 2 }
        ' "$serial" >"$dir/fixture.out"
        diff -u "$expected" "$dir/fixture.out"
        grep -aEx "MOO-AP-ONLINE $((cpus-1))[[:space:]]*" "$serial"
        grep -aEx "MOO-KERNE $cpus[[:space:]]*" "$serial"
        echo "PASS $fixture Kernel -smp $cpus: .expected identisch"
    done
done
