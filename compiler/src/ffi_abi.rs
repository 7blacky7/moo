//! P2-T19/T56 (Plan 91033131 A2): LLVM-freier C-ABI-Klassifizierer fuer
//! Strukturwerte by value. Genau drei ABIs: SysV x86_64 (Linux, bare),
//! MS x64 (Windows gnu/msvc, UEFI, @aufrufkonvention win64/uefi) und AAPCS64
//! (aarch64 Linux, bare). Unions, Bitfelder, Vektortypen, long double,
//! variadische Funktionen und ueberausgerichtete Strukturen (> 16) sind nicht
//! im Umfang.
//!
//! Die Regeln sind am Clang-Orakel kalibriert (clang 22, 22 Formen je Triple
//! x86_64-unknown-linux-gnu, x86_64-pc-windows-msvc, aarch64-unknown-linux-gnu):
//! - SysV: > 16 Byte oder ein Feld nicht natuerlich ausgerichtet (gepackt) ->
//!   Speicher (Argument byval align max(8, a), Rueckgabe sret). Sonst je
//!   Achtbyte GANZZAHL (irgendein Ganzzahl-/Zeiger-/bool-Feld) oder SSE.
//!   GANZZAHL-Registerform: ein Zeiger allein -> ptr; deckt das Feld am
//!   Achtbyte-Anfang alle Daten -> dessen Breite; sonst iN bis Strukturende
//!   (i24, i40, ...). SSE: double, <2 x float> oder float. Fehlen Register
//!   (6 GP abzueglich sret, 8 SSE; Skalare davor zaehlen), geht die ganze
//!   Struktur byval. Ausnahme (Clang getIndirectResult): ist kein GP-Register
//!   mehr frei, geht ein Aggregat <= 8 Byte (Ausrichtung <= 8) als iN direkt
//!   auf den Stack, auch in der Speicherklasse (gepackt).
//! - MS x64: Groesse 1/2/4/8 -> iN (auch bei Float-Feldern), sonst Zeiger
//!   auf eine Aufruferkopie bzw. Rueckgabe sret.
//! - AAPCS64: HFA (1..4 gleiche Floats nach Abflachung, ohne Padding) ->
//!   Argument [n x float|double] (alignstack 8), Rueckgabe {float|double x n};
//!   sonst <= 8 Byte -> Argument i64, Rueckgabe i(8*Groesse); <= 16 Byte ->
//!   [2 x i64]; > 16 Byte -> Zeiger auf Aufruferkopie bzw. sret.

/// Klasse eines flachen Skalarfeldes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Art { Ganzzahl, Zeiger, F32, F64 }

/// Ein Skalarfeld nach dem Abflachen von Unterstrukturen und Arrays.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) struct FlachFeld {
    pub(crate) versatz: u64,
    pub(crate) groesse: u64,
    pub(crate) art: Art,
}

/// Groesse, Ausrichtung und flache Felder einer Struktur (native_struct::flach).
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct StrukturForm {
    pub(crate) groesse: u64,
    pub(crate) ausrichtung: u64,
    pub(crate) felder: Vec<FlachFeld>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Abi { SysV, Ms, Aapcs64 }

impl Abi {
    /// ABI aus dem Zieltriple; eine explizite x86_64-Aufrufkonvention
    /// (win64/uefi -> MS, sysv64 -> SysV) hat Vorrang.
    pub(crate) fn aus_triple(triple: &str, konvention: Option<&str>) -> Result<Self, String> {
        let x86 = triple.starts_with("x86_64-");
        let aarch64 = triple.starts_with("aarch64-");
        match konvention {
            Some("win64" | "uefi") if x86 => return Ok(Abi::Ms),
            Some("sysv64") if x86 => return Ok(Abi::SysV),
            Some("uefi") if aarch64 => return Ok(Abi::Aapcs64),
            Some(k) => return Err(format!(
                "C-ABI: Aufrufkonvention '{k}' fuer Strukturwerte auf '{triple}' nicht unterstuetzt"
            )),
            None => {}
        }
        if x86 && (triple.contains("windows") || triple.contains("-uefi")) {
            return Ok(Abi::Ms);
        }
        if x86 && (triple.contains("linux") || triple.contains("-none")) {
            return Ok(Abi::SysV);
        }
        if aarch64 && (triple.contains("linux") || triple.contains("-none")) {
            return Ok(Abi::Aapcs64);
        }
        Err(format!(
            "C-ABI: Strukturwerte by value fuer Ziel '{triple}' nicht unterstuetzt (SysV x86_64, MS x64, AAPCS64)"
        ))
    }
}

/// Registerform eines Strukturteils (entspricht einem LLVM-Typ).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Teil {
    /// iN mit N Bit.
    Ganz(u32),
    /// ptr (SysV-Achtbyte, das genau einen Zeiger enthaelt).
    Zeiger,
    F32,
    F64,
    /// <2 x float> (SysV-SSE-Achtbyte mit zwei f32).
    F32x2,
    /// [2 x i64] (AAPCS64, 9..16 Byte).
    I64x2,
    /// AAPCS64-HFA: Argument [n x T] mit alignstack(8), Rueckgabe {T x n}.
    Hfa { doppelt: bool, n: u32 },
}

