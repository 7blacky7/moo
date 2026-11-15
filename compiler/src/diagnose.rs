//! P2-T97: Einheitliches Diagnoseformat (Plan 00fbd28f, Entscheid #38204).
//!
//! Jede Compilerdiagnose hat einen Code (`M-S001`), eine Schwere (Fehler oder
//! Hinweis), einen Ort (Datei:Zeile:Spalte, soweit bekannt), eine deutsche
//! Meldung und optional erwartet/gefunden/Vorschlag.
//!
//! Ausgabe menschenlesbar (Standard, gcc-artige Kopfzeile + Quellausschnitt)
//! oder als JSON (`--diagnose=json`): englische Schluessel nach Agenten-/LSP-
//! Konvention, Meldungstexte deutsch. Unbekanntes wird `null`, nie erfunden.
//!
//! `KATALOG` ist die einzige Quelle fuer `moo erklaere <CODE>` und die Doku.

use std::cell::RefCell;
use std::collections::HashMap;
use std::fmt::Write;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Schwere {
    Fehler,
    Hinweis,
}

impl Schwere {
    /// Wert des JSON-Felds `severity`.
    pub fn json_name(self) -> &'static str {
        match self {
            Schwere::Fehler => "error",
            Schwere::Hinweis => "hint",
        }
    }

    /// Wort in der menschenlesbaren Kopfzeile.
    pub fn wort(self) -> &'static str {
        match self {
            Schwere::Fehler => "Fehler",
            Schwere::Hinweis => "Hinweis",
        }
    }
}

/// Eine Diagnose. Zeile/Spalte sind 1-basiert; 0 bedeutet "unbekannt".
#[derive(Debug, Clone, PartialEq)]
pub struct Diagnose {
    pub code: &'static str,
    pub schwere: Schwere,
    pub datei: Option<String>,
    pub zeile: usize,
    pub spalte: usize,
    /// Exklusives Spaltenende auf derselben Zeile (0 = nur ein Zeichen markieren).
    pub end_spalte: usize,
    pub funktion: Option<String>,
    pub meldung: String,
    pub erwartet: Option<String>,
    pub gefunden: Option<String>,
    pub vorschlag: Option<String>,
}

impl Diagnose {
    pub fn neu(code: &'static str, schwere: Schwere, meldung: impl Into<String>) -> Self {
        Self {
            code,
            schwere,
            datei: None,
            zeile: 0,
            spalte: 0,
            end_spalte: 0,
            funktion: None,
            meldung: meldung.into(),
            erwartet: None,
            gefunden: None,
            vorschlag: None,
        }
    }

    pub fn fehler(code: &'static str, meldung: impl Into<String>) -> Self {
        Self::neu(code, Schwere::Fehler, meldung)
    }

    pub fn hinweis(code: &'static str, meldung: impl Into<String>) -> Self {
        Self::neu(code, Schwere::Hinweis, meldung)
    }

    pub fn datei(mut self, datei: impl Into<String>) -> Self {
        self.datei = Some(datei.into());
        self
    }

    /// Setzt die Datei nur, wenn noch keine bekannt ist (Modul-Diagnosen
    /// behalten ihre eigene Datei).
    pub fn datei_falls_leer(mut self, datei: impl Into<String>) -> Self {
        if self.datei.is_none() {
            self.datei = Some(datei.into());
        }
        self
    }

    pub fn ort(mut self, zeile: usize, spalte: usize) -> Self {
        self.zeile = zeile;
        self.spalte = spalte;
        self
    }

    pub fn end_spalte(mut self, end_spalte: usize) -> Self {
        self.end_spalte = end_spalte;
        self
    }

    pub fn funktion(mut self, funktion: impl Into<String>) -> Self {
        self.funktion = Some(funktion.into());
        self
    }

    pub fn erwartet(mut self, text: impl Into<String>) -> Self {
        self.erwartet = Some(text.into());
        self
    }

    pub fn gefunden(mut self, text: impl Into<String>) -> Self {
        self.gefunden = Some(text.into());
        self
    }

    pub fn vorschlag(mut self, text: impl Into<String>) -> Self {
        self.vorschlag = Some(text.into());
        self
    }

