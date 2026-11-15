//! P2-T96 (Plan 12e114f2, Freigabe #38308): unbehandelte wirf-Pfade zur
//! Compilezeit. Lesender AST-Pass nach resolve_modules; aendert nichts und
//! erzeugt keinen Code (Legacy-IR unveraendert).
//!
//! M-F001 (Hinweis) steht an einer Aufrufstelle ausserhalb von versuche:,
//! wenn die aufgerufene Funktion SELBST ungeschuetzt wirft: ein `wirf`, das
//! nicht im versuche-Teil derselben Funktion steht (ein wirf im fange-Teil
//! zaehlt). Wie die Laufzeit (P2-T95) gibt es genau einen Hinweis je Kette:
//! Wirft g nur ueber einen weiteren Aufruf, ist dessen innerste Stelle schon
//! gemeldet (O4). Deshalb braucht es keinen Fixpunkt, Rekursion ist kein
//! Sonderfall.
//!
//! Aufgeloest wird nur sicher: Namensaufruf einer benannten Funktion,
//! `selbst.m()` sowie `neu K(..)` (Konstruktor `erstelle` von K), wenn genau
//! eine Klasse m definiert. Sonst still: Methoden auf anderen Objekten,
//! Funktionswerte, Rueckrufe, aufraeumen-Ausdruecke, Testbloecke, @nativ.
//! Lambda-Koerper laufen erst beim Aufruf, sie gelten deshalb nie als
//! geschuetzt. Stufe 2 (#38339): Aufrufe
//! fehlbarer Builtins (FEHLBARE_BUILTINS) melden M-F002, eigene Funktionen
//! gleichen Namens gehen vor (wie im Codegen). Hinweise in stdlib-Modulen
//! entfallen; die stdlib-Funktion gilt dann selbst als werfend (Fixpunkt) und
//! der Aufruf im eigenen Code bekommt M-F001. Grenze (O5): Top-Level-Code importierter
//! Module wird nicht geprueft (nach dem Inlining fehlt die Datei).
//! `--typ-streng` macht M-F001 zum Fehler mit Abbruch vor dem Codegen (O2).
//!
//! Neu entschieden (#38315, Messung 258 Hinweise): Der Pass laeuft nur mit
//! `--typpruefung` (bzw. `--typ-streng`), und je Paar (aufrufende Funktion,
//! gerufene Funktion) gibt es hoechstens einen Hinweis; weitere Stellen
//! stehen als "und N weitere Aufrufe" darin. Jedes Lambda ist ein eigener
//! Aufrufer `f.<lambda@Zeile>` (Review 015acc93). Ebenso still: Namen, die
//! auf oberster Ebene VOR der Definition der aufrufenden Funktion als Variable
//! gebunden sind; der Codegen loest den Namen dann auf die Variable auf, eine
//! spaetere Bindung verdeckt die Funktion nicht (Proben build/opus-t96/proben
//! a_ohne_global/c_var_vor_k, Review 6078a97b). `global x` im Koerper zaehlt
//! lokal. Im Hauptprogramm zaehlt jede Top-Level-Bindung (Grenze: auch eine
//! spaetere macht still). Review 049ea57c: Ist der Name
//! am Aufrufort lokal gebunden (Parameter, setze/konstante, fuer-Variable,
//! fange-Variable, Lambda-Parameter), ist der Aufruf nicht sicher
//! aufloesbar und bleibt still.

use std::cell::Cell;
use std::collections::{HashMap, HashSet};

use crate::ast::{Decorator, Expr, Program, Stmt};
use crate::diagnose::{self, Diagnose, Schwere};

thread_local! {
    static FEHLER: Cell<usize> = const { Cell::new(0) };
}

/// Erste ungeschuetzte Wurfstelle einer Funktion oder Methode.
#[derive(Clone)]
struct Wurf {
    datei: String,
    zeile: usize,
    /// Stufe 2 (Q3): stdlib-Funktion wirft ueber diesen Aufruf (Builtin oder Funktion).
    ueber: Option<String>,
}

