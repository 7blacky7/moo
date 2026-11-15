// build_aritaet.rs — P2-T100: Aritaet der Builtin-Funktionen aus src/codegen.rs
// ableiten (beim Build, kann daher nicht vom Codegen abweichen).
//
// Gesucht wird der Builtin-Match in compile_expr (`match name.as_str()` mit den
// meisten Armen). Fail-closed, alles Unklare bleibt draussen:
//  - EXAKT n: jeder Weg durch den Arm, der nicht mit Err endet, liest das
//    Argument n-1, und kein Argument ab n wird beruehrt;
//  - MINDESTENS n: jeder solche Weg liest das Argument n-1 (unbedingter
//    Index), der Arm nutzt `args` aber auch anders (len/iter/Weitergabe,
//    nicht konstanter Index) oder liest hoehere Indizes nur bedingt.
// Bedingungen fuer beide:
//  - Muster nur aus Stringliteralen, kein Guard; kein frueherer Arm mit einem
//    Nicht-Literal-Muster, der den Namen vorher abfangen koennte;
//  - der Name steht in genau einem Arm;
//  - exakt: `args` erscheint nur als `args[<Ganzzahlliteral>]`;
//  - ABBILD: `let x = args.iter().map(..).collect..()?` (nur iter/into_iter/
//    map/cloned/copied/enumerate/collect und `?`) hat die Laenge von args;
//    `x[N]` zaehlt wie `args[N]` (nie exakt: alle Argumente werden
//    ausgewertet). Nur fuer eine UNVERAENDERLICHE Bindung (kein `mut`, kein
//    `ref`), deren Name ausserhalb von Closures im ganzen Arm genau einmal
//    gebunden wird (kein Schatten durch let/Muster/for). Innerhalb von
//    Closures gilt KEIN Abbild (Parameter inkl. Tuple-/Struct-Muster, match,
//    if let, let im Closure koennten den Namen verdecken); Zugriffe dort
//    zaehlen ohnehin als bedingt und liefern keine Stellen. Ohne `mut` kann
//    Rust den Vec nicht veraendern (push, Zuweisung, &mut-Entleihe sind
//    Compilefehler; Vec hat keine innere Veraenderlichkeit);
//  - nie in Makros, nicht neu gebunden;
//  - "unbedingt" heisst: nicht in einem if/match-Zweig, Schleifenkoerper,
//    Closure, let-else, rechts von && / || und nicht nach einem return;
//  - ein `return` ohne Bedingung beendet den Arm, ein bedingtes `return`
//    (ausser `return Err(..)`) verlaesst ihn vielleicht: danach gelesene
//    Indizes zaehlen in beiden Faellen nicht als unbedingt (nur Lesen VOR
//    jedem moeglichen Ausstieg gilt auf jedem Weg); kein break/continue aus
//    dem Arm, keine verschachtelten Items.
// Ergebnis: OUT_DIR/builtin_aritaet.rs mit ARITAET (Name, Anzahl, exakt;
// sortiert), AUSGESCHLOSSEN (Name -> Grund, alle uebrigen Namen des Matchs) und
// ARGS_STELLEN (Name -> Zeile/Spalte jedes `args[..]` in seinen Armen; damit
// erkennt das Panic-Netz ein Lesen ueber das Ende der Argumente exakt).
// Fail-closed (Review 5dbf36bc): Stellen nur aus Armen, deren Pruefung Ok ist;
// ein Arm, der `args` irgendwo neu bindet (jede Bindungsform, auch Closure),
// ist Err und liefert keine Stellen.

use std::collections::{BTreeMap, BTreeSet, HashMap};
use syn::spanned::Spanned;
use syn::visit::{self, Visit};