/// Wie eine Struktur uebergeben bzw. zurueckgegeben wird.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum Uebergabe {
    /// In Registern, Teile in Reihenfolge.
    Register(Vec<Teil>),
    /// SysV-MEMORY-Argument: byval-Kopie auf dem Stack.
    Byval { ausrichtung: u64 },
    /// MS x64 / AAPCS64: Zeiger auf eine Kopie des Aufrufers.
    Indirekt,
    /// Rueckgabe ueber den versteckten Ergebniszeiger.
    Sret,
}

impl Uebergabe {
    /// (GP-, SSE-Register) fuer die SysV-Registerbuchhaltung.
    fn register_bedarf(&self) -> (u32, u32) {
        match self {
            Uebergabe::Register(teile) => teile.iter().fold((0, 0), |(g, s), teil| match teil {
                Teil::Ganz(_) | Teil::Zeiger => (g + 1, s),
                Teil::F32 | Teil::F64 | Teil::F32x2 => (g, s + 1),
                Teil::I64x2 => (g + 2, s),
                Teil::Hfa { n, .. } => (g, s + n),
            }),
            _ => (0, 0),
        }
    }
}

/// Klassifiziert eine Struktur als Argument (`rueckgabe == false`) oder
/// als Rueckgabewert, ohne Registerdruck (siehe `plane`).
pub(crate) fn klassifiziere(abi: Abi, form: &StrukturForm, rueckgabe: bool) -> Result<Uebergabe, String> {
    if form.groesse == 0 || form.felder.is_empty() {
        return Err("C-ABI: leere Strukturen sind by value nicht unterstuetzt".into());
    }
    if form.ausrichtung > 16 {
        return Err("C-ABI: ueberausgerichtete Strukturen (> 16) sind by value nicht unterstuetzt".into());
    }
    Ok(match abi {
        Abi::SysV => sysv(form, rueckgabe),
        Abi::Ms if matches!(form.groesse, 1 | 2 | 4 | 8) => Uebergabe::Register(vec![Teil::Ganz((form.groesse * 8) as u32)]),
        Abi::Ms if rueckgabe => Uebergabe::Sret,
        Abi::Ms => Uebergabe::Indirekt,
        Abi::Aapcs64 => aapcs64(form, rueckgabe),
    })
}

fn sysv(form: &StrukturForm, rueckgabe: bool) -> Uebergabe {
    let speicher = if rueckgabe { Uebergabe::Sret } else { Uebergabe::Byval { ausrichtung: form.ausrichtung.max(8) } };
    if form.groesse > 16 || form.felder.iter().any(|f| f.versatz % f.groesse.max(1) != 0) {
        return speicher;
    }
    let mut teile = Vec::new();
    for achtbyte in 0..form.groesse.div_ceil(8) {
        let lo = achtbyte * 8;
        let hi = (lo + 8).min(form.groesse);
        let drin: Vec<&FlachFeld> = form.felder.iter()
            .filter(|f| f.versatz < hi && f.versatz + f.groesse > lo)
            .collect();
        let ganz = drin.iter().any(|f| matches!(f.art, Art::Ganzzahl | Art::Zeiger));
        let teil = if ganz {
            let erstes = drin.iter().find(|f| f.versatz == lo);
            let deckt_alles = erstes.is_some_and(|e| drin.iter().all(|f| f.versatz + f.groesse <= lo + e.groesse));
            match erstes {
                Some(e) if deckt_alles && e.art == Art::Zeiger => Teil::Zeiger,
                Some(e) if deckt_alles && e.art == Art::Ganzzahl => Teil::Ganz((e.groesse * 8) as u32),
                _ => Teil::Ganz(((hi - lo) * 8) as u32),
            }
        } else if drin.iter().any(|f| f.art == Art::F64) {
            Teil::F64
        } else if drin.iter().any(|f| f.versatz >= lo + 4) {
            Teil::F32x2
        } else {
            Teil::F32
        };
        teile.push(teil);
    }
    Uebergabe::Register(teile)
}

