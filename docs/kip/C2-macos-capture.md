# C2-MAC: Kamera- und Mikrofon-Capture unter macOS

## Stand

Die macOS-Systemgrenze implementiert den C0-Vertrag über AVFoundation
(Kamera) und AVAudioEngine/CoreAudio (Mikrofon). Sie verwendet denselben
plattformneutralen Pull-Zustandsautomaten wie C2-WIN. Dadurch sind
Deadline-Bindung, Latest-Frame-Auswahl, BGRA→RGBA, Audio-Spill,
Mono-Downmix, begrenzte Recovery und Cleanup-vor-Throw nur einmal definiert.

Der Adapter ist implementiert, aber noch nicht auf echter macOS-Hardware
kompiliert oder mit Kamera/Mikrofon ausgeführt. Bis ein macOS-Runner und ein
echter Mac die unten beschriebenen Gates bestehen, bleibt C2-MAC ausdrücklich
hardware- und build-unverifiziert.

## Kamera

- AVCaptureSession, AVCaptureDeviceInput und AVCaptureVideoDataOutput mit BGRA.
- alwaysDiscardsLateVideoFrames begrenzt die native Queue auf das neueste Bild;
  vor Rückkehr aus dem Delegate wird der CVPixelBuffer vollständig kopiert.
- bytesPerRow, Geometrie und Puffergröße werden vor der Kopie geprüft.
- Close setzt zuerst den Stop-Zustand, entfernt den Delegate, stoppt die Session
  und drainiert die serielle Delegate-Queue.

## Mikrofon

- AVAudioEngine.inputNode liefert Float32-Mono/Stereo-Pakete.
- Der Adapter kopiert und interleavt planare Kanäle; der gemeinsame Pull-Core
  übernimmt exakte Blockgrößen, Stereo→Mono und Spill-Reste.
- Der native Puffer ist auf 8192 Frames begrenzt; bei Überlauf bleiben die
  neuesten Samples erhalten.
- Eine AVAudioEngineConfigurationChangeNotification wird als recoverable
  gemeldet. Recovery baut Engine, InputNode und Tap vollständig neu auf und
  ist im Pull-Core auf drei Versuche begrenzt.
- Close entfernt Tap und Observer, stoppt die Engine und drainiert die Queue.

## TCC und CLI-Binaries

moo_capture_macos_info.plist enthält NSCameraUsageDescription und
NSMicrophoneUsageDescription. Der Compiler bettet sie als Mach-O-Sektion
__TEXT,__info_plist in jedes erzeugte macOS-Programm ein. NotDetermined wird
mit einer auf zehn Sekunden begrenzten Anfrage behandelt; verweigerte oder
eingeschränkte Berechtigungen werden als normale MOO-Fehler materialisiert.

## Gates

Auf jedem Host prüft run_sanitize.sh die injizierte gemeinsame
test_capture_pull_ops_asan.c-Fault-Matrix unter ASan/LSan und UBSan.

Auf einem macOS-Runner:

    bash skripte/capture_macos_gate.sh
    mise run test-compiler
    bash compiler/runtime/tests/run_sanitize.sh all

Das macOS-Gate verlangt ARC- und -Werror-Compile, echten Framework-Link,
gültige/eingebettete Info.plist sowie Startup und hardwarefreie
Kamera-Enumeration. Ein separates echtes Hardware-Gate muss danach Kamera und
Mikrofon unter Streaming-Last öffnen, Frames/Samples prüfen, Close parallel
auslösen und Leaks/Races mit Instruments beziehungsweise Thread Sanitizer
ausschließen.
