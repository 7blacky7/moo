//! P2-T100: Letztes Netz fuer Compiler-Panics (Diagnose M-C999 statt Absturz).
//!
//! Zwei Stufen:
//! 1. `im_netz` (compile_program): faengt einen unwindenden Panic im Codegen
//!    mit catch_unwind ab. Der Aufrufer meldet M-C999 mit Ort und dem
//!    innersten offenen Builtin-Aufruf (Kontext-Stapel).
//! 2. Der Panic-Hook (`netz_installieren`, einmal in main): Panics ausserhalb
//!    von `im_netz` (Parser, Importe, Typpruefung, Linkaufruf ...) im
//!    Hauptthread werden als M-C999 ausgegeben, RC 1. Andere Threads und
//!    MOO_PANIK_ROH=1 gehen an den vorherigen Hook (Rust-Standardausgabe).
//!
//! Grenzen: Nur unwindende Rust-Panics. abort (Panic im Panic, panic=abort),
//! Signale aus C/LLVM und Stapelueberlauf faengt das Netz nicht.

use std::cell::{Cell, RefCell};
use std::panic::{AssertUnwindSafe, PanicHookInfo};

/// Ein offener Builtin-Aufruf (Name, Anzahl der Argumente, Moo-Zeile).
#[derive(Clone, Debug)]
pub struct Aufruf {
    pub name: String,
    pub anzahl: usize,
    pub zeile: usize,
}

/// Abgefangener Panic: Rust-Meldung mit Rust-Ort und innerster Aufruf.
/// `nachricht` ist die rohe Panic-Meldung, `ort` der Rust-Ort (Datei, Zeile,
/// Spalte), soweit der Hook ihn festhalten konnte.
#[derive(Debug)]
pub struct Panik {
    pub text: String,
    pub nachricht: String,
    pub ort: Option<(String, u32, u32)>,
    pub aufruf: Option<Aufruf>,
}

impl Panik {
    pub fn meldung(&self) -> String {
        match &self.aufruf {
            Some(a) => format!(
                "Interner Compilerfehler: {} (innerster offener Builtin-Aufruf: '{}' mit {} Argument(en))",
                self.text, a.name, a.anzahl
            ),
            None => format!("Interner Compilerfehler: {}", self.text),
        }
    }
}

thread_local! {
    static KONTEXT: RefCell<Vec<Aufruf>> = const { RefCell::new(Vec::new()) };
    /// true, solange `im_netz` auf diesem Thread einen Panic abfaengt.
    static NETZ: Cell<bool> = const { Cell::new(false) };
    /// true auf dem Thread, der `netz_installieren` aufgerufen hat (CLI-Hauptthread).
    static ZUSTAENDIG: Cell<bool> = const { Cell::new(false) };
    /// Vom Hook still festgehaltene Meldung und Ort fuer `im_netz`.
    static LETZTER: RefCell<Option<(String, Option<(String, u32, u32)>)>> = const { RefCell::new(None) };
}

/// Haelt einen Builtin-Aufruf auf dem Kontext-Stapel. Normales Verlassen
/// (Ok oder Err) nimmt ihn ab; beim Unwind bleibt er stehen, damit `im_netz`
/// den innersten Aufruf noch sieht (es raeumt danach selbst auf).
pub struct AufrufWache {
    tiefe: usize,
}

pub fn aufruf_betreten(name: &str, anzahl: usize, zeile: usize) -> AufrufWache {
    KONTEXT.with(|k| {
        let mut k = k.borrow_mut();
        let tiefe = k.len();
        k.push(Aufruf { name: name.to_string(), anzahl, zeile });
        AufrufWache { tiefe }
    })
}

impl Drop for AufrufWache {
    fn drop(&mut self) {
        if std::thread::panicking() {
            return;
        }
        let _ = KONTEXT.try_with(|k| {
            if let Ok(mut k) = k.try_borrow_mut() {
                k.truncate(self.tiefe);
            }
        });
    }
}

fn panik_nachricht(info: &PanicHookInfo<'_>) -> String {
    payload_text(info.payload())
}

fn mit_ort(nachricht: &str, ort: &Option<(String, u32, u32)>) -> String {
    match ort {
        Some((datei, zeile, _)) => format!("{nachricht} [{datei}:{zeile}]"),
        None => nachricht.to_string(),
    }
}

fn panik_ort(info: &PanicHookInfo<'_>) -> Option<(String, u32, u32)> {
    info.location().map(|o| (o.file().to_string(), o.line(), o.column()))
}

fn panik_text(info: &PanicHookInfo<'_>) -> String {
    mit_ort(&panik_nachricht(info), &panik_ort(info))
}

