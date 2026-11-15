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
//! geschuetzt. Stufe 1 kennt nur `wirf` (O3); Builtins folgen spaeter mit
//! kuratierter, belegter Liste. Grenze (O5): Top-Level-Code importierter
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
                Stmt::FunctionDef { name, body, decorators, .. } if !ist_nativ(decorators) => {
                    if let Some(zeile) = wurfstelle(body) {
                        let datei = self.datei_von(name);
                        self.funktionen.insert(name.clone(), Wurf { datei, zeile });
                    }
                }
                Stmt::ClassDef { name: klasse, body, .. } => {
                    for m in body {
                        if let Stmt::FunctionDef { name, body, .. } = ohne_export(m) {
                            let wurf = wurfstelle(body).map(|zeile| Wurf { datei: self.datei_von(klasse), zeile });
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

    fn melde(&mut self, ort: &Ort<'_>, zeile: usize, name: &str, wurf: &Wurf) {
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
        let mut d = Diagnose::hinweis("M-F001", format!(
            "Aufruf von '{name}' kann einen Fehler werfen (wirf in '{name}', {}:{}) und ist hier nicht mit versuche: geschützt; {wer} danach mit offenem Fehler weiter",
            wurf.datei, wurf.zeile))
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
                        self.melde(ort, z, name, &wurf);
                    }
                }
                self.exprs(args, ort, geschuetzt, z);
            }
            Expr::MethodCall { object, method, args } => {
                if !geschuetzt && matches!(object.as_ref(), Expr::This) && ort.klasse.is_some() {
                    if let Some((name, wurf)) = self.methode(method, None) {
                        self.melde(ort, zeile, &name, &wurf);
                    }
                }
                self.expr(object, ort, geschuetzt, zeile);
                self.exprs(args, ort, geschuetzt, zeile);
            }
            Expr::New { class_name, args } => {
                if !geschuetzt {
                    if let Some((name, wurf)) = self.methode("erstelle", Some(class_name)) {
                        self.melde(ort, zeile, &name, &wurf);
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
                        self.melde(ort, zeile, name, &wurf);
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
    if pass.funktionen.is_empty() && pass.methoden.values().flatten().all(|(_, w)| w.is_none()) {
        return;
    }
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
