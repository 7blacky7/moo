# Moo-Langtextvergleich – Auswertung und vollständige Textproben

**Stand: 30. Oktober 2026. Alle 60 Inferenzproben abgeschlossen; kein neues Training.**

## Mein Ergebnis

**Die längeren Texte bestätigen noch keine überzeugende Verbesserung des Deutschen durch das kurze Mischtraining.** Sampling erzeugt abwechslungsreichere Fortsetzungen, doch in der qualitativen Sichtung bleiben Wortbildungsfehler, fehlerhafte Satzanschlüsse und fehlender Zusammenhang deutlich. Mehr Abwechslung ist hier nicht gleichbedeutend mit besserer Grammatik.

Original, SGD, Adam und AdamW wurden mit denselben drei Satzanfängen getestet. Pro Probe entstanden 200 neue Tokens: Greedy einmal und Top-k=20 bei Temperatur 0,6 beziehungsweise 0,9 jeweils mit Seeds 11 und 23. Das ergibt 15 Proben pro Modell, 60 insgesamt. Die vollständigen Ausgaben folgen unten, einschließlich Leerraum, Wiederholungen und Fehlern.

### Konkrete Beobachtungen

**Adam und AdamW sind bei allen 15 entsprechenden Proben byte-identisch.** Dies wurde anhand der SHA-256-Werte der erzeugten Bytefolgen geprüft, nicht nur anhand des sichtbaren Texts. Unterschiedliche Checkpoint-Hashes stehen im Rohbericht. In diesem Versuch gibt es daher keinen sichtbaren Textqualitätsvorteil von AdamW gegenüber Adam.

**Die längeren Ausgaben zeigen Unterschiede, die der kurze Test verdeckt hat.** SGD und Original hatten zuvor dieselben drei 30-Token-Fortsetzungen. Bei 200 Tokens weicht nun die Fortsetzung von „Der König “ ab; die beiden anderen Greedy-Fortsetzungen bleiben identisch. Das ist eine Verhaltensänderung, noch kein Qualitätsgewinn.

**Greedy gerät teilweise in lange Leerzeichenfolgen.** Besonders sichtbar ist das bei Original und SGD nach „Im Schloss “ sowie bei Adam und AdamW nach „Der König “. Die hohen Greedy-Wiederholungswerte werden deshalb teilweise durch Leerraum geprägt; sie dürfen nicht als reine Wortwiederholungsrate interpretiert werden.

**49 von 60 Proben sind gültiges UTF-8; 11 sind es nicht.** Alle zwölf Greedy-Proben waren gültig. Bei Temperatur 0,6 waren 20 von 24 Proben gültig, bei 0,9 waren es 17 von 24. In der Textansicht werden ungültige Bytes als `\xNN` angezeigt. Sie wurden weder entfernt noch in vermeintlich richtige Wörter verwandelt. Die exakten Bytefolgen sind zusätzlich im Projekt gesichert. Der Befund betrifft die frei generierten Ausgaben und beweist für sich keinen Fehler beim Encode/Decode-Roundtrip des Tokenizers.

| Modell | Gültige UTF-8-Proben | Ungültige Proben |
|---|---:|---:|
| Original | 12 / 15 | 3 |
| SGD | 13 / 15 | 2 |
| Adam | 12 / 15 | 3 |
| AdamW | 12 / 15 | 3 |

## Einordnung

Die Modelle bilden erkennbare Märchenwörter und gelegentlich verständliche kurze Wendungen. In diesen längeren Proben entsteht aber kein zuverlässig zusammenhängender, grammatisch korrekter Absatz. Das ist meine qualitative Bewertung der hier abgedruckten Texte, keine automatische Grammatikmessung. Ich leite daraus weder einen allgemeinen Sieg eines Optimierers noch ein Scheitern der Silbenidee ab.

Es wurde nur die Ausgabeauswahl verändert, nicht das Modell weitertrainiert. Drei bekannte Prompts und zwei Sampling-Seeds ersetzen keinen unabhängigen Sprachbenchmark. Das Kontextfenster bleibt bei 64 Tokens, auch wenn 200 neue Tokens erzeugt werden. Für einen weiteren Entwicklungsversuch wären die auffälligen Leerzeichenfolgen und die ungültigen Byteausgaben konkrete Prüfstellen; hier wurde daran nichts repariert.

## Nachweis und Ablage

Synapse-Job: `847fc96f-e12c-4b97-bdfc-c0d7c9b1a838`, regulär beendet mit Exit-Code 0. Inferenzlauf am 30.10.2026 von 18:32:47 bis 18:33:27 UTC, entsprechend etwa 19:32–19:33 Uhr in Deutschland. Alle vier Checkpoints sowie Tokenizer, Sampler und Eingabekonfiguration wurden auf Identität geprüft; die Checkpoints blieben unverändert.

