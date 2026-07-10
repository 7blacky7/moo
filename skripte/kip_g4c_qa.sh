#!/usr/bin/env bash
# ============================================================
# KIP-G4c-QA — unabhaengiges End-to-End-Gate (Task 8972d152, kip-daten).
# GPU-Training -> E2b-Checkpoint -> ECHTER Prozessneustart -> Restore CPU/GPU
# -> Weitertrainieren/Eval.
#
# Jede Phase (Referenzlauf/Train/Resume/Mismatch) ist ein EIGENER OS-Prozess
# (kein fork() im Testbinary noetig -- die Shell startet jede Phase separat).
# Das beweist Ueberleben eines ECHTEN Prozessneustarts (neuer Adressraum,
# neues Vulkan-Init, keine ueberlebenden globalen C-Statics) -- staerker als
# die bestehenden E2/E2b-Harnesse, die Kill+Resume im selben Prozess simulieren.
#
# Vertrag + Testmatrix: docs/kip/G4c-QA-contract.md
# Quelle: compiler/runtime/tests/test_g4c_e2e_qa.c
#
# GEPRUEFT (heute mit vorhandenen Hooks, kein Production-Wiring noetig):
#   [A] CPU: Parameter + Dropout-Zaehler + globaler Schritt ueberleben echten
#       Prozessneustart bit-identisch (cpu_ref N == cpu_train M + cpu_resume N-M).
#   [B] CPU: Versions-Mismatch (Negativ-Kontrolle) wirft ueber Prozessgrenze.
#   [C] GPU: Adam m/v/t + Parameter ueberleben echten Prozessneustart
#       bit-identisch (gpu_ref N == gpu_train K + gpu_resume_gpu N-K),
#       Residenztelemetrie cpu_fallbacks==0 waehrend aller Device-Ops.
#   [D] Cross-Device: GPU-Checkpoint ladbar + weitertrainierbar auf einem
#       Binary OHNE jede Vulkan-Bindung (gpu_resume_cpu), UND auf einem
#       Vulkan-gebundenen Binary -- gleicher Checkpoint, gleicher Restore-Pfad.
#
# PENDING (dokumentiert, nicht ausgefuehrt -- fehlender G4c-Produktionshook):
#   [E] Echter CPU<->GPU-Loss-Kurvenvergleich (moo_nn_vorwaerts GPU-resident).
#       Siehe gpu_vs_cpu_curve-Modus + docs/kip/G4c-QA-contract.md §4.
#
# Transparent skippbar: [C]/[D]/gpu_* ohne libvulkan -> SKIP (Exit 0).
# ============================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RT="$ROOT/compiler/runtime"
FAIL=0
PENDING=0

pass()    { echo "  ✅ $*"; }
fail()    { echo "  ❌ $*"; FAIL=1; }
skip()    { echo "  ⏭️  SKIP: $*"; }
pending() { echo "  🕓 PENDING: $*"; PENDING=1; }

have_vulkan() { [ -n "$(ldconfig -p 2>/dev/null | grep libvulkan || true)" ]; }

RES_LINE_RE='^RESULT '
field() { # field <line> <key> -> value (whitespace-getrennt key=value)
    local line="$1" key="$2"
    echo "$line" | tr ' ' '\n' | sed -n "s/^${key}=//p" | head -1
}

echo "== KIP-G4c-QA Gate =="
cd "$RT"

BIN_CPU="/tmp/kip_g4c_qa_bin_cpu"
BIN_VK="/tmp/kip_g4c_qa_bin_vk"
SRCS=(tests/test_g4c_e2e_qa.c moo_nn.c moo_nn_easy.c moo_json.c moo_tensor.c
      moo_tensor_ops.c moo_ki_gpu.c moo_autograd.c moo_memory.c moo_value.c
      moo_print.c moo_string.c moo_dict.c moo_list.c moo_ops.c)

echo "-- Build (CPU-Stub, kein MOO_HAS_VULKAN) --"
clang -std=gnu11 -O1 -I. -o "$BIN_CPU" "${SRCS[@]}" -lm
pass "Stub-Build ohne MOO_HAS_VULKAN kompiliert (CPU-Default-Pfad unveraendert nutzbar)"

VULKAN_OK=0
if have_vulkan; then
    echo "-- Build (mit MOO_HAS_VULKAN) --"
    if clang -std=gnu11 -O2 -DMOO_HAS_VULKAN -I. -o "$BIN_VK" "${SRCS[@]}" -lvulkan -lm; then
        VULKAN_OK=1
        pass "Vulkan-Build kompiliert"
    else
        fail "Vulkan-Build fehlgeschlagen (libvulkan vorhanden, Build brach ab)"
    fi
else
    skip "kein libvulkan installiert -- GPU-Teile [C]/[D] werden uebersprungen"
fi

# ------------------------------------------------------------
# [A]+[B] CPU-Pfad -- funktioniert IMMER (kein Vulkan noetig), echter
# Prozessneustart ueber 3 separate Aufrufe von $BIN_CPU.
# ------------------------------------------------------------
echo "-- [A] CPU: echter Prozessneustart, Parameter/Dropout/Schritt --"
CK_CPU="$(mktemp /tmp/g4c_qa_cpu_XXXXXX)"
REF_LINE="$("$BIN_CPU" cpu_ref 7)"
"$BIN_CPU" cpu_train "$CK_CPU" 4 >/dev/null
RESUME_LINE="$("$BIN_CPU" cpu_resume "$CK_CPU" 3)"