/// Ist `e` eine Kette ab `args` aus laengenerhaltenden Schritten
/// (iter/into_iter/map/cloned/copied/enumerate/collect, auch mit `?`)?
fn ist_args_abbild(e: &syn::Expr) -> bool {
    match e {
        syn::Expr::Try(t) => ist_args_abbild(&t.expr),
        syn::Expr::Paren(p) => ist_args_abbild(&p.expr),
        syn::Expr::MethodCall(mc) => {
            let m = mc.method.to_string();
            let erlaubt = matches!(m.as_str(), "iter" | "into_iter" | "map" | "cloned" | "copied" | "enumerate" | "collect");
            erlaubt && (ist_ident(&mc.receiver, "args") || ist_args_abbild(&mc.receiver))
                && (m != "iter" && m != "into_iter" || ist_ident(&mc.receiver, "args"))
        }
        _ => false,
    }
}

/// Zaehlt jede Namensbindung (let, Muster, for) im Arm ausserhalb von
/// Closures (deren Parameter und Inneres sind ein eigener Gueltigkeitsbereich).
#[derive(Default)]
struct BindungsZaehler {
    anzahl: HashMap<String, usize>,
}

impl<'a> Visit<'a> for BindungsZaehler {
    fn visit_pat_ident(&mut self, p: &'a syn::PatIdent) {
        *self.anzahl.entry(p.ident.to_string()).or_default() += 1;
        visit::visit_pat_ident(self, p);
    }
    fn visit_expr_closure(&mut self, _: &'a syn::ExprClosure) {}
}

/// Name einer unveraenderlichen Einzelbindung (`x` oder `x: T`, ohne mut/ref).
fn pat_ident_fest(p: &syn::Pat) -> Option<String> {
    match p {
        syn::Pat::Ident(i) if i.subpat.is_none() && i.mutability.is_none() && i.by_ref.is_none() => {
            Some(i.ident.to_string())
        }
        syn::Pat::Type(t) => pat_ident_fest(&t.pat),
        _ => None,
    }
}

fn pat_ident(p: &syn::Pat) -> Option<String> {
    match p {
        syn::Pat::Ident(i) if i.subpat.is_none() => Some(i.ident.to_string()),
        syn::Pat::Type(t) => pat_ident(&t.pat),
        _ => None,
    }
}

#[derive(Default)]
struct MatchSuche<'a> {
    kandidaten: Vec<&'a syn::ExprMatch>,
}

impl<'a> Visit<'a> for MatchSuche<'a> {
    fn visit_expr_match(&mut self, m: &'a syn::ExprMatch) {
        if let syn::Expr::MethodCall(mc) = &*m.expr {
            if mc.method == "as_str" && mc.args.is_empty() && ist_ident(&mc.receiver, "name") {
                self.kandidaten.push(m);
            }
        }
        visit::visit_expr_match(self, m);
    }
}

fn ist_ident(e: &syn::Expr, name: &str) -> bool {
    matches!(e, syn::Expr::Path(p) if p.qself.is_none() && p.path.is_ident(name))
}

/// Alle Namen eines Musters, wenn es nur aus Stringliteralen besteht.
fn literal_namen(p: &syn::Pat) -> Option<Vec<String>> {
    match p {
        syn::Pat::Lit(l) => match &l.lit {
            syn::Lit::Str(s) => Some(vec![s.value()]),
            _ => None,
        },
        syn::Pat::Or(o) => {
            let mut v = Vec::new();
            for c in &o.cases {
                v.extend(literal_namen(c)?);
            }
            Some(v)
        }
        syn::Pat::Paren(p) => literal_namen(&p.pat),
        _ => None,
    }
}

#[derive(Default)]
struct ArmPruefung {
    bedingt: usize,
    schleife: usize,
    closure: usize,
    unbedingt: BTreeSet<usize>,
    alle: BTreeSet<usize>,
    /// Nach einem unbedingten oder bedingten (nicht-Err) `return`: der Arm ist
    /// (vielleicht) verlassen, spaetere Indizes gelten nicht mehr als unbedingt.
    beendet: bool,
    /// `args` auch anders als mit konstantem Index genutzt.
    ganz: bool,
    /// Namen, die gerade an ein Abbild von args (gleiche Laenge) gebunden sind.
    abbilder: BTreeSet<String>,
    /// Namen, die im Arm mehr als einmal gebunden werden (nie Abbild).
    mehrfach: BTreeSet<String>,
    /// Zeile/Spalte (1-basiert wie std::panic::Location) jedes args-/Abbild-Index.
    stellen: BTreeSet<(usize, usize)>,
    verboten: Option<&'static str>,
}

