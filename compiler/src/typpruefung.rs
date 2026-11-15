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
    let mut p = Pruefer {
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
                | Stmt::ConstAssignment { zeile, name, ty, value } => {
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
