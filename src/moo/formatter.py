"""moo Formatter — formatiert moo-Code einheitlich (AST → moo).

Regeln:
- 4 Leerzeichen Einrückung
- Leerzeichen um Operatoren
- Leerzeile nach Funktionen und Klassen
- Konsistente Keyword-Nutzung (Deutsch)
"""

from .ast_nodes import (
    Assignment, AwaitExpr, BinaryOp, BooleanLiteral, BreakStatement, ClassDef,
    CompoundAssignment, ConstAssignment, ContinueStatement, DataClassDef, DictLiteral,
    ExportStatement, ForLoop, FunctionCall, FunctionDef, Identifier,
    IfStatement, ImportStatement, IndexAccess, IndexAssignment, LambdaExpression,
    ListComprehension, ListLiteral, MatchExpr, MatchStatement, MethodCall, NewExpression,
    Node, NoneLiteral, NullishCoalesce, NumberLiteral, OptionalChain, PipeExpr, Program,
    PropertyAccess, PropertyAssignment, RangeExpr, ReturnStatement, ShowStatement,
    SpreadExpr, StringLiteral, ThisExpression, ThrowStatement, TryCatch, UnaryOp,
    WhileLoop,
)


class MooFormatter:
    def __init__(self):
        self.indent = 0

    def format(self, program: Program) -> str:
        lines = []
        prev_was_block = False
        for stmt in program.statements:
            is_block = isinstance(stmt, (FunctionDef, ClassDef, DataClassDef))
            if prev_was_block and lines:
                lines.append("")
            lines.append(self._stmt(stmt))
            prev_was_block = is_block
        return "\n".join(lines) + "\n"

    def _pre(self) -> str:
        return "    " * self.indent

    def _block(self, body: list[Node]) -> list[str]:
        self.indent += 1
        lines = []
        prev_was_block = False
        for s in body:
            is_block = isinstance(s, (FunctionDef, ClassDef))
            if prev_was_block and lines:
                lines.append("")
            lines.append(self._stmt(s))
            prev_was_block = is_block
        self.indent -= 1
        return lines

    def _expr(self, node: Node) -> str:
        if isinstance(node, NumberLiteral):
            v = node.value
            if isinstance(v, float) and v == int(v) and "." not in str(node.value):
                return str(int(v))
            return str(v)
        if isinstance(node, StringLiteral):
            escaped = node.value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
            return f'"{escaped}"'
        if isinstance(node, BooleanLiteral):
            return "wahr" if node.value else "falsch"
        if isinstance(node, NoneLiteral):
            return "nichts"
        if isinstance(node, Identifier):
            return node.name
        if isinstance(node, ThisExpression):
            return "selbst"
        if isinstance(node, BinaryOp):
            left = self._expr(node.left)
            right = self._expr(node.right)
            return f"{left} {node.op} {right}"
        if isinstance(node, UnaryOp):
            operand = self._expr(node.operand)
            if node.op == "nicht" or node.op == "not":
                return f"nicht {operand}"
            return f"{node.op}{operand}"
        if isinstance(node, FunctionCall):
            args = ", ".join(self._expr(a) for a in node.args)
            return f"{node.name}({args})"
        if isinstance(node, MethodCall):
            obj = self._expr(node.object)
            args = ", ".join(self._expr(a) for a in node.args)
            return f"{obj}.{node.method}({args})"
        if isinstance(node, PropertyAccess):
            return f"{self._expr(node.object)}.{node.property}"
        if isinstance(node, IndexAccess):
            return f"{self._expr(node.object)}[{self._expr(node.index)}]"
        if isinstance(node, RangeExpr):
            return f"{self._expr(node.start)}..{self._expr(node.end)}"
        if isinstance(node, ListLiteral):
            elems = ", ".join(self._expr(e) for e in node.elements)
            return f"[{elems}]"
        if isinstance(node, ListComprehension):
            base = f"[{self._expr(node.expr)} für {node.var_name} in {self._expr(node.iterable)}"
            if node.condition:
                base += f" wenn {self._expr(node.condition)}"
            return base + "]"
        if isinstance(node, DictLiteral):
            pairs = ", ".join(f"{self._expr(k)}: {self._expr(v)}" for k, v in node.pairs)
            return "{" + pairs + "}"
        if isinstance(node, NewExpression):
            args = ", ".join(self._expr(a) for a in node.args)
            return f"neu {node.class_name}({args})"
        if isinstance(node, LambdaExpression):
            params = ", ".join(node.params)
            return f"({params}) => {self._expr(node.body)}"
        if isinstance(node, OptionalChain):
            return f"{self._expr(node.object)}?.{node.property}"
        if isinstance(node, NullishCoalesce):
            return f"{self._expr(node.left)} ?? {self._expr(node.right)}"
        if isinstance(node, PipeExpr):
            return f"{self._expr(node.left)} |> {self._expr(node.right)}"
        if isinstance(node, SpreadExpr):
            return f"...{self._expr(node.expr)}"
        if isinstance(node, AwaitExpr):
            return f"warte {self._expr(node.expr)}"
        return f"/* unbekannt: {type(node).__name__} */"

    def _stmt(self, node: Node) -> str:
        if isinstance(node, Assignment):
            return f"{self._pre()}setze {node.name} auf {self._expr(node.value)}"
        if isinstance(node, ConstAssignment):
            return f"{self._pre()}konstante {node.name} auf {self._expr(node.value)}"
        if isinstance(node, CompoundAssignment):
            return f"{self._pre()}{node.name} {node.op} {self._expr(node.value)}"
        if isinstance(node, PropertyAssignment):
            return f"{self._pre()}{self._expr(node.object)}.{node.property} = {self._expr(node.value)}"
        if isinstance(node, IndexAssignment):
            return f"{self._pre()}{self._expr(node.object)}[{self._expr(node.index)}] = {self._expr(node.value)}"
        if isinstance(node, ShowStatement):
            return f"{self._pre()}zeige {self._expr(node.value)}"
        if isinstance(node, IfStatement):
            lines = [f"{self._pre()}wenn {self._expr(node.condition)}:"]
            lines.extend(self._block(node.body))
            if node.else_body:
                if len(node.else_body) == 1 and isinstance(node.else_body[0], IfStatement):
                    lines.append(f"{self._pre()}sonst {self._stmt(node.else_body[0]).lstrip()}")
                else:
                    lines.append(f"{self._pre()}sonst:")
                    lines.extend(self._block(node.else_body))
            return "\n".join(lines)
        if isinstance(node, WhileLoop):
            lines = [f"{self._pre()}solange {self._expr(node.condition)}:"]
            lines.extend(self._block(node.body))
            return "\n".join(lines)
        if isinstance(node, ForLoop):
            lines = [f"{self._pre()}für {node.var_name} in {self._expr(node.iterable)}:"]
            lines.extend(self._block(node.body))
            return "\n".join(lines)
        if isinstance(node, FunctionDef):
            params = ", ".join(node.params)
            prefix = "asynchron " if node.is_async else ""
            decorators = "".join(f"{self._pre()}@{d}\n" for d in node.decorators)
            lines = [f"{decorators}{self._pre()}{prefix}funktion {node.name}({params}):"]
            lines.extend(self._block(node.body))
            return "\n".join(lines)
        if isinstance(node, ReturnStatement):
            if node.value:
                return f"{self._pre()}gib_zurück {self._expr(node.value)}"
            return f"{self._pre()}gib_zurück"
        if isinstance(node, BreakStatement):
            return f"{self._pre()}stopp"
        if isinstance(node, ContinueStatement):
            return f"{self._pre()}weiter"
        if isinstance(node, ClassDef):
            parent = f"({node.parent})" if node.parent else ""
            lines = [f"{self._pre()}klasse {node.name}{parent}:"]
            lines.extend(self._block(node.body))
            return "\n".join(lines)
        if isinstance(node, DataClassDef):
            fields = ", ".join(node.fields)
            return f"{self._pre()}daten klasse {node.name}({fields})"
        if isinstance(node, TryCatch):
            lines = [f"{self._pre()}versuche:"]
            lines.extend(self._block(node.try_body))
            catch = f"fange {node.catch_var}" if node.catch_var else "fange"
            lines.append(f"{self._pre()}{catch}:")
            lines.extend(self._block(node.catch_body))
            return "\n".join(lines)
        if isinstance(node, ThrowStatement):
            return f"{self._pre()}wirf {self._expr(node.value)}"
        if isinstance(node, ImportStatement):
            if node.names:
                names = ", ".join(node.names)
                return f"{self._pre()}aus {node.module} importiere {names}"
            alias = f" als {node.alias}" if node.alias else ""
            return f"{self._pre()}importiere {node.module}{alias}"
        if isinstance(node, ExportStatement):
            return f"{self._pre()}exportiere {self._stmt(node.statement).lstrip()}"
        if isinstance(node, MatchStatement):
            lines = [f"{self._pre()}prüfe {self._expr(node.value)}:"]
            self.indent += 1
            for pattern, body in node.cases:
                if pattern is None:
                    lines.append(f"{self._pre()}standard:")
                else:
                    lines.append(f"{self._pre()}fall {self._expr(pattern)}:")
                lines.extend(self._block(body))
            self.indent -= 1
            return "\n".join(lines)
        # Fallback: Expression als Statement
        return f"{self._pre()}{self._expr(node)}"


def format_source(source: str) -> str:
    """Formatiert moo-Quelltext."""
    from .lexer import Lexer
    from .parser import Parser

    tokens = Lexer(source).tokenize()
    program = Parser(tokens).parse()
    return MooFormatter().format(program)
