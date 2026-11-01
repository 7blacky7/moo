# P2 Frontend: vorbereitete Tests

Diese Dateien beschreiben den freigegebenen SOLL-Vertrag aus Thought f67bfcbd,
keinen bereits implementierten Zustand. Sie sind nicht in run_all.sh eingebunden;
native, oktale und zusammengesetzte Typen benötigen G1/G2 und spätere
Implementierung. Die Positivfälle mit .expected vergleichen stdout und Exit 0.
legacy_basen.moos bewahrt das beobachtete Altverhalten im dynamischen Kontext;
ein etwaiger Versionswechsel dieser Semantik bedarf gesonderter Entscheidung.

Die Negativfälle müssen einzeln mit Exit != 0 und einer quellenbezogenen
Diagnose scheitern; Erwartungen stehen in manifest.json. Diagnosecodes und
konkretes Fehlerformat sind nicht erfunden. Ein Syntaxfehler an einer anderen
Stelle als der beschriebenen Zielstelle ist kein Bestehen. Keine Testdatei im
Legacy-Gate blind als vorhandenes Programm behandeln.

positiv_annotationen.moos ist ein AST-Test: Der spätere Parser-Test muss die
Typbäume und Quellspans der verschachtelten Parameter, des Defaults, der
Rückgabe und beider lokalen Annotationen prüfen; bloßer Kompiliererfolg beweist
nicht, dass die Typen erhalten bleiben. Ein eingebetteter AST- oder IR-Vergleich
und Tests für Alias-/Modulpräfixierung sind zusätzlich nötig (P2-T8).