Die Originaldaten liegen im Moo-Projekt unter `build/p5_langtext_20261030T183247_434841/`. Der folgende maschinell erzeugte Rohbericht wurde beim Export bytegenau gegen seine Projekt-SHA-256 geprüft: `91620af407e677e90f984b54360c5a4e56085dc2f1e116aac4ffdf30756d42ad`. Alle Texte sind tatsächlich von Moo erzeugt; die Beurteilung in diesem Vorspann stammt von mir.

---

# Moo: Längere Textgenerierung nach dem Mischtraining

Durchgeführt: 2026-10-30T18:32:47.983841+00:00 (UTC).

## Versuchsaufbau

Verglichen werden der vorhandene SIL-14-Checkpoint und die drei vorhandenen Mischtraining-Checkpoints (SGD, Adam, AdamW). Keine neuen Gewichtsänderungen.

- Drei identische Satzanfänge: `Der König `, `Die Kinder `, `Im Schloss `.
- Jeweils 200 neu erzeugte Tokens statt der bisherigen 30. Kein Abbruch am ersten Satzzeichen, kein Abschneiden nach 120 Bytes.
- Greedy sowie Top-k=20 bei Temperatur 0,6 und 0,9; die beiden Sampling-Varianten jeweils mit Seeds 11 und 23.
- Vier Modelle × drei Prompts × fünf Einstellungen/Seeds = 60 vollständige Fortsetzungen.
- Inferenz seriell auf CPU. Kontextfenster bleibt 64 Tokens; längere Ausgabe erweitert nicht das Kontextfenster.
- PRNG: derselbe deterministische LCG48271 wie im vorhandenen P5-Sampler. Seed wird je Fortsetzung zurückgesetzt.
- Keine Rechtschreibkorrektur, kein Nachschreiben, keine Wiederholungsstrafe und kein Filtern einzelner Token-IDs.
- Unzulässige UTF-8-Bytefolgen erscheinen in der Anzeige als `\xNN`. Originalbytes und Token-IDs sind zusätzlich unverändert in den JSON-Dateien bzw. `.bin`-Dateien enthalten.

## Messübersicht

Der Anteil wiederholter Token-4-Gramme ist eine rein beschreibende Wiederholungskennzahl, keine Grammatik- oder Verständnisnote.

| Modell | Modus | Proben | UTF-8 gültig | Mittlere Bytes | Wiederholte Token-4-Gramme |
|---|---|---:|---:|---:|---:|
| Original | Greedy | 3 | 3/3 | 267.0 | 27.7 % |
| Original | Top-k 20, T=0.6 | 6 | 5/6 | 284.3 | 4.1 % |
| Original | Top-k 20, T=0.9 | 6 | 4/6 | 299.3 | 1.2 % |
| SGD | Greedy | 3 | 3/3 | 269.3 | 28.3 % |
| SGD | Top-k 20, T=0.6 | 6 | 5/6 | 294.7 | 1.5 % |
| SGD | Top-k 20, T=0.9 | 6 | 5/6 | 299.5 | 2.9 % |
| Adam | Greedy | 3 | 3/3 | 281.3 | 18.6 % |
| Adam | Top-k 20, T=0.6 | 6 | 5/6 | 288.5 | 2.5 % |
| Adam | Top-k 20, T=0.9 | 6 | 4/6 | 294.3 | 1.4 % |
| AdamW | Greedy | 3 | 3/3 | 281.3 | 18.6 % |
| AdamW | Top-k 20, T=0.6 | 6 | 5/6 | 288.5 | 2.5 % |
| AdamW | Top-k 20, T=0.9 | 6 | 4/6 | 294.3 | 1.4 % |

## Vollständige Originalausgaben

Der Prompt steht getrennt darüber; der folgende Codeblock enthält ausschließlich die generierte Fortsetzung. Abgebrochene Wörter am Ende ergeben sich aus der festen Tokenobergrenze.

### Original – Schritt 36000

Checkpoint: `beispiele/p4_silben/run_sil14_grimm36k/full.mook`

SHA-256: `2edc53610bd0207563540c0941b67fb5bf63f3ca70442099a78dd4b56ad6ca2d`

#### Probe 01: Greedy · Seed 1

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 282 Bytes · **UTF-8 gültig:** ja

````text
aber fein. Als es gesallten die Königin in einen Schneimmel und als die
Schüststeckten sie sich auf dem Weg steil steine Krüchen und hoßte sie, »Hei mich mit seiner
gehalt, daß du die Heinsttell in der Stein und hühle, die Königin hätte sie
Kaufmann aus, so streick ihm die
````

