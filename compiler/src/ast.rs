/// AST-Knoten für moo — sprachunabhängige Programmstruktur.

#[derive(Debug, Clone)]
pub enum Expr {
    Number(f64),
    String(std::string::String),
    Boolean(bool),
    None,
    Identifier(std::string::String),
    BinaryOp {
        left: Box<Expr>,
        op: BinOp,
        right: Box<Expr>,
    },
    UnaryOp {
        op: UnaryOpKind,
        operand: Box<Expr>,
    },
    FunctionCall {
        name: std::string::String,
        args: Vec<Expr>,
    },
    MethodCall {
        object: Box<Expr>,
        method: std::string::String,
        args: Vec<Expr>,
    },
    PropertyAccess {
        object: Box<Expr>,
        property: std::string::String,
    },
    IndexAccess {
        object: Box<Expr>,
        index: Box<Expr>,
    },
    Range {
        start: Box<Expr>,
        end: Box<Expr>,
    },
    List(Vec<Expr>),
    ListComprehension {
        expr: Box<Expr>,
        var_name: std::string::String,
        iterable: Box<Expr>,
        condition: Option<Box<Expr>>,
    },
    Dict(Vec<(Expr, Expr)>),
    New {
        class_name: std::string::String,
        args: Vec<Expr>,
    },
    This,
    Lambda {
        params: Vec<std::string::String>,
        body: Box<Expr>,
    },
    OptionalChain {
        object: Box<Expr>,
        property: std::string::String,
    },
    NullishCoalesce {
        left: Box<Expr>,
        right: Box<Expr>,
    },
    Spread(Box<Expr>),
    Pipe {
        left: Box<Expr>,
        right: Box<Expr>,
    },
    /// Ternary: bedingung ? dann_wert : sonst_wert
    Ternary {
        condition: Box<Expr>,
        then_val: Box<Expr>,
        else_val: Box<Expr>,
    },
    /// Match als Expression (Kotlin when): gibt einen Wert zurück
    MatchExpr {
        value: Box<Expr>,
        /// (pattern, guard, result_expr)
        cases: Vec<(Option<Expr>, Option<Expr>, Expr)>,
    },
    /// LINQ Query: von x in quelle [wo bedingung] [sortiere expr] wähle expr
    QueryExpr {
        var_name: std::string::String,
        source: Box<Expr>,
        where_cond: Option<Box<Expr>>,
        order_expr: Option<Box<Expr>>,
        select_expr: Box<Expr>,
    },
}

#[derive(Debug, Clone)]
pub enum BinOp {
    Add, Sub, Mul, Div, Mod, Pow,
    Eq, NotEq, Less, Greater, LessEq, GreaterEq,
    And, Or,
    BitAnd, BitOr, BitXor, LShift, RShift,
}

#[derive(Debug, Clone)]
pub enum UnaryOpKind {
    Neg,
    Not,
    BitNot,
}

#[derive(Debug, Clone)]
pub enum Stmt {
    Assignment {
        name: std::string::String,
        value: Expr,
    },
    ConstAssignment {
        name: std::string::String,
        value: Expr,
    },
    CompoundAssignment {
        name: std::string::String,
        op: std::string::String,
        value: Expr,
    },
    PropertyAssignment {
        object: Expr,
        property: std::string::String,
        value: Expr,
    },
    IndexAssignment {
        object: Expr,
        index: Expr,
        value: Expr,
    },
    Show(Expr),
    If {
        condition: Expr,
        body: Vec<Stmt>,
        else_body: Vec<Stmt>,
    },
    While {
        condition: Expr,
        body: Vec<Stmt>,
    },
    For {
        var_name: std::string::String,
        iterable: Expr,
        body: Vec<Stmt>,
    },
    /// Fortran-inspiriert: parallel für i in 0..N: body
    ParallelFor {
        var_name: std::string::String,
        iterable: Expr,
        body: Vec<Stmt>,
    },
    /// Ada Contracts: vorbedingung expr, "msg" / nachbedingung expr, "msg"
    Precondition {
        condition: Expr,
        message: std::string::String,
    },
    Postcondition {
        condition: Expr,
        message: std::string::String,
    },
    FunctionDef {
        name: std::string::String,
        params: Vec<std::string::String>,
        defaults: Vec<Option<Expr>>,
        body: Vec<Stmt>,
        decorators: Vec<std::string::String>,
    },
    Return(Option<Expr>),
    Break,
    Continue,
    ClassDef {
        name: std::string::String,
        parent: Option<std::string::String>,
        interfaces: Vec<std::string::String>,
        body: Vec<Stmt>,
    },
    /// Interface/Trait (Java/Rust-inspiriert): nur Methodennamen, kein Body
    InterfaceDef {
        name: std::string::String,
        methods: Vec<std::string::String>,
    },
    TryCatch {
        try_body: Vec<Stmt>,
        catch_var: Option<std::string::String>,
        catch_body: Vec<Stmt>,
    },
    Throw(Expr),
    Import {
        module: std::string::String,
        names: Vec<std::string::String>,
        alias: Option<std::string::String>,
    },
    Export(Box<Stmt>),
    Match {
        value: Expr,
        /// (pattern, guard, body) — pattern=None ist default, guard ist optionale wenn/if-Bedingung
        cases: Vec<(Option<Expr>, Option<Expr>, Vec<Stmt>)>,
    },
    Expression(Expr),
    /// Defer-Statement (Go-inspiriert): aufräumen: ausdruck / defer: ausdruck
    /// Wird am Funktionsende (vor Return) ausgeführt, LIFO-Reihenfolge
    Defer(Expr),
    /// Guard-Statement (Swift-inspiriert): garantiere bedingung, sonst: body
    Guard {
        condition: Expr,
        else_body: Vec<Stmt>,
    },
    /// Data-Klasse (Kotlin-inspiriert): daten klasse Name(feld1, feld2)
    DataClassDef {
        name: std::string::String,
        fields: Vec<std::string::String>,
    },
    /// Unsafe-Block (Rust-inspiriert): unsicher: body
    UnsafeBlock {
        body: Vec<Stmt>,
    },
}

#[derive(Debug, Clone)]
pub struct Program {
    pub statements: Vec<Stmt>,
}
