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
//!   Das gilt auch fuer die Namen, aus denen ein zugewiesener Wert
//!   abgeleitet wird: sie muessen an DIESER Zuweisung dominiert sein
//!   (keine Vorwaertsreferenz, Review 255aca1c).
//! - Neuzuweisung wird nur gegen eine dominierende, im Bereich eindeutige
//!   Annotation geprueft (nicht bei global mutierbaren Namen).
//! - `nichts` statt eines Werts ist ein Hinweis (optionaler Wert).
//! - Rueckgabetypen von Aufrufen gelten nicht als sicher (Unbekannt).
//!
//! Meldungen: M-T001 Zuweisung/Konstante/Neuzuweisung/Standardwert,
//! M-T002 Argumenttyp, M-T003 Rueckgabe, M-T004 Argumentzahl (Hinweis: der
//! Codegen fuellt fehlende mit nichts, verwirft ueberzaehlige), M-T005
//! Element-/Feldtyp (S3: Liste[T]/Dict[K, V] bei Literal, Neuzuweisung,
//! Index-Zuweisung und hinzufügen/append; Felder eigener Datenklassen bei
//! neu D(...) und d.feld = wert; gelesene Elemente bleiben unbekannt). Geprueft
//! werden nur eigene Funktionen/Methoden der Datei; Builtins, Importe,
//! @nativ/extern bleiben still. Der Pass aendert den AST nicht.
//!
//! S4: M-H006 (Hinweis) fuer sicher unsinnige Operatoren zwischen Zahl und
//! Text, so wie die Laufzeit sie ausfuehrt (Text zaehlt in Rechnungen als 0,
//! == zwischen verschiedenen Typen ist nie wahr). M-H001 (Methode passt nicht
//! zum Typ) kommt mit `--typpruefung` aus diesem Pass statt aus dem Codegen.
//! `--typ-streng` schaltet den Pass ein und macht SEINE Hinweise zu Fehlern;
//! Hinweise anderer Phasen (Lexer, Codegen) bleiben Hinweise.

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

/// S3: Elementtypen einer generischen Annotation (Liste[T], Dict[K, V]).
#[derive(Debug, Clone, PartialEq)]
enum Elem {
    Liste(Ty),
    Dict(Ty, Ty),
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
    /// S4: `--typ-streng` (Hinweise dieses Passes werden Fehler).
    static STRENG: Cell<bool> = const { Cell::new(false) };
}

/// Schaltet den Pass fuer das laufende Kommando ein (compile/pruefe mit
/// `--typpruefung`). `ast` und `layout` pruefen nie.
pub fn aktivieren() {
    AKTIV.with(|a| a.set(true));
}

/// S4: `--typ-streng`: Hinweise DIESES Passes werden Fehler (nicht die des
/// Lexers oder des Codegens).
pub fn streng_aktivieren() {
    STRENG.with(|s| s.set(true));
}

/// P2-T96: `--typ-streng` gilt auch fuer M-F001 (fehlerpfade.rs).
pub fn streng() -> bool {
    STRENG.with(|s| s.get())
}

/// Laeuft der Pass? Dann meldet der Codegen M-H001 nicht selbst.
pub fn aktiv() -> bool {
    AKTIV.with(|a| a.get())
}

