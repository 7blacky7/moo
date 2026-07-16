# Compile-only smoke: kein .expected, weil live-network. Signatur-Lock fuer
# http_hole_mit_headers / http_sende_mit_headers — stellt sicher, dass die
# Aliase nicht versehentlich aus codegen.rs rausfliegen.
setze h auf {"X-Test": "moo", "User-Agent": "moolang"}
setze g auf http_hole_mit_headers("http://example.invalid/", h)
setze p auf http_sende_mit_headers("http://example.invalid/", {"k": "v"}, h)
