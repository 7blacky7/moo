/* P5: common method selection; methods own the actual training step. */
#include "moo_train_api.h"
#include "moo_sft.h"
#include "moo_distill.h"
#include "moo_dpo.h"
#include <string.h>

MooValue moo_train_methode(MooValue forward, MooValue daten, MooValue konfig) {
    if (konfig.tag!=MOO_DICT) {
        moo_throw(moo_error("trainiere_methode: Konfiguration muss ein Dict sein"));
        return moo_none();
    }
    MooValue name=moo_dict_get(konfig,moo_string_new("methode"));
    bool sft=name.tag==MOO_STRING && MV_STR(name)->length==3 &&
        !memcmp(MV_STR(name)->chars,"sft",3);
    bool korrektur=name.tag==MOO_STRING && MV_STR(name)->length==11 &&
        !memcmp(MV_STR(name)->chars,"korrekturen",11);
    bool distillation=name.tag==MOO_STRING && MV_STR(name)->length==12 &&
        !memcmp(MV_STR(name)->chars,"distillation",12);
    bool praeferenzen=name.tag==MOO_STRING && MV_STR(name)->length==12 &&
        !memcmp(MV_STR(name)->chars,"praeferenzen",12);
    moo_release(name);
    if (distillation) return moo_train_distillation(forward,daten,konfig);
    if (praeferenzen) return moo_train_praeferenzen(forward,daten,konfig);
    if (sft) return moo_train_sft(forward,daten,konfig);
    if (korrektur) return moo_train_korrekturen(forward,daten,konfig);
    moo_throw(moo_error("trainiere_methode: unbekannte oder nicht unterstuetzte Methode"));
    return moo_none();
}
