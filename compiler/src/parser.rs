/// Parser für moo — wandelt Tokens in einen AST um.

mod ffi;

use crate::ast::*;
use crate::tokens::{Token, TokenType};

#[derive(Debug, Clone)]
pub struct ParseError {
    pub message: String,
    pub line: usize,
    /// P2-T97: 1-basierte Spalte (0 = unbekannt), Diagnosecode und
    /// strukturierte Teile fuer `--diagnose=json`.
    pub column: usize,
    pub code: &'static str,
    pub erwartet: Option<String>,
    pub gefunden: Option<String>,
    pub vorschlag: Option<String>,
}

impl ParseError {
    /// Ort und Standardcode M-S000; die Meldung setzt der Aufrufer per
    /// Struct-Update (`ParseError { message, ..ParseError::bei(tok) }`).
    pub fn an(line: usize, column: usize) -> Self {
        ParseError {
            message: String::new(),
            line,
            column,
            code: "M-S000",
            erwartet: None,
            gefunden: None,
            vorschlag: None,
        }
    }

    pub fn bei(tok: &Token) -> Self {
        Self::an(tok.line, tok.column)
    }

    /// Einheitliche Diagnose (Datei setzt der Aufrufer).
    pub fn diagnose(&self) -> crate::diagnose::Diagnose {
        let mut d = crate::diagnose::Diagnose::fehler(self.code, self.message.clone())
            .ort(self.line, self.column);
        d.erwartet = self.erwartet.clone();
        d.gefunden = self.gefunden.clone();
        d.vorschlag = self.vorschlag.clone();
        d
    }
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Zeile {}: {}", self.line, self.message)
    }
}

/// Levenshtein-Distanz zwischen zwei Strings
fn levenshtein(a: &str, b: &str) -> usize {
    let (la, lb) = (a.len(), b.len());
    let mut prev: Vec<usize> = (0..=lb).collect();
    let mut curr = vec![0; lb + 1];
    for (i, ca) in a.chars().enumerate() {
        curr[0] = i + 1;
        for (j, cb) in b.chars().enumerate() {
            let cost = if ca == cb { 0 } else { 1 };
            curr[j + 1] = (prev[j] + cost).min(prev[j + 1] + 1).min(curr[j] + 1);
        }
        std::mem::swap(&mut prev, &mut curr);
    }
    prev[lb]
}

/// Menschenlesbare Beschreibung eines erwarteten TokenType
fn token_description(tt: &TokenType) -> &'static str {
    match tt {
        TokenType::Indent => "Einrückung (4 Leerzeichen)",
        TokenType::Dedent => "Ende der Einrückung",
        TokenType::Colon => "Doppelpunkt ':'",
        TokenType::LParen => "öffnende Klammer '('",
        TokenType::RParen => "schließende Klammer ')'",
        TokenType::LBracket => "öffnende eckige Klammer '['",
        TokenType::RBracket => "schließende eckige Klammer ']'",
        TokenType::LBrace => "öffnende geschweifte Klammer '{'",
        TokenType::RBrace => "schließende geschweifte Klammer '}'",
        TokenType::Assign => "Gleichheitszeichen '='",
        TokenType::Arrow => "Pfeil '=>'",
        TokenType::ThinArrow => "Pfeil '->'",
        TokenType::To => "'auf' / 'to'",
        TokenType::Newline => "Zeilenende",
        TokenType::Eof => "Dateiende",
        TokenType::Comma => "Komma ','",
        TokenType::Semicolon => "Semikolon ';'",
        _ => "Token",
    }
}

/// Deutsche Schluesselwoerter, die gern als Name gewaehlt werden. Fuer den
/// Hinweis, wenn ein Bezeichner erwartet war (z.B. 'setze fall auf 3').
const NAMENS_KOLLISIONEN: &[&str] = &[
    "fall", "neu", "von", "aus", "als", "wo", "daten", "standard", "teste",
    "erwarte", "parallel", "bis", "in", "ist", "klasse", "selbst", "zeige",
    "solange", "setze", "auf", "funktion", "versuche", "fange", "pruefe",
    "sortiere", "importiere", "exportiere", "konstante", "und", "oder", "nicht",
];

/// Hinweis, falls `tt` ein Schluesselwort ist, das der Nutzer als Name meinte.
fn keyword_name_hint(tt: &TokenType) -> String {
    let wort = NAMENS_KOLLISIONEN.iter().find(|w| {
        crate::tokens::keyword_lookup(w)
            .is_some_and(|k| std::mem::discriminant(&k) == std::mem::discriminant(tt))
    });
    match wort {
        Some(w) => format!(
            " Hinweis: '{w}' ist ein Schlüsselwort ({tt:?}) und kann nicht als Name verwendet werden, bitte einen anderen Namen wählen (z.B. '{w}_wert')."
        ),
        None => String::new(),
    }
}

/// Beschreibung was der Parser tatsaechlich bekommen hat
fn got_description(tt: &TokenType) -> String {
    match tt {
        TokenType::Identifier(name) => format!("'{name}'"),
        TokenType::Number(n) => format!("Zahl {n}"),
        TokenType::String(s) => format!("Text \"{s}\""),
        TokenType::Boolean(b) => format!("{}", if *b { "wahr" } else { "falsch" }),
        TokenType::None => "nichts".to_string(),
        TokenType::Newline => "Zeilenende".to_string(),
        TokenType::Eof => "Dateiende".to_string(),
        TokenType::Indent => "Einrückung".to_string(),
        TokenType::Dedent => "Ende der Einrückung".to_string(),
        other => format!("{:?}", other),
    }
}

/// P2-T100: Hoechste Tiefe des Syntaxbaums (Klammern, Bloecke, Operator-
/// und Postfixketten zusammen) fuer einen Compiler-Stack von
/// MAX_TIEFE_REFERENZ_STACK; gemessen: 990 Ebenen durch Parser, Typpruefung
/// und Codegen. Darueber M-S004 statt Stack Overflow. main.rs setzt das
/// Budget proportional zum tatsaechlich gestarteten Stack.
pub const MAX_TIEFE_STANDARD: usize = 1000;
pub const MAX_TIEFE_REFERENZ_STACK: usize = 512 * 1024 * 1024;
static MAX_TIEFE: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(MAX_TIEFE_STANDARD);

/// Tiefenbudget fuer einen Stack von `stack` Bytes (proportional zur Messung).
/// P2-T114 B: erster Sprung, der einen mit-Block verlassen wuerde
/// (gib_zurueck immer; abbrechen/weiter ausserhalb einer inneren Schleife).
/// Verschachtelte Funktionen und Klassen zaehlen nicht.
fn mit_sprung(stmts: &[Stmt], in_schleife: bool) -> Option<&'static str> {
    for s in stmts {
        let r = match s {
            Stmt::Return(..) => Some("gib_zurück"),
            Stmt::Break(_) if !in_schleife => Some("abbrechen"),
            Stmt::Continue(_) if !in_schleife => Some("weiter"),
            Stmt::If { body, else_body, .. } => mit_sprung(body, in_schleife).or_else(|| mit_sprung(else_body, in_schleife)),
            Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::ParallelFor { body, .. } => mit_sprung(body, true),
            Stmt::TryCatch { try_body, catch_body, .. } =>
                mit_sprung(try_body, in_schleife).or_else(|| mit_sprung(catch_body, in_schleife)),
            Stmt::Match { cases, .. } => cases.iter().find_map(|(_, _, b)| mit_sprung(b, in_schleife)),
            Stmt::Guard { else_body, .. } => mit_sprung(else_body, in_schleife),
            Stmt::UnsafeBlock { body, .. } => mit_sprung(body, in_schleife),
            _ => None,
        };
        if r.is_some() {
            return r;
        }
    }
    None
}

pub fn tiefe_fuer_stack(stack: usize) -> usize {
    MAX_TIEFE_STANDARD * (stack / (1024 * 1024)) / (MAX_TIEFE_REFERENZ_STACK / (1024 * 1024))
}

pub fn setze_max_tiefe(tiefe: usize) {
    MAX_TIEFE.store(tiefe, std::sync::atomic::Ordering::Relaxed);
}