fn aapcs64(form: &StrukturForm, rueckgabe: bool) -> Uebergabe {
    let erstes = form.felder[0];
    let n = form.felder.len() as u64;
    let hfa = matches!(erstes.art, Art::F32 | Art::F64)
        && form.felder.iter().all(|f| f.art == erstes.art)
        && (1..=4).contains(&n)
        && form.groesse == erstes.groesse * n;
    if hfa {
        return Uebergabe::Register(vec![Teil::Hfa { doppelt: erstes.art == Art::F64, n: n as u32 }]);
    }
    if form.groesse > 16 {
        return if rueckgabe { Uebergabe::Sret } else { Uebergabe::Indirekt };
    }
    if form.groesse <= 8 {
        let bits = if rueckgabe { form.groesse * 8 } else { 64 };
        return Uebergabe::Register(vec![Teil::Ganz(bits as u32)]);
    }
    Uebergabe::Register(vec![Teil::I64x2])
}

/// Registerklasse eines Skalarparameters (fuer die SysV-Buchhaltung).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum Skalar { Gp, Sse }

#[derive(Clone, Copy, Debug)]
pub(crate) enum ParamForm<'a> {
    Skalar(Skalar),
    Struktur(&'a StrukturForm),
}

/// Uebergabe je Parameter (None = Skalar, unveraendert) und der Rueckgabe.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Signaturplan {
    pub(crate) params: Vec<Option<Uebergabe>>,
    pub(crate) rueckgabe: Option<Uebergabe>,
}

