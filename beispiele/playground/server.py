"""moo Playground Server — transpiliert und fuehrt moo-Code aus."""

import http.server
import json
import io
import sys
import contextlib
from pathlib import Path

# moo-Projekt zum Python-Path hinzufuegen
sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "src"))

from moo.lexer import Lexer, LexerError
from moo.parser import Parser, ParseError
from moo.generators.python import PythonGenerator


def run_moo(source: str) -> dict:
    """Transpiliert moo-Code zu Python und fuehrt ihn aus."""
    try:
        tokens = Lexer(source).tokenize()
        ast = Parser(tokens).parse()
        py_code = PythonGenerator().generate(ast)
    except (LexerError, ParseError) as e:
        return {"ok": False, "error": str(e), "python": ""}

    # Python-Code ausfuehren und Output erfassen
    stdout_capture = io.StringIO()
    try:
        with contextlib.redirect_stdout(stdout_capture):
            exec(py_code, {"__builtins__": __builtins__})
        return {"ok": True, "output": stdout_capture.getvalue(), "python": py_code}
    except Exception as e:
        return {
            "ok": False,
            "error": f"Laufzeitfehler: {e}",
            "output": stdout_capture.getvalue(),
            "python": py_code,
        }


class PlaygroundHandler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path == "/run":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length))
            result = run_moo(body.get("code", ""))
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(result).encode())
        else:
            self.send_error(404)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self.path = "/index.html"
        super().do_GET()


if __name__ == "__main__":
    port = 8080
    server = http.server.HTTPServer(("0.0.0.0", port), PlaygroundHandler)
    print(f"moo Playground: http://localhost:{port}")
    server.serve_forever()