impl ArmPruefung {
    fn verbieten(&mut self, grund: &'static str) {
        self.verboten.get_or_insert(grund);
    }

    fn bedingt<F: FnOnce(&mut Self)>(&mut self, f: F) {
        self.bedingt += 1;
        f(self);
        self.bedingt -= 1;
    }
}

fn ist_err_aufruf(e: &syn::Expr) -> bool {
    match e {
        syn::Expr::Call(c) => ist_ident(&c.func, "Err"),
        syn::Expr::Paren(p) => ist_err_aufruf(&p.expr),
        _ => false,
    }
}

impl<'a> Visit<'a> for ArmPruefung {
    fn visit_expr_index(&mut self, i: &'a syn::ExprIndex) {
        let abbild = matches!(&*i.expr, syn::Expr::Path(p) if p.qself.is_none()
            && p.path.get_ident().is_some_and(|id| self.abbilder.contains(&id.to_string())));
        if ist_ident(&i.expr, "args") || abbild {
            // Der Panic-Ort einer Indexgrenze zeigt (gemessen, rustc) auf das
            // `[`; beide Positionen desselben Ausdrucks gelten als Stelle.
            let start = i.span().start();
            self.stellen.insert((start.line, start.column + 1));
            let klammer = i.bracket_token.span.open().start();
            self.stellen.insert((klammer.line, klammer.column + 1));
            match &*i.index {
                syn::Expr::Lit(syn::ExprLit { lit: syn::Lit::Int(n), .. }) => match n.base10_parse::<usize>() {
                    Ok(n) => {
                        self.alle.insert(n);
                        if self.bedingt == 0 && !self.beendet {
                            self.unbedingt.insert(n);
                        }
                    }
                    Err(_) => self.verbieten("Index nicht lesbar"),
                },
                _ => {
                    self.ganz = true;
                    self.visit_expr(&i.index);
                }
            }
            return;
        }
        visit::visit_expr_index(self, i);
    }
    fn visit_expr_path(&mut self, p: &'a syn::ExprPath) {
        if p.qself.is_none() && p.path.is_ident("args") {
            self.ganz = true;
        }
        visit::visit_expr_path(self, p);
    }
    fn visit_macro(&mut self, m: &'a syn::Macro) {
        let text = m.tokens.to_string();
        if text.split(|c: char| !(c.is_alphanumeric() || c == '_')).any(|w| w == "args") {
            self.verbieten("args in einem Makro");
        }
        visit::visit_macro(self, m);
    }
    fn visit_pat_ident(&mut self, p: &'a syn::PatIdent) {
        if p.ident == "args" {
            self.verbieten("args neu gebunden");
        }
        visit::visit_pat_ident(self, p);
    }
    fn visit_item(&mut self, _: &'a syn::Item) {
        self.verbieten("verschachteltes Item");
    }
    fn visit_expr_if(&mut self, e: &'a syn::ExprIf) {
        self.visit_expr(&e.cond);
        self.bedingt(|s| {
            s.visit_block(&e.then_branch);
            if let Some((_, sonst)) = &e.else_branch {
                s.visit_expr(sonst);
            }
        });
    }
    fn visit_expr_match(&mut self, e: &'a syn::ExprMatch) {
        self.visit_expr(&e.expr);
        self.bedingt(|s| {
            for arm in &e.arms {
                s.visit_arm(arm);
            }
        });
    }
    fn visit_expr_binary(&mut self, e: &'a syn::ExprBinary) {
        if matches!(e.op, syn::BinOp::And(_) | syn::BinOp::Or(_)) {
            self.visit_expr(&e.left);
            self.bedingt(|s| s.visit_expr(&e.right));
        } else {
            visit::visit_expr_binary(self, e);
        }
    }
    fn visit_expr_while(&mut self, e: &'a syn::ExprWhile) {
        self.schleife += 1;
        self.bedingt(|s| visit::visit_expr_while(s, e));
        self.schleife -= 1;
    }
    fn visit_expr_for_loop(&mut self, e: &'a syn::ExprForLoop) {
        self.visit_expr(&e.expr);
        self.schleife += 1;
        self.bedingt(|s| {
            s.visit_pat(&e.pat);
            s.visit_block(&e.body);
        });
        self.schleife -= 1;
    }
    fn visit_expr_loop(&mut self, e: &'a syn::ExprLoop) {
        self.schleife += 1;
        self.bedingt(|s| visit::visit_expr_loop(s, e));
        self.schleife -= 1;
    }
    fn visit_expr_closure(&mut self, e: &'a syn::ExprClosure) {
        // Review 21765f57: im Closure kein Abbild, gleich welche Bindung verdeckt.
        let gesichert = std::mem::take(&mut self.abbilder);
        self.closure += 1;
        self.bedingt(|s| visit::visit_expr_closure(s, e));
        self.closure -= 1;
        self.abbilder = gesichert;
    }
    fn visit_local(&mut self, l: &'a syn::Local) {
        self.visit_pat(&l.pat);
        if let Some(init) = &l.init {
            self.visit_expr(&init.expr);
            if let Some((_, sonst)) = &init.diverge {
                self.bedingt(|s| s.visit_expr(sonst));
            }
        }
        if let Some(n) = pat_ident(&l.pat) {
            let ist_abbild = l.init.as_ref().is_some_and(|i| i.diverge.is_none() && ist_args_abbild(&i.expr))
                && pat_ident_fest(&l.pat).is_some()
                && !self.mehrfach.contains(&n);
            if ist_abbild && self.bedingt == 0 {
                self.abbilder.insert(n);
            } else {
                self.abbilder.remove(&n);
            }
        }
    }
    fn visit_expr_return(&mut self, e: &'a syn::ExprReturn) {
        visit::visit_expr_return(self, e);
        if self.closure > 0 {
            return;
        }
        if self.bedingt == 0 || !e.expr.as_deref().is_some_and(ist_err_aufruf) {
            self.beendet = true;
        }
    }
    fn visit_expr_break(&mut self, e: &'a syn::ExprBreak) {
        if e.label.is_some() || (self.schleife == 0 && self.closure == 0) {
            self.verbieten("break aus dem Arm");
        }
        visit::visit_expr_break(self, e);
    }
    fn visit_expr_continue(&mut self, e: &'a syn::ExprContinue) {
        if e.label.is_some() || (self.schleife == 0 && self.closure == 0) {
            self.verbieten("continue aus dem Arm");
        }
        visit::visit_expr_continue(self, e);
    }
}