#### Probe 02: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 268 Bytes · **UTF-8 gültig:** ja

````text
aber so lieb's. Da hief er schrachte, daß er auf auf den
Finger.

Endtet dabald, daß es zerwig sie Kunnimm, und wenn ich habe will dir Bildnimnich abmacht,
als du mir er einer schnist, als du die Wandstorde und die Braumem zuranhst.

Einen                         
 
````

#### Probe 03: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 301 Bytes · **UTF-8 gültig:** ja

````text
aber sah die Feuer dablickne nicht ge füünfte.

Als der Wolf war die Schusegkte lopfz, tier aber die fall lte, daß er es
die Geiß und Einen ich nicht will dichkehrte.« Aber der treue
bei sprach: »Meinuch bei der Winter, als es die Sonne Krauben zuropfen, da es
und solst es zu den Baucht und die
````

#### Probe 04: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 320 Bytes · **UTF-8 gültig:** nein

````text
aber die Feuermisehen und wollte den Karbe zu Tochter er
wieder von, der einem Mann zu lickcho\xb6he gen. « Wieken, die Waschlechte
herausschlagenzten konnte, die zährte das Mädchen gechitte, als sie an und
spich ihm die Hering darin und Spiellde und demten an die
Frußmen sienen Astrbl. Die Königstochter zu herkonnte
````

#### Probe 05: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 286 Bytes · **UTF-8 gültig:** nein

````text
aber
gleis die Tür und sichsch hief, die damit den Schlülsel
ausspeckngt. Als eralnn eine Frau, als ich alle Schusech derben
in Heilft und näche es\xbb\xa4nkwann, als du heben lanner nicht, ich's haben, daß
färörnglichen Bließ daf und als er es anziem, daß es den Bluchte
in seimei Lu
````

#### Probe 06: Greedy · Seed 1

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 306 Bytes · **UTF-8 gültig:** ja

````text
und sich ff die Jungfrau Maria und sprach: »Wer steill st du mit
und schüpfte den Fußer aus, und als es ein Stimme, so ich tein damit
dein Herz gehaugehraufen und die Königin einen Schneim, und der König
hatte ihm die Schustermer und lag nicht ablein und die Seifel und die Spielte und spielte die Sti
````

#### Probe 07: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 291 Bytes · **UTF-8 gültig:** ja

````text
und stecht, wenn es nach als Kopf auf
und es schnit, kam er wie ein Mes herankam, tand, da es slag
in die Händelge, daß er das Rusten sah, so fug er hatte sich den
Bandere und ist het gesaure Herzen, und wollte
mit ihm nicht reing aus dem Schlagten sie und schnitte, so sahs nitllit den
ge
````

#### Probe 08: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 305 Bytes · **UTF-8 gültig:** ja

````text
und richten es nicht
auf, und weil er seinen Eckeiner ein Antackte, wie er es sich
hinten, und ist vorwinglören und rief: »Herind g ische, so
st du lesen hätten, daß du der König steis und dert in das Herze bauter die
hen Staubesstraufansten. Der König aber kaufte sich die Maria sein
und Herz hait 
````

#### Probe 09: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 299 Bytes · **UTF-8 gültig:** ja

````text
und rugel es ein Zuuterssere, ich will dir
deiner KniHichtoch ld sprachen.«
Du suchr einfauten, suchte die Königin die verbote Kinder und
trtrckeigel zuechrten sich die betrauttete schück, so der örmen in der
Sohn. Als sie ein Kind ein das Pechterds aus.


Das allein traten und einen sprechen: 
````

#### Probe 10: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 261 Bytes · **UTF-8 gültig:** ja

````text
und richtefesten ein Brot Paes schwarzen Perappendern stdickel an und waußmen
föör die Wungsungst, so nernlein besolllzgest lolte, das
stehen Stelvkte lär; mätte ihm: »Has damit be es
verfuhl den Schüssel.« Sie lache sah, daß sie
die goldene  Au. »Ach
````

#### Probe 11: Greedy · Seed 1

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 213 Bytes · **UTF-8 gültig:** ja

````text
nahn, so nursegte es in seine
Kammer reicht und warm Schin mit.

                                                                                                                                                    
````

#### Probe 12: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 288 Bytes · **UTF-8 gültig:** ja

````text
nörchen alle
Schäften, ohte das Leben, daß es nicht solf, und das strichte, so erzichahte sie nicht. Es schnier ein Licht
hervor, der zurähtte das Gefall geschonnert, die es grachte, es die man in
die Heinricht zu den Halfteiß, und die Türe gerachen und als es angesanden
hatte, der
````

