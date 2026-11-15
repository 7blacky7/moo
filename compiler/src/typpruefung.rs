//! P2-T29: Typpruefung zur Compilezeit fuer dynamischen Code (Entwurf
//! a393ce15), nur mit `--typpruefung`. Lesender AST-Pass je Quelldatei:
//! compile/pruefe rufen ihn fuer die Hauptdatei, resolve_modules fuer jedes
//! Modul vor dem Inlining (jede Meldung traegt ihre Datei).
//!
//! GRUNDSATZ (Koordinator, Review da045f85): Ein Fehler nur, wenn beide Typen
//! sicher bekannt sind UND der Fluss eindeutig ist. Alles Unsichere ist
//! hoechstens ein Hinweis oder still. Daraus folgt:
//! - Der Wertetyp einer Variable stammt nur aus tatsaechlich zugewiesenen
//!   Werten (alle Zuweisungen im Bereich gleich), nie aus einer Annotation.
//!   Parameter, `global`-Namen, Schleifen-/Fangvariablen, +=, Musternamen
//!   haben keinen sicheren Wert.
//! - Ein Variablenzugriff zaehlt nur, wenn eine Zuweisung ihn dominiert
//!   (sequenziell im selben Block oder aussen davor; Zweige und Schleifen
//!   geben nichts nach aussen weiter, nur `unsicher:` laeuft immer).
//! - Neuzuweisung wird nur gegen eine dominierende, im Bereich eindeutige
//!   Annotation geprueft (nicht bei global mutierbaren Namen).
//! - `nichts` statt eines Werts ist ein Hinweis (optionaler Wert).
//! - Rueckgabetypen von Aufrufen gelten nicht als sicher (Unbekannt).
//!
//! Meldungen: M-T001 Zuweisung/Konstante/Neuzuweisung/Standardwert,
//! M-T002 Argumenttyp, M-T003 Rueckgabe, M-T004 Argumentzahl (Hinweis: der
//! Codegen fuellt fehlende mit nichts, verwirft ueberzaehlige). Geprueft
//! werden nur eigene Funktionen/Methoden der Datei; Builtins, Importe,
//! @nativ/extern bleiben still. Der Pass aendert den AST nicht.

use std::cell::Cell;
use std::collections::{HashMap, HashSet};

use crate::ast::{BinOp, Decorator, Expr, Program, Stmt, TypeKind, TypeRef, UnaryOpKind};
use crate::diagnose::{self, Diagnose, Schwere};

/// Typgitter der dynamischen Ebene (Namen wie codegen::infer_type).
#[derive(Debug, Clone, PartialEq)]
enum Ty {
    Zahl,
    Text,
    Bool,
    Nichts,
    Liste,
    Dict,
    /// Instanz einer eigenen Klasse oder Datenklasse dieser Datei.
    Objekt(String),
    /// Nicht sicher bestimmbar; passt zu allem.
    Unbekannt,
}

impl Ty {
    fn name(&self) -> &str {
        match self {
            Ty::Zahl => "Zahl",
            Ty::Text => "Text",
            Ty::Bool => "Bool",
            Ty::Nichts => "Nichts",
            Ty::Liste => "Liste",
            Ty::Dict => "Dict",
            Ty::Objekt(klasse) => klasse,
            Ty::Unbekannt => "unbekannt",
        }
    }

    /// Sicherer eingebauter Werttyp: Operatoren laufen dann ueber die
    /// Laufzeit, nie ueber eine __op__-Ueberladung einer Klasse.
    fn eingebaut(&self) -> bool {
        !matches!(self, Ty::Objekt(_) | Ty::Unbekannt)
    }
}

fn vereinen(a: Ty, b: Ty) -> Ty {
    if a == b { a } else { Ty::Unbekannt }
}

fn ist_nativ(decorators: &[Decorator]) -> bool {
    decorators.iter().any(|d| d.name == "nativ" || d.name == "native")
}

/// Signatur einer eigenen Funktion/Methode (S2).
#[derive(Debug, Clone)]
struct Signatur {
    params: Vec<String>,
    typen: Vec<Ty>,
    /// Mindestzahl Argumente: bis zum letzten Parameter ohne Standardwert.
    pflicht: usize,
}

thread_local! {
    static AKTIV: Cell<bool> = const { Cell::new(false) };
    static FEHLER: Cell<usize> = const { Cell::new(0) };
}

/// Schaltet den Pass fuer das laufende Kommando ein (compile/pruefe mit
/// `--typpruefung`). `ast` und `layout` pruefen nie.
pub fn aktivieren() {
    AKTIV.with(|a| a.set(true));
}

/// Prueft eine Quelldatei, falls der Pass aktiv ist, und meldet die Befunde.
pub fn pruefe_datei(program: &Program, datei: &str) {
    if !AKTIV.with(|a| a.get()) {
        return;
    }
    for d in pruefe(program, datei) {
        if d.schwere == Schwere::Fehler {
            FEHLER.with(|f| f.set(f.get() + 1));
        }
        diagnose::melden(d);
    }
}

/// Abbruch vor dem Codegen, wenn der Pass Fehler gemeldet hat.
pub fn ergebnis() -> Result<(), String> {
    match FEHLER.with(|f| f.get()) {
        0 => Ok(()),
        n => Err(format!("{n} Typfehler")),
    }
}