    /// Kopfzeile `datei:zeile:spalte: Fehler M-XXXX: meldung`; unbekannte
    /// Ortsteile entfallen von rechts.
    pub fn kopfzeile(&self) -> String {
        let mut out = String::new();
        if let Some(datei) = &self.datei {
            out.push_str(datei);
            if self.zeile > 0 {
                let _ = write!(out, ":{}", self.zeile);
                if self.spalte > 0 {
                    let _ = write!(out, ":{}", self.spalte);
                }
            }
            out.push_str(": ");
        } else if self.zeile > 0 {
            let _ = write!(out, "Zeile {}: ", self.zeile);
        }
        let _ = write!(out, "{} {}: {}", self.schwere.wort(), self.code, self.meldung);
        out
    }

    /// Menschenlesbare Darstellung; `quelle` ist der Quelltext der Datei
    /// (fuer den Ausschnitt), falls verfuegbar.
    pub fn menschlich(&self, quelle: Option<&str>) -> String {
        let mut out = self.kopfzeile();
        out.push('\n');
        if let Some(text) = quelle.filter(|_| self.zeile > 0).and_then(|q| q.lines().nth(self.zeile - 1)) {
            let nummer = self.zeile.to_string();
            let rand = " ".repeat(nummer.len());
            let _ = writeln!(out, "    {nummer} | {text}");
            if self.spalte > 0 {
                // Einrueckung zeichengenau nachbilden (Tabs bleiben Tabs).
                let prefix: String = text.chars().take(self.spalte - 1)
                    .map(|c| if c == '\t' { '\t' } else { ' ' }).collect();
                let breite = if self.end_spalte > self.spalte { self.end_spalte - self.spalte } else { 1 };
                let _ = writeln!(out, "    {rand} | {prefix}{}", "^".repeat(breite));
            }
        }
        if self.zeile == 0 {
            if let Some(funktion) = &self.funktion {
                let _ = writeln!(out, "    in Funktion: {funktion}");
            }
        }
        match (&self.erwartet, &self.gefunden) {
            (Some(e), Some(g)) => { let _ = writeln!(out, "    erwartet: {e} | gefunden: {g}"); }
            (Some(e), None) => { let _ = writeln!(out, "    erwartet: {e}"); }
            (None, Some(g)) => { let _ = writeln!(out, "    gefunden: {g}"); }
            (None, None) => {}
        }
        // Steht der Vorschlag schon in der (kompatibel gehaltenen) Meldung,
        // nicht doppelt drucken; JSON fuehrt ihn trotzdem als suggestion.
        if let Some(v) = self.vorschlag.as_ref().filter(|v| !self.meldung.contains(v.as_str())) {
            let _ = writeln!(out, "    Vorschlag: {v}");
        }
        out
    }

    /// Ein JSON-Objekt (eine Zeile), englische Schluessel.
    pub fn json(&self) -> String {
        let zahl = |n: usize| if n > 0 { n.to_string() } else { "null".to_string() };
        let text = |t: &Option<String>| t.as_deref().map(json_string).unwrap_or_else(|| "null".to_string());
        format!(
            "{{\"code\":{},\"severity\":\"{}\",\"file\":{},\"line\":{},\"column\":{},\"end_column\":{},\"function\":{},\"message\":{},\"expected\":{},\"found\":{},\"suggestion\":{}}}",
            json_string(self.code),
            self.schwere.json_name(),
            text(&self.datei),
            zahl(self.zeile),
            zahl(self.spalte),
            zahl(self.end_spalte),
            text(&self.funktion),
            json_string(&self.meldung),
            text(&self.erwartet),
            text(&self.gefunden),
            text(&self.vorschlag),
        )
    }
}

/// Ausgabeformat der Diagnosen (`--diagnose=text|json`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum Format {
    #[default]
    Text,
    Json,
}

impl Format {
    pub fn parse(wert: &str) -> Result<Self, String> {
        match wert {
            "text" | "mensch" | "human" => Ok(Format::Text),
            "json" => Ok(Format::Json),
            andere => Err(format!("--diagnose: unbekanntes Format '{andere}' (erlaubt: text, json)")),
        }
    }
}

