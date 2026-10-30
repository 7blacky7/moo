#ifndef MOO_DPO_H
#define MOO_DPO_H
#include "moo_runtime.h"
/* Log-Wahrscheinlichkeiten der Antwort als Summen; Referenzwerte sind Konstanten. */
MooValue moo_dpo_loss(MooValue gewaehlt, MooValue verworfen, double ref_gewaehlt, double ref_verworfen, double beta);
MooValue moo_train_praeferenzen(MooValue forward, MooValue daten, MooValue konfig);
#endif