/// Alle Befunde einer Datei (ohne sie zu melden).
pub fn pruefe(program: &Program, datei: &str) -> Vec<Diagnose> {
    let mut p = Pruefer {
        datei,
        klassen: HashMap::new(),
        fremd: HashSet::new(),
        funktionen: HashMap::new(),
        methoden: HashMap::new(),
        lokal: Vec::new(),
        befunde: Vec::new(),
    };
    p.typen_sammeln(&program.statements);
    p.signaturen_sammeln(&program.statements);
    // `global x` in einer Funktion schreibt die Top-Level-Variable x.
    let mut globale = HashSet::new();
    globale_sammeln(&program.statements, &mut globale);
    let kontext = Kontext { funktion: None, rueckgabe: None, klasse: None };
    p.bereich_pruefen(&program.statements, Vec::new(), globale, &kontext);
    p.befunde
}

struct Pruefer<'d> {
    datei: &'d str,
    /// Eigene Klassen und Datenklassen -> Elternklasse.
    klassen: HashMap<String, Option<String>>,
    /// Eigene oder importierte Namen ohne pruefbaren Typ (Strukturen,
    /// Interfaces, importierte Namen): solche Annotationen bleiben ungeprueft.
    fremd: HashSet<String>,
    /// Eigene Funktionen der Datei; None = nicht eindeutig (mehrfach
    /// definiert, extern, @nativ) und damit ungeprueft.
    funktionen: HashMap<String, Option<Signatur>>,
    /// (Klasse, Methode) -> Signatur; None = mehrfach definiert oder @nativ.
    methoden: HashMap<(String, String), Option<Signatur>>,
    /// Lokale Namen der umschliessenden Bereiche (verschatten Funktionen).
    lokal: Vec<HashSet<String>>,
    befunde: Vec<Diagnose>,
}

/// Variablen eines Bereichs (Hauptprogramm, Funktion, Klassenkoerper, Test).
#[derive(Default)]
struct Bereich<'a> {
    /// Alle zugewiesenen Werte je Name (auch die annotierter Zuweisungen).
    werte: HashMap<String, Vec<&'a Expr>>,
    /// Alle Annotationen je Name (inkl. Parameter).
    annotiert: HashMap<String, Vec<Ty>>,
    /// Namen ohne sicheren Wert: Parameter, global, Schleife, Fang, +=, Muster.
    unsicher: HashSet<String>,
    /// Von aussen mutierbar (`global`): keine Deklarationspruefung.
    global: HashSet<String>,
}

/// Was an einer Programmstelle sicher vorangeht (dominiert).
#[derive(Clone, Default)]
struct Fluss {
    zugewiesen: HashSet<String>,
    deklariert: HashMap<String, Ty>,
}

/// Sicht fuer die Ableitung: Bereichstypen, optional nur dominiert
/// zugewiesene Namen.
struct Sicht<'e> {
    env: &'e HashMap<String, Ty>,
    nur: Option<&'e HashSet<String>>,
}

struct Kontext {
    funktion: Option<String>,
    rueckgabe: Option<Ty>,
    /// Gesetzt nur im Klassenkoerper selbst (Methodennamen Klasse.methode).
    klasse: Option<String>,
}

impl<'d> Pruefer<'d> {
    fn typen_sammeln(&mut self, stmts: &[Stmt]) {
        for stmt in stmts {
            match stmt {
                Stmt::ClassDef { name, parent, .. } => {
                    self.klassen.insert(name.clone(), parent.clone());
                }
                Stmt::DataClassDef { name, .. } => {
                    self.klassen.insert(name.clone(), None);
                }
                Stmt::StructDef { name, .. } | Stmt::InterfaceDef { name, .. } => {
                    self.fremd.insert(name.clone());
                }
                Stmt::Import { names, alias, .. } => {
                    self.fremd.extend(names.iter().cloned());
                    if let Some(alias) = alias {
                        self.fremd.insert(alias.clone());
                    }
                }
                Stmt::Export(inner, _) => self.typen_sammeln(std::slice::from_ref(inner.as_ref())),
                _ => {}
            }
        }
    }

    /// S2: Signaturen der eigenen Top-Level-Funktionen und Klassenmethoden.
    /// Ein Name, der in der Datei mehrfach als Funktion (in beliebiger Tiefe)
    /// oder als extern-Funktion vorkommt, bleibt ungeprueft.
    fn signaturen_sammeln(&mut self, stmts: &[Stmt]) {
        let mut anzahl: HashMap<String, usize> = HashMap::new();
        funktionsnamen_zaehlen(stmts, &mut anzahl);
        for stmt in stmts {
            let stmt = match stmt {
                Stmt::Export(inner, _) => inner.as_ref(),
                other => other,
            };
            match stmt {
                Stmt::FunctionDef { name, .. } => {
                    let sig = if anzahl.get(name).copied().unwrap_or(0) == 1 { self.signatur(stmt) } else { None };
                    self.funktionen.insert(name.clone(), sig);
                }
                Stmt::ExternC { name, .. } => {
                    self.funktionen.insert(name.clone(), None);
                }
                Stmt::ClassDef { name: klasse, body, .. } => {
                    for methode in body {
                        if let Stmt::FunctionDef { name, .. } = methode {
                            let schluessel = (klasse.clone(), name.clone());
                            let sig = if self.methoden.contains_key(&schluessel) { None } else { self.signatur(methode) };
                            self.methoden.insert(schluessel, sig);
                        }
                    }
                }
                _ => {}
            }
        }
    }

