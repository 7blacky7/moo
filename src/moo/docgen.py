"""Dokumentations-Generator für moo — erzeugt Markdown-API-Doku aus ## Kommentaren."""

import re
from pathlib import Path


def extract_docs(source: str, filename: str = "") -> list[dict]:
    """Extrahiert Dokumentation aus ## Kommentaren vor Funktionen/Klassen.

    Erkennt:
    - ## Kommentare (Doc-Kommentare)
    - funktion/func Name(params)
    - klasse/class Name
    - daten klasse/data class Name(felder)
    """
    entries: list[dict] = []
    lines = source.split("\n")
    doc_lines: list[str] = []

    for i, line in enumerate(lines):
        stripped = line.strip()

        # Doc-Kommentar sammeln
        if stripped.startswith("##"):
            doc_lines.append(stripped[2:].strip())
            continue

        # Funktion erkennen
        func_match = re.match(
            r"(?:funktion|func)\s+(\w+)\s*\(([^)]*)\)\s*:", stripped
        )
        if func_match:
            name = func_match.group(1)
            params = [p.strip() for p in func_match.group(2).split(",") if p.strip()]
            doc = _parse_doc(doc_lines)
            entries.append({
                "type": "function",
                "name": name,
                "params": params,
                "doc": doc,
                "line": i + 1,
                "file": filename,
            })
            doc_lines = []
            continue

        # Klasse erkennen
        class_match = re.match(
            r"(?:klasse|class)\s+(\w+)(?:\s*\((\w+)\))?\s*:", stripped
        )
        if class_match:
            name = class_match.group(1)
            parent = class_match.group(2)
            doc = _parse_doc(doc_lines)
            entries.append({
                "type": "class",
                "name": name,
                "parent": parent,
                "doc": doc,
                "line": i + 1,
                "file": filename,
            })
            doc_lines = []
            continue

        # Data-Klasse erkennen
        data_match = re.match(
            r"(?:daten\s+klasse|data\s+class)\s+(\w+)\s*\(([^)]*)\)", stripped
        )
        if data_match:
            name = data_match.group(1)
            fields = [f.strip() for f in data_match.group(2).split(",") if f.strip()]
            doc = _parse_doc(doc_lines)
            entries.append({
                "type": "data_class",
                "name": name,
                "fields": fields,
                "doc": doc,
                "line": i + 1,
                "file": filename,
            })
            doc_lines = []
            continue

        # Nicht-Doc-Zeile → Doc-Buffer leeren
        if stripped and not stripped.startswith("#"):
            doc_lines = []

    return entries


def _parse_doc(lines: list[str]) -> dict:
    """Parst Doc-Kommentare in strukturierte Doku."""
    description = []
    params: list[dict] = []
    returns = ""

    for line in lines:
        param_match = re.match(r"Parameter:\s*(.*)", line, re.IGNORECASE)
        if param_match:
            # Mehrere Parameter: "a - erste Zahl, b - zweite Zahl"
            for part in param_match.group(1).split(","):
                part = part.strip()
                if " - " in part:
                    pname, pdesc = part.split(" - ", 1)
                    params.append({"name": pname.strip(), "desc": pdesc.strip()})
                elif part:
                    params.append({"name": part, "desc": ""})
            continue

        ret_match = re.match(r"(?:Gibt|Returns?):\s*(.*)", line, re.IGNORECASE)
        if ret_match:
            returns = ret_match.group(1).strip()
            continue

        description.append(line)

    return {
        "description": " ".join(description).strip(),
        "params": params,
        "returns": returns,
    }


def generate_markdown(entries: list[dict], title: str = "moo API-Dokumentation") -> str:
    """Erzeugt Markdown-Dokumentation aus extrahierten Einträgen."""
    lines = [f"# {title}", ""]

    # Gruppiere nach Dateien
    files: dict[str, list[dict]] = {}
    for entry in entries:
        fname = entry.get("file", "")
        files.setdefault(fname, []).append(entry)

    for fname, file_entries in files.items():
        if fname:
            lines.append(f"## {fname}")
            lines.append("")

        # Funktionen
        funcs = [e for e in file_entries if e["type"] == "function"]
        if funcs:
            lines.append("### Funktionen")
            lines.append("")
            for f in funcs:
                params_str = ", ".join(f["params"])
                lines.append(f"#### `{f['name']}({params_str})`")
                lines.append("")
                doc = f["doc"]
                if doc["description"]:
                    lines.append(doc["description"])
                    lines.append("")
                if doc["params"]:
                    lines.append("**Parameter:**")
                    for p in doc["params"]:
                        lines.append(f"- `{p['name']}` — {p['desc']}")
                    lines.append("")
                if doc["returns"]:
                    lines.append(f"**Gibt zurück:** {doc['returns']}")
                    lines.append("")
                lines.append(f"*Zeile {f['line']}*")
                lines.append("")
                lines.append("---")
                lines.append("")

        # Klassen
        classes = [e for e in file_entries if e["type"] == "class"]
        if classes:
            lines.append("### Klassen")
            lines.append("")
            for c in classes:
                parent = f" (erbt von {c['parent']})" if c.get("parent") else ""
                lines.append(f"#### `{c['name']}`{parent}")
                lines.append("")
                doc = c["doc"]
                if doc["description"]:
                    lines.append(doc["description"])
                    lines.append("")
                lines.append(f"*Zeile {c['line']}*")
                lines.append("")
                lines.append("---")
                lines.append("")

        # Data-Klassen
        data_classes = [e for e in file_entries if e["type"] == "data_class"]
        if data_classes:
            lines.append("### Daten-Klassen")
            lines.append("")
            for dc in data_classes:
                fields_str = ", ".join(dc["fields"])
                lines.append(f"#### `{dc['name']}({fields_str})`")
                lines.append("")
                doc = dc["doc"]
                if doc["description"]:
                    lines.append(doc["description"])
                    lines.append("")
                lines.append(f"**Felder:** {', '.join(f'`{f}`' for f in dc['fields'])}")
                lines.append("")
                lines.append(f"*Zeile {dc['line']}*")
                lines.append("")
                lines.append("---")
                lines.append("")

    return "\n".join(lines)
