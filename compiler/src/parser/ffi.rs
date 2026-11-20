//! Kontextuelle extern-C-Deklarationen fuer Funktionen und konstante Daten.

use super::*;

impl Parser {
    pub(super) fn parse_extern_c(&mut self, decorators: Vec<Decorator>) -> Result<Stmt, ParseError> {
        self.pos += 1; // kontextuelles Identifier-Token "extern"
        match self.current_type() {
            TokenType::String(abi) if abi == "C" => self.pos += 1,
            _ => return Err(ParseError {
                message: "extern erwartet die ABI-Bezeichnung \"C\"".to_string(),
                ..ParseError::bei(self.current())
            }),
        }
        if matches!(self.current_type(), TokenType::Const) {
            return self.parse_extern_c_const(decorators);
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
                        ..ParseError::bei(self.current())
                    });
                }
                let param_name = self.eat_identifier()?;
                if params.iter().any(|(existing, _)| existing == &param_name) {
                    return Err(ParseError {
                        message: format!("Doppelter extern-Parameter '{param_name}'"),
                        ..ParseError::bei(self.current())
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
        Ok(Stmt::ExternC { zeile: 0, name, params, result, decorators })
    }

    fn parse_extern_c_const(&mut self, decorators: Vec<Decorator>) -> Result<Stmt, ParseError> {
        self.pos += 1; // konstante/const
        let name = self.eat_identifier()?;
        self.eat(&TokenType::Colon)?;
        let previous_native = self.native_context;
        self.native_context = true;
        let ty = self.parse_type();
        self.native_context = previous_native;
        let ty = ty?;
        if matches!(self.current_type(), TokenType::To | TokenType::Assign) {
            return Err(ParseError {
                message: format!("Externe C-Konstante '{name}' darf keinen Initialisierer haben"),
                ..ParseError::bei(self.current())
            });
        }
        if !matches!(self.current_type(), TokenType::Newline | TokenType::Dedent | TokenType::Eof) {
            return Err(ParseError {
                message: "Zeilenende nach externer C-Konstante erwartet".to_string(),
                ..ParseError::bei(self.current())
            });
        }
        Ok(Stmt::NativeConst { zeile: 0, name, ty, value: None, decorators })
    }
}
