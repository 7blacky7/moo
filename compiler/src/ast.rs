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
}

#[derive(Debug, Clone)]
pub enum BinOp {
    Add, Sub, Mul, Div, Mod, Pow,
    Eq, NotEq, Less, Greater, LessEq, GreaterEq,
    And, Or,
}

#[derive(Debug, Clone)]
pub enum UnaryOpKind {
    Neg,
    Not,
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
    FunctionDef {
        name: std::string::String,
        params: Vec<std::string::String>,
        defaults: Vec<Option<Expr>>,
        body: Vec<Stmt>,
    },
    Return(Option<Expr>),
    Break,
    Continue,
    ClassDef {
        name: std::string::String,
        parent: Option<std::string::String>,
        body: Vec<Stmt>,
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
        cases: Vec<(Option<Expr>, Vec<Stmt>)>,
    },
    Expression(Expr),
}

#[derive(Debug, Clone)]
pub struct Program {
    pub statements: Vec<Stmt>,
}