/// Argumentzahl eines Arms (Anzahl, exakt) oder der Grund, warum keine
/// nachweisbar ist, dazu die args-/Abbild-Indexstellen des Arms.
pub(crate) fn arm_pruefen(body: &syn::Expr) -> (Result<(u8, bool), &'static str>, BTreeSet<(usize, usize)>) {
    let mut zaehler = BindungsZaehler::default();
    zaehler.visit_expr(body);
    let mut p = ArmPruefung {
        mehrfach: zaehler.anzahl.into_iter().filter(|(_, n)| *n > 1).map(|(name, _)| name).collect(),
        ..ArmPruefung::default()
    };
    p.visit_expr(body);
    let stellen = std::mem::take(&mut p.stellen);
    let ergebnis = arm_aritaet(p);
    let stellen = if ergebnis.is_ok() { stellen } else { BTreeSet::new() };
    (ergebnis, stellen)
}

fn arm_aritaet(p: ArmPruefung) -> Result<(u8, bool), &'static str> {
    if let Some(grund) = p.verboten {
        return Err(grund);
    }
    let max_alle = p.alle.iter().next_back().copied();
    let max_unbedingt = p.unbedingt.iter().next_back().copied();
    let zahl = |m: usize| u8::try_from(m + 1).map_err(|_| "Index zu gross");
    match (max_alle, max_unbedingt) {
        (None, _) if !p.ganz => Ok((0, true)),
        (Some(a), Some(u)) if a == u && !p.ganz => Ok((zahl(u)?, true)),
        (_, Some(u)) => Ok((zahl(u)?, false)),
        (_, None) if p.ganz => Err("args als Ganzes ohne unbedingten Index"),
        (_, None) => Err("Index nur bedingt gelesen"),
    }
}