#### Probe 13: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 253 Bytes · **UTF-8 gültig:** nein

````text
ners Sündagmor
sse sök to to segggstein, so nehübte ihm die Sostot und hall die
Himme deckte ersprach: »Wenn stül\xab steck abgefallen, so frang es zum Tieusties ander Tuerst
zu, so liliegssen war, als sie ihm die Schuster schwand
ker, so nars nahmen 
````

#### Probe 14: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 324 Bytes · **UTF-8 gültig:** ja

````text
neggen, da fährtem und
wollte nuhen optzigen, das bund wieder mit den Sokter hinauf waren. Es wanegel
Königs, der ein, und ging sich hineinnochat hatten zu sich ihn über
wie das Kopf aus dem Huten sie; der getreue Johannes und sand hatte sie:
»Ich habe er, bin es menlahste, und so ging Kind geweillt es hinaufsckeul zu 
````

#### Probe 15: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 306 Bytes · **UTF-8 gültig:** ja

````text
nuchte sie auf und
ihm schenken der Schob die Frau gesanden und war froßt die Jungfrau Maria zu sprach: »Der bin
König'stugeschwar, daß du die müsag du nicht, mir
sichscht, du die Pfertermal, daß den es versech golle, so warzehn von Will ligt lossen.« Als die
ersprach: »Wer ich bin gehalten, keit 
````

### SGD – Schritt 36048

Checkpoint: `build/p5_grimm_mix_20261030/mix-sgd.mook`

SHA-256: `86b05d72d8312157430d6b465bcdc9d723ef913ca6d1ea2c65612ab3833e1f09`

#### Probe 16: Greedy · Seed 1

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 289 Bytes · **UTF-8 gültig:** ja

````text
aber fein. Als es gesallten die Königin in einen Schneimmel und als die
Schüststeckten sie sich auf dem Weg steil steine Krüchen und hoßte sie, »Hei mich mit seiner
gehalt, daß du nicht mehr wuöör den Hecht steckt ausgehen, so kleickte es sich an den
geschlafen, daß du die Himmel
````

#### Probe 17: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 284 Bytes · **UTF-8 gültig:** ja

````text
aber so lieb's. Da hief er schwes verges du mir
Küchen?« -- »O« segt
sich die Tot vel stieben, « antwortete er, »im,
der streine Geiß ge und rief: »Weil du mit getagen dir
verschwunder, so st siene von Rauft.« Das Mädchen Mädchen tider und sprich
und fand, was sie zusammen
````

#### Probe 18: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 293 Bytes · **UTF-8 gültig:** nein

````text
aber sah die Feuer dablickne nicht ge füünfte. Es war
blieg sie in die Oäfter, das netlich zu halten sie dem geben jungehen, daß es
fuchr aufen wie es ein Schifff tel damit be,« und sprach ihr
die Nachbaben: \xb6ragf dabür er und sprach: »Wer stich schif geführt und der
rühtig bei dem S
````

#### Probe 19: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 293 Bytes · **UTF-8 gültig:** nein

````text
aber die Feuermisehen und wollte den Karbe zu Tochter er
wieder von, der einem Mann zu lickcho\xb6he gen. « Wieken, die Waschlechte mit
eine feis und ließ Przeigt, als es du liegust mit dem Schiff von
er Feuer hinab. Die
Gefämme?« Die Waumter sich
vorhen, als das siesehen Bigtz ein, sie den 
````

#### Probe 20: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 297 Bytes · **UTF-8 gültig:** ja

````text
aber
gleis die Tür und sichsch friesch hatte, so will mit mir abkalte der dritte,
daß es es so liebtochter, mpich must du die Schließenststecht.« Da
es frief gten sie in ihm, so limmergeht und dachte sich über aus dem Weg es
ihm nicht, bis so weg er sein rüb: « segstüüßt Kind abgefalle 
````

#### Probe 21: Greedy · Seed 1

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 306 Bytes · **UTF-8 gültig:** ja

````text
und sich ff die Jungfrau Maria und sprach: »Wer steill st du mit
und schüpfte den Fußer aus, und als es ein Stimme, so ich tein damit
dein Herz gehaugehraufen und die Königin einen Schneim, und der König
hatte ihm die Schustermer und lag nicht ablein und die Seifel und die Spielte und spielte die Sti
````

#### Probe 22: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 303 Bytes · **UTF-8 gültig:** ja

````text
und stecht, wenn es nach als Kopf auf
und es schnit, kam er wie ein Mes herankam, treck da er
sich bedat t der Treur hinge, und die verfloßen siene
floste mit Küchenjungen und sprechen Stümle und lagte sie in seinem Volllleröt
Bee und machte Lann von Gold
zurätltest, als ihr die Heinrd davon Gold 
````