pub fn max_tiefe() -> usize {
    MAX_TIEFE.load(std::sync::atomic::Ordering::Relaxed)
}

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
    errors: Vec<ParseError>,
    native_context: bool,
    /// P2-T100: aktuelle Tiefe im Syntaxbaum (siehe MAX_TIEFE_STANDARD).
    tiefe: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, pos: 0, errors: Vec::new(), native_context: false, tiefe: 0 }
    }

    /// P2-T100: eine Ebene tiefer (Verschachtelung oder Kettenglied).
    /// Zurueckgesetzt wird ueber `mit_tiefe` auf allen Wegen (auch Err).
    fn glied(&mut self) -> Result<(), ParseError> {
        self.tiefe += 1;
        let grenze = max_tiefe();
        if self.tiefe > grenze {
            return Err(ParseError {
                message: format!(
                    "Zu tief verschachtelt: mehr als {grenze} Ebenen (Klammern, Bloecke, Operator- oder Aufrufketten). \
                     Die Grenze haengt vom verfuegbaren Stack bzw. Speicherlimit ab (regulaer {MAX_TIEFE_STANDARD})."
                ),
                code: "M-S004",
                vorschlag: Some("Teilausdruecke in Variablen oder Funktionen auslagern.".into()),
                ..ParseError::bei(self.current())
            });
        }
        Ok(())
    }

    /// P2-T100: fuehrt `f` aus und stellt danach die Tiefe wieder her.
    fn mit_tiefe<T>(&mut self, f: impl FnOnce(&mut Self) -> Result<T, ParseError>) -> Result<T, ParseError> {
        let basis = self.tiefe;
        let r = f(self);
        self.tiefe = basis;
        r
    }

    /// P2-T97: alle Syntaxfehler eines Laufs einzeln (Recovery je Statement,
    /// auch innerhalb von Bloecken).
    pub fn parse_alle(&mut self) -> Result<Program, Vec<ParseError>> {
        let statements = match self.parse_block_body(true) {
            Ok(statements) => statements,
            Err(err) => {
                self.errors.push(err);
                Vec::new()
            }
        };
        if !self.errors.is_empty() {
            return Err(std::mem::take(&mut self.errors));
        }
        Ok(Program { statements, funktion_quellen: Default::default() })
    }

    pub fn parse(&mut self) -> Result<Program, ParseError> {
        self.parse_alle().map_err(|fehler| {
            // Ein Result, das alle Fehler nennt (bisherige Sammelmeldung).
            let mut msg = format!("{} Fehler gefunden:", fehler.len());
            for err in &fehler {
                msg.push_str(&format!("\n  Zeile {}: {}", err.line, err.message));
            }
            ParseError { message: msg, ..fehler[0].clone() }
        })
    }

    /// Springt zum nächsten Statement (nach Newline) bei einem Parse-Fehler.
    /// `im_block` (P2-T97): ein Dedent beendet den umgebenden Block und bleibt
    /// stehen; top-level wird er wie bisher verschluckt. Progression: der
    /// Aufrufer ruft parse_statement nie auf Dedent/Eof, daher steht hier
    /// entweder ein konsumierbares Token oder es wurde schon konsumiert.
    fn sync_to_next_statement(&mut self, im_block: bool) {
        loop {
            match self.current_type() {
                TokenType::Eof => return,
                TokenType::Dedent if im_block => return,
                TokenType::Newline => {
                    self.pos += 1;
                    break;
                }
                _ => self.pos += 1,
            }
        }
        // Ein direkt folgender eingerueckter Block gehoert zum fehlerhaften
        // Kopf (z. B. `wenn x` ohne ':'): ganz ueberspringen, sonst entstehen
        // Folgefehler und der umgebende Block endet zu frueh.
        if matches!(self.current_type(), TokenType::Indent) {
            let mut tiefe = 0usize;
            loop {
                match self.current_type() {
                    TokenType::Eof => return,
                    TokenType::Indent => {
                        tiefe += 1;
                        self.pos += 1;
                    }
                    TokenType::Dedent => {
                        self.pos += 1;
                        tiefe -= 1;
                        if tiefe == 0 {
                            return;
                        }
                    }
                    _ => self.pos += 1,
                }
            }
        }
    }

    /// P2-T29 S5: Nach einem Statement muss die Zeile enden. Einfache
    /// Statements stehen danach vor Zeilenende/Dateiende/Dedent; Block-
    /// Statements haben ihr abschliessendes Dedent (oder Zeilenende) schon
    /// konsumiert. Sonst wurde ein Rest der Zeile nicht verstanden, z. B.
    /// `setze z auf 3 über 4` (bisher still als zweites Statement gelesen).
    /// M-S005: `+=`/`-=` an einem Ziel, das keine Variable ist. Der Vorschlag
    /// nennt den passenden Operator; enthaelt das Ziel einen Aufruf, weist er
    /// auf die einmalige Auswertung hin (ausgeschrieben liefe der Aufruf zweimal).
    fn verbund_fehler(&self, ziel: Option<&Expr>) -> ParseError {
        let (op, zeichen) = if matches!(self.current_type(), TokenType::PlusAssign) {
            ("+=", "+")
        } else {
            ("-=", "-")
        };
        let mut vorschlag = format!(
            "Ausschreiben: ziel = ziel {zeichen} wert, z. B. p.x = p.x {zeichen} n oder v[i] = v[i] {zeichen} 1.");
        if ziel.is_some_and(ausdruck_hat_aufruf) {
            vorschlag.push_str(&format!(
                " Das Ziel enthält einen Aufruf und würde ausgeschrieben zweimal ausgewertet: den Teil vorher einer Variablen zuweisen (z. B. setze i auf f(), dann a[i] = a[i] {zeichen} wert)."));
        }
        ParseError {
            message: format!("Zusammengesetzte Zuweisung '{op}' ist nur an eine Variable möglich, nicht an ein Feld, einen Index oder einen anderen Ausdruck."),
            code: "M-S005",
            erwartet: Some("Variable vor '+=' bzw. '-='".to_string()),
            gefunden: Some(op.to_string()),
            vorschlag: Some(vorschlag),
            ..ParseError::bei(self.current())
        }
    }

    fn statement_ende(&self) -> Result<(), ParseError> {
        if matches!(self.current_type(), TokenType::Newline | TokenType::Eof | TokenType::Dedent) {
            return Ok(());
        }
        if self.pos > 0
            && matches!(self.tokens[self.pos - 1].token_type, TokenType::Newline | TokenType::Dedent)
        {
            return Ok(());
        }
        // `+=`/`-=` hinter einem Feld, Index oder anderem Ausdruck: zusammengesetzte
        // Zuweisung gibt es nur fuer Variablen (Ziel einmal ausgewertet).
        if matches!(self.current_type(), TokenType::PlusAssign | TokenType::MinusAssign) {
            return Err(self.verbund_fehler(None));
        }
        let gefunden = match token_description(self.current_type()) {
            "Token" => got_description(self.current_type()),
            text => text.to_string(),
        };
        // Entscheid #38228: Der Vorschlag nennt den richtigen Schreibweg.
        // Klammer hinter fertigem Ausdruck (z. B. `x[i](f())`) war bisher
        // still ein zweites Statement.
        let vorschlag = match self.current_type() {
            TokenType::LParen | TokenType::LBracket =>
                "Eine Klammer direkt hinter einem fertigen Ausdruck ist kein Aufruf. Aufruf: Funktionsname direkt vor '(' (f(x)); Wert aus Index oder Liste erst zuweisen (setze f auf x[i]) und dann f(...) aufrufen; ein eigener Ausdruck gehoert in eine eigene Zeile.",
            _ => "Ueberzaehlige Angabe entfernen, einen Operator ergaenzen oder sie in eine eigene Zeile schreiben.",
        };
        Err(ParseError {
            message: format!("Zeilenende erwartet, aber {gefunden} gefunden. Der Rest der Zeile gehoert zu keinem Ausdruck."),
            code: "M-S003",
            erwartet: Some("Zeilenende".to_string()),
            gefunden: Some(gefunden),
            vorschlag: Some(vorschlag.to_string()),
            ..ParseError::bei(self.current())
        })
    }

    // --- Hilfsfunktionen ---

    fn current(&self) -> &Token {
        &self.tokens[self.pos]
    }

    /// Nur die neue Typform enthaelt einen duennen Pfeil im Indexpaar.
    /// Gewoehnliches reg[0] oder reg[fn(x)] behaelt seine Indexbedeutung.
    fn registration_type_follows(&self) -> bool {
        let mut brackets = 0usize;
        for token in self.tokens.iter().skip(self.pos) {
            match &token.token_type {
                TokenType::LBracket => brackets += 1,
                TokenType::RBracket => {
                    if brackets == 0 { return false; }
                    brackets -= 1;
                    if brackets == 0 { return false; }
                }
                TokenType::ThinArrow if brackets > 0 => return true,
                TokenType::Newline | TokenType::Eof => return false,
                _ => {}
            }
        }
        false
    }

    fn current_type(&self) -> &TokenType {
        &self.tokens[self.pos].token_type
    }

    fn eat(&mut self, expected: &TokenType) -> Result<&Token, ParseError> {
        if std::mem::discriminant(self.current_type()) == std::mem::discriminant(expected) {
            let tok = &self.tokens[self.pos];
            self.pos += 1;
            Ok(tok)
        } else {
            let expected_desc = token_description(expected);
            let got_desc = got_description(self.current_type());
            let hint = match expected {
                TokenType::Indent => " Hast du die Einrückung (4 Leerzeichen) vergessen?",
                TokenType::Colon => " Nach Bedingungen und Funktionsköpfen muss ein ':' stehen.",
                TokenType::RParen => " Schließende Klammer ')' fehlt.",
                TokenType::RBracket => " Schließende eckige Klammer ']' fehlt.",
                TokenType::To => " Nach 'setze name' kommt 'auf' (z.B. 'setze x auf 5').",
                _ => "",
            };
            Err(ParseError {
                message: format!("{expected_desc} erwartet, aber {got_desc} gefunden.{hint}"),
                code: "M-S001",
                erwartet: Some(expected_desc.to_string()),
                gefunden: Some(got_desc.to_string()),
                vorschlag: Some(hint.trim()).filter(|h| !h.is_empty()).map(str::to_string),
                ..ParseError::bei(self.current())
            })
        }
    }

    fn eat_identifier(&mut self) -> Result<String, ParseError> {
        if let TokenType::Identifier(name) = self.current_type().clone() {
            self.pos += 1;
            Ok(name)
        } else if matches!(self.current_type(), TokenType::Data) {
            // Soft-Keyword: "daten" nur in "daten klasse ..." Kontext reserviert.
            self.pos += 1;
            Ok("daten".to_string())
        } else if matches!(self.current_type(), TokenType::Try) {
            // Soft-Keyword: "versuche" ist nur als Statement-Start (gefolgt von ':')
            // reserviert. Sonst als normaler Identifier erlaubt.
            self.pos += 1;
            Ok("versuche".to_string())
        } else if matches!(self.current_type(), TokenType::Test) {
            self.pos += 1;
            Ok("teste".to_string())
        } else if matches!(self.current_type(), TokenType::Match) {
            self.pos += 1;
            Ok("pruefe".to_string())
        } else if matches!(self.current_type(), TokenType::Expect) {
            self.pos += 1;
            Ok("erwarte".to_string())
        } else if matches!(self.current_type(), TokenType::Where) {
            self.pos += 1;
            Ok("wo".to_string())
        } else if matches!(self.current_type(), TokenType::Order) {
            self.pos += 1;
            Ok("sortiere".to_string())
        } else if matches!(self.current_type(), TokenType::Show) {
            self.pos += 1;
            Ok("zeige".to_string())
        } else {
            let got_desc = got_description(self.current_type());
            let hint = keyword_name_hint(self.current_type());
            Err(ParseError {
                message: format!("Name/Bezeichner erwartet, aber {got_desc} gefunden.{hint}"),
                code: "M-S002",
                erwartet: Some("Name/Bezeichner".to_string()),
                gefunden: Some(got_desc.to_string()),
                vorschlag: Some(hint.trim()).filter(|h| !h.is_empty()).map(str::to_string),
                ..ParseError::bei(self.current())
            })
        }
    }

    fn skip_newlines(&mut self) {
        while matches!(self.current_type(), TokenType::Newline) {
            self.pos += 1;
        }
    }

    fn parse_block_body(&mut self, top_level: bool) -> Result<Vec<Stmt>, ParseError> {
        self.mit_tiefe(|p| {
            if !top_level {
                p.glied()?;
            }
            p.parse_block_body_kern(top_level)
        })
    }

    fn parse_block_body_kern(&mut self, top_level: bool) -> Result<Vec<Stmt>, ParseError> {
        let mut stmts = Vec::new();

        if !top_level {
            self.eat(&TokenType::Colon)?;
            self.skip_newlines();
            self.eat(&TokenType::Indent)?;
        }

        loop {
            self.skip_newlines();
            match self.current_type() {
                TokenType::Eof => break,
                TokenType::Dedent if !top_level => {
                    self.pos += 1;
                    break;
                }
                TokenType::Dedent if top_level => {
                    // Defensiv: Unerwarteter Dedent im top-level (z.B. nach Parse-Error
                    // in nested Block). Skippen damit der Loop weiter Fortschritt macht.
                    self.pos += 1;
                }
                _ => {
                    match self.parse_statement() {
                        Ok(stmt) => {
                            stmts.push(stmt);
                            if let Err(err) = self.statement_ende() {
                                self.errors.push(err);
                                self.sync_to_next_statement(!top_level);
                            }
                        }
                        Err(err) => {
                            // Error Recovery (P2-T97 auch in Blöcken): Fehler sammeln,
                            // zum nächsten Statement springen; der Block bleibt offen.
                            self.errors.push(err);
                            self.sync_to_next_statement(!top_level);
                        }
                    }
                }
            }
        }

        Ok(stmts)
    }

    // --- Statements ---

    /// P2-T97 B5b: jedes Statement traegt die Zeile seines ersten Tokens.
    fn parse_statement(&mut self) -> Result<Stmt, ParseError> {
        let zeile = self.current().line;
        let mut stmt = self.parse_statement_kern()?;
        stmt.zeile_setzen_wenn_leer(zeile);
        Ok(stmt)
    }

    fn parse_statement_kern(&mut self) -> Result<Stmt, ParseError> {
        match self.current_type().clone() {
            // Soft-Keyword: `global name[, name]` nur wenn ein Name folgt;
            // `global` bleibt als normaler Variablenname nutzbar.
            TokenType::Identifier(kw) if kw == "global"
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Identifier(_))) => self.parse_global_decl(),
            TokenType::Identifier(kw) if (kw == "struktur" || kw == "struct")
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Identifier(_)))
                && matches!(self.tokens.get(self.pos + 2).map(|t| &t.token_type),
                            Some(TokenType::Colon | TokenType::Implements | TokenType::LBracket)) => self.parse_struct_def(vec![]),
            TokenType::Identifier(kw) if (kw == "aufzählung" || kw == "aufzaehlung")
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Identifier(_)))
                && matches!(self.tokens.get(self.pos + 2).map(|t| &t.token_type),
                            Some(TokenType::Colon)) => self.parse_aufzaehlung(),
            TokenType::Identifier(kw) if kw == "bitfeld"
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type), Some(TokenType::Identifier(_)))
                && matches!(self.tokens.get(self.pos + 2).map(|t| &t.token_type), Some(TokenType::Colon)) =>
                    self.parse_bitfeld(vec![]),
            TokenType::Identifier(kw) if kw == "extern"
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::String(_)))
                && matches!(self.tokens.get(self.pos + 2).map(|t| &t.token_type),
                            Some(TokenType::Func | TokenType::Const)) => self.parse_extern_c(vec![]),
            // P2-T114 Phase C: Soft-Keyword `geteilt` (P2-T119: EN `shared`) nur
            // direkt vor `konstante`/`setze`; sonst bleibt es ein normaler Name.
            TokenType::Identifier(kw) if (kw == "geteilt" || kw == "shared")
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Const) | Some(TokenType::Set)) => self.parse_geteilt(),
            // P2-T114 Phase B: Soft-Keyword `mit SPERRE als NAME:` (EN `with ... as`),
            // nur wenn auf derselben Zeile `als NAME :` folgt.
            TokenType::Identifier(kw) if (kw == "mit" || kw == "with") && self.mit_block_folgt() => self.parse_mit(),
            TokenType::Set => self.parse_assignment(),
            TokenType::Const => self.parse_const_assignment(),
            TokenType::Show => self.parse_show(),
            TokenType::If => self.parse_if(),
            TokenType::While => self.parse_while(),
            TokenType::For => self.parse_for(),
            TokenType::Parallel => self.parse_parallel_for(),
            TokenType::Precondition => self.parse_precondition(),
            TokenType::Postcondition => self.parse_postcondition(),
            TokenType::At => self.parse_decorated_function(),
            TokenType::Func => self.parse_function_def_with_decorators(vec![]),
            TokenType::Data => {
                // Soft-Keyword: nur "daten klasse ..." → parse_data_class.
                // Sonst wie ein normaler Identifier behandeln (auch fuer
                // Index-/Property-Assignment, nicht nur compound).
                if matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Class)) {
                    self.parse_data_class()
                } else {
                    let expr = self.parse_expression()?;
                    // Property-Zuweisung: daten.prop = wert
                    if let Expr::PropertyAccess { object, property } = &expr {
                        if matches!(self.current_type(), TokenType::Assign) {
                            self.pos += 1;
                            let value = self.parse_expression()?;
                            return Ok(Stmt::PropertyAssignment {
                                zeile: 0,
                                object: *object.clone(),
                                property: property.clone(),
                                value,
                            });
                        }
                    }
                    // Index-Zuweisung: daten[i] = wert
                    if let Expr::IndexAccess { object, index } = &expr {
                        if matches!(self.current_type(), TokenType::Assign) {
                            self.pos += 1;
                            let value = self.parse_expression()?;
                            return Ok(Stmt::IndexAssignment {
                                zeile: 0,
                                object: *object.clone(),
                                index: *index.clone(),
                                value,
                            });
                        }
                    }
                    if let Expr::Identifier(name) = &expr {
                        match self.current_type() {
                            TokenType::PlusAssign => {
                                self.pos += 1;
                                let value = self.parse_expression()?;
                                return Ok(Stmt::CompoundAssignment {
                                    zeile: 0, name: name.clone(), op: "+=".to_string(), value,
                                });
                            }
                            TokenType::MinusAssign => {
                                self.pos += 1;
                                let value = self.parse_expression()?;
                                return Ok(Stmt::CompoundAssignment {
                                    zeile: 0, name: name.clone(), op: "-=".to_string(), value,
                                });
                            }
                            _ => {}
                        }
                    }
                    if matches!(self.current_type(), TokenType::PlusAssign | TokenType::MinusAssign) {
                        return Err(self.verbund_fehler(Some(&expr)));
                    }
                    Ok(Stmt::Expression(expr, 0))
                }
            }
            TokenType::Return => self.parse_return(),
            TokenType::Break => { self.pos += 1; Ok(Stmt::Break(0)) }
            TokenType::Continue => { self.pos += 1; Ok(Stmt::Continue(0)) }
            TokenType::Class => self.parse_class_def(),
            TokenType::Interface => self.parse_interface_def(),
            TokenType::Try => {
                // Soft-Keyword: 'versuche' als Block nur wenn direkt ':' folgt.
                // Sonst als Identifier-Expression (z.B. setze versuche auf 5).
                if matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Colon)) {
                    self.parse_try_catch()
                } else {
                    let expr = self.parse_expression()?;
                    Ok(Stmt::Expression(expr, 0))
                }
            }
            TokenType::Throw => self.parse_throw(),
            TokenType::Import => self.parse_import(),
            TokenType::From => self.parse_from_import(),
            TokenType::Export => self.parse_export(),
            TokenType::Match => {
                // Soft-Keyword: 'pruefe' als match-block nur wenn direkt ein
                // Expression-Start folgt. Sonst als Identifier.
                if self.is_expression_start_at(self.pos + 1) {
                    self.parse_match()
                } else {
                    let expr = self.parse_expression()?;
                    Ok(Stmt::Expression(expr, 0))
                }
            }
            TokenType::Defer => self.parse_defer(),
            TokenType::Guard => self.parse_guard(),
            TokenType::Unsafe => {
                self.pos += 1;
                let body = self.parse_block_body(false)?;
                Ok(Stmt::UnsafeBlock { zeile: 0, body })
            }
            TokenType::Test if matches!(
                self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                Some(TokenType::String(_))
            ) => {
                self.pos += 1;
                // teste "name":
                let name = if let TokenType::String(s) = self.current_type().clone() {
                    self.pos += 1;
                    s
                } else {
                    self.eat_identifier()?
                };
                let body = self.parse_block_body(false)?;
                Ok(Stmt::TestDef { zeile: 0, name, body })
            }
            TokenType::Expect => {
                // Soft-Keyword: 'erwarte' als Test-Assertion nur wenn das
                // direkt folgende Token KEIN '(' ist. Mit '(' ist es ein
                // Funktionsaufruf 'erwarte(...)' (User-Funktion mit Namen
                // 'erwarte') und faellt durch zum default arm.
                if matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::LParen)) {
                    let expr = self.parse_expression()?;
                    Ok(Stmt::Expression(expr, 0))
                } else {
                    self.pos += 1;
                    let expr = self.parse_expression()?;
                    Ok(Stmt::Expect(expr, 0))
                }
            }
            _ => {
                let expr = self.parse_expression()?;

                // Property-Zuweisung
                if let Expr::PropertyAccess { object, property } = &expr {
                    if matches!(self.current_type(), TokenType::Assign) {
                        self.pos += 1;
                        let value = self.parse_expression()?;
                        return Ok(Stmt::PropertyAssignment {
                            zeile: 0,
                            object: *object.clone(),
                            property: property.clone(),
                            value,
                        });
                    }
                }

                // Index-Zuweisung
                if let Expr::IndexAccess { object, index } = &expr {
                    if matches!(self.current_type(), TokenType::Assign) {
                        self.pos += 1;
                        let value = self.parse_expression()?;
                        return Ok(Stmt::IndexAssignment {
                            zeile: 0,
                            object: *object.clone(),
                            index: *index.clone(),
                            value,
                        });
                    }
                }

                // Compound Assignment
                if let Expr::Identifier(name) = &expr {
                    match self.current_type() {
                        TokenType::PlusAssign => {
                            self.pos += 1;
                            let value = self.parse_expression()?;
                            return Ok(Stmt::CompoundAssignment {
                                zeile: 0,
                                name: name.clone(),
                                op: "+=".to_string(),
                                value,
                            });
                        }
                        TokenType::MinusAssign => {
                            self.pos += 1;
                            let value = self.parse_expression()?;
                            return Ok(Stmt::CompoundAssignment {
                                zeile: 0,
                                name: name.clone(),
                                op: "-=".to_string(),
                                value,
                            });
                        }
                        _ => {}
                    }
                }
                if matches!(self.current_type(), TokenType::PlusAssign | TokenType::MinusAssign) {
                    return Err(self.verbund_fehler(Some(&expr)));
                }

                Ok(Stmt::Expression(expr, 0))
            }
        }
    }


    fn parse_assignment(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // set/setze

        // Spezialfall: 'setze selbst.prop auf wert' und 'setze a[i] auf wert'
        // sind Property- bzw. Index-Zuweisungen auf Receiver-Expressions.
        // Parse die LHS als Expression und pruefe ob sie zuweisbar ist.
        if matches!(self.current_type(), TokenType::This)
            || matches!(self.current_type(), TokenType::LParen)
        {
            let lhs = self.parse_unary()?;
            self.eat(&TokenType::To)?;
            let value = self.parse_expression()?;
            return match lhs {
                Expr::PropertyAccess { object, property } => Ok(Stmt::PropertyAssignment {
                    zeile: 0, object: *object,
                    property,
                    value,
                }),
                Expr::IndexAccess { object, index } => Ok(Stmt::IndexAssignment {
                    zeile: 0, object: *object,
                    index: *index,
                    value,
                }),
                _ => Err(ParseError {
                    message: "Nach 'setze' wird ein Name, 'selbst.X', 'selbst[i]' oder 'obj[i]' erwartet.".to_string(),
                    ..ParseError::bei(self.current())
                }),
            };
        }

        let name = self.eat_identifier()?;

        // Zweiter Spezialfall: 'setze name.prop auf wert' oder 'setze name[i] auf wert'
        // Wenn nach dem Identifier Dot oder LBracket folgt, wird die LHS zu einem
        // Property-/Index-Target.
        if matches!(self.current_type(), TokenType::Dot | TokenType::LBracket) {
            // Baue die LHS aus Identifier + trailing Dots/Indices
            let mut lhs = Expr::Identifier(name);
            loop {
                match self.current_type() {
                    TokenType::Dot => {
                        self.pos += 1;
                        let prop = self.eat_identifier()?;
                        lhs = Expr::PropertyAccess {
                            object: Box::new(lhs),
                            property: prop,
                        };
                    }
                    TokenType::LBracket => {
                        self.pos += 1;
                        let idx = self.parse_expression()?;
                        self.eat(&TokenType::RBracket)?;
                        lhs = Expr::IndexAccess {
                            object: Box::new(lhs),
                            index: Box::new(idx),
                        };
                    }
                    _ => break,
                }
            }
            self.eat(&TokenType::To)?;
            let value = self.parse_expression()?;
            return match lhs {
                Expr::PropertyAccess { object, property } => Ok(Stmt::PropertyAssignment {
                    zeile: 0, object: *object, property, value,
                }),
                Expr::IndexAccess { object, index } => Ok(Stmt::IndexAssignment {
                    zeile: 0, object: *object, index: *index, value,
                }),
                _ => unreachable!(),
            };
        }
        // Optionale Typ-Annotation: setze name: Typ auf wert
        let ty = self.parse_optional_annotation()?;
        self.eat(&TokenType::To)?;
        let value = self.parse_expression()?;
        Ok(Stmt::Assignment { zeile: 0, name, ty, value })
    }

    fn parse_const_assignment(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // const/konstante
        let name = self.eat_identifier()?;
        // Optionale Typ-Annotation
        let ty = self.parse_optional_annotation()?;
        self.eat(&TokenType::To)?;
        let value = self.parse_expression()?;
        Ok(Stmt::ConstAssignment { zeile: 0, name, ty, value, geteilt: false })
    }

    /// P2-T114 Phase C: `geteilt konstante NAME auf wert` oder
    /// `geteilt setze NAME auf wert`. Ergebnis ist eine Konstante mit
    /// Markierung geteilt (tief eingefroren, von Threads lesbar).
    fn parse_geteilt(&mut self) -> Result<Stmt, ParseError> {
        let ort = ParseError::bei(self.current());
        self.pos += 1; // geteilt / shared
        let stmt = if matches!(self.current_type(), TokenType::Const) {
            self.parse_const_assignment()?
        } else {
            self.parse_assignment()?
        };
        match stmt {
            Stmt::Assignment { zeile, name, ty, value }
            | Stmt::ConstAssignment { zeile, name, ty, value, .. } =>
                Ok(Stmt::ConstAssignment { zeile, name, ty, value, geteilt: true }),
            _ => Err(ParseError {
                message: "'geteilt'/'shared' markiert nur eine Globale: geteilt konstante NAME auf wert".to_string(),
                ..ort
            }),
        }
    }

    /// P2-T114 B: `mit ... als NAME:` auf einer Zeile (Klammertiefe 0).
    fn mit_block_folgt(&self) -> bool {
        let mut tiefe = 0i32;
        let mut i = self.pos + 1;
        while let Some(t) = self.tokens.get(i) {
            match &t.token_type {
                TokenType::Newline | TokenType::Eof => return false,
                TokenType::LParen | TokenType::LBracket | TokenType::LBrace => tiefe += 1,
                TokenType::RParen | TokenType::RBracket | TokenType::RBrace => tiefe -= 1,
                TokenType::Colon if tiefe == 0 => return false,
                TokenType::As if tiefe == 0 => {
                    return i > self.pos + 1
                        && matches!(self.tokens.get(i + 1).map(|t| &t.token_type), Some(TokenType::Identifier(_)))
                        && matches!(self.tokens.get(i + 2).map(|t| &t.token_type), Some(TokenType::Colon));
                }
                _ => {}
            }
            i += 1;
        }
        false
    }

    /// P2-T114 B: `mit s als k: rumpf` sperrt s, bindet den Inhalt an k und
    /// gibt die Sperre am Blockende frei, auch wenn der Rumpf wirft. Entzuckert
    /// zu versuche/fange mit erneutem wirf; Spruenge aus dem Block (gib_zurueck,
    /// abbrechen, weiter) sind ein Compilefehler, damit nie eine Sperre leckt.
    fn parse_mit(&mut self) -> Result<Stmt, ParseError> {
        let ort = ParseError::bei(self.current());
        let zeile = self.current().line;
        self.pos += 1; // mit
        let sperre = self.parse_expression()?;
        self.eat(&TokenType::As)?;
        let name = self.eat_identifier()?;
        let body = self.parse_block_body(false)?;
        if let Some(was) = mit_sprung(&body, false) {
            return Err(ParseError {
                message: format!("'{was}' verlaesst den mit-Block; das ist nicht erlaubt, weil die Sperre sonst nicht freigegeben wuerde. Ergebnis in einer Variablen merken und nach dem Block verwenden."),
                ..ort
            });
        }
        let s = format!("__mit_sperre_{}", self.pos);
        let f = format!("__mit_fehler_{}", self.pos);
        let id = |n: &str| Expr::Identifier(n.to_string());
        let setze = |n: &str, value: Expr| Stmt::Assignment { zeile, name: n.to_string(), ty: None, value };
        let verlassen = |pruefen: bool| Stmt::Expression(Expr::FunctionCall {
            type_args: Vec::new(),
            name: "__moo_sperre_verlassen".to_string(),
            args: vec![id(&s), Expr::Boolean(pruefen)], zeile, spalten: (0, 0) }, zeile);
        let stmts = vec![
            setze(&s, sperre),
            setze(&name, Expr::FunctionCall { type_args: Vec::new(), name: "__moo_sperre_betreten".to_string(), args: vec![id(&s)], zeile, spalten: (0, 0) }),
            Stmt::TryCatch {
                zeile,
                try_body: body,
                catch_var: Some(f.clone()),
                catch_body: vec![setze(&name, Expr::None), verlassen(false), Stmt::Throw(id(&f), zeile)],
            },
            setze(&name, Expr::None),
            verlassen(true),
            setze(&s, Expr::None),
        ];
        Ok(Stmt::If { zeile, condition: Expr::Boolean(true), body: stmts, else_body: Vec::new() })
    }

    fn parse_show(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let value = self.parse_expression()?;
        Ok(Stmt::Show(value, 0))
    }

    fn parse_if(&mut self) -> Result<Stmt, ParseError> {
        // Eigene Zeile: `sonst wenn` ruft parse_if direkt, nicht parse_statement.
        let zeile = self.current().line;
        self.pos += 1; // if/wenn
        let condition = self.parse_expression()?;
        let body = self.parse_block_body(false)?;

        let mut else_body = Vec::new();
        self.skip_newlines();
        if matches!(self.current_type(), TokenType::Else) {
            self.pos += 1;
            if matches!(self.current_type(), TokenType::If) {
                else_body = vec![self.parse_if()?];
            } else {
                else_body = self.parse_block_body(false)?;
            }
        }

        Ok(Stmt::If { zeile, condition, body, else_body })
    }

    fn parse_while(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let condition = self.parse_expression()?;
        let body = self.parse_block_body(false)?;
        Ok(Stmt::While { zeile: 0, condition, body })
    }

    fn parse_for(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let var_name = self.eat_identifier()?;
        self.eat(&TokenType::In)?;
        let iterable = self.parse_expression()?;
        let body = self.parse_block_body(false)?;
        Ok(Stmt::For { zeile: 0, var_name, iterable, body })
    }

    fn parse_parallel_for(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // parallel
        self.eat(&TokenType::For)?; // für/for
        let var_name = self.eat_identifier()?;
        self.eat(&TokenType::In)?;
        let iterable = self.parse_expression()?;
        let body = self.parse_block_body(false)?;
        Ok(Stmt::ParallelFor { zeile: 0, var_name, iterable, body })
    }

    fn parse_precondition(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // vorbedingung/precondition
        let condition = self.parse_expression()?;
        let message = if matches!(self.current_type(), TokenType::Comma) {
            self.pos += 1;
            if let TokenType::String(s) = self.current_type().clone() {
                self.pos += 1;
                s
            } else {
                "Vorbedingung verletzt".to_string()
            }
        } else {
            "Vorbedingung verletzt".to_string()
        };
        Ok(Stmt::Precondition { zeile: 0, condition, message })
    }

    fn parse_postcondition(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // nachbedingung/postcondition
        let condition = self.parse_expression()?;
        let message = if matches!(self.current_type(), TokenType::Comma) {
            self.pos += 1;
            if let TokenType::String(s) = self.current_type().clone() {
                self.pos += 1;
                s
            } else {
                "Nachbedingung verletzt".to_string()
            }
        } else {
            "Nachbedingung verletzt".to_string()
        };
        Ok(Stmt::Postcondition { zeile: 0, condition, message })
    }

    fn parse_decorated_function(&mut self) -> Result<Stmt, ParseError> {
        self.parse_decorated_function_native(false)
    }

    fn parse_decorated_function_native(&mut self, native_default: bool) -> Result<Stmt, ParseError> {
        let mut decorators = Vec::new();
        while matches!(self.current_type(), TokenType::At) {
            self.pos += 1; // @
            let name = self.eat_identifier()?;
            // P011-A1: optionale Literal-Argumente — z.B. @ausrichten(16), @sektion(".boot")
            let mut args = Vec::new();
            if matches!(self.current_type(), TokenType::LParen) {
                self.pos += 1; // (
                while !matches!(self.current_type(), TokenType::RParen) {
                    let tt = self.current_type().clone();
                    match tt {
                        TokenType::Identifier(key)
                            if (name == "bibliothek" || name == "library")
                                && self.tokens.get(self.pos + 1).is_some_and(|t| matches!(t.token_type, TokenType::Assign)) =>
                        {
                            self.pos += 2; // Name und =
                            let value = match self.current_type().clone() {
                                TokenType::String(value) => { self.pos += 1; value }
                                _ => return Err(ParseError {
                                    message: "Benanntes Bibliotheksargument erwartet ein Text-Literal".to_string(),
                                    ..ParseError::bei(self.current())
                                }),
                            };
                            args.push(crate::ast::DecoratorArg::NamedText { name: key, value });
                        }
                        TokenType::Number(n) => {
                            args.push(crate::ast::DecoratorArg::Zahl(n));
                            self.pos += 1;
                        }
                        TokenType::String(s) => {
                            args.push(crate::ast::DecoratorArg::Text(s));
                            self.pos += 1;
                        }
                        _ => {
                            return Err(ParseError {
                                message: "Decorator-Argument muss Zahl- oder Text-Literal sein".to_string(),
                                ..ParseError::bei(self.current())
                            });
                        }
                    }
                    if matches!(self.current_type(), TokenType::Comma) {
                        self.pos += 1;
                    } else {
                        break;
                    }
                }
                self.eat(&TokenType::RParen)?;
            }
            decorators.push(crate::ast::Decorator { name, args });
            self.skip_newlines();
        }
        if matches!(self.current_type(), TokenType::Identifier(name) if name == "struktur" || name == "struct") {
            return self.parse_struct_def(decorators);
        }
        if matches!(self.current_type(), TokenType::Identifier(name) if name == "bitfeld") {
            return self.parse_bitfeld(decorators);
        }
        // P011-A3: @rohdaten_u32/@rawdata_u32 + konstante -> reines Daten-Global
        if matches!(self.current_type(), TokenType::Const)
            && decorators.iter().any(|d| d.name == "rohdaten_u32" || d.name == "rawdata_u32")
        {
            self.pos += 1; // konstante/const
            let name = self.eat_identifier()?;
            let ty = self.parse_optional_annotation()?;
            self.eat(&TokenType::To)?;
            let value = self.parse_expression()?;
            return Ok(Stmt::RawDataU32 { zeile: 0, name, ty, value, decorators });
        }
        // P7-T4 (1A): @nativ konstante NAME: T = wert (Literale exakt wie in @nativ).
        if matches!(self.current_type(), TokenType::Const)
            && decorators.iter().any(|d| d.name == "nativ" || d.name == "native")
        {
            self.pos += 1; // konstante/const
            let name = self.eat_identifier()?;
            let line = self.current().line;
            let column = self.current().column;
            // Typ (Arraylängen) und Wert als exakte Rohlexeme wie in @nativ.
            let previous_native = self.native_context;
            self.native_context = true;
            let parsed = (|| {
                let ty = self.parse_optional_annotation()?.ok_or_else(|| ParseError {
                    message: "Native Konstante braucht eine Typannotation (konstante name: Typ auf wert)".to_string(),
                    ..ParseError::an(line, column)
                })?;
                self.eat(&TokenType::To)?;
                Ok((ty, self.parse_expression()?))
            })();
            self.native_context = previous_native;
            let (ty, value) = parsed?;
            return Ok(Stmt::NativeConst { zeile: 0, name, ty, value: Some(value), decorators });
        }
        // P7-T6: @nativ statisch NAME: T auf wert — beschreibbarer statischer
        // Speicher (.data/.bss) fuer Kernel-Stack, GDT, Seitentabellen, Puffer.
        // Gleiche Form wie die native Konstante; das Merkmal traegt ein
        // internes Decorator "statisch" (Auswertung in native_const.rs).
        if matches!(self.current_type(), TokenType::Identifier(name) if name == "statisch" || name == "static")
            && decorators.iter().any(|d| matches!(d.name.as_str(), "nativ" | "native" | "pro_kern"))
            && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type), Some(TokenType::Identifier(_)))
        {
            self.pos += 1; // statisch/static
            let name = self.eat_identifier()?;
            let line = self.current().line;
            let column = self.current().column;
            let previous_native = self.native_context;
            self.native_context = true;
            let parsed = (|| {
                let ty = self.parse_optional_annotation()?.ok_or_else(|| ParseError {
                    message: "Statischer Speicher braucht eine Typannotation (statisch name: Typ auf wert)".to_string(),
                    ..ParseError::an(line, column)
                })?;
                self.eat(&TokenType::To)?;
                Ok((ty, self.parse_expression()?))
            })();
            self.native_context = previous_native;
            let (ty, value) = parsed?;
            let mut decorators = decorators;
            decorators.push(crate::ast::Decorator { name: "statisch".to_string(), args: Vec::new() });
            return Ok(Stmt::NativeConst { zeile: 0, name, ty, value: Some(value), decorators });
        }
        if matches!(self.current_type(), TokenType::Identifier(name) if name == "extern") {
            return self.parse_extern_c(decorators);
        }
        if !matches!(self.current_type(), TokenType::Func) {
            return Err(ParseError {
                message: "funktion/func nach Decorator erwartet".to_string(),
                ..ParseError::bei(self.current())
            });
        }
        if native_default && !decorators.iter().any(|d| matches!(d.name.as_str(), "nativ" | "native")) {
            decorators.push(crate::ast::Decorator { name: "nativ".into(), args: Vec::new() });
        }
        self.parse_function_def_with_decorators(decorators)
    }

    fn parse_function_def_with_decorators(&mut self, decorators: Vec<crate::ast::Decorator>) -> Result<Stmt, ParseError> {
        let previous_native = self.native_context;
        self.native_context = decorators.iter().any(|d| d.name == "nativ" || d.name == "native");
        let result = (|| {
            self.pos += 1;
            let name = self.eat_identifier()?;
            self.eat(&TokenType::LParen)?;
            let mut params = Vec::new();
            let mut param_types = Vec::new();
            let mut defaults = Vec::new();
            if !matches!(self.current_type(), TokenType::RParen) {
                self.parse_param(&mut params, &mut param_types, &mut defaults)?;
                while matches!(self.current_type(), TokenType::Comma) {
                    self.pos += 1;
                    self.parse_param(&mut params, &mut param_types, &mut defaults)?;
                }
            }
            self.eat(&TokenType::RParen)?;
            let return_type = if matches!(self.current_type(), TokenType::ThinArrow) {
                self.pos += 1;
                if matches!(self.current_type(), TokenType::Identifier(_) | TokenType::LBracket) {
                    Some(self.parse_type()?)
                } else {
                    // Legacy: ein alleinstehender Pfeil wurde bisher übersprungen.
                    Option::None
                }
            } else {
                Option::None
            };
            let body = self.parse_block_body(false)?;
            Ok(Stmt::FunctionDef { zeile: 0, name, params, param_types, return_type, defaults, body, decorators })
        })();
        // Auch bei Parserfehler und bei verschachtelten Funktionen restaurieren.
        self.native_context = previous_native;
        result
    }

    fn parse_bitfeld(&mut self, decorators: Vec<crate::ast::Decorator>) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let name = self.eat_identifier()?;
        self.eat(&TokenType::Colon)?;
        let basis = self.parse_type()?;
        if !matches!(&basis.kind, crate::ast::TypeKind::Name(n) if matches!(n.as_str(), "u8" | "u16" | "u32" | "u64")) {
            return Err(ParseError { message: "bitfeld braucht u8, u16, u32 oder u64 als Basis".into(),
                ..ParseError::bei(self.current()) });
        }
        self.eat(&TokenType::Colon)?;
        self.skip_newlines();
        self.eat(&TokenType::Indent)?;
        let mut bitfelder = Vec::new();
        loop {
            self.skip_newlines();
            if matches!(self.current_type(), TokenType::Dedent) { self.pos += 1; break; }
            let zeile = self.current().line;
            let feld = self.eat_identifier()?;
            self.eat(&TokenType::Colon)?;
            let anfang = self.parse_bitposition()?;
            let ende = if matches!(self.current_type(), TokenType::Range) {
                self.pos += 1;
                self.parse_bitposition()?
            } else { anfang };
            if ende < anfang {
                return Err(ParseError { message: format!("Bitbereich {feld}: Ende liegt vor Anfang"),
                    ..ParseError::bei(self.current()) });
            }
            bitfelder.push(crate::ast::BitfeldBereich { name: feld, anfang, breite: ende - anfang + 1, zeile });
            if !matches!(self.current_type(), TokenType::Newline | TokenType::Dedent | TokenType::Eof) {
                return Err(ParseError { message: "Zeilenende nach Bitbereich erwartet".into(),
                    ..ParseError::bei(self.current()) });
            }
        }
        if bitfelder.is_empty() {
            return Err(ParseError { message: "bitfeld braucht mindestens einen benannten Bereich".into(),
                ..ParseError::bei(self.current()) });
        }
        Ok(Stmt::StructDef { zeile: 0, name, fields: vec![("__bits".into(), basis)], decorators, bitfelder, varianten: Vec::new(), typparameter: Vec::new(), interfaces: Vec::new(), methods: Vec::new() })
    }

    fn parse_bitposition(&mut self) -> Result<u32, ParseError> {
        match self.current_type() {
            TokenType::Number(n) if n.is_finite() && *n >= 0.0 && *n <= 63.0 && n.fract() == 0.0 => {
                let value = *n as u32;
                self.pos += 1;
                Ok(value)
            }
            _ => Err(ParseError { message: "Bitposition muss eine ganze Zahl von 0 bis 63 sein".into(),
                ..ParseError::bei(self.current()) }),
        }
    }

    fn parse_aufzaehlung(&mut self) -> Result<Stmt, ParseError> {
        let deklaration = self.current().clone();
        self.pos += 1;
        let name = self.eat_identifier()?;
        self.eat(&TokenType::Colon)?;
        self.skip_newlines();
        self.eat(&TokenType::Indent)?;
        let vorher = self.native_context;
        self.native_context = true;
        let ergebnis = (|| {
            let mut fields = vec![("aufzaehlung.tag".into(), crate::ast::TypeRef {
                kind: crate::ast::TypeKind::Name("u32".into()),
                line: deklaration.line, column: deklaration.column,
                end_line: deklaration.line, end_column: deklaration.column,
            })];
            let mut varianten = Vec::new();
            let mut namen = std::collections::HashSet::new();
            loop {
                self.skip_newlines();
                if matches!(self.current_type(), TokenType::Dedent) {
                    self.pos += 1;
                    break;
                }
                self.eat(&TokenType::Case)?;
                let stelle = self.current().clone();
                let variante = self.eat_identifier()?;
                if variante == "_" || !namen.insert(variante.clone()) {
                    return Err(ParseError {
                        message: format!("Ungültiger oder doppelter Variantenname '{variante}'"),
                        ..ParseError::bei(&stelle)
                    });
                }
                let erster_nutzwert = fields.len();
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    let mut nutznamen = std::collections::HashSet::new();
                    if !matches!(self.current_type(), TokenType::RParen) {
                        loop {
                            let stelle = self.current().clone();
                            let feld = self.eat_identifier()?;
                            if feld == "_" || !nutznamen.insert(feld.clone()) {
                                return Err(ParseError {
                                    message: format!("Ungültiger oder doppelter Nutzfeldname '{feld}'"),
                                    ..ParseError::bei(&stelle)
                                });
                            }
                            self.eat(&TokenType::Colon)?;
                            fields.push((format!("{variante}.{feld}"), self.parse_type()?));
                            if !matches!(self.current_type(), TokenType::Comma) { break; }
                            self.pos += 1;
                        }
                    }
                    self.eat(&TokenType::RParen)?;
                }
                if !matches!(self.current_type(), TokenType::Newline | TokenType::Dedent) {
                    return Err(ParseError {
                        message: "Zeilenende nach der Variante erwartet".into(),
                        ..ParseError::bei(self.current())
                    });
                }
                varianten.push(crate::ast::AufzaehlungsVariante {
                    name: variante, erster_nutzwert, nutzwerte: fields.len() - erster_nutzwert,
                });
            }
            if varianten.is_empty() {
                return Err(ParseError {
                    message: format!("Aufzählung '{name}' braucht mindestens eine Variante"),
                    ..ParseError::bei(&deklaration)
                });
            }
            Ok(Stmt::StructDef {
                zeile: 0, name, fields, decorators: Vec::new(), bitfelder: Vec::new(),
                varianten, typparameter: Vec::new(), interfaces: Vec::new(), methods: Vec::new(),
            })
        })();
        self.native_context = vorher;
        ergebnis
    }

    fn parse_struct_def(&mut self, decorators: Vec<crate::ast::Decorator>) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let name = self.eat_identifier()?;
        let mut typparameter = Vec::new();
        if matches!(self.current_type(), TokenType::LBracket) {
            self.pos += 1;
            loop {
                let ort = self.current().clone();
                let parameter = self.eat_identifier()?;
                if parameter == "_" || parameter == "Self" || typparameter.contains(&parameter) {
                    return Err(ParseError {
                        message: format!("Ungültiger oder doppelter Typparameter '{parameter}'"),
                        ..ParseError::bei(&ort)
                    });
                }
                typparameter.push(parameter);
                if !matches!(self.current_type(), TokenType::Comma) { break; }
                self.pos += 1;
            }
            self.eat(&TokenType::RBracket)?;
        }
        let mut interfaces = Vec::new();
        if matches!(self.current_type(), TokenType::Implements) {
            self.pos += 1;
            interfaces.push(self.eat_identifier()?);
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                interfaces.push(self.eat_identifier()?);
            }
        }
        if decorators.iter().any(|d| d.name == "opak" || d.name == "opaque")
            && matches!(self.current_type(), TokenType::Newline | TokenType::Eof)
        {
            return Ok(Stmt::StructDef { zeile: 0, name, fields: Vec::new(), decorators,
                bitfelder: Vec::new(), varianten: Vec::new(), typparameter, interfaces, methods: Vec::new() });
        }
        self.eat(&TokenType::Colon)?;
        self.skip_newlines();
        self.eat(&TokenType::Indent)?;
        let previous_native = self.native_context;
        self.native_context = true;
        let result = (|| {
            let mut fields = Vec::new();
            let mut methods = Vec::new();
            loop {
                self.skip_newlines();
                match self.current_type() {
                    TokenType::Dedent => { self.pos += 1; break; }
                    TokenType::Eof => return Err(ParseError {
                        message: "Strukturblock nicht abgeschlossen".to_string(),
                        ..ParseError::bei(self.current())
                    }),
                    TokenType::Func | TokenType::At => {
                        let line = self.current().line;
                        let mut method = if matches!(self.current_type(), TokenType::At) {
                            self.parse_decorated_function_native(true)?
                        } else {
                            self.parse_function_def_with_decorators(vec![crate::ast::Decorator {
                                name: "nativ".into(), args: Vec::new(),
                            }])?
                        };
                        match &mut method {
                            Stmt::FunctionDef { zeile, .. } => *zeile = line,
                            _ => return Err(ParseError {
                                message: "Im Strukturblock sind Felder und native Methoden erlaubt".into(),
                                ..ParseError::an(line, 1)
                            }),
                        }
                        methods.push(method);
                    }
                    _ => {
                        let field = self.eat_identifier()?;
                        self.eat(&TokenType::Colon)?;
                        let ty = self.parse_type()?;
                        if !matches!(self.current_type(), TokenType::Newline | TokenType::Dedent | TokenType::Eof) {
                            return Err(ParseError {
                                message: "Zeilenende nach Strukturfeld erwartet".to_string(),
                                ..ParseError::bei(self.current())
                            });
                        }
                        fields.push((field, ty));
                    }
                }
            }
            Ok(Stmt::StructDef { zeile: 0, name, fields, decorators, bitfelder: Vec::new(), varianten: Vec::new(), typparameter, interfaces, methods })
        })();
        self.native_context = previous_native;
        result
    }

    fn parse_data_class(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // data/daten
        // Erwarte "klasse"/"class"
        if !matches!(self.current_type(), TokenType::Class) {
            return Err(ParseError {
                message: "klasse/class nach daten/data erwartet".to_string(),
                ..ParseError::bei(self.current())
            });
        }
        self.pos += 1; // class/klasse
        let name = self.eat_identifier()?;
        self.eat(&TokenType::LParen)?;
        let mut fields = Vec::new();
        let mut field_types = Vec::new();
        if !matches!(self.current_type(), TokenType::RParen) {
            loop {
                fields.push(self.eat_identifier()?);
                let ty = if matches!(self.current_type(), TokenType::Colon) {
                    self.pos += 1;
                    Some(self.parse_type()?)
                } else {
                    Option::None
                };
                field_types.push(ty);
                if !matches!(self.current_type(), TokenType::Comma) { break; }
                self.pos += 1;
            }
        }
        self.eat(&TokenType::RParen)?;
        Ok(Stmt::DataClassDef { zeile: 0, name, fields, field_types })
    }

    /// Rekursive Typgrammatik, ausschließlich in explizitem Typkontext.
    fn parse_type(&mut self) -> Result<crate::ast::TypeRef, ParseError> {
        self.mit_tiefe(|p| {
            p.glied()?;
            p.parse_type_kern()
        })
    }

    fn parse_type_kern(&mut self) -> Result<crate::ast::TypeRef, ParseError> {
        use crate::ast::{TypeKind, TypeRef};

        let start = self.current().clone();
        let (kind, end_line, end_column) = if matches!(self.current_type(), TokenType::LBracket) {
            self.pos += 1;
            let element = self.parse_type()?;
            self.eat(&TokenType::Semicolon)?;
            let length = self.parse_expression()?;
            let end = self.eat(&TokenType::RBracket)?.clone();
            (TypeKind::Array { element: Box::new(element), length: Box::new(length) },
             end.line, end.column + 1)
        } else {
            let name = match self.current_type().clone() {
                TokenType::Identifier(name) => { self.pos += 1; name }
                _ => return Err(ParseError {
                    message: "Typname erwartet".to_string(),
                    ..ParseError::bei(self.current())
                }),
            };
            if name == "dyn" && matches!(self.current_type(), TokenType::Identifier(_)) {
                let target = self.parse_type()?;
                let end = (target.end_line, target.end_column);
                (TypeKind::Generic { name, args: vec![target] }, end.0, end.1)
            } else if name == "fn" && matches!(self.current_type(), TokenType::LParen | TokenType::LBracket) {
                // P7-T4 (7A): optionale Aufrufkonvention `fn[win64](...) -> T`.
                let convention = if matches!(self.current_type(), TokenType::LBracket) {
                    self.pos += 1;
                    let convention = self.eat_identifier()?;
                    self.eat(&TokenType::RBracket)?;
                    Some(convention)
                } else {
                    Option::None
                };
                self.eat(&TokenType::LParen)?;
                let mut params = Vec::new();
                if !matches!(self.current_type(), TokenType::RParen) {
                    loop {
                        params.push(self.parse_type()?);
                        if !matches!(self.current_type(), TokenType::Comma) { break; }
                        self.pos += 1;
                    }
                }
                self.eat(&TokenType::RParen)?;
                self.eat(&TokenType::ThinArrow)?;
                let return_type = self.parse_type()?;
                let end = (return_type.end_line, return_type.end_column);
                (TypeKind::Function { params, return_type: Box::new(return_type), convention }, end.0, end.1)
            } else if matches!(self.current_type(), TokenType::LBracket) {
                self.pos += 1;
                let mut args = vec![self.parse_type()?];
                while matches!(self.current_type(), TokenType::Comma) {
                    self.pos += 1;
                    args.push(self.parse_type()?);
                }
                let end = self.eat(&TokenType::RBracket)?.clone();
                (TypeKind::Generic { name, args }, end.line, end.column + 1)
            } else if matches!(self.current_type(), TokenType::Dot)
                && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type), Some(TokenType::Identifier(_)))
            {
                // P2-T47 (Plan b1f51d65 C1): qualifizierter Typname `q.Typ`.
                // Der Resolver fuehrt ihn auf `Typ` zurueck oder meldet einen
                // Fehler; ein Punktname erreicht den Codegen nie.
                self.pos += 1;
                let glied_token = self.current().clone();
                let glied = self.eat_identifier()?;
                let end_column = glied_token.column + glied.chars().count();
                (TypeKind::Name(format!("{name}.{glied}")), glied_token.line, end_column)
            } else {
                let end_column = start.column + name.chars().count();
                (TypeKind::Name(name), start.line, end_column)
            }
        };
        Ok(TypeRef { kind, line: start.line, column: start.column, end_line, end_column })
    }

    fn parse_optional_annotation(&mut self) -> Result<Option<crate::ast::TypeRef>, ParseError> {
        if matches!(self.current_type(), TokenType::Colon)
            && self.tokens.get(self.pos + 1).is_some_and(|t|
                matches!(t.token_type, TokenType::Identifier(_) | TokenType::LBracket))
        {
            self.pos += 1;
            Ok(Some(self.parse_type()?))
        } else {
            Ok(Option::None)
        }
    }

    fn parse_param(&mut self, params: &mut Vec<String>, param_types: &mut Vec<Option<crate::ast::TypeRef>>, defaults: &mut Vec<Option<Expr>>) -> Result<(), ParseError> {
        self.parse_param_receiver(params, param_types, defaults, self.native_context)
    }

    fn parse_param_receiver(&mut self, params: &mut Vec<String>, param_types: &mut Vec<Option<crate::ast::TypeRef>>, defaults: &mut Vec<Option<Expr>>, allow_receiver: bool) -> Result<(), ParseError> {
        let name = if allow_receiver && params.is_empty() && matches!(self.current_type(), TokenType::This) {
            self.pos += 1;
            "selbst".to_string()
        } else {
            self.eat_identifier()?
        };
        params.push(name);
        let ty = if matches!(self.current_type(), TokenType::Colon)
            && self.tokens.get(self.pos + 1).is_some_and(|t|
                matches!(t.token_type, TokenType::Identifier(_) | TokenType::LBracket))
        {
            self.pos += 1;
            Some(self.parse_type()?)
        } else {
            Option::None
        };
        param_types.push(ty);
        if matches!(self.current_type(), TokenType::Assign) {
            self.pos += 1;
            defaults.push(Some(self.parse_expression()?));
        } else {
            defaults.push(Option::None);
        }
        Ok(())
    }

    fn parse_return(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        if matches!(self.current_type(), TokenType::Newline | TokenType::Eof | TokenType::Dedent) {
            Ok(Stmt::Return(Option::None, 0))
        } else {
            Ok(Stmt::Return(Some(self.parse_expression()?), 0))
        }
    }

    fn parse_class_def(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let name = self.eat_identifier()?;
        let parent = if matches!(self.current_type(), TokenType::LParen) {
            self.pos += 1;
            let p = self.eat_identifier()?;
            self.eat(&TokenType::RParen)?;
            Some(p)
        } else {
            Option::None
        };
        // Optional: implementiert/implements Interface1, Interface2
        let mut interfaces = Vec::new();
        if matches!(self.current_type(), TokenType::Implements) {
            self.pos += 1;
            interfaces.push(self.eat_identifier()?);
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                interfaces.push(self.eat_identifier()?);
            }
        }
        let body = self.parse_block_body(false)?;
        Ok(Stmt::ClassDef { zeile: 0, name, parent, interfaces, body })
    }

    fn parse_interface_def(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let name = self.eat_identifier()?;
        self.eat(&TokenType::Colon)?;
        self.skip_newlines();
        self.eat(&TokenType::Indent)?;
        let mut methods = Vec::new();
        let mut signatures = Vec::new();
        loop {
            self.skip_newlines();
            match self.current_type() {
                TokenType::Dedent => { self.pos += 1; break; }
                TokenType::Eof => break,
                TokenType::Func => {
                    self.pos += 1;
                    let method_name = self.eat_identifier()?;
                    self.eat(&TokenType::LParen)?;
                    let start = self.pos;
                    let parsed = (|| {
                        let mut params = Vec::new();
                        let mut param_types = Vec::new();
                        let mut defaults = Vec::new();
                        if !matches!(self.current_type(), TokenType::RParen) {
                            loop {
                                self.parse_param_receiver(&mut params, &mut param_types, &mut defaults, true)?;
                                // P2-T60: parse_param uebernimmt den Typ nur bei Name/'['
                                // nach ':'. Ein verbliebenes ':' ist eine Annotation ohne
                                // gueltigen Typ; parse_type liefert die echte Typdiagnose.
                                if matches!(self.current_type(), TokenType::Colon) {
                                    self.pos += 1;
                                    self.parse_type()?;
                                }
                                if !matches!(self.current_type(), TokenType::Comma) { break; }
                                self.pos += 1;
                            }
                        }
                        self.eat(&TokenType::RParen)?;
                        Ok::<_, ParseError>((params, param_types, defaults))
                    })();
                    let parsed = match parsed {
                        Ok(params) => Some(params),
                        Err(err) => {
                            // P2-T60: Ein ':' bis zur ersten ')' ist eine explizite
                            // Typannotation (`name: Typ`). Deren Fehler wird gemeldet
                            // statt verworfen. Nur untypisierte Altformen ohne ':'
                            // (Bestand: compiler/tests/test_interface.moos) behalten
                            // den toleranten Fallback mit signatures = None.
                            self.pos = start;
                            let mut typisiert = false;
                            while !matches!(self.current_type(), TokenType::RParen | TokenType::Eof) {
                                if matches!(self.current_type(), TokenType::Colon) { typisiert = true; }
                                self.pos += 1;
                            }
                            if typisiert {
                                return Err(ParseError {
                                    message: format!("Interface-Methode '{}': {}", method_name, err.message),
                                    ..ParseError::an(err.line, err.column)
                                });
                            }
                            // Bestehende Interfaces akzeptierten beliebige Tokens
                            // bis zur ersten ')'. Diese alten Formen bleiben parsebar.
                            self.eat(&TokenType::RParen)?;
                            Option::None
                        }
                    };
                    let return_type = if matches!(self.current_type(), TokenType::ThinArrow) {
                        self.pos += 1;
                        Some(self.parse_type()?)
                    } else { Option::None };
                    methods.push(method_name.clone());
                    signatures.push(parsed.map(|(params, param_types, defaults)| crate::ast::InterfaceSignature {
                        name: method_name, params, param_types, defaults, return_type,
                    }));
                }
                _ => return Err(ParseError {
                    message: "funktion/func in Interface erwartet".to_string(),
                    ..ParseError::bei(self.current())
                }),
            }
        }
        Ok(Stmt::InterfaceDef { zeile: 0, name, methods, signatures })
    }

    fn parse_try_catch(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let try_body = self.parse_block_body(false)?;
        self.skip_newlines();
        self.eat(&TokenType::Catch)?;
        let catch_var = if matches!(self.current_type(), TokenType::Identifier(_)) {
            Some(self.eat_identifier()?)
        } else {
            Option::None
        };
        let catch_body = self.parse_block_body(false)?;
        Ok(Stmt::TryCatch { zeile: 0, try_body, catch_var, catch_body })
    }

    fn parse_throw(&mut self) -> Result<Stmt, ParseError> {
        let zeile = self.current().line;
        self.pos += 1;
        let value = self.parse_expression()?;
        Ok(Stmt::Throw(value, zeile))
    }

    fn parse_global_decl(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let mut names = vec![self.eat_identifier()?];
        while matches!(self.current_type(), TokenType::Comma) {
            self.pos += 1;
            names.push(self.eat_identifier()?);
        }
        Ok(Stmt::Global(names, 0))
    }

    /// Modulname fuer importiere/aus: Bezeichner (`mathe`) oder Pfad-String
    /// (`"../lib/welt"`, relativ zur importierenden Datei).
    fn eat_module_name(&mut self) -> Result<String, ParseError> {
        if let TokenType::String(path) = self.current_type().clone() {
            self.pos += 1;
            Ok(path)
        } else {
            self.eat_identifier()
        }
    }

    fn parse_import(&mut self) -> Result<Stmt, ParseError> {
        let line = self.current().line;
        self.pos += 1;
        let module = self.eat_module_name()?;
        let alias = if matches!(self.current_type(), TokenType::As) {
            self.pos += 1;
            Some(self.eat_identifier()?)
        } else {
            Option::None
        };
        Ok(Stmt::Import { module, names: vec![], alias, line })
    }

    fn parse_from_import(&mut self) -> Result<Stmt, ParseError> {
        let line = self.current().line;
        self.pos += 1;
        let module = self.eat_module_name()?;
        self.eat(&TokenType::Import)?;
        let mut names = vec![self.eat_identifier()?];
        while matches!(self.current_type(), TokenType::Comma) {
            self.pos += 1;
            names.push(self.eat_identifier()?);
        }
        Ok(Stmt::Import { module, names, alias: Option::None, line })
    }

    fn parse_export(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let stmt = self.parse_statement()?;
        Ok(Stmt::Export(Box::new(stmt), 0))
    }

    fn parse_defer(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // defer/aufräumen
        self.eat(&TokenType::Colon)?;
        let expr = self.parse_expression()?;
        Ok(Stmt::Defer(expr, 0))
    }

    fn parse_guard(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // guard/garantiere
        let condition = self.parse_expression()?;
        // Erwarte Komma vor sonst/else
        if matches!(self.current_type(), TokenType::Comma) {
            self.pos += 1;
        }
        self.eat(&TokenType::Else)?;
        let else_body = self.parse_block_body(false)?;
        Ok(Stmt::Guard { zeile: 0, condition, else_body })
    }

    fn parse_match(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let value = self.parse_expression()?;
        self.eat(&TokenType::Colon)?;
        self.skip_newlines();
        self.eat(&TokenType::Indent)?;

        let mut cases = Vec::new();
        loop {
            self.skip_newlines();
            match self.current_type() {
                TokenType::Dedent => { self.pos += 1; break; }
                TokenType::Eof => break,
                TokenType::Case => {
                    self.pos += 1;
                    // Wildcard: fall _ → wie default
                    if let TokenType::Identifier(name) = self.current_type().clone() {
                        if name == "_" {
                            self.pos += 1;
                            let body = self.parse_block_body(false)?;
                            cases.push((Option::None, Option::None, body));
                            continue;
                        }
                    }
                    let pattern = self.parse_expression()?;
                    // Guard: fall n wenn n > 10: / fall n if n > 10:
                    let guard = if matches!(self.current_type(), TokenType::If) {
                        self.pos += 1;
                        Some(self.parse_expression()?)
                    } else {
                        Option::None
                    };
                    let body = self.parse_block_body(false)?;
                    cases.push((Some(pattern), guard, body));
                }
                TokenType::Default => {
                    self.pos += 1;
                    let body = self.parse_block_body(false)?;
                    cases.push((Option::None, Option::None, body));
                }
                _ => return Err(ParseError {
                    message: format!("fall/case oder standard/default erwartet"),
                    ..ParseError::bei(self.current())
                }),
            }
        }

        Ok(Stmt::Match { zeile: 0, value, cases })
    }

    // --- Expressions ---

    fn parse_expression(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_expression_kern())
    }

    fn parse_expression_kern(&mut self) -> Result<Expr, ParseError> {
        self.glied()?;
        let expr = self.parse_pipe()?;
        // Ternary: expr ? then_val : else_val
        if matches!(self.current_type(), TokenType::Question) {
            self.pos += 1;
            let then_val = self.parse_pipe()?;
            self.eat(&TokenType::Colon)?;
            let else_val = self.parse_pipe()?;
            return Ok(Expr::Ternary {
                condition: Box::new(expr),
                then_val: Box::new(then_val),
                else_val: Box::new(else_val),
            });
        }
        Ok(expr)
    }

    fn parse_pipe(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_pipe_kern())
    }

    fn parse_pipe_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_nullish_coalesce()?;
        while matches!(self.current_type(), TokenType::Pipe) {
            self.pos += 1;
            self.glied()?;
            let right = self.parse_nullish_coalesce()?;
            left = Expr::Pipe { left: Box::new(left), right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_nullish_coalesce(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_nullish_coalesce_kern())
    }

    fn parse_nullish_coalesce_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_or()?;
        while matches!(self.current_type(), TokenType::NullishCoalesce) {
            self.pos += 1;
            self.glied()?;
            let right = self.parse_or()?;
            left = Expr::NullishCoalesce { left: Box::new(left), right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_or(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_or_kern())
    }

    fn parse_or_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and()?;
        while matches!(self.current_type(), TokenType::Or) {
            self.pos += 1;
            self.glied()?;
            let right = self.parse_and()?;
            left = Expr::BinaryOp { left: Box::new(left), op: BinOp::Or, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_and_kern())
    }

    fn parse_and_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_not()?;
        while matches!(self.current_type(), TokenType::And) {
            self.pos += 1;
            self.glied()?;
            let right = self.parse_not()?;
            left = Expr::BinaryOp { left: Box::new(left), op: BinOp::And, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_not(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_not_kern())
    }

    fn parse_not_kern(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.current_type(), TokenType::Not) {
            self.pos += 1;
            self.glied()?;
            let operand = self.parse_not()?;
            Ok(Expr::UnaryOp { op: UnaryOpKind::Not, operand: Box::new(operand) })
        } else {
            self.parse_comparison()
        }
    }

    fn parse_comparison(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_comparison_kern())
    }

    fn parse_comparison_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_bitwise()?;
        loop {
            let op = match self.current_type() {
                TokenType::Equals => BinOp::Eq,
                TokenType::NotEquals => BinOp::NotEq,
                TokenType::Less => BinOp::Less,
                TokenType::Greater => BinOp::Greater,
                TokenType::LessEq => BinOp::LessEq,
                TokenType::GreaterEq => BinOp::GreaterEq,
                _ => break,
            };
            self.pos += 1;
            self.glied()?;
            let right = self.parse_bitwise()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_bitwise(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_bitwise_kern())
    }

    fn parse_bitwise_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_addition()?;
        loop {
            let op = match self.current_type() {
                TokenType::BitAnd => BinOp::BitAnd,
                TokenType::BitOr => BinOp::BitOr,
                TokenType::BitXor => BinOp::BitXor,
                TokenType::LShift => BinOp::LShift,
                TokenType::RShift => BinOp::RShift,
                _ => break,
            };
            self.pos += 1;
            self.glied()?;
            let right = self.parse_addition()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_addition(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_addition_kern())
    }

    fn parse_addition_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_multiplication()?;
        // Range: 0..10, 2..n+1 — rechte Seite als vollständige Addition parsen
        if matches!(self.current_type(), TokenType::Range) {
            self.pos += 1;
            self.glied()?;
            let right = self.parse_addition()?;
            return Ok(Expr::Range { start: Box::new(left), end: Box::new(right) });
        }
        loop {
            let op = match self.current_type() {
                TokenType::Plus => BinOp::Add,
                TokenType::Minus => BinOp::Sub,
                _ => break,
            };
            self.pos += 1;
            self.glied()?;
            let right = self.parse_multiplication()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_multiplication(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_multiplication_kern())
    }

    fn parse_multiplication_kern(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_power()?;
        loop {
            let op = match self.current_type() {
                TokenType::Multiply => BinOp::Mul,
                TokenType::Divide => BinOp::Div,
                TokenType::Modulo => BinOp::Mod,
                _ => break,
            };
            self.pos += 1;
            self.glied()?;
            let right = self.parse_power()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_power(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_power_kern())
    }

    fn parse_power_kern(&mut self) -> Result<Expr, ParseError> {
        let left = self.parse_unary()?;
        if matches!(self.current_type(), TokenType::Power) {
            self.pos += 1;
            self.glied()?;
            let right = self.parse_power()?; // Rechts-assoziativ
            Ok(Expr::BinaryOp { left: Box::new(left), op: BinOp::Pow, right: Box::new(right) })
        } else {
            Ok(left)
        }
    }

    fn parse_unary(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_unary_kern())
    }

    fn parse_unary_kern(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.current_type(), TokenType::Minus) {
            self.pos += 1;
            self.glied()?;
            let operand = self.parse_postfix()?;
            Ok(Expr::UnaryOp { op: UnaryOpKind::Neg, operand: Box::new(operand) })
        } else if matches!(self.current_type(), TokenType::BitNot) {
            self.pos += 1;
            self.glied()?;
            let operand = self.parse_postfix()?;
            Ok(Expr::UnaryOp { op: UnaryOpKind::BitNot, operand: Box::new(operand) })
        } else {
            self.parse_postfix()
        }
    }

    fn parse_postfix(&mut self) -> Result<Expr, ParseError> {
        self.mit_tiefe(|p| p.parse_postfix_kern())
    }

    fn parse_postfix_kern(&mut self) -> Result<Expr, ParseError> {
        let mut node = self.parse_primary()?;

        loop {
            match self.current_type() {
                TokenType::Dot => {
                    self.pos += 1;
                    let prop = self.eat_identifier()?;
                    if matches!(self.current_type(), TokenType::LParen) {
                        self.pos += 1;
                        let args = self.parse_args_list()?;
                        self.eat(&TokenType::RParen)?;
                        node = Expr::MethodCall {
                            object: Box::new(node),
                            method: prop,
                            args,
                        };
                    } else {
                        node = Expr::PropertyAccess {
                            object: Box::new(node),
                            property: prop,
                        };
                    }
                }
                TokenType::OptionalChain => {
                    self.pos += 1;
                    let prop = self.eat_identifier()?;
                    node = Expr::OptionalChain {
                        object: Box::new(node),
                        property: prop,
                    };
                }
                TokenType::LBracket => {
                    self.pos += 1;
                    let index = self.parse_expression()?;
                    self.eat(&TokenType::RBracket)?;
                    node = Expr::IndexAccess {
                        object: Box::new(node),
                        index: Box::new(index),
                    };
                }
                _ => break,
            }
            self.glied()?;
        }

        Ok(node)
    }

    fn parse_args_list(&mut self) -> Result<Vec<Expr>, ParseError> {
        self.parse_call_args(false)
    }

    fn parse_call_args(&mut self, named_tail: bool) -> Result<Vec<Expr>, ParseError> {
        let mut args = Vec::new();
        if matches!(self.current_type(), TokenType::RParen) {
            return Ok(args);
        }
        loop {
            if named_tail && matches!(self.current_type(), TokenType::Identifier(_) | TokenType::Order)
                && self.tokens.get(self.pos + 1).is_some_and(|t| matches!(t.token_type, TokenType::Assign))
            {
                let mut pairs = Vec::new();
                loop {
                    // Das Query-Schluesselwort ist hier der englische Optionsname.
                    let key = if matches!(self.current_type(), TokenType::Order) {
                        self.pos += 1;
                        "order".to_string()
                    } else {
                        self.eat_identifier()?
                    };
                    self.eat(&TokenType::Assign)?;
                    pairs.push((Expr::String(key), self.parse_expression()?));
                    if !matches!(self.current_type(), TokenType::Comma) { break; }
                    self.pos += 1;
                }
                args.push(Expr::Dict(pairs));
                break;
            }
            args.push(self.parse_expression()?);
            if !matches!(self.current_type(), TokenType::Comma) { break; }
            self.pos += 1;
        }
        Ok(args)
    }

    /// Validiert Basispräfixe nur in explizit nativen Funktionen. Im Legacy-
    /// Pfad bleiben die historischen Tokens und das f64-Verhalten unverändert.
    fn parse_native_number(&mut self, token: Token, legacy: f64) -> Result<Expr, ParseError> {
        let mut text = token.number_text.ok_or_else(|| ParseError {
            message: "Zahlliteral ohne Quelltext".to_string(), ..ParseError::an(token.line, token.column)
        })?;
        // Der Legacy-Lexer trennt 0o... in Number(0) und Identifier(o...).
        if text == "0" && self.current().line == token.line
            && self.current().column == token.column + 1
        {
            if let TokenType::Identifier(suffix) = self.current_type() {
                if suffix.starts_with('o') || suffix.starts_with('O') {
                    text.push_str(suffix);
                    self.pos += 1;
                }
            }
        }
        let base = if text.starts_with("0x") || text.starts_with("0X") {
            Some(16)
        } else if text.starts_with("0b") || text.starts_with("0B") {
            Some(2)
        } else if text.starts_with("0o") || text.starts_with("0O") {
            Some(8)
        } else {
            Option::None
        };
        if let Some(base) = base {
            // Der Legacy-Lexer kann ungültige Ziffern als direkt folgendes
            // Token abschneiden; im nativen Kontext gehört das zur Diagnose.
            if self.current().line == token.line
                && self.current().column == token.column + text.chars().count()
            {
                let suffix = match self.current_type() {
                    TokenType::Identifier(s) => Some(s.clone()),
                    TokenType::Number(_) => self.current().number_text.clone(),
                    _ => Option::None,
                };
                if let Some(suffix) = suffix { text.push_str(&suffix); }
            }
            let digits = &text[2..];
            if digits.is_empty() || !digits.chars().all(|ch| ch.to_digit(base).is_some()) {
                return Err(ParseError {
                    message: format!("Ungültiges Zahl-Literal '{text}' zur Basis {base}"),
                    ..ParseError::an(token.line, token.column)
                });
            }
        }
        // Exponenten nur im nativen Kontext zusammensetzen: der Legacy-Lexer
        // behält seine historischen Number-/Identifier-/Operator-Tokens.
        if base.is_none() && self.current().line == token.line
            && self.current().column == token.column + text.chars().count()
        {
            let exponent = match self.current_type() {
                TokenType::Identifier(s) if s.starts_with('e') || s.starts_with('E') => Some(s.clone()),
                _ => Option::None,
            };
            if let Some(exponent) = exponent {
                text.push_str(&exponent);
                self.pos += 1;
                let mut digits = &exponent[1..];
                if digits.is_empty() && self.current().line == token.line
                    && self.current().column == token.column + text.chars().count()
                    && matches!(self.current_type(), TokenType::Plus | TokenType::Minus)
                {
                    text.push(if matches!(self.current_type(), TokenType::Plus) { '+' } else { '-' });
                    self.pos += 1;
                    if self.current().line == token.line
                        && self.current().column == token.column + text.chars().count()
                    {
                        if let TokenType::Number(_) = self.current_type() {
                            if let Some(raw) = self.current().number_text.clone() {
                                text.push_str(&raw);
                                self.pos += 1;
                            }
                        }
                    }
                    digits = text.rsplit(['+', '-']).next().unwrap_or("");
                }
                if digits.is_empty() || !digits.chars().all(|ch| ch.is_ascii_digit()) {
                    return Err(ParseError {
                        message: format!("Ungültiger Exponent im Zahl-Literal '{text}'"),
                        ..ParseError::an(token.line, token.column)
                    });
                }
            }
        }
        Ok(Expr::NativeNumber { legacy, text, line: token.line, column: token.column })
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        let start = self.current().clone();
        match self.current_type().clone() {
            TokenType::Number(n) => {
                let token = self.current().clone();
                self.pos += 1;
                if self.native_context {
                    self.parse_native_number(token, n)
                } else {
                    Ok(Expr::Number(n))
                }
            }
            TokenType::String(s) => {
                self.pos += 1;
                Ok(Expr::String(s))
            }
            TokenType::Boolean(b) => {
                self.pos += 1;
                Ok(Expr::Boolean(b))
            }
            TokenType::None => {
                self.pos += 1;
                Ok(Expr::None)
            }
            TokenType::This => {
                self.pos += 1;
                if self.native_context { Ok(Expr::Identifier("selbst".into())) } else { Ok(Expr::This) }
            }
            TokenType::New => {
                self.pos += 1;
                let mut class_name = self.eat_identifier()?;
                // P2-T47: `neu q.Klasse(...)`; der Resolver loest den Punktnamen auf.
                if matches!(self.current_type(), TokenType::Dot)
                    && matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type), Some(TokenType::Identifier(_)))
                {
                    self.pos += 1;
                    let glied = self.eat_identifier()?;
                    class_name = format!("{class_name}.{glied}");
                }
                self.eat(&TokenType::LParen)?;
                let args = self.parse_args_list()?;
                self.eat(&TokenType::RParen)?;
                Ok(Expr::New { class_name, args })
            }
            TokenType::Identifier(name) => {
                self.pos += 1;
                // P2-T114 B: `übergib puffer` (EN `transfer`) als Praefix; zwei
                // Namen hintereinander waren vorher ein Syntaxfehler.
                if matches!(name.as_str(), "übergib" | "uebergib" | "transfer") {
                    if let TokenType::Identifier(var) = self.current_type().clone() {
                        let zeile = self.current().line;
                        self.pos += 1;
                        return Ok(Expr::FunctionCall { type_args: Vec::new(), name: "übergib".to_string(), args: vec![Expr::Identifier(var)], zeile, spalten: (0, 0) });
                    }
                }
                // P2-T86: Die Rückrufsignatur ist ein echter Typkontext;
                // sie wird bis zum Codegen im AST erhalten.
                if matches!(name.as_str(), "rueckruf_registrieren" | "rückruf_registrieren")
                    && matches!(self.current_type(), TokenType::LBracket)
                    && self.registration_type_follows() {
                    self.pos += 1;
                    let mut type_args = Vec::new();
                    loop {
                        type_args.push(self.parse_type()?);
                        if !matches!(self.current_type(), TokenType::Comma) { break; }
                        self.pos += 1;
                    }
                    self.eat(&TokenType::RBracket)?;
                    self.eat(&TokenType::LParen)?;
                    let mut args = Vec::new();
                    if !matches!(self.current_type(), TokenType::RParen) {
                        // Ein Pipe fuegt die Funktion erst im Codegen vorne ein.
                        // Dann ist bereits das erste geschriebene Argument der
                        // Fehlerwert. Zahlen auch hier verlustfrei erhalten,
                        // Lambda-/Factory-Ausdruecke bleiben dynamischer Kontext.
                        let alt = self.native_context;
                        if matches!(self.current_type(), TokenType::Number(_) | TokenType::Minus) {
                            self.native_context = true;
                        }
                        let erster = self.parse_expression();
                        self.native_context = alt;
                        args.push(erster?);
                        while matches!(self.current_type(), TokenType::Comma) {
                            self.pos += 1;
                            // Fehlerwerte haben den expliziten nativen Ergebnistyp.
                            // Das Original-Literal behalten, auch jenseits von 2^53.
                            let alt = self.native_context;
                            self.native_context = true;
                            let wert = self.parse_expression();
                            self.native_context = alt;
                            args.push(wert?);
                        }
                    }
                    self.eat(&TokenType::RParen)?;
                    return Ok(Expr::FunctionCall {
                        name, type_args, args, zeile: start.line,
                        spalten: (start.column, if self.tokens[self.pos - 1].line == start.line {
                            self.tokens[self.pos - 1].column + 1
                        } else { 0 }),
                    });
                }
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    // Native Optionen: assembler und Atomics erhalten die benannten Argumente als Dict.
                    let named_tail = self.native_context
                        && (name == "assembler" || crate::native_system::atom_art(&name).is_some());
                    let args = self.parse_call_args(named_tail)?;
                    self.eat(&TokenType::RParen)?;
                    Ok(Expr::FunctionCall { type_args: Vec::new(), name, args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
                } else {
                    Ok(Expr::Identifier(name))
                }
            }
            TokenType::As => {
                // P2-T34: als(Typ, Wert) ist der gepruefte Standardcast (ADR §4).
                // Sonst bleibt 'als' Schluesselwort (importiere ... als ...);
                // ohne '(' war es hier schon immer ein Fehler.
                if !matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type), Some(TokenType::LParen)) {
                    let got = got_description(self.current_type());
                    return Err(ParseError {
                        message: format!("Hier wird ein Wert erwartet (Zahl, Text, Variable, ...), aber {got} gefunden."),
                        ..ParseError::bei(self.current())
                    });
                }
                self.pos += 2;
                let args = self.parse_args_list()?;
                self.eat(&TokenType::RParen)?;
                Ok(Expr::FunctionCall { type_args: Vec::new(), name: "als".to_string(), args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
            }
            TokenType::Data => {
                // Soft-Keyword: "daten" als Variable/Funktion erlaubt, ausser
                // direkt gefolgt von "klasse" (Data-Class-Syntax).
                if matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Class)) {
                    let got = got_description(self.current_type());
                    return Err(ParseError {
                        message: format!("Hier wird ein Wert erwartet (Zahl, Text, Variable, ...), aber {got} gefunden."),
                        ..ParseError::bei(self.current())
                    });
                }
                self.pos += 1;
                let name = "daten".to_string();
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    let args = self.parse_args_list()?;
                    self.eat(&TokenType::RParen)?;
                    Ok(Expr::FunctionCall { type_args: Vec::new(), name, args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
                } else {
                    Ok(Expr::Identifier(name))
                }
            }
            TokenType::Try => {
                // Soft-Keyword: 'versuche' als Identifier erlaubt, ausser als
                // Block-Start (gefolgt von ':')
                if matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::Colon)) {
                    let got = got_description(self.current_type());
                    return Err(ParseError {
                        message: format!("Hier wird ein Wert erwartet (Zahl, Text, Variable, ...), aber {got} gefunden."),
                        ..ParseError::bei(self.current())
                    });
                }
                self.pos += 1;
                let name = "versuche".to_string();
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    let args = self.parse_args_list()?;
                    self.eat(&TokenType::RParen)?;
                    Ok(Expr::FunctionCall { type_args: Vec::new(), name, args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
                } else {
                    Ok(Expr::Identifier(name))
                }
            }
            TokenType::Test => {
                // Soft-Keyword: 'teste' als Identifier erlaubt (Test-Framework
                // nutzt teste "name": Block, also ist nach teste ein String).
                if matches!(self.tokens.get(self.pos + 1).map(|t| &t.token_type),
                            Some(TokenType::String(_))) {
                    let got = got_description(self.current_type());
                    return Err(ParseError {
                        message: format!("Hier wird ein Wert erwartet (Zahl, Text, Variable, ...), aber {got} gefunden."),
                        ..ParseError::bei(self.current())
                    });
                }
                self.pos += 1;
                let name = "teste".to_string();
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    let args = self.parse_args_list()?;
                    self.eat(&TokenType::RParen)?;
                    Ok(Expr::FunctionCall { type_args: Vec::new(), name, args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
                } else {
                    Ok(Expr::Identifier(name))
                }
            }
            TokenType::LParen => {
                // Lambda oder Gruppierung
                if self.is_lambda() {
                    self.parse_lambda()
                } else {
                    self.pos += 1;
                    let expr = self.parse_expression()?;
                    self.eat(&TokenType::RParen)?;
                    Ok(expr)
                }
            }
            TokenType::LBracket => self.parse_list(),
            TokenType::LBrace => self.parse_dict(),
            TokenType::Match => {
                if self.is_expression_start_at(self.pos + 1) {
                    self.parse_match_expr()
                } else {
                    self.pos += 1;
                    let name = "pruefe".to_string();
                    if matches!(self.current_type(), TokenType::LParen) {
                        self.pos += 1;
                        let args = self.parse_args_list()?;
                        self.eat(&TokenType::RParen)?;
                        Ok(Expr::FunctionCall { type_args: Vec::new(), name, args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
                    } else {
                        Ok(Expr::Identifier(name))
                    }
                }
            }
            TokenType::Expect => {
                // Soft-Keyword: im Expression-Kontext immer als Identifier.
                self.pos += 1;
                let name = "erwarte".to_string();
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    let args = self.parse_args_list()?;
                    self.eat(&TokenType::RParen)?;
                    Ok(Expr::FunctionCall { type_args: Vec::new(), name, args, zeile: if self.native_context { start.line } else { self.tokens[self.pos - 1].line }, spalten: (start.column, if self.tokens[self.pos - 1].line == start.line { self.tokens[self.pos - 1].column + 1 } else { 0 }) })
                } else {
                    Ok(Expr::Identifier(name))
                }
            }
            TokenType::Where => {
                self.pos += 1;
                Ok(Expr::Identifier("wo".to_string()))
            }
            TokenType::Order => {
                self.pos += 1;
                Ok(Expr::Identifier("sortiere".to_string()))
            }
            TokenType::QueryFrom | TokenType::From => self.parse_query_expr(),
            _ => {
                let got = got_description(self.current_type());
                Err(ParseError {
                    message: format!("Hier wird ein Wert erwartet (Zahl, Text, Variable, ...), aber {got} gefunden."),
                    ..ParseError::bei(self.current())
                })
            }
        }
    }

    fn parse_query_expr(&mut self) -> Result<Expr, ParseError> {
        self.pos += 1; // von/from
        let var_name = self.eat_identifier()?;
        self.eat(&TokenType::In)?;
        let source = self.parse_expression()?;

        // Optional: wo/where
        let where_cond = if matches!(self.current_type(), TokenType::Where) {
            self.pos += 1;
            Some(Box::new(self.parse_expression()?))
        } else {
            None
        };

        // Optional: sortiere/order
        let order_expr = if matches!(self.current_type(), TokenType::Order) {
            self.pos += 1;
            Some(Box::new(self.parse_expression()?))
        } else {
            None
        };

        // Pflicht: wähle/select
        self.eat(&TokenType::Select)?;
        let select_expr = self.parse_expression()?;

        Ok(Expr::QueryExpr {
            var_name,
            source: Box::new(source),
            where_cond,
            order_expr,
            select_expr: Box::new(select_expr),
        })
    }

    fn is_lambda(&self) -> bool {
        let mut depth = 0i32;
        let mut i = self.pos;
        while i < self.tokens.len() {
            match &self.tokens[i].token_type {
                TokenType::LParen => depth += 1,
                TokenType::RParen => {
                    depth -= 1;
                    if depth == 0 {
                        return i + 1 < self.tokens.len()
                            && matches!(self.tokens[i + 1].token_type, TokenType::Arrow);
                    }
                }
                TokenType::Newline | TokenType::Eof => return false,
                _ => {}
            }
            i += 1;
        }
        false
    }

    fn parse_lambda(&mut self) -> Result<Expr, ParseError> {
        self.eat(&TokenType::LParen)?;
        let mut params = Vec::new();
        let mut param_types = Vec::new();
        if !matches!(self.current_type(), TokenType::RParen) {
            loop {
                params.push(self.eat_identifier()?);
                param_types.push(self.parse_optional_annotation()?);
                if !matches!(self.current_type(), TokenType::Comma) { break; }
                self.pos += 1;
            }
        }
        self.eat(&TokenType::RParen)?;
        self.eat(&TokenType::Arrow)?;
        let body = self.parse_expression()?;
        Ok(Expr::Lambda { params, param_types, body: Box::new(body) })
    }

    fn parse_list(&mut self) -> Result<Expr, ParseError> {
        self.eat(&TokenType::LBracket)?;
        // Newlines/Indent/Dedent nach '[' ignorieren — erlaubt mehrzeilige Listen
        self.skip_layout_tokens();
        let mut elements = Vec::new();
        if !matches!(self.current_type(), TokenType::RBracket) {
            let first = self.parse_list_element()?;
            self.skip_layout_tokens();
            // List comprehension: [expr für/for x in iterable wenn/if cond]
            if !matches!(&first, Expr::Spread(_)) && matches!(self.current_type(), TokenType::For) {
                self.pos += 1; // skip für/for
                let var_name = self.eat_identifier()?;
                self.eat(&TokenType::In)?;
                let iterable = self.parse_expression()?;
                let condition = if matches!(self.current_type(), TokenType::If) {
                    self.pos += 1; // skip wenn/if
                    Some(Box::new(self.parse_expression()?))
                } else {
                    None
                };
                self.skip_layout_tokens();
                self.eat(&TokenType::RBracket)?;
                return Ok(Expr::ListComprehension {
                    expr: Box::new(first),
                    var_name,
                    iterable: Box::new(iterable),
                    condition,
                });
            }
            elements.push(first);
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                self.skip_layout_tokens();
                if matches!(self.current_type(), TokenType::RBracket) { break; }
                elements.push(self.parse_list_element()?);
                self.skip_layout_tokens();
            }
        }
        self.eat(&TokenType::RBracket)?;
        Ok(Expr::List(elements))
    }

    /// Ueberspringt Newline/Indent/Dedent — fuer mehrzeilige Literale innerhalb
    /// offener Klammern ([...], {...}, (...)). Layout-Tokens sind dort bedeutungslos.
    fn skip_layout_tokens(&mut self) {
        while matches!(self.current_type(),
            TokenType::Newline | TokenType::Indent | TokenType::Dedent) {
            self.pos += 1;
        }
    }

    /// Prueft ob das Token an der gegebenen Position einen Ausdruck starten kann.
    /// Wird von Soft-Keywords (match/pruefe) genutzt um zu entscheiden ob ein
    /// Block-Keyword oder ein Identifier vorliegt.
    fn is_expression_start_at(&self, pos: usize) -> bool {
        let tok = match self.tokens.get(pos) {
            Some(t) => &t.token_type,
            None => return false,
        };
        matches!(tok,
            TokenType::Identifier(_)
            | TokenType::Number(_)
            | TokenType::String(_)
            | TokenType::Boolean(_)
            | TokenType::None
            | TokenType::This
            | TokenType::New
            | TokenType::LParen
            | TokenType::LBracket
            | TokenType::LBrace
            | TokenType::Minus
            | TokenType::Not
            | TokenType::Data
            | TokenType::Try
            | TokenType::Test
        )
    }

    fn parse_list_element(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.current_type(), TokenType::Spread) {
            self.pos += 1;
            let expr = self.parse_expression()?;
            Ok(Expr::Spread(Box::new(expr)))
        } else {
            self.parse_expression()
        }
    }

    fn parse_dict(&mut self) -> Result<Expr, ParseError> {
        self.eat(&TokenType::LBrace)?;
        self.skip_layout_tokens();
        let mut pairs = Vec::new();
        if !matches!(self.current_type(), TokenType::RBrace) {
            self.parse_dict_entry(&mut pairs)?;
            self.skip_layout_tokens();
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                self.skip_layout_tokens();
                if matches!(self.current_type(), TokenType::RBrace) { break; }
                self.parse_dict_entry(&mut pairs)?;
                self.skip_layout_tokens();
            }
        }
        self.eat(&TokenType::RBrace)?;
        Ok(Expr::Dict(pairs))
    }

    /// Match als Expression (Kotlin when): prüfe wert: fall x: ergebnis
    fn parse_match_expr(&mut self) -> Result<Expr, ParseError> {
        self.pos += 1; // prüfe/match
        let value = self.parse_expression()?;
        self.eat(&TokenType::Colon)?;
        self.skip_newlines();
        self.eat(&TokenType::Indent)?;

        let mut cases = Vec::new();
        loop {
            self.skip_newlines();
            match self.current_type() {
                TokenType::Dedent => { self.pos += 1; break; }
                TokenType::Eof => break,
                TokenType::Case => {
                    self.pos += 1;
                    // Wildcard: fall _
                    if let TokenType::Identifier(name) = self.current_type().clone() {
                        if name == "_" {
                            self.pos += 1;
                            self.eat(&TokenType::Colon)?;
                            let result = self.parse_expression()?;
                            cases.push((Option::None, Option::None, result));
                            continue;
                        }
                    }
                    let pattern = self.parse_expression()?;
                    let guard = if matches!(self.current_type(), TokenType::If) {
                        self.pos += 1;
                        Some(self.parse_expression()?)
                    } else {
                        Option::None
                    };
                    self.eat(&TokenType::Colon)?;
                    let result = self.parse_expression()?;
                    cases.push((Some(pattern), guard, result));
                }
                TokenType::Default => {
                    self.pos += 1;
                    self.eat(&TokenType::Colon)?;
                    let result = self.parse_expression()?;
                    cases.push((Option::None, Option::None, result));
                }
                _ => return Err(ParseError {
                    message: "fall/case oder standard/default erwartet".to_string(),
                    ..ParseError::bei(self.current())
                }),
            }
        }

        Ok(Expr::MatchExpr {
            value: Box::new(value),
            cases,
        })
    }

    fn parse_dict_entry(&mut self, pairs: &mut Vec<(Expr, Expr)>) -> Result<(), ParseError> {
        if matches!(self.current_type(), TokenType::Spread) {
            self.pos += 1;
            let expr = self.parse_expression()?;
            // Spread in Dict: Sentinel-Paar mit Spread-Marker
            pairs.push((Expr::Spread(Box::new(expr)), Expr::None));
            Ok(())
        } else {
            let key = self.parse_expression()?;
            self.eat(&TokenType::Colon)?;
            let val = self.parse_expression()?;
            pairs.push((key, val));
            Ok(())
        }
    }
}

