/*
 * moo_contrastive.c — KI-MULTI-L1: Kosinus-Aehnlichkeit und symmetrischer
 * CLIP/InfoNCE-Verlust als reine Komposition vorhandener Tensor-Registry-Ops.
 *
 * Absichtlich KEIN eigener Tape-Op und KEIN handgeschriebener Backward:
 * Autograd sieht mul/summe/adds/sqrt/div/reshape/transpose/matmul/divs/
 * logsoftmax/muls/add und differenziert die gesamte Komposition.
 */
#include "moo_runtime.h"
#include <math.h>

static MooTensor* kontrastiv_tensor(MooValue v) {
    return MV_TENSOR(v);
}

static bool kontrastiv_form_pruefen(MooValue a, MooValue b, const char* wo) {
    if (a.tag != MOO_TENSOR || b.tag != MOO_TENSOR) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: erwarte zwei Tensoren", wo);
        moo_throw(moo_error(msg));
        return false;
    }
    MooTensor* at = kontrastiv_tensor(a);
    MooTensor* bt = kontrastiv_tensor(b);
    if (at->ndim != 2 || bt->ndim != 2) {
        char msg[192];
        snprintf(msg, sizeof(msg),
                 "%s: beide Tensoren muessen 2D sein [batch, dim]", wo);
        moo_throw(moo_error(msg));
        return false;
    }
    if (at->shape[0] != bt->shape[0] || at->shape[1] != bt->shape[1]) {
        char msg[224];
        snprintf(msg, sizeof(msg),
                 "%s: beide Tensoren brauchen dieselbe Form [batch, dim] "
                 "(erhalten [%d,%d] und [%d,%d])",
                 wo, at->shape[0], at->shape[1], bt->shape[0], bt->shape[1]);
        moo_throw(moo_error(msg));
        return false;
    }
    if (at->shape[0] < 1 || at->shape[1] < 1) {
        char msg[160];
        snprintf(msg, sizeof(msg), "%s: batch und dim muessen positiv sein", wo);
        moo_throw(moo_error(msg));
        return false;
    }
    return true;
}

/* Zeilenweise L2-Normierung. 1e-12 verhindert 0/0 bei Nullvektoren. */
static MooValue kontrastiv_normieren(MooValue x) {
    MooValue q = moo_tensor_mul(x, x);
    MooValue s = q.tag == MOO_TENSOR
        ? moo_tensor_summe(q, moo_number(1.0)) : moo_none();
    MooValue e = s.tag == MOO_TENSOR
        ? moo_tensor_adds(s, moo_number(1e-12)) : moo_none();
    MooValue n = e.tag == MOO_TENSOR ? moo_tensor_sqrt(e) : moo_none();
    MooValue r = n.tag == MOO_TENSOR ? moo_tensor_div(x, n) : moo_none();
    moo_release(q);
    moo_release(s);
    moo_release(e);
    moo_release(n);
    return r;
}

/* CE(logits, diag(0..batch-1)) als bestehende Op-Komposition. */
static MooValue kontrastiv_diagonal_ce(MooValue logits, int32_t batch) {
    int32_t shape[2] = { batch, batch };
    MooTensor* oh = moo_tensor_raw(2, shape);
    if (!oh) return moo_none();
    for (int32_t i = 0; i < batch; i++)
        oh->data[(int64_t)i * batch + i] = 1.0f;
    MooValue onehot;
    onehot.tag = MOO_TENSOR;
    moo_val_set_ptr(&onehot, oh);

    MooValue ls = moo_tensor_logsoftmax(logits);
    MooValue p = ls.tag == MOO_TENSOR
        ? moo_tensor_mul(onehot, ls) : moo_none();
    MooValue s = p.tag == MOO_TENSOR
        ? moo_tensor_summe(p, moo_number(-1.0)) : moo_none();
    MooValue loss = s.tag == MOO_TENSOR
        ? moo_tensor_muls(s, moo_number(-1.0 / (double)batch)) : moo_none();

    moo_release(onehot);
    moo_release(ls);
    moo_release(p);
    moo_release(s);
    return loss;
}

/* kosinus(a,b) / cosine(a,b) -> [batch]. */
MooValue moo_nn_kosinus(MooValue a, MooValue b) {
    if (!kontrastiv_form_pruefen(a, b, "kosinus")) return moo_none();

    MooValue an = kontrastiv_normieren(a);
    MooValue bn = kontrastiv_normieren(b);
    MooValue p = (an.tag == MOO_TENSOR && bn.tag == MOO_TENSOR)
        ? moo_tensor_mul(an, bn) : moo_none();
    MooValue s = p.tag == MOO_TENSOR
        ? moo_tensor_summe(p, moo_number(1.0)) : moo_none();

    MooValue form = moo_list_new(1);
    moo_list_append(form, moo_number((double)kontrastiv_tensor(a)->shape[0]));
    MooValue out = s.tag == MOO_TENSOR
        ? moo_tensor_umformen(s, form) : moo_none();

    moo_release(an);
    moo_release(bn);
    moo_release(p);
    moo_release(s);
    moo_release(form);
    return out;
}

/* kontrastiv(a,b,temperatur?) / contrastive: symmetrisches CLIP-InfoNCE. */
MooValue moo_nn_kontrastiv(MooValue a, MooValue b, MooValue temperatur) {
    if (!kontrastiv_form_pruefen(a, b, "kontrastiv")) return moo_none();

    double temp = 0.07;
    if (temperatur.tag != MOO_NONE) {
        if (temperatur.tag != MOO_NUMBER || !isfinite(MV_NUM(temperatur)) ||
            MV_NUM(temperatur) < 1e-6) {
            moo_throw(moo_error(
                "kontrastiv: temperatur muss endlich und mindestens 0.000001 sein"));
            return moo_none();
        }
        temp = MV_NUM(temperatur);
    }

    MooValue an = kontrastiv_normieren(a);
    MooValue bn = kontrastiv_normieren(b);
    MooValue bt = bn.tag == MOO_TENSOR
        ? moo_tensor_transponieren(bn) : moo_none();
    MooValue mm = (an.tag == MOO_TENSOR && bt.tag == MOO_TENSOR)
        ? moo_tensor_matmul(an, bt) : moo_none();
    MooValue logits = mm.tag == MOO_TENSOR
        ? moo_tensor_divs(mm, moo_number(temp)) : moo_none();

    int32_t batch = kontrastiv_tensor(a)->shape[0];
    MooValue ab = logits.tag == MOO_TENSOR
        ? kontrastiv_diagonal_ce(logits, batch) : moo_none();
    MooValue logits_t = logits.tag == MOO_TENSOR
        ? moo_tensor_transponieren(logits) : moo_none();
    MooValue ba = logits_t.tag == MOO_TENSOR
        ? kontrastiv_diagonal_ce(logits_t, batch) : moo_none();
    MooValue sum = (ab.tag == MOO_TENSOR && ba.tag == MOO_TENSOR)
        ? moo_tensor_add(ab, ba) : moo_none();
    MooValue loss = sum.tag == MOO_TENSOR
        ? moo_tensor_muls(sum, moo_number(0.5)) : moo_none();

    moo_release(an);
    moo_release(bn);
    moo_release(bt);
    moo_release(mm);
    moo_release(logits);
    moo_release(ab);
    moo_release(logits_t);
    moo_release(ba);
    moo_release(sum);
    return loss;
}
