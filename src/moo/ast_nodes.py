"""AST-Knoten für moo — sprachunabhängige Programmstruktur."""

from dataclasses import dataclass, field


@dataclass
class Node:
    line: int = 0


# === Expressions ===

@dataclass
class NumberLiteral(Node):
    value: int | float = 0

@dataclass
class StringLiteral(Node):
    value: str = ""

@dataclass
class BooleanLiteral(Node):
    value: bool = False

@dataclass
class NoneLiteral(Node):
    pass

@dataclass
class Identifier(Node):
    name: str = ""

@dataclass
class BinaryOp(Node):
    left: Node = field(default_factory=Node)
    op: str = ""
    right: Node = field(default_factory=Node)

@dataclass
class UnaryOp(Node):
    op: str = ""
    operand: Node = field(default_factory=Node)

@dataclass
class FunctionCall(Node):
    name: str = ""
    args: list[Node] = field(default_factory=list)

@dataclass
class MethodCall(Node):
    object: Node = field(default_factory=Node)
    method: str = ""
    args: list[Node] = field(default_factory=list)

@dataclass
class PropertyAccess(Node):
    object: Node = field(default_factory=Node)
    property: str = ""

@dataclass
class IndexAccess(Node):
    object: Node = field(default_factory=Node)
    index: Node = field(default_factory=Node)

@dataclass
class RangeExpr(Node):
    start: Node = field(default_factory=Node)
    end: Node = field(default_factory=Node)

@dataclass
class ListLiteral(Node):
    elements: list[Node] = field(default_factory=list)

@dataclass
class ListComprehension(Node):
    expr: Node = field(default_factory=Node)
    var_name: str = ""
    iterable: Node = field(default_factory=Node)
    condition: Node | None = None

@dataclass
class DictLiteral(Node):
    pairs: list[tuple[Node, Node]] = field(default_factory=list)

@dataclass
class NewExpression(Node):
    class_name: str = ""
    args: list[Node] = field(default_factory=list)

@dataclass
class ThisExpression(Node):
    pass

@dataclass
class LambdaExpression(Node):
    params: list[str] = field(default_factory=list)
    body: Node = field(default_factory=Node)

@dataclass
class OptionalChain(Node):
    object: Node = field(default_factory=Node)
    property: str = ""

@dataclass
class NullishCoalesce(Node):
    left: Node = field(default_factory=Node)
    right: Node = field(default_factory=Node)

@dataclass
class PipeExpr(Node):
    left: Node = field(default_factory=Node)
    right: Node = field(default_factory=Node)  # Must be a FunctionCall or MethodCall

@dataclass
class SpreadExpr(Node):
    expr: Node = field(default_factory=Node)


# === Statements ===

@dataclass
class Assignment(Node):
    name: str = ""
    value: Node = field(default_factory=Node)

@dataclass
class ConstAssignment(Node):
    name: str = ""
    value: Node = field(default_factory=Node)

@dataclass
class CompoundAssignment(Node):
    name: str = ""
    op: str = ""  # += or -=
    value: Node = field(default_factory=Node)

@dataclass
class PropertyAssignment(Node):
    object: Node = field(default_factory=Node)
    property: str = ""
    value: Node = field(default_factory=Node)

@dataclass
class IndexAssignment(Node):
    object: Node = field(default_factory=Node)
    index: Node = field(default_factory=Node)
    value: Node = field(default_factory=Node)

@dataclass
class ShowStatement(Node):
    value: Node = field(default_factory=Node)

@dataclass
class IfStatement(Node):
    condition: Node = field(default_factory=Node)
    body: list[Node] = field(default_factory=list)
    else_body: list[Node] = field(default_factory=list)

@dataclass
class WhileLoop(Node):
    condition: Node = field(default_factory=Node)
    body: list[Node] = field(default_factory=list)

@dataclass
class ForLoop(Node):
    var_name: str = ""
    iterable: Node = field(default_factory=Node)
    body: list[Node] = field(default_factory=list)

@dataclass
class FunctionDef(Node):
    name: str = ""
    params: list[str] = field(default_factory=list)
    defaults: list[Node | None] = field(default_factory=list)
    body: list[Node] = field(default_factory=list)
    decorators: list[str] = field(default_factory=list)
    is_async: bool = False

@dataclass
class AwaitExpr(Node):
    """warte/await Expression."""
    expr: Node = field(default_factory=Node)

@dataclass
class ReturnStatement(Node):
    value: Node | None = None

@dataclass
class BreakStatement(Node):
    pass

@dataclass
class ContinueStatement(Node):
    pass

@dataclass
class ClassDef(Node):
    name: str = ""
    parent: str | None = None
    body: list[Node] = field(default_factory=list)

@dataclass
class TryCatch(Node):
    try_body: list[Node] = field(default_factory=list)
    catch_var: str | None = None
    catch_body: list[Node] = field(default_factory=list)

@dataclass
class ThrowStatement(Node):
    value: Node = field(default_factory=Node)

@dataclass
class ImportStatement(Node):
    module: str = ""
    names: list[str] = field(default_factory=list)
    alias: str | None = None

@dataclass
class ExportStatement(Node):
    statement: Node = field(default_factory=Node)

@dataclass
class MatchStatement(Node):
    value: Node = field(default_factory=Node)
    cases: list[tuple[Node | None, list[Node]]] = field(default_factory=list)  # None = default

@dataclass
class MatchExpr(Node):
    """Match als Expression (Kotlin when) — gibt einen Wert zurück."""
    value: Node = field(default_factory=Node)
    cases: list[tuple[Node | None, Node | None, Node]] = field(default_factory=list)  # (pattern, guard, result_expr)

@dataclass
class DataClassDef(Node):
    """Data-Klasse (Kotlin-inspiriert): daten klasse Name(feld1, feld2)."""
    name: str = ""
    fields: list[str] = field(default_factory=list)

@dataclass
class Program(Node):
    statements: list[Node] = field(default_factory=list)
