# P016 V1: Windows moo_ui-only Import-/Link-Grenzvertrag.
# Keine Fenstererzeugung, kein Desktop-Input und keine Capture-API.
# Der Test nutzt nur den Mock-Konstruktor, muss aber als vollstaendiges
# Programm mit "importiere ui_moo" auf jeder moo_ui-Plattform linken.

importiere ui_moo

setze kontext auf uim_mock_wurzel(32, 24)
wenn kontext == nichts:
    wirf "ui_moo Mock-Kontext fehlt"
wenn kontext["wurzel"] == nichts:
    wirf "ui_moo Mock-Wurzel fehlt"

zeige "P016-V1-UI-MOO-IMPORT-LINK-OK"
