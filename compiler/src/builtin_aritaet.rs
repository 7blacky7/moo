//! P2-T100: Aritaet der Builtin-Funktionen. Die Tabelle erzeugt build.rs
//! (compiler/build_aritaet.rs) bei jedem Build aus dem Builtin-Match in
//! src/codegen.rs, fail-closed: nur Arme mit nachweisbar exakter bzw.
//! Mindestanzahl (Regeln im Kopf von build_aritaet.rs).

include!(concat!(env!("OUT_DIR"), "/builtin_aritaet.rs"));

/// (Anzahl, exakt) des Builtins `name`, falls nachweisbar; nicht exakt
/// heisst: mindestens so viele.
pub fn erwartet(name: &str) -> Option<(usize, bool)> {
    ARITAET.binary_search_by(|(n, _, _)| (*n).cmp(name)).ok().map(|i| (ARITAET[i].1 as usize, ARITAET[i].2))
}

/// P2-T100: Ist ein gefangener Panic nachweislich ein Lesen ueber das Ende der
/// Argumente des Builtins `name`? Nur wenn der Panic-Ort exakt eine
/// `args[..]`-Stelle eines Arms von `name` in codegen.rs ist und die Meldung
/// "index out of bounds: the len is K but the index is I" mit K == `anzahl`
/// lautet. Liefert dann I (gelesener Index), sonst None.
pub fn args_ueberlauf(name: &str, anzahl: usize, datei: &str, zeile: u32, spalte: u32, nachricht: &str) -> Option<usize> {
    if !datei.replace('\\', "/").ends_with("src/codegen.rs") {
        return None;
    }
    let i = ARGS_STELLEN.binary_search_by(|(n, _)| (*n).cmp(name)).ok()?;
    if !ARGS_STELLEN[i].1.contains(&(zeile, spalte)) {
        return None;
    }
    let rest = nachricht.strip_prefix("index out of bounds: the len is ")?;
    let (laenge, rest) = rest.split_once(" but the index is ")?;
    let index: usize = rest.split(|c: char| !c.is_ascii_digit()).next()?.parse().ok()?;
    (laenge.parse::<usize>().ok()? == anzahl && index >= anzahl).then_some(index)
}

/// JSON aller Builtin-Namen des Matchs: Argumentzahl oder (null + Grund)
/// fuer nicht tabellierte Namen.
pub fn als_json() -> String {
    let mut eintraege: Vec<(&str, String)> = ARITAET.iter()
        .map(|(n, a, exakt)| (*n, format!("\"argumente\":{a},\"exakt\":{exakt}")))
        .chain(AUSGESCHLOSSEN.iter().map(|(n, g)| {
            (*n, format!("\"argumente\":null,\"grund\":{}", crate::diagnose::json_string(g)))
        }))
        .collect();
    eintraege.sort();
    let liste: Vec<String> = eintraege.iter()
        .map(|(n, rest)| format!("{{\"name\":{},{rest}}}", crate::diagnose::json_string(n)))
        .collect();
    format!("{{\"arme\":{ARME_GESAMT},\"tabelliert\":{},\"builtins\":[{}]}}", ARITAET.len(), liste.join(","))
}

/// P2-T100 (Review f70ad5ec): die Ableitung selbst, fuer synthetische Gegentests.
#[cfg(test)]
#[path = "../build_aritaet.rs"]
#[allow(dead_code)]
mod ableitung;

