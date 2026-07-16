# Bug 3 Regression (Synapse Thought 23c1749b): Top-Level-Variablen
# sollen aus Funktionen heraus sichtbar sein. Am 2026-04-11 war das
# kaputt ("Variable 'db' nicht gefunden"). Der Fix lebt in
# compiler/src/codegen.rs Z. 157-176 (LLVM-Globals __moo_g_<name>).

setze db auf "synapse"
setze version auf 42

funktion zeige_config():
    zeige db
    zeige version

zeige_config()