pub fn erzeugen() {
    println!("cargo:rerun-if-changed=src/codegen.rs");
    println!("cargo:rerun-if-changed=build_aritaet.rs");
    let manifest = std::path::PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").expect("CARGO_MANIFEST_DIR"));
    let quelle = std::fs::read_to_string(manifest.join("src/codegen.rs")).expect("src/codegen.rs nicht lesbar");
    let datei = syn::parse_file(&quelle).expect("src/codegen.rs fuer die Aritaetsableitung nicht parsebar");
    let mut suche = MatchSuche::default();
    suche.visit_file(&datei);
    let builtin_match = suche.kandidaten.iter().copied().max_by_key(|m| m.arms.len())
        .expect("Builtin-Match `match name.as_str()` in src/codegen.rs nicht gefunden");
    assert!(builtin_match.arms.len() >= 100,
        "Builtin-Match in src/codegen.rs hat nur {} Arme; Aritaetsableitung pruefen", builtin_match.arms.len());

    let mut stellen: BTreeMap<String, BTreeSet<(usize, usize)>> = BTreeMap::new();
    let mut vorkommen: HashMap<String, usize> = HashMap::new();
    let mut ergebnis: BTreeMap<String, Result<(u8, bool), &'static str>> = BTreeMap::new();
    let mut abgefangen = false;
    for arm in &builtin_match.arms {
        let Some(namen) = literal_namen(&arm.pat) else {
            abgefangen = true;
            continue;
        };
        let (gepr, arm_stellen) = arm_pruefen(&arm.body);
        let wert = if abgefangen {
            Err("frueherer Arm mit Nicht-Literal-Muster")
        } else if arm.guard.is_some() {
            Err("Arm mit Guard")
        } else {
            gepr
        };
        for n in &namen {
            stellen.entry(n.clone()).or_default().extend(arm_stellen.iter().copied());
        }
        for n in namen {
            *vorkommen.entry(n.clone()).or_default() += 1;
            ergebnis.entry(n).or_insert(wert);
        }
    }
    for (n, wert) in ergebnis.iter_mut() {
        if vorkommen.get(n).copied().unwrap_or(0) > 1 {
            *wert = Err("Name in mehreren Armen");
        }
    }

    let mut out = String::from(
        "// Generiert von compiler/build_aritaet.rs aus src/codegen.rs (P2-T100). Nicht von Hand aendern.\n");
    out.push_str(&format!("pub const ARME_GESAMT: usize = {};\n", builtin_match.arms.len()));
    out.push_str("pub static ARITAET: &[(&str, u8, bool)] = &[\n");
    for (n, (a, exakt)) in ergebnis.iter().filter_map(|(n, w)| w.ok().map(|a| (n, a))) {
        out.push_str(&format!("    ({n:?}, {a}, {exakt}),\n"));
    }
    out.push_str("];\npub static AUSGESCHLOSSEN: &[(&str, &str)] = &[\n");
    for (n, g) in ergebnis.iter().filter_map(|(n, w)| w.err().map(|g| (n, g))) {
        out.push_str(&format!("    ({n:?}, {g:?}),\n"));
    }
    out.push_str("];\npub static ARGS_STELLEN: &[(&str, &[(u32, u32)])] = &[\n");
    for (n, s) in &stellen {
        let liste: Vec<String> = s.iter().map(|(z, sp)| format!("({z}, {sp})")).collect();
        out.push_str(&format!("    ({n:?}, &[{}]),\n", liste.join(", ")));
    }
    out.push_str("];\n");
    let ziel = std::path::PathBuf::from(std::env::var("OUT_DIR").expect("OUT_DIR")).join("builtin_aritaet.rs");
    std::fs::write(&ziel, out).expect("builtin_aritaet.rs nicht schreibbar");
}