    fn signatur(&self, stmt: &Stmt) -> Option<Signatur> {
        let Stmt::FunctionDef { params, param_types, defaults, decorators, .. } = stmt else {
            return None;
        };
        if ist_nativ(decorators) {
            return None;
        }
        let typen = (0..params.len())
            .map(|i| param_types.get(i).and_then(|t| t.as_ref()).map(|t| self.aus_annotation(t)).unwrap_or(Ty::Unbekannt))
            .collect();
        let pflicht = (0..params.len())
            .filter(|i| !matches!(defaults.get(*i), Some(Some(_))))
            .map(|i| i + 1)
            .max()
            .unwrap_or(0);
        Some(Signatur { params: params.clone(), typen, pflicht })
    }

    /// Signatur einer aufgerufenen eigenen Funktion, sofern der Name nicht
    /// lokal verschattet ist (Variable, Parameter, Lambda-/Schleifenvariable).
    fn funktion_sig(&self, name: &str) -> Option<Signatur> {
        if self.lokal.iter().any(|namen| namen.contains(name)) {
            return None;
        }
        self.funktionen.get(name).cloned().flatten()
    }

    /// Methode einer eigenen Klasse (entlang der Elternkette in dieser
    /// Datei). Hat die Klasse hier eine Unterklasse, koennte ein Override mit
    /// anderer Signatur greifen: dann ungeprueft.
    fn methode_sig(&self, klasse: &str, methode: &str) -> Option<Signatur> {
        if self.klassen.values().any(|eltern| eltern.as_deref() == Some(klasse)) {
            return None;
        }
        let mut aktuell = klasse.to_string();
        for _ in 0..64 {
            if let Some(eintrag) = self.methoden.get(&(aktuell.clone(), methode.to_string())) {
                return eintrag.clone();
            }
            match self.klassen.get(&aktuell) {
                Some(Some(eltern)) => aktuell = eltern.clone(),
                _ => return None,
            }
        }
        None
    }

    /// Typ einer Annotation. Eigene Klassen gehen vor eingebauten Namen
    /// (z. B. `klasse Zahl`); native und unbekannte Namen bleiben ungeprueft.
    fn aus_annotation(&self, ty: &TypeRef) -> Ty {
        let (name, generisch) = match &ty.kind {
            TypeKind::Name(name) => (name.as_str(), false),
            TypeKind::Generic { name, .. } => (name.as_str(), true),
            _ => return Ty::Unbekannt,
        };
        if self.klassen.contains_key(name) {
            return if generisch { Ty::Unbekannt } else { Ty::Objekt(name.to_string()) };
        }
        if self.fremd.contains(name) {
            return Ty::Unbekannt;
        }
        match name.to_lowercase().as_str() {
            "liste" | "list" => Ty::Liste,
            "dict" | "wörterbuch" | "woerterbuch" => Ty::Dict,
            _ if generisch => Ty::Unbekannt,
            "zahl" | "number" => Ty::Zahl,
            "text" | "string" | "str" => Ty::Text,
            "bool" | "boolean" | "wahrheitswert" => Ty::Bool,
            "nichts" | "none" => Ty::Nichts,
            _ => Ty::Unbekannt,
        }
    }

    /// Sicher abgeleiteter Typ eines Ausdrucks, sonst Unbekannt. Aufrufe
    /// (auch eigener Funktionen mit -> T) gelten nicht als sicher.
    fn ableiten(&self, expr: &Expr, s: &Sicht) -> Ty {
        match expr {
            Expr::Number(_) | Expr::NativeNumber { .. } => Ty::Zahl,
            Expr::String(_) => Ty::Text,
            Expr::Boolean(_) => Ty::Bool,
            Expr::None => Ty::Nichts,
            Expr::List(_) | Expr::ListComprehension { .. } | Expr::Range { .. } => Ty::Liste,
            Expr::Dict(_) => Ty::Dict,
            Expr::New { class_name, .. } if self.klassen.contains_key(class_name) => {
                Ty::Objekt(class_name.clone())
            }
            Expr::Identifier(name) => {
                if s.nur.is_some_and(|nur| !nur.contains(name)) {
                    return Ty::Unbekannt;
                }
                s.env.get(name).cloned().unwrap_or(Ty::Unbekannt)
            }
            Expr::BinaryOp { left, op, right } => {
                binaer(op, self.ableiten(left, s), self.ableiten(right, s))
            }
            Expr::UnaryOp { op: UnaryOpKind::Not, .. } => Ty::Bool,
            Expr::UnaryOp { operand, .. } => match self.ableiten(operand, s) {
                Ty::Zahl => Ty::Zahl,
                _ => Ty::Unbekannt,
            },
            Expr::Ternary { then_val, else_val, .. } => {
                vereinen(self.ableiten(then_val, s), self.ableiten(else_val, s))
            }
            _ => Ty::Unbekannt,
        }
    }

    fn passt(&self, erwartet: &Ty, gefunden: &Ty) -> bool {
        match (erwartet, gefunden) {
            (Ty::Unbekannt, _) | (_, Ty::Unbekannt) => true,
            // Objektreferenzen duerfen leer sein.
            (Ty::Objekt(_), Ty::Nichts) => true,
            (Ty::Objekt(vorfahr), Ty::Objekt(kind)) => self.erbt(kind, vorfahr),
            (a, b) => a == b,
        }
    }

    /// None = passt. `nichts` statt eines Werts ist nur ein Hinweis; sonst
    /// (beide Typen sicher, siehe passt) ein Fehler.
    fn urteil(&self, erwartet: &Ty, gefunden: &Ty) -> Option<Schwere> {
        if self.passt(erwartet, gefunden) {
            None
        } else if *gefunden == Ty::Nichts {
            Some(Schwere::Hinweis)
        } else {
            Some(Schwere::Fehler)
        }
    }

