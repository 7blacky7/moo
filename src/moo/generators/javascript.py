"""JavaScript Code-Generator — erzeugt JS-Code aus dem moo AST."""

from ..ast_nodes import (
    Assignment, BinaryOp, BooleanLiteral, BreakStatement, ClassDef,
    CompoundAssignment, ConstAssignment, ContinueStatement, DictLiteral,
    ExportStatement, ForLoop, FunctionCall, FunctionDef, Identifier,
    IfStatement, ImportStatement, IndexAccess, IndexAssignment, LambdaExpression,
    ListLiteral, MatchStatement, MethodCall, NewExpression, Node, NoneLiteral,
    NumberLiteral, Program, PropertyAccess, PropertyAssignment, ReturnStatement,
    ShowStatement, StringLiteral, ThisExpression, ThrowStatement, TryCatch,
    UnaryOp, WhileLoop,
)


class JavaScriptGenerator:
    def __init__(self):
        self.indent = 0
        self._known_vars: set[str] = set()

    def generate(self, program: Program) -> str:
        self._known_vars.clear()
        lines = []
        for stmt in program.statements:
            lines.append(self._gen(stmt))
        return "\n".join(lines) + "\n"

    def _prefix(self) -> str:
        return "  " * self.indent

    def _gen(self, node: Node) -> str:
        method = f"_gen_{type(node).__name__}"
        gen = getattr(self, method, None)
        if not gen:
            raise NotImplementedError(f"JS-Generator: {type(node).__name__} nicht implementiert")
        return gen(node)

    def _gen_block(self, body: list[Node]) -> list[str]:
        self.indent += 1
        lines = [self._gen(s) for s in body]
        self.indent -= 1
        return lines

    # === Statements ===

    def _gen_Assignment(self, node: Assignment) -> str:
        if node.name in self._known_vars:
            return f"{self._prefix()}{node.name} = {self._gen(node.value)};"
        self._known_vars.add(node.name)
        return f"{self._prefix()}let {node.name} = {self._gen(node.value)};"

    def _gen_ConstAssignment(self, node: ConstAssignment) -> str:
        self._known_vars.add(node.name)
        return f"{self._prefix()}const {node.name} = {self._gen(node.value)};"

    def _gen_CompoundAssignment(self, node: CompoundAssignment) -> str:
        return f"{self._prefix()}{node.name} {node.op} {self._gen(node.value)};"

    def _gen_PropertyAssignment(self, node: PropertyAssignment) -> str:
        return f"{self._prefix()}{self._gen(node.object)}.{node.property} = {self._gen(node.value)};"

    def _gen_IndexAssignment(self, node: IndexAssignment) -> str:
        return f"{self._prefix()}{self._gen(node.object)}[{self._gen(node.index)}] = {self._gen(node.value)};"

    def _gen_ShowStatement(self, node: ShowStatement) -> str:
        return f"{self._prefix()}console.log({self._gen(node.value)});"

    def _gen_IfStatement(self, node: IfStatement) -> str:
        lines = [f"{self._prefix()}if ({self._gen(node.condition)}) {{"]
        lines.extend(self._gen_block(node.body))
        if node.else_body:
            if len(node.else_body) == 1 and isinstance(node.else_body[0], IfStatement):
                inner = self._gen_IfStatement(node.else_body[0]).lstrip()
                lines.append(f"{self._prefix()}}} else {inner}")
            else:
                lines.append(f"{self._prefix()}}} else {{")
                lines.extend(self._gen_block(node.else_body))
                lines.append(f"{self._prefix()}}}")
        else:
            lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    def _gen_WhileLoop(self, node: WhileLoop) -> str:
        lines = [f"{self._prefix()}while ({self._gen(node.condition)}) {{"]
        lines.extend(self._gen_block(node.body))
        lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    def _gen_ForLoop(self, node: ForLoop) -> str:
        lines = [f"{self._prefix()}for (const {node.var_name} of {self._gen(node.iterable)}) {{"]
        lines.extend(self._gen_block(node.body))
        lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    def _gen_FunctionDef(self, node: FunctionDef) -> str:
        params_parts = []
        for i, p in enumerate(node.params):
            if node.defaults and node.defaults[i] is not None:
                params_parts.append(f"{p} = {self._gen(node.defaults[i])}")
            else:
                params_parts.append(p)
        params = ", ".join(params_parts)
        lines = [f"{self._prefix()}function {node.name}({params}) {{"]
        lines.extend(self._gen_block(node.body))
        lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    def _gen_ReturnStatement(self, node: ReturnStatement) -> str:
        if node.value:
            return f"{self._prefix()}return {self._gen(node.value)};"
        return f"{self._prefix()}return;"

    def _gen_BreakStatement(self, node: BreakStatement) -> str:
        return f"{self._prefix()}break;"

    def _gen_ContinueStatement(self, node: ContinueStatement) -> str:
        return f"{self._prefix()}continue;"

    def _gen_ClassDef(self, node: ClassDef) -> str:
        extends = f" extends {node.parent}" if node.parent else ""
        lines = [f"{self._prefix()}class {node.name}{extends} {{"]
        self.indent += 1
        for stmt in node.body:
            if isinstance(stmt, FunctionDef):
                name = stmt.name
                if name in ("erstelle", "create"):
                    name = "constructor"
                params_parts = []
                for i, p in enumerate(stmt.params):
                    if stmt.defaults and stmt.defaults[i] is not None:
                        params_parts.append(f"{p} = {self._gen(stmt.defaults[i])}")
                    else:
                        params_parts.append(p)
                params = ", ".join(params_parts)
                lines.append(f"{self._prefix()}{name}({params}) {{")
                lines.extend(self._gen_block(stmt.body))
                lines.append(f"{self._prefix()}}}")
            else:
                lines.append(self._gen(stmt))
        self.indent -= 1
        lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    def _gen_TryCatch(self, node: TryCatch) -> str:
        lines = [f"{self._prefix()}try {{"]
        lines.extend(self._gen_block(node.try_body))
        catch_var = node.catch_var or "e"
        lines.append(f"{self._prefix()}}} catch ({catch_var}) {{")
        lines.extend(self._gen_block(node.catch_body))
        lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    def _gen_ThrowStatement(self, node: ThrowStatement) -> str:
        return f"{self._prefix()}throw new Error({self._gen(node.value)});"

    def _gen_ImportStatement(self, node: ImportStatement) -> str:
        if node.names:
            names = ", ".join(node.names)
            return f"{self._prefix()}import {{ {names} }} from \"{node.module}\";"
        alias = node.alias or node.module
        return f"{self._prefix()}import {alias} from \"{node.module}\";"

    def _gen_ExportStatement(self, node: ExportStatement) -> str:
        inner = self._gen(node.statement)
        return f"{self._prefix()}export {inner.lstrip()}"

    def _gen_MatchStatement(self, node: MatchStatement) -> str:
        lines = [f"{self._prefix()}switch ({self._gen(node.value)}) {{"]
        self.indent += 1
        for pattern, body in node.cases:
            if pattern is None:
                lines.append(f"{self._prefix()}default:")
            else:
                lines.append(f"{self._prefix()}case {self._gen(pattern)}:")
            lines.extend(self._gen_block(body))
            lines.append(f"{self._prefix()}  break;")
        self.indent -= 1
        lines.append(f"{self._prefix()}}}")
        return "\n".join(lines)

    # === Expressions ===

    def _gen_NumberLiteral(self, node: NumberLiteral) -> str:
        return repr(node.value)

    def _gen_StringLiteral(self, node: StringLiteral) -> str:
        escaped = node.value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")
        return f'"{escaped}"'

    def _gen_BooleanLiteral(self, node: BooleanLiteral) -> str:
        return "true" if node.value else "false"

    def _gen_NoneLiteral(self, node: NoneLiteral) -> str:
        return "null"

    def _gen_Identifier(self, node: Identifier) -> str:
        return node.name

    def _gen_ThisExpression(self, node: ThisExpression) -> str:
        return "this"

    def _gen_BinaryOp(self, node: BinaryOp) -> str:
        op = node.op
        if op == "and":
            op = "&&"
        elif op == "or":
            op = "||"
        elif op == "==":
            op = "==="
        elif op == "!=":
            op = "!=="
        return f"({self._gen(node.left)} {op} {self._gen(node.right)})"

    def _gen_UnaryOp(self, node: UnaryOp) -> str:
        op = "!" if node.op == "not" else node.op
        return f"({op}{self._gen(node.operand)})"

    def _gen_FunctionCall(self, node: FunctionCall) -> str:
        args = ", ".join(self._gen(a) for a in node.args)
        return f"{node.name}({args})"

    def _gen_MethodCall(self, node: MethodCall) -> str:
        args = ", ".join(self._gen(a) for a in node.args)
        return f"{self._gen(node.object)}.{node.method}({args})"

    def _gen_PropertyAccess(self, node: PropertyAccess) -> str:
        return f"{self._gen(node.object)}.{node.property}"

    def _gen_IndexAccess(self, node: IndexAccess) -> str:
        return f"{self._gen(node.object)}[{self._gen(node.index)}]"

    def _gen_ListLiteral(self, node: ListLiteral) -> str:
        elements = ", ".join(self._gen(e) for e in node.elements)
        return f"[{elements}]"

    def _gen_DictLiteral(self, node: DictLiteral) -> str:
        pairs = ", ".join(f"{self._gen(k)}: {self._gen(v)}" for k, v in node.pairs)
        return f"{{{pairs}}}"

    def _gen_NewExpression(self, node: NewExpression) -> str:
        args = ", ".join(self._gen(a) for a in node.args)
        return f"new {node.class_name}({args})"

    def _gen_LambdaExpression(self, node: LambdaExpression) -> str:
        params = ", ".join(node.params)
        return f"({params}) => {self._gen(node.body)}"
