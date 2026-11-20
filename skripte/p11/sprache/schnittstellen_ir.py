"""Prüft tatsächlichen LLVM-Dispatch, keinen nachgebauten Compiler."""
import pathlib
import re
import sys

ir = pathlib.Path(sys.argv[1]).read_text()
def body(name):
    match = re.search(r"^define[^\n]*@" + re.escape(name) + r"\([^\n]*\n(.*?)^}", ir, re.M | re.S)
    if not match:
        raise SystemExit("Native Funktion fehlt im IR: " + name)
    return match.group(1)

direct = body("__moo_native.schnittstelle_direkt")
dynamic = body("__moo_native.__moo_dispatch.Speicher.lesen")
indirect = r"\bcall[^\n]*%[-\w.]+\("
assert not re.search(indirect, direct), "konkreter Empfänger ruft indirekt"
assert re.search(indirect, dynamic), "Interface-Empfänger hat keinen indirekten Aufruf"
for implementation in ("PlusSpeicher", "XorSpeicher"):
    name = "__moo_native.__moo_dynzeiger." + implementation + ".Speicher"
    header = re.search(r"^define[^\n]*@" + re.escape(name) + r"\([^\n]*", ir, re.M)
    assert header and "sret({ ptr, ptr })" in header.group(0), "Interfacezeiger hat nicht genau Daten-/Vtablezeiger"
    assert "align 8 dereferenceable(16)" in header.group(0), "Interfacezeiger ist nicht 16 Byte groß/8 Byte ausgerichtet"
print("PASS IR: konkreter Typ direkt, Interface typisiert indirekt, fetter Zeiger mit zwei Zeigern")
