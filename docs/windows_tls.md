# Native Windows-TLS-Pipeline

Stand: 2026-07-18

Diese Pipeline baut den Rust-/LLVM-Compiler und die C-Runtime **nativ auf Windows mit MSVC**. Sie ist kein Cross-Compile- oder Wine-Ersatz.

## Backends

| Wert `MOO_TLS_BACKEND` | Windows | Nicht-Windows |
|---|---|---|
| nicht gesetzt | **SChannel** | OpenSSL |
| `schannel` | SChannel | nicht erlaubt |
| `mbedtls` | vendored mbedTLS | vendored mbedTLS |
| `openssl` | nicht verdrahtet | OpenSSL |

Alle Backends erfüllen `compiler/runtime/moo_tls.h` und damit denselben Vertrag für:

- `tls_verbinde(host, port, timeout_ms?)`
- `tls_starttls(tcp_socket, host)`
- `tls_sende`
- `tls_empfange`
- `tls_schliesse`

## Verifizierte Toolchain

Der Runner verwendet standardmäßig:

- MSVC Build Tools über `C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat`
- Rust MSVC Toolchain aus `C:\moo-tools\cargo` und `%USERPROFILE%\.rustup`
- LLVM 18 unter `C:\moo-tools\llvm18\Library`
- `clang.exe` aus dem LLVM-18-Prefix
- `lld-link.exe` aus dem aktiven Rust-MSVC-Sysroot
- `sqlite3.h`, `sqlite3.lib` und `sqlite3.dll` aus dem LLVM-/Conda-Prefix

Die Standardpfade können über diese Variablen überschrieben werden:

- `MOO_WIN_TOOLS`
- `MOO_WIN_VCVARS`
- `MOO_WIN_LLVM_ROOT`
- `CARGO_HOME`
- `RUSTUP_HOME`
- `MOO_CLANG`
- `MOO_LLD`

Der Runner verändert keine globalen Systemvariablen und verlangt keinen Neustart.

## Build

```bat
scripts\windows_tls_build.cmd schannel
scripts\windows_tls_build.cmd mbedtls
```

Beide Befehle führen einen Release-Build mit `--no-default-features` aus. Dadurch benötigt das TLS-Gate keine installierten UI-/3D-Bibliotheken.

## Native Integrationsprüfung

```bat
beispiele\windows_tls_smoke.cmd schannel
beispiele\windows_tls_smoke.cmd mbedtls
```

Der Runner baut den gewählten Backendpfad, kompiliert `beispiele/windows_tls_smoke.moos` mit dem **auf Windows gebauten Moo-Compiler** und prüft vier reale Modi:

1. HTTPS-GET gegen `example.com`
2. Ablehnung von `self-signed.badssl.com`
3. STARTTLS gegen `smtp.gmail.com:587`
4. begrenzter TCP-/TLS-Connect-Timeout

Erwarteter Abschlussmarker:

```text
WINDOWS-TLS-SMOKE PASS backend=schannel
WINDOWS-TLS-SMOKE PASS backend=mbedtls
```

## Backend-Symbolgate

```bat
scripts\windows_tls_backend_gate.cmd schannel
scripts\windows_tls_backend_gate.cmd mbedtls
```

Das Gate untersucht die frisch gebaute `moo_runtime.lib` mit `dumpbin /symbols`.

SChannel muss unter anderem enthalten:

- `InitializeSecurityContextA`
- `EncryptMessage`
- `DecryptMessage`
- `CertVerifyCertificateChainPolicy`

und darf `mbedtls_ssl_handshake` nicht enthalten.

mbedTLS muss unter anderem enthalten:

- `mbedtls_ssl_handshake`
- `mbedtls_ssl_write`
- `mbedtls_ssl_read`

und darf die SChannel-Funktionen nicht enthalten.

## Sicherheitsvertrag

### SChannel

- Windows ist der native TLS-Provider.
- Der Handshake läuft über SSPI/SChannel.
- Die Zertifikatskette wird mit `CertGetCertificateChain` aufgebaut.
- Zertifikat und Hostname werden explizit mit `CERT_CHAIN_POLICY_SSL` geprüft.
- Self-Signed-Zertifikate werden abgelehnt.
- Optionale Client-Zertifikatsanforderungen werden ohne Zertifikat fortgesetzt; ein zwingend erforderliches Client-Zertifikat bleibt ein Fehler.
- `close_notify` wird vor dem Freigeben der Verbindung gesendet.

### vendored mbedTLS

- Der Socket bleibt pointerbreit; Windows-`SOCKET` wird nicht in `int` gekürzt.
- Eigene mbedTLS-BIO-Callbacks verwenden Winsock direkt.
- Vertrauensanker werden read-only aus den Windows-ROOT-Stores `CurrentUser` und `LocalMachine` geladen.
- Hostname- und Zertifikatsprüfung bleiben aktiv.

## Ehrliche Grenzen

- Es existiert noch keine öffentliche API zur Auswahl eines Client-Zertifikats.
- SChannel-Renegotiation nach einem abgeschlossenen Datenkanal wird derzeit nicht unterstützt.
- `timeout_ms` begrenzt den TCP-Verbindungsaufbau. Ein allgemeiner öffentlicher TLS-Lese-/Schreibtimeout ist weiterhin nicht separat exponiert; STARTTLS übernimmt vorhandene Socket-Timeouts.
- macOS besitzt noch kein eigenes Network.framework-/Secure-Transport-Backend und verwendet derzeit den Nicht-Windows-Standardpfad.
