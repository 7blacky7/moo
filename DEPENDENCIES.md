# moo Dependencies

Übersicht aller externen Abhängigkeiten von moo. Wird beim Update-Plan geprüft.

## Rust Crates (`compiler/Cargo.toml`)

| Crate | Version | Zweck | Update-Risiko |
|---|---|---|---|
| `inkwell` | git/master + `llvm18-1` | LLVM-Bindings für Codegen | **HOCH** — git pinned, LLVM-API-Brüche möglich |
| `clap` | 4.x | CLI-Argument-Parser | niedrig |
| `cc` | 1.x | C-Build in `build.rs` | niedrig |

**Inkwell-Sonderfall**: Pinned auf `git = "...inkwell", branch = "master", features = ["llvm18-1"]`. Bei LLVM-Major-Updates (LLVM 19, 20…) bricht das Feature-Flag — manueller Eingriff nötig. Nicht durch normales `cargo update` geupdatet (git-Branch wird trotzdem mitgezogen).

## C System-Libraries (über pacman/distro)

| Header | Pacman-Paket | Genutzt in | Zweck |
|---|---|---|---|
| `sqlite3.h` | `sqlite` | `moo_db.c` | SQLite-Backend |
| `SDL2/SDL.h` | `sdl2` | `moo_graphics.c` | 2D-Grafik + Input |
| `GLFW/glfw3.h` | `glfw` | `moo_3d.c` | 3D-Fenster |
| `GL/gl.h` | `mesa` | `moo_3d.c` | OpenGL-Rendering |
| `curl/curl.h` | `curl` | `moo_http.c` | HTTP-Client |
| `regex.h` | `glibc` (system) | `moo_regex.c` | POSIX-Regex |
| `pthread.h` | `glibc` (system) | `moo_thread.c` | Threading |
| `dirent.h` | `glibc` (system) | `moo_file.c` | Verzeichnis-Iter |

System-Libs werden via `pacman -Syu` aktualisiert (Distro-weit). moo bricht selten — nur bei API-Brüchen einer dieser Libs.

## Update-Plan

### Wöchentlich (passiv, nur Bericht)
```sh
scripts/check-deps.sh > /tmp/moo-deps-$(date +%F).txt
```
Schaut nach veralteten Crates und CVEs, schreibt Bericht — **kein Auto-Update**.

### Monatlich (aktiv)
```sh
git checkout -b update-deps-$(date +%Y%m)
cd compiler && cargo update
cargo build --release
# Alle Stresstest-Beispiele probelaufen lassen
for f in beispiele/*.moo; do
  timeout 30 compiler/target/release/moo-compiler compile "$f" -o /tmp/out
done
git commit -am "chore(deps): cargo update $(date +%Y-%m)"
```
Bei Erfolg → merge in master. Bei Bruch → branch behalten, Issue notieren.

### Quartal
- `pacman -Syu` (System-Libs) — moo testen ob noch baut
- Inkwell-Repo prüfen ob neue LLVM-Version verfügbar (LLVM 19/20…)

### Sicherheits-Audit
```sh
cargo install cargo-audit  # einmalig
cargo audit                # findet bekannte CVEs in Crates
```
Sofort handeln bei: HIGH oder CRITICAL Funden.

## Bekannte Pinning-Gründe

- **inkwell git/master**: Crates.io-Version hängt LLVM-Releases hinterher. Wir brauchen LLVM 18 Support → git/master ist die einzige Quelle.
- **edition = "2024"**: Rust 2024 Edition aktiv. Mindest-Toolchain Rust 1.85+.

## Notes

- Keine npm/Python-Dependencies (außer optionalen VS-Code-Extension Tools in `editors/vscode/moo/`)
- Ngrok/Docker für Test-Container sind **runtime-only**, keine Build-Deps
