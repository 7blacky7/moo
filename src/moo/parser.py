"""Parser für moo — wandelt Tokens in einen AST um."""

from .ast_nodes import (
    Assignment, BinaryOp, BooleanLiteral, BreakStatement, ClassDef,
    CompoundAssignment, ConstAssignment, ContinueStatement, DataClassDef, DictLiteral,
    ExportStatement, ForLoop, FunctionCall, FunctionDef, Identifier,
    IfStatement, ImportStatement, IndexAccess, IndexAssignment, LambdaExpression,
    ListComprehension, ListLiteral, MatchExpr, MatchStatement, MethodCall, NewExpression, Node, NoneLiteral,
    NullishCoalesce, OptionalChain, PipeExpr, RangeExpr,
    NumberLiteral, Program, PropertyAccess, PropertyAssignment, ReturnStatement,
    ShowStatement, SpreadExpr, StringLiteral, ThisExpression, ThrowStatement, TryCatch,
    UnaryOp, WhileLoop,
)
from .tokens import Token, TokenType


class ParseError(Exception):
    def __init__(self, message: str, token: Token):
        super().__init__(f"Zeile {token.line}: {message} (bekommen: {token.type.name})")
        self.token = token


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0

    def parse(self) -> Program:
        statements = self._parse_block_body(top_level=True)
        return Program(statements=statements)

    # --- Hilfsfunktionen ---

    def _current(self) -> Token:
        return self.tokens[self.pos]

    def _peek(self, offset: int = 1) -> Token:
        idx = self.pos + offset
        if idx < len(self.tokens):
            return self.tokens[idx]
        return self.tokens[-1]

    def _eat(self, expected: TokenType) -> Token:
        tok = self._current()
        if tok.type != expected:
            raise ParseError(f"Erwartet {expected.name}", tok)
        self.pos += 1
        return tok

    def _skip_newlines(self):
        while self._current().type == TokenType.NEWLINE:
            self.pos += 1

    def _parse_block_body(self, top_level: bool = False) -> list[Node]:
        stmts: list[Node] = []
        if not top_level:
            self._eat(TokenType.COLON)
            self._skip_newlines()
            self._eat(TokenType.INDENT)

        while True:
            self._skip_newlines()
            ct = self._current().type
            if ct == TokenType.EOF:
                break
            if ct == TokenType.DEDENT and not top_level:
                self.pos += 1
                break
            stmts.append(self._parse_statement())

        return stmts

    # --- Statements ---

    def _parse_statement(self) -> Node:
        ct = self._current().type

        if ct == TokenType.SET:
            return self._parse_assignment()
        if ct == TokenType.CONST:
            return self._parse_const_assignment()
        if ct == TokenType.SHOW:
            return self._parse_show()
        if ct == TokenType.IF:
            return self._parse_if()
        if ct == TokenType.WHILE:
            return self._parse_while()
        if ct == TokenType.FOR:
            return self._parse_for()
        if ct == TokenType.AT:
            return self._parse_decorated_function()
        if ct == TokenType.FUNC:
            return self._parse_function_def()
        if ct == TokenType.DATA:
            return self._parse_data_class()
        if ct == TokenType.RETURN:
            return self._parse_return()
        if ct == TokenType.BREAK:
            self.pos += 1
            return BreakStatement(line=self._current().line)
        if ct == TokenType.CONTINUE:
            self.pos += 1
            return ContinueStatement(line=self._current().line)
        if ct == TokenType.CLASS:
            return self._parse_class_def()
        if ct == TokenType.TRY:
            return self._parse_try_catch()
        if ct == TokenType.THROW:
            return self._parse_throw()
        if ct == TokenType.IMPORT:
            return self._parse_import()
        if ct == TokenType.FROM:
            return self._parse_from_import()
        if ct == TokenType.EXPORT:
            return self._parse_export()
        if ct == TokenType.MATCH:
            return self._parse_match()

        # Expression-Statement oder Zuweisung mit Punkt/Index
        expr = self._parse_expression()

        # Property-Zuweisung: obj.prop = wert
        if isinstance(expr, PropertyAccess) and self._current().type == TokenType.ASSIGN:
            self.pos += 1
            value = self._parse_expression()
            return PropertyAssignment(object=expr.object, property=expr.property, value=value, line=expr.line)

        # Index-Zuweisung: liste[0] = wert
        if isinstance(expr, IndexAccess) and self._current().type == TokenType.ASSIGN:
            self.pos += 1
            value = self._parse_expression()
            return IndexAssignment(object=expr.object, index=expr.index, value=value, line=expr.line)

        # Compound Assignment: x += 1, x -= 1
        if isinstance(expr, Identifier) and self._current().type in (TokenType.PLUS_ASSIGN, TokenType.MINUS_ASSIGN):
            op = self._current().value
            self.pos += 1
            value = self._parse_expression()
            return CompoundAssignment(name=expr.name, op=op, value=value, line=expr.line)

        return expr

    def _parse_assignment(self) -> Assignment:
        tok = self._eat(TokenType.SET)
        name_tok = self._eat(TokenType.IDENTIFIER)
        self._eat(TokenType.TO)
        value = self._parse_expression()
        return Assignment(name=name_tok.value, value=value, line=tok.line)

    def _parse_const_assignment(self) -> ConstAssignment:
        tok = self._eat(TokenType.CONST)
        name_tok = self._eat(TokenType.IDENTIFIER)
        self._eat(TokenType.TO)
        value = self._parse_expression()
        return ConstAssignment(name=name_tok.value, value=value, line=tok.line)

    def _parse_show(self) -> ShowStatement:
        tok = self._eat(TokenType.SHOW)
        value = self._parse_expression()
        return ShowStatement(value=value, line=tok.line)

    def _parse_if(self) -> IfStatement:
        tok = self._eat(TokenType.IF)
        condition = self._parse_expression()
        body = self._parse_block_body()

        else_body: list[Node] = []
        self._skip_newlines()
        if self._current().type == TokenType.ELSE:
            self.pos += 1
            if self._current().type == TokenType.IF:
                else_body = [self._parse_if()]
            else:
                else_body = self._parse_block_body()

        return IfStatement(condition=condition, body=body, else_body=else_body, line=tok.line)

    def _parse_while(self) -> WhileLoop:
        tok = self._eat(TokenType.WHILE)
        condition = self._parse_expression()
        body = self._parse_block_body()
        return WhileLoop(condition=condition, body=body, line=tok.line)

    def _parse_for(self) -> ForLoop:
        tok = self._eat(TokenType.FOR)
        var_tok = self._eat(TokenType.IDENTIFIER)
        self._eat(TokenType.IN)
        iterable = self._parse_expression()
        body = self._parse_block_body()
        return ForLoop(var_name=var_tok.value, iterable=iterable, body=body, line=tok.line)

    def _parse_decorated_function(self) -> FunctionDef:
        decorators: list[str] = []
        while self._current().type == TokenType.AT:
            self.pos += 1  # @
            decorators.append(self._eat(TokenType.IDENTIFIER).value)
            self._skip_newlines()
        if self._current().type != TokenType.FUNC:
            raise ParseError("funktion/func nach Decorator erwartet", self._current())
        return self._parse_function_def(decorators)

    def _parse_function_def(self, decorators: list[str] | None = None) -> FunctionDef:
        tok = self._eat(TokenType.FUNC)
        name_tok = self._eat(TokenType.IDENTIFIER)
        self._eat(TokenType.LPAREN)
        params: list[str] = []
        defaults: list[Node | None] = []
        if self._current().type != TokenType.RPAREN:
            self._parse_param(params, defaults)
            while self._current().type == TokenType.COMMA:
                self.pos += 1
                self._parse_param(params, defaults)
        self._eat(TokenType.RPAREN)
        body = self._parse_block_body()
        return FunctionDef(name=name_tok.value, params=params, defaults=defaults, body=body,
                           decorators=decorators or [], line=tok.line)

    def _parse_data_class(self) -> DataClassDef:
        tok = self._current()
        self.pos += 1  # data/daten
        if self._current().type != TokenType.CLASS:
            raise ParseError("klasse/class nach daten/data erwartet", self._current())
        self.pos += 1  # class/klasse
        name_tok = self._eat(TokenType.IDENTIFIER)
        self._eat(TokenType.LPAREN)
        fields: list[str] = []
        if self._current().type != TokenType.RPAREN:
            fields.append(self._eat(TokenType.IDENTIFIER).value)
            while self._current().type == TokenType.COMMA:
                self.pos += 1
                fields.append(self._eat(TokenType.IDENTIFIER).value)
        self._eat(TokenType.RPAREN)
        return DataClassDef(name=name_tok.value, fields=fields, line=tok.line)

    def _parse_param(self, params: list[str], defaults: list[Node | None]):
        name = self._eat(TokenType.IDENTIFIER).value
        params.append(name)
        if self._current().type == TokenType.ASSIGN:
            self.pos += 1
            defaults.append(self._parse_expression())
        else:
            defaults.append(None)

    def _parse_return(self) -> ReturnStatement:
        tok = self._eat(TokenType.RETURN)
        value = None
        if self._current().type not in (TokenType.NEWLINE, TokenType.EOF, TokenType.DEDENT):
            value = self._parse_expression()
        return ReturnStatement(value=value, line=tok.line)

    def _parse_class_def(self) -> ClassDef:
        tok = self._eat(TokenType.CLASS)
        name_tok = self._eat(TokenType.IDENTIFIER)
        parent = None
        if self._current().type == TokenType.LPAREN:
            self.pos += 1
            parent = self._eat(TokenType.IDENTIFIER).value
            self._eat(TokenType.RPAREN)
        body = self._parse_block_body()
        return ClassDef(name=name_tok.value, parent=parent, body=body, line=tok.line)

    def _parse_try_catch(self) -> TryCatch:
        tok = self._eat(TokenType.TRY)
        try_body = self._parse_block_body()
        self._skip_newlines()
        self._eat(TokenType.CATCH)
        catch_var = None
        if self._current().type == TokenType.IDENTIFIER:
            catch_var = self._eat(TokenType.IDENTIFIER).value
        catch_body = self._parse_block_body()
        return TryCatch(try_body=try_body, catch_var=catch_var, catch_body=catch_body, line=tok.line)

    def _parse_throw(self) -> ThrowStatement:
        tok = self._eat(TokenType.THROW)
        value = self._parse_expression()
        return ThrowStatement(value=value, line=tok.line)

    def _parse_import(self) -> ImportStatement:
        tok = self._eat(TokenType.IMPORT)
        module = self._eat(TokenType.IDENTIFIER).value
        alias = None
        if self._current().type == TokenType.AS:
            self.pos += 1
            alias = self._eat(TokenType.IDENTIFIER).value
        return ImportStatement(module=module, names=[], alias=alias, line=tok.line)

    def _parse_from_import(self) -> ImportStatement:
        tok = self._eat(TokenType.FROM)
        module = self._eat(TokenType.IDENTIFIER).value
        self._eat(TokenType.IMPORT)
        names: list[str] = []
        names.append(self._eat(TokenType.IDENTIFIER).value)
        while self._current().type == TokenType.COMMA:
            self.pos += 1
            names.append(self._eat(TokenType.IDENTIFIER).value)
        return ImportStatement(module=module, names=names, line=tok.line)

    def _parse_export(self) -> ExportStatement:
        tok = self._eat(TokenType.EXPORT)
        stmt = self._parse_statement()
        return ExportStatement(statement=stmt, line=tok.line)

    def _parse_match(self) -> MatchStatement:
        tok = self._eat(TokenType.MATCH)
        value = self._parse_expression()
        self._eat(TokenType.COLON)
        self._skip_newlines()
        self._eat(TokenType.INDENT)

        cases: list[tuple[Node | None, list[Node]]] = []
        while True:
            self._skip_newlines()
            ct = self._current().type
            if ct == TokenType.DEDENT:
                self.pos += 1
                break
            if ct == TokenType.EOF:
                break
            if ct == TokenType.CASE:
                self.pos += 1
                pattern = self._parse_expression()
                body = self._parse_block_body()
                cases.append((pattern, body))
            elif ct == TokenType.DEFAULT:
                self.pos += 1
                body = self._parse_block_body()
                cases.append((None, body))

        return MatchStatement(value=value, cases=cases, line=tok.line)

    # --- Expressions (Precedence Climbing) ---

    def _parse_expression(self) -> Node:
        # Lambda: (x, y) => ausdruck
        if self._current().type == TokenType.LPAREN and self._is_lambda():
            return self._parse_lambda()
        return self._parse_pipe()

    def _parse_pipe(self) -> Node:
        left = self._parse_nullish_coalesce()
        while self._current().type == TokenType.PIPE:
            self.pos += 1
            right = self._parse_nullish_coalesce()
            # Transform: left |> func(args) => func(left, args)
            if isinstance(right, FunctionCall):
                right.args.insert(0, left)
                left = right
            elif isinstance(right, MethodCall):
                right.args.insert(0, left)
                left = right
            else:
                left = PipeExpr(left=left, right=right, line=left.line)
        return left

    def _parse_nullish_coalesce(self) -> Node:
        left = self._parse_or()
        while self._current().type == TokenType.NULLISH_COALESCE:
            self.pos += 1
            right = self._parse_or()
            left = NullishCoalesce(left=left, right=right, line=left.line)
        return left

    def _is_lambda(self) -> bool:
        """Vorausschauen ob das eine Lambda-Expression ist."""
        depth = 0
        i = self.pos
        while i < len(self.tokens):
            t = self.tokens[i].type
            if t == TokenType.LPAREN:
                depth += 1
            elif t == TokenType.RPAREN:
                depth -= 1
                if depth == 0:
                    return i + 1 < len(self.tokens) and self.tokens[i + 1].type == TokenType.ARROW
            elif t in (TokenType.NEWLINE, TokenType.EOF):
                return False
            i += 1
        return False

    def _parse_lambda(self) -> LambdaExpression:
        tok = self._eat(TokenType.LPAREN)
        params: list[str] = []
        if self._current().type != TokenType.RPAREN:
            params.append(self._eat(TokenType.IDENTIFIER).value)
            while self._current().type == TokenType.COMMA:
                self.pos += 1
                params.append(self._eat(TokenType.IDENTIFIER).value)
        self._eat(TokenType.RPAREN)
        self._eat(TokenType.ARROW)
        body = self._parse_expression()
        return LambdaExpression(params=params, body=body, line=tok.line)

    def _parse_or(self) -> Node:
        left = self._parse_and()
        while self._current().type == TokenType.OR:
            self.pos += 1
            right = self._parse_and()
            left = BinaryOp(left=left, op="or", right=right)
        return left

    def _parse_and(self) -> Node:
        left = self._parse_not()
        while self._current().type == TokenType.AND:
            self.pos += 1
            right = self._parse_not()
            left = BinaryOp(left=left, op="and", right=right)
        return left

    def _parse_not(self) -> Node:
        if self._current().type == TokenType.NOT:
            tok = self._current()
            self.pos += 1
            operand = self._parse_not()
            return UnaryOp(op="not", operand=operand, line=tok.line)
        return self._parse_comparison()

    def _parse_comparison(self) -> Node:
        left = self._parse_addition()
        ops = {
            TokenType.EQUALS: "==", TokenType.NOT_EQUALS: "!=",
            TokenType.LESS: "<", TokenType.GREATER: ">",
            TokenType.LESS_EQ: "<=", TokenType.GREATER_EQ: ">=",
        }
        while self._current().type in ops:
            op = ops[self._current().type]
            self.pos += 1
            right = self._parse_addition()
            left = BinaryOp(left=left, op=op, right=right)
        return left

    def _parse_addition(self) -> Node:
        left = self._parse_multiplication()
        # Range: 0..10
        if self._current().type == TokenType.RANGE:
            self.pos += 1
            right = self._parse_multiplication()
            return RangeExpr(start=left, end=right, line=left.line)
        while self._current().type in (TokenType.PLUS, TokenType.MINUS):
            op = "+" if self._current().type == TokenType.PLUS else "-"
            self.pos += 1
            right = self._parse_multiplication()
            left = BinaryOp(left=left, op=op, right=right)
        return left

    def _parse_multiplication(self) -> Node:
        left = self._parse_power()
        while self._current().type in (TokenType.MULTIPLY, TokenType.DIVIDE, TokenType.MODULO):
            op = {TokenType.MULTIPLY: "*", TokenType.DIVIDE: "/", TokenType.MODULO: "%"}[self._current().type]
            self.pos += 1
            right = self._parse_power()
            left = BinaryOp(left=left, op=op, right=right)
        return left

    def _parse_power(self) -> Node:
        left = self._parse_unary()
        if self._current().type == TokenType.POWER:
            self.pos += 1
            right = self._parse_power()  # Rechts-assoziativ
            left = BinaryOp(left=left, op="**", right=right)
        return left

    def _parse_unary(self) -> Node:
        if self._current().type == TokenType.MINUS:
            tok = self._current()
            self.pos += 1
            operand = self._parse_postfix()
            return UnaryOp(op="-", operand=operand, line=tok.line)
        return self._parse_postfix()

    def _parse_postfix(self) -> Node:
        """Verarbeitet Ketten von .prop, [index], (args)"""
        node = self._parse_primary()

        while True:
            if self._current().type == TokenType.DOT:
                self.pos += 1
                prop = self._eat(TokenType.IDENTIFIER).value
                if self._current().type == TokenType.LPAREN:
                    # Methodenaufruf: obj.methode(args)
                    self._eat(TokenType.LPAREN)
                    args = self._parse_args_list()
                    self._eat(TokenType.RPAREN)
                    node = MethodCall(object=node, method=prop, args=args, line=node.line)
                else:
                    node = PropertyAccess(object=node, property=prop, line=node.line)
            elif self._current().type == TokenType.OPTIONAL_CHAIN:
                self.pos += 1
                prop = self._eat(TokenType.IDENTIFIER).value
                node = OptionalChain(object=node, property=prop, line=node.line)
            elif self._current().type == TokenType.LBRACKET:
                self.pos += 1
                index = self._parse_expression()
                self._eat(TokenType.RBRACKET)
                node = IndexAccess(object=node, index=index, line=node.line)
            else:
                break

        return node

    def _parse_args_list(self) -> list[Node]:
        args: list[Node] = []
        if self._current().type != TokenType.RPAREN:
            args.append(self._parse_expression())
            while self._current().type == TokenType.COMMA:
                self.pos += 1
                args.append(self._parse_expression())
        return args

    def _parse_primary(self) -> Node:
        tok = self._current()

        if tok.type == TokenType.NUMBER:
            self.pos += 1
            return NumberLiteral(value=tok.value, line=tok.line)

        if tok.type == TokenType.STRING:
            self.pos += 1
            return StringLiteral(value=tok.value, line=tok.line)

        if tok.type == TokenType.BOOLEAN:
            self.pos += 1
            return BooleanLiteral(value=tok.value, line=tok.line)

        if tok.type == TokenType.NONE:
            self.pos += 1
            return NoneLiteral(line=tok.line)

        if tok.type == TokenType.THIS:
            self.pos += 1
            return ThisExpression(line=tok.line)

        if tok.type == TokenType.NEW:
            return self._parse_new()

        if tok.type == TokenType.IDENTIFIER:
            self.pos += 1
            if self._current().type == TokenType.LPAREN:
                return self._parse_call(tok)
            return Identifier(name=tok.value, line=tok.line)

        if tok.type == TokenType.LPAREN:
            self.pos += 1
            expr = self._parse_expression()
            self._eat(TokenType.RPAREN)
            return expr

        if tok.type == TokenType.LBRACKET:
            return self._parse_list()

        if tok.type == TokenType.LBRACE:
            return self._parse_dict()

        if tok.type == TokenType.MATCH:
            return self._parse_match_expr()

        raise ParseError("Ausdruck erwartet", tok)

    def _parse_new(self) -> NewExpression:
        tok = self._eat(TokenType.NEW)
        name = self._eat(TokenType.IDENTIFIER).value
        self._eat(TokenType.LPAREN)
        args = self._parse_args_list()
        self._eat(TokenType.RPAREN)
        return NewExpression(class_name=name, args=args, line=tok.line)

    def _parse_call(self, name_tok: Token) -> FunctionCall:
        self._eat(TokenType.LPAREN)
        args = self._parse_args_list()
        self._eat(TokenType.RPAREN)
        return FunctionCall(name=name_tok.value, args=args, line=name_tok.line)

    def _parse_list(self) -> ListLiteral | ListComprehension:
        tok = self._eat(TokenType.LBRACKET)
        elements: list[Node] = []
        if self._current().type != TokenType.RBRACKET:
            first = self._parse_expression()
            # List comprehension: [expr für/for x in iterable wenn/if cond]
            if self._current().type == TokenType.FOR:
                self.pos += 1  # skip für/for
                var_tok = self._eat(TokenType.IDENTIFIER)
                self._eat(TokenType.IN)
                iterable = self._parse_expression()
                condition = None
                if self._current().type == TokenType.IF:
                    self.pos += 1  # skip wenn/if
                    condition = self._parse_expression()
                self._eat(TokenType.RBRACKET)
                return ListComprehension(expr=first, var_name=var_tok.value,
                                         iterable=iterable, condition=condition, line=tok.line)
            elements.append(first)
            while self._current().type == TokenType.COMMA:
                self.pos += 1
                if self._current().type == TokenType.RBRACKET:
                    break
                elements.append(self._parse_list_element())
        self._eat(TokenType.RBRACKET)
        return ListLiteral(elements=elements, line=tok.line)

    def _parse_list_element(self) -> Node:
        if self._current().type == TokenType.SPREAD:
            tok = self._current()
            self.pos += 1
            expr = self._parse_expression()
            return SpreadExpr(expr=expr, line=tok.line)
        return self._parse_expression()

    def _parse_dict(self) -> DictLiteral:
        tok = self._eat(TokenType.LBRACE)
        pairs: list[tuple[Node, Node]] = []
        if self._current().type != TokenType.RBRACE:
            self._parse_dict_entry(pairs)
            while self._current().type == TokenType.COMMA:
                self.pos += 1
                if self._current().type == TokenType.RBRACE:
                    break
                self._parse_dict_entry(pairs)
        self._eat(TokenType.RBRACE)
        return DictLiteral(pairs=pairs, line=tok.line)

    def _parse_dict_entry(self, pairs: list[tuple[Node, Node]]):
        if self._current().type == TokenType.SPREAD:
            tok = self._current()
            self.pos += 1
            expr = self._parse_expression()
            # Use SpreadExpr as key, None-equivalent as val to signal spread
            pairs.append((SpreadExpr(expr=expr, line=tok.line), SpreadExpr(expr=expr, line=tok.line)))
        else:
            key = self._parse_expression()
            self._eat(TokenType.COLON)
            val = self._parse_expression()
            pairs.append((key, val))

    def _parse_match_expr(self) -> MatchExpr:
        """Match als Expression (Kotlin when): prüfe wert: fall x: ergebnis"""
        tok = self._eat(TokenType.MATCH)
        value = self._parse_expression()
        self._eat(TokenType.COLON)
        self._skip_newlines()
        self._eat(TokenType.INDENT)

        cases: list[tuple[Node | None, Node | None, Node]] = []
        while True:
            self._skip_newlines()
            ct = self._current().type
            if ct == TokenType.DEDENT:
                self.pos += 1
                break
            if ct == TokenType.EOF:
                break
            if ct == TokenType.CASE:
                self.pos += 1
                # Wildcard: fall _
                if (self._current().type == TokenType.IDENTIFIER
                        and self._current().value == "_"):
                    self.pos += 1
                    self._eat(TokenType.COLON)
                    result = self._parse_expression()
                    cases.append((None, None, result))
                    continue
                pattern = self._parse_expression()
                guard = None
                if self._current().type == TokenType.IF:
                    self.pos += 1
                    guard = self._parse_expression()
                self._eat(TokenType.COLON)
                result = self._parse_expression()
                cases.append((pattern, guard, result))
            elif ct == TokenType.DEFAULT:
                self.pos += 1
                self._eat(TokenType.COLON)
                result = self._parse_expression()
                cases.append((None, None, result))
            else:
                raise ParseError("fall/case oder standard/default erwartet", self._current())

        return MatchExpr(value=value, cases=cases, line=tok.line)