/// Wo gerade geprueft wird (fuer Ort und Meldungstext).
#[derive(Clone)]
struct Ort<'a> {
    datei: &'a str,
    /// Funktion/Methode/Lambda; None = Hauptprogramm.
    funktion: Option<String>,
    klasse: Option<&'a str>,
    /// Im Gueltigkeitsbereich lokal gebundene Namen (still bei Aufruf).
    lokal: HashSet<String>,
}

struct Pass<'a> {
    haupt: &'a str,
    quellen: &'a HashMap<String, String>,
    funktionen: HashMap<String, Wurf>,
    /// Methodenname -> je definierender Klasse die Wurfstelle (falls sie wirft).
    methoden: HashMap<String, Vec<(String, Option<Wurf>)>>,
    befunde: Vec<Diagnose>,
    /// (Datei, aufrufende Funktion, gerufene Funktion) -> Index in befunde.
    paare: HashMap<(String, Option<String>, String), usize>,
    /// Weitere Aufrufstellen je Befund (gleicher Index wie befunde).
    weitere: Vec<usize>,
    /// Lambdas je (Datei, Zeile): jedes Lambda ist ein eigener Aufrufer (015acc93).
    lambdas: HashMap<(String, usize), usize>,
    /// Je Funktion/Klasse: Top-Level-Variablen, die vor ihrer Definition gebunden sind.
    global_vor: HashMap<String, HashSet<String>>,
    /// Stufe 2: Namen ALLER eigenen Funktionen (auch nicht werfende, @nativ);
    /// sie haben im Codegen Vorrang vor gleichnamigen Builtins (codegen.rs:6980).
    nutzer: HashSet<String>,
    /// Stufe 2 (Q3): Sammellauf, nur stdlib-Wurfstellen nachtragen, nichts melden.
    nur_sammeln: bool,
    /// Stufe 2 (Q3): im Sammellauf neue Wurfstelle gefunden.
    neu: bool,
}

fn ist_nativ(decorators: &[Decorator]) -> bool {
    decorators.iter().any(|d| d.name == "nativ" || d.name == "native")
}

/// Exportierte Definitionen wie gewoehnliche behandeln.
fn ohne_export(stmt: &Stmt) -> &Stmt {
    match stmt {
        Stmt::Export(inner, _) => ohne_export(inner),
        other => other,
    }
}

