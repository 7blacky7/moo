/// AST-Knoten für moo — sprachunabhängige Programmstruktur.

/// Quellort und verlustfrei erhaltene Struktur einer Typannotation.
#[derive(Debug, Clone)]
pub struct TypeRef {
    pub kind: TypeKind,
    pub line: usize,
    pub column: usize,
    /// Exklusives Ende der letzten Typ-Komponente (1-basiert).
    pub end_line: usize,
    pub end_column: usize,
}

#[derive(Debug, Clone)]
pub enum TypeKind {
    Name(std::string::String),
    Generic { name: std::string::String, args: Vec<TypeRef> },
    Array { element: Box<TypeRef>, length: Box<Expr> },
    /// `fn(...) -> T` bzw. mit Aufrufkonvention `fn[win64](...) -> T` (P7-T4, 7A).
    Function { params: Vec<TypeRef>, return_type: Box<TypeRef>, convention: Option<std::string::String> },
}

#[derive(Debug, Clone)]
pub enum Expr {
    Number(f64),
    /// Unverändertes Quellliteral eines explizit nativen Funktionskörpers.
    NativeNumber { legacy: f64, text: std::string::String, line: usize, column: usize },
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
        param_types: Vec<Option<TypeRef>>,
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

/// Decorator mit optionalen Literal-Argumenten: @name oder @name(arg, ...)
/// P011-A1: getrennter Typ statt blossem String — "weniger Magie an den tiefsten Stellen".
#[derive(Debug, Clone)]
pub struct Decorator {
    pub name: std::string::String,
    pub args: Vec<DecoratorArg>,
}

#[derive(Debug, Clone)]
pub enum DecoratorArg {
    Zahl(f64),
    Text(std::string::String),
    NamedText { name: std::string::String, value: std::string::String },
}

/// Strukturierte Interfacesignatur, soweit die alte Syntax erkennbar ist.
#[derive(Debug, Clone)]
pub struct InterfaceSignature {
    pub name: std::string::String,
    pub params: Vec<std::string::String>,
    pub param_types: Vec<Option<TypeRef>>,
    pub defaults: Vec<Option<Expr>>,
    pub return_type: Option<TypeRef>,
}

#[derive(Debug, Clone)]
pub enum Stmt {
    Assignment {
        name: std::string::String,
        ty: Option<TypeRef>,
        value: Expr,
    },
    ConstAssignment {
        name: std::string::String,
        ty: Option<TypeRef>,
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
        param_types: Vec<Option<TypeRef>>,
        return_type: Option<TypeRef>,
        defaults: Vec<Option<Expr>>,
        body: Vec<Stmt>,
        decorators: Vec<Decorator>,
    },
    /// Fremdfunktion mit expliziter C-Signatur; Linknamen bleiben unveraendert.
    ExternC {
        name: std::string::String,
        params: Vec<(std::string::String, TypeRef)>,
        result: TypeRef,
        decorators: Vec<Decorator>,
    },
    /// P011-A3 PoC: reines u32-Daten-Global (kein MooValue) — fuer
    /// Layout-kritische Daten wie den Multiboot2-Header. Werte muessen
    /// compile-time-konstante Ausdruecke sein (eval_const_f64 im Codegen).
    RawDataU32 {
        name: std::string::String,
        ty: Option<TypeRef>,
        value: Expr,
        decorators: Vec<Decorator>,
    },
    /// P7-T4 (1A): `@nativ konstante NAME: T = wert` als konstantes Datenglobal
    /// mit exaktem Ziel-ABI-Layout (kein MooValue), siehe native_const.rs.
    NativeConst {
        name: std::string::String,
        ty: TypeRef,
        value: Expr,
        decorators: Vec<Decorator>,
    },
    Return(Option<Expr>),
    Break,
    Continue,
    /// Native Strukturdefinition; Feldreihenfolge ist Bestandteil des Layouts.
    StructDef {
        name: std::string::String,
        fields: Vec<(std::string::String, TypeRef)>,
        decorators: Vec<Decorator>,
    },
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
        signatures: Vec<Option<InterfaceSignature>>,
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
        /// Quellzeile der Importanweisung (Diagnose mit Herkunft, P2-T47).
        line: usize,
    },
    Export(Box<Stmt>),
    /// `global a, b` in einer Funktion: Zuweisungen an a/b gehen an die
    /// Top-Level-Variable statt eine lokale Variable anzulegen (ERDE-13).
    Global(Vec<std::string::String>),
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
        field_types: Vec<Option<TypeRef>>,
    },
    /// Unsafe-Block (Rust-inspiriert): unsicher: body
    UnsafeBlock {
        body: Vec<Stmt>,
    },
    /// Test-Block: teste "name": body mit erwarte-Statements
    TestDef {
        name: std::string::String,
        body: Vec<Stmt>,
    },
    /// Erwarte-Statement: erwarte expr (assertion)
    Expect(Expr),
}

#[derive(Debug, Clone)]
pub struct Program {
    pub statements: Vec<Stmt>,
}
