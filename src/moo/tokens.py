"""Token-Definitionen und zweisprachiges Keyword-Mapping für moo."""

from enum import Enum, auto


class TokenType(Enum):
    # Literals
    NUMBER = auto()
    STRING = auto()
    IDENTIFIER = auto()
    BOOLEAN = auto()
    NONE = auto()

    # Keywords
    SET = auto()        # setze / set
    TO = auto()         # auf / to
    IF = auto()         # wenn / if
    ELSE = auto()       # sonst / else
    WHILE = auto()      # solange / while
    FOR = auto()        # für / for
    IN = auto()         # in / in
    FUNC = auto()       # funktion / func
    RETURN = auto()     # gib_zurück / return
    SHOW = auto()       # zeige / show
    AND = auto()        # und / and
    OR = auto()         # oder / or
    NOT = auto()        # nicht / not
    CLASS = auto()      # klasse / class
    NEW = auto()        # neu / new
    THIS = auto()       # selbst / this
    IMPORT = auto()     # importiere / import
    FROM = auto()       # aus / from
    EXPORT = auto()     # exportiere / export
    TRY = auto()        # versuche / try
    CATCH = auto()      # fange / catch
    THROW = auto()      # wirf / throw
    BREAK = auto()      # stopp / break
    CONTINUE = auto()   # weiter / continue
    CONST = auto()      # konstante / const
    MATCH = auto()      # prüfe / match
    CASE = auto()       # fall / case
    DEFAULT = auto()    # standard / default
    AS = auto()         # als / as
    DATA = auto()       # daten / data
    AT = auto()         # @
    ASYNC = auto()      # asynchron / async
    AWAIT = auto()      # warte / await

    # Operators
    RANGE = auto()      # ..
    PLUS = auto()
    MINUS = auto()
    MULTIPLY = auto()
    DIVIDE = auto()
    MODULO = auto()       # %
    POWER = auto()        # **
    ASSIGN = auto()       # =
    PLUS_ASSIGN = auto()  # +=
    MINUS_ASSIGN = auto() # -=
    EQUALS = auto()       # ==
    NOT_EQUALS = auto()   # !=
    LESS = auto()         # <
    GREATER = auto()      # >
    LESS_EQ = auto()      # <=
    GREATER_EQ = auto()   # >=
    DOT = auto()          # .
    OPTIONAL_CHAIN = auto()   # ?.
    NULLISH_COALESCE = auto() # ??
    PIPE = auto()             # |>
    SPREAD = auto()           # ...

    # Delimiters
    COLON = auto()
    COMMA = auto()
    LPAREN = auto()
    RPAREN = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    LBRACE = auto()       # {
    RBRACE = auto()       # }
    ARROW = auto()        # =>

    # Special
    NEWLINE = auto()
    INDENT = auto()
    DEDENT = auto()
    EOF = auto()


# Zweisprachiges Keyword-Mapping: beide Sprachen → gleicher TokenType
KEYWORDS: dict[str, TokenType] = {
    # Deutsch
    "setze": TokenType.SET,
    "auf": TokenType.TO,
    "wenn": TokenType.IF,
    "sonst": TokenType.ELSE,
    "solange": TokenType.WHILE,
    "für": TokenType.FOR,
    "in": TokenType.IN,
    "funktion": TokenType.FUNC,
    "gib_zurück": TokenType.RETURN,
    "zeige": TokenType.SHOW,
    "und": TokenType.AND,
    "oder": TokenType.OR,
    "nicht": TokenType.NOT,
    "wahr": TokenType.BOOLEAN,
    "falsch": TokenType.BOOLEAN,
    "nichts": TokenType.NONE,
    "klasse": TokenType.CLASS,
    "neu": TokenType.NEW,
    "selbst": TokenType.THIS,
    "importiere": TokenType.IMPORT,
    "aus": TokenType.FROM,
    "exportiere": TokenType.EXPORT,
    "versuche": TokenType.TRY,
    "fange": TokenType.CATCH,
    "wirf": TokenType.THROW,
    "stopp": TokenType.BREAK,
    "weiter": TokenType.CONTINUE,
    "konstante": TokenType.CONST,
    "prüfe": TokenType.MATCH,
    "fall": TokenType.CASE,
    "standard": TokenType.DEFAULT,
    "als": TokenType.AS,
    "daten": TokenType.DATA,
    "asynchron": TokenType.ASYNC,
    "warte": TokenType.AWAIT,

    # English
    "set": TokenType.SET,
    "to": TokenType.TO,
    "if": TokenType.IF,
    "else": TokenType.ELSE,
    "while": TokenType.WHILE,
    "for": TokenType.FOR,
    "func": TokenType.FUNC,
    "return": TokenType.RETURN,
    "show": TokenType.SHOW,
    "and": TokenType.AND,
    "or": TokenType.OR,
    "not": TokenType.NOT,
    "true": TokenType.BOOLEAN,
    "false": TokenType.BOOLEAN,
    "none": TokenType.NONE,
    "class": TokenType.CLASS,
    "new": TokenType.NEW,
    "this": TokenType.THIS,
    "import": TokenType.IMPORT,
    "from": TokenType.FROM,
    "export": TokenType.EXPORT,
    "try": TokenType.TRY,
    "catch": TokenType.CATCH,
    "throw": TokenType.THROW,
    "break": TokenType.BREAK,
    "continue": TokenType.CONTINUE,
    "const": TokenType.CONST,
    "match": TokenType.MATCH,
    "case": TokenType.CASE,
    "default": TokenType.DEFAULT,
    "as": TokenType.AS,
    "data": TokenType.DATA,
    "async": TokenType.ASYNC,
    "await": TokenType.AWAIT,
}

# Boolean-Werte Mapping
BOOLEAN_VALUES: dict[str, bool] = {
    "wahr": True,
    "falsch": False,
    "true": True,
    "false": False,
}


class Token:
    __slots__ = ("type", "value", "line", "column")

    def __init__(self, type: TokenType, value: object, line: int, column: int):
        self.type = type
        self.value = value
        self.line = line
        self.column = column

    def __repr__(self) -> str:
        return f"Token({self.type.name}, {self.value!r}, L{self.line}:{self.column})"