#### Probe 23: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 337 Bytes · **UTF-8 gültig:** ja

````text
und richten es nicht
auf, und weil er es nicht ein Geißlein und machte ihm so
weworin einen, und hätte das Gewalt, und der König sprachen: »Ich habe verhoßen,« und die
war ihr die Türe Kind und die freichereten, und einmal gesand hatte, der war und sich
alles zugewunderte
und sprachen. Da als er sie schnester Mand wieder andern,
````

#### Probe 24: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 301 Bytes · **UTF-8 gültig:** ja

````text
und rugel es ein Zuuterssere, ich will dir
deiner KniHichtoch ld sprachen.«
Du suchr einfauten, suchte die Königin die schönen
Tinen fles, und der Königin die Hand zu trüßen und
von Kuchte die Alte und sprach ihr: »Ach, ich geracht, so gart will te
du: du bist die Liebgen: »Dat hendstat du, s
````

#### Probe 25: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 301 Bytes · **UTF-8 gültig:** ja

````text
und richtefesten ein Brot Paes schwarzen Perapfen. Wie
 sie so flüngst und wollte sich die Männnerchen hispringt. Und als der
Tochter als er eine Zucklein keitter. Es friebeten, als ihr sein Kind zur
König Haus und stäre Stter, und die Brad zu palten sich den
frögen nicht getöben.


           
````

#### Probe 26: Greedy · Seed 1

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 213 Bytes · **UTF-8 gültig:** ja

````text
nahn, so nursegte es in seine
Kammer reicht und warm Schin mit.

                                                                                                                                                    
````

#### Probe 27: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 299 Bytes · **UTF-8 gültig:** ja

````text
nörchen alle
Schäften, ohte das Leben, daß es sobte ihm die Schneiff ge
gehen, und war es seinem Marß, so stinter hod die Pfote Schneifmman,
dazu hrück  ihm Brot, das sie helte zu der treue Johannes
schur und die Alte von schwir von dem einem Willler.« Da sprach
sie die Königin wieder eine K
````

#### Probe 28: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 252 Bytes · **UTF-8 gültig:** ja

````text
ners Sündagmor
sse sök to to segggstein, so nehmen weg s nör em en dröt
Swinegel aver let het Fro: »Ick bün all.« De Haas lad leustop nich an
unsereein, antworte sie, hieß ihm im Wimmmel und hort. Augen
End st lood lst er schall stechte ihm die
````

#### Probe 29: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 317 Bytes · **UTF-8 gültig:** ja

````text
negten, sie andern
die Brunnen törne zu ihnen ränglein und sollen in die Himmmmsesen und gabe, wortete ihr einen Kohlen.

Da rief: »Könidlich darer ihm, daß es ihn einen haunen und wenn Laute seine
Stieh und rauten sichstraut und die Jungfrau nahm be
aber sah die Heimnerrn auf,« schleprd sagte: »Hie ich dich 
````

#### Probe 30: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 288 Bytes · **UTF-8 gültig:** ja

````text
nuchte sie auf und
ihm schenken der Schob die Frau gesanden und war frore spach
einen Da holkeich das Glanz, so kehrte sie alle die Handfrißtießen und fiel.
Die Wasprück waschenroden Swire gen Hecke zusteln aufschlitsteren Strööm, ren alnegel Höle hatten
Stöhm und windlich war hol
````

### Adam – Schritt 36048

Checkpoint: `build/p5_grimm_mix_20261030/mix-adam.mook`

SHA-256: `16bbc4542c35d59f8a8939ce7752094f37b792161e4f77ade08a325caa0a5af9`

#### Probe 31: Greedy · Seed 1

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 248 Bytes · **UTF-8 gültig:** ja

````text
aber feinte und strinken sie in den Wald und hertig auf dem Schweißlein
ster Schwanzt von Herzen seill an die Hinftel. Dir wingunst du mit
in den Feuertenwes, Stühle mit den Schnee weg, nah
                                                        
````

#### Probe 32: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 263 Bytes · **UTF-8 gültig:** nein

````text
aber so liebt, daß du die Bepach siehs mir auf und aß ihn
bei dem könntet, und die Königin tanzt aus schlon hlein, so kerst
dem an?   erz\xc3eidder Schlafen friß. »Er schluchte ein Mannd von
Weckt ihm im Schlör.« -- »Nuch ich h die Angers, so daß sie finge
````

#### Probe 33: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 303 Bytes · **UTF-8 gültig:** ja

