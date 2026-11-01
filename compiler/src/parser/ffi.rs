//! Kontextuelle extern-C-Deklaration. Dieses Modul wird erst gemeinsam mit
//! Stmt::ExternC und den Codegen-Diagnosen eingebunden.

use super::*;

impl Parser {
    pub(super) fn parse_extern_c(&mut self, decorators: Vec<Decorator>) -> Result<Stmt, ParseError> {
        self.pos += 1; // kontextuelles Identifier-Token "extern"
        match self.current_type() {
            TokenType::String(abi) if abi == "C" => self.pos += 1,
            _ => return Err(ParseError {
                message: "extern erwartet die ABI-Bezeichnung \"C\"".to_string(),
                line: self.current().line,
            }),
        }
        self.eat(&TokenType::Func)?;
        let name = self.eat_identifier()?;
        self.eat(&TokenType::LParen)?;
        let mut params = Vec::new();
        if !matches!(self.current_type(), TokenType::RParen) {
            loop {
                if matches!(self.current_type(), TokenType::Spread) {
                    return Err(ParseError {
                        message: "Variadische extern-C-Signaturen sind nicht unterstützt".to_string(),
                        line: self.current().line,
                    });
                }
                let param_name = self.eat_identifier()?;
                if params.iter().any(|(existing, _)| existing == &param_name) {
                    return Err(ParseError {
                        message: format!("Doppelter extern-Parameter '{param_name}'"),
                        line: self.current().line,
                    });
                }
                self.eat(&TokenType::Colon)?;
                let param_type = self.parse_type()?;
                params.push((param_name, param_type));
                if !matches!(self.current_type(), TokenType::Comma) { break; }
                self.pos += 1;
            }
        }
        self.eat(&TokenType::RParen)?;
        self.eat(&TokenType::ThinArrow)?;
        let result = self.parse_type()?;
        Ok(Stmt::ExternC { name, params, result, decorators })
    }
}