/// Enthaelt ein Zuweisungsziel einen Aufruf (Seiteneffekt bei mehrfacher Auswertung)?
/// Erschoepfend ueber alle Ausdrucksformen (kein `_`-Arm): eine neue Form muss
/// hier bewusst eingeordnet werden. Ein Pipe ist ein Aufruf; ein Lambda wird beim
/// Auswerten des Ziels nur erzeugt, nicht ausgefuehrt.
fn ausdruck_hat_aufruf(expr: &Expr) -> bool {
    let a = |e: &Expr| ausdruck_hat_aufruf(e);
    match expr {
        Expr::FunctionCall { .. } | Expr::MethodCall { .. } | Expr::New { .. } | Expr::Pipe { .. } => true,
        Expr::Number(_) | Expr::NativeNumber { .. } | Expr::String(_) | Expr::Boolean(_) | Expr::None
        | Expr::Identifier(_) | Expr::This | Expr::Lambda { .. } => false,
        Expr::IndexAccess { object: x, index: y } | Expr::Range { start: x, end: y }
        | Expr::BinaryOp { left: x, right: y, .. } | Expr::NullishCoalesce { left: x, right: y } => a(x) || a(y),
        Expr::PropertyAccess { object: x, .. } | Expr::OptionalChain { object: x, .. }
        | Expr::UnaryOp { operand: x, .. } | Expr::Spread(x) => a(x),
        Expr::List(elemente) => elemente.iter().any(a),
        Expr::Dict(paare) => paare.iter().any(|(k, v)| a(k) || a(v)),
        Expr::Ternary { condition, then_val, else_val } => a(condition) || a(then_val) || a(else_val),
        Expr::ListComprehension { expr, iterable, condition, .. } =>
            a(expr) || a(iterable) || condition.as_deref().is_some_and(a),
        Expr::MatchExpr { value, cases } => a(value) || cases.iter().any(|(muster, waechter, ergebnis)|
            muster.as_ref().is_some_and(a) || waechter.as_ref().is_some_and(a) || a(ergebnis)),
        Expr::QueryExpr { source, where_cond, order_expr, select_expr, .. } =>
            a(source) || where_cond.as_deref().is_some_and(a) || order_expr.as_deref().is_some_and(a) || a(select_expr),
    }
}
