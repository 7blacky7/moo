/// Token-Definitionen und zweisprachiges Keyword-Mapping für moo.

#[derive(Debug, Clone, PartialEq)]
pub enum TokenType {
    // Literals
    Number(f64),
    String(std::string::String),
    Boolean(bool),
    None,
    Identifier(std::string::String),

    // Keywords
    Set,        // setze / set
    To,         // auf / to
    If,         // wenn / if
    Else,       // sonst / else
    While,      // solange / while
    For,        // für / for
    In,         // in
    Func,       // funktion / func
    Return,     // gib_zurück / return
    Show,       // zeige / show
    And,        // und / and
    Or,         // oder / or
    Not,        // nicht / not
    Class,      // klasse / class
    New,        // neu / new
    This,       // selbst / this
    Try,        // versuche / try
    Catch,      // fange / catch
    Throw,      // wirf / throw
    Break,      // stopp / break
    Continue,   // weiter / continue
    Const,      // konstante / const
    Match,      // prüfe / match
    Case,       // fall / case
    Default,    // standard / default
    Import,     // importiere / import
    From,       // aus / from
    Export,     // exportiere / export
    As,         // als / as
    Defer,      // aufräumen / defer
    Data,       // daten / data
    Guard,      // garantiere / guard
    Where,      // wo / where
    Select,     // wähle / select
    Order,      // sortiere / order
    QueryFrom,  // von (Query-Kontext, nicht Import)
    Interface,  // schnittstelle / interface
    Implements, // implementiert / implements
    Parallel,       // parallel
    Precondition,   // vorbedingung / precondition
    Postcondition,  // nachbedingung / postcondition
    Unsafe,         // unsicher / unsafe
    Test,           // teste / test
    Expect,         // erwarte / expect

    // Bitwise Operators
    BitAnd,       // &
    BitOr,        // |
    BitNot,       // ~
    BitXor,       // ^
    LShift,       // <<
    RShift,       // >>

    // Operators
    At,           // @
    Plus,
    Minus,
    Multiply,
    Divide,
    Modulo,
    Range,        // ..
    Power,        // **
    Assign,       // =
    PlusAssign,   // +=
    MinusAssign,  // -=
    Equals,       // ==
    NotEquals,    // !=
    Less,
    Greater,
    LessEq,
    GreaterEq,
    Dot,
    OptionalChain,   // ?.
    NullishCoalesce, // ??
    Spread,          // ...
    Pipe,            // |>
    Question,        // ?

    // Delimiters
    Colon,
    Comma,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Arrow,        // =>
    ThinArrow,    // -> (Rueckgabetyp)

    // Special
    Newline,
    Indent,
    Dedent,
    Eof,
}

#[derive(Debug, Clone)]
pub struct Token {
    pub token_type: TokenType,
    pub line: usize,
    pub column: usize,
}

impl Token {
    pub fn new(token_type: TokenType, line: usize, column: usize) -> Self {
        Self { token_type, line, column }
    }
}

