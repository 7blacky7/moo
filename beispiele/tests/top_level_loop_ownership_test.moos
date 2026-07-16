# P016-M1 B4b: top-level control-flow local ownership regression.
# A value first assigned inside a top-level loop is allocated as a main-frame
# local. The main epilogue must release its final owning value. LeakSanitizer is
# the authoritative gate; predeclaring the variable before the loop would mask
# this exact compiler path by turning it into an LLVM global.

importiere ui
importiere ui_moo

setze fehler auf 0
setze i auf 0
solange i < 1:
    setze innerer_kontext auf uim_mock_wurzel(96, 64)
    wenn typ_von(innerer_kontext) != "Woerterbuch":
        setze fehler auf fehler + 1
    setze i auf i + 1

wenn fehler == 0 und i == 1:
    zeige "P016-M1-TOP-LEVEL-LOOP-OWNERSHIP-OK"
sonst:
    zeige "P016-M1-TOP-LEVEL-LOOP-OWNERSHIP-RED fehler=" + text(fehler)
    wirf "top-level loop ownership regression"