````text
aber keine Aliste ihr eine von Rechteick, seht
und sie steine Krück flopfte und sprach: »Macht versonnt du mit
die rechteserne Kopf ausstück davon den Kopppln.« Das Mädchen
soll sein und einen tegnen und wenn er Hälte den Schnefr und sagte sie zum Knaman und hollen die Jungfrau Maria und sprach:

````

#### Probe 34: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 308 Bytes · **UTF-8 gültig:** ja

````text
aber verbote sich, könig ohsagt, so will ich
dein treuen treuesterlen Johannes dem Kropf
zu, und weil du der ge König damit freuten. Als sie eine Naunn aussu und
einfört: trechten es ein Pfote die Stiel an und freute ihm
mit ihrem Heindsmal, davon den Leingenbl hinein, wenn er kalliebs
alte Süste höhe 
````

#### Probe 35: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 306 Bytes · **UTF-8 gültig:** ja

````text
aber hernigin tanzt, da sie zog, wie er sich ich
mit mir auf große Nochmön: er an, »stes ist bei
Körenst, wie ein Mutter ihm die Stief Liel
zu auf die Frau Balg zum Kächen und dachte nicht
konnte. Da Hockufannste die Wolte es freute sein. Die Königstochter sich folg, die
  künte und sprang aus dem 
````

#### Probe 36: Greedy · Seed 1

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 280 Bytes · **UTF-8 gültig:** ja

````text
und sich ff in die Schuhe und die Schuhe so viel sie in
die Herzen fiel zu, und als er das Schneif und sprang sie
schnell zu alle Stück und sprachen die Schuhe und die Schürestes Stein gesprachen hatte, und der
gand in die Schuhe gehen die Königin in einen Schlafen friefen zu 
````

#### Probe 37: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 317 Bytes · **UTF-8 gültig:** ja

````text
und steckte es sich über der
Lauft und herin mit seine Kleichen. Da lich sagten die Königstochte lag und
sagte, es aber der treue Johannes zu wingscht, so nelte es in seine Wasser
gewesen war und rief:

  »Was mir geraufen: enst du mir, du nicht bist: der Johannes te.»Der Heimm schell du mit den
Rech machen, als
````

#### Probe 38: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 290 Bytes · **UTF-8 gültig:** ja

````text
und richtet zu stand und hied in die Braut in
Stein.« -- Der Mädchen scheit
und sprach: »Jet minen ich habe gerachtert,« und die Jungfrau Maria die
woben s kam, sie zu sich auf belebltene in die Erschnim, so ich erselbst, wenn du mit miel
diene Kuter bestrichen.« Da führten der junge
````

#### Probe 39: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 305 Bytes · **UTF-8 gültig:** nein

````text
und rusten es nicht, und die nahm len das
eine Feuheben gescheiche und verlangssen es war, so w\xc3verrmechte die Einlein
alles mit Topt, das belte zu halte, der sprachen und frieden
ster an den andern Morgen, keh da und er sprachen die Stankrochen und sagte un
in das Felte gebüm lilt. Wie die König schon
````

#### Probe 40: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 276 Bytes · **UTF-8 gültig:** nein

````text
und richtefes, Stiese nein der Wolf,« und verboote die Tür Wasser
und friefen zu und ihnen lebte besticht holen. Da llegte es drug alles leifen an und
unsen mit diekehweit gemüähe war, k warnf ein Scharze armen
St\xc3ne fiänge Tagt narst du; will mit den Becker gefreicht mi
````

#### Probe 41: Greedy · Seed 1

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 316 Bytes · **UTF-8 gültig:** ja

````text
nahn holte sich auf den Weg an und ren st steillte und
schieg ihm die Steine Krüchen und sprachen die Schuhe und die Schüsselten sich herum
und hinein, so legte es sich an den Körbchen auf, und der König sprachen. Der König sprach die Königin einen hohn, der König stellte und rief:
 »Was du nicht getan? Kö
````

#### Probe 42: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 293 Bytes · **UTF-8 gültig:** ja

````text
nelll hil mir dein
und vechrrriefen und steine damit be und die Brieft
werden. Da kunst du in das Mädchen zu zum
drittenmal zum Tinken vor Kusckeins vor Eseis Stück hinle und die Feuerblat ge. Es war zernig die Jungfrau Maria wieder zu zweite Serschückte und wider
sprechen.


Du           
````

#### Probe 43: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 265 Bytes · **UTF-8 gültig:** ja

````text
negt du mit den Bristeg an der Haupft und wel jeden Tr Wan scheit Steln un in
Eneahrten ein Stüs vor Schnei Süchschereinen Kopf hart i und Herzen hinein, mwindem
Stman, und der Sühle gefürfen hatte. Aber der König sprach zu schlichte:
»Wie, was ich gehübüng
````

#### Probe 44: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 274 Bytes · **UTF-8 gültig:** ja

