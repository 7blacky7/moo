#ifndef MOO_DISTILL_H
#define MOO_DISTILL_H
#include "moo_runtime.h"
/* Geliehene Tensoren; Ergebnis owning. KL(Lehrer || Schueler) * T^2,
 * gemittelt ueber aktive Zeilen. Der Lehrer ist immer gradientenlos. */
MooValue moo_distill_kl(MooValue schueler, MooValue lehrer, MooValue maske, double temperatur);
MooValue moo_train_distillation(MooValue forward, MooValue daten, MooValue konfig);
#endif