    /// Ist `kind` gleich `vorfahr` oder davon abgeleitet? Eine Elternklasse
    /// aus einer anderen Datei ist hier nicht entscheidbar und gilt als passend.
    fn erbt(&self, kind: &str, vorfahr: &str) -> bool {
        let mut aktuell = kind.to_string();
        for _ in 0..64 {
            if aktuell == vorfahr {
                return true;
            }
            match self.klassen.get(&aktuell) {
                Some(Some(eltern)) => aktuell = eltern.clone(),
                Some(None) => return false,
                None => return true,
            }
        }
        true
    }

    /// Variablen eines Bereichs einsammeln (ohne verschachtelte Funktionen,
    /// Klassen und Tests: die haben eigene Bereiche).
    fn sammeln<'a>(&self, stmts: &'a [Stmt], b: &mut Bereich<'a>) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { name, ty, value, .. } | Stmt::ConstAssignment { name, ty, value, .. } => {
                    if let Some(ty) = ty {
                        let typ = self.aus_annotation(ty);
                        b.annotiert.entry(name.clone()).or_default().push(typ);
                    }
                    b.werte.entry(name.clone()).or_default().push(value);
                }
                Stmt::CompoundAssignment { name, .. } => {
                    b.unsicher.insert(name.clone());
                }
                Stmt::For { var_name, body, .. } | Stmt::ParallelFor { var_name, body, .. } => {
                    b.unsicher.insert(var_name.clone());
                    self.sammeln(body, b);
                }
                Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                    if let Some(var) = catch_var {
                        b.unsicher.insert(var.clone());
                    }
                    self.sammeln(try_body, b);
                    self.sammeln(catch_body, b);
                }
                Stmt::Global(namen, _) => {
                    b.unsicher.extend(namen.iter().cloned());
                    b.global.extend(namen.iter().cloned());
                }
                Stmt::If { body, else_body, .. } => {
                    self.sammeln(body, b);
                    self.sammeln(else_body, b);
                }
                Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } => self.sammeln(body, b),
                Stmt::Guard { else_body, .. } => self.sammeln(else_body, b),
                Stmt::Match { cases, .. } => {
                    for (muster, _, body) in cases {
                        if let Some(muster) = muster {
                            namen_in(muster, &mut b.unsicher);
                        }
                        self.sammeln(body, b);
                    }
                }
                Stmt::Export(inner, _) => self.sammeln(std::slice::from_ref(inner.as_ref()), b),
                _ => {}
            }
        }
    }

    /// Wertetyp je Variable: Vereinigung ALLER zugewiesenen Werte im Bereich
    /// (gleich = sicher, sonst Unbekannt). Annotationen zaehlen nicht als
    /// Wert. Unsichere Namen fehlen. Fixpunkt ab Unbekannt (monoton).
    fn umgebung(&self, b: &Bereich) -> HashMap<String, Ty> {
        let mut env: HashMap<String, Ty> = HashMap::new();
        let mut kandidaten: Vec<&String> = b.werte.keys().filter(|name| !b.unsicher.contains(*name)).collect();
        kandidaten.sort();
        for _ in 0..=kandidaten.len().min(32) {
            let mut geaendert = false;
            for name in &kandidaten {
                let typ = {
                    let s = Sicht { env: &env, nur: None };
                    b.werte[*name].iter().map(|wert| self.ableiten(wert, &s)).reduce(vereinen).unwrap_or(Ty::Unbekannt)
                };
                if env.get(*name) != Some(&typ) {
                    env.insert((*name).clone(), typ);
                    geaendert = true;
                }
            }
            if !geaendert {
                break;
            }
        }
        env
    }

    /// Eindeutige Annotation eines Namens im Bereich (alle gleich).
    fn einheitlich(b: &Bereich, name: &str) -> Option<Ty> {
        let annotationen = b.annotiert.get(name)?;
        let erster = annotationen.first()?;
        annotationen.iter().all(|t| t == erster).then(|| erster.clone())
    }

    fn bereich_pruefen(&mut self, stmts: &[Stmt], parameter: Vec<(String, Option<Ty>)>, globale: HashSet<String>, k: &Kontext) {
        let mut b = Bereich::default();
        b.unsicher.extend(globale.iter().cloned());
        b.global = globale;
        let mut fluss = Fluss::default();
        for (name, typ) in parameter {
            b.unsicher.insert(name.clone());
            fluss.zugewiesen.insert(name.clone());
            if let Some(typ) = typ {
                b.annotiert.entry(name.clone()).or_default().push(typ.clone());
                fluss.deklariert.insert(name, typ);
            }
        }
        self.sammeln(stmts, &mut b);
        let lokal: HashSet<String> = b.werte.keys().chain(b.annotiert.keys()).chain(b.unsicher.iter())
            .cloned()
            .collect();
        self.lokal.push(lokal);
        let env = self.umgebung(&b);
        self.block_pruefen(stmts, &b, &env, k, &fluss);
        self.lokal.pop();
    }

    /// Sequenziell: was ein Statement sicher zuweist bzw. deklariert, gilt fuer
    /// die folgenden Statements dieses Blocks und deren Unterbloecke.
    /// Liefert den Fluss am Blockende (nur `unsicher:` gibt ihn weiter).
    fn block_pruefen(&mut self, stmts: &[Stmt], b: &Bereich, env: &HashMap<String, Ty>, k: &Kontext, start: &Fluss) -> Fluss {
        let mut fluss = start.clone();
        for stmt in stmts {
            self.stmt_ausdruecke(stmt, &Sicht { env, nur: Some(&fluss.zugewiesen) }, k);
            match stmt {
                Stmt::Assignment { zeile, name, ty, value }
                | Stmt::ConstAssignment { zeile, name, ty, value } => {
                    self.zuweisung_pruefen(*zeile, name, ty.as_ref(), value, b, env, &fluss, k);
                    fluss.zugewiesen.insert(name.clone());
                    if ty.is_some() && !b.global.contains(name) {
                        if let Some(typ) = Self::einheitlich(b, name) {
                            fluss.deklariert.insert(name.clone(), typ);
                        }
                    }
                }
                Stmt::Return(wert, zeile) => {
                    if let Some(erwartet) = &k.rueckgabe {
                        let gefunden = match wert {
                            Some(wert) => self.ableiten(wert, &Sicht { env, nur: Some(&fluss.zugewiesen) }),
                            None => Ty::Nichts,
                        };
                        if let Some(schwere) = self.urteil(erwartet, &gefunden) {
                            let funktion = k.funktion.as_deref().unwrap_or("?");
                            let d = Diagnose::neu("M-T003", schwere, format!(
                                "'{funktion}' gibt laut Signatur {} zurück, hier aber {}",
                                erwartet.name(), gefunden.name()))
                                .ort(*zeile, 0)
                                .erwartet(erwartet.name())
                                .gefunden(gefunden.name())
                                .vorschlag(format!(
                                    "Rückgabewert anpassen oder den Rückgabetyp auf -> {} ändern",
                                    gefunden.name()));
                            self.melden(d, k);
                        }
                    }
                }
                Stmt::If { body, else_body, .. } => {
                    self.block_pruefen(body, b, env, k, &fluss);
                    self.block_pruefen(else_body, b, env, k, &fluss);
                }
                Stmt::While { body, .. } => {
                    self.block_pruefen(body, b, env, k, &fluss);
                }
                // `unsicher:` laeuft immer: sein Fluss gilt danach weiter.
                Stmt::UnsafeBlock { body, .. } => {
                    fluss = self.block_pruefen(body, b, env, k, &fluss);
                }
                Stmt::For { var_name, body, .. } | Stmt::ParallelFor { var_name, body, .. } => {
                    let mut innen = fluss.clone();
                    innen.zugewiesen.insert(var_name.clone());
                    self.block_pruefen(body, b, env, k, &innen);
                }
                Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                    self.block_pruefen(try_body, b, env, k, &fluss);
                    let mut innen = fluss.clone();
                    if let Some(var) = catch_var {
                        innen.zugewiesen.insert(var.clone());
                    }
                    self.block_pruefen(catch_body, b, env, k, &innen);
                }
                Stmt::Guard { else_body, .. } => {
                    self.block_pruefen(else_body, b, env, k, &fluss);
                }
                Stmt::Match { cases, .. } => {
                    for (_, _, body) in cases {
                        self.block_pruefen(body, b, env, k, &fluss);
                    }
                }
                Stmt::Export(inner, _) => {
                    fluss = self.block_pruefen(std::slice::from_ref(inner.as_ref()), b, env, k, &fluss);
                }
                Stmt::FunctionDef { .. } => self.funktion_pruefen(stmt, k.klasse.as_deref()),
                Stmt::ClassDef { name, body, .. } => {
                    let kontext = Kontext { funktion: None, rueckgabe: None, klasse: Some(name.clone()) };
                    self.bereich_pruefen(body, Vec::new(), HashSet::new(), &kontext);
                }
                Stmt::TestDef { body, .. } => {
                    let kontext = Kontext { funktion: None, rueckgabe: None, klasse: None };
                    self.bereich_pruefen(body, Vec::new(), HashSet::new(), &kontext);
                }
                _ => {}
            }
        }
        fluss
    }

    #[allow(clippy::too_many_arguments)]
    fn zuweisung_pruefen(&mut self, zeile: usize, name: &str, ty: Option<&TypeRef>, wert: &Expr,
                         b: &Bereich, env: &HashMap<String, Ty>, fluss: &Fluss, k: &Kontext) {
        let gefunden = self.ableiten(wert, &Sicht { env, nur: Some(&fluss.zugewiesen) });
        match ty {
            Some(annotation) => {
                let erwartet = self.aus_annotation(annotation);
                if let Some(schwere) = self.urteil(&erwartet, &gefunden) {
                    let d = Diagnose::neu("M-T001", schwere, format!(
                        "'{name}' ist als {} annotiert, der Wert ist aber {}",
                        erwartet.name(), gefunden.name()));
                    let d = an_annotation(d, zeile, annotation)
                        .erwartet(erwartet.name())
                        .gefunden(gefunden.name())
                        .vorschlag(format!("Wert anpassen oder die Annotation auf {} ändern", gefunden.name()));
                    self.melden(d, k);
                }
            }
            None => {
                // Nur gegen eine dominierende, eindeutige Annotation.
                if b.global.contains(name) {
                    return;
                }
                let Some(erwartet) = fluss.deklariert.get(name) else { return };
                if Self::einheitlich(b, name).as_ref() != Some(erwartet) {
                    return;
                }
                if let Some(schwere) = self.urteil(erwartet, &gefunden) {
                    let d = Diagnose::neu("M-T001", schwere, format!(
                        "'{name}' ist als {} deklariert, die Zuweisung liefert aber {}",
                        erwartet.name(), gefunden.name()))
                        .ort(zeile, 0)
                        .erwartet(erwartet.name())
                        .gefunden(gefunden.name())
                        .vorschlag(format!(
                            "Wert anpassen oder für den {}-Wert eine eigene Variable verwenden",
                            gefunden.name()));
                    self.melden(d, k);
                }
            }
        }
    }

    /// S2: die direkten Ausdruecke eines Statements auf Aufrufe pruefen
    /// (Bloecke laufen ueber block_pruefen).
    fn stmt_ausdruecke(&mut self, stmt: &Stmt, s: &Sicht, k: &Kontext) {
        let zeile = stmt.zeile();
        let mut ausdruecke: Vec<&Expr> = Vec::new();
        match stmt {
            Stmt::Assignment { value, .. }
            | Stmt::ConstAssignment { value, .. }
            | Stmt::CompoundAssignment { value, .. } => ausdruecke.push(value),
            Stmt::PropertyAssignment { object, value, .. } => ausdruecke.extend([object, value]),
            Stmt::IndexAssignment { object, index, value, .. } => ausdruecke.extend([object, index, value]),
            Stmt::Show(e, _) | Stmt::Throw(e, _) | Stmt::Expression(e, _) | Stmt::Defer(e, _) | Stmt::Expect(e, _) => {
                ausdruecke.push(e)
            }
            Stmt::Return(Some(e), _) => ausdruecke.push(e),
            Stmt::If { condition, .. }
            | Stmt::While { condition, .. }
            | Stmt::Guard { condition, .. }
            | Stmt::Precondition { condition, .. }
            | Stmt::Postcondition { condition, .. } => ausdruecke.push(condition),
            Stmt::For { iterable, .. } | Stmt::ParallelFor { iterable, .. } => ausdruecke.push(iterable),
            Stmt::Match { value, cases, .. } => {
                ausdruecke.push(value);
                ausdruecke.extend(cases.iter().filter_map(|(_, waechter, _)| waechter.as_ref()));
            }
            _ => {}
        }
        for e in ausdruecke {
            self.ausdruck_pruefen(e, s, zeile, k);
        }
    }

    /// S2: Aufrufe in einem Ausdruck gegen die Signaturen pruefen.
    fn ausdruck_pruefen(&mut self, e: &Expr, s: &Sicht, zeile: usize, k: &Kontext) {
        match e {
            Expr::FunctionCall { zeile: aufruf_zeile, name, args } => {
                for arg in args {
                    self.ausdruck_pruefen(arg, s, zeile, k);
                }
                if let Some(sig) = self.funktion_sig(name) {
                    let ort = if *aufruf_zeile > 0 { *aufruf_zeile } else { zeile };
                    self.aufruf_pruefen(&sig, name, args, s, ort, k);
                }
            }
            Expr::MethodCall { object, method, args } => {
                self.ausdruck_pruefen(object, s, zeile, k);
                for arg in args {
                    self.ausdruck_pruefen(arg, s, zeile, k);
                }
                // `selbst.m()` kann ein Override einer Unterklasse treffen: still.
                if let Ty::Objekt(klasse) = self.ableiten(object, s) {
                    if let Some(sig) = self.methode_sig(&klasse, method) {
                        self.aufruf_pruefen(&sig, &format!("{klasse}.{method}"), args, s, zeile, k);
                    }
                }
            }
            // Rechts einer Pipe bekommt der Aufruf ein implizites erstes Argument.
            Expr::Pipe { left, right } => {
                self.ausdruck_pruefen(left, s, zeile, k);
                match right.as_ref() {
                    Expr::FunctionCall { args, .. } => {
                        for arg in args {
                            self.ausdruck_pruefen(arg, s, zeile, k);
                        }
                    }
                    other => self.ausdruck_pruefen(other, s, zeile, k),
                }
            }
            Expr::Lambda { params, body, .. } => {
                self.verschattet_pruefen(params.iter().cloned().collect(), s, |p, innen| {
                    p.ausdruck_pruefen(body, innen, zeile, k);
                });
            }
            Expr::ListComprehension { expr, var_name, iterable, condition } => {
                self.ausdruck_pruefen(iterable, s, zeile, k);
                self.verschattet_pruefen(HashSet::from([var_name.clone()]), s, |p, innen| {
                    p.ausdruck_pruefen(expr, innen, zeile, k);
                    if let Some(c) = condition {
                        p.ausdruck_pruefen(c, innen, zeile, k);
                    }
                });
            }
            Expr::QueryExpr { var_name, source, where_cond, order_expr, select_expr } => {
                self.ausdruck_pruefen(source, s, zeile, k);
                self.verschattet_pruefen(HashSet::from([var_name.clone()]), s, |p, innen| {
                    for teil in [where_cond.as_deref(), order_expr.as_deref(), Some(select_expr.as_ref())].into_iter().flatten() {
                        p.ausdruck_pruefen(teil, innen, zeile, k);
                    }
                });
            }
            Expr::MatchExpr { value, cases } => {
                self.ausdruck_pruefen(value, s, zeile, k);
                let mut gebunden = HashSet::new();
                for (muster, _, _) in cases {
                    if let Some(muster) = muster {
                        namen_in(muster, &mut gebunden);
                    }
                }
                self.verschattet_pruefen(gebunden, s, |p, innen| {
                    for (_, waechter, ergebnis) in cases {
                        if let Some(w) = waechter {
                            p.ausdruck_pruefen(w, innen, zeile, k);
                        }
                        p.ausdruck_pruefen(ergebnis, innen, zeile, k);
                    }
                });
            }
            _ => {
                for kind in kinder(e) {
                    self.ausdruck_pruefen(kind, s, zeile, k);
                }
            }
        }
    }

    /// Prueft `f` mit verschatteten Namen: sie verlieren ihren Typ und
    /// verdecken gleichnamige Funktionen.
    fn verschattet_pruefen(&mut self, namen: HashSet<String>, s: &Sicht, f: impl FnOnce(&mut Self, &Sicht)) {
        let mut env = s.env.clone();
        for name in &namen {
            env.remove(name);
        }
        self.lokal.push(namen);
        f(self, &Sicht { env: &env, nur: s.nur });
        self.lokal.pop();
    }

    /// S2: Argumentzahl (Hinweis M-T004) und Argumenttypen (M-T002).
    fn aufruf_pruefen(&mut self, sig: &Signatur, name: &str, args: &[Expr], s: &Sicht, zeile: usize, k: &Kontext) {
        if args.iter().any(|a| matches!(a, Expr::Spread(_))) {
            return;
        }
        let n = args.len();
        let max = sig.params.len();
        if n > max {
            let d = Diagnose::hinweis("M-T004", format!(
                "'{name}' erwartet höchstens {max} Argumente, bekommt {n}; die überzähligen werden verworfen"))
                .ort(zeile, 0)
                .erwartet(format!("höchstens {max}"))
                .gefunden(n.to_string())
                .vorschlag("Überzählige Argumente entfernen oder die Funktion um Parameter erweitern");
            self.melden(d, k);
        } else if n < sig.pflicht {
            let d = Diagnose::hinweis("M-T004", format!(
                "'{name}' erwartet mindestens {} Argumente, bekommt {n}; die fehlenden werden zu nichts",
                sig.pflicht))
                .ort(zeile, 0)
                .erwartet(format!("mindestens {}", sig.pflicht))
                .gefunden(n.to_string())
                .vorschlag("Argumente ergänzen oder in der Signatur Standardwerte angeben (name = wert)");
            self.melden(d, k);
        }
        for (i, arg) in args.iter().enumerate().take(max) {
            let erwartet = &sig.typen[i];
            let gefunden = self.ableiten(arg, s);
            if let Some(schwere) = self.urteil(erwartet, &gefunden) {
                let param = &sig.params[i];
                let d = Diagnose::neu("M-T002", schwere, format!(
                    "Argument {} ('{param}') von '{name}' ist {}, erwartet {}",
                    i + 1, gefunden.name(), erwartet.name()))
                    .ort(zeile, 0)
                    .erwartet(erwartet.name())
                    .gefunden(gefunden.name())
                    .vorschlag(format!(
                        "Argument anpassen oder die Annotation von '{param}' auf {} ändern",
                        gefunden.name()));
                self.melden(d, k);
            }
        }
    }

    fn funktion_pruefen(&mut self, stmt: &Stmt, klasse: Option<&str>) {
        let Stmt::FunctionDef { name, params, param_types, return_type, defaults, body, decorators, .. } = stmt else {
            return;
        };
        if ist_nativ(decorators) {
            return;
        }
        let kontext = Kontext {
            funktion: Some(match klasse {
                Some(klasse) => format!("{klasse}.{name}"),
                None => name.clone(),
            }),
            rueckgabe: return_type.as_ref().map(|t| self.aus_annotation(t)),
            klasse: None,
        };
        let leer = HashMap::new();
        let mut parameter = Vec::new();
        for (i, param) in params.iter().enumerate() {
            let annotation = param_types.get(i).and_then(|t| t.as_ref());
            let erwartet = annotation.map(|t| self.aus_annotation(t));
            if let (Some(annotation), Some(erwartet), Some(Some(standard))) = (annotation, &erwartet, defaults.get(i)) {
                let gefunden = self.ableiten(standard, &Sicht { env: &leer, nur: None });
                if let Some(schwere) = self.urteil(erwartet, &gefunden) {
                    let d = Diagnose::neu("M-T001", schwere, format!(
                        "Standardwert von Parameter '{param}' ist {}, annotiert ist {}",
                        gefunden.name(), erwartet.name()));
                    let d = an_annotation(d, annotation.line, annotation)
                        .erwartet(erwartet.name())
                        .gefunden(gefunden.name())
                        .vorschlag(format!("Standardwert anpassen oder die Annotation auf {} ändern", gefunden.name()));
                    self.melden(d, &kontext);
                }
            }
            parameter.push((param.clone(), erwartet));
        }
        self.bereich_pruefen(body, parameter, HashSet::new(), &kontext);
    }

    fn melden(&mut self, d: Diagnose, k: &Kontext) {
        let d = d.datei(self.datei.to_string());
        let d = match &k.funktion {
            Some(funktion) => d.funktion(funktion.clone()),
            None => d,
        };
        self.befunde.push(d);
    }
}

