#ifndef MOO_SFT_H
#define MOO_SFT_H
#include "moo_runtime.h"
MooValue moo_train_sft(MooValue forward, MooValue daten, MooValue konfig);
MooValue moo_train_korrekturen(MooValue forward, MooValue daten, MooValue konfig);
#endif
