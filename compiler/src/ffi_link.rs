//! Konservative Bibliotheksmetadaten fuer extern-C-Deklarationen.
//! Der Aufrufer muss die Resultate als getrennte argv-Eintraege an den
//! passenden Linker-Treiber uebergeben, nie als Shell-Text.

use crate::ast::{DecoratorArg, Stmt};

/// Bestimmte Reihenfolge folgt der aufgeloesten Programmliste.
/// Diese Funktion linkt nicht selbst und behandelt keine Objekt-/Archivpfade.
fn valid_name(name: &str) -> bool {
    !name.is_empty() && !name.starts_with('-')
        && name.bytes().all(|c| c.is_ascii_alphanumeric()
            || matches!(c, b'_' | b'+' | b'-' | b'.'))
}

fn selected_library<'a>(args: &'a [DecoratorArg], triple: &str) -> Result<&'a str, String> {
    if let [DecoratorArg::Text(name)] = args {
        return Ok(name);
    }
    if args.is_empty() {
        return Err("@bibliothek erwartet einen Namen oder Zielnamen".into());
    }
    let os = triple.split('-').nth(2).unwrap_or("");
    let key = if os == "linux" { "linux" }
        else if os == "windows" || os == "mingw32" { "windows" }
        else if os.starts_with("darwin") || os.starts_with("macos") { "macos" }
        else { return Err(format!("@bibliothek: Zielbetriebssystem in '{triple}' nicht unterstützt")); };
    let mut names = std::collections::HashSet::new();
    let mut selected = None;
    for arg in args {
        let DecoratorArg::NamedText { name, value } = arg else {
            return Err("@bibliothek: benannte und positionsbasierte Argumente nicht mischen".into());
        };
        if !matches!(name.as_str(), "linux" | "windows" | "macos") {
            return Err(format!("@bibliothek: unbekanntes Ziel '{name}'"));
        }
        if !names.insert(name.as_str()) {
            return Err(format!("@bibliothek: Ziel '{name}' doppelt angegeben"));
        }
        if name == key { selected = Some(value.as_str()); }
    }
    selected.ok_or_else(|| format!("@bibliothek: kein Name fuer Ziel '{key}'"))
}

pub(crate) fn libraries(stmts: &[Stmt], triple: &str) -> Result<Vec<String>, String> {
    fn visit(stmt: &Stmt, names: &mut Vec<String>, triple: &str) -> Result<(), String> {
        match stmt {
            Stmt::Export(inner) => visit(inner, names, triple)?,
            Stmt::ExternC { decorators, .. } => {
                for decorator in decorators {
                    match decorator.name.as_str() {
                        "bibliothek" | "library" => {
                            let name = selected_library(&decorator.args, triple)?;
                            // Alle Varianten validieren, auch die nicht gewaehlten.
                            for arg in &decorator.args {
                                let value = match arg {
                                    DecoratorArg::Text(value) | DecoratorArg::NamedText { value, .. } => value,
                                    _ => return Err("@bibliothek erwartet Textnamen".into()),
                                };
                                if !valid_name(value) {
                                    return Err(format!("Ungueltiger Bibliotheksname: '{value}'"));
                                }
                            }
                            // Keine Optionen, Pfade, Shell-Fragmente oder leere Namen.
                            // Aufgeloeste Suchpfade/Objekte sind ein separater Vertrag.
                            if !valid_name(name) {
                                return Err(format!("Ungueltiger Bibliotheksname: '{name}'"));
                            }
                            names.push(name.to_owned()); // Wiederholungen koennen beim statischen Linken relevant sein.
                        }
                        // P7-T4 (7A): Pruefung und Absenkung im Codegen (ffi_types).
                        "aufrufkonvention" | "calling_convention" => {}
                        other => return Err(format!(
                            "Decorator '@{other}' fuer extern C noch nicht unterstuetzt"
                        )),
                    }
                }
            }
            _ => {}
        }
        Ok(())
    }

    let mut names = Vec::new();
    for stmt in stmts {
        visit(stmt, &mut names, triple)?;
    }
    Ok(names)
}

/// MSVC-Umgebung eines COFF-Triples. GNU/MinGW/Cygwin/Itanium-Windows zaehlen
/// nicht dazu; ein Windows-Triple ohne Umgebung ist bei LLVM MSVC.
fn is_msvc_coff(triple: &str) -> bool {
    triple.contains("-windows")
        && !["-gnu", "mingw", "cygnus", "itanium"].iter().any(|env| triple.contains(env))
}

/// P2-T43: @bibliothek als eingebettete Link-Direktive. Auf MSVC-COFF wird je
/// Bibliothek einmal `/DEFAULTLIB:<name>.lib` in `llvm.linker.options` gelegt;
/// LLVM schreibt das in die .drectve-Sektion (wie clang bei
/// `#pragma comment(lib, ...)`), link.exe/lld-link loesen es ohne `-l` auf.
/// GNU-COFF bleibt ohne Direktive: GNU ld ignoriert .drectve-Bibliotheken, dort
/// muss der Treiber `-l` uebergeben (main.rs). ELF/Mach-O/Bare: keine Aenderung.
/// Ohne @bibliothek bleibt das Modul unveraendert (Legacy-IR).
pub(crate) fn embed_link_options<'ctx>(
    context: &'ctx inkwell::context::Context,
    module: &inkwell::module::Module<'ctx>,
    libraries: &[String],
    triple: &str,
) -> Result<(), String> {
    if libraries.is_empty() || !is_msvc_coff(triple) {
        return Ok(());
    }
    let mut seen = std::collections::HashSet::new();
    for name in libraries {
        if !seen.insert(name.as_str()) {
            continue;
        }
        let option = if name.to_ascii_lowercase().ends_with(".lib") {
            format!("/DEFAULTLIB:{name}")
        } else {
            format!("/DEFAULTLIB:{name}.lib")
        };
        let node = context.metadata_node(&[context.metadata_string(&option).into()]);
        module.add_global_metadata("llvm.linker.options", &node)
            .map_err(|e| format!("Link-Direktive '{option}' nicht einbettbar: {e}"))?;
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::is_msvc_coff;

    #[test]
    fn msvc_coff_erkennung() {
        assert!(is_msvc_coff("x86_64-pc-windows-msvc"));
        assert!(is_msvc_coff("aarch64-pc-windows-msvc"));
        assert!(is_msvc_coff("x86_64-pc-windows"));
        assert!(!is_msvc_coff("x86_64-pc-windows-gnu"));
        assert!(!is_msvc_coff("x86_64-w64-windows-gnu"));
        assert!(!is_msvc_coff("x86_64-w64-mingw32"));
        assert!(!is_msvc_coff("x86_64-unknown-linux-gnu"));
        assert!(!is_msvc_coff("x86_64-unknown-none"));
    }
}