/// Direkte Teilausdruecke (ohne eigene Bindungen; die behandelt
/// ausdruck_pruefen gesondert).
fn kinder(e: &Expr) -> Vec<&Expr> {
    match e {
        Expr::BinaryOp { left, right, .. } | Expr::NullishCoalesce { left, right } => vec![&**left, &**right],
        Expr::UnaryOp { operand, .. } => vec![&**operand],
        Expr::PropertyAccess { object, .. } | Expr::OptionalChain { object, .. } => vec![&**object],
        Expr::IndexAccess { object, index } => vec![&**object, &**index],
        Expr::Range { start, end } => vec![&**start, &**end],
        Expr::List(teile) | Expr::New { args: teile, .. } => teile.iter().collect(),
        Expr::Dict(paare) => paare.iter().flat_map(|(k, v)| [k, v]).collect(),
        Expr::Spread(inner) => vec![&**inner],
        Expr::Ternary { condition, then_val, else_val } => vec![&**condition, &**then_val, &**else_val],
        _ => Vec::new(),
    }
}

/// Funktionsnamen der Datei zaehlen (alle Tiefen ausser Methoden direkt im
/// Klassenkoerper; extern-Funktionen zaehlen mit).
fn funktionsnamen_zaehlen(stmts: &[Stmt], anzahl: &mut HashMap<String, usize>) {
    for stmt in stmts {
        match stmt {
            Stmt::FunctionDef { name, body, .. } => {
                *anzahl.entry(name.clone()).or_default() += 1;
                funktionsnamen_zaehlen(body, anzahl);
            }
            Stmt::ExternC { name, .. } => *anzahl.entry(name.clone()).or_default() += 1,
            Stmt::ClassDef { body, .. } => {
                for methode in body {
                    if let Stmt::FunctionDef { body, .. } = methode {
                        funktionsnamen_zaehlen(body, anzahl);
                    }
                }
            }
            Stmt::TestDef { body, .. }
            | Stmt::While { body, .. }
            | Stmt::UnsafeBlock { body, .. }
            | Stmt::For { body, .. }
            | Stmt::ParallelFor { body, .. } => funktionsnamen_zaehlen(body, anzahl),
            Stmt::If { body, else_body, .. } => {
                funktionsnamen_zaehlen(body, anzahl);
                funktionsnamen_zaehlen(else_body, anzahl);
            }
            Stmt::TryCatch { try_body, catch_body, .. } => {
                funktionsnamen_zaehlen(try_body, anzahl);
                funktionsnamen_zaehlen(catch_body, anzahl);
            }
            Stmt::Guard { else_body, .. } => funktionsnamen_zaehlen(else_body, anzahl),
            Stmt::Match { cases, .. } => {
                for (_, _, body) in cases {
                    funktionsnamen_zaehlen(body, anzahl);
                }
            }
            Stmt::Export(inner, _) => funktionsnamen_zaehlen(std::slice::from_ref(inner.as_ref()), anzahl),
            _ => {}
        }
    }
}