/// P2-T96 Stufe 2 (Plan d888a365, Entscheid #38339): Builtins, deren Laufzeit bei
/// aeusserem Fehlschlag (Netz, Datenbank, Datei fehlt/kaputt) moo_throw ruft.
/// Jede Gruppe mit Laufzeitprobe „WIRFT“ (moo-t96e, build/opus-t96/stufe2/proben,
/// Jobs c88959a9, 7781acb8, 2c53d5eb); Kommentar = Laufzeitmeldung.
/// Bewusst NICHT: datei_lesen/datei_zeilen/datei_schreiben & Co. (still "" bzw.
/// falsch), JSON (kein moo_throw), Methoden (Empfaenger statisch unbekannt),
/// Geraete/UI (zurueckgestellt), Speichern ohne Probe (checkpoint_speichern,
/// tokenizer_speichern, shard_schreiben, tls_starttls) und Wuerfe nur bei
/// Programmierfehlern (Argumente, Form, Index, Division, Grenzen).
const FEHLBARE_BUILTINS: &[(&[&str], &str)] = &[
    (&["tcp_verbinde", "tcp_connect"], "Verbindung fehlgeschlagen"), // moo_net.c moo_tcp_connect: "TCP-Verbindung fehlgeschlagen"
    (&["tcp_server"], "Bind/Listen fehlgeschlagen"), // moo_net.c moo_tcp_server: "Bind oder Listen fehlgeschlagen"
    (&["udp_socket"], "Socket/Bind fehlgeschlagen"), // moo_net.c moo_udp_socket: "Socket oder Bind fehlgeschlagen"
    (&["tls_verbinde", "tls_connect"], "Verbindung/Handshake fehlgeschlagen"), // moo_tls.c: "tls_verbinde: TCP-Verbindung fehlgeschlagen"
    (&["web_server", "web_erstelle"], "Bind/Listen fehlgeschlagen"), // moo_web.c moo_web_server: "WebServer: Bind fehlgeschlagen"
    (&["db_verbinde", "db_connect", "dbv"], "Datenbank nicht zu öffnen"), // moo_db.c: "Konnte Datenbank nicht oeffnen"
    (&["db_ausführen", "db_execute", "dbe"], "SQL-Fehler"), // moo_db.c: "SQL-Fehler in ..."
    (&["db_abfrage", "db_query", "dba"], "SQL-Fehler"), // moo_db.c: "SQL-Fehler in ..."
    (&["db_ausführen_mit_params", "db_execute_with_params"], "SQL-Fehler"), // moo_db.c (Entscheid B1)
    (&["db_abfrage_mit_params", "db_query_with_params"], "SQL-Fehler"), // moo_db.c (Entscheid B1)
    (&["db_vorbereite", "db_prepare"], "SQL-Fehler"), // moo_db.c (Entscheid B1)
    (&["ki_laden", "ai_load", "netz_laden"], "Datei fehlt oder ist kaputt"), // moo_nn_easy.c: "ki_laden: kann ... nicht oeffnen"
    (&["safetensors_laden", "safetensors_load"], "Datei fehlt oder ist kaputt"), // moo_nn_easy.c: "safetensors_laden: kann ... nicht oeffnen"
    (&["checkpoint_laden", "checkpoint_load"], "Datei fehlt oder ist kaputt"), // moo_nn_easy.c: "checkpoint_laden: kann ... nicht oeffnen"
    (&["checkpoint_rotieren", "checkpoint_rotate"], "Verzeichnis nicht lesbar"), // moo_nn_easy.c: "checkpoint_rotieren: Verzeichnis ... nicht lesbar"
    (&["tokenizer_laden", "tokenizer_load"], "Datei fehlt oder ist kaputt"), // moo_tokenizer.c: "tokenizer_laden: kann ... nicht oeffnen"
    (&["mnist_laden", "mnist_load"], "Datei fehlt oder ist kaputt"), // moo_dataset.c: "mnist_laden: kann ... nicht oeffnen"
    (&["datensatz_csv", "dataset_csv"], "Datei fehlt"), // moo_dataset.c: "datensatz_csv: kann ... nicht oeffnen"
    (&["bild_laden", "image_load"], "Datei fehlt oder ist kaputt"), // moo_dataset.c: "bild_laden: kann ... nicht oeffnen"
    (&["wav_lesen", "wav_read"], "Datei fehlt oder ist kaputt"), // moo_audio.c: "wav_lesen: Datei konnte nicht geoeffnet werden"
    (&["shard_info"], "Datei fehlt oder ist kaputt"), // moo_shard.c: "shard_info: kann Datei nicht oeffnen"
    (&["shard_fenster", "shard_window"], "Datei fehlt oder ist kaputt"), // moo_shard.c: "shard_fenster: kann Datei nicht oeffnen"
    (&["shard_fenster_maske", "shard_mask_window"], "Datei fehlt oder ist kaputt"), // moo_shard.c: "shard_fenster_maske: kann Datei nicht oeffnen"
    (&["shard_reihenfolge", "shard_order"], "Datei fehlt oder ist kaputt"), // moo_shard.c: "shard_reihenfolge: kann Datei nicht oeffnen"
];

/// Kurzgrund, wenn `name` ein fehlbares Builtin ist.
fn builtin_grund(name: &str) -> Option<&'static str> {
    FEHLBARE_BUILTINS.iter().find(|(namen, _)| namen.contains(&name)).map(|(_, grund)| *grund)
}

