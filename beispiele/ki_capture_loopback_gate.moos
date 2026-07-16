# Privilegiertes Linux-Integrationsgate: v4l2loopback + snd-aloop.
setze kamera_pfad auf umgebung("MOO_CAPTURE_CAMERA_DEVICE")
setze mikro_geraet auf umgebung("MOO_CAPTURE_AUDIO_DEVICE")

setze kamera auf kamera_oeffnen(kamera_pfad)
setze frame auf kamera_frame(kamera, 2000)
setze bild auf tensor_aus_frame(frame, "rgb")
kamera_schliessen(kamera)

setze mikro auf mikro_oeffnen(48000, 1, mikro_geraet)
setze block auf mikro_lesen(mikro, 1024, 2000)
mikro_schliessen(mikro)

wenn bild.form()[2] == 3 und block["daten"].form()[0] == 1024 und block["rate"] > 0:
    zeige "SELFTEST_RESULT: PASS ki_capture_loopback"
sonst:
    zeige "SELFTEST_RESULT: FAIL ki_capture_loopback"
