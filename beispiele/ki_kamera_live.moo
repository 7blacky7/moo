# ki_kamera_live.moo — Live-Kamera -> Frame -> Tensor
#
# Optional: kamera_oeffnen("/dev/video2", 1280, 720, 30)
# Vollstaendig angegebene Werte verlangen ein exaktes Format.

setze kamera auf nichts
versuche:
    zeige "Kameras: " + text(kamera_liste())
    setze kamera auf kamera_oeffnen()
    setze i auf 0
    solange i < 100:
        setze frame auf kamera_frame(kamera, 1000)
        setze tensor auf tensor_aus_frame(frame, "rgb")
        wenn i % 30 == 0:
            zeige "Frame " + text(i) + ": " + text(tensor.form())
            test_frame_save_bmp(frame, "/tmp/ki_kamera_live.bmp")
        setze i auf i + 1
    kamera_schliessen(kamera)
fange fehler:
    wenn kamera != nichts:
        kamera_schliessen(kamera)
    zeige "Kamera-Fehler: " + fehler