/// Plant eine ganze Signatur. SysV zaehlt die Register ueber ALLE Parameter
/// (6 GP abzueglich sret, 8 SSE); passt eine Registerstruktur nicht mehr
/// vollstaendig, wird sie byval uebergeben und verbraucht keine Register.
pub(crate) fn plane(abi: Abi, params: &[ParamForm<'_>], rueckgabe: Option<&StrukturForm>) -> Result<Signaturplan, String> {
    let rueckgabe = rueckgabe.map(|form| klassifiziere(abi, form, true)).transpose()?;
    let mut gp: u32 = if matches!(rueckgabe, Some(Uebergabe::Sret)) { 5 } else { 6 };
    let mut sse: u32 = 8;
    let mut plan = Vec::with_capacity(params.len());
    for param in params {
        match param {
            ParamForm::Skalar(Skalar::Gp) => { gp = gp.saturating_sub(1); plan.push(None); }
            ParamForm::Skalar(Skalar::Sse) => { sse = sse.saturating_sub(1); plan.push(None); }
            ParamForm::Struktur(form) => {
                let mut uebergabe = klassifiziere(abi, form, false)?;
                if abi == Abi::SysV {
                    let (g, s) = uebergabe.register_bedarf();
                    let im_register = matches!(uebergabe, Uebergabe::Register(_));
                    if im_register && g <= gp && s <= sse {
                        gp -= g;
                        sse -= s;
                    } else if gp == 0 && form.groesse <= 8 && form.ausrichtung <= 8 {
                        // Clang: ohne freie GP-Register kleines Aggregat als iN
                        // auf den Stack (kein byval, verbraucht kein Register).
                        uebergabe = Uebergabe::Register(vec![Teil::Ganz((form.groesse * 8) as u32)]);
                    } else if im_register {
                        uebergabe = Uebergabe::Byval { ausrichtung: form.ausrichtung.max(8) };
                    }
                }
                plan.push(Some(uebergabe));
            }
        }
    }
    Ok(Signaturplan { params: plan, rueckgabe })
}

#[cfg(test)]
mod tests {
    use super::*;
    use Teil::*;
    const G: Art = Art::Ganzzahl;
    const Z: Art = Art::Zeiger;
    const AF32: Art = Art::F32;
    const AF64: Art = Art::F64;

    fn form(groesse: u64, ausrichtung: u64, felder: &[(u64, u64, Art)]) -> StrukturForm {
        StrukturForm {
            groesse, ausrichtung,
            felder: felder.iter().map(|&(versatz, groesse, art)| FlachFeld { versatz, groesse, art }).collect(),
        }
    }
    fn w(n: u64) -> StrukturForm { form(4 * n, 4, &(0..n).map(|i| (4 * i, 4, G)).collect::<Vec<_>>()) }
    fn reg(teile: &[Teil]) -> Uebergabe { Uebergabe::Register(teile.to_vec()) }
    fn arg(abi: Abi, f: &StrukturForm) -> Uebergabe { klassifiziere(abi, f, false).unwrap() }
    fn ret(abi: Abi, f: &StrukturForm) -> Uebergabe { klassifiziere(abi, f, true).unwrap() }

    fn mix() -> StrukturForm { form(16, 8, &[(0, 8, AF64), (8, 4, G)]) }
    fn f4() -> StrukturForm { form(16, 4, &[(0, 4, AF32), (4, 4, AF32), (8, 4, AF32), (12, 4, AF32)]) }
    fn f3() -> StrukturForm { form(12, 4, &[(0, 4, AF32), (4, 4, AF32), (8, 4, AF32)]) }
    fn packed() -> StrukturForm { form(5, 1, &[(0, 1, G), (1, 4, G)]) }
    fn d2() -> StrukturForm { form(16, 8, &[(0, 8, AF64), (8, 8, AF64)]) }
    fn d4() -> StrukturForm { form(32, 8, &[(0, 8, AF64), (8, 8, AF64), (16, 8, AF64), (24, 8, AF64)]) }
    fn f5() -> StrukturForm { form(20, 4, &(0..5).map(|i| (4 * i, 4, AF32)).collect::<Vec<_>>()) }
    fn pu() -> StrukturForm { form(16, 8, &[(0, 8, Z), (8, 4, G)]) }
    fn fu() -> StrukturForm { form(8, 4, &[(0, 4, AF32), (4, 4, G)]) }
    fn b(n: u64) -> StrukturForm { form(n, 1, &(0..n).map(|i| (i, 1, G)).collect::<Vec<_>>()) }
    fn h3() -> StrukturForm { form(6, 2, &[(0, 2, G), (2, 2, G), (4, 2, G)]) }
    fn bb() -> StrukturForm { form(2, 1, &[(0, 1, G), (1, 1, G)]) }

    #[test]
    fn sysv_wie_clang() {
        let s = Abi::SysV;
        assert_eq!(arg(s, &w(1)), reg(&[Ganz(32)]));
        assert_eq!(arg(s, &w(2)), reg(&[Ganz(64)]));
        assert_eq!(arg(s, &w(3)), reg(&[Ganz(64), Ganz(32)]));
        assert_eq!(arg(s, &w(4)), reg(&[Ganz(64), Ganz(64)]));
        assert_eq!(arg(s, &w(6)), Uebergabe::Byval { ausrichtung: 8 });
        assert_eq!(ret(s, &w(6)), Uebergabe::Sret);
        assert_eq!(arg(s, &mix()), reg(&[F64, Ganz(32)]));
        assert_eq!(arg(s, &f4()), reg(&[F32x2, F32x2]));
        assert_eq!(arg(s, &f3()), reg(&[F32x2, F32]));
        assert_eq!(arg(s, &packed()), Uebergabe::Byval { ausrichtung: 8 });
        assert_eq!(ret(s, &packed()), Uebergabe::Sret);
        assert_eq!(arg(s, &d2()), reg(&[F64, F64]));
        assert_eq!(arg(s, &d4()), Uebergabe::Byval { ausrichtung: 8 });
        assert_eq!(arg(s, &f5()), Uebergabe::Byval { ausrichtung: 8 });
        assert_eq!(arg(s, &pu()), reg(&[Zeiger, Ganz(32)]));
        assert_eq!(arg(s, &fu()), reg(&[Ganz(64)]));
        assert_eq!(arg(s, &b(3)), reg(&[Ganz(24)]));
        assert_eq!(arg(s, &b(5)), reg(&[Ganz(40)]));
        assert_eq!(arg(s, &b(7)), reg(&[Ganz(56)]));
        assert_eq!(arg(s, &h3()), reg(&[Ganz(48)]));
        assert_eq!(arg(s, &bb()), reg(&[Ganz(16)]));
        assert_eq!(arg(s, &form(16, 8, &[(0, 8, G), (8, 8, G)])), reg(&[Ganz(64), Ganz(64)]));
        // Feld am Achtbyte-Anfang deckt alle Daten: {u8, pad, u64} -> {i8, i64}.
        assert_eq!(arg(s, &form(16, 8, &[(0, 1, G), (8, 8, G)])), reg(&[Ganz(8), Ganz(64)]));
    }

    #[test]
    fn ms_x64_wie_clang() {
        let m = Abi::Ms;
        assert_eq!(arg(m, &w(1)), reg(&[Ganz(32)]));
        assert_eq!(arg(m, &w(2)), reg(&[Ganz(64)]));
        assert_eq!(arg(m, &fu()), reg(&[Ganz(64)]));
        assert_eq!(arg(m, &bb()), reg(&[Ganz(16)]));
        assert_eq!(ret(m, &bb()), reg(&[Ganz(16)]));
        for f in [w(3), w(4), w(6), mix(), f4(), f3(), packed(), d2(), pu(), b(3), b(5), h3(), b(7), d4(), f5()] {
            assert_eq!(arg(m, &f), Uebergabe::Indirekt);
            assert_eq!(ret(m, &f), Uebergabe::Sret);
        }
    }

    #[test]
    fn aapcs64_wie_clang() {
        let a = Abi::Aapcs64;
        assert_eq!(arg(a, &w(1)), reg(&[Ganz(64)]));
        assert_eq!(ret(a, &w(1)), reg(&[Ganz(32)]));
        assert_eq!(ret(a, &w(2)), reg(&[Ganz(64)]));
        assert_eq!(arg(a, &w(3)), reg(&[I64x2]));
        assert_eq!(ret(a, &w(4)), reg(&[I64x2]));
        assert_eq!(arg(a, &w(6)), Uebergabe::Indirekt);
        assert_eq!(ret(a, &w(6)), Uebergabe::Sret);
        assert_eq!(arg(a, &mix()), reg(&[I64x2]));
        assert_eq!(arg(a, &f4()), reg(&[Hfa { doppelt: false, n: 4 }]));
        assert_eq!(arg(a, &f3()), reg(&[Hfa { doppelt: false, n: 3 }]));
        assert_eq!(ret(a, &d2()), reg(&[Hfa { doppelt: true, n: 2 }]));
        assert_eq!(arg(a, &d4()), reg(&[Hfa { doppelt: true, n: 4 }]));
        assert_eq!(arg(a, &f5()), Uebergabe::Indirekt);
        assert_eq!(arg(a, &packed()), reg(&[Ganz(64)]));
        assert_eq!(ret(a, &packed()), reg(&[Ganz(40)]));
        assert_eq!(ret(a, &b(3)), reg(&[Ganz(24)]));
        assert_eq!(ret(a, &bb()), reg(&[Ganz(16)]));
        assert_eq!(arg(a, &pu()), reg(&[I64x2]));
        // HFA n=1 (Clang: Argument [1 x float|double] alignstack(8), Rueckgabe {T}).
        let f1 = form(4, 4, &[(0, 4, AF32)]);
        let d1 = form(8, 8, &[(0, 8, AF64)]);
        assert_eq!(arg(a, &f1), reg(&[Hfa { doppelt: false, n: 1 }]));
        assert_eq!(ret(a, &f1), reg(&[Hfa { doppelt: false, n: 1 }]));
        assert_eq!(arg(a, &d1), reg(&[Hfa { doppelt: true, n: 1 }]));
        assert_eq!(ret(a, &d1), reg(&[Hfa { doppelt: true, n: 1 }]));
    }

    #[test]
    fn sysv_registerdruck() {
        let s = Abi::SysV;
        let w4 = w(4);
        let gp = |n: usize| vec![ParamForm::Skalar(Skalar::Gp); n];
        // 4 GP + W4 (2 GP) passt genau, 5 GP + W4 nicht mehr -> byval.
        let mut p = gp(4); p.push(ParamForm::Struktur(&w4));
        assert_eq!(plane(s, &p, None).unwrap().params[4], Some(reg(&[Ganz(64), Ganz(64)])));
        let mut p = gp(5); p.push(ParamForm::Struktur(&w4));
        assert_eq!(plane(s, &p, None).unwrap().params[5], Some(Uebergabe::Byval { ausrichtung: 8 }));
        // sret verbraucht ein GP-Register.
        let w6 = w(6);
        let mut p = gp(4); p.push(ParamForm::Struktur(&w4));
        let plan = plane(s, &p, Some(&w6)).unwrap();
        assert_eq!(plan.rueckgabe, Some(Uebergabe::Sret));
        assert_eq!(plan.params[4], Some(Uebergabe::Byval { ausrichtung: 8 }));
        // SSE-Druck: 7 Floats + D2 (2 SSE) -> byval; danach passt W1 noch.
        let d2 = d2();
        let w1 = w(1);
        let mut p = vec![ParamForm::Skalar(Skalar::Sse); 7];
        p.push(ParamForm::Struktur(&d2));
        p.push(ParamForm::Struktur(&w1));
        let plan = plane(s, &p, None).unwrap();
        assert_eq!(plan.params[7], Some(Uebergabe::Byval { ausrichtung: 8 }));
        assert_eq!(plan.params[8], Some(reg(&[Ganz(32)])));
        // Ohne freie GP-Register: <= 8 Byte als iN direkt (Clang druck0.c, Fall a/c).
        let k5 = packed();
        let f2 = form(8, 4, &[(0, 4, AF32), (4, 4, AF32)]);
        let h1 = form(2, 2, &[(0, 2, G)]);
        let p16 = form(16, 8, &[(0, 8, G), (8, 8, G)]);
        let w2 = w(2);
        let mut p = gp(6);
        for f in [&w2, &k5, &f2, &h1, &p16] { p.push(ParamForm::Struktur(f)); }
        let plan = plane(s, &p, None).unwrap();
        assert_eq!(plan.params[6], Some(reg(&[Ganz(64)])));
        assert_eq!(plan.params[7], Some(reg(&[Ganz(40)])));
        assert_eq!(plan.params[8], Some(reg(&[F32x2])));
        assert_eq!(plan.params[9], Some(reg(&[Ganz(16)])));
        assert_eq!(plan.params[10], Some(Uebergabe::Byval { ausrichtung: 8 }));
        // SSE erschoepft, GP frei: F2 byval, danach W2 normal im Register (Fall b).
        let mut p = vec![ParamForm::Skalar(Skalar::Sse); 8];
        p.push(ParamForm::Struktur(&f2));
        p.push(ParamForm::Struktur(&w2));
        let plan = plane(s, &p, None).unwrap();
        assert_eq!(plan.params[8], Some(Uebergabe::Byval { ausrichtung: 8 }));
        assert_eq!(plan.params[9], Some(reg(&[Ganz(64)])));
        // 5 GP: erstes W2 nimmt das letzte Register, zweites geht als i64 auf den Stack (Fall c).
        let mut p = gp(5);
        p.push(ParamForm::Struktur(&w2));
        p.push(ParamForm::Struktur(&w2));
        let plan = plane(s, &p, None).unwrap();
        assert_eq!(plan.params[5], Some(reg(&[Ganz(64)])));
        assert_eq!(plan.params[6], Some(reg(&[Ganz(64)])));
        // MS/AAPCS64 kennen keine Byval-Umstellung.
        let mut p = gp(8); p.push(ParamForm::Struktur(&w4));
        assert_eq!(plane(Abi::Ms, &p, None).unwrap().params[8], Some(Uebergabe::Indirekt));
    }

    #[test]
    fn abi_aus_triple() {
        assert_eq!(Abi::aus_triple("x86_64-unknown-linux-gnu", None), Ok(Abi::SysV));
        assert_eq!(Abi::aus_triple("x86_64-unknown-none", None), Ok(Abi::SysV));
        assert_eq!(Abi::aus_triple("x86_64-pc-windows-msvc", None), Ok(Abi::Ms));
        assert_eq!(Abi::aus_triple("x86_64-pc-windows-gnu", None), Ok(Abi::Ms));
        assert_eq!(Abi::aus_triple("x86_64-unknown-uefi", None), Ok(Abi::Ms));
        assert_eq!(Abi::aus_triple("x86_64-unknown-linux-gnu", Some("win64")), Ok(Abi::Ms));
        assert_eq!(Abi::aus_triple("aarch64-unknown-linux-gnu", None), Ok(Abi::Aapcs64));
        assert_eq!(Abi::aus_triple("aarch64-unknown-none", None), Ok(Abi::Aapcs64));
        assert!(Abi::aus_triple("i686-unknown-linux-gnu", None).is_err());
        assert!(Abi::aus_triple("wasm32-unknown-unknown", None).is_err());
    }

    #[test]
    fn grenzen() {
        assert!(klassifiziere(Abi::SysV, &form(32, 32, &[(0, 4, G)]), false).is_err());
        assert!(klassifiziere(Abi::SysV, &form(0, 1, &[]), false).is_err());
    }
}
