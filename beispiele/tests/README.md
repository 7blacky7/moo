# UI Visual Test Suite (Plan-004 P4)

Automatisierte visuelle Tests fuer die bestehenden UI-Demos.

## Tests

| Test                      | Fokus                                                   |
|---------------------------|---------------------------------------------------------|
| `ui_layout_test.moo`      | Layout (Spalte/Zeile, Padding, Abstand, Relayout)       |
| `ui_table_test.moo`       | ListView (sortieren, Zelle setzen, Zeile entfernen)     |
| `ui_binding_test.moo`     | Eingabe + Button-Callback (text_setze, klick_id)        |
| `ui_shortcuts_test.moo`   | Aktionen + Tastenkombinationen (Ctrl+S, Escape)         |
| `ui_canvas_test.moo`      | Leinwand + on_maus-Callback (klick_xy)                  |

Jeder Test nutzt die P3-Automation-Wrapper (`ui_test_sequenz`, `ui_test_frame`)
und schreibt **PNG + JSON-Sidecar** pro Frame in `beispiele/snapshots/<name>/`.

## Ausfuehren

```bash
scripts/ui-test-runner.sh
```

Der Runner:
- nutzt `$DISPLAY` wenn gesetzt, sonst startet er `Xvfb :99` (headless)
- kompiliert + faehrt jeden Test mit Timeout 15s
- validiert alle `frame_*.json` via `python3 -m json.tool`
- prueft PNG-Magic-Bytes + Mindestgroesse (200 B)
- schreibt Report nach `beispiele/snapshots/test_report.json`
- Exit 0 = alle gruen, 1 = mind. ein Test rot

## Artefakte

```
beispiele/snapshots/
├── layout/    frame_001.png frame_001.json ...
├── table/     frame_001.png frame_001.json ...
├── binding/   frame_001.png frame_001.json ...
├── shortcuts/ frame_001.png frame_001.json ...
├── canvas/    frame_001.png frame_001.json ...
└── test_report.json
```

Jedes Sidecar-JSON (P3-Schema) enthaelt:
`fenster_titel`, `timestamp`, `timestamp_unix`, `backend`, `scale`,
`window_size`, `action`, `widget_tree`, `screenshot_path`.
