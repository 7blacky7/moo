/// Parser für moo — wandelt Tokens in einen AST um.

use crate::ast::*;
use crate::tokens::{Token, TokenType};

#[derive(Debug)]
pub struct ParseError {
    pub message: String,
    pub line: usize,
}

impl std::fmt::Display for ParseError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Zeile {}: {}", self.line, self.message)
    }
}

pub struct Parser {
    tokens: Vec<Token>,
    pos: usize,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        Self { tokens, pos: 0 }
    }

    pub fn parse(&mut self) -> Result<Program, ParseError> {
        let statements = self.parse_block_body(true)?;
        Ok(Program { statements })
    }

    // --- Hilfsfunktionen ---

    fn current(&self) -> &Token {
        &self.tokens[self.pos]
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
            Err(ParseError {
                message: format!("Erwartet {:?}, bekommen {:?}", expected, self.current_type()),
                line: self.current().line,
            })
        }
    }

    fn eat_identifier(&mut self) -> Result<String, ParseError> {
        if let TokenType::Identifier(name) = self.current_type().clone() {
            self.pos += 1;
            Ok(name)
        } else {
            Err(ParseError {
                message: format!("Bezeichner erwartet, bekommen {:?}", self.current_type()),
                line: self.current().line,
            })
        }
    }

    fn skip_newlines(&mut self) {
        while matches!(self.current_type(), TokenType::Newline) {
            self.pos += 1;
        }
    }

    fn parse_block_body(&mut self, top_level: bool) -> Result<Vec<Stmt>, ParseError> {
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
                _ => stmts.push(self.parse_statement()?),
            }
        }

        Ok(stmts)
    }

    // --- Statements ---

    fn parse_statement(&mut self) -> Result<Stmt, ParseError> {
        match self.current_type().clone() {
            TokenType::Set => self.parse_assignment(),
            TokenType::Const => self.parse_const_assignment(),
            TokenType::Show => self.parse_show(),
            TokenType::If => self.parse_if(),
            TokenType::While => self.parse_while(),
            TokenType::For => self.parse_for(),
            TokenType::At => self.parse_decorated_function(),
            TokenType::Func => self.parse_function_def_with_decorators(vec![]),
            TokenType::Data => self.parse_data_class(),
            TokenType::Return => self.parse_return(),
            TokenType::Break => { self.pos += 1; Ok(Stmt::Break) }
            TokenType::Continue => { self.pos += 1; Ok(Stmt::Continue) }
            TokenType::Class => self.parse_class_def(),
            TokenType::Try => self.parse_try_catch(),
            TokenType::Throw => self.parse_throw(),
            TokenType::Import => self.parse_import(),
            TokenType::From => self.parse_from_import(),
            TokenType::Export => self.parse_export(),
            TokenType::Match => self.parse_match(),
            TokenType::Defer => self.parse_defer(),
            TokenType::Guard => self.parse_guard(),
            _ => {
                let expr = self.parse_expression()?;

                // Property-Zuweisung
                if let Expr::PropertyAccess { object, property } = &expr {
                    if matches!(self.current_type(), TokenType::Assign) {
                        self.pos += 1;
                        let value = self.parse_expression()?;
                        return Ok(Stmt::PropertyAssignment {
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
                                name: name.clone(),
                                op: "+=".to_string(),
                                value,
                            });
                        }
                        TokenType::MinusAssign => {
                            self.pos += 1;
                            let value = self.parse_expression()?;
                            return Ok(Stmt::CompoundAssignment {
                                name: name.clone(),
                                op: "-=".to_string(),
                                value,
                            });
                        }
                        _ => {}
                    }
                }

                Ok(Stmt::Expression(expr))
            }
        }
    }

    fn parse_assignment(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // set/setze
        let name = self.eat_identifier()?;
        self.eat(&TokenType::To)?;
        let value = self.parse_expression()?;
        Ok(Stmt::Assignment { name, value })
    }

    fn parse_const_assignment(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // const/konstante
        let name = self.eat_identifier()?;
        self.eat(&TokenType::To)?;
        let value = self.parse_expression()?;
        Ok(Stmt::ConstAssignment { name, value })
    }

    fn parse_show(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let value = self.parse_expression()?;
        Ok(Stmt::Show(value))
    }

    fn parse_if(&mut self) -> Result<Stmt, ParseError> {
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

        Ok(Stmt::If { condition, body, else_body })
    }

    fn parse_while(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let condition = self.parse_expression()?;
        let body = self.parse_block_body(false)?;
        Ok(Stmt::While { condition, body })
    }

    fn parse_for(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let var_name = self.eat_identifier()?;
        self.eat(&TokenType::In)?;
        let iterable = self.parse_expression()?;
        let body = self.parse_block_body(false)?;
        Ok(Stmt::For { var_name, iterable, body })
    }

    fn parse_decorated_function(&mut self) -> Result<Stmt, ParseError> {
        let mut decorators = Vec::new();
        while matches!(self.current_type(), TokenType::At) {
            self.pos += 1; // @
            let name = self.eat_identifier()?;
            decorators.push(name);
            self.skip_newlines();
        }
        if !matches!(self.current_type(), TokenType::Func) {
            return Err(ParseError {
                message: "funktion/func nach Decorator erwartet".to_string(),
                line: self.current().line,
            });
        }
        self.parse_function_def_with_decorators(decorators)
    }

    fn parse_function_def_with_decorators(&mut self, decorators: Vec<String>) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let name = self.eat_identifier()?;
        self.eat(&TokenType::LParen)?;

        let mut params = Vec::new();
        let mut defaults = Vec::new();

        if !matches!(self.current_type(), TokenType::RParen) {
            self.parse_param(&mut params, &mut defaults)?;
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                self.parse_param(&mut params, &mut defaults)?;
            }
        }

        self.eat(&TokenType::RParen)?;
        let body = self.parse_block_body(false)?;

        Ok(Stmt::FunctionDef { name, params, defaults, body, decorators })
    }

    fn parse_data_class(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // data/daten
        // Erwarte "klasse"/"class"
        if !matches!(self.current_type(), TokenType::Class) {
            return Err(ParseError {
                message: "klasse/class nach daten/data erwartet".to_string(),
                line: self.current().line,
            });
        }
        self.pos += 1; // class/klasse
        let name = self.eat_identifier()?;
        self.eat(&TokenType::LParen)?;
        let mut fields = Vec::new();
        if !matches!(self.current_type(), TokenType::RParen) {
            fields.push(self.eat_identifier()?);
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                fields.push(self.eat_identifier()?);
            }
        }
        self.eat(&TokenType::RParen)?;
        Ok(Stmt::DataClassDef { name, fields })
    }

    fn parse_function_def(&mut self) -> Result<Stmt, ParseError> {
        self.parse_function_def_with_decorators(vec![])
    }

    fn parse_param(&mut self, params: &mut Vec<String>, defaults: &mut Vec<Option<Expr>>) -> Result<(), ParseError> {
        let name = self.eat_identifier()?;
        params.push(name);
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
            Ok(Stmt::Return(Option::None))
        } else {
            Ok(Stmt::Return(Some(self.parse_expression()?)))
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
        let body = self.parse_block_body(false)?;
        Ok(Stmt::ClassDef { name, parent, body })
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
        Ok(Stmt::TryCatch { try_body, catch_var, catch_body })
    }

    fn parse_throw(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let value = self.parse_expression()?;
        Ok(Stmt::Throw(value))
    }

    fn parse_import(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let module = self.eat_identifier()?;
        let alias = if matches!(self.current_type(), TokenType::As) {
            self.pos += 1;
            Some(self.eat_identifier()?)
        } else {
            Option::None
        };
        Ok(Stmt::Import { module, names: vec![], alias })
    }

    fn parse_from_import(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let module = self.eat_identifier()?;
        self.eat(&TokenType::Import)?;
        let mut names = vec![self.eat_identifier()?];
        while matches!(self.current_type(), TokenType::Comma) {
            self.pos += 1;
            names.push(self.eat_identifier()?);
        }
        Ok(Stmt::Import { module, names, alias: Option::None })
    }

    fn parse_export(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1;
        let stmt = self.parse_statement()?;
        Ok(Stmt::Export(Box::new(stmt)))
    }

    fn parse_defer(&mut self) -> Result<Stmt, ParseError> {
        self.pos += 1; // defer/aufräumen
        self.eat(&TokenType::Colon)?;
        let expr = self.parse_expression()?;
        Ok(Stmt::Defer(expr))
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
        Ok(Stmt::Guard { condition, else_body })
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
                    line: self.current().line,
                }),
            }
        }

        Ok(Stmt::Match { value, cases })
    }

    // --- Expressions ---

    fn parse_expression(&mut self) -> Result<Expr, ParseError> {
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
        let mut left = self.parse_nullish_coalesce()?;
        while matches!(self.current_type(), TokenType::Pipe) {
            self.pos += 1;
            let right = self.parse_nullish_coalesce()?;
            left = Expr::Pipe { left: Box::new(left), right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_nullish_coalesce(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_or()?;
        while matches!(self.current_type(), TokenType::NullishCoalesce) {
            self.pos += 1;
            let right = self.parse_or()?;
            left = Expr::NullishCoalesce { left: Box::new(left), right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_or(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_and()?;
        while matches!(self.current_type(), TokenType::Or) {
            self.pos += 1;
            let right = self.parse_and()?;
            left = Expr::BinaryOp { left: Box::new(left), op: BinOp::Or, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_and(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_not()?;
        while matches!(self.current_type(), TokenType::And) {
            self.pos += 1;
            let right = self.parse_not()?;
            left = Expr::BinaryOp { left: Box::new(left), op: BinOp::And, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_not(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.current_type(), TokenType::Not) {
            self.pos += 1;
            let operand = self.parse_not()?;
            Ok(Expr::UnaryOp { op: UnaryOpKind::Not, operand: Box::new(operand) })
        } else {
            self.parse_comparison()
        }
    }

    fn parse_comparison(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_addition()?;
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
            let right = self.parse_addition()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_addition(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_multiplication()?;
        // Range: 0..10
        if matches!(self.current_type(), TokenType::Range) {
            self.pos += 1;
            let right = self.parse_multiplication()?;
            return Ok(Expr::Range { start: Box::new(left), end: Box::new(right) });
        }
        loop {
            let op = match self.current_type() {
                TokenType::Plus => BinOp::Add,
                TokenType::Minus => BinOp::Sub,
                _ => break,
            };
            self.pos += 1;
            let right = self.parse_multiplication()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_multiplication(&mut self) -> Result<Expr, ParseError> {
        let mut left = self.parse_power()?;
        loop {
            let op = match self.current_type() {
                TokenType::Multiply => BinOp::Mul,
                TokenType::Divide => BinOp::Div,
                TokenType::Modulo => BinOp::Mod,
                _ => break,
            };
            self.pos += 1;
            let right = self.parse_power()?;
            left = Expr::BinaryOp { left: Box::new(left), op, right: Box::new(right) };
        }
        Ok(left)
    }

    fn parse_power(&mut self) -> Result<Expr, ParseError> {
        let left = self.parse_unary()?;
        if matches!(self.current_type(), TokenType::Power) {
            self.pos += 1;
            let right = self.parse_power()?; // Rechts-assoziativ
            Ok(Expr::BinaryOp { left: Box::new(left), op: BinOp::Pow, right: Box::new(right) })
        } else {
            Ok(left)
        }
    }

    fn parse_unary(&mut self) -> Result<Expr, ParseError> {
        if matches!(self.current_type(), TokenType::Minus) {
            self.pos += 1;
            let operand = self.parse_postfix()?;
            Ok(Expr::UnaryOp { op: UnaryOpKind::Neg, operand: Box::new(operand) })
        } else {
            self.parse_postfix()
        }
    }

    fn parse_postfix(&mut self) -> Result<Expr, ParseError> {
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
        }

        Ok(node)
    }

    fn parse_args_list(&mut self) -> Result<Vec<Expr>, ParseError> {
        let mut args = Vec::new();
        if !matches!(self.current_type(), TokenType::RParen) {
            args.push(self.parse_expression()?);
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                args.push(self.parse_expression()?);
            }
        }
        Ok(args)
    }

    fn parse_primary(&mut self) -> Result<Expr, ParseError> {
        match self.current_type().clone() {
            TokenType::Number(n) => {
                self.pos += 1;
                Ok(Expr::Number(n))
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
                Ok(Expr::This)
            }
            TokenType::New => {
                self.pos += 1;
                let class_name = self.eat_identifier()?;
                self.eat(&TokenType::LParen)?;
                let args = self.parse_args_list()?;
                self.eat(&TokenType::RParen)?;
                Ok(Expr::New { class_name, args })
            }
            TokenType::Identifier(name) => {
                self.pos += 1;
                if matches!(self.current_type(), TokenType::LParen) {
                    self.pos += 1;
                    let args = self.parse_args_list()?;
                    self.eat(&TokenType::RParen)?;
                    Ok(Expr::FunctionCall { name, args })
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
            TokenType::Match => self.parse_match_expr(),
            _ => Err(ParseError {
                message: format!("Ausdruck erwartet, bekommen {:?}", self.current_type()),
                line: self.current().line,
            }),
        }
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
        if !matches!(self.current_type(), TokenType::RParen) {
            params.push(self.eat_identifier()?);
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                params.push(self.eat_identifier()?);
            }
        }
        self.eat(&TokenType::RParen)?;
        self.eat(&TokenType::Arrow)?;
        let body = self.parse_expression()?;
        Ok(Expr::Lambda { params, body: Box::new(body) })
    }

    fn parse_list(&mut self) -> Result<Expr, ParseError> {
        self.eat(&TokenType::LBracket)?;
        let mut elements = Vec::new();
        if !matches!(self.current_type(), TokenType::RBracket) {
            let first = self.parse_list_element()?;
            // List comprehension: [expr für/for x in iterable wenn/if cond]
            // (nur wenn erstes Element kein Spread ist)
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
                if matches!(self.current_type(), TokenType::RBracket) { break; }
                elements.push(self.parse_list_element()?);
            }
        }
        self.eat(&TokenType::RBracket)?;
        Ok(Expr::List(elements))
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
        let mut pairs = Vec::new();
        if !matches!(self.current_type(), TokenType::RBrace) {
            self.parse_dict_entry(&mut pairs)?;
            while matches!(self.current_type(), TokenType::Comma) {
                self.pos += 1;
                if matches!(self.current_type(), TokenType::RBrace) { break; }
                self.parse_dict_entry(&mut pairs)?;
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
                    line: self.current().line,
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