/// Ergebnistyp eines Operators; nur fuer sichere eingebaute Operanden.
fn binaer(op: &BinOp, l: Ty, r: Ty) -> Ty {
    use BinOp::*;
    match op {
        Eq | NotEq | Less | Greater | LessEq | GreaterEq => {
            if l.eingebaut() && r.eingebaut() { Ty::Bool } else { Ty::Unbekannt }
        }
        And | Or => {
            if l == Ty::Bool && r == Ty::Bool { Ty::Bool } else { Ty::Unbekannt }
        }
        Add => match (&l, &r) {
            (Ty::Zahl, Ty::Zahl) => Ty::Zahl,
            (Ty::Text, Ty::Text) => Ty::Text,
            (Ty::Liste, Ty::Liste) => Ty::Liste,
            _ => Ty::Unbekannt,
        },
        Sub | Mul | Div | Mod | Pow | BitAnd | BitOr | BitXor | LShift | RShift => {
            if l == Ty::Zahl && r == Ty::Zahl { Ty::Zahl } else { Ty::Unbekannt }
        }
    }
}

/// Ort einer Meldung an der Annotation (Zeile, Spalte, Ende).
fn an_annotation(d: Diagnose, zeile: usize, t: &TypeRef) -> Diagnose {
    let zeile = if t.line > 0 { t.line } else { zeile };
    let d = d.ort(zeile, t.column);
    if t.end_line == t.line && t.end_column > t.column { d.end_spalte(t.end_column) } else { d }
}

