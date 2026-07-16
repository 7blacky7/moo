# Test: datei_mtime + datei_ist_verzeichnis (neue Stat-Builtins)

datei_schreiben("/tmp/moo_stat_test.txt", "hallo")

# mtime sollte > 0 sein (Unix-Timestamp)
setze mt auf datei_mtime("/tmp/moo_stat_test.txt")
zeige mt > 0

# Datei ist KEIN Verzeichnis
zeige datei_ist_verzeichnis("/tmp/moo_stat_test.txt")

# /tmp IST ein Verzeichnis
zeige datei_ist_verzeichnis("/tmp")

# Nicht existierender Pfad: mtime = -1, is_dir = falsch
zeige datei_mtime("/tmp/gibt_es_nicht_123xyz")
zeige datei_ist_verzeichnis("/tmp/gibt_es_nicht_123xyz")

datei_löschen("/tmp/moo_stat_test.txt")