/// Sammler aller Diagnosen eines Laufs.
#[derive(Debug, Clone, Default)]
pub struct Diagnosen {
    pub liste: Vec<Diagnose>,
}

impl Diagnosen {
    pub fn push(&mut self, d: Diagnose) {
        self.liste.push(d);
    }

    pub fn extend(&mut self, ds: impl IntoIterator<Item = Diagnose>) {
        self.liste.extend(ds);
    }

    pub fn fehler_anzahl(&self) -> usize {
        self.liste.iter().filter(|d| d.schwere == Schwere::Fehler).count()
    }

    pub fn hinweis_anzahl(&self) -> usize {
        self.liste.iter().filter(|d| d.schwere == Schwere::Hinweis).count()
    }

    pub fn hat_fehler(&self) -> bool {
        self.fehler_anzahl() > 0
    }

    /// Zusammenfassung "2 Fehler, 1 Hinweis" (leer, wenn nichts vorliegt).
    pub fn zusammenfassung(&self) -> String {
        let f = self.fehler_anzahl();
        let h = self.hinweis_anzahl();
        let fehler = format!("{f} Fehler");
        let hinweise = if h == 1 { "1 Hinweis".to_string() } else { format!("{h} Hinweise") };
        match (f, h) {
            (0, 0) => String::new(),
            (_, 0) => fehler,
            (0, _) => hinweise,
            _ => format!("{fehler}, {hinweise}"),
        }
    }

    /// Alle Diagnosen menschenlesbar plus Zusammenfassung. Quelltexte werden
    /// je Datei einmal gelesen (fuer den Ausschnitt; fehlt die Datei, entfaellt er).
    pub fn menschlich(&self) -> String {
        let mut quellen: HashMap<String, Option<String>> = HashMap::new();
        let mut out = String::new();
        for d in &self.liste {
            let quelle = d.datei.as_ref().and_then(|datei| {
                quellen.entry(datei.clone())
                    .or_insert_with(|| std::fs::read_to_string(datei).ok())
                    .clone()
            });
            out.push_str(&d.menschlich(quelle.as_deref()));
        }
        let summe = self.zusammenfassung();
        if !summe.is_empty() {
            out.push_str(&summe);
            out.push('\n');
        }
        out
    }

    /// Genau ein JSON-Objekt (eine Zeile) fuer den ganzen Lauf.
    pub fn json(&self) -> String {
        let eintraege: Vec<String> = self.liste.iter().map(Diagnose::json).collect();
        format!(
            "{{\"schema\":1,\"diagnostics\":[{}],\"errors\":{},\"hints\":{}}}",
            eintraege.join(","),
            self.fehler_anzahl(),
            self.hinweis_anzahl(),
        )
    }

    pub fn ausgeben(&self, format: Format) -> String {
        match format {
            Format::Text => self.menschlich(),
            Format::Json => {
                let mut s = self.json();
                s.push('\n');
                s
            }
        }
    }
}

// ---- Sammler des laufenden Kommandos ----------------------------------------
// Diagnosen entstehen tief in Parser, Modulaufloesung und Codegen, deren
// Fehlerweg `Result<_, String>` ist. Strukturiert abgelegt werden sie hier;
// der Err-String bleibt fuer den bisherigen Kontrollfluss. Ein Kommando gibt
// den Sammler genau einmal aus (`abschliessen`).

#[derive(Default)]
struct Lauf {
    diagnosen: Diagnosen,
    format: Format,
}

thread_local! {
    static LAUF: RefCell<Lauf> = RefCell::new(Lauf::default());
}

pub fn setze_format(format: Format) {
    LAUF.with(|l| l.borrow_mut().format = format);
}

/// Identische Diagnosen (mehrfach durchlaufene Pfade, z. B. Zielaufloesung)
/// werden nur einmal abgelegt.
pub fn melden(d: Diagnose) {
    LAUF.with(|l| {
        let mut l = l.borrow_mut();
        if !l.diagnosen.liste.contains(&d) {
            l.diagnosen.push(d);
        }
    });
}

