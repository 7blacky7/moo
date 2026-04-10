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
        "für" => Some(TokenType::For),
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
        "parallel" => Some(TokenType::Parallel),
        "vorbedingung" => Some(TokenType::Precondition),
        "nachbedingung" => Some(TokenType::Postcondition),
        "von" => Some(TokenType::QueryFrom),
        "wo" => Some(TokenType::Where),
        "wähle" => Some(TokenType::Select),
        "sortiere" => Some(TokenType::Order),

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
        "precondition" => Some(TokenType::Precondition),
        "postcondition" => Some(TokenType::Postcondition),
        "where" => Some(TokenType::Where),
        "select" => Some(TokenType::Select),
        "order" => Some(TokenType::Order),

        // Español
        "establecer" => Some(TokenType::Set),    // setze
        "en" => Some(TokenType::In),             // in (para x en lista)
        // KEIN "a" als To — kollidiert mit Variable "a" (1-Buchstaben-Keyword zu riskant)
        "si" => Some(TokenType::If),             // wenn
        "sino" => Some(TokenType::Else),         // sonst
        "mientras" => Some(TokenType::While),    // solange
        "para" => Some(TokenType::For),          // für
        "funcion" => Some(TokenType::Func),      // funktion
        "función" => Some(TokenType::Func),      // funktion (mit Akzent)
        "retornar" => Some(TokenType::Return),   // gib_zurück
        "mostrar" => Some(TokenType::Show),      // zeige
        "no" => Some(TokenType::Not),            // nicht
        "verdadero" => Some(TokenType::Boolean(true)),
        "falso" => Some(TokenType::Boolean(false)),
        "nulo" => Some(TokenType::None),         // nichts
        "clase" => Some(TokenType::Class),       // klasse
        "nuevo" => Some(TokenType::New),         // neu
        "intentar" => Some(TokenType::Try),      // versuche
        "capturar" => Some(TokenType::Catch),    // fange
        "lanzar" => Some(TokenType::Throw),      // wirf
        "romper" => Some(TokenType::Break),      // stopp
        "continuar" => Some(TokenType::Continue),// weiter
        "constante" => Some(TokenType::Const),   // konstante
        "importar" => Some(TokenType::Import),   // importiere
        "desde" => Some(TokenType::From),        // aus
        "exportar" => Some(TokenType::Export),   // exportiere
        "como" => Some(TokenType::As),           // als
        "datos" => Some(TokenType::Data),        // daten

        // Français
        "définir" => Some(TokenType::Set),       // setze
        "definir" => Some(TokenType::Set),       // setze (ohne Akzent)
        // "si" → bereits als ES/IF registriert
        "sinon" => Some(TokenType::Else),        // sonst
        "tantque" => Some(TokenType::While),     // solange
        "pour" => Some(TokenType::For),          // für
        "fonction" => Some(TokenType::Func),     // funktion
        "retourner" => Some(TokenType::Return),  // gib_zurück
        "afficher" => Some(TokenType::Show),     // zeige
        "et" => Some(TokenType::And),            // und
        "ou" => Some(TokenType::Or),             // oder
        "pas" => Some(TokenType::Not),           // nicht
        "vrai" => Some(TokenType::Boolean(true)),
        "faux" => Some(TokenType::Boolean(false)),
        "rien" => Some(TokenType::None),         // nichts
        "classe" => Some(TokenType::Class),      // klasse
        "nouveau" => Some(TokenType::New),       // neu
        "essayer" => Some(TokenType::Try),       // versuche
        "attraper" => Some(TokenType::Catch),    // fange
        "lancer" => Some(TokenType::Throw),      // wirf
        "arrêter" => Some(TokenType::Break),     // stopp
        "importer" => Some(TokenType::Import),   // importiere
        "depuis" => Some(TokenType::From),       // aus
        "exporter" => Some(TokenType::Export),   // exportiere
        "données" => Some(TokenType::Data),      // daten

        // Türkçe
        "ayarla" => Some(TokenType::Set),        // setze
        "eğer" => Some(TokenType::If),           // wenn
        "değilse" => Some(TokenType::Else),      // sonst
        "iken" => Some(TokenType::While),        // solange
        "için" => Some(TokenType::For),          // für
        "fonksiyon" => Some(TokenType::Func),    // funktion
        "döndür" => Some(TokenType::Return),     // gib_zurück
        "göster" => Some(TokenType::Show),       // zeige
        "ve" => Some(TokenType::And),            // und
        "veya" => Some(TokenType::Or),           // oder
        "değil" => Some(TokenType::Not),         // nicht
        "doğru" => Some(TokenType::Boolean(true)),
        "yanlış" => Some(TokenType::Boolean(false)),
        "boş" => Some(TokenType::None),          // nichts
        "sınıf" => Some(TokenType::Class),       // klasse
        "yeni" => Some(TokenType::New),          // neu
        "dene" => Some(TokenType::Try),          // versuche
        "yakala" => Some(TokenType::Catch),      // fange
        "fırlat" => Some(TokenType::Throw),      // wirf
        "dur" => Some(TokenType::Break),         // stopp
        "devam" => Some(TokenType::Continue),    // weiter
        "sabit" => Some(TokenType::Const),       // konstante
        "içeaktar" => Some(TokenType::Import),   // importiere
        "dışaaktar" => Some(TokenType::Export),  // exportiere
        "veri" => Some(TokenType::Data),         // daten

        _ => Option::None,
    }
}
