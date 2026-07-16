funktion hex(b):
    setze chars auf "0123456789abcdef"
    setze out auf ""
    setze i auf 0
    setze bb auf bytes_zu_liste(b)
    solange i < länge(bb):
        setze v auf bb[i]
        setze out auf out + chars[boden(v / 16)] + chars[v % 16]
        setze i auf i + 1
    gib_zurück out

funktion check(label, ist, soll):
    wenn ist == soll:
        zeige "OK   " + label
    sonst:
        zeige "FAIL " + label
        zeige "  ist:  " + ist
        zeige "  soll: " + soll

setze v_sha auf "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
check("sha256(abc)", sha256("abc"), v_sha)
check("sha256_bytes(abc)", hex(sha256_bytes("abc")), v_sha)

setze v_hmac auf "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
check("hmac(Jefe,...)", hex(hmac_sha256("Jefe", "what do ya want for nothing?")), v_hmac)

setze v_pb1 auf "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783"
check("pbkdf2(passwd,salt,1,64)", hex(pbkdf2_sha256("passwd", "salt", 1, 64)), v_pb1)

setze v_pb2 auf "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d"
check("pbkdf2(Password,NaCl,80000,64)", hex(pbkdf2_sha256("Password", "NaCl", 80000, 64)), v_pb2)
