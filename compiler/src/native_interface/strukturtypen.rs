//! Instanziierung nativer Strukturvorlagen vor Layout und Methodenregistrierung.

use super::*;

#[derive(Default)]
pub(super) struct Strukturtypen {
    vorlagen: BTreeMap<String, Stmt>,
    instanzen: BTreeMap<String, Stmt>,
    bindungen: HashMap<String, TypeRef>,
    tiefe: usize,
}

impl Strukturtypen {
    pub(super) fn vorlagen_sammeln(&mut self, statements: &mut Vec<Stmt>) -> Result<(), String> {
        let mut normal = Vec::with_capacity(statements.len());
        for stmt in std::mem::take(statements) {
            match &stmt {
                Stmt::StructDef { name, typparameter, zeile, .. } if !typparameter.is_empty() => {
                    let (name, zeile) = (name.clone(), *zeile);
                    if self.vorlagen.insert(name.clone(), stmt).is_some() {
                        return Err(error(zeile, format!("Strukturvorlage '{name}' mehrfach deklariert")));
                    }
                }
                _ => normal.push(stmt),
            }
        }
        for stmt in &normal {
            if let Stmt::StructDef { name, zeile, .. } = stmt {
                if self.vorlagen.contains_key(name) {
                    return Err(error(*zeile, format!("Struktur '{name}' und Strukturvorlage kollidieren")));
                }
            }
        }
        *statements = normal;
        Ok(())
    }

    pub(super) fn instanzen(&mut self) -> impl Iterator<Item = Stmt> {
        std::mem::take(&mut self.instanzen).into_values()
    }
}

pub(super) fn typ(ty: &mut TypeRef, used: &mut LoweredTypes) -> Result<bool, String> {
    match &mut ty.kind {
        TypeKind::Name(name) => {
            if let Some(wert) = used.strukturtypen.bindungen.get(name) {
                ty.kind = wert.kind.clone();
                return Ok(true);
            }
            if used.strukturtypen.vorlagen.contains_key(name) {
                return Err(error(ty.line, format!("Strukturvorlage '{name}' braucht Typargumente")));
            }
        }
        TypeKind::Generic { name, args } if used.strukturtypen.vorlagen.contains_key(name) => {
            let mut vorlage = used.strukturtypen.vorlagen[name].clone();
            let Stmt::StructDef { name: deklariert, typparameter, .. } = &mut vorlage else {
                return Err(error(ty.line, "Interne Strukturvorlage fehlt"));
            };
            if typparameter.len() != args.len() {
                return Err(error(ty.line, format!("'{name}' erwartet {} Typargumente, bekommen {}",
                    typparameter.len(), args.len())));
            }
            for arg in args.iter_mut() { lower_type(arg, used)?; }
            let argumente = args.iter().map(|arg| {
                resolve_type(arg, &used.nominals).map(|id| id.to_string()).map_err(|e| error(arg.line, e))
            }).collect::<Result<Vec<_>, _>>()?;
            let instanz = format!("{name}[{}]", argumente.join(", "));
            let stelle = ty.line;
            let mut bindungen: HashMap<_, _> = typparameter.drain(..).zip(args.iter().cloned()).collect();
            bindungen.insert("Self".into(), named(&instanz));
            *deklariert = instanz.clone();
            ty.kind = TypeKind::Name(instanz.clone());
            if used.nominals.contains_key(&instanz) { return Ok(true); }
            if used.strukturtypen.tiefe == 64 {
                return Err(error(stelle, "Zu tief verschachtelte Strukturinstanziierung"));
            }

            // Vor der Absenkung reservieren. ptr[Name[T]] kann damit dieselbe
            // Instanz adressieren. Direkte Wertzyklen prüft der Layouter.
            used.nominals.insert(instanz.clone(), ("native_generic".into(), instanz.clone()));
            let vorher = std::mem::replace(&mut used.strukturtypen.bindungen, bindungen);
            used.strukturtypen.tiefe += 1;
            let ergebnis = lower_statements(std::slice::from_mut(&mut vorlage), used);
            used.strukturtypen.tiefe -= 1;
            used.strukturtypen.bindungen = vorher;
            ergebnis?;
            used.strukturtypen.instanzen.insert(instanz, vorlage);
            return Ok(true);
        }
        _ => {}
    }
    Ok(false)
}

/// Die bestehende Layoutabfrage erwartet einen Namen. Ein Typname aus der
/// Instanz kann direkt eingesetzt werden. Für ptr[T] und Arrays trägt eine
/// Einfeld-Struktur exakt Größe und Ausrichtung des vollständigen Typs.
pub(super) fn layout_argument(name: &str, args: &mut [Expr], used: &mut LoweredTypes) -> Result<(), String> {
    if !matches!(name, "groesse_von" | "größe_von" | "size_of" | "ausrichtung_von" | "align_of") {
        return Ok(());
    }
    let [Expr::Identifier(parameter)] = args else { return Ok(()); };
    let Some(ty) = used.strukturtypen.bindungen.get(parameter) else { return Ok(()); };
    if let TypeKind::Name(name) = &ty.kind {
        *parameter = name.clone();
        return Ok(());
    }
    let identitaet = resolve_type(ty, &used.nominals).map_err(|e| error(ty.line, e))?;
    let layout = format!("__moo_layout[{identitaet}]");
    used.strukturen.entry(layout.clone()).or_insert_with(|| vec![("wert".into(), ty.clone())]);
    *parameter = layout;
    Ok(())
}