````text
nug stolm (du bist versost
vor  Ihwß rehgebloied, wenn du dein Lugeh, men ja sick
denmal dlich.« Da führten es allen und rief:
te »War bin ich sang nicht, du sondern einklopfte den Hönnerwören un sehas de Lusch
ah.« Da böreem sunt de Brad den. Da sprach spiellte de:
````

#### Probe 45: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 297 Bytes · **UTF-8 gültig:** ja

````text
nuchte sie auf und fingeim nachnen lieg zum Antogen Tochter aber die
let, und nickte es in seiner Raghließ herab. Er sprang sie nicht, da seilbs in die
Wanden Füßen den benrnlich heras, so wie erbleich
vorblden. Ach Morgen war, als es sie auferschitter und versprechen im Wockannst mir auf, das
````

### AdamW – Schritt 36048

Checkpoint: `build/p5_grimm_mix_20261030/mix-adamw.mook`

SHA-256: `fec63371a932c0616381da9681d534406726f3cd2ec5d0f66e32b25d56a46ba8`

#### Probe 46: Greedy · Seed 1

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 248 Bytes · **UTF-8 gültig:** ja

````text
aber feinte und strinken sie in den Wald und hertig auf dem Schweißlein
ster Schwanzt von Herzen seill an die Hinftel. Dir wingunst du mit
in den Feuertenwes, Stühle mit den Schnee weg, nah
                                                        
````

#### Probe 47: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 263 Bytes · **UTF-8 gültig:** nein

````text
aber so liebt, daß du die Bepach siehs mir auf und aß ihn
bei dem könntet, und die Königin tanzt aus schlon hlein, so kerst
dem an?   erz\xc3eidder Schlafen friß. »Er schluchte ein Mannd von
Weckt ihm im Schlör.« -- »Nuch ich h die Angers, so daß sie finge
````

#### Probe 48: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 303 Bytes · **UTF-8 gültig:** ja

````text
aber keine Aliste ihr eine von Rechteick, seht
und sie steine Krück flopfte und sprach: »Macht versonnt du mit
die rechteserne Kopf ausstück davon den Kopppln.« Das Mädchen
soll sein und einen tegnen und wenn er Hälte den Schnefr und sagte sie zum Knaman und hollen die Jungfrau Maria und sprach:

````

#### Probe 49: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 308 Bytes · **UTF-8 gültig:** ja

````text
aber verbote sich, könig ohsagt, so will ich
dein treuen treuesterlen Johannes dem Kropf
zu, und weil du der ge König damit freuten. Als sie eine Naunn aussu und
einfört: trechten es ein Pfote die Stiel an und freute ihm
mit ihrem Heindsmal, davon den Leingenbl hinein, wenn er kalliebs
alte Süste höhe 
````

#### Probe 50: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Der König `  
**Länge:** 200 Tokens / 306 Bytes · **UTF-8 gültig:** ja

````text
aber hernigin tanzt, da sie zog, wie er sich ich
mit mir auf große Nochmön: er an, »stes ist bei
Körenst, wie ein Mutter ihm die Stief Liel
zu auf die Frau Balg zum Kächen und dachte nicht
konnte. Da Hockufannste die Wolte es freute sein. Die Königstochter sich folg, die
  künte und sprang aus dem 
````

#### Probe 51: Greedy · Seed 1

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 280 Bytes · **UTF-8 gültig:** ja

````text
und sich ff in die Schuhe und die Schuhe so viel sie in
die Herzen fiel zu, und als er das Schneif und sprang sie
schnell zu alle Stück und sprachen die Schuhe und die Schürestes Stein gesprachen hatte, und der
gand in die Schuhe gehen die Königin in einen Schlafen friefen zu 
````

#### Probe 52: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 317 Bytes · **UTF-8 gültig:** ja

````text
und steckte es sich über der
Lauft und herin mit seine Kleichen. Da lich sagten die Königstochte lag und
sagte, es aber der treue Johannes zu wingscht, so nelte es in seine Wasser
gewesen war und rief:

  »Was mir geraufen: enst du mir, du nicht bist: der Johannes te.»Der Heimm schell du mit den
Rech machen, als
````

#### Probe 53: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 290 Bytes · **UTF-8 gültig:** ja

````text
und richtet zu stand und hied in die Braut in
Stein.« -- Der Mädchen scheit
und sprach: »Jet minen ich habe gerachtert,« und die Jungfrau Maria die
woben s kam, sie zu sich auf belebltene in die Erschnim, so ich erselbst, wenn du mit miel
diene Kuter bestrichen.« Da führten der junge
````

#### Probe 54: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 305 Bytes · **UTF-8 gültig:** nein

````text
und rusten es nicht, und die nahm len das
eine Feuheben gescheiche und verlangssen es war, so w\xc3verrmechte die Einlein
alles mit Topt, das belte zu halte, der sprachen und frieden
ster an den andern Morgen, keh da und er sprachen die Stankrochen und sagte un
in das Felte gebüm lilt. Wie die König schon
````

#### Probe 55: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Die Kinder `  
**Länge:** 200 Tokens / 276 Bytes · **UTF-8 gültig:** nein

````text
und richtefes, Stiese nein der Wolf,« und verboote die Tür Wasser
und friefen zu und ihnen lebte besticht holen. Da llegte es drug alles leifen an und
unsen mit diekehweit gemüähe war, k warnf ein Scharze armen
St\xc3ne fiänge Tagt narst du; will mit den Becker gefreicht mi
````

#### Probe 56: Greedy · Seed 1

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 316 Bytes · **UTF-8 gültig:** ja

````text
nahn holte sich auf den Weg an und ren st steillte und
schieg ihm die Steine Krüchen und sprachen die Schuhe und die Schüsselten sich herum
und hinein, so legte es sich an den Körbchen auf, und der König sprachen. Der König sprach die Königin einen hohn, der König stellte und rief:
 »Was du nicht getan? Kö
````

#### Probe 57: Top-k 20, T=0.6 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 293 Bytes · **UTF-8 gültig:** ja

````text
nelll hil mir dein
und vechrrriefen und steine damit be und die Brieft
werden. Da kunst du in das Mädchen zu zum
drittenmal zum Tinken vor Kusckeins vor Eseis Stück hinle und die Feuerblat ge. Es war zernig die Jungfrau Maria wieder zu zweite Serschückte und wider
sprechen.


Du           
````

#### Probe 58: Top-k 20, T=0.6 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 265 Bytes · **UTF-8 gültig:** ja

````text
negt du mit den Bristeg an der Haupft und wel jeden Tr Wan scheit Steln un in
Eneahrten ein Stüs vor Schnei Süchschereinen Kopf hart i und Herzen hinein, mwindem
Stman, und der Sühle gefürfen hatte. Aber der König sprach zu schlichte:
»Wie, was ich gehübüng
````

#### Probe 59: Top-k 20, T=0.9 · Seed 11

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 274 Bytes · **UTF-8 gültig:** ja

````text
nug stolm (du bist versost
vor  Ihwß rehgebloied, wenn du dein Lugeh, men ja sick
denmal dlich.« Da führten es allen und rief:
te »War bin ich sang nicht, du sondern einklopfte den Hönnerwören un sehas de Lusch
ah.« Da böreem sunt de Brad den. Da sprach spiellte de:
````

#### Probe 60: Top-k 20, T=0.9 · Seed 23

**Prompt:** `Im Schloss `  
**Länge:** 200 Tokens / 297 Bytes · **UTF-8 gültig:** ja

````text
nuchte sie auf und fingeim nachnen lieg zum Antogen Tochter aber die
let, und nickte es in seiner Raghließ herab. Er sprang sie nicht, da seilbs in die
Wanden Füßen den benrnlich heras, so wie erbleich
vorblden. Ach Morgen war, als es sie auferschitter und versprechen im Wockannst mir auf, das
````

## Grenzen der Aussage

Dies ist ein beschreibender Inferenzvergleich mit drei bereits verwendeten Prompts und zwei Sampling-Seeds, kein neuer unabhängiger Qualitätsbenchmark. Temperatursampling und Top-k verändern die Auswahl vorhandener Modellvorhersagen; in diesem Versuch findet kein Training statt. Einzelne gelungene Sätze begründen keine allgemeine Verbesserung. Die 64-Token-Kontextgrenze gilt für alle Varianten. Die Prüfsummen der vier Checkpoints werden vor und nach dem Vergleich kontrolliert.

## Reproduktion und Dateien

- Moo-Programm: `beispiele/p5/grimm_langtext_sampling_20261030.moos`
- Runner: `skripte/p5_langtextvergleich_20261030.py` (nur Python-Standardbibliothek)
- Laufverzeichnis: `build/p5_langtext_20261030T183247_434841`
- `manifest.json`: Eingabepfade, SHA-256, Konfiguration und ausführbare Datei.
- `ergebnisse.json`: sämtliche Proben mit Token-IDs, Originalbytes und UTF-8-Prüfung.
- `*.jsonl` und `*.stderr.log`: unveränderte Programmausgaben.
- `rohtexte/*.bin`: exakte generierte Bytefolgen.
- `abschluss.json`: Abschlussstatus und verifizierte unveränderte Eingabedateien.