/// Legt einen Fehler ab und liefert seine Kopfzeile als Err-Text.
pub fn melde_fehler(d: Diagnose) -> String {
    let text = d.kopfzeile();
    melden(d);
    text
}

pub fn hat_fehler() -> bool {
    LAUF.with(|l| l.borrow().diagnosen.hat_fehler())
}

// P2-T98: Code und Zeile fuer Fehler, deren Fehlerweg nur ein String ist
// (nativer Codegen). compile_program uebernimmt sie statt M-N000, wenn der
// Err-Text die vorgemerkte Meldung enthaelt (Rueckfallpfade, die einen
// Fehler verwerfen, beeinflussen spaetere Meldungen dadurch nicht).
thread_local! {
    static VORGEMERKT: RefCell<Vec<(String, &'static str, usize)>> = RefCell::new(Vec::new());
}

/// Merkt Code und Zeile fuer `meldung` vor und liefert die Meldung zurueck.
pub fn vormerken(code: &'static str, zeile: usize, meldung: String) -> String {
    VORGEMERKT.with(|v| v.borrow_mut().push((meldung.clone(), code, zeile)));
    meldung
}

/// Code und Zeile der zuletzt vorgemerkten Meldung, die in `fehler` steckt.
pub fn vorgemerkt(fehler: &str) -> Option<(&'static str, usize)> {
    VORGEMERKT.with(|v| v.borrow().iter().rev()
        .find(|(m, _, _)| fehler.contains(m.as_str()))
        .map(|(_, code, zeile)| (*code, *zeile)))
}

/// Setzt die Datei an allen bisher gemeldeten Diagnosen ohne Datei (z. B.
/// Lexer-Hinweise wie M-H007, der Lexer kennt den Dateinamen nicht).
pub fn datei_nachtragen(datei: &str) {
    LAUF.with(|l| {
        let mut l = l.borrow_mut();
        for d in l.diagnosen.liste.iter_mut() {
            if d.datei.is_none() {
                d.datei = Some(datei.to_string());
            }
        }
    });
}

/// Ein Err-Text ohne strukturierte Diagnose (Aufruf-, Datei-, Linkfehler):
/// nur ablegen, wenn noch kein Fehler gemeldet ist; sonst ist der Text die
/// Folge des bereits gemeldeten Fehlers.
pub fn fehler_aus_text(text: impl Into<String>) {
    if !hat_fehler() {
        melden(Diagnose::fehler("M-W000", text));
    }
}

/// Gibt alle Diagnosen auf stderr aus und leert den Sammler (Text: nur wenn
/// etwas vorliegt; JSON: immer genau ein Objekt). Ergebnis: Fehler dabei?
pub fn abschliessen() -> bool {
    let (ds, format) = LAUF.with(|l| {
        let mut l = l.borrow_mut();
        (std::mem::take(&mut l.diagnosen), l.format)
    });
    let text = ds.ausgeben(format);
    if !text.is_empty() {
        eprint!("{text}");
    }
    ds.hat_fehler()
}

/// JSON-Stringkodierung ohne Annahmen ueber den Inhalt (auch fuer `layout`).
pub fn json_string(value: &str) -> String {
    let mut out = String::from("\"");
    for ch in value.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            c if (c as u32) < 0x20 => {
                let _ = write!(&mut out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

/// Ein Eintrag im Diagnosekatalog.
pub struct Eintrag {
    pub code: &'static str,
    pub titel: &'static str,
    pub erklaerung: &'static str,
}

/// Diagnosekatalog. Bereiche: L Lexer, S Syntax, I Import/Module, C Uebersetzung,
/// N nativer Code, H Hinweise; reserviert: T Typen (P2-T29), M Speicher
/// (P2-T98), F Fehlerpfade (P2-T96).
pub const KATALOG: &[Eintrag] = &[
    Eintrag { code: "M-L001", titel: "Unbekanntes Zeichen",
        erklaerung: "Der Quelltext enthaelt ein Zeichen, das in Moo kein Token beginnt. Zeichen entfernen oder in einen Text (\"...\") setzen." },
    Eintrag { code: "M-L002", titel: "Text nicht geschlossen",
        erklaerung: "Ein Textliteral wurde in derselben Zeile nicht mit dem passenden Anfuehrungszeichen beendet." },
    Eintrag { code: "M-L003", titel: "f-String: '}' fehlt",
        erklaerung: "Ein Platzhalter {ausdruck} in einem f-String wurde nicht mit '}' geschlossen." },
    Eintrag { code: "M-L004", titel: "f-String nicht geschlossen",
        erklaerung: "Ein f-String wurde in derselben Zeile nicht beendet." },
    Eintrag { code: "M-L000", titel: "Lexerfehler",
        erklaerung: "Sonstiger Fehler beim Zerlegen des Quelltexts in Tokens." },
    Eintrag { code: "M-S001", titel: "Token erwartet",
        erklaerung: "An dieser Stelle verlangt die Grammatik ein bestimmtes Zeichen oder Schluesselwort (z. B. ':' nach einer Bedingung). Die Felder erwartet/gefunden nennen beide Seiten, der Vorschlag die uebliche Korrektur." },
    Eintrag { code: "M-S002", titel: "Name erwartet",
        erklaerung: "Hier muss ein Bezeichner (Variablen-, Funktions- oder Typname) stehen. Schluesselwoerter sind als Name nur eingeschraenkt erlaubt." },
    Eintrag { code: "M-S003", titel: "Zeilenende erwartet",
        erklaerung: "Nach einem vollstaendigen Statement steht noch etwas in derselben Zeile (z. B. `setze z auf 3 über 4`). Jedes Statement endet mit der Zeile; den Rest entfernen, einen fehlenden Operator ergaenzen oder ihn in eine eigene Zeile schreiben." },
    Eintrag { code: "M-S000", titel: "Syntaxfehler",
        erklaerung: "Sonstiger Fehler beim Parsen. Die Meldung nennt das Konstrukt." },
    Eintrag { code: "M-I001", titel: "Modul nicht gefunden",
        erklaerung: "Ein importiere-Ziel wurde weder relativ zur Datei noch in den Bibliothekspfaden (--lib-pfad, Pakete) gefunden." },
    Eintrag { code: "M-I002", titel: "Modul definiert den Namen nicht",
        erklaerung: "`aus modul importiere name`: das Modul definiert diesen Namen nicht selbst und gibt ihn auch nicht weiter (Namen aus seinen Importen zaehlen nur, wenn es sie selbst mit `aus x importiere name` holt)." },
    Eintrag { code: "M-I003", titel: "Quelldatei nicht lesbar",
        erklaerung: "Die Quelldatei oder ein importiertes Modul konnte nicht gelesen werden (Pfad, Rechte, Kodierung UTF-8)." },
    Eintrag { code: "M-I004", titel: "Doppelte Definition aus zwei Dateien",
        erklaerung: "Derselbe Top-Level-Name (Funktion, Klasse, Datenklasse, Schnittstelle, Konstante, Struktur, extern; nicht Struktur gegen Struktur oder extern gegen extern) ist in der Datei und einem importierten Modul oder in zwei Modulen definiert. Module werden inline in EINEN Namensraum eingefuegt; es gilt nur die zuerst eingefuegte Definition, die andere wuerde still ersetzt, auch fuer Aufrufe im Modul. `aus m importiere x` fuegt ebenfalls alle Definitionen von m ein, auch interne Helfer. Eine Definition umbenennen oder aus dem anderen Modul importieren statt kopieren." },
    Eintrag { code: "M-I005", titel: "Gleichnamige Top-Level-Variable",
        erklaerung: "Eine Top-Level-Variable der Datei oder eines Moduls traegt denselben Namen wie eine Variable oder Definition einer anderen Datei. Beide teilen denselben globalen Namen (gemeinsamer Zustand); das kann gewollte Konfiguration sein. Sonst umbenennen." },
    Eintrag { code: "M-I006", titel: "Unsicheres Builtin verschattet",
        erklaerung: "Eine Funktion oder ein extern traegt den Namen eines unsicheren Builtins (zeiger, adresse_von, speicher_lesen/-schreiben, puffer_zeiger, volatile_*, port_*, Barrieren, assembler, struktur-/registerfeld_*). Dann waere nicht eindeutig, ob ein Aufruf die eigene Definition oder den Rohzugriff meint, und die unsicher:-Pruefung liesse sich umgehen. Die eigene Definition umbenennen." },
    Eintrag { code: "M-I000", titel: "Importfehler",
        erklaerung: "Sonstiger Fehler beim Aufloesen von Modulen." },
    Eintrag { code: "M-C001", titel: "Funktion unbekannt",
        erklaerung: "Die aufgerufene Funktion ist weder definiert noch importiert noch eingebaut. Der Vorschlag nennt einen aehnlichen bekannten Namen." },
    Eintrag { code: "M-C002", titel: "Methode unbekannt",
        erklaerung: "Die Methode ist fuer den Empfaenger nicht definiert. Der Vorschlag nennt eine aehnliche Methode." },
    Eintrag { code: "M-C000", titel: "Uebersetzungsfehler",
        erklaerung: "Sonstiger Fehler bei der Codeerzeugung. Ort: Datei und Funktion; die Zeile, soweit bekannt." },
    Eintrag { code: "M-N000", titel: "Fehler in nativem Code",
        erklaerung: "Fehler beim Uebersetzen einer @nativ-Funktion, einer Struktur oder einer nativen Konstante (Typen, Layout, Zeiger, Hardwarezugriff)." },
    Eintrag { code: "M-M001", titel: "Konstanter Index ausserhalb eines festen Arrays",
        erklaerung: "Ein Index, der schon beim Uebersetzen feststeht, liegt ausserhalb eines Arrays fester Laenge [T; N] (gueltig 0..N-1); der Zugriff wuerde immer fehlschlagen. Nicht konstante Indizes prueft weiter die Laufzeit (Trap). Puffer haben eine Laufzeitlaenge und werden nur zur Laufzeit geprueft." },
    Eintrag { code: "M-M002", titel: "Geliehener Zeiger entkommt",
        erklaerung: "Ein Zeiger auf Speicher, der nur waehrend des Aufrufs lebt (adresse_von eines Locals oder Parameters, puffer_zeiger), wird zurueckgegeben oder in Speicher geschrieben, der den Aufruf ueberlebt. Nach dem Ende der Funktion zeigt er ins Leere. Dauerhafte Zeiger brauchen Speicher mit eigenem Besitz (Griff, Puffer, globale Daten)." },
    Eintrag { code: "M-M003", titel: "Nutzung nach Freigabe",
        erklaerung: "Ein extern mit @verbraucht(n) gibt das Objekt hinter Parameter n frei; danach gehoert es nicht mehr dem Aufrufer. Wird dieselbe Variable im selben Block danach noch genutzt oder ein zweites Mal freigegeben, ist das sicher ein Fehler. Gemeldet werden nur eindeutige Faelle; Neuzuweisung, Freigabe nur in einem Zweig, Schleifen ueber Iterationen und Aliasse bleiben der Laufzeit ueberlassen." },
    Eintrag { code: "M-M004", titel: "C behaelt geliehenen Zeiger",
        erklaerung: "Ein extern mit @behaelt(n) speichert den Wert aus Parameter n ueber den Aufruf hinaus (z. B. user_data eines Rueckrufs). Ein geliehener Zeiger (adresse_von eines Locals oder Parameters, puffer_zeiger) zeigt nach dem Ende der Funktion ins Leere. Die Daten global, in einem Puffer mit eigenem Besitz oder per Griff halten." },
    Eintrag { code: "M-M005", titel: "Rohzugriff ohne unsicher:",
        erklaerung: "Zeigerumwandlung, adresse_von, speicher_lesen/-schreiben, Hardwarezugriffe und andere Rohzugriffe sind in einer @nativ-Funktion nur innerhalb eines unsicher:-Blocks erlaubt. Die betroffene Zeile in unsicher: einschliessen." },
    Eintrag { code: "M-H001", titel: "Typ-Hinweis",
        erklaerung: "Der Compiler kennt den Typ eines Werts und die Operation passt nicht dazu (z. B. Listenmethode auf einer Zahl). Ohne Typangabe ist das ein Hinweis; das Programm wird trotzdem erzeugt. Mit --typpruefung meldet ihn die Typpruefung, mit --typ-streng ist er ein Fehler." },
    Eintrag { code: "M-H002", titel: "Eigene Struktur ueberdeckt eingebauten Typ",
        erklaerung: "Eine eigene struktur traegt den Namen eines eingebauten Typs; sie gewinnt, die eingebauten Operatoren des Typs gelten dann nicht." },
    Eintrag { code: "M-H003", titel: "Modulalias verdeckt",
        erklaerung: "Eine Variable traegt denselben Namen wie ein Modulalias; `alias.name` greift dann auf die Variable zu. Variable oder Alias umbenennen." },
    Eintrag { code: "M-H004", titel: "Floatliteral rundet auf null",
        erklaerung: "Das Literal ist kleiner als der kleinste darstellbare Wert des Zieltyps und wird zu 0. Groesseren Typ (f64) waehlen oder 0 schreiben." },
    Eintrag { code: "M-H005", titel: "Unbekanntes Ziel",
        erklaerung: "--target nennt kein bekanntes Ziel; der Wert wird unveraendert als LLVM-Triple benutzt. Liste: `moo-compiler targets`." },
    Eintrag { code: "M-H006", titel: "Unsinniger Operator zwischen Zahl und Text",
        erklaerung: "Nur mit --typpruefung: Ein Operand ist sicher Text, der andere sicher Zahl oder Text, und die Laufzeit rechnet den Text als 0 (-, /, %, **, Text * Text, <, >, <=, >=) oder vergleicht verschiedene Typen (== ist immer falsch, != immer wahr). Text als Divisor wirft bei / \"Division durch Null!\" und liefert nichts, bei % entsteht NaN. Text * Zahl (Wiederholung) und + (Verkettung) sind gewollt und bleiben still. Abhilfe: zahl(...) bzw. text(...). Mit --typ-streng ein Fehler." },
    Eintrag { code: "M-H007", titel: "Zahlliteral ohne Ziffern oder zu gross",
        erklaerung: "Ein 0x/0b/0o-Literal hat keine Ziffern oder ueberschreitet u64. Im dynamischen Code wird es wie bisher zu 0, im nativen Code ist es ein Fehler. Ziffern ergaenzen oder den Wert verkleinern." },
    Eintrag { code: "M-T001", titel: "Typ passt nicht zur Annotation",
        erklaerung: "Der Wert einer Zuweisung, Konstante oder eines Parameter-Standardwerts hat sicher einen anderen Typ als die Annotation (z. B. `setze x: Zahl auf \"a\"`), oder eine annotierte Variable bekommt spaeter einen Wert anderen Typs. Eigene Klassen gehen vor eingebauten Typnamen; unbekannte Typen werden nicht geprueft." },
    Eintrag { code: "M-T002", titel: "Argumenttyp passt nicht zum Parameter",
        erklaerung: "Ein Argument beim Aufruf einer eigenen Funktion oder Methode hat sicher einen anderen Typ als die Annotation des Parameters. `nichts` als Argument ist nur ein Hinweis. Builtins, importierte und @nativ-Funktionen werden nicht geprueft." },
    Eintrag { code: "M-T004", titel: "Argumentzahl passt nicht (Hinweis)",
        erklaerung: "Ein Aufruf einer eigenen Funktion oder Methode hat mehr Argumente als Parameter oder weniger als Parameter ohne Standardwert. Das Programm wird erzeugt: ueberzaehlige Argumente werden verworfen, fehlende sind nichts." },
    Eintrag { code: "M-T003", titel: "Rueckgabetyp passt nicht",
        erklaerung: "gib_zurück liefert sicher einen anderen Typ als der Rueckgabetyp der Funktion (-> T). `gib_zurück` ohne Wert liefert Nichts." },
    Eintrag { code: "M-T005", titel: "Element- oder Feldtyp passt nicht",
        erklaerung: "Ein Element einer als Liste[T] bzw. Dict[K, V] annotierten Variable (Literal, Index-Zuweisung, hinzufügen/append) oder ein Feld einer eigenen Datenklasse (neu D(...), d.feld = wert) hat sicher einen anderen Typ als deklariert. `nichts` ist nur ein Hinweis. Gelesene Elemente gelten als unbekannt; normale Klassen, selbst.feld und Spread werden nicht geprueft." },
    Eintrag { code: "M-F001", titel: "Fehler kann unbehandelt durchlaufen",
        erklaerung: "Die aufgerufene Funktion wirft selbst einen Fehler (wirf ausserhalb ihres versuche-Teils), und der Aufruf steht nicht in versuche:. Zur Laufzeit meldet Moo den Fehler als Hinweis und der Aufrufer laeuft mit offenem Fehler weiter, bis ein aeusseres versuche: ihn faengt. Je Kette wird nur die innerste Stelle gemeldet, je Paar aus aufrufender und gerufener Funktion ein Hinweis (weitere Stellen als Anzahl). Abhilfe: versuche: um den Aufruf legen. Die Pruefung laeuft mit --typpruefung; mit --typ-streng ist es ein Fehler. Geprueft werden Namensaufrufe, selbst.m() und neu K(); Methoden anderer Objekte, Funktionswerte, als Variable gebundene Namen (Parameter, lokale Variablen, global-Anweisung, Lambda-Parameter sowie globale Variablen, die vor der aufrufenden Funktion gebunden sind) und Top-Level-Code importierter Module bleiben ungeprueft." },
    Eintrag { code: "M-W000", titel: "Werkzeug- oder Aufruffehler",
        erklaerung: "Fehler ausserhalb des Quelltexts: Kommandozeile, Dateien, Linker, externe Werkzeuge." },
];

pub fn eintrag(code: &str) -> Option<&'static Eintrag> {
    KATALOG.iter().find(|e| e.code.eq_ignore_ascii_case(code))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn kopfzeile_und_ausschnitt() {
        let d = Diagnose::fehler("M-S001", "':' erwartet, aber Zeilenende gefunden.")
            .datei("a.moos").ort(2, 11).erwartet("':'").gefunden("Zeilenende");
        let text = d.menschlich(Some("zeige 1\nwenn x > 1\n"));
        assert!(text.starts_with("a.moos:2:11: Fehler M-S001: ':' erwartet, aber Zeilenende gefunden.\n"));
        assert!(text.contains("    2 | wenn x > 1\n"));
        assert!(text.contains("      |           ^\n"));
        assert!(text.contains("erwartet: ':' | gefunden: Zeilenende"));
    }

    #[test]
    fn unbekannter_ort_entfaellt() {
        let d = Diagnose::fehler("M-C000", "kaputt").datei("a.moos").funktion("f");
        assert_eq!(d.kopfzeile(), "a.moos: Fehler M-C000: kaputt");
        assert!(d.json().contains("\"line\":null"));
        assert!(d.json().contains("\"function\":\"f\""));
    }

    #[test]
    fn json_lauf() {
        let mut ds = Diagnosen::default();
        ds.push(Diagnose::fehler("M-L001", "Unbekanntes Zeichen: '\"'").datei("x.moos").ort(1, 3));
        ds.push(Diagnose::hinweis("M-H001", "Hinweis"));
        let j = ds.json();
        assert!(j.starts_with("{\"schema\":1,\"diagnostics\":[{\"code\":\"M-L001\",\"severity\":\"error\""));
        assert!(j.contains("\"message\":\"Unbekanntes Zeichen: '\\\"'\""));
        assert!(j.ends_with("\"errors\":1,\"hints\":1}"));
        assert_eq!(ds.zusammenfassung(), "1 Fehler, 1 Hinweis");
    }

    #[test]
    fn katalog_eindeutig() {
        for (i, a) in KATALOG.iter().enumerate() {
            assert!(KATALOG[i + 1..].iter().all(|b| b.code != a.code), "doppelt: {}", a.code);
        }
        assert!(eintrag("m-s001").is_some());
    }
}