/// Zweisprachiges Keyword-Mapping
pub fn keyword_lookup(word: &str) -> Option<TokenType> {
    match word {
        // Deutsch
        "setze" => Some(TokenType::Set),
        "auf" => Some(TokenType::To),
        "wenn" => Some(TokenType::If),
        "sonst" => Some(TokenType::Else),
        "solange" => Some(TokenType::While),
        "für" | "fuer" => Some(TokenType::For),
        "in" => Some(TokenType::In),
        "funktion" => Some(TokenType::Func),
        "gib_zurück" => Some(TokenType::Return),
        "zeige" => Some(TokenType::Show),
        "und" => Some(TokenType::And),
        "oder" => Some(TokenType::Or),
        "nicht" => Some(TokenType::Not),
        "wahr" => Some(TokenType::Boolean(true)),
        "falsch" => Some(TokenType::Boolean(false)),
        "nichts" => Some(TokenType::None),
        "klasse" => Some(TokenType::Class),
        "neu" => Some(TokenType::New),
        "selbst" => Some(TokenType::This),
        "versuche" => Some(TokenType::Try),
        "fange" => Some(TokenType::Catch),
        "wirf" => Some(TokenType::Throw),
        "stopp" => Some(TokenType::Break),
        "weiter" => Some(TokenType::Continue),
        "konstante" => Some(TokenType::Const),
        "prüfe" => Some(TokenType::Match),
        "fall" => Some(TokenType::Case),
        "standard" => Some(TokenType::Default),
        "importiere" => Some(TokenType::Import),
        "aus" => Some(TokenType::From),
        "exportiere" => Some(TokenType::Export),
        "als" => Some(TokenType::As),
        "aufräumen" => Some(TokenType::Defer),
        "daten" => Some(TokenType::Data),
        "garantiere" => Some(TokenType::Guard),
        "schnittstelle" => Some(TokenType::Interface),
        "implementiert" => Some(TokenType::Implements),
        "unsicher" => Some(TokenType::Unsafe),

        // Lern-Modus: ausfuehrliche Keywords fuer Anfaenger
        "setze_variable" => Some(TokenType::Set),
        "zeige_auf_bildschirm" => Some(TokenType::Show),
        "wenn_bedingung" => Some(TokenType::If),
        "sonst_alternative" => Some(TokenType::Else),
        "solange_wiederhole" => Some(TokenType::While),
        "fuer_jedes" => Some(TokenType::For),
        "funktion_definiere" => Some(TokenType::Func),
        "gib_wert_zurück" => Some(TokenType::Return),
        "neue_klasse" => Some(TokenType::Class),
        "importiere_modul" => Some(TokenType::Import),
        "versuche_ausfuehrung" => Some(TokenType::Try),
        "fange_fehler" => Some(TokenType::Catch),
        "teste" => Some(TokenType::Test),
        "erwarte" => Some(TokenType::Expect),
        "parallel" => Some(TokenType::Parallel),
        "vorbedingung" => Some(TokenType::Precondition),
        "nachbedingung" => Some(TokenType::Postcondition),
        "von" => Some(TokenType::QueryFrom),
        "wo" => Some(TokenType::Where),
        "wähle" => Some(TokenType::Select),
        "sortiere" => Some(TokenType::Order),

        // Experten-Kurzformen (2 Buchstaben)
        "se" => Some(TokenType::Set),
        "ze" => Some(TokenType::Show),
        "we" => Some(TokenType::If),
        "so" => Some(TokenType::Else),
        "sl" => Some(TokenType::While),
        "fu" => Some(TokenType::For),
        "fn" => Some(TokenType::Func),
        "kl" => Some(TokenType::Class),
        "gr" => Some(TokenType::Return),
        "ko" => Some(TokenType::Const),
        "st" => Some(TokenType::Break),
        "wt" => Some(TokenType::Continue),
        "im" => Some(TokenType::Import),
        "pr" => Some(TokenType::Match),
        "fa" => Some(TokenType::Case),
        "ve" => Some(TokenType::Try),
        // "fg" entfernt — kollidiert mit haeufigem Variablenname (foreground,
        // file-grep, frame-grab). 'fange' bzw. 'catch' ausgeschrieben verwenden.
        "wi" => Some(TokenType::Throw),
        "un" => Some(TokenType::Unsafe),
        // "wa" => Await existiert nicht im Rust-Compiler Token-Enum

        // English
        "set" => Some(TokenType::Set),
        "to" => Some(TokenType::To),
        "if" => Some(TokenType::If),
        "else" => Some(TokenType::Else),
        "while" => Some(TokenType::While),
        "for" => Some(TokenType::For),
        "func" => Some(TokenType::Func),
        "return" => Some(TokenType::Return),
        "show" => Some(TokenType::Show),
        "and" => Some(TokenType::And),
        "or" => Some(TokenType::Or),
        "not" => Some(TokenType::Not),
        "true" => Some(TokenType::Boolean(true)),
        "false" => Some(TokenType::Boolean(false)),
        "none" => Some(TokenType::None),
        "class" => Some(TokenType::Class),
        "new" => Some(TokenType::New),
        "this" => Some(TokenType::This),
        "try" => Some(TokenType::Try),
        "catch" => Some(TokenType::Catch),
        "throw" => Some(TokenType::Throw),
        "break" => Some(TokenType::Break),
        "continue" => Some(TokenType::Continue),
        "const" => Some(TokenType::Const),
        "match" => Some(TokenType::Match),
        "case" => Some(TokenType::Case),
        "default" => Some(TokenType::Default),
        "import" => Some(TokenType::Import),
        "from" => Some(TokenType::From),
        "export" => Some(TokenType::Export),
        "as" => Some(TokenType::As),
        "defer" => Some(TokenType::Defer),
        "data" => Some(TokenType::Data),
        "guard" => Some(TokenType::Guard),
        "interface" => Some(TokenType::Interface),
        "implements" => Some(TokenType::Implements),
        "unsafe" => Some(TokenType::Unsafe),
        "test" => Some(TokenType::Test),
        "expect" => Some(TokenType::Expect),
        "precondition" => Some(TokenType::Precondition),
        "postcondition" => Some(TokenType::Postcondition),
        "where" => Some(TokenType::Where),
        "select" => Some(TokenType::Select),
        "order" => Some(TokenType::Order),

        _ => Option::None,
    }
}
