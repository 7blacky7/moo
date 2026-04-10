/// Lexer für moo — wandelt Quelltext in Tokens um.

use crate::tokens::{Token, TokenType, keyword_lookup};

#[derive(Debug)]
pub struct LexerError {
    pub message: String,
    pub line: usize,
    pub column: usize,
}

impl std::fmt::Display for LexerError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Zeile {}, Spalte {}: {}", self.line, self.column, self.message)
    }
}

pub struct Lexer {
    indent_stack: Vec<usize>,
}

impl Lexer {
    pub fn new() -> Self {
        Self {
            indent_stack: vec![0],
        }
    }

    pub fn tokenize(&mut self, source: &str) -> Result<Vec<Token>, LexerError> {
        let mut tokens = Vec::new();

        for (line_num, line_text) in source.lines().enumerate() {
            let line = line_num + 1;
            let stripped = line_text.trim_start();

            // Leerzeilen und Kommentare überspringen
            if stripped.is_empty() || stripped.starts_with('#') {
                continue;
            }

            // Indentation
            let indent = line_text.len() - stripped.len();
            self.handle_indent(indent, line, &mut tokens);

            // Zeile tokenisieren
            self.tokenize_line(stripped, line, indent, &mut tokens)?;
            tokens.push(Token::new(TokenType::Newline, line, 0));
        }

        // Alle offenen Indents schließen
        while self.indent_stack.len() > 1 {
            self.indent_stack.pop();
            let line = tokens.last().map(|t| t.line).unwrap_or(1);
            tokens.push(Token::new(TokenType::Dedent, line, 0));
        }

        let line = tokens.last().map(|t| t.line).unwrap_or(1);
        tokens.push(Token::new(TokenType::Eof, line, 0));
        Ok(tokens)
    }

    fn handle_indent(&mut self, indent: usize, line: usize, tokens: &mut Vec<Token>) {
        let current = *self.indent_stack.last().unwrap();
        if indent > current {
            self.indent_stack.push(indent);
            tokens.push(Token::new(TokenType::Indent, line, 0));
        } else {
            while indent < *self.indent_stack.last().unwrap() {
                self.indent_stack.pop();
                tokens.push(Token::new(TokenType::Dedent, line, 0));
            }
        }
    }

    fn tokenize_line(
        &self,
        line: &str,
        line_num: usize,
        base_col: usize,
        tokens: &mut Vec<Token>,
    ) -> Result<(), LexerError> {
        let chars: Vec<char> = line.chars().collect();
        let mut i = 0;

        while i < chars.len() {
            let ch = chars[i];
            let col = base_col + i + 1;

            // Whitespace
            if ch == ' ' || ch == '\t' {
                i += 1;
                continue;
            }

            // Kommentar
            if ch == '#' {
                break;
            }

            // String
            if ch == '"' || ch == '\'' {
                let (tok, end) = self.read_string(&chars, i, line_num, col)?;
                tokens.push(tok);
                i = end;
                continue;
            }

            // Zahl
            if ch.is_ascii_digit() {
                let (tok, end) = self.read_number(&chars, i, line_num, col);
                tokens.push(tok);
                i = end;
                continue;
            }

            // f-String: f"..." oder f'...'
            if ch == 'f' && i + 1 < chars.len() && (chars[i + 1] == '"' || chars[i + 1] == '\'') {
                let end = self.read_fstring(&chars, i + 1, line_num, col, tokens)?;
                i = end;
                continue;
            }

            // Identifier / Keyword
            if ch.is_alphabetic() || ch == '_' {
                let (tok, end) = self.read_identifier(&chars, i, line_num, col);
                tokens.push(tok);
                i = end;
                continue;
            }

            // Zwei-Zeichen-Operatoren
            // Drei-Zeichen-Operatoren (vor Zwei-Zeichen!)
            if i + 2 < chars.len() && chars[i] == '.' && chars[i + 1] == '.' && chars[i + 2] == '.' {
                tokens.push(Token::new(TokenType::Spread, line_num, col));
                i += 3;
                continue;
            }

            if i + 1 < chars.len() {
                let two: String = chars[i..=i + 1].iter().collect();
                let op = match two.as_str() {
                    "==" => Some(TokenType::Equals),
                    "!=" => Some(TokenType::NotEquals),
                    "<=" => Some(TokenType::LessEq),
                    ">=" => Some(TokenType::GreaterEq),
                    ".." => Some(TokenType::Range),
                    "**" => Some(TokenType::Power),
                    "+=" => Some(TokenType::PlusAssign),
                    "-=" => Some(TokenType::MinusAssign),
                    "=>" => Some(TokenType::Arrow),
                    "?." => Some(TokenType::OptionalChain),
                    "??" => Some(TokenType::NullishCoalesce),
                    "|>" => Some(TokenType::Pipe),
                    _ => Option::None,
                };
                if let Some(token_type) = op {
                    tokens.push(Token::new(token_type, line_num, col));
                    i += 2;
                    continue;
                }
            }

            // Ein-Zeichen-Operatoren
            let single = match ch {
                '+' => Some(TokenType::Plus),
                '-' => Some(TokenType::Minus),
                '*' => Some(TokenType::Multiply),
                '/' => Some(TokenType::Divide),
                '%' => Some(TokenType::Modulo),
                '=' => Some(TokenType::Assign),
                '<' => Some(TokenType::Less),
                '>' => Some(TokenType::Greater),
                ':' => Some(TokenType::Colon),
                ',' => Some(TokenType::Comma),
                '(' => Some(TokenType::LParen),
                ')' => Some(TokenType::RParen),
                '[' => Some(TokenType::LBracket),
                ']' => Some(TokenType::RBracket),
                '{' => Some(TokenType::LBrace),
                '}' => Some(TokenType::RBrace),
                '.' => Some(TokenType::Dot),
                _ => Option::None,
            };

            if let Some(token_type) = single {
                tokens.push(Token::new(token_type, line_num, col));
                i += 1;
            } else {
                return Err(LexerError {
                    message: format!("Unbekanntes Zeichen: '{ch}'"),
                    line: line_num,
                    column: col,
                });
            }
        }

        Ok(())
    }

