"""Python Code-Generator — erzeugt Python-Code aus dem moo AST."""

from ..ast_nodes import (
    Assignment, BinaryOp, BooleanLiteral, BreakStatement, ClassDef,
    CompoundAssignment, ConstAssignment, ContinueStatement, DictLiteral,
    ExportStatement, ForLoop, FunctionCall, FunctionDef, Identifier,
    IfStatement, ImportStatement, IndexAccess, IndexAssignment, LambdaExpression,
    ListComprehension, ListLiteral, MatchStatement, MethodCall, NewExpression, Node, NoneLiteral,
    NullishCoalesce, NumberLiteral, OptionalChain, Program, PropertyAccess,
    PropertyAssignment, RangeExpr,
    ReturnStatement, ShowStatement, StringLiteral, ThisExpression, ThrowStatement,
    TryCatch, UnaryOp, WhileLoop,
)


class PythonGenerator:
    def __init__(self):
        self.indent = 0

    def generate(self, program: Program) -> str:
        lines = []
        for stmt in program.statements:
            lines.append(self._gen(stmt))
        return "\n".join(lines) + "\n"

    def _prefix(self) -> str:
        return "    " * self.indent

    def _gen(self, node: Node) -> str:
        method = f"_gen_{type(node).__name__}"
        gen = getattr(self, method, None)
        if not gen:
            raise NotImplementedError(f"Python-Generator: {type(node).__name__} nicht implementiert")
        return gen(node)

    def _gen_block(self, body: list[Node]) -> list[str]:
        self.indent += 1
        lines = [self._gen(s) for s in body]
        if not lines:
            lines = [f"{self._prefix()}pass"]
        self.indent -= 1
        return lines

    # === Statements ===

    def _gen_Assignment(self, node: Assignment) -> str:
        return f"{self._prefix()}{node.name} = {self._gen(node.value)}"

    def _gen_ConstAssignment(self, node: ConstAssignment) -> str:
        return f"{self._prefix()}{node.name.upper()} = {self._gen(node.value)}"

    def _gen_CompoundAssignment(self, node: CompoundAssignment) -> str:
        return f"{self._prefix()}{node.name} {node.op} {self._gen(node.value)}"

    def _gen_PropertyAssignment(self, node: PropertyAssignment) -> str:
        return f"{self._prefix()}{self._gen(node.object)}.{node.property} = {self._gen(node.value)}"

    def _gen_IndexAssignment(self, node: IndexAssignment) -> str:
        return f"{self._prefix()}{self._gen(node.object)}[{self._gen(node.index)}] = {self._gen(node.value)}"

    def _gen_ShowStatement(self, node: ShowStatement) -> str:
        return f"{self._prefix()}print({self._gen(node.value)})"

    def _gen_IfStatement(self, node: IfStatement) -> str:
        lines = [f"{self._prefix()}if {self._gen(node.condition)}:"]
        lines.extend(self._gen_block(node.body))
        if node.else_body:
            if len(node.else_body) == 1 and isinstance(node.else_body[0], IfStatement):
                lines.append(f"{self._prefix()}el{self._gen_IfStatement(node.else_body[0]).lstrip()}")
            else:
                lines.append(f"{self._prefix()}else:")
                lines.extend(self._gen_block(node.else_body))
        return "\n".join(lines)

    def _gen_WhileLoop(self, node: WhileLoop) -> str:
        lines = [f"{self._prefix()}while {self._gen(node.condition)}:"]
        lines.extend(self._gen_block(node.body))
        return "\n".join(lines)

    def _gen_ForLoop(self, node: ForLoop) -> str:
        lines = [f"{self._prefix()}for {node.var_name} in {self._gen(node.iterable)}:"]
        lines.extend(self._gen_block(node.body))
        return "\n".join(lines)

    def _gen_FunctionDef(self, node: FunctionDef) -> str:
        params_parts = []
        for i, p in enumerate(node.params):
            if node.defaults and node.defaults[i] is not None:
                params_parts.append(f"{p}={self._gen(node.defaults[i])}")
            else:
                params_parts.append(p)
        params = ", ".join(params_parts)
        lines = [f"{self._prefix()}def {node.name}({params}):"]
        lines.extend(self._gen_block(node.body))
        return "\n".join(lines)

    def _gen_ReturnStatement(self, node: ReturnStatement) -> str:
        if node.value:
            return f"{self._prefix()}return {self._gen(node.value)}"
        return f"{self._prefix()}return"

    def _gen_BreakStatement(self, node: BreakStatement) -> str:
        return f"{self._prefix()}break"

    def _gen_ContinueStatement(self, node: ContinueStatement) -> str:
        return f"{self._prefix()}continue"

    def _gen_ClassDef(self, node: ClassDef) -> str:
        parent = f"({node.parent})" if node.parent else ""
        lines = [f"{self._prefix()}class {node.name}{parent}:"]
        # Methoden: "selbst"/"this" Parameter → self
        body = []
        for stmt in node.body:
            if isinstance(stmt, FunctionDef):
                # __init__ für erstelle/create
                name = stmt.name
                if name in ("erstelle", "create"):
                    name = "__init__"
                params = ["self"] + stmt.params
                defaults = [None] + (stmt.defaults if stmt.defaults else [None] * len(stmt.params))
                modified = FunctionDef(name=name, params=params, defaults=defaults, body=stmt.body, line=stmt.line)
                body.append(modified)
            else:
                body.append(stmt)
        lines.extend(self._gen_block(body))
        return "\n".join(lines)

    def _gen_TryCatch(self, node: TryCatch) -> str:
        lines = [f"{self._prefix()}try:"]
        lines.extend(self._gen_block(node.try_body))
        catch_part = f" as {node.catch_var}" if node.catch_var else ""
        lines.append(f"{self._prefix()}except Exception{catch_part}:")
        lines.extend(self._gen_block(node.catch_body))
        return "\n".join(lines)

    def _gen_ThrowStatement(self, node: ThrowStatement) -> str:
        return f"{self._prefix()}raise Exception({self._gen(node.value)})"

    def _gen_ImportStatement(self, node: ImportStatement) -> str:
        if node.names:
            names = ", ".join(node.names)
            return f"{self._prefix()}from {node.module} import {names}"
        alias = f" as {node.alias}" if node.alias else ""
        return f"{self._prefix()}import {node.module}{alias}"

    def _gen_ExportStatement(self, node: ExportStatement) -> str:
        return self._gen(node.statement)

    def _gen_MatchStatement(self, node: MatchStatement) -> str:
        lines = [f"{self._prefix()}match {self._gen(node.value)}:"]
        self.indent += 1
        for pattern, body in node.cases:
            if pattern is None:
                lines.append(f"{self._prefix()}case _:")
            else:
                lines.append(f"{self._prefix()}case {self._gen(pattern)}:")
            lines.extend(self._gen_block(body))
        self.indent -= 1
        return "\n".join(lines)

    # === Expressions ===

    def _gen_NumberLiteral(self, node: NumberLiteral) -> str:
        return repr(node.value)

    def _gen_StringLiteral(self, node: StringLiteral) -> str:
        return repr(node.value)

    def _gen_BooleanLiteral(self, node: BooleanLiteral) -> str:
        return "True" if node.value else "False"

    def _gen_NoneLiteral(self, node: NoneLiteral) -> str:
        return "None"

    def _gen_Identifier(self, node: Identifier) -> str:
        return node.name

    def _gen_ThisExpression(self, node: ThisExpression) -> str:
        return "self"

    def _gen_BinaryOp(self, node: BinaryOp) -> str:
        return f"({self._gen(node.left)} {node.op} {self._gen(node.right)})"

    def _gen_UnaryOp(self, node: UnaryOp) -> str:
        if node.op == "not":
            return f"(not {self._gen(node.operand)})"
        return f"({node.op}{self._gen(node.operand)})"

    def _gen_FunctionCall(self, node: FunctionCall) -> str:
        args = ", ".join(self._gen(a) for a in node.args)
        return f"{node.name}({args})"

    def _gen_MethodCall(self, node: MethodCall) -> str:
        args = ", ".join(self._gen(a) for a in node.args)
        return f"{self._gen(node.object)}.{node.method}({args})"

    def _gen_PropertyAccess(self, node: PropertyAccess) -> str:
        return f"{self._gen(node.object)}.{node.property}"

    def _gen_IndexAccess(self, node: IndexAccess) -> str:
        if isinstance(node.index, RangeExpr):
            return f"{self._gen(node.object)}[{self._gen(node.index.start)}:{self._gen(node.index.end)}]"
        return f"{self._gen(node.object)}[{self._gen(node.index)}]"

    def _gen_RangeExpr(self, node: RangeExpr) -> str:
        return f"range({self._gen(node.start)}, {self._gen(node.end)})"

    def _gen_ListLiteral(self, node: ListLiteral) -> str:
        elements = ", ".join(self._gen(e) for e in node.elements)
        return f"[{elements}]"

    def _gen_ListComprehension(self, node: ListComprehension) -> str:
        expr = self._gen(node.expr)
        var = node.var_name
        iterable = self._gen(node.iterable)
        if node.condition:
            cond = self._gen(node.condition)
            return f"[{expr} for {var} in {iterable} if {cond}]"
        return f"[{expr} for {var} in {iterable}]"

    def _gen_DictLiteral(self, node: DictLiteral) -> str:
        pairs = ", ".join(f"{self._gen(k)}: {self._gen(v)}" for k, v in node.pairs)
        return f"{{{pairs}}}"

    def _gen_NewExpression(self, node: NewExpression) -> str:
        args = ", ".join(self._gen(a) for a in node.args)
        return f"{node.class_name}({args})"

    def _gen_LambdaExpression(self, node: LambdaExpression) -> str:
        params = ", ".join(node.params)
        return f"lambda {params}: {self._gen(node.body)}"

    def _gen_OptionalChain(self, node: OptionalChain) -> str:
        obj = self._gen(node.object)
        return f"({obj}.{node.property} if {obj} is not None else None)"

    def _gen_NullishCoalesce(self, node: NullishCoalesce) -> str:
        left = self._gen(node.left)
        return f"({left} if {left} is not None else {self._gen(node.right)})"