/// Modul im Baum eines stdlib-Verzeichnisses (find_module: <dir>/stdlib, <exe>/stdlib,
/// <cwd>/stdlib), auch verschachtelt (stdlib/tief/x.moos). Review 1bdc285e: normalisierter
/// Pfad (../ und Symlinks aufgeloest), irgendein Ordner darueber heisst stdlib.
fn ist_stdlib(datei: &str) -> bool {
    let pfad = std::fs::canonicalize(datei).unwrap_or_else(|_| std::path::PathBuf::from(datei));
    pfad.parent().is_some_and(|p| p.components().any(|c| c.as_os_str() == "stdlib"))
}

/// Zeile des ersten `wirf` ausserhalb eines versuche-Teils; verschachtelte
/// Definitionen gehoeren nicht dazu.
fn wurfstelle(stmts: &[Stmt]) -> Option<usize> {
    stmts.iter().find_map(|stmt| match stmt {
        Stmt::Throw(_, zeile) => Some(*zeile),
        Stmt::TryCatch { catch_body, .. } => wurfstelle(catch_body),
        Stmt::If { body, else_body, .. } => wurfstelle(body).or_else(|| wurfstelle(else_body)),
        Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::ParallelFor { body, .. }
        | Stmt::UnsafeBlock { body, .. } => wurfstelle(body),
        Stmt::Guard { else_body, .. } => wurfstelle(else_body),
        Stmt::Match { cases, .. } => cases.iter().find_map(|(_, _, body)| wurfstelle(body)),
        _ => None,
    })
}

/// Namen, die ein Block lokal bindet; verschachtelte Definitionen zaehlen nicht.
fn gebunden(stmts: &[Stmt], namen: &mut HashSet<String>) {
    for stmt in stmts {
        match stmt {
            Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. } => {
                namen.insert(name.clone());
            }
            Stmt::For { var_name, body, .. } | Stmt::ParallelFor { var_name, body, .. } => {
                namen.insert(var_name.clone());
                gebunden(body, namen);
            }
            Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                if let Some(v) = catch_var { namen.insert(v.clone()); }
                gebunden(try_body, namen);
                gebunden(catch_body, namen);
            }
            Stmt::If { body, else_body, .. } => {
                gebunden(body, namen);
                gebunden(else_body, namen);
            }
            Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } => gebunden(body, namen),
            Stmt::Guard { else_body, .. } => gebunden(else_body, namen),
            Stmt::Match { cases, .. } => for (_, _, body) in cases { gebunden(body, namen); },
            Stmt::Export(inner, _) => gebunden(std::slice::from_ref(inner.as_ref()), namen),
            Stmt::Global(g, _) => namen.extend(g.iter().cloned()),
            _ => {}
        }
    }
}


/// Lokale Namen eines Funktions- oder Methodenkoerpers samt Parametern.
fn lokale(params: &[String], body: &[Stmt]) -> HashSet<String> {
    let mut namen: HashSet<String> = params.iter().cloned().collect();
    gebunden(body, &mut namen);
    namen
}

impl<'a> Pass<'a> {
    fn datei_von(&self, name: &str) -> String {
        self.quellen.get(name).cloned().unwrap_or_else(|| self.haupt.to_string())
    }

    /// Lokale Namen plus die vor der Definition gebundenen Top-Level-Variablen.
    fn mit_global(&self, def: &str, mut lokal: HashSet<String>) -> HashSet<String> {
        if let Some(g) = self.global_vor.get(def) {
            lokal.extend(g.iter().cloned());
        }
        lokal
    }

    fn sammeln(&mut self, stmts: &[Stmt]) {
        for stmt in stmts {
            match ohne_export(stmt) {
                Stmt::FunctionDef { name, body, decorators, .. } => {
                    self.nutzer.insert(name.clone());
                    if ist_nativ(decorators) {
                        continue;
                    }
                    if let Some(zeile) = wurfstelle(body) {
                        let datei = self.datei_von(name);
                        self.funktionen.insert(name.clone(), Wurf { datei, zeile, ueber: None });
                    }
                }
                Stmt::ClassDef { name: klasse, body, .. } => {
                    for m in body {
                        if let Stmt::FunctionDef { name, body, .. } = ohne_export(m) {
                            let wurf = wurfstelle(body).map(|zeile| Wurf { datei: self.datei_von(klasse), zeile, ueber: None });
                            self.methoden.entry(name.clone()).or_default().push((klasse.clone(), wurf));
                        }
                    }
                }
                _ => {}
            }
        }
    }