#[cfg(test)]
mod ableitung_tests {
    fn pruefe(arm: &str) -> Result<(u8, bool), &'static str> {
        let body: syn::Expr = syn::parse_str(arm).expect("Testarm parsebar");
        super::ableitung::arm_pruefen(&body).0
    }

    #[test]
    fn exakt_und_mindestens() {
        assert_eq!(pruefe("{ let a = self.c(&args[0])?; let b = self.c(&args[1])?; return Ok(a); }"), Ok((2, true)));
        assert_eq!(pruefe("{ let a = args.iter().map(|x| self.c(x)).collect::<Result<Vec<_>, _>>()?; f(a[2]) }"), Ok((3, false)));
    }

    #[test]
    fn veraenderliches_abbild_zaehlt_nicht() {
        // Review f70ad5ec: nach push waere a[2] bei zwei Argumenten gueltig.
        assert!(pruefe("{ let mut a: Vec<_> = args.iter().cloned().collect(); a.push(5); let _ = a[2]; }").is_err());
    }

    #[test]
    fn geschattetes_abbild_zaehlt_nicht() {
        assert!(pruefe("{ let a = args.iter().collect::<Vec<_>>(); let a = vec![1, 2, 3]; f(a[2]) }").is_err());
        assert!(pruefe("{ let a = args.iter().collect::<Vec<_>>(); if c { let a = vec![1]; g(a[0]); } f(a[2]) }").is_err());
        // Closure-Parameter gleichen Namens: a[3] im Closure zaehlt nicht, das aeussere a[0] schon.
        assert_eq!(pruefe("{ let a = args.iter().collect::<Vec<_>>(); let g = |a: Vec<u8>| a[3]; f(a[0]) }"), Ok((1, false)));
        // Muster wie in codegen.rs: Closure-Parameter `a` im Initialisierer.
        assert_eq!(pruefe("{ let a = args.iter().map(|a| self.c(a)).collect::<Result<Vec<_>, _>>()?; f(a[2]) }"), Ok((3, false)));
    }

    #[test]
    fn shadow_args() {
        // Review 5dbf36bc: args neu gebunden (auch als Closure-Muster) -> Err, keine Stellen.
        for arm in [
            "{ let g = |(args, _): (Vec<u8>, u8)| args[0]; f(&args[0]) }",
            "{ let args = vec![1]; f(args[0]) }",
            "{ match x { Some(args) => args[0], None => f(&args[1]) } }",
        ] {
            let body: syn::Expr = syn::parse_str(arm).expect("Testarm parsebar");
            let (ergebnis, stellen) = super::ableitung::arm_pruefen(&body);
            assert!(ergebnis.is_err(), "{arm}");
            assert!(stellen.is_empty(), "{arm}: {stellen:?}");
        }
        let body: syn::Expr = syn::parse_str("{ f(&args[0]) }").expect("Testarm parsebar");
        let (ergebnis, stellen) = super::ableitung::arm_pruefen(&body);
        assert_eq!(ergebnis, Ok((1, true)));
        assert_eq!(stellen.len(), 2);
    }

    #[test]
    fn shadow_tuple() {
        // Review 21765f57: Tuple-/Struct-Muster im Closure verdecken das Abbild;
        // a[3] im Closure darf weder zaehlen noch als Stelle gelten.
        let arm = "{ let a = args.iter().collect::<Vec<_>>(); let g = |(a, b): (Vec<u8>, u8)| a[3]; f(a[0]) }";
        let body: syn::Expr = syn::parse_str(arm).expect("Testarm parsebar");
        let (ergebnis, stellen) = super::ableitung::arm_pruefen(&body);
        assert_eq!(ergebnis, Ok((1, false)));
        assert_eq!(stellen.len(), 2, "nur a[0] (Anfang und Klammer): {stellen:?}");
        let arm = "{ let a = args.iter().collect::<Vec<_>>(); let g = |P { a, .. }: P| a[3]; f(a[0]) }";
        let body: syn::Expr = syn::parse_str(arm).expect("Testarm parsebar");
        assert_eq!(super::ableitung::arm_pruefen(&body).1.len(), 2);
        assert!(pruefe("{ let a = args.iter().collect::<Vec<_>>(); let g = |x: Option<Vec<u8>>| match x { Some(a) => a[3], None => 0 }; g(None) }").is_err());
    }

    #[test]
    fn bedingtes_frueh_ok_zaehlt_nicht() {
        assert!(pruefe("{ if c { return Ok(1); } f(&args[0]) }").is_err());
        // Lesen VOR dem bedingten Ausstieg gilt auf jedem Weg (Muster tls_verbinde).
        assert_eq!(pruefe("{ let h = c(&args[0])?; let p = c(&args[1])?; if args.len() > 2 { return Ok(t(&args[2])); } Ok(x) }"), Ok((2, false)));
        assert_eq!(pruefe("{ f(&args[0]); if c { return Ok(1); } g(&args[1]) }"), Ok((1, false)));
        assert_eq!(pruefe("{ if c { return Err(e); } f(&args[0]) }"), Ok((1, true)));
    }

    #[test]
    fn nicht_laengenerhaltende_kette_zaehlt_nicht() {
        assert!(pruefe("{ let a = args.iter().filter(|x| p(x)).collect::<Vec<_>>(); f(a[1]) }").is_err());
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn tabelle_sortiert_und_eindeutig() {
        assert!(super::ARITAET.windows(2).all(|w| w[0].0 < w[1].0));
        assert!(super::ARITAET.len() > 300, "nur {} Eintraege", super::ARITAET.len());
    }

    #[test]
    fn bekannte_eintraege() {
        assert_eq!(super::erwartet("shard_reihenfolge"), Some((3, true)));
        assert_eq!(super::erwartet("gibt_es_nicht_xyz"), None);
    }
}