/// Namen, die ein match-Muster binden koennte.
fn namen_in(muster: &Expr, out: &mut HashSet<String>) {
    match muster {
        Expr::Identifier(name) => {
            out.insert(name.clone());
        }
        Expr::List(teile) | Expr::FunctionCall { args: teile, .. } | Expr::New { args: teile, .. } => {
            for teil in teile {
                namen_in(teil, out);
            }
        }
        _ => {}
    }
}

/// Alle `global`-Namen der Datei (auch in Funktionen und Methoden).
fn globale_sammeln(stmts: &[Stmt], out: &mut HashSet<String>) {
    for stmt in stmts {
        match stmt {
            Stmt::Global(namen, _) => out.extend(namen.iter().cloned()),
            Stmt::FunctionDef { body, .. }
            | Stmt::ClassDef { body, .. }
            | Stmt::TestDef { body, .. }
            | Stmt::While { body, .. }
            | Stmt::UnsafeBlock { body, .. }
            | Stmt::For { body, .. }
            | Stmt::ParallelFor { body, .. } => globale_sammeln(body, out),
            Stmt::If { body, else_body, .. } => {
                globale_sammeln(body, out);
                globale_sammeln(else_body, out);
            }
            Stmt::TryCatch { try_body, catch_body, .. } => {
                globale_sammeln(try_body, out);
                globale_sammeln(catch_body, out);
            }
            Stmt::Guard { else_body, .. } => globale_sammeln(else_body, out),
            Stmt::Match { cases, .. } => {
                for (_, _, body) in cases {
                    globale_sammeln(body, out);
                }
            }
            Stmt::Export(inner, _) => globale_sammeln(std::slice::from_ref(inner.as_ref()), out),
            _ => {}
        }
    }
}