    fn read_string(
        &self,
        chars: &[char],
        start: usize,
        line: usize,
        col: usize,
    ) -> Result<(Token, usize), LexerError> {
        let quote = chars[start];
        let mut i = start + 1;
        let mut result = String::new();

        while i < chars.len() && chars[i] != quote {
            if chars[i] == '\\' && i + 1 < chars.len() {
                let escaped = match chars[i + 1] {
                    'n' => '\n',
                    't' => '\t',
                    '\\' => '\\',
                    '"' => '"',
                    '\'' => '\'',
                    other => other,
                };
                result.push(escaped);
                i += 2;
            } else {
                result.push(chars[i]);
                i += 1;
            }
        }

        if i >= chars.len() {
            return Err(LexerError {
                message: "String nicht geschlossen".to_string(),
                line,
                column: col,
            });
        }

        i += 1; // Schließendes Anführungszeichen
        Ok((Token::new(TokenType::String(result), line, col), i))
    }

    fn read_number(&self, chars: &[char], start: usize, line: usize, col: usize) -> (Token, usize) {
        let mut i = start;
        let mut has_dot = false;

        while i < chars.len() && (chars[i].is_ascii_digit() || (chars[i] == '.' && !has_dot)) {
            if chars[i] == '.' {
                // ".." ist Range-Operator, kein Dezimalpunkt
                if i + 1 < chars.len() && chars[i + 1] == '.' {
                    break;
                }
                has_dot = true;
            }
            i += 1;
        }

        let num_str: String = chars[start..i].iter().collect();
        let value: f64 = num_str.parse().unwrap();
        (Token::new(TokenType::Number(value), line, col), i)
    }

    fn read_identifier(&self, chars: &[char], start: usize, line: usize, col: usize) -> (Token, usize) {
        let mut i = start;
        while i < chars.len() && (chars[i].is_alphanumeric() || chars[i] == '_'
            || "äöüÄÖÜß".contains(chars[i]))
        {
            i += 1;
        }

        let word: String = chars[start..i].iter().collect();

        let token_type = keyword_lookup(&word)
            .unwrap_or(TokenType::Identifier(word));

        (Token::new(token_type, line, col), i)
    }

    fn read_fstring(
        &self,
        chars: &[char],
        start: usize,
        line: usize,
        col: usize,
        tokens: &mut Vec<Token>,
    ) -> Result<usize, LexerError> {
        let quote = chars[start];
        let mut i = start + 1;
        // Collect parts: (string_content, is_expr)
        let mut parts: Vec<(String, bool)> = Vec::new();
        let mut current = String::new();

        while i < chars.len() && chars[i] != quote {
            if chars[i] == '{' {
                if !current.is_empty() {
                    parts.push((current.clone(), false));
                    current.clear();
                }
                i += 1;
                let mut depth = 1;
                let mut expr = String::new();
                while i < chars.len() && depth > 0 {
                    if chars[i] == '{' {
                        depth += 1;
                    } else if chars[i] == '}' {
                        depth -= 1;
                        if depth == 0 {
                            break;
                        }
                    }
                    expr.push(chars[i]);
                    i += 1;
                }
                if i >= chars.len() {
                    return Err(LexerError {
                        message: "f-String: '}' fehlt".to_string(),
                        line,
                        column: col,
                    });
                }
                i += 1; // skip }
                parts.push((expr, true));
            } else if chars[i] == '\\' && i + 1 < chars.len() {
                let escaped = match chars[i + 1] {
                    'n' => '\n',
                    't' => '\t',
                    '\\' => '\\',
                    '"' => '"',
                    '\'' => '\'',
                    other => other,
                };
                current.push(escaped);
                i += 2;
            } else {
                current.push(chars[i]);
                i += 1;
            }
        }

        if i >= chars.len() {
            return Err(LexerError {
                message: "f-String nicht geschlossen".to_string(),
                line,
                column: col,
            });
        }
        i += 1; // skip closing quote

        if !current.is_empty() {
            parts.push((current, false));
        }

        if parts.is_empty() {
            tokens.push(Token::new(TokenType::String(String::new()), line, col));
            return Ok(i);
        }

        // Emit tokens: part1 + part2 + ...
        let mut first = true;
        for (content, is_expr) in &parts {
            if !first {
                tokens.push(Token::new(TokenType::Plus, line, col));
            }
            first = false;
            if *is_expr {
                // Emit text(<expr_tokens>)
                tokens.push(Token::new(TokenType::Identifier("text".to_string()), line, col));
                tokens.push(Token::new(TokenType::LParen, line, col));
                // Sub-tokenize the expression
                self.tokenize_line(content, line, 0, tokens)?;
                // Remove the trailing Newline if tokenize_line doesn't add one (it shouldn't since we call tokenize_line directly)
                tokens.push(Token::new(TokenType::RParen, line, col));
            } else {
                tokens.push(Token::new(TokenType::String(content.clone()), line, col));
            }
        }

        Ok(i)
    }
}