    /// Wurfstelle einer Methode, wenn genau eine Klasse sie definiert.
    fn methode(&self, name: &str, klasse: Option<&str>) -> Option<(String, Wurf)> {
        let liste = self.methoden.get(name)?;
        let (k, wurf) = match klasse {
            Some(k) => liste.iter().find(|(kl, _)| kl == k)?,
            None if liste.len() == 1 => &liste[0],
            None => return None,
        };
        wurf.clone().map(|w| (format!("{k}.{name}"), w))
    }

    /// Q3 (#38339): Funktion bzw. Methode am stdlib-Ort wirft ueber den Aufruf von `name`.
    fn stdlib_wurf(&mut self, ort: &Ort<'_>, zeile: usize, name: &str) {
        let Some(f) = ort.funktion.as_deref() else { return };
        if f.contains(".<lambda@") {
            return; // Lambda-Koerper: wer ihn aufruft, ist statisch unbekannt
        }
        let wurf = Wurf { datei: ort.datei.to_string(), zeile, ueber: Some(name.to_string()) };
        match ort.klasse {
            Some(k) => {
                let m = f.strip_prefix(&format!("{k}.")).unwrap_or(f);
                let eintrag = self.methoden.get_mut(m).and_then(|l| l.iter_mut().find(|(kl, _)| kl == k));
                if let Some((_, w @ None)) = eintrag {
                    *w = Some(wurf);
                    self.neu = true;
                }
            }
            None => {
                if !self.funktionen.contains_key(f) {
                    self.funktionen.insert(f.to_string(), wurf);
                    self.neu = true;
                }
            }
        }
    }

    /// `wurf` = werfende Funktion (M-F001); None = fehlbares Builtin (M-F002).
    fn melde(&mut self, ort: &Ort<'_>, zeile: usize, name: &str, wurf: Option<&Wurf>) {
        // Q3: in stdlib-Modulen nichts melden, die Funktion gilt als werfend.
        if ort.datei != self.haupt && ist_stdlib(ort.datei) {
            self.stdlib_wurf(ort, zeile, name);
            return;
        }
        if self.nur_sammeln {
            return;
        }
        let schluessel = (ort.datei.to_string(), ort.funktion.clone(), name.to_string());
        if let Some(&i) = self.paare.get(&schluessel) {
            self.weitere[i] += 1;
            return;
        }
        self.paare.insert(schluessel, self.befunde.len());
        self.weitere.push(0);
        let wer = match &ort.funktion {
            Some(f) => format!("'{f}' läuft"),
            None => "das Hauptprogramm läuft".to_string(),
        };
        let (code, text) = match wurf {
            Some(Wurf { datei, zeile, ueber: None }) => ("M-F001", format!(
                "Aufruf von '{name}' kann einen Fehler werfen (wirf in '{name}', {datei}:{zeile}) und ist hier nicht mit versuche: geschützt; {wer} danach mit offenem Fehler weiter")),
            Some(Wurf { datei, zeile, ueber: Some(u) }) => ("M-F001", format!(
                "Aufruf von '{name}' kann einen Fehler werfen ('{u}' in '{name}', {datei}:{zeile}) und ist hier nicht mit versuche: geschützt; {wer} danach mit offenem Fehler weiter")),
            None => ("M-F002", format!(
                "Aufruf von '{name}' kann bei äußerem Fehlschlag einen Laufzeitfehler werfen ({}) und ist hier nicht mit versuche: geschützt; {wer} danach mit offenem Fehler weiter",
                builtin_grund(name).unwrap_or("Builtin"))),
        };
        let mut d = Diagnose::hinweis(code, text)
            .datei(ort.datei)
            .ort(zeile, 0)
            .vorschlag(format!("versuche: um den Aufruf von '{name}' legen"));
        if let Some(f) = &ort.funktion {
            d = d.funktion(f.clone());
        }
        self.befunde.push(d);
    }