/// Prueft eine Quelldatei, falls der Pass aktiv ist, und meldet die Befunde.
pub fn pruefe_datei(program: &Program, datei: &str) {
    if !AKTIV.with(|a| a.get()) {
        return;
    }
    let streng = STRENG.with(|s| s.get());
    for mut d in pruefe(program, datei) {
        if streng && d.schwere == Schwere::Hinweis {
            d.schwere = Schwere::Fehler;
            d.meldung.push_str(" (--typ-streng)");
        }
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
    let mut p = Pruefer::neu(program, datei);
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
    /// S3: eigene Datenklassen -> (Feld, Typ); None = mehrdeutig.
    datenklassen: HashMap<String, Option<Vec<(String, Ty)>>>,
    /// Lokale Namen der umschliessenden Bereiche (verschatten Funktionen).
    lokal: Vec<HashSet<String>>,
    befunde: Vec<Diagnose>,
}

/// Variablen eines Bereichs (Hauptprogramm, Funktion, Klassenkoerper, Test).
#[derive(Default)]
struct Bereich<'a> {
    /// Alle zugewiesenen Werte je Name (auch die annotierter Zuweisungen),
    /// je mit den gelesenen Namen, die an dieser Zuweisung schon dominiert
    /// zugewiesen sind (Review 255aca1c).
    werte: HashMap<String, Vec<(&'a Expr, HashSet<String>)>>,
    /// Alle Annotationen je Name (inkl. Parameter).
    annotiert: HashMap<String, Vec<Ty>>,
    /// S3: Elementannotation je Annotation (None = nicht generisch).
    element_annotiert: HashMap<String, Vec<Option<Elem>>>,
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
    /// S3: dominierende, eindeutige Elementannotation.
    elemente: HashMap<String, Elem>,
    /// S3 (Review fc83a96d): Namen, deren aktueller Wert sicher die
    /// deklarierte Liste bzw. das Dict ist (nur dann Index/hinzufügen pruefen).
    empfaenger: HashSet<String>,
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
    fn neu(program: &Program, datei: &'d str) -> Self {
        let mut p = Self {
            datei,
            klassen: HashMap::new(),
            fremd: HashSet::new(),
            funktionen: HashMap::new(),
            methoden: HashMap::new(),
            datenklassen: HashMap::new(),
            lokal: Vec::new(),
            befunde: Vec::new(),
        };
        p.typen_sammeln(&program.statements);
        p.signaturen_sammeln(&program.statements);
        p
    }

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
                Stmt::DataClassDef { name, fields, field_types, .. } => {
                    let felder: Vec<(String, Ty)> = fields.iter().enumerate()
                        .map(|(i, feld)| {
                            let typ = field_types.get(i).and_then(|t| t.as_ref())
                                .map(|t| self.aus_annotation(t))
                                .unwrap_or(Ty::Unbekannt);
                            (feld.clone(), typ)
                        })
                        .collect();
                    let eintrag = (!self.datenklassen.contains_key(name)).then_some(felder);
                    self.datenklassen.insert(name.clone(), eintrag);
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
        // Eine gleichnamige normale Klasse macht die Datenklasse mehrdeutig.
        for stmt in stmts {
            let stmt = match stmt {
                Stmt::Export(inner, _) => inner.as_ref(),
                other => other,
            };
            if let Stmt::ClassDef { name, .. } = stmt {
                if let Some(eintrag) = self.datenklassen.get_mut(name) {
                    *eintrag = None;
                }
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

    /// S3: Elementtypen einer Annotation Liste[T] bzw. Dict[K, V]; None bei
    /// anderen Annotationen, eigenen/fremden Namen oder lauter Unbekannt.
    fn aus_element_annotation(&self, ty: &TypeRef) -> Option<Elem> {
        let TypeKind::Generic { name, args } = &ty.kind else { return None };
        if self.klassen.contains_key(name) || self.fremd.contains(name) {
            return None;
        }
        let klein = name.to_lowercase();
        let elem = match (klein.as_str(), args.as_slice()) {
            ("liste" | "list", [t]) => Elem::Liste(self.aus_annotation(t)),
            ("dict" | "wörterbuch" | "woerterbuch", [k, v]) => Elem::Dict(self.aus_annotation(k), self.aus_annotation(v)),
            _ => return None,
        };
        match elem {
            Elem::Liste(Ty::Unbekannt) | Elem::Dict(Ty::Unbekannt, Ty::Unbekannt) => None,
            elem => Some(elem),
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
    fn sammeln<'a>(&self, stmts: &'a [Stmt], b: &mut Bereich<'a>, dom: &mut HashSet<String>) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { name, ty, value, .. } | Stmt::ConstAssignment { name, ty, value, .. } => {
                    if let Some(ty) = ty {
                        let typ = self.aus_annotation(ty);
                        b.annotiert.entry(name.clone()).or_default().push(typ);
                        b.element_annotiert.entry(name.clone()).or_default().push(self.aus_element_annotation(ty));
                    }
                    let mut gelesen = HashSet::new();
                    gelesene_namen(value, &mut gelesen);
                    gelesen.retain(|n| dom.contains(n));
                    b.werte.entry(name.clone()).or_default().push((value, gelesen));
                    dom.insert(name.clone());
                }
                Stmt::CompoundAssignment { name, .. } => {
                    b.unsicher.insert(name.clone());
                }
                Stmt::For { var_name, body, .. } | Stmt::ParallelFor { var_name, body, .. } => {
                    b.unsicher.insert(var_name.clone());
                    let mut innen = dom.clone();
                    innen.insert(var_name.clone());
                    self.sammeln(body, b, &mut innen);
                }
                Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                    if let Some(var) = catch_var {
                        b.unsicher.insert(var.clone());
                    }
                    self.sammeln(try_body, b, &mut dom.clone());
                    let mut innen = dom.clone();
                    if let Some(var) = catch_var {
                        innen.insert(var.clone());
                    }
                    self.sammeln(catch_body, b, &mut innen);
                }
                Stmt::Global(namen, _) => {
                    b.unsicher.extend(namen.iter().cloned());
                    b.global.extend(namen.iter().cloned());
                }
                Stmt::If { body, else_body, .. } => {
                    self.sammeln(body, b, &mut dom.clone());
                    self.sammeln(else_body, b, &mut dom.clone());
                }
                Stmt::While { body, .. } => self.sammeln(body, b, &mut dom.clone()),
                // `unsicher:` laeuft immer: sein Fluss gilt danach weiter.
                Stmt::UnsafeBlock { body, .. } => self.sammeln(body, b, dom),
                Stmt::Guard { else_body, .. } => self.sammeln(else_body, b, &mut dom.clone()),
                Stmt::Match { cases, .. } => {
                    for (muster, _, body) in cases {
                        if let Some(muster) = muster {
                            namen_in(muster, &mut b.unsicher);
                        }
                        self.sammeln(body, b, &mut dom.clone());
                    }
                }
                Stmt::Export(inner, _) => self.sammeln(std::slice::from_ref(inner.as_ref()), b, dom),
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
                    // Jeder Wert nur mit den Namen, die an seiner Zuweisung
                    // schon dominiert zugewiesen sind (Review 255aca1c).
                    b.werte[*name].iter()
                        .map(|(wert, dom)| self.ableiten(wert, &Sicht { env: &env, nur: Some(dom) }))
                        .reduce(vereinen)
                        .unwrap_or(Ty::Unbekannt)
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

    /// S3: eindeutige Elementannotation eines Namens (alle Annotationen
    /// generisch und gleich).
    fn element_einheitlich(b: &Bereich, name: &str) -> Option<Elem> {
        let annotationen = b.element_annotiert.get(name)?;
        let erster = annotationen.first()?.as_ref()?;
        annotationen.iter().all(|t| t.as_ref() == Some(erster)).then(|| erster.clone())
    }

    fn bereich_pruefen(&mut self, stmts: &[Stmt], parameter: Vec<(String, Option<Ty>, Option<Elem>)>, globale: HashSet<String>, k: &Kontext) {
        let mut b = Bereich::default();
        b.unsicher.extend(globale.iter().cloned());
        b.global = globale;
        let mut fluss = Fluss::default();
        for (name, typ, elem) in parameter {
            b.unsicher.insert(name.clone());
            fluss.zugewiesen.insert(name.clone());
            if let Some(typ) = typ {
                b.element_annotiert.entry(name.clone()).or_default().push(elem.clone());
                if let Some(elem) = elem {
                    fluss.elemente.insert(name.clone(), elem);
                    fluss.empfaenger.insert(name.clone());
                }
                b.annotiert.entry(name.clone()).or_default().push(typ.clone());
                fluss.deklariert.insert(name, typ);
            }
        }
        let mut dom = fluss.zugewiesen.clone();
        self.sammeln(stmts, &mut b, &mut dom);
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
                | Stmt::ConstAssignment { zeile, name, ty, value, .. } => {
                    let wert_ty = self.ableiten(value, &Sicht { env, nur: Some(&fluss.zugewiesen) });
                    self.zuweisung_pruefen(*zeile, name, ty.as_ref(), value, b, env, &fluss, k);
                    fluss.zugewiesen.insert(name.clone());
                    if ty.is_some() && !b.global.contains(name) {
                        if let Some(typ) = Self::einheitlich(b, name) {
                            fluss.deklariert.insert(name.clone(), typ);
                        }
                        if let Some(elem) = Self::element_einheitlich(b, name) {
                            fluss.elemente.insert(name.clone(), elem);
                        }
                    }
                    // Review fc83a96d: Empfaenger nur, solange der aktuelle Wert
                    // sicher die deklarierte Liste bzw. das Dict ist.
                    match (fluss.elemente.get(name), &wert_ty) {
                        (Some(Elem::Liste(_)), Ty::Liste) | (Some(Elem::Dict(..)), Ty::Dict) => {
                            fluss.empfaenger.insert(name.clone());
                        }
                        _ => {
                            fluss.empfaenger.remove(name);
                        }
                    }
                }
                Stmt::IndexAssignment { zeile, object, index, value } => {
                    self.index_zuweisung_pruefen(*zeile, object, index, value, b, env, &fluss, k);
                }
                Stmt::PropertyAssignment { zeile, object, property, value } => {
                    self.feld_zuweisung_pruefen(*zeile, object, property, value, env, &fluss, k);
                }
                Stmt::Expression(Expr::MethodCall { object, method, args }, zeile) => {
                    self.anhaengen_pruefen(*zeile, object, method, args, b, env, &fluss, k);
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
                // Review 0fc5ac15: ab der zweiten Runde gilt, was der Rumpf
                // selbst neu bindet (Schleifenrueckfluss).
                Stmt::While { body, .. } => {
                    let innen = Self::ohne_empfaenger(&fluss, body, &[]);
                    self.block_pruefen(body, b, env, k, &innen);
                }
                // `unsicher:` laeuft immer: sein Fluss gilt danach weiter.
                Stmt::UnsafeBlock { body, .. } => {
                    fluss = self.block_pruefen(body, b, env, k, &fluss);
                }
                Stmt::For { var_name, body, .. } | Stmt::ParallelFor { var_name, body, .. } => {
                    // Der Binder bindet bei jedem Eintritt neu, dazu der Rueckfluss.
                    let mut innen = Self::ohne_empfaenger(&fluss, body, &[var_name]);
                    innen.zugewiesen.insert(var_name.clone());
                    self.block_pruefen(body, b, env, k, &innen);
                }
                Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                    self.block_pruefen(try_body, b, env, k, &fluss);
                    // Der Fang bindet seine Variable und kann nach jedem Teil
                    // von try_body beginnen.
                    let binder: Vec<&String> = catch_var.iter().collect();
                    let mut innen = Self::ohne_empfaenger(&fluss, try_body, &binder);
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
            // Review fc83a96d/0fc5ac15: was jedes andere Statement neu bindet
            // (Zweig, Schleife, try, Guard, match, +=, Definition, Import), ist
            // danach als Empfaenger unsicher. Zuweisung, `unsicher:` und export
            // fuehren ihren Fluss selbst. Muster binden nichts (`fall xs:`
            // vergleicht); zuweisungen_in nimmt ihre Namen trotzdem mit.
            if !matches!(stmt, Stmt::Assignment { .. } | Stmt::ConstAssignment { .. }
                | Stmt::UnsafeBlock { .. } | Stmt::Export(..)) {
                let mut namen = HashSet::new();
                zuweisungen_in(std::slice::from_ref(stmt), &mut namen);
                fluss.empfaenger.retain(|n| !namen.contains(n));
            }
        }
        fluss
    }

    /// Review 0fc5ac15: Fluss beim Eintritt in einen Rumpf ohne die
    /// Empfaenger-Fakten der Namen, die der Binder (Schleifenvariable,
    /// Fangvariable) oder `stmts` (Schleifenrueckfluss, abgebrochenes try)
    /// neu binden koennen.
    fn ohne_empfaenger(fluss: &Fluss, stmts: &[Stmt], binder: &[&String]) -> Fluss {
        let mut namen: HashSet<String> = binder.iter().map(|n| (*n).clone()).collect();
        zuweisungen_in(stmts, &mut namen);
        let mut innen = fluss.clone();
        innen.empfaenger.retain(|n| !namen.contains(n));
        innen
    }

    #[allow(clippy::too_many_arguments)]
    fn zuweisung_pruefen(&mut self, zeile: usize, name: &str, ty: Option<&TypeRef>, wert: &Expr,
                         b: &Bereich, env: &HashMap<String, Ty>, fluss: &Fluss, k: &Kontext) {
        let s = Sicht { env, nur: Some(&fluss.zugewiesen) };
        let gefunden = self.ableiten(wert, &s);
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
                if let Some(elem) = self.aus_element_annotation(annotation) {
                    self.literal_elemente_pruefen(&elem, wert, name, &s, zeile, k);
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
                if let Some(elem) = fluss.elemente.get(name).cloned() {
                    if Self::element_einheitlich(b, name).as_ref() == Some(&elem) {
                        self.literal_elemente_pruefen(&elem, wert, name, &s, zeile, k);
                    }
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
            Expr::FunctionCall { zeile: aufruf_zeile, name, args, .. } => {
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
                // S4: M-H001 kommt mit --typpruefung von hier (dominanzsicher),
                // nicht aus dem Codegen.
                if let Expr::Identifier(name) = object.as_ref() {
                    let typ = self.ableiten(object, s);
                    let t = typ.name();
                    if typ.eingebaut() && methode_unpassend(t, method) {
                        let d = Diagnose::hinweis("M-H001", format!(
                            "'{name}' ist vom Typ {t}, aber '.{method}()' ist nicht fuer {t} definiert"))
                            .ort(zeile, 0)
                            .gefunden(t);
                        self.melden(d, k);
                    }
                }
                // `selbst.m()` kann ein Override einer Unterklasse treffen: still.
                if let Ty::Objekt(klasse) = self.ableiten(object, s) {
                    if let Some(sig) = self.methode_sig(&klasse, method) {
                        self.aufruf_pruefen(&sig, &format!("{klasse}.{method}"), args, s, zeile, k);
                    }
                }
            }
            // S3: neu D(...) gegen die Feldtypen einer eigenen Datenklasse.
            Expr::New { class_name, args } => {
                for arg in args {
                    self.ausdruck_pruefen(arg, s, zeile, k);
                }
                if args.iter().any(|a| matches!(a, Expr::Spread(_))) {
                    return;
                }
                let Some(Some(felder)) = self.datenklassen.get(class_name).cloned() else { return };
                for (i, (arg, (feld, erwartet))) in args.iter().zip(felder.iter()).enumerate() {
                    let gefunden = self.ableiten(arg, s);
                    self.element_melden(erwartet, &gefunden, format!("Feld {} ('{feld}') von '{class_name}'", i + 1), zeile, k);
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
                if let Expr::BinaryOp { left, op, right } = e {
                    self.operator_pruefen(left, op, right, s, zeile, k);
                }
                for kind in kinder(e) {
                    self.ausdruck_pruefen(kind, s, zeile, k);
                }
            }
        }
    }

    /// S4 (M-H006): sicher unsinnige Operatoren zwischen Zahl und Text, so wie
    /// die Laufzeit sie ausfuehrt (moo_ops.c): Text zaehlt in Rechnungen als 0
    /// (moo_as_number), == zwischen verschiedenen Typen ist nie wahr. Still
    /// bleiben Text * Zahl (Wiederholung), + (Verkettung), Text gegen Text
    /// verglichen, Bool, Listen und alles Unsichere.
    fn operator_pruefen(&mut self, left: &Expr, op: &BinOp, right: &Expr, s: &Sicht, zeile: usize, k: &Kontext) {
        use BinOp::*;
        let (l, r) = (self.ableiten(left, s), self.ableiten(right, s));
        let zahl_oder_text = |t: &Ty| matches!(t, Ty::Zahl | Ty::Text);
        if !zahl_oder_text(&l) || !zahl_oder_text(&r) {
            return;
        }
        let (lt, rt) = (l == Ty::Text, r == Ty::Text);
        let was = format!("{} {} {}", l.name(), symbol(op), r.name());
        let umwandeln = "Den Text vorher mit zahl(...) in eine Zahl umwandeln";
        let angleichen = "Beide Seiten in denselben Typ bringen (zahl(...) oder text(...))";
        let (meldung, vorschlag) = match op {
            Div if rt => (format!(
                "Division {was}: der Text als Divisor zählt als 0, die Division wirft „Division durch Null!“ und liefert nichts"),
                umwandeln),
            Mod if rt => (format!(
                "Rest {was}: der Text als Divisor zählt als 0, das Ergebnis ist keine Zahl (NaN)"),
                umwandeln),
            Sub | Div | Mod | Pow if lt || rt => (format!(
                "Rechnung {was}: Text zählt hier als 0, nicht als die Zahl darin; das Ergebnis ist unsinnig"),
                umwandeln),
            Mul if lt && rt => (format!(
                "Rechnung {was}: beide Texte zählen hier als 0, das Ergebnis ist immer 0"),
                umwandeln),
            Eq if lt != rt => (format!(
                "Vergleich {was} ist immer falsch: Werte verschiedener Typen sind nie gleich"),
                angleichen),
            NotEq if lt != rt => (format!(
                "Vergleich {was} ist immer wahr: Werte verschiedener Typen sind nie gleich"),
                angleichen),
            Less | Greater | LessEq | GreaterEq if lt != rt => (format!(
                "Größenvergleich {was}: der Text zählt als 0, verglichen wird nur die Zahl mit 0"),
                umwandeln),
            _ => return,
        };
        let d = Diagnose::hinweis("M-H006", meldung)
            .ort(zeile, 0)
            .gefunden(was)
            .vorschlag(vorschlag);
        self.melden(d, k);
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

    /// S3 (M-T005): sichere Elemente eines Listen-/Dict-Literals gegen die
    /// deklarierten Elementtypen. Spread und unsichere Elemente bleiben still.
    fn literal_elemente_pruefen(&mut self, elem: &Elem, wert: &Expr, name: &str, s: &Sicht, zeile: usize, k: &Kontext) {
        match (elem, wert) {
            (Elem::Liste(t), Expr::List(teile)) => {
                for (i, teil) in teile.iter().enumerate() {
                    if matches!(teil, Expr::Spread(_)) {
                        continue;
                    }
                    let gefunden = self.ableiten(teil, s);
                    self.element_melden(t, &gefunden, format!("Element {} von '{name}'", i + 1), zeile, k);
                }
            }
            (Elem::Dict(kt, vt), Expr::Dict(paare)) => {
                for (schluessel, wert) in paare {
                    let gk = self.ableiten(schluessel, s);
                    self.element_melden(kt, &gk, format!("Schlüssel in '{name}'"), zeile, k);
                    let gv = self.ableiten(wert, s);
                    self.element_melden(vt, &gv, format!("Wert in '{name}'"), zeile, k);
                }
            }
            _ => {}
        }
    }

    fn element_melden(&mut self, erwartet: &Ty, gefunden: &Ty, was: String, zeile: usize, k: &Kontext) {
        if let Some(schwere) = self.urteil(erwartet, gefunden) {
            let d = Diagnose::neu("M-T005", schwere, format!(
                "{was} ist {}, erwartet {}", gefunden.name(), erwartet.name()))
                .ort(zeile, 0)
                .erwartet(erwartet.name())
                .gefunden(gefunden.name())
                .vorschlag(format!("Wert anpassen oder den deklarierten Typ auf {} ändern", gefunden.name()));
            self.melden(d, k);
        }
    }

    /// S3: deklarierte Elementtypen eines lokalen Namens, nur bei
    /// dominierender, im Bereich eindeutiger Annotation (nicht global).
    fn elemente_von(object: &Expr, b: &Bereich, fluss: &Fluss) -> Option<(String, Elem)> {
        let Expr::Identifier(name) = object else { return None };
        if b.global.contains(name) {
            return None;
        }
        if !fluss.empfaenger.contains(name) {
            return None;
        }
        let elem = fluss.elemente.get(name)?;
        (Self::element_einheitlich(b, name).as_ref() == Some(elem)).then(|| (name.clone(), elem.clone()))
    }

    /// S3: `xs[i] = wert` (Liste: Element; Dict: Schluessel und Wert).
    #[allow(clippy::too_many_arguments)]
    fn index_zuweisung_pruefen(&mut self, zeile: usize, object: &Expr, index: &Expr, wert: &Expr,
                               b: &Bereich, env: &HashMap<String, Ty>, fluss: &Fluss, k: &Kontext) {
        let Some((name, elem)) = Self::elemente_von(object, b, fluss) else { return };
        let s = Sicht { env, nur: Some(&fluss.zugewiesen) };
        match &elem {
            Elem::Liste(t) => {
                let gefunden = self.ableiten(wert, &s);
                self.element_melden(t, &gefunden, format!("Element von '{name}'"), zeile, k);
            }
            Elem::Dict(kt, vt) => {
                let gk = self.ableiten(index, &s);
                self.element_melden(kt, &gk, format!("Schlüssel von '{name}'"), zeile, k);
                let gv = self.ableiten(wert, &s);
                self.element_melden(vt, &gv, format!("Wert von '{name}'"), zeile, k);
            }
        }
    }

    /// S3: `xs.hinzufügen(wert)` / `xs.append(wert)` als Statement.
    #[allow(clippy::too_many_arguments)]
    fn anhaengen_pruefen(&mut self, zeile: usize, object: &Expr, method: &str, args: &[Expr],
                         b: &Bereich, env: &HashMap<String, Ty>, fluss: &Fluss, k: &Kontext) {
        if !matches!(method, "append" | "hinzufügen") || args.len() != 1 || matches!(args[0], Expr::Spread(_)) {
            return;
        }
        let Some((name, Elem::Liste(t))) = Self::elemente_von(object, b, fluss) else { return };
        let gefunden = self.ableiten(&args[0], &Sicht { env, nur: Some(&fluss.zugewiesen) });
        self.element_melden(&t, &gefunden, format!("Neues Element von '{name}'"), zeile, k);
    }

    /// S3: Feldzuweisung an eine sichere Instanz einer eigenen Datenklasse
    /// ohne Unterklasse. `selbst.x` und normale Klassen bleiben still.
    #[allow(clippy::too_many_arguments)]
    fn feld_zuweisung_pruefen(&mut self, zeile: usize, object: &Expr, feld: &str, wert: &Expr,
                              env: &HashMap<String, Ty>, fluss: &Fluss, k: &Kontext) {
        let s = Sicht { env, nur: Some(&fluss.zugewiesen) };
        let Ty::Objekt(klasse) = self.ableiten(object, &s) else { return };
        let Some(erwartet) = self.feldtyp(&klasse, feld) else { return };
        let gefunden = self.ableiten(wert, &s);
        self.element_melden(&erwartet, &gefunden, format!("Feld '{feld}' von '{klasse}'"), zeile, k);
    }

    /// Deklarierter Feldtyp einer eindeutigen Datenklasse ohne Unterklasse.
    fn feldtyp(&self, klasse: &str, feld: &str) -> Option<Ty> {
        if self.klassen.values().any(|eltern| eltern.as_deref() == Some(klasse)) {
            return None;
        }
        let felder = self.datenklassen.get(klasse)?.as_ref()?;
        felder.iter().find(|(name, _)| name == feld).map(|(_, t)| t.clone())
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
            let elem = annotation.and_then(|t| self.aus_element_annotation(t));
            parameter.push((param.clone(), erwartet, elem));
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

/// Sicher ausgewertete Lesezugriffe für native Must-use-Werte. Anders als
/// die Annotationdominanz oben werden Aufrufargumente berücksichtigt; bei
/// Kurzschluss/Ternaer zählen nur auf jedem Ausführungspfad gelesene Namen.
pub(crate) fn native_gelesene_namen(expr: &Expr, out: &mut HashSet<String>) {
    match expr {
        Expr::Identifier(n) => { out.insert(n.clone()); }
        Expr::FunctionCall { args, .. } => for e in args { native_gelesene_namen(e, out); },
        Expr::MethodCall { object, args, .. } => {
            native_gelesene_namen(object, out);
            for e in args { native_gelesene_namen(e, out); }
        }
        Expr::BinaryOp { left, op: BinOp::And | BinOp::Or, .. } =>
            native_gelesene_namen(left, out),
        Expr::Ternary { condition, then_val, else_val } => {
            native_gelesene_namen(condition, out);
            let mut yes = HashSet::new(); let mut no = HashSet::new();
            native_gelesene_namen(then_val, &mut yes);
            native_gelesene_namen(else_val, &mut no);
            out.extend(yes.intersection(&no).cloned());
        }
        _ => for e in kinder(expr) { native_gelesene_namen(e, out); },
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

/// M-H001: Methode, die es fuer den eingebauten Typ nicht gibt. Eine Tabelle
/// fuer beide Wege (Codegen ohne, dieser Pass mit --typpruefung).
pub fn methode_unpassend(typ: &str, method: &str) -> bool {
    const LISTE: &[&str] = &["append", "hinzufügen", "pop", "sort", "sortieren", "reverse", "umkehren", "join", "leeren", "entfernen", "einfügen"];
    const TEXT: &[&str] = &["upper", "gross", "lower", "klein", "trim", "trimmen", "split", "teilen", "replace", "ersetzen"];
    const DICT: &[&str] = &["keys", "schlüssel", "has", "hat"];
    let (liste, text, dict) = (LISTE.contains(&method), TEXT.contains(&method), DICT.contains(&method));
    match typ {
        "Zahl" | "Bool" => liste || text || dict,
        "Text" => liste || dict,
        "Liste" => text || dict,
        "Dict" => liste || text,
        _ => false,
    }
}

/// Quelltextzeichen eines Operators (fuer Meldungen).
fn symbol(op: &BinOp) -> &'static str {
    use BinOp::*;
    match op {
        Add => "+", Sub => "-", Mul => "*", Div => "/", Mod => "%", Pow => "**",
        Eq => "==", NotEq => "!=", Less => "<", Greater => ">", LessEq => "<=", GreaterEq => ">=",
        And => "und", Or => "oder",
        BitAnd => "&", BitOr => "|", BitXor => "^", LShift => "<<", RShift => ">>",
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

/// Namen, die Statements (auch in Unterbloecken, nicht in eigenen
/// Bereichen) neu binden (Review fc83a96d).
fn zuweisungen_in(stmts: &[Stmt], out: &mut HashSet<String>) {
    for stmt in stmts {
        match stmt {
            Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. } | Stmt::CompoundAssignment { name, .. } => {
                out.insert(name.clone());
            }
            Stmt::For { var_name, body, .. } | Stmt::ParallelFor { var_name, body, .. } => {
                out.insert(var_name.clone());
                zuweisungen_in(body, out);
            }
            Stmt::TryCatch { try_body, catch_var, catch_body, .. } => {
                if let Some(var) = catch_var {
                    out.insert(var.clone());
                }
                zuweisungen_in(try_body, out);
                zuweisungen_in(catch_body, out);
            }
            Stmt::If { body, else_body, .. } => {
                zuweisungen_in(body, out);
                zuweisungen_in(else_body, out);
            }
            Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } => zuweisungen_in(body, out),
            Stmt::Guard { else_body, .. } => zuweisungen_in(else_body, out),
            Stmt::Match { cases, .. } => {
                for (muster, _, body) in cases {
                    if let Some(muster) = muster {
                        namen_in(muster, out);
                    }
                    zuweisungen_in(body, out);
                }
            }
            // Review 0fc5ac15: Definitionen, Importe und global binden den
            // Namen ebenfalls neu.
            Stmt::FunctionDef { name, .. } | Stmt::ExternC { name, .. } | Stmt::ClassDef { name, .. }
            | Stmt::DataClassDef { name, .. } | Stmt::StructDef { name, .. } | Stmt::InterfaceDef { name, .. }
            | Stmt::RawDataU32 { name, .. } | Stmt::NativeConst { name, .. } => {
                out.insert(name.clone());
            }
            Stmt::Import { names, alias, .. } => {
                out.extend(names.iter().cloned());
                out.extend(alias.iter().cloned());
            }
            Stmt::Global(namen, _) => out.extend(namen.iter().cloned()),
            Stmt::Export(inner, _) => zuweisungen_in(std::slice::from_ref(inner.as_ref()), out),
            _ => {}
        }
    }
}

/// Namen, die `ableiten` in einem Ausdruck liest (Bezeichner ueber
/// Operatoren und Ternaer): Grundlage der Dominanzpruefung je Zuweisung.
fn gelesene_namen(expr: &Expr, out: &mut HashSet<String>) {
    match expr {
        Expr::Identifier(name) => {
            out.insert(name.clone());
        }
        Expr::BinaryOp { left, right, .. } => {
            gelesene_namen(left, out);
            gelesene_namen(right, out);
        }
        Expr::UnaryOp { operand, .. } => gelesene_namen(operand, out),
        Expr::Ternary { then_val, else_val, .. } => {
            gelesene_namen(then_val, out);
            gelesene_namen(else_val, out);
        }
        _ => {}
    }
}


// ===================================================================
// P2-T114 Phase C: Globale gehoeren dem Hauptthread (ADR 2290622c).
// ===================================================================
//
// Laeuft fuer jedes compile/pruefe (nicht nur mit --typpruefung) auf dem
// aufgeloesten Programm (Module eingefuegt). Wurzeln sind die Funktionen und
// Lambdas, die an starte/spawn gehen. Was von dort ueber direkte Aufrufe,
// Konstruktoren und im Koerper erzeugte Lambdas sicher erreicht wird und eine
// Globale liest oder schreibt, die nicht `geteilt` markiert ist, ist ein
// Fehler (M-P001). Ausnahme ohne Markierung: `konstante N auf 5` (Zahl,
// Wahrheitswert, nichts), die nirgends neu gesetzt wird. Vielleicht erreichbar (Funktionswerte, Methoden nach Name,
// unbekannter starte-Wert): mit --typ-streng Fehler (M-P002), sonst
// Laufzeitpruefung moo_global_pruefen im Codegen (GlobalePruefplan).
// Die Namensaufloesung folgt dem Codegen: Parameter und lokal zuerst
// geschriebene Namen sind lokal (collect_all_var_names,
// first_use_is_local_write, `global x`); Lambdas im Hauptprogramm fangen alle
// Variablen des Hauptrahmens ein (Kopie), lesen also keine Globale.

/// Ergebnis der Effektpruefung fuer den Codegen.
#[derive(Debug, Clone, Default)]
pub struct GlobalePruefplan {
    /// LLVM-Namen der Funktionen (Methoden als `Klasse__methode`), in denen
    /// jeder Globalzugriff zur Laufzeit geprueft wird.
    pub funktionen: HashSet<String>,
    /// Lambdas (`__lambda_N`) mit Globalzugriff vielleicht auf einem Thread.
    pub lambdas: bool,
    /// `geteilt` markierte Globale (eingefroren, Lesen erlaubt).
    pub geteilt: HashSet<String>,
    /// P2-T114 B: unveraenderliche Text-Konstanten, die Threads ohne `geteilt`
    /// lesen; der Codegen teilt sie bei der Initialisierung (atomarer Zaehler).
    pub text_teilen: HashSet<String>,
}

/// P2-T114 B: Builtins, deren Funktionsargument die Runtime auf einem fremden
/// Thread aufruft (Name, Argumentindex). Gesichtet 16.11.2026: Webserver
/// (web_server/web_annehmen: Annahmeschleife im Programm, kein Handler-Rueckruf),
/// UI (GTK/X11/Wayland/Win32/Tray: Rueckrufe aus der Ereignisschleife des
/// Hauptthreads), bei/ausloesen (synchron), parallel fuer (sequentiell):
/// keiner ruft auf einem fremden Thread zurueck. Neue Quellen hier eintragen.
/// Unabhaengig davon gelten Funktionswerte, die an extern-C-Funktionen gehen
/// (C-Rueckrufe), als vielleicht fremd (Laufzeitpruefung).
const FREMDTHREAD_RUECKRUFE: &[(&str, usize)] = &[];

#[derive(Debug, Clone)]
enum KoerperArt {
    Haupt,
    Funktion(String),
    Methode(String, String),
    Lambda,
    Test(String),
}

struct Koerper {
    art: KoerperArt,
    zeile: usize,
    /// (Globale, Zeile, schreibt)
    zugriffe: Vec<(String, usize, bool)>,
    sicher: Vec<usize>,
    vielleicht: Vec<usize>,
    dynamisch: bool,
    /// Native Koerper koennen C-Funktionszeiger bilden.
    nativ: bool,
}

impl Koerper {
    fn neu(art: KoerperArt, zeile: usize) -> Self {
        Koerper { art, zeile, zugriffe: Vec::new(), sicher: Vec::new(), vielleicht: Vec::new(), dynamisch: false, nativ: false }
    }

    fn anzeige(&self) -> String {
        match &self.art {
            KoerperArt::Haupt => "Hauptprogramm".to_string(),
            KoerperArt::Funktion(n) => format!("Funktion '{n}'"),
            KoerperArt::Methode(k, m) => format!("Methode '{k}.{m}'"),
            KoerperArt::Lambda => format!("Lambda in Zeile {}", self.zeile),
            KoerperArt::Test(n) => format!("Test '{n}'"),
        }
    }

    fn kurz(&self) -> String {
        match &self.art {
            KoerperArt::Funktion(n) | KoerperArt::Test(n) => n.clone(),
            KoerperArt::Methode(k, m) => format!("{k}.{m}"),
            KoerperArt::Lambda => format!("Lambda (Zeile {})", self.zeile),
            KoerperArt::Haupt => "Hauptprogramm".to_string(),
        }
    }
}

/// Lokale Namen eines Koerpers; `alle` im Hauptrahmen (dort gehoert ohnehin
/// alles dem Hauptthread, Lambdas fangen alles ein).
#[derive(Clone)]
struct Lokal {
    namen: HashSet<String>,
    alle: bool,
    /// Aus allen Zuweisungen bewiesene Typen, keine blossen Annotationen.
    typen: HashMap<String, Ty>,
}

struct EffektScan<'d> {
    typen: Pruefer<'d>,
    /// Top-Level-Variablen (LLVM-Globale) -> geteilt markiert.
    globale: HashMap<String, bool>,
    funktionen: HashMap<String, usize>,
    methoden: HashMap<String, Vec<usize>>,
    methoden_typ: HashMap<(String, String), usize>,
    konstruktoren: HashMap<String, usize>,
    koerper: Vec<Koerper>,
    /// starte-Wurzeln: Koerper (None = Funktionswert unbekannt), Zeile.
    wurzeln: Vec<(Option<usize>, usize)>,
    adresse: HashSet<usize>,
    lambdas: Vec<usize>,
    haupt_schreibt: Vec<(String, usize)>,
    geteilt_falsch: Vec<(String, usize)>,
    /// P2-T114 B: extern-C-Funktionen (Ziele von C-Rueckrufen).
    extern_c: HashSet<String>,
    /// P2-T114 B: Koerper, die vielleicht auf einem fremden (Nicht-Moo-)Thread
    /// laufen (C-Rueckrufe, FREMDTHREAD_RUECKRUFE).
    fremd_wurzeln: Vec<usize>,
}

impl EffektScan<'_> {
    fn ist_global(&self, n: &str, l: &Lokal) -> bool {
        !l.alle && !l.namen.contains(n) && self.globale.contains_key(n)
    }

    /// Name einer eigenen Top-Level-Funktion, nicht verschattet.
    fn funktion(&self, n: &str, l: &Lokal) -> Option<usize> {
        if l.namen.contains(n) || self.globale.contains_key(n) {
            return None;
        }
        self.funktionen.get(n).copied()
    }

    fn zugriff(&mut self, id: usize, n: &str, z: usize, l: &Lokal, schreibt: bool) {
        if matches!(self.koerper[id].art, KoerperArt::Haupt) {
            if schreibt && self.globale.contains_key(n) {
                self.haupt_schreibt.push((n.to_string(), z));
            }
            return;
        }
        if self.ist_global(n, l) {
            self.koerper[id].zugriffe.push((n.to_string(), z, schreibt));
        }
    }

    /// Wie der Codegen (compile_function_def): Parameter, und Namen, deren
    /// erste Verwendung ein lokales Schreiben ist, ohne `global`.
    fn lokale(&self, params: &[String], body: &[Stmt]) -> Lokal {
        let decl: HashSet<String> = body.iter()
            .filter_map(|s| if let Stmt::Global(n, _) = s { Some(n.clone()) } else { None })
            .flatten()
            .collect();
        let mut namen: HashSet<String> = params.iter().cloned().collect();
        for n in crate::codegen::CodeGen::collect_all_var_names(body) {
            if namen.contains(&n) || decl.contains(&n) {
                continue;
            }
            if self.globale.contains_key(&n)
                && crate::codegen::CodeGen::first_use_is_local_write(body, &n) != Some(true) {
                continue;
            }
            namen.insert(n);
        }
        let mut bereich = Bereich::default();
        bereich.unsicher.extend(params.iter().cloned());
        self.typen.sammeln(body, &mut bereich, &mut params.iter().cloned().collect());
        let mut typen = self.typen.umgebung(&bereich);
        // Globale koennen durch andere Aufrufe neu gesetzt werden. Nur
        // lokale, aus echten Zuweisungen bewiesene Empfaenger verwenden.
        typen.retain(|n, _| namen.contains(n));
        Lokal { namen, alle: false, typen }
    }

    fn methode_fuer_typ(&self, objekt: &Expr, l: &Lokal, methode: &str) -> Option<usize> {
        let Ty::Objekt(mut klasse) = self.typen.ableiten(objekt, &Sicht { env: &l.typen, nur: None }) else {
            return None;
        };
        let mut besucht = HashSet::new();
        while besucht.insert(klasse.clone()) {
            if let Some(&ziel) = self.methoden_typ.get(&(klasse.clone(), methode.to_string())) {
                return Some(ziel);
            }
            klasse = self.typen.klassen.get(&klasse)?.clone()?;
        }
        None
    }

    fn stmts(&mut self, id: usize, stmts: &[Stmt], l: &Lokal, oberste: bool) {
        for s in stmts {
            self.stmt(id, s, l, oberste);
        }
    }

    fn stmt(&mut self, id: usize, s: &Stmt, l: &Lokal, oberste: bool) {
        let z = s.zeile();
        match s {
            Stmt::Assignment { name, value, .. } => {
                self.expr(id, value, z, l);
                self.zugriff(id, name, z, l, true);
            }
            Stmt::CompoundAssignment { name, value, .. } => {
                self.expr(id, value, z, l);
                self.zugriff(id, name, z, l, false);
                self.zugriff(id, name, z, l, true);
            }
            Stmt::ConstAssignment { name, value, geteilt, .. } => {
                let haupt = matches!(self.koerper[id].art, KoerperArt::Haupt);
                if *geteilt && !(oberste && haupt) {
                    self.geteilt_falsch.push((name.clone(), z));
                }
                self.expr(id, value, z, l);
                if !(*geteilt && oberste && haupt) {
                    self.zugriff(id, name, z, l, true);
                }
            }
            Stmt::PropertyAssignment { object, value, .. } => {
                self.expr(id, object, z, l);
                self.expr(id, value, z, l);
            }
            Stmt::IndexAssignment { object, index, value, .. } => {
                self.expr(id, object, z, l);
                self.expr(id, index, z, l);
                self.expr(id, value, z, l);
            }
            Stmt::Show(e, _) | Stmt::Expression(e, _) | Stmt::Throw(e, _) | Stmt::Defer(e, _)
            | Stmt::Expect(e, _) | Stmt::Return(Some(e), _) => self.expr(id, e, z, l),
            Stmt::If { condition, body, else_body, .. } => {
                self.expr(id, condition, z, l);
                self.stmts(id, body, l, false);
                self.stmts(id, else_body, l, false);
            }
            Stmt::While { condition, body, .. } => {
                self.expr(id, condition, z, l);
                self.stmts(id, body, l, false);
            }
            Stmt::For { iterable, body, .. } | Stmt::ParallelFor { iterable, body, .. } => {
                self.expr(id, iterable, z, l);
                self.stmts(id, body, l, false);
            }
            Stmt::Precondition { condition, .. } | Stmt::Postcondition { condition, .. } => {
                self.expr(id, condition, z, l);
            }
            Stmt::TryCatch { try_body, catch_body, .. } => {
                self.stmts(id, try_body, l, false);
                self.stmts(id, catch_body, l, false);
            }
            Stmt::Match { value, cases, .. } => {
                self.expr(id, value, z, l);
                for (muster, waechter, body) in cases {
                    let mut l2 = l.clone();
                    if let Some(m) = muster {
                        let mut gebunden = HashSet::new();
                        namen_in(m, &mut gebunden);
                        for n in gebunden { l2.typen.remove(&n); l2.namen.insert(n); }
                    }
                    if let Some(w) = waechter {
                        self.expr(id, w, z, &l2);
                    }
                    self.stmts(id, body, &l2, false);
                }
            }
            Stmt::Guard { condition, else_body, .. } => {
                self.expr(id, condition, z, l);
                self.stmts(id, else_body, l, false);
            }
            Stmt::UnsafeBlock { body, .. } => self.stmts(id, body, l, false),
            Stmt::Export(inner, _) => self.stmt(id, inner, l, oberste),
            _ => {}
        }
    }

    fn lambda(&mut self, id: usize, params: &[String], body: &Expr, z: usize, l: &Lokal) -> usize {
        let lid = self.koerper.len();
        self.koerper.push(Koerper::neu(KoerperArt::Lambda, z));
        self.lambdas.push(lid);
        self.koerper[id].sicher.push(lid);
        let mut l2 = l.clone();
        l2.namen.extend(params.iter().cloned());
        for p in params { l2.typen.remove(p); }
        self.expr(lid, body, z, &l2);
        lid
    }

    fn expr(&mut self, id: usize, e: &Expr, z: usize, l: &Lokal) {
        match e {
            Expr::Identifier(n) => {
                if self.ist_global(n, l) {
                    self.koerper[id].zugriffe.push((n.clone(), z, false));
                } else if let Some(f) = self.funktion(n, l) {
                    // Funktion als Wert: laeuft vielleicht hier (map, Rueckruf).
                    self.adresse.insert(f);
                    self.koerper[id].vielleicht.push(f);
                    // Im nativen Systemkoerper ist ein Funktionsname als
                    // Wert nur als C-Rueckrufadresse erlaubt (rueckruf_adresse).
                    // Schon das Bilden erfassen: deckt auch lokale Zeiger,
                    // Strukturfelder, Weitergabe und spaetere C-Aufrufe ab.
                    if self.koerper[id].nativ {
                        self.fremd_wurzeln.push(f);
                    }
                }
            }
            Expr::FunctionCall { name, args, zeile, .. } => {
                let z = if *zeile > 0 { *zeile } else { z };
                let mut rest: &[Expr] = args;
                if name == "starte" || name == "spawn" {
                    match args.first() {
                        Some(Expr::Identifier(f)) if self.funktion(f, l).is_some() => {
                            let f = self.funktion(f, l);
                            self.wurzeln.push((f, z));
                            rest = &args[1..];
                        }
                        Some(Expr::Lambda { params, body, .. }) => {
                            let lid = self.lambda(id, params, body, z, l);
                            self.wurzeln.push((Some(lid), z));
                            rest = &args[1..];
                        }
                        _ => self.wurzeln.push((None, z)),
                    }
                } else if self.extern_c.contains(name.as_str())
                    || FREMDTHREAD_RUECKRUFE.iter().any(|(n, _)| n == name)
                {
                    // P2-T114 B: Rueckruf, den C bzw. die Runtime auf einem
                    // fremden Thread aufrufen kann -> Laufzeitpruefung.
                    for (i, a) in args.iter().enumerate() {
                        let gilt = self.extern_c.contains(name.as_str())
                            || FREMDTHREAD_RUECKRUFE.iter().any(|(n, k)| n == name && *k == i);
                        match a {
                            Expr::Identifier(f) if gilt && self.funktion(f, l).is_some() => {
                                let f = self.funktion(f, l).unwrap_or_default();
                                self.fremd_wurzeln.push(f);
                            }
                            Expr::Lambda { params, body, .. } if gilt => {
                                let lid = self.lambda(id, params, body, z, l);
                                self.fremd_wurzeln.push(lid);
                                continue;
                            }
                            _ => {}
                        }
                        self.expr(id, a, z, l);
                    }
                    rest = &[];
                } else if let Some(f) = self.funktion(name, l) {
                    self.koerper[id].sicher.push(f);
                } else if l.namen.contains(name.as_str()) || self.globale.contains_key(name.as_str()) {
                    // Aufruf eines Funktionswerts.
                    if self.ist_global(name, l) {
                        self.koerper[id].zugriffe.push((name.clone(), z, false));
                    }
                    self.koerper[id].dynamisch = true;
                }
                for a in rest {
                    self.expr(id, a, z, l);
                }
            }
            Expr::MethodCall { object, method, args } => {
                self.expr(id, object, z, l);
                for a in args {
                    self.expr(id, a, z, l);
                }
                if let Some(ziel) = self.methode_fuer_typ(object, l, method) {
                    self.koerper[id].vielleicht.push(ziel);
                } else if let Some(ms) = self.methoden.get(method).cloned() {
                    // Parameter, wechselnde Typen und andere unbekannte
                    // Empfaenger bleiben konservativ nach Namen aufgeloest.
                    self.koerper[id].vielleicht.extend(ms);
                }
            }
            Expr::New { class_name, args } => {
                for a in args {
                    self.expr(id, a, z, l);
                }
                if let Some(&k) = self.konstruktoren.get(class_name) {
                    self.koerper[id].sicher.push(k);
                }
            }
            Expr::Lambda { params, body, .. } => {
                self.lambda(id, params, body, z, l);
            }
            Expr::Pipe { left, right } => {
                self.expr(id, left, z, l);
                match right.as_ref() {
                    Expr::Identifier(f) if self.funktion(f, l).is_some() => {
                        let f = self.funktion(f, l).unwrap_or_default();
                        self.koerper[id].sicher.push(f);
                    }
                    r => self.expr(id, r, z, l),
                }
            }
            Expr::ListComprehension { expr, var_name, iterable, condition } => {
                self.expr(id, iterable, z, l);
                let mut l2 = l.clone();
                l2.namen.insert(var_name.clone());
                l2.typen.remove(var_name);
                self.expr(id, expr, z, &l2);
                if let Some(c) = condition {
                    self.expr(id, c, z, &l2);
                }
            }
            Expr::QueryExpr { var_name, source, where_cond, order_expr, select_expr } => {
                self.expr(id, source, z, l);
                let mut l2 = l.clone();
                l2.namen.insert(var_name.clone());
                l2.typen.remove(var_name);
                for t in [where_cond.as_deref(), order_expr.as_deref(), Some(select_expr.as_ref())].into_iter().flatten() {
                    self.expr(id, t, z, &l2);
                }
            }
            Expr::MatchExpr { value, cases } => {
                self.expr(id, value, z, l);
                for (muster, waechter, ergebnis) in cases {
                    let mut l2 = l.clone();
                    if let Some(m) = muster {
                        let mut gebunden = HashSet::new();
                        namen_in(m, &mut gebunden);
                        for n in gebunden { l2.typen.remove(&n); l2.namen.insert(n); }
                    }
                    if let Some(w) = waechter {
                        self.expr(id, w, z, &l2);
                    }
                    self.expr(id, ergebnis, z, &l2);
                }
            }
            andere => {
                for k in kinder(andere) {
                    self.expr(id, k, z, l);
                }
            }
        }
    }
}

/// Literal eines Werttyps ohne Referenzzaehler (Zahl, Wahrheitswert, nichts).
fn ist_skalar_literal(e: &Expr) -> bool {
    match e {
        Expr::Number(_) | Expr::Boolean(_) | Expr::None => true,
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => matches!(operand.as_ref(), Expr::Number(_)),
        _ => false,
    }
}

fn effekt_melden(d: Diagnose) {
    FEHLER.with(|f| f.set(f.get() + 1));
    diagnose::melden(d);
}

const ABHILFE_GLOBALE: &str = "Den Wert per Kanal senden oder als Argument von starte uebergeben, \
    die Globale mit 'geteilt' markieren (geteilt konstante NAME auf wert, dann tief eingefroren) \
    oder den Zugriff ueber eine Sperre bzw. ein Atom fuehren.";

/// P2-T114 Phase C: Effektpruefung der Globalen (siehe Abschnittskopf).
/// Meldet Fehler selbst; Err, wenn welche gemeldet wurden.
pub fn globale_pruefen(program: &Program, datei: &str) -> Result<GlobalePruefplan, String> {
    let mut plan = GlobalePruefplan::default();
    let mut globale: HashMap<String, bool> = HashMap::new();
    for s in &program.statements {
        match s {
            Stmt::Assignment { name, .. } => {
                globale.entry(name.clone()).or_insert(false);
            }
            Stmt::ConstAssignment { name, geteilt, .. } => {
                let e = globale.entry(name.clone()).or_insert(false);
                *e |= *geteilt;
            }
            _ => {}
        }
    }
    plan.geteilt = globale.iter().filter(|(_, g)| **g).map(|(n, _)| n.clone()).collect();
    // Kandidaten fuer unveraenderliche Wertkonstanten (siehe unten).
    let skalar_konst: HashSet<String> = program.statements.iter().filter_map(|s| match s {
        Stmt::ConstAssignment { name, value, geteilt: false, .. } if ist_skalar_literal(value) => Some(name.clone()),
        _ => None,
    }).collect();
    // P2-T114 B: Text-Konstanten (Texte sind unveraenderlich). Nur solche vor
    // der ersten Anweisung, die Code ausfuehren kann: dann ist die Globale
    // gesetzt und geteilt, bevor irgendein Thread starten kann.
    let mut text_konst: HashSet<String> = HashSet::new();
    for s in &program.statements {
        match s {
            Stmt::ConstAssignment { name, value: Expr::String(_), geteilt: false, .. } => {
                text_konst.insert(name.clone());
            }
            Stmt::ConstAssignment { value, .. } | Stmt::Assignment { value, .. }
                if ist_skalar_literal(value) || matches!(value, Expr::String(_)) => {}
            Stmt::FunctionDef { .. } | Stmt::ClassDef { .. } | Stmt::StructDef { .. }
            | Stmt::InterfaceDef { .. } | Stmt::DataClassDef { .. } | Stmt::ExternC { .. }
            | Stmt::Import { .. } => {}
            _ => break,
        }
    }
    let extern_c: HashSet<String> = program.statements.iter().filter_map(|s| match s {
        Stmt::ExternC { name, .. } => Some(name.clone()),
        _ => None,
    }).collect();

    let mut scan = EffektScan {
        typen: Pruefer::neu(program, datei),
        globale,
        funktionen: HashMap::new(),
        methoden: HashMap::new(),
        methoden_typ: HashMap::new(),
        konstruktoren: HashMap::new(),
        koerper: vec![Koerper::neu(KoerperArt::Haupt, 0)],
        wurzeln: Vec::new(),
        adresse: HashSet::new(),
        lambdas: Vec::new(),
        haupt_schreibt: Vec::new(),
        geteilt_falsch: Vec::new(),
        extern_c,
        fremd_wurzeln: Vec::new(),
    };
    // Koerper anlegen (Funktionen, Methoden, Tests), dann scannen.
    let mut zu_scannen: Vec<(usize, &[String], &[Stmt])> = Vec::new();
    let leer: &[String] = &[];
    for s in &program.statements {
        match s {
            Stmt::FunctionDef { name, params, body, decorators, zeile, .. } => {
                let id = scan.koerper.len();
                scan.koerper.push(Koerper::neu(KoerperArt::Funktion(name.clone()), *zeile));
                scan.koerper[id].nativ = ist_nativ(decorators);
                scan.funktionen.insert(name.clone(), id);
                zu_scannen.push((id, params, body));
            }
            Stmt::ClassDef { name: klasse, body, .. } => {
                for m in body {
                    if let Stmt::FunctionDef { name, params, body, decorators, zeile, .. } = m {
                        let id = scan.koerper.len();
                        scan.koerper.push(Koerper::neu(KoerperArt::Methode(klasse.clone(), name.clone()), *zeile));
                        scan.koerper[id].nativ = ist_nativ(decorators);
                        scan.methoden.entry(name.clone()).or_default().push(id);
                        scan.methoden_typ.insert((klasse.clone(), name.clone()), id);
                        if name == "erstelle" {
                            scan.konstruktoren.insert(klasse.clone(), id);
                        }
                        zu_scannen.push((id, params, body));
                    }
                }
            }
            Stmt::TestDef { name, body, zeile } => {
                let id = scan.koerper.len();
                scan.koerper.push(Koerper::neu(KoerperArt::Test(name.clone()), *zeile));
                zu_scannen.push((id, leer, body));
            }
            _ => {}
        }
    }
    scan.stmts(0, &program.statements, &Lokal { namen: HashSet::new(), alle: true, typen: HashMap::new() }, true);
    for (id, params, body) in zu_scannen {
        let l = scan.lokale(params, body);
        scan.stmts(id, body, &l, false);
    }

    // Sicher erreichbar (mit Vorgaenger fuer die Aufrufkette).
    let n = scan.koerper.len();
    let mut von: Vec<Option<usize>> = vec![None; n];
    let mut wurzel_zeile: Vec<usize> = vec![0; n];
    let mut sicher = vec![false; n];
    let mut schlange: std::collections::VecDeque<usize> = std::collections::VecDeque::new();
    for (w, z) in &scan.wurzeln {
        if let Some(w) = *w {
            if !sicher[w] {
                sicher[w] = true;
                wurzel_zeile[w] = *z;
                schlange.push_back(w);
            }
        }
    }
    while let Some(b) = schlange.pop_front() {
        for &k in &scan.koerper[b].sicher {
            if !sicher[k] {
                sicher[k] = true;
                von[k] = Some(b);
                schlange.push_back(k);
            }
        }
    }
    // Vielleicht erreichbar.
    let mut vielleicht = sicher.clone();
    // P2-T114 B: Rueckrufe auf fremden Threads sind vielleicht-Wurzeln.
    for &w in &scan.fremd_wurzeln {
        vielleicht[w] = true;
    }
    schlange.extend((0..n).filter(|&b| vielleicht[b]));
    let mut dynamisch = scan.wurzeln.iter().any(|(w, _)| w.is_none());
    let mut erweitert = false;
    loop {
        while let Some(b) = schlange.pop_front() {
            dynamisch |= scan.koerper[b].dynamisch;
            let kanten: Vec<usize> = scan.koerper[b].sicher.iter().chain(scan.koerper[b].vielleicht.iter()).copied().collect();
            for k in kanten {
                if !vielleicht[k] {
                    vielleicht[k] = true;
                    schlange.push_back(k);
                }
            }
        }
        if dynamisch && !erweitert {
            erweitert = true;
            let ziele: Vec<usize> = scan.adresse.iter().chain(scan.lambdas.iter()).copied().collect();
            for k in ziele {
                if !vielleicht[k] {
                    vielleicht[k] = true;
                    schlange.push_back(k);
                }
            }
            continue;
        }
        break;
    }

    // `konstante N auf 5` (Zahl, Wahrheitswert, nichts), im ganzen Programm
    // genau einmal geschrieben: Wertetyp ohne Referenzzaehler und nie
    // geaendert, also von Threads lesbar wie eine geteilte Globale.
    let mut schreibzahl: HashMap<&str, usize> = HashMap::new();
    for (g, _) in &scan.haupt_schreibt {
        *schreibzahl.entry(g.as_str()).or_default() += 1;
    }
    for k in &scan.koerper {
        for (g, _, schreibt) in &k.zugriffe {
            if *schreibt {
                *schreibzahl.entry(g.as_str()).or_default() += 1;
            }
        }
    }
    let unveraenderlich: HashSet<String> = skalar_konst.into_iter().chain(text_konst.iter().cloned())
        .filter(|g| schreibzahl.get(g.as_str()).copied().unwrap_or(0) == 1)
        .collect();
    plan.geteilt.extend(unveraenderlich.iter().cloned());
    // Nur wenn es Threads geben kann (starte/Rueckruf): sonst IR unveraendert.
    if !scan.wurzeln.is_empty() || !scan.fremd_wurzeln.is_empty() {
        plan.text_teilen = unveraenderlich.iter().filter(|g| text_konst.contains(*g)).cloned().collect();
    }

    let quelle = |k: &Koerper| -> String {
        match &k.art {
            KoerperArt::Funktion(f) => program.funktion_quellen.get(f).cloned().unwrap_or_else(|| datei.to_string()),
            _ => datei.to_string(),
        }
    };
    let streng = STRENG.with(|s| s.get());
    let mut fehler = 0usize;
    for b in 0..n {
        let k = &scan.koerper[b];
        let mut gemeldet: HashSet<&str> = HashSet::new();
        for (g, z, schreibt) in &k.zugriffe {
            let ist_geteilt = scan.globale.get(g).copied().unwrap_or(false);
            if ist_geteilt {
                if *schreibt && gemeldet.insert(g.as_str()) {
                    fehler += 1;
                    effekt_melden(Diagnose::fehler("M-P003", format!(
                        "Die Globale '{g}' ist geteilt und damit eingefroren; sie kann nicht neu gesetzt werden."))
                        .ort(*z, 0).datei(quelle(k)).funktion(k.kurz())
                        .vorschlag("Eine veraenderliche Globale ohne 'geteilt' anlegen, die nur der Hauptthread aendert, oder eine Sperre bzw. ein Atom verwenden."));
                }
                continue;
            }
            if unveraenderlich.contains(g) {
                continue;
            }
            if !vielleicht[b] || !gemeldet.insert(g.as_str()) {
                continue;
            }
            let tun = if *schreibt { "schreibt" } else { "liest" };
            if sicher[b] {
                // Aufrufkette bis zur Wurzel.
                let mut kette = vec![k.kurz()];
                let mut c = b;
                while let Some(p) = von[c] {
                    kette.push(scan.koerper[p].kurz());
                    c = p;
                }
                kette.reverse();
                fehler += 1;
                effekt_melden(Diagnose::fehler("M-P001", format!(
                    "{} laeuft auf einem eigenen Thread (starte in Zeile {}: {}) und {tun} die Globale '{g}'. Globale gehoeren dem Hauptthread.",
                    k.anzeige(), wurzel_zeile[c], kette.join(" -> ")))
                    .ort(*z, 0).datei(quelle(k)).funktion(k.kurz())
                    .vorschlag(ABHILFE_GLOBALE));
            } else if streng {
                fehler += 1;
                effekt_melden(Diagnose::fehler("M-P002", format!(
                    "{} laeuft vielleicht auf einem eigenen Thread (Aufruf ueber einen Funktionswert oder eine Methode, nicht beweisbar) und {tun} die Globale '{g}' (--typ-streng).",
                    k.anzeige()))
                    .ort(*z, 0).datei(quelle(k)).funktion(k.kurz())
                    .vorschlag(ABHILFE_GLOBALE));
            } else {
                match &k.art {
                    KoerperArt::Funktion(f) => { plan.funktionen.insert(f.clone()); }
                    KoerperArt::Methode(kl, m) => { plan.funktionen.insert(format!("{kl}__{m}")); }
                    KoerperArt::Lambda => plan.lambdas = true,
                    KoerperArt::Haupt | KoerperArt::Test(_) => {}
                }
            }
        }
    }
    let mut doppelt: HashSet<&str> = HashSet::new();
    for (g, z) in &scan.haupt_schreibt {
        if scan.globale.get(g).copied().unwrap_or(false) && doppelt.insert(g.as_str()) {
            fehler += 1;
            effekt_melden(Diagnose::fehler("M-P003", format!(
                "Die Globale '{g}' ist geteilt und damit eingefroren; sie kann nicht neu gesetzt werden."))
                .ort(*z, 0).datei(datei.to_string())
                .vorschlag("Eine veraenderliche Globale ohne 'geteilt' anlegen, die nur der Hauptthread aendert, oder eine Sperre bzw. ein Atom verwenden."));
        }
    }
    for (g, z) in &scan.geteilt_falsch {
        fehler += 1;
        effekt_melden(Diagnose::fehler("M-P003", format!(
            "'geteilt' gilt nur fuer Globale auf oberster Ebene, nicht fuer '{g}' in einem Block oder einer Funktion."))
            .ort(*z, 0).datei(datei.to_string())
            .vorschlag("Die Zeile 'geteilt konstante NAME auf wert' auf die oberste Ebene der Datei stellen."));
    }
    if fehler > 0 {
        return Err(format!("{fehler} Fehler bei Globalen und Threads"));
    }
    Ok(plan)
}


#[cfg(test)]
mod effekt_tests {
    use super::*;

    fn plan(quelle: &str) -> GlobalePruefplan {
        STRENG.with(|s| s.set(false));
        let tokens = crate::lexer::Lexer::new().tokenize(quelle).expect("lexer");
        let programm = crate::parser::Parser::new(tokens).parse().expect("parser");
        globale_pruefen(&programm, "effekt-test.moos").expect("Effektplan")
    }

    const KLASSEN: &str = "setze g auf 3\n\
        klasse A:\n    funktion m(x):\n        gib_zurück x * g\n\
        klasse B:\n    funktion m(x):\n        gib_zurück x\n\
        klasse C(B):\n    funktion erstelle():\n        gib_zurück nichts\n";

    fn methoden_plan(body: &str) -> GlobalePruefplan {
        plan(&format!("{KLASSEN}\nfunktion arbeite(n):\n{body}\nstarte(arbeite, 0)\n"))
    }

    #[test]
    fn bekannte_klasse_alias_und_erbe_trennen_gleichnamige_methoden() {
        for klasse in ["B", "C"] {
            let p = methoden_plan(&format!(
                "    setze r auf neu {klasse}()\n    setze alias auf r\n    gib_zurück alias.m(n)"));
            assert!(!p.funktionen.contains("A__m"), "{klasse}: fremde Methode erfasst");
        }
    }

    #[test]
    fn wechselnder_empfaengertyp_bleibt_konservativ() {
        let p = methoden_plan("    setze r auf neu B()\n    wenn n > 0:\n        setze r auf neu A()\n    gib_zurück r.m(n)");
        assert!(p.funktionen.contains("A__m"));
    }

    #[test]
    fn parameter_annotation_ist_keine_bewiesene_klasse() {
        let p = plan(&format!("{KLASSEN}\nfunktion arbeite(r: B):\n    gib_zurück r.m(1)\nstarte(arbeite, neu A())\n"));
        assert!(p.funktionen.contains("A__m"));
    }

    #[test]
    fn echte_zuweisung_gilt_statt_widersprechender_annotation() {
        let p = methoden_plan("    setze r: A auf neu B()\n    gib_zurück r.m(n)");
        assert!(!p.funktionen.contains("A__m"));
    }

    #[test]
    fn lambda_parameter_verschattet_bekannten_aussentyp() {
        let p = methoden_plan("    setze r auf neu B()\n    setze f auf (r) => r.m(1)\n    gib_zurück f(n)");
        assert!(p.funktionen.contains("A__m"));
    }

    #[test]
    fn schleifenvariable_hat_nicht_den_frueheren_typ() {
        let p = methoden_plan("    setze r auf neu B()\n    für r in [neu A()]:\n        r.m(1)\n    gib_zurück n");
        assert!(p.funktionen.contains("A__m"));
    }

    #[test]
    fn native_funktionsadresse_erfasst_indirekt_aufgerufenen_koerper() {
        // Effektpass isoliert: die Backend-Zulaessigkeit der Koerper wird
        // hier nicht getestet. Die C-ABI-Ausfuehrung prueft rueckruf.sh.
        let p = plan("setze g auf 3\nfunktion lies():\n    gib_zurück g\n\
            @nativ\n@rückruf\nfunktion cb(x: i32) -> i32:\n    lies()\n    gib_zurück x\n\
            @nativ\nfunktion vorbereiten() -> i32:\n    unsicher:\n        setze p: ptr[fn(i32) -> i32] auf cb\n        setze q: ptr[fn(i32) -> i32] auf p\n        gib_zurück q(1)\n");
        assert!(p.funktionen.contains("lies"));
    }

    #[test]
    fn native_funktionsadresse_verfolgt_transitive_aufrufe() {
        let p = plan("setze g auf 3\nfunktion lies():\n    gib_zurück g\n\
            @nativ\nfunktion helfer(x: i32) -> i32:\n    lies()\n    gib_zurück x\n\
            @nativ\n@rückruf\nfunktion cb(x: i32) -> i32:\n    gib_zurück helfer(x)\n\
            @nativ\nfunktion vorbereiten() -> i32:\n    unsicher:\n        setze p: ptr[fn(i32) -> i32] auf cb\n        gib_zurück 0\n");
        assert!(p.funktionen.contains("lies"));
    }

    #[test]
    fn reiner_direkter_native_aufruf_ist_keine_fremdthread_wurzel() {
        let p = plan("setze g auf 3\nfunktion lies():\n    gib_zurück g\n\
            @nativ\nfunktion nur_hier(x: i32) -> i32:\n    lies()\n    gib_zurück x\n\
            nur_hier(1)\n");
        assert!(!p.funktionen.contains("lies"));
    }
}