ref_chk="$(field "$REF_LINE" checksum)"; ref_loss="$(field "$REF_LINE" loss)"
ref_dz="$(field "$REF_LINE" dropz)";     ref_t="$(field "$REF_LINE" t)"
rs_chk="$(field "$RESUME_LINE" checksum)"; rs_loss="$(field "$RESUME_LINE" loss)"
rs_dz="$(field "$RESUME_LINE" dropz)";     rs_t="$(field "$RESUME_LINE" t)"

if [ "$ref_chk" = "$rs_chk" ] && [ "$ref_loss" = "$rs_loss" ] && \
   [ "$ref_dz" = "$rs_dz" ] && [ "$ref_t" = "$rs_t" ]; then
    pass "CPU Prozessneustart bit-identisch: loss=$ref_loss checksum=$ref_chk dropz=$ref_dz schritt=$ref_t"
else
    fail "CPU Prozessneustart WEICHT AB: ref=[$REF_LINE] resume=[$RESUME_LINE]"
fi

echo "-- [B] CPU: Negativ-Kontrolle Versions-Mismatch ueber Prozessgrenze --"
MM_LINE="$("$BIN_CPU" cpu_mismatch "$CK_CPU")"
if [ "$(field "$MM_LINE" status)" = "wirft-wie-erwartet" ]; then
    pass "Versions-Mismatch wirft erklaerend (neuer Prozess, frischer Fehlerzustand)"
else
    fail "Versions-Mismatch wirft NICHT: [$MM_LINE]"
fi
rm -f "$CK_CPU"

# ------------------------------------------------------------
# [C]+[D] GPU-Pfad -- nur mit Vulkan.
# ------------------------------------------------------------
if [ "$VULKAN_OK" = "1" ]; then
    echo "-- [C] GPU: echter Prozessneustart, Adam m/v/t + Residenztelemetrie --"
    CK_GPU="$(mktemp /tmp/g4c_qa_gpu_XXXXXX)"
    GREF_LINE="$("$BIN_VK" gpu_ref 7)"
    if [ "$(field "$GREF_LINE" status || true)" = "SKIP-kein-vulkan" ]; then
        skip "gpu_ref meldet keine GPU-Residenz zur Laufzeit (Treiber/Device-Init) — [C]/[D] uebersprungen"
    else
        "$BIN_VK" gpu_train "$CK_GPU" 4 >/dev/null
        GRES_LINE="$("$BIN_VK" gpu_resume_gpu "$CK_GPU" 3)"

        gref_chk="$(field "$GREF_LINE" checksum)"; gref_t="$(field "$GREF_LINE" t)"
        gres_chk="$(field "$GRES_LINE" checksum)"; gres_t="$(field "$GRES_LINE" t)"
        gres_fb="$(field "$GRES_LINE" cpu_fallbacks)"

        if [ "$gref_chk" = "$gres_chk" ] && [ "$gref_t" = "$gres_t" ]; then
            pass "GPU Prozessneustart bit-identisch: checksum=$gref_chk schritt=$gref_t (Adam elementweise -> deterministisch)"
        else
            fail "GPU Prozessneustart WEICHT AB: ref=[$GREF_LINE] resume=[$GRES_LINE]"
        fi
        if [ "$gres_fb" = "0" ]; then
            pass "Residenztelemetrie: cpu_fallbacks==0 waehrend GPU-Resume+Weitertraining"
        else
            fail "Residenztelemetrie: cpu_fallbacks=$gres_fb != 0 (versteckter CPU-Fallback im Device-Pfad)"
        fi

        echo "-- [D] Cross-Device: GPU-Checkpoint auf Vulkan- UND Nicht-Vulkan-Binary --"
        CROSS_VK_LINE="$("$BIN_VK" gpu_resume_cpu "$CK_GPU" 2)"
        CROSS_CPU_LINE="$("$BIN_CPU" gpu_resume_cpu "$CK_GPU" 2)"
        cv_chk="$(field "$CROSS_VK_LINE" checksum)"
        cc_chk="$(field "$CROSS_CPU_LINE" checksum)"
        if [ -n "$cv_chk" ] && [ "$cv_chk" = "$cc_chk" ]; then
            pass "Cross-Device-Restore identisch ob Vulkan gebunden ist oder nicht (checksum=$cv_chk) — echtes CPU-Weitertraining nach GPU-Checkpoint"
        else
            fail "Cross-Device-Restore WEICHT AB: vk-binary=[$CROSS_VK_LINE] cpu-only-binary=[$CROSS_CPU_LINE]"
        fi
    fi
    rm -f "$CK_GPU"
else
    skip "[C] GPU echter Prozessneustart (Adam m/v/t + Telemetrie)"
    skip "[D] Cross-Device-Restore (GPU-Checkpoint auf Nicht-Vulkan-Binary)"
fi

# ------------------------------------------------------------
# [E] Dokumentiert-PENDING: kein echter GPU-Forward/Backward-Kurvenvergleich
# moeglich, solange die G4c-Produktionsverdrahtung fehlt.
# ------------------------------------------------------------
echo "-- [E] CPU<->GPU-Loss-Kurvenvergleich --"
"$BIN_CPU" gpu_vs_cpu_curve >/dev/null
pending "echter GPU-Forward/Backward-Loss fehlt (kein moo_nn_vorwaerts-GPU-Routing) — siehe docs/kip/G4c-QA-contract.md §4, Modus gpu_vs_cpu_curve bleibt als Platzhalter bestehen"

echo
if [ "$FAIL" = "1" ]; then
    echo "== KIP-G4c-QA: FEHLGESCHLAGEN =="
    exit 1
fi
if [ "$PENDING" = "1" ]; then
    echo "== KIP-G4c-QA: GRUEN fuer alle heute pruefbaren Kriterien, [E] PENDING bis G4c-Wiring =="
else
    echo "== KIP-G4c-QA: VOLLSTAENDIG GRUEN =="
fi
exit 0
