"""Lexer für moo — wandelt Quelltext in Tokens um."""

from .tokens import BOOLEAN_VALUES, KEYWORDS, Token, TokenType


class LexerError(Exception):
    def __init__(self, message: str, line: int, column: int):
        super().__init__(f"Zeile {line}, Spalte {column}: {message}")
        self.line = line
        self.column = column


class Lexer:
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1
        self.column = 1
        self.indent_stack = [0]
        self.tokens: list[Token] = []

    def tokenize(self) -> list[Token]:
        lines = self.source.split("\n")
        for line_num, line_text in enumerate(lines, 1):
            self.line = line_num

            # Leerzeilen überspringen
            stripped = line_text.lstrip()
            if not stripped or stripped.startswith("#"):
                continue

            # Indentation verarbeiten
            indent = len(line_text) - len(stripped)
            self._handle_indent(indent)

            # Zeile tokenisieren
            self.pos = 0
            self.column = indent + 1
            self._tokenize_line(stripped)

            self.tokens.append(Token(TokenType.NEWLINE, "\\n", self.line, self.column))

        # Alle offenen Indents schließen
        while len(self.indent_stack) > 1:
            self.indent_stack.pop()
            self.tokens.append(Token(TokenType.DEDENT, "", self.line, 0))

        self.tokens.append(Token(TokenType.EOF, "", self.line, 0))
        return self.tokens

    def _handle_indent(self, indent: int):
        current = self.indent_stack[-1]
        if indent > current:
            self.indent_stack.append(indent)
            self.tokens.append(Token(TokenType.INDENT, "", self.line, 0))
        else:
            while indent < self.indent_stack[-1]:
                self.indent_stack.pop()
                self.tokens.append(Token(TokenType.DEDENT, "", self.line, 0))

    def _tokenize_line(self, line: str):
        i = 0
        while i < len(line):
            ch = line[i]

            # Whitespace
            if ch in " \t":
                i += 1
                self.column += 1
                continue

            # Kommentar
            if ch == "#":
                break

            # String
            if ch in ('"', "'"):
                i = self._read_string(line, i, ch)
                continue

            # Zahl
            if ch.isdigit():
                i = self._read_number(line, i)
                continue

            # Identifier / Keyword
            if ch.isalpha() or ch == "_":
                i = self._read_identifier(line, i)
                continue

            # Drei-Zeichen-Operatoren (noch keine, aber Platz für Erweiterungen)

            # Zwei-Zeichen-Operatoren
            if i + 1 < len(line):
                two = line[i : i + 2]
                op = {
                    "==": TokenType.EQUALS, "!=": TokenType.NOT_EQUALS,
                    "<=": TokenType.LESS_EQ, ">=": TokenType.GREATER_EQ,
                    "**": TokenType.POWER, "+=": TokenType.PLUS_ASSIGN,
                    "-=": TokenType.MINUS_ASSIGN, "=>": TokenType.ARROW,
                }.get(two)
                if op:
                    self.tokens.append(Token(op, two, self.line, self.column))
                    i += 2
                    self.column += 2
                    continue

            # Ein-Zeichen-Operatoren
            single = {
                "+": TokenType.PLUS, "-": TokenType.MINUS,
                "*": TokenType.MULTIPLY, "/": TokenType.DIVIDE,
                "%": TokenType.MODULO,
                "=": TokenType.ASSIGN, "<": TokenType.LESS,
                ">": TokenType.GREATER, ":": TokenType.COLON,
                ",": TokenType.COMMA, "(": TokenType.LPAREN,
                ")": TokenType.RPAREN, "[": TokenType.LBRACKET,
                "]": TokenType.RBRACKET, "{": TokenType.LBRACE,
                "}": TokenType.RBRACE, ".": TokenType.DOT,
            }.get(ch)

            if single:
                self.tokens.append(Token(single, ch, self.line, self.column))
                i += 1
                self.column += 1
            else:
                raise LexerError(f"Unbekanntes Zeichen: '{ch}'", self.line, self.column)

    def _read_string(self, line: str, start: int, quote: str) -> int:
        i = start + 1
        result = []
        while i < len(line) and line[i] != quote:
            if line[i] == "\\" and i + 1 < len(line):
                esc = line[i + 1]
                escaped = {"n": "\n", "t": "\t", "\\": "\\", '"': '"', "'": "'"}.get(esc, esc)
                result.append(escaped)
                i += 2
            else:
                result.append(line[i])
                i += 1
        if i >= len(line):
            raise LexerError("String nicht geschlossen", self.line, self.column)
        i += 1
        self.tokens.append(Token(TokenType.STRING, "".join(result), self.line, self.column))
        self.column += i - start
        return i

    def _read_number(self, line: str, start: int) -> int:
        i = start
        has_dot = False
        while i < len(line) and (line[i].isdigit() or (line[i] == "." and not has_dot)):
            if line[i] == ".":
                has_dot = True
            i += 1
        value = float(line[start:i]) if has_dot else int(line[start:i])
        self.tokens.append(Token(TokenType.NUMBER, value, self.line, self.column))
        self.column += i - start
        return i

    def _read_identifier(self, line: str, start: int) -> int:
        i = start
        while i < len(line) and (line[i].isalnum() or line[i] in "_äöüÄÖÜß"):
            i += 1
        word = line[start:i]
        token_type = KEYWORDS.get(word, TokenType.IDENTIFIER)
        if token_type == TokenType.BOOLEAN:
            value = BOOLEAN_VALUES[word]
        elif token_type == TokenType.NONE:
            value = None
        else:
            value = word
        self.tokens.append(Token(token_type, value, self.line, self.column))
        self.column += i - start
        return i
