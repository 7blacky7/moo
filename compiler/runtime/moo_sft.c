/* P5: Maskiertes Next-Token-Training. Argumente borrowed, Ergebnisse owning. */
#include "moo_sft.h"
#include <math.h>
#include <string.h>

static MooValue sft_get(MooValue d, const char *k) {
    return moo_dict_get(d, moo_string_new(k));
}
static void sft_set(MooValue d, const char *k, MooValue v) {
    moo_dict_set(d, moo_string_new(k), v);
}
static MooValue sft_fail(const char *msg) {
    moo_throw(moo_error(msg));
    return moo_none();
}
static bool sft_int(MooValue v, int lo, int hi, int *out) {
    if (v.tag != MOO_NUMBER) return false;
    double n = MV_NUM(v);
    if (!isfinite(n) || n < lo || n > hi || floor(n) != n) return false;
    *out = (int)n;
    return true;
}
static bool sft_text(MooValue v) {
    return v.tag == MOO_STRING && MV_STR(v)->length > 0;
}
/* Zwei Formate: historisch Aufgabe/Original als Prompt; alternativ
 * expliziter Prompt + Korrektur in halb ueberlappenden Fenstern.
 * Zielpositionen werden im letzteren Format nur einmal maskiert. */
static bool sft_korrektur_tokenbezug(MooValue konfig, MooValue eintrag,
                                      MooValue aufgabe, MooValue original, MooValue korr,
                                      MooValue input, MooValue ziel, int seq,
                                      int used, int start) {
    MooValue encoder = sft_get(konfig, "encoder");
    if (encoder.tag != MOO_FUNC || !MV_FUNC(encoder)->fn_ptr || MV_FUNC(encoder)->arity != 1) {
        moo_release(encoder); return false;
    }
    MooValue prompt = sft_get(eintrag, "prompt");
    MooValue offsetv = sft_get(eintrag, "fenster_offset");
    bool rolling = prompt.tag != MOO_NONE || offsetv.tag != MOO_NONE;
    int offset = 0;
    if (rolling) {
        if (!sft_text(prompt) || !sft_int(offsetv, 0, 1000000, &offset)
            || offset % (seq / 2) != 0) {
            moo_release(encoder); moo_release(prompt); moo_release(offsetv); return false;
        }
    } else {
        moo_release(prompt);
        MooValue nl1 = moo_string_new("\n");
        MooValue a = moo_string_concat(aufgabe, nl1);
        MooValue b = moo_string_concat(a, original);
        MooValue nl2 = moo_string_new("\n");
        prompt = moo_string_concat(b, nl2);
        moo_release(nl1); moo_release(nl2); moo_release(a); moo_release(b);
    }
    moo_release(offsetv);
    /* Moo-Funktionen konsumieren ihre Argumentreferenz beim Return. */
    moo_retain(prompt);
    MooValue p = moo_func_call_1(encoder, prompt);
    MooValue t = moo_none();
    if (!moo_error_flag && p.tag == MOO_TENSOR) {
        moo_retain(korr);
        t = moo_func_call_1(encoder, korr);
    }
    bool ok = !moo_error_flag && p.tag == MOO_TENSOR && t.tag == MOO_TENSOR;
    if (ok) {
        MooTensor *pt = MV_TENSOR(p), *tt = MV_TENSOR(t);
        moo_tensor_f32_sichern(pt); moo_tensor_f32_sichern(tt);
        ok = pt->ndim == 1 && tt->ndim == 1 && pt->size > 0 && tt->size > 0
            && pt->size <= 1000000 && tt->size <= 1000000;
        if (ok) {
            int64_t total = pt->size + tt->size - 1;
            int64_t available = total - offset;
            int64_t expected_used = available < seq ? available : seq;
            int64_t earliest = pt->size - 1;
            if (rolling && offset > 0 && earliest < (int64_t)offset + seq - seq / 2)
                earliest = (int64_t)offset + seq - seq / 2;
            if (earliest < offset) earliest = offset;
            ok = offset < total && used == expected_used && start == earliest - offset
                && start < used;
            if (!rolling) ok = ok && offset == 0 && total == used;
            if (ok) for (int j = 0; j <= used; j++) {
                int64_t pos = (int64_t)offset + j;
                double expected = pos < pt->size ? pt->data[pos] : tt->data[pos - pt->size];
                if (!isfinite(expected) || floor(expected) != expected
                    || (j < used && MV_NUM(MV_LIST(input)->items[j]) != expected)
                    || (j > 0 && MV_NUM(MV_LIST(ziel)->items[j - 1]) != expected)) {
                    ok = false; break;
                }
            }
        }
    }
    moo_release(encoder); moo_release(prompt); moo_release(p); moo_release(t);
    return ok;
}
static bool sft_validate(MooValue daten, MooValue konfig, bool korrektur,
                         int *seq, int *vocab, int *steps, MooValue *examples) {
    if (daten.tag != MOO_DICT || konfig.tag != MOO_DICT) return false;
    MooValue n = sft_get(konfig, "sequenzlaenge");
    MooValue v = sft_get(konfig, "vokab");
    MooValue s = sft_get(konfig, "schritte");
    MooValue dt = sft_get(daten, "tokenizer_id");
    MooValue kt = sft_get(konfig, "tokenizer_id");
    *examples = sft_get(daten, "beispiele");
    bool ok = sft_int(n, 2, 4096, seq) && sft_int(v, 1, 16777216, vocab)
        && sft_int(s, 1, 1000000, steps) && sft_text(dt) && sft_text(kt)
        && MV_STR(dt)->length == MV_STR(kt)->length
        && memcmp(MV_STR(dt)->chars, MV_STR(kt)->chars, (size_t)MV_STR(dt)->length) == 0
        && examples->tag == MOO_LIST && MV_LIST(*examples)->length > 0;
    moo_release(n); moo_release(v); moo_release(s); moo_release(dt); moo_release(kt);
    if (!ok) return false;
    MooList *l = MV_LIST(*examples);
    for (int32_t i = 0; i < l->length && ok; i++) {
        MooValue e = l->items[i];
        if (e.tag != MOO_DICT) return false;
        MooValue in = sft_get(e, "input_ids"), target = sft_get(e, "target_ids");
        MooValue mask = sft_get(e, "loss_mask"), used = sft_get(e, "nutzlaenge");
        MooValue start = sft_get(e, "ziel_start");
        int u = 0, z = 0;
        ok = in.tag == MOO_LIST && target.tag == MOO_LIST && mask.tag == MOO_LIST
            && MV_LIST(in)->length == *seq && MV_LIST(target)->length == *seq
            && MV_LIST(mask)->length == *seq && sft_int(used, 1, *seq, &u)
            && sft_int(start, 0, u - 1, &z);
        if (ok) {
            for (int j = 0; j < *seq; j++) {
                int x, y, m;
                if (!sft_int(MV_LIST(in)->items[j], 0, *vocab - 1, &x)
                    || !sft_int(MV_LIST(target)->items[j], 0, *vocab - 1, &y)
                    || !sft_int(MV_LIST(mask)->items[j], 0, 1, &m)
                    || m != (j >= z && j < u)
                    || (j + 1 < u && MV_LIST(in)->items[j + 1].data != MV_LIST(target)->items[j].data)) {
                    ok = false; break;
                }
            }
        }
        if (korrektur && ok) {
            MooValue akzeptiert = sft_get(e, "akzeptiert");
            MooValue aufgabe = sft_get(e, "aufgabe"), original = sft_get(e, "original");
            MooValue korr = sft_get(e, "korrektur"), grund = sft_get(e, "begruendung");
            ok = akzeptiert.tag == MOO_BOOL && MV_BOOL(akzeptiert)
                && sft_text(aufgabe) && sft_text(original) && sft_text(korr) && sft_text(grund);
            if (ok) ok = sft_korrektur_tokenbezug(konfig, e, aufgabe, original, korr, in, target, *seq, u, z);
            moo_release(akzeptiert); moo_release(aufgabe); moo_release(original);
            moo_release(korr); moo_release(grund);
        }
        moo_release(in); moo_release(target); moo_release(mask); moo_release(used); moo_release(start);
    }
    return ok;
}
static MooValue sft_train(MooValue forward, MooValue daten, MooValue konfig, bool korrektur) {
    int seq, vocab, steps;
    MooValue examples = moo_none();
    if (forward.tag != MOO_FUNC || !MV_FUNC(forward)->fn_ptr || MV_FUNC(forward)->arity != 1 || !sft_validate(daten, konfig, korrektur, &seq, &vocab, &steps, &examples)) {
        moo_release(examples);
        return sft_fail("SFT: ungueltige Konfiguration oder Daten; Tokenizer, Sequenz, Zielmaske und Paare pruefen");
    }
    MooValue params = sft_get(konfig, "params");
    MooValue opt = sft_get(konfig, "opt");
    if (params.tag != MOO_LIST || MV_LIST(params)->length == 0) {
        moo_release(params); moo_release(opt); moo_release(examples);
        return sft_fail("SFT: params muss eine nichtleere Parameterliste sein");
    }
    if (opt.tag == MOO_NONE) {
        MooValue art = sft_get(konfig, "optimierer"), rate = sft_get(konfig, "rate");
        MooValue mom = sft_get(konfig, "momentum"), decay = sft_get(konfig, "decay");
        if (art.tag == MOO_STRING && rate.tag == MOO_NUMBER && isfinite(MV_NUM(rate)) && MV_NUM(rate) > 0) {
            if (!strcmp(MV_STR(art)->chars, "sgd")) opt = moo_nn_opt_sgd(params, rate, mom);
            else if (!strcmp(MV_STR(art)->chars, "adam")) opt = moo_nn_opt_adam(params, rate);
            else if (!strcmp(MV_STR(art)->chars, "adamw")) opt = moo_nn_opt_adamw(params, rate, decay);
        }
        moo_release(art); moo_release(rate); moo_release(mom); moo_release(decay);
    } else if (opt.tag == MOO_DICT) {
        MooValue op = sft_get(opt, "params");
        bool same = op.tag == MOO_LIST && MV_LIST(op)->length == MV_LIST(params)->length;
        if (same) for (int32_t i = 0; i < MV_LIST(op)->length; i++)
            if (MV_LIST(op)->items[i].tag != MOO_TENSOR ||
                MV_LIST(op)->items[i].data != MV_LIST(params)->items[i].data) { same = false; break; }
        moo_release(op);
        if (!same) { moo_release(opt); opt = moo_none(); }
    }
    if (opt.tag != MOO_DICT || moo_error_flag) {
        moo_release(params); moo_release(opt); moo_release(examples);
        if (moo_error_flag) return moo_none();
        return sft_fail("SFT: Optimierer ungueltig oder Parameter stimmen nicht ueberein");
    }
    bool was_on = moo_ag_ist_an();
    moo_ag_an();
    MooValue historie = moo_list_new(steps);
    MooList *l = MV_LIST(examples);
    bool failed = false;
    for (int i = 0; i < steps; i++) {
        MooValue e = l->items[i % l->length];
        MooValue il = sft_get(e, "input_ids"), zl = sft_get(e, "target_ids"), ml = sft_get(e, "loss_mask");
        MooValue in = moo_tensor_aus_liste(il), ziel = moo_tensor_aus_liste(zl), mask = moo_tensor_aus_liste(ml);
        moo_release(il); moo_release(zl); moo_release(ml);
        MooValue out = moo_none(), loss = moo_none();
        if (!moo_error_flag && in.tag == MOO_TENSOR && ziel.tag == MOO_TENSOR && mask.tag == MOO_TENSOR) {
            moo_retain(in); /* Callback konsumiert diese Referenz. */
            out = moo_func_call_1(forward, in);
        }
        if (!moo_error_flag && out.tag == MOO_TENSOR) {
            MooTensor *t = MV_TENSOR(out);
            if (t->ndim == 2 && t->shape[0] == seq && t->shape[1] == vocab)
                loss = moo_nn_kreuzentropie_maskiert(out, ziel, mask);
        }
        if (!moo_error_flag && loss.tag == MOO_TENSOR) {
            moo_tensor_f32_sichern(MV_TENSOR(loss));
            double wert = MV_TENSOR(loss)->data[0];
            if (isfinite(wert)) {
                moo_list_append(historie, moo_number(wert));
                moo_release(moo_tensor_rueckwaerts(loss));
                if (!moo_error_flag) moo_release(moo_nn_opt_schritt(opt));
            } else failed = true;
        } else failed = true;
        moo_release(in); moo_release(ziel); moo_release(mask); moo_release(out); moo_release(loss);
        if (moo_error_flag || failed) { failed = true; break; }
    }
    if (failed) moo_ag_reset();
    if (!was_on) moo_ag_aus();
    moo_release(params); moo_release(examples);
    if (failed) {
        moo_release(opt); moo_release(historie);
        if (moo_error_flag) return moo_none();
        return sft_fail("SFT: Vorwaertslauf oder Verlust ungueltig");
    }
    MooValue result = moo_dict_new();
    sft_set(result, "opt", opt);
    sft_set(result, "verluste", historie);
    sft_set(result, "schritte", moo_number(steps));
    return result;
}
MooValue moo_train_sft(MooValue forward, MooValue daten, MooValue konfig) {
    return sft_train(forward, daten, konfig, false);
}
MooValue moo_train_korrekturen(MooValue forward, MooValue daten, MooValue konfig) {
    return sft_train(forward, daten, konfig, true);
}