    fn stmts(&mut self, stmts: &[Stmt], ort: &Ort<'_>, geschuetzt: bool) {
        for stmt in stmts {
            let z = stmt.zeile();
            match stmt {
                Stmt::TryCatch { try_body, catch_body, .. } => {
                    self.stmts(try_body, ort, true);
                    self.stmts(catch_body, ort, geschuetzt);
                }
                Stmt::If { condition, body, else_body, .. } => {
                    self.expr(condition, ort, geschuetzt, z);
                    self.stmts(body, ort, geschuetzt);
                    self.stmts(else_body, ort, geschuetzt);
                }
                Stmt::While { condition: e, body, .. } | Stmt::For { iterable: e, body, .. }
                | Stmt::ParallelFor { iterable: e, body, .. } => {
                    self.expr(e, ort, geschuetzt, z);
                    self.stmts(body, ort, geschuetzt);
                }
                Stmt::UnsafeBlock { body, .. } => self.stmts(body, ort, geschuetzt),
                Stmt::Guard { condition, else_body, .. } => {
                    self.expr(condition, ort, geschuetzt, z);
                    self.stmts(else_body, ort, geschuetzt);
                }
                Stmt::Match { value, cases, .. } => {
                    self.expr(value, ort, geschuetzt, z);
                    for (muster, wache, body) in cases {
                        if let Some(e) = muster { self.expr(e, ort, geschuetzt, z); }
                        if let Some(e) = wache { self.expr(e, ort, geschuetzt, z); }
                        self.stmts(body, ort, geschuetzt);
                    }
                }
                Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
                | Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _)
                | Stmt::Expression(value, _) | Stmt::Return(Some(value), _)
                | Stmt::Throw(value, _) | Stmt::Expect(value, _) => self.expr(value, ort, geschuetzt, z),
                Stmt::PropertyAssignment { object, value, .. } => {
                    self.expr(object, ort, geschuetzt, z);
                    self.expr(value, ort, geschuetzt, z);
                }
                Stmt::IndexAssignment { object, index, value, .. } => {
                    self.expr(object, ort, geschuetzt, z);
                    self.expr(index, ort, geschuetzt, z);
                    self.expr(value, ort, geschuetzt, z);
                }
                Stmt::Export(inner, _) => self.stmts(std::slice::from_ref(inner.as_ref()), ort, geschuetzt),
                // Definitionen prueft `definitionen`; aufräumen, Tests u. a. bleiben still.
                _ => {}
            }
        }
    }

    fn exprs(&mut self, exprs: &[Expr], ort: &Ort<'_>, geschuetzt: bool, zeile: usize) {
        for e in exprs { self.expr(e, ort, geschuetzt, zeile); }
    }

    fn expr(&mut self, expr: &Expr, ort: &Ort<'_>, geschuetzt: bool, zeile: usize) {
        match expr {
            Expr::FunctionCall { zeile: z, name, args } => {
                let z = if *z > 0 { *z } else { zeile };
                if !geschuetzt && !ort.lokal.contains(name) {
                    if let Some(wurf) = self.funktionen.get(name).cloned() {
                        self.melde(ort, z, name, Some(&wurf));
                    } else if !self.nutzer.contains(name) && builtin_grund(name).is_some() {
                        // Stufe 2: fehlbares Builtin (M-F002); eigene Funktionen gehen vor.
                        self.melde(ort, z, name, None);
                    }
                }
                self.exprs(args, ort, geschuetzt, z);
            }
            Expr::MethodCall { object, method, args } => {
                if !geschuetzt && matches!(object.as_ref(), Expr::This) && ort.klasse.is_some() {
                    if let Some((name, wurf)) = self.methode(method, None) {
                        self.melde(ort, zeile, &name, Some(&wurf));
                    }
                }
                self.expr(object, ort, geschuetzt, zeile);
                self.exprs(args, ort, geschuetzt, zeile);
            }
            Expr::New { class_name, args } => {
                if !geschuetzt {
                    if let Some((name, wurf)) = self.methode("erstelle", Some(class_name)) {
                        self.melde(ort, zeile, &name, Some(&wurf));
                    }
                }
                self.exprs(args, ort, geschuetzt, zeile);
            }
            Expr::Lambda { params, body, .. } => {
                let mut lokal = ort.lokal.clone();
                lokal.extend(params.iter().cloned());
                let aussen = ort.funktion.as_deref().unwrap_or("Hauptprogramm");
                let nr = self.lambdas.entry((ort.datei.to_string(), zeile)).or_insert(0);
                *nr += 1;
                let name = match *nr {
                    1 => format!("{aussen}.<lambda@{zeile}>"),
                    n => format!("{aussen}.<lambda@{zeile}#{n}>"),
                };
                let innen = Ort { datei: ort.datei, funktion: Some(name), klasse: ort.klasse, lokal };
                self.expr(body, &innen, false, zeile);
            }
            Expr::Pipe { left, right } => {
                self.expr(left, ort, geschuetzt, zeile);
                if let (false, Expr::Identifier(name)) = (geschuetzt, right.as_ref()) {
                    if ort.lokal.contains(name) {
                        // als Variable gebunden: nicht sicher aufloesbar (049ea57c, 6078a97b)
                    } else if let Some(wurf) = self.funktionen.get(name).cloned() {
                        self.melde(ort, zeile, name, Some(&wurf));
                    }
                }
                self.expr(right, ort, geschuetzt, zeile);
            }
            Expr::BinaryOp { left, right, .. } | Expr::NullishCoalesce { left, right }
            | Expr::Range { start: left, end: right } | Expr::IndexAccess { object: left, index: right } => {
                self.expr(left, ort, geschuetzt, zeile);
                self.expr(right, ort, geschuetzt, zeile);
            }
            Expr::UnaryOp { operand: e, .. } | Expr::PropertyAccess { object: e, .. }
            | Expr::OptionalChain { object: e, .. } | Expr::Spread(e) => self.expr(e, ort, geschuetzt, zeile),
            Expr::List(items) => self.exprs(items, ort, geschuetzt, zeile),
            Expr::Dict(paare) => for (k, v) in paare {
                self.expr(k, ort, geschuetzt, zeile);
                self.expr(v, ort, geschuetzt, zeile);
            },
            Expr::ListComprehension { expr, var_name, iterable, condition } => {
                self.expr(iterable, ort, geschuetzt, zeile);
                let mut innen = ort.clone();
                innen.lokal.insert(var_name.clone());
                self.expr(expr, &innen, geschuetzt, zeile);
                if let Some(c) = condition { self.expr(c, &innen, geschuetzt, zeile); }
            }
            Expr::Ternary { condition, then_val, else_val } => {
                self.expr(condition, ort, geschuetzt, zeile);
                self.expr(then_val, ort, geschuetzt, zeile);
                self.expr(else_val, ort, geschuetzt, zeile);
            }
            Expr::MatchExpr { value, cases } => {
                self.expr(value, ort, geschuetzt, zeile);
                for (muster, wache, ergebnis) in cases {
                    if let Some(e) = muster { self.expr(e, ort, geschuetzt, zeile); }
                    if let Some(e) = wache { self.expr(e, ort, geschuetzt, zeile); }
                    self.expr(ergebnis, ort, geschuetzt, zeile);
                }
            }
            Expr::QueryExpr { source, where_cond, order_expr, select_expr, .. } => {
                self.expr(source, ort, geschuetzt, zeile);
                if let Some(e) = where_cond { self.expr(e, ort, geschuetzt, zeile); }
                if let Some(e) = order_expr { self.expr(e, ort, geschuetzt, zeile); }
                self.expr(select_expr, ort, geschuetzt, zeile);
            }
            _ => {}
        }
    }

    /// Koerper aller Funktionen und Methoden (Hauptdatei und Module).
    fn definitionen(&mut self, stmts: &[Stmt]) {
        for stmt in stmts {
            match ohne_export(stmt) {
                Stmt::FunctionDef { name, params, body, decorators, .. } if !ist_nativ(decorators) => {
                    let datei = self.datei_von(name);
                    let ort = Ort { datei: &datei, funktion: Some(name.clone()), klasse: None, lokal: self.mit_global(name, lokale(params, body)) };
                    self.stmts(body, &ort, false);
                }
                Stmt::ClassDef { name: klasse, body, .. } => {
                    let datei = self.datei_von(klasse);
                    for m in body {
                        if let Stmt::FunctionDef { name, params, body, .. } = ohne_export(m) {
                            let ort = Ort { datei: &datei, funktion: Some(format!("{klasse}.{name}")), klasse: Some(klasse), lokal: self.mit_global(klasse, lokale(params, body)) };
                            self.stmts(body, &ort, false);
                        }
                    }
                }
                _ => {}
            }
        }
    }
}