fn payload_text(payload: &(dyn std::any::Any + Send)) -> String {
    if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else {
        "Panic ohne Text".to_string()
    }
}

/// Installiert den Hook (prozessweit genau einmal) und macht den aufrufenden
/// Thread zustaendig. Der Hook greift nur auf zustaendigen Threads bzw. in
/// `im_netz`; alles andere geht unveraendert an den vorherigen Hook.
pub fn netz_installieren() {
    static EINMAL: std::sync::Once = std::sync::Once::new();
    EINMAL.call_once(|| {
        let vorher = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |info| {
            if std::env::var_os("MOO_PANIK_ROH").is_some() {
                return vorher(info);
            }
            if NETZ.try_with(|n| n.get()).unwrap_or(false) {
                let festgehalten = (panik_nachricht(info), panik_ort(info));
                let _ = LETZTER.try_with(|l| {
                    if let Ok(mut l) = l.try_borrow_mut() {
                        *l = Some(festgehalten);
                    }
                });
                return;
            }
            if ZUSTAENDIG.try_with(|z| z.get()).unwrap_or(false) {
                let aufruf = KONTEXT.try_with(|k| k.try_borrow().ok().and_then(|k| k.last().cloned()))
                    .ok().flatten();
                let p = Panik { text: panik_text(info), nachricht: panik_nachricht(info), ort: panik_ort(info), aufruf };
                let mut d = crate::diagnose::Diagnose::fehler("M-C999", p.meldung());
                if let Some(a) = &p.aufruf {
                    if a.zeile > 0 {
                        d = d.ort(a.zeile, 0);
                    }
                }
                crate::diagnose::notfall_abschliessen(d);
                std::process::exit(1);
            }
            vorher(info)
        }));
    });
    ZUSTAENDIG.with(|z| z.set(true));
}

/// Fuehrt `f` aus und faengt einen unwindenden Panic ab. Verschachtelte
/// Aufrufe sind erlaubt (der aeussere Zustand wird wiederhergestellt).
pub fn im_netz<R>(f: impl FnOnce() -> R) -> Result<R, Panik> {
    let tiefe = KONTEXT.with(|k| k.borrow().len());
    let vorher = NETZ.with(|n| n.replace(true));
    let ergebnis = std::panic::catch_unwind(AssertUnwindSafe(f));
    NETZ.with(|n| n.set(vorher));
    match ergebnis {
        Ok(r) => Ok(r),
        Err(payload) => {
            let (nachricht, ort) = LETZTER.with(|l| l.borrow_mut().take())
                .unwrap_or_else(|| (payload_text(payload.as_ref()), None));
            let text = mit_ort(&nachricht, &ort);
            let aufruf = KONTEXT.with(|k| {
                let mut k = k.borrow_mut();
                let a = if k.len() > tiefe { k.last().cloned() } else { None };
                k.truncate(tiefe);
                a
            });
            Err(Panik { text, nachricht, ort, aufruf })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn innen(panik: bool) -> usize {
        let _w = aufruf_betreten("innen", 1, 7);
        if panik {
            let v: Vec<u8> = Vec::new();
            return v[3] as usize;
        }
        1
    }

    #[test]
    fn innerer_panic_meldet_inneren_aufruf() {
        netz_installieren();
        let r = im_netz(|| {
            let _w = aufruf_betreten("aussen", 2, 3);
            innen(true)
        });
        let p = r.unwrap_err();
        assert_eq!(p.aufruf.as_ref().unwrap().name, "innen");
        assert!(p.text.contains("index out of bounds"), "{}", p.text);
        assert_eq!(KONTEXT.with(|k| k.borrow().len()), 0);
    }

    #[test]
    fn aeusserer_panic_nach_innerem_erfolg_meldet_aeusseren() {
        netz_installieren();
        let r = im_netz(|| {
            let _w = aufruf_betreten("aussen", 2, 3);
            let _ = innen(false);
            let v: Vec<u8> = Vec::new();
            v[1]
        });
        let p = r.unwrap_err();
        assert_eq!(p.aufruf.as_ref().unwrap().name, "aussen");
        assert_eq!(p.aufruf.as_ref().unwrap().zeile, 3);
    }

    #[test]
    fn danach_unabhaengiger_lauf_ohne_altlast() {
        netz_installieren();
        let _ = im_netz(|| {
            let _w = aufruf_betreten("x", 0, 1);
            innen(true)
        });
        let r = im_netz(|| {
            let v: Vec<u8> = Vec::new();
            v[0]
        });
        let p = r.unwrap_err();
        assert!(p.aufruf.is_none());
        assert_eq!(im_netz(|| 5).unwrap(), 5);
    }
}
