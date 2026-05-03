# Regression-Test fuer closure-segfault-multi-call Bug.
#
# Erwartet: 3x "alpha" auf stdout, exit 0.
# Bug-Stand 2026-05-03: SIGSEGV beim 3. Aufruf in Expression-Position.
#
# Root-Cause-Hypothese: Closure-Body `() => name` liefert captured MooValue ohne
# moo_retain. Aufrufer released nach Gebrauch -> Refcount sinkt pro Call -> bei
# 3+ Calls freed memory.

funktion factory(name):
    setze cb auf () => name
    gib_zurück cb

setze c1 auf factory("alpha")
zeige(c1())
zeige(c1())
zeige(c1())