/// Prueft das aufgeloeste Programm. `haupt` sind die Anweisungen der
/// Hauptdatei vor dem Inlining (ihr Top-Level-Code mit richtiger Datei).
pub fn pruefe(haupt: &[Stmt], program: &Program, datei: &str) {
    // #38315: nur zusammen mit --typpruefung (--typ-streng schliesst es ein).
    if !crate::typpruefung::aktiv() {
        return;
    }
    let mut pass = Pass {
        haupt: datei,
        quellen: &program.funktion_quellen,
        funktionen: HashMap::new(),
        methoden: HashMap::new(),
        befunde: Vec::new(),
        paare: HashMap::new(),
        weitere: Vec::new(),
        lambdas: HashMap::new(),
        nutzer: HashSet::new(),
        nur_sammeln: false,
        neu: false,
        global_vor: {
            let mut vor = HashMap::new();
            let mut bisher = HashSet::new();
            for stmt in &program.statements {
                match ohne_export(stmt) {
                    Stmt::FunctionDef { name, .. } | Stmt::ClassDef { name, .. } => {
                        vor.insert(name.clone(), bisher.clone());
                    }
                    s => gebunden(std::slice::from_ref(s), &mut bisher),
                }
            }
            vor
        },
    };
    pass.sammeln(&program.statements);
    // Q3 (#38339): stdlib-Funktionen, die ungeschuetzt werfen lassen, als werfend
    // nachtragen (Fixpunkt ueber Aufrufketten innerhalb der stdlib).
    pass.nur_sammeln = true;
    loop {
        pass.neu = false;
        pass.definitionen(&program.statements);
        pass.lambdas.clear();
        if !pass.neu {
            break;
        }
    }
    pass.nur_sammeln = false;
    pass.definitionen(&program.statements);
    let ort = Ort { datei, funktion: None, klasse: None, lokal: lokale(&[], haupt) };
    pass.stmts(haupt, &ort, false);
    let streng = crate::typpruefung::streng();
    for (mut d, weitere) in pass.befunde.into_iter().zip(pass.weitere) {
        if weitere > 0 {
            let wort = if weitere == 1 { "weiterer Aufruf" } else { "weitere Aufrufe" };
            d.meldung.push_str(&format!(" (und {weitere} {wort})"));
        }
        if streng {
            d.schwere = Schwere::Fehler;
            d.meldung.push_str(" (--typ-streng)");
            FEHLER.with(|f| f.set(f.get() + 1));
        }
        diagnose::melden(d);
    }
}

/// Abbruch vor dem Codegen, wenn `--typ-streng` Fehlerpfade gemeldet hat.
pub fn ergebnis() -> Result<(), String> {
    match FEHLER.with(|f| f.get()) {
        0 => Ok(()),
        n => Err(format!("{n} unbehandelte Fehlerpfade (--typ-streng)")),
    }
}
