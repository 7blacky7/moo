//! P7-T5/P2-T56: Systemkoerper fuer `@nativ`-Funktionen in Bare/UEFI
//! sowie enger Hosted-Win64-Slice fuer echte native Strukturwerte.
//!
//! Ein Systemkoerper ist ein `unsicher:`-Block mit allgemeinen Anweisungen
//! (Bisher konnten `@nativ`-Funktionen nur genau einen Aufruf, einen
//! Zeigerzugriff oder eine Folge von Hardware-Intrinsics enthalten.)
//! Er entsteht im Bare-Profil, wenn kein engerer Bestandspfad (Hardware,
//! indirekter Ein-Aufruf ueber Parameter, Zeigerlesen) passt, und seit P2-T66
//! auch hosted auf x86_64 Linux/Windows und aarch64 Linux fuer unsicher:-Koerper mit Zeigern,
//! Strukturwerten oder mehreren Anweisungen. Im hosted Usermode sind Port-I/O,
//! Barrieren, assembler, volatile_* und registerfeld_* gesperrt (nur Bare/UEFI).
//!
//! Anweisungen:
//!   setze name: T auf wert      Deklaration (T: u8..u64, i8..i64, bool, ptr[...])
//!   setze name auf wert         Zuweisung an Parameter oder deklariertes Local
//!   wenn/sonst, solange, stopp, weiter, gib_zurueck, Aufruf als Anweisung
//! Locals sind funktionsweit, werden im Eintrittsblock angelegt und mit 0
//! (bzw. Nullzeiger/falsch) vorbelegt.
//!
//! Ausdruecke: Integerliterale (Typ aus dem Kontext), wahr/falsch, nichts
//! (Nullzeiger), + - * / % & | ^ << >> (Integer gleichen Typs; + - * wickeln,
//! / % und Shifts mit ungueltigem Operanden loesen einen Trap aus), Vergleiche
//! (-> bool), und/oder (Kurzschluss), nicht, Bit-Negation.
//! Aufrufe:
//!   u8(x) .. u64(x), i8(x) .. i64(x)  explizite Konversion (kuerzen/erweitern;
//!                                    Zeiger -> Adresse, bool -> 0/1)
//!   zeiger(adresse)                  u64 -> ptr[...] (Zieltyp aus dem Kontext)
//!   adresse_von(name)                Adresse (u64) eines Locals/Parameters,
//!                                    einer nativen Konstante oder Funktion
//!   speicher_lesen(adresse, breite)  breite 1/2/4/8 -> u8/u16/u32/u64
//!   speicher_schreiben(adresse, wert, breite)
//!   volatile_lesen(p[, index]) / volatile_schreiben(p[, index], wert) auf ptr[T]
//!   port_lese*/port_schreibe*, Barrieren, assembler(...) wie in Hardwarekoerpern,
//!     Ein-/Ausgaenge des Assemblers duerfen beliebige Integer-/Zeigerausdruecke sein
//!   andere @nativ-Funktionen (direkt), Funktionszeiger-Locals/Parameter
//!     vom Typ ptr[fn[...](...) -> T] (indirekt, mit deren Aufrufkonvention),
//!   extern "C"-Funktionen mit Ganzzahlen aller Breiten (8/16 Bit mit
//!     signext/zeroext an der Aufrufstelle), f32/f64 und Zeigern.
//! P2-T66 Stufe 2a: hosted Systemkoerper mit rein skalarer Signatur sind aus
//! dynamischem Moo aufrufbar (nur innerhalb `unsicher:`, in main oder einer
//! benannten Funktion; seit P2-T74 auch innerhalb `versuche:`, weil die Fehler
//! fangbar sind). Fehler im Koerper (Division/Shift/Index/Konversion): Bare/UEFI
//! und C-Exporte (@einstieg) llvm.trap; hosted Moo-interne Funktionen (P2-T34,
//! ADR §3) moo_native_fehler mit Nullrueckgabe, ohne aktiven Handler beendet
//! moo_throw den Prozess mit Diagnose.
//! Erreicht der Kontrollfluss das Ende einer Funktion mit Rueckgabetyp ohne
//! gib_zurück, gilt derselbe Fehlervertrag: hosted Moo-intern moo_native_fehler
//! ('endet ohne Rueckgabewert') mit Nullwert, sonst Trap. Fehlt gib_zurück ganz
//! (und keine Schleife), lehnt der Compiler den Koerper ab (P2-T74,
//! enthaelt_rueckgabe_oder_schleife). Speicherzugriffe ueber freie Zeiger tragen keine Ausrichtungs-
//! annahme (align 1); eigene Slots und Zwischenspeicher sind ABI-ausgerichtet,
//! und daraus mit Registry-Offsets bewiesene Feld-/Elementadressen ebenfalls.
//! Strukturwerte (P2-T61) leben im Speicher statt als LLVM-Aggregat: Kopien
//! per llvm.memcpy, Konstruktoren als Feldstores, interne Moo-Aufrufe geben
//! Zeiger auf exklusive Kopien weiter und schreiben Rueckgaben per sret.

use std::collections::{HashMap, HashSet};

use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::targets::TargetData;
use inkwell::attributes::{Attribute, AttributeLoc};
use inkwell::types::{AnyType, BasicMetadataTypeEnum, BasicType, BasicTypeEnum};
use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, FunctionValue, IntValue, PointerValue, VectorValue};
use inkwell::{AddressSpace, IntPredicate};

use crate::ast::{BinOp, Expr, Stmt, TypeKind, TypeRef, UnaryOpKind};
use crate::codegen_native::{NType, NValue, NativeParameter, NativePointerSignature};
use crate::native_struct::{NativeStructRegistry, VecMatArt};
use crate::native_vecmat as vm;
use crate::native_type_id::{resolve_type, NativeTypeId, NominalTypes, ScalarType};

/// Typ eines Wertes im Systemkoerper.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum STy {
    Int(NType),
    Float(NType),
    Bool,
    Ptr(NativeTypeId),
    /// Nativer Strukturwert, ohne MooValue-Boxing.
    Struct(NativeTypeId),
    /// P2-T50 (Plan bb3878ef E1/E2): puffer[T] als nicht besitzender Ausschnitt
    /// {Datenzeiger, Laenge}; T ist ein Zahlentyp (i8..u64, f32, f64).
    Buffer(NType),
    /// P2-T48 (Plan a71947f7 D3): puffer[Struktur], wie Buffer {Datenzeiger,
    /// Laenge}; Elementstride = Strukturgroesse, Identitaet nominal.
    StrukturPuffer(NativeTypeId),
    /// P2-T48 (Plan a71947f7 E1): festes Array [T; N] als Wert, im Speicher
    /// gefuehrt wie Strukturen (Kopie per memcpy, kein Aggregat in Registern).
    Array { element: Box<STy>, length: u32 },
}

impl STy {
    fn from_type_ref(ty: &TypeRef, nominal: &NominalTypes) -> Result<Self, String> {
        match &ty.kind {
            TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger" => {
                let identity = resolve_type(ty, nominal)?;
                Ok(STy::Ptr(identity))
            }
            TypeKind::Name(name) if nominal.contains_key(name) =>
                Ok(STy::Struct(resolve_type(ty, nominal)?)),
            TypeKind::Generic { name, .. } if name == "puffer" || name == "buffer" => match resolve_type(ty, nominal)? {
                NativeTypeId::Buffer { element } => Self::puffer_element(&element),
                _ => Err("Systemkoerper: puffer[T] ist nicht aufloesbar".into()),
            },
            TypeKind::Array { element, length } => {
                let element = Self::from_type_ref(element, nominal)?;
                if matches!(element, STy::Buffer(_) | STy::StrukturPuffer(_)) {
                    return Err("Systemkoerper: Arrays aus puffer[T] sind nicht freigegeben".into());
                }
                let length = crate::native_struct::array_laenge(length)?;
                Ok(STy::Array { element: Box::new(element), length })
            }
            _ => Self::from_ntype(NType::from_type_ref(ty)?),
        }
    }

    /// P2-T50 B1: Elementtyp eines Puffers; nur Zahlentypen (Struktur-Puffer
    /// folgen mit P2-T13/T48, bool ist kein Pufferelement).
    fn puffer_element(element: &NativeTypeId) -> Result<Self, String> {
        match Self::from_identity(element) {
            Ok(Some(STy::Int(t) | STy::Float(t))) => Ok(STy::Buffer(t)),
            Ok(Some(STy::Struct(id))) => Ok(STy::StrukturPuffer(id)),
            _ => Err("Systemkoerper: puffer[T] braucht einen Zahlen-Elementtyp (i8..u64, f32, f64) oder eine Struktur; bool und Zeiger sind als Element noch nicht freigegeben".into()),
        }
    }

    fn from_ntype(ty: NType) -> Result<Self, String> {
        match ty {
            NType::Bool => Ok(STy::Bool),
            NType::F32 | NType::F64 => Ok(STy::Float(ty)),
            other => Ok(STy::Int(other)),
        }
    }

    fn from_identity(identity: &NativeTypeId) -> Result<Option<Self>, String> {
        Ok(Some(match identity {
            NativeTypeId::Void => return Ok(None),
            NativeTypeId::Pointer { .. } => STy::Ptr(identity.clone()),
            NativeTypeId::Nominal { .. } => STy::Struct(identity.clone()),
            NativeTypeId::Buffer { element } => Self::puffer_element(element)?,
            NativeTypeId::Scalar(scalar) => match scalar {
                ScalarType::I8 => STy::Int(NType::I8), ScalarType::I16 => STy::Int(NType::I16),
                ScalarType::I32 => STy::Int(NType::I32), ScalarType::I64 => STy::Int(NType::I64),
                ScalarType::U8 => STy::Int(NType::U8), ScalarType::U16 => STy::Int(NType::U16),
                ScalarType::U32 => STy::Int(NType::U32), ScalarType::U64 => STy::Int(NType::U64),
                ScalarType::F32 => STy::Float(NType::F32), ScalarType::F64 => STy::Float(NType::F64),
                ScalarType::Bool => STy::Bool,
                _ => return Err("Systemkoerper: Typ nicht freigegeben".into()),
            },
            _ => return Err("Systemkoerper: Typ nicht freigegeben".into()),
        }))
    }

    /// P2-T48: Werte, die im Speicher kopiert werden (Struktur, festes Array).
    fn ist_aggregat(&self) -> bool {
        matches!(self, STy::Struct(_) | STy::Array { .. })
    }

    /// P2-T50: Identitaet des Elementtyps eines Puffers (fuer puffer_zeiger).
    fn identity_element(&self) -> NativeTypeId {
        match self {
            STy::Buffer(t) => match t {
                NType::F32 | NType::F64 => STy::Float(*t),
                _ => STy::Int(*t),
            }.identity(),
            STy::StrukturPuffer(id) => id.clone(),
            other => other.identity(),
        }
    }

    fn identity(&self) -> NativeTypeId {
        match self {
            STy::Ptr(identity) | STy::Struct(identity) => identity.clone(),
            STy::Buffer(t) => NativeTypeId::Buffer { element: Box::new(match t {
                NType::F32 | NType::F64 => STy::Float(*t),
                _ => STy::Int(*t),
            }.identity()) },
            STy::StrukturPuffer(id) => NativeTypeId::Buffer { element: Box::new(id.clone()) },
            // P2-T48: feste Arrays haben (noch) keine eigene Typidentitaet;
            // ptr[[T; N]] ist nicht freigegeben (Zeiger auf das Element nutzen).
            STy::Array { .. } => NativeTypeId::Void,
            STy::Bool => NativeTypeId::Scalar(ScalarType::Bool),
            STy::Float(NType::F32) => NativeTypeId::Scalar(ScalarType::F32),
            STy::Float(NType::F64) => NativeTypeId::Scalar(ScalarType::F64),
            STy::Float(_) => unreachable!("Float-STy darf nur f32/f64 tragen"),
            STy::Int(ty) => NativeTypeId::Scalar(match ty {
                NType::I8 => ScalarType::I8, NType::I16 => ScalarType::I16,
                NType::I32 => ScalarType::I32, NType::I64 => ScalarType::I64,
                NType::U8 => ScalarType::U8, NType::U16 => ScalarType::U16,
                NType::U32 => ScalarType::U32, _ => ScalarType::U64,
            }),
        }
    }

    fn llvm<'ctx>(
        &self, context: &'ctx Context, layouts: &NativeStructRegistry<'ctx>,
    ) -> Result<BasicTypeEnum<'ctx>, String> {
        match self {
            STy::Int(ty) | STy::Float(ty) => Ok(ty.llvm_type(context)),
            STy::Bool => Ok(context.bool_type().into()),
            STy::Ptr(_) => Ok(context.ptr_type(AddressSpace::default()).into()),
            STy::Buffer(_) | STy::StrukturPuffer(_) => Ok(context.struct_type(
                &[context.ptr_type(AddressSpace::default()).into(), context.i64_type().into()], false,
            ).into()),
            STy::Array { element, length } => Ok(element.llvm(context, layouts)?.array_type(*length).into()),
            STy::Struct(NativeTypeId::Nominal { name, .. }) => layouts.layout(name)
                .map(|layout| layout.llvm_type.into())
                .ok_or_else(|| format!("Systemkoerper: natives Struktur-Layout '{name}' fehlt")),
            STy::Struct(_) => Err("Systemkoerper: ungueltige Strukturidentitaet".into()),
        }
    }

    fn name(&self) -> String {
        match self {
            STy::Int(ty) | STy::Float(ty) => format!("{ty:?}").to_lowercase(),
            STy::Bool => "bool".into(),
            STy::Ptr(identity) => format!("{identity:?}"),
            STy::Buffer(t) => format!("puffer[{}]", format!("{t:?}").to_lowercase()),
            STy::StrukturPuffer(id) => format!("puffer[{}]", STy::Struct(id.clone()).name()),
            STy::Array { element, length } => format!("[{}; {length}]", element.name()),
            STy::Struct(NativeTypeId::Nominal { name, .. }) => name.clone(),
            STy::Struct(identity) => format!("{identity:?}"),
        }
    }
}

/// P2-T48 D: Art eines puffer-Parameters am dyn->nativ-Adapter.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum PufferArt {
    Skalar(NType),
    /// Strukturname; die Laufzeit-ID liefert NativeStructRegistry::struktur_id.
    Struktur(String),
    /// P2-T23/T53 B2 (Plan 36aca1f2 E1/E2): Griff fuer ptr[ziel] (None = ptr[void]);
    /// verbraucht = @verbraucht(n): der Aufruf gibt das fremde Objekt frei.
    Griff { ziel: Option<String>, verbraucht: bool },
    /// P2-T49 (Plan 38c59c64 B2): Strukturwert by value; dyn: Box -> exklusive
    /// Aufruferkopie (moo_struct_unbox_value), keine Leihe ueber den Aufruf.
    Wert(String),
}

/// P2-T49 B2: Ergebnis einer dyn aufrufbaren nativen Funktion jenseits von Skalaren.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum DynErgebnis {
    /// P2-T23/T53: Zeigerergebnis als Griff (None = ptr[void]).
    Griff(Option<String>),
    /// Strukturwert (sret-Puffer) als neue MOO_STRUCT-Box.
    Struktur(String),
}

/// P2-T23/T53 B2: Zeigerziel, das an der dyn-Grenze als Griff laufen darf:
/// ptr[void] -> Some(None), ptr[Struktur] -> Some(Some(name)), sonst None.
fn griff_ziel(ty: &STy) -> Option<Option<String>> {
    match ty {
        STy::Ptr(NativeTypeId::Pointer { address_space: 0, pointee }) => match pointee.as_ref() {
            NativeTypeId::Void => Some(None),
            NativeTypeId::Nominal { name, .. } => Some(Some(name.clone())),
            _ => None,
        },
        _ => None,
    }
}

#[derive(Clone, Debug)]
struct SVal<'ctx> {
    ty: STy,
    value: BasicValueEnum<'ctx>,
}

/// Typisierte Speicherpfade: Einzelwert, eingebettete Struktur oder
/// festes Array. Die Array-Groesse und der Elementstride kommen aus LLVM
/// TargetData, niemals aus der dynamischen MooValue-Darstellung.
#[derive(Clone, Debug)]
enum FieldKind {
    Value(STy),
    Embedded(String),
    Array { element: Box<FieldKind>, length: u64, stride: u64 },
}

/// Aufrufbares natives Ziel (Hardware-, Zeiger-, Rein- oder Systemfunktion).
#[derive(Clone, Debug)]
pub(crate) struct NativeCallee<'ctx> {
    pub(crate) function: FunctionValue<'ctx>,
    params: Vec<STy>,
    result: Option<STy>,
    /// P2-T58 C1: Callee kann hosted einen Moo-Fehler melden (fehlbare_funktionen);
    /// der Aufrufer prueft danach das Flag (call_system_value, C3).
    pub(crate) fehlbar: bool,
    /// P2-T21/T57 B2: @rückruf-Thunk (__moo_rueckruf.<name>) und die
    /// Funktionsidentitaet (Parameter, Ergebnis, Konventionsname), gegen die eine
    /// erwartete ptr[fn(...)]-Stelle streng verglichen wird (Batch C).
    pub(crate) rueckruf: Option<(FunctionValue<'ctx>, NativeTypeId)>,
}

impl<'ctx> NativeCallee<'ctx> {
    pub(crate) fn new(function: FunctionValue<'ctx>, signature: &NativePointerSignature) -> Result<Self, String> {
        let params = signature.params.iter().map(|param| match param {
            NativeParameter::Scalar(ty) => STy::from_ntype(*ty),
            NativeParameter::Pointer { identity, .. } => Ok(STy::Ptr(identity.clone())),
        }).collect::<Result<Vec<_>, _>>()?;
        let result = signature.result.map(STy::from_ntype).transpose()?;
        Ok(Self { function, params, result, fehlbar: false, rueckruf: None })
    }

    /// Fuer reine Moo-zu-Moo-Aufrufe mit kompletten Strukturwerten.
    pub(crate) fn from_system(function: FunctionValue<'ctx>, signature: &SystemSignature) -> Self {
        Self { function, params: signature.params.clone(), result: signature.result.clone(), fehlbar: false, rueckruf: None }
    }
}

/// P2-T58 Batch C1 (Plan 4500717b): syntaktisch fehlbare native Funktionen
/// (Fixpunkt ueber den Aufrufgraphen). Fehlbar: Div/Rest/Shift, gepruefter oder
/// saettigender Cast, Indexzugriff, Rueckgabetyp ohne abschliessendes
/// gib_zurück, Aufruf eines fehlbaren nativen Ziels; unbekannte Formen gelten
/// konservativ als fehlbar. Nicht fehlbar (keine Zusatzkosten): + - * Bit,
/// Float-Rechnung, Vergleiche, als_wrap, Aufrufe nicht-nativer Ziele.
///
/// P2-T58 (Review 06d10060 #4): typbewusst ueber die annotierten Typen von
/// Parametern, Locals und Callee-Ergebnissen (expr_art). Float-/ % ** und
/// unsigned ** melden nie einen Moo-Fehler; signed ** nur bei moeglicherweise
/// negativem Exponenten. Unbekannter Typ bleibt konservativ fehlbar.
/// P2-T21/T57 C3: `fremd` = Namen, deren Aufruf als fehlbar gilt (extern C in
/// Modulen mit fehlbaren Rueckrufen); dann auch Aufrufe ueber Funktionszeiger-
/// Locals/-Parameter. Leer = bisherige Analyse.
pub(crate) fn fehlbare_funktionen(defs: &[FehlbarDef<'_>], fremd: &HashSet<String>) -> HashSet<String> {
    let namen: HashSet<&str> = defs.iter().map(|d| d.name.as_str()).collect();
    let ergebnisse: HashMap<&str, Zahlart> = defs.iter()
        .map(|d| (d.name.as_str(), d.ergebnis.map(zahlart).unwrap_or(Zahlart::Sonst)))
        .collect();
    let typen: Vec<HashMap<&str, Zahlart>> = defs.iter().map(|d| {
        let mut typen = HashMap::new();
        for (name, ty) in &d.params {
            typen.insert(*name, ty.map(zahlart).unwrap_or(Zahlart::Sonst));
        }
        lokale_typen(d.body, &mut typen);
        typen
    }).collect();
    let mut fehlbar: HashSet<String> = HashSet::new();
    loop {
        let mut neu = false;
        for (index, def) in defs.iter().enumerate() {
            if fehlbar.contains(&def.name) { continue; }
            let ist_fehlbar = {
                let kontext = FehlbarKontext { namen: &namen, fehlbar: &fehlbar, typen: &typen[index], ergebnisse: &ergebnisse, fremd };
                // `-> void` hat kein Ergebnis, das am Koerperende fehlen koennte.
                let hat_ergebnis = def.ergebnis
                    .is_some_and(|ty| !matches!(&ty.kind, TypeKind::Name(n) if n == "void"));
                (hat_ergebnis && !endet_mit_rueckgabe(def.body)) || stmts_fehlbar(def.body, &kontext)
            };
            if ist_fehlbar {
                fehlbar.insert(def.name.clone());
                neu = true;
            }
        }
        if !neu { return fehlbar; }
    }
}

/// P2-T83 B3 (Plan f3217c27 E3): schreibt der Koerper per Elementzuweisung
/// `name[i] = ...` ueber den Pufferparameter `name` (auch in Bloecken)?
/// Weitergabe, Aliase und puffer_zeiger prueft liest_verletzung.
pub(crate) fn schreibt_puffer(stmts: &[Stmt], name: &str) -> bool {
    stmts.iter().any(|stmt| match stmt {
        Stmt::IndexAssignment { object: Expr::Identifier(ziel), .. } => ziel == name,
        Stmt::If { body, else_body, .. } => schreibt_puffer(body, name) || schreibt_puffer(else_body, name),
        Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } | Stmt::For { body, .. } => schreibt_puffer(body, name),
        _ => false,
    })
}

/// P2-T83 B3 (Review 6315dba0): je Funktion die puffer[T]-Parameterpositionen
/// und die @liest-Positionen (0-basiert), Grundlage fuer liest_verletzung.
pub(crate) type LiestKarte = std::collections::HashMap<String, (Vec<bool>, std::collections::HashSet<usize>)>;

pub(crate) fn liest_karte(statements: &[Stmt]) -> LiestKarte {
    statements.iter().filter_map(|stmt| {
        let Stmt::FunctionDef { name, param_types, decorators, .. } = stmt else { return None };
        let puffer = param_types.iter().map(|ty| ty.as_ref().is_some_and(|ty| matches!(
            &ty.kind, TypeKind::Generic { name, .. } if name == "puffer" || name == "buffer"))).collect();
        let liest = decorators.iter().filter(|d| matches!(d.name.as_str(), "liest" | "reads"))
            .filter_map(|d| match d.args.as_slice() {
                [crate::ast::DecoratorArg::Zahl(z)] if z.fract() == 0.0 && *z >= 1.0 => Some(*z as usize - 1),
                _ => None,
            }).collect();
        Some((name.clone(), (puffer, liest)))
    }).collect()
}

/// P2-T83 B3 (Plan f3217c27, Review 6315dba0): ein @liest-Parameter `name`
/// darf nur gelesen werden. Zulaessig sind ausschliesslich `name[i]` (lesend),
/// `länge(name)`, die Weitergabe an einen puffer-Parameter, der selbst @liest
/// ist, und `puffer_zeiger(name)` innerhalb von unsicher: (bewusstes Escape,
/// R3). Jede andere Verwendung (Weitergabe an schreibbare Pufferparameter,
/// Alias per setze, puffer_zeiger ausserhalb unsicher:) liefert den Grund.
/// Elementzuweisungen meldet bereits schreibt_puffer.
pub(crate) fn liest_verletzung(stmts: &[Stmt], name: &str, karte: &LiestKarte) -> Option<String> {
    LiestPruefung { name, karte }.block(stmts, false)
}

struct LiestPruefung<'a> {
    name: &'a str,
    karte: &'a LiestKarte,
}

impl LiestPruefung<'_> {
    fn block(&self, stmts: &[Stmt], unsicher: bool) -> Option<String> {
        stmts.iter().find_map(|stmt| self.stmt(stmt, unsicher))
    }

    fn stmt(&self, stmt: &Stmt, u: bool) -> Option<String> {
        match stmt {
            Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
            | Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _)
            | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) => self.expr(value, u),
            Stmt::PropertyAssignment { object, value, .. } =>
                self.expr(object, u).or_else(|| self.expr(value, u)),
            Stmt::IndexAssignment { object, index, value, .. } =>
                self.expr(object, u).or_else(|| self.expr(index, u)).or_else(|| self.expr(value, u)),
            Stmt::If { condition, body, else_body, .. } => self.expr(condition, u)
                .or_else(|| self.block(body, u)).or_else(|| self.block(else_body, u)),
            Stmt::While { condition, body, .. } => self.expr(condition, u).or_else(|| self.block(body, u)),
            Stmt::For { iterable, body, .. } => self.expr(iterable, u).or_else(|| self.block(body, u)),
            Stmt::UnsafeBlock { body, .. } => self.block(body, true),
            _ => None,
        }
    }

    fn expr(&self, expr: &Expr, u: bool) -> Option<String> {
        let name = self.name;
        match expr {
            Expr::Identifier(n) if n == name =>
                Some(format!("'{name}' wird als Wert weiterverwendet (Alias oder Weitergabe)")),
            Expr::IndexAccess { object, index } => match object.as_ref() {
                Expr::Identifier(n) if n == name => None,
                other => self.expr(other, u),
            }.or_else(|| self.expr(index, u)),
            Expr::FunctionCall { name: callee, args, .. } => args.iter().enumerate().find_map(|(i, arg)| match arg {
                Expr::Identifier(n) if n == name => self.argument(callee, i, u),
                other => self.expr(other, u),
            }),
            Expr::BinaryOp { left, right, .. } => self.expr(left, u).or_else(|| self.expr(right, u)),
            Expr::UnaryOp { operand, .. } => self.expr(operand, u),
            Expr::PropertyAccess { object, .. } => self.expr(object, u),
            Expr::MethodCall { object, args, .. } =>
                self.expr(object, u).or_else(|| args.iter().find_map(|a| self.expr(a, u))),
            Expr::List(items) => items.iter().find_map(|a| self.expr(a, u)),
            Expr::Ternary { condition, then_val, else_val } => self.expr(condition, u)
                .or_else(|| self.expr(then_val, u)).or_else(|| self.expr(else_val, u)),
            _ => None,
        }
    }

    /// `name` steht direkt als Argument `i` von `callee`. Eigene Funktionen
    /// haben wie bei puffer_builtin_typ Vorrang vor länge/puffer_zeiger.
    fn argument(&self, callee: &str, i: usize, unsicher: bool) -> Option<String> {
        let name = self.name;
        if let Some((puffer, liest)) = self.karte.get(callee) {
            if puffer.get(i) == Some(&true) && liest.contains(&i) { return None; }
            return Some(format!("'{name}' wird an Parameter {} von '{callee}' weitergegeben, der nicht @liest({}) ist (schreibbarer Puffer)", i + 1, i + 1));
        }
        match callee {
            "länge" | "laenge" | "length" | "len" => None,
            "puffer_zeiger" | "buffer_pointer" if unsicher => None,
            "puffer_zeiger" | "buffer_pointer" =>
                Some(format!("puffer_zeiger({name}) steht ausserhalb von unsicher: (dort nur als bewusstes Escape)")),
            _ => Some(format!("'{name}' wird an '{callee}' weitergegeben")),
        }
    }
}

/// P2-T98 S3 (M-M005): erster Rohzugriff ausserhalb eines unsicher:-Blocks
/// eines @nativ-Koerpers als (Builtin, Zeile). Erfasst nur Builtins, die in
/// keinem nativen Pfad ohne unsicher: erlaubt sind (zeiger, adresse_von,
/// speicher_*); puffer_zeiger/länge bleiben erlaubt, volatile_* und andere
/// Hardware-Intrinsics meldet classify_body (MissingUnsafe). Spaetere Pfade
/// (reiner Kern, Systemkoerper) melden sonst nur M-C000/M-N000.
pub(crate) fn rohzugriff_ohne_unsicher(body: &[Stmt]) -> Option<(String, usize)> {
    // Alle Aliase DE/EN (Review 8d6be1e5); volatile_*, port_*, Barrieren und
    // assembler meldet classify_body, puffer_zeiger/länge sind erlaubt.
    fn roh(name: &str) -> bool {
        matches!(name, "zeiger" | "pointer" | "adresse_von" | "address_of"
            | "speicher_lesen" | "mem_read" | "speicher_schreiben" | "mem_write"
            | "strukturfeld_lesen" | "struct_field_read" | "strukturfeld_schreiben" | "struct_field_write"
            | "registerfeld_lesen" | "register_field_read" | "registerfeld_schreiben" | "register_field_write")
    }
    fn ausdruck(expr: &Expr) -> Option<String> {
        match expr {
            Expr::FunctionCall { name, args, .. } => if roh(name) { Some(name.clone()) } else { args.iter().find_map(ausdruck) },
            Expr::BinaryOp { left, right, .. } => ausdruck(left).or_else(|| ausdruck(right)),
            Expr::UnaryOp { operand, .. } => ausdruck(operand),
            Expr::PropertyAccess { object, .. } => ausdruck(object),
            Expr::IndexAccess { object, index } => ausdruck(object).or_else(|| ausdruck(index)),
            Expr::MethodCall { object, args, .. } => ausdruck(object).or_else(|| args.iter().find_map(ausdruck)),
            Expr::List(items) => items.iter().find_map(ausdruck),
            Expr::Ternary { condition, then_val, else_val } =>
                ausdruck(condition).or_else(|| ausdruck(then_val)).or_else(|| ausdruck(else_val)),
            _ => None,
        }
    }
    body.iter().find_map(|stmt| {
        let treffer = match stmt {
            Stmt::UnsafeBlock { .. } => return None,
            Stmt::If { condition, body, else_body, .. } => return ausdruck(condition).map(|n| (n, stmt.zeile()))
                .or_else(|| rohzugriff_ohne_unsicher(body)).or_else(|| rohzugriff_ohne_unsicher(else_body)),
            Stmt::While { condition, body, .. } => return ausdruck(condition).map(|n| (n, stmt.zeile()))
                .or_else(|| rohzugriff_ohne_unsicher(body)),
            Stmt::For { iterable, body, .. } => return ausdruck(iterable).map(|n| (n, stmt.zeile()))
                .or_else(|| rohzugriff_ohne_unsicher(body)),
            Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
            | Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _)
            | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) => ausdruck(value),
            Stmt::PropertyAssignment { object, value, .. } => ausdruck(object).or_else(|| ausdruck(value)),
            Stmt::IndexAssignment { object, index, value, .. } =>
                ausdruck(object).or_else(|| ausdruck(index)).or_else(|| ausdruck(value)),
            _ => None,
        };
        treffer.map(|n| (n, stmt.zeile()))
    })
}

/// P2-T98 S4 (M-M003): Nutzung nach Freigabe in eindeutigen Faellen. Ein
/// Aufruf `f(.., p, ..)` als eigene Anweisung (auch `setze r auf f(..)`), dessen
/// Parameter n laut @verbraucht(n) freigegeben wird, beendet den Besitz an der
/// Variablen p. Jede spaetere Nutzung von p im selben Block ist ein Fehler, eine
/// zweite Freigabe ebenso; unsicher: zaehlt als derselbe Block, innere Bloecke
/// werden mit durchsucht. Nutzung heisst: p direkt als Aufrufargument
/// (ea0e9eae). Konservativ (bleibt Laufzeit): Neuzuweisung von p
/// oder eine unbekannte Anweisungsart beendet die Suche, ebenso gib_zurück/
/// stopp/weiter/wirf; eine Freigabe nur in einem Zweig gilt nach dem Zweig
/// nicht; Schleifen ueber Iterationen, Aliasse und Felder werden nicht
/// verfolgt. adresse_von(p) (Adresse der Variablen) ist keine Nutzung.
pub(crate) fn freigabe_verletzung(body: &[Stmt], frei: &dyn Fn(&str, usize) -> bool) -> Option<(String, usize)> {
    // Koordinator nach 196dbf18: wird irgendwo die Adresse von p genommen
    // (adresse_von(p)), kann p ueber den Zeiger neu belegt werden; M-M003 bleibt
    // fuer p still (wie M-M004 ueber FluchtPruefung::adressiert).
    let mut adressiert = HashSet::new();
    FluchtPruefung::adressen(body, &mut adressiert);
    freigabe_in(body, frei, &adressiert)
}

fn freigabe_in(body: &[Stmt], frei: &dyn Fn(&str, usize) -> bool, adressiert: &HashSet<String>) -> Option<(String, usize)> {
    let mut linear = Vec::new();
    freigabe_flach(body, &mut linear);
    for (k, stmt) in linear.iter().enumerate() {
        let innen = match stmt {
            Stmt::If { body, else_body, .. } =>
                freigabe_in(body, frei, adressiert).or_else(|| freigabe_in(else_body, frei, adressiert)),
            Stmt::While { body, .. } | Stmt::For { body, .. } => freigabe_in(body, frei, adressiert),
            _ => None,
        };
        if innen.is_some() { return innen; }
        for (f, p) in freigaben(stmt, frei) {
            if adressiert.contains(p) { continue; }
            for spaeter in &linear[k + 1..] {
                match freigabe_nutzung(spaeter, p, frei) {
                    FreiNutzung::Keine => {}
                    FreiNutzung::Ende => break,
                    FreiNutzung::Lesen(zeile, true) => return Some((format!(
                        "'{p}' wird ein zweites Mal freigegeben (schon durch '{f}', @verbraucht); doppelte Freigabe. Vorschlag: nur einmal freigeben"), zeile)),
                    FreiNutzung::Lesen(zeile, false) => return Some((format!(
                        "'{p}' wird nach der Freigabe durch '{f}' (@verbraucht) weiter genutzt; der Speicher gehoert nicht mehr dem Aufrufer. Vorschlag: '{p}' vor dem Aufruf von '{f}' fertig nutzen oder neu zuweisen"), zeile)),
                }
                if matches!(spaeter, Stmt::Return(..) | Stmt::Break(_) | Stmt::Continue(_) | Stmt::Throw(..)) { break; }
            }
        }
    }
    None
}

/// unsicher:-Bloecke sind kein eigener Kontrollfluss: flach in die Folge.
fn freigabe_flach<'s>(body: &'s [Stmt], aus: &mut Vec<&'s Stmt>) {
    for stmt in body {
        match stmt {
            Stmt::UnsafeBlock { body, .. } => freigabe_flach(body, aus),
            _ => aus.push(stmt),
        }
    }
}

/// (Funktion, Variable) je bloss uebergebener Variable, die der Aufruf laut
/// @verbraucht freigibt; nur der Aufruf als ganze Anweisung bzw. Zuweisungswert.
fn freigaben<'s>(stmt: &'s Stmt, frei: &dyn Fn(&str, usize) -> bool) -> Vec<(&'s str, &'s str)> {
    let (ziel, wert) = match stmt {
        Stmt::Expression(wert, _) => (None, wert),
        Stmt::Assignment { name, value, .. } | Stmt::ConstAssignment { name, value, .. } => (Some(name.as_str()), value),
        _ => return Vec::new(),
    };
    let Expr::FunctionCall { name, args, .. } = wert else { return Vec::new() };
    args.iter().enumerate().filter_map(|(i, a)| match a {
        Expr::Identifier(p) if frei(name.as_str(), i) && ziel != Some(p.as_str()) => Some((name.as_str(), p.as_str())),
        _ => None,
    }).collect()
}

enum FreiNutzung { Keine, Ende, Lesen(usize, bool) }

/// Koordinator nach ea0e9eae: als Nutzung zaehlt nur die ganze Variable p als
/// direktes Argument eines Aufrufs (auch eine zweite Freigabe). Felder,
/// Elemente, Aliase, Rechnungen, Vergleiche und Umwandlungen bleiben still.
fn freigabe_liest(expr: &Expr, p: &str) -> bool {
    let l = |e: &Expr| freigabe_liest(e, p);
    let direkt = |args: &[Expr]| args.iter().any(|a| matches!(a, Expr::Identifier(n) if n == p));
    match expr {
        Expr::FunctionCall { name, args, .. } => {
            let rein = matches!(name.as_str(), "adresse_von" | "address_of" | "zeiger" | "pointer"
                | "als" | "as" | "als_wrap" | "als_saettigend") || conversion(name).is_some();
            (!rein && direkt(args)) || args.iter().any(l)
        }
        Expr::MethodCall { object, args, .. } => direkt(args) || l(object) || args.iter().any(l),
        Expr::List(args) => args.iter().any(l),
        Expr::BinaryOp { left, right, .. } => l(left) || l(right),
        Expr::UnaryOp { operand, .. } => l(operand),
        Expr::PropertyAccess { object, .. } => l(object),
        Expr::IndexAccess { object, index } => l(object) || l(index),
        Expr::Ternary { condition, then_val, else_val } => l(condition) || l(then_val) || l(else_val),
        _ => false,
    }
}

/// Erstes Ereignis fuer p in `stmt`: Nutzung (Zeile, zweite Freigabe?),
/// Neuzuweisung bzw. Unklares (Ende) oder nichts.
fn freigabe_nutzung(stmt: &Stmt, p: &str, frei: &dyn Fn(&str, usize) -> bool) -> FreiNutzung {
    let lesen = |e: &Expr| freigabe_liest(e, p);
    let treffer = || FreiNutzung::Lesen(stmt.zeile(), freigaben(stmt, frei).iter().any(|(_, q)| *q == p));
    let folge = |stmts: &[Stmt]| {
        for s in stmts {
            match freigabe_nutzung(s, p, frei) {
                FreiNutzung::Keine => {}
                anderes => return anderes,
            }
            if matches!(s, Stmt::Return(..) | Stmt::Break(_) | Stmt::Continue(_) | Stmt::Throw(..)) { break; }
        }
        FreiNutzung::Keine
    };
    match stmt {
        Stmt::Assignment { name, value, .. } | Stmt::ConstAssignment { name, value, .. } => {
            if lesen(value) { treffer() } else if name == p { FreiNutzung::Ende } else { FreiNutzung::Keine }
        }
        Stmt::CompoundAssignment { name, value, .. } =>
            if lesen(value) { treffer() } else if name == p { FreiNutzung::Ende } else { FreiNutzung::Keine },
        Stmt::Show(value, _) | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) | Stmt::Throw(value, _) =>
            if lesen(value) { treffer() } else { FreiNutzung::Keine },
        Stmt::Return(None, _) | Stmt::Break(_) | Stmt::Continue(_) => FreiNutzung::Keine,
        Stmt::PropertyAssignment { object, value, .. } =>
            if lesen(object) || lesen(value) { treffer() } else { FreiNutzung::Keine },
        Stmt::IndexAssignment { object, index, value, .. } =>
            if lesen(object) || lesen(index) || lesen(value) { treffer() } else { FreiNutzung::Keine },
        Stmt::If { condition, body, else_body, .. } => {
            if lesen(condition) { return FreiNutzung::Lesen(stmt.zeile(), false); }
            match folge(body) { FreiNutzung::Keine => folge(else_body), anderes => anderes }
        }
        Stmt::While { condition, body, .. } => {
            if lesen(condition) { return FreiNutzung::Lesen(stmt.zeile(), false); }
            folge(body)
        }
        Stmt::For { var_name, iterable, body, .. } => {
            if lesen(iterable) { return FreiNutzung::Lesen(stmt.zeile(), false); }
            if var_name == p { return FreiNutzung::Ende; }
            folge(body)
        }
        Stmt::UnsafeBlock { body, .. } => folge(body),
        _ => FreiNutzung::Ende,
    }
}

/// P2-T51 E5 (Plan f5b52a0b C1, O2: streng, ohne Ausnahmeschalter): ein
/// GELIEHENER Zeiger zeigt in Speicher, der nur waehrend des Aufrufs lebt:
/// `adresse_von(x)` fuer Locals/Wertparameter (auch deren Felder/Elemente und
/// Pufferelemente) und `puffer_zeiger(p)` (Pin endet mit dem Aufruf). Die
/// Markierung wandert ueber lokale Zuweisungen, zeiger()/Konversionen,
/// Rechnungen, Strukturkonstruktoren und lokale Struktur-/Arrayfelder
/// (Fixpunkt, flussunabhaengig = bewusst konservativ). Compilefehler, wenn ein
/// markierter Wert (a) zurueckgegeben, (b) in Speicher geschrieben wird, der
/// nicht selbst lokal oder geliehen ist (p[i] = .., p.f = .., speicher_/
/// volatile_schreiben), (c) in einem zurueckgegebenen Strukturwert steckt.
/// Erlaubt: lokale Nutzung und Weitergabe als Argument (Vertrag: der
/// Aufgerufene, auch C, behaelt den Zeiger nach der Rueckkehr nicht).
pub(crate) fn flucht_verletzung(
    params: &[(String, STy)], body: &[Stmt], ist_struktur: &dyn Fn(&str) -> bool,
    behaelt: &dyn Fn(&str, usize) -> bool,
) -> Option<(&'static str, String, usize)> {
    let mut pruefung = FluchtPruefung {
        lokal: params.iter().map(|(n, _)| n.clone()).collect(),
        zeiger: params.iter().filter(|(_, t)| matches!(t, STy::Ptr(_))).map(|(n, _)| n.clone()).collect(),
        puffer: params.iter().filter(|(_, t)| matches!(t, STy::Buffer(_) | STy::StrukturPuffer(_)))
            .map(|(n, _)| n.clone()).collect(),
        geliehen: HashMap::new(),
        ist_struktur,
        behaelt,
        adressiert: HashSet::new(),
    };
    pruefung.lokale(body);
    FluchtPruefung::adressen(body, &mut pruefung.adressiert);
    loop {
        let vorher = pruefung.geliehen.len();
        pruefung.markiere(body);
        if pruefung.geliehen.len() == vorher { break; }
    }
    let flucht = pruefung.pruefe(body);
    let behalten = pruefung.behalten_fluss(body, &mut HashMap::new()).map(|(g, z)| ("M-M004", g, z));
    match (flucht, behalten) {
        (Some(a), Some(b)) => Some(if b.2 < a.2 { b } else { a }),
        (a, b) => a.or(b),
    }
}

struct FluchtPruefung<'a> {
    lokal: HashSet<String>,
    zeiger: HashSet<String>,
    puffer: HashSet<String>,
    geliehen: HashMap<String, String>,
    ist_struktur: &'a dyn Fn(&str) -> bool,
    /// P2-T98 S5: extern-Parameter mit @behaelt(n) (Name, 0-basiert).
    behaelt: &'a dyn Fn(&str, usize) -> bool,
    /// P2-T98 S5-Fix: Namen, deren Adresse genommen wird; sie koennen ueber den
    /// Zeiger ueberschrieben werden, M-M004 bleibt fuer sie still.
    adressiert: HashSet<String>,
}

impl FluchtPruefung<'_> {
    fn typ_art(ty: &TypeRef) -> Option<bool> {
        match &ty.kind {
            TypeKind::Generic { name, .. } if matches!(name.as_str(), "ptr" | "zeiger" | "pointer") => Some(true),
            TypeKind::Generic { name, .. } if matches!(name.as_str(), "puffer" | "buffer") => Some(false),
            _ => None,
        }
    }

    fn lokale(&mut self, stmts: &[Stmt]) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { name, ty, .. } | Stmt::ConstAssignment { name, ty, .. } => {
                    self.lokal.insert(name.clone());
                    match ty.as_ref().and_then(Self::typ_art) {
                        Some(true) => { self.zeiger.insert(name.clone()); }
                        Some(false) => { self.puffer.insert(name.clone()); }
                        None => {}
                    }
                }
                Stmt::If { body, else_body, .. } => { self.lokale(body); self.lokale(else_body); }
                Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } | Stmt::For { body, .. } => self.lokale(body),
                _ => {}
            }
        }
    }

    fn wurzel(expr: &Expr) -> Option<&str> {
        match expr {
            Expr::Identifier(n) => Some(n.as_str()),
            Expr::PropertyAccess { object, .. } | Expr::IndexAccess { object, .. } => Self::wurzel(object),
            _ => None,
        }
    }

    /// Speicher des eigenen Rahmens (Local/Wertparameter, kein Zeiger/Puffer).
    fn eigener_speicher(&self, name: &str) -> bool {
        self.lokal.contains(name) && !self.zeiger.contains(name) && !self.puffer.contains(name)
    }

    /// Zeigerausdruck nach AST: ptr-typisierter Name, zeiger(...)/puffer_zeiger(...),
    /// p + n / n + p / p - n (aber nicht p - q, das ist ein Abstand).
    fn ist_zeiger(&self, expr: &Expr) -> bool {
        match expr {
            Expr::Identifier(n) => self.zeiger.contains(n),
            Expr::FunctionCall { name, .. } =>
                matches!(name.as_str(), "zeiger" | "pointer" | "puffer_zeiger" | "buffer_pointer"),
            Expr::BinaryOp { op: BinOp::Add, left, right } => self.ist_zeiger(left) || self.ist_zeiger(right),
            Expr::BinaryOp { op: BinOp::Sub, left, right } => self.ist_zeiger(left) && !self.ist_zeiger(right),
            _ => false,
        }
    }

    /// Quelle, falls `expr` einen geliehenen Zeiger (oder Wert mit einem) liefert.
    fn quelle(&self, expr: &Expr) -> Option<String> {
        match expr {
            Expr::Identifier(n) => self.geliehen.get(n).cloned(),
            Expr::FunctionCall { name, args, .. } => match name.as_str() {
                "adresse_von" | "address_of" => {
                    let ziel = args.first()?;
                    let wurzel = Self::wurzel(ziel)?;
                    let direkt = matches!(ziel, Expr::Identifier(_));
                    let im_rahmen = self.lokal.contains(wurzel)
                        && (direkt || self.puffer.contains(wurzel) || !self.zeiger.contains(wurzel));
                    im_rahmen.then(|| format!("adresse_von({wurzel}) zeigt in den Rahmen der Funktion"))
                        .or_else(|| self.geliehen.get(wurzel).filter(|_| !direkt && !self.zeiger.contains(wurzel)).cloned())
                }
                "puffer_zeiger" | "buffer_pointer" => {
                    let p = args.first().and_then(Self::wurzel).unwrap_or("?");
                    Some(format!("puffer_zeiger({p}) ist nur waehrend des Aufrufs gepinnt"))
                }
                "zeiger" | "pointer" | "als" | "as" | "als_wrap" | "als_saettigend" =>
                    args.iter().find_map(|a| self.quelle(a)),
                other if conversion(other).is_some() || (self.ist_struktur)(other) =>
                    args.iter().find_map(|a| self.quelle(a)),
                _ => None,
            },
            Expr::BinaryOp { op, left, right } => {
                // Vergleiche/Logik liefern bool; *, /, % und Shifts liefern keine
                // Adresse (z. B. Ausrichtungsrest adresse_von(x) % 32).
                if matches!(op, BinOp::Eq | BinOp::NotEq | BinOp::Less | BinOp::LessEq
                    | BinOp::Greater | BinOp::GreaterEq | BinOp::And | BinOp::Or
                    | BinOp::Mul | BinOp::Div | BinOp::Mod | BinOp::LShift | BinOp::RShift) { return None; }
                // Zeigerdifferenz p - q (beide Operanden Zeigerausdruecke, auch
                // gemischt geliehen/normal) liefert einen i64-Abstand (Review 65d12ed4).
                if matches!(op, BinOp::Sub) && self.ist_zeiger(left) && self.ist_zeiger(right) { return None; }
                let (l, r) = (self.quelle(left), self.quelle(right));
                // Differenz zweier geliehener Adressen (z. B. u64-Werte) ist ein Abstand.
                if matches!(op, BinOp::Sub) && l.is_some() && r.is_some() { return None; }
                l.or(r)
            }
            Expr::UnaryOp { op: UnaryOpKind::Not, .. } => None,
            Expr::UnaryOp { operand, .. } => self.quelle(operand),
            Expr::Ternary { then_val, else_val, .. } => self.quelle(then_val).or_else(|| self.quelle(else_val)),
            // Feld/Element eines markierten lokalen Struktur-/Arraywerts; ueber
            // einen Zeiger gelesene Werte sind Daten des Ziels, nicht der Zeiger.
            Expr::PropertyAccess { object, .. } | Expr::IndexAccess { object, .. } => {
                let wurzel = Self::wurzel(object)?;
                if self.zeiger.contains(wurzel) || self.puffer.contains(wurzel) { return None; }
                self.geliehen.get(wurzel).cloned()
            }
            _ => None,
        }
    }

    /// P2-T98 S5 (Koordinator nach ea0e9eae): die Quelle selbst, nur fuer eine
    /// ganze Variable: `adresse_von(lokal)` bzw. `puffer_zeiger(b)`.
    fn quelle_direkt(&self, expr: &Expr) -> Option<String> {
        let Expr::FunctionCall { name, args, .. } = expr else { return None };
        let [Expr::Identifier(n)] = args.as_slice() else { return None };
        match name.as_str() {
            "adresse_von" | "address_of" if self.lokal.contains(n) =>
                Some(format!("adresse_von({n}) zeigt in den Rahmen der Funktion")),
            "puffer_zeiger" | "buffer_pointer" => Some(format!("puffer_zeiger({n}) ist nur waehrend des Aufrufs gepinnt")),
            _ => None,
        }
    }

    /// M-M004 nur sicher: eine ganze lokale Variable mit sicherem Leihstand oder
    /// die Quelle selbst als Argument. Felder, Elemente, Aliase, Rechnungen = still.
    fn geliehen_direkt(&self, arg: &Expr, stand: &HashMap<String, String>) -> Option<String> {
        match arg {
            Expr::Identifier(n) => stand.get(n).cloned(),
            other => self.quelle_direkt(other),
        }
    }

    fn markiere(&mut self, stmts: &[Stmt]) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { name, value, .. } | Stmt::ConstAssignment { name, value, .. } => {
                    if let Some(q) = self.quelle(value) { self.geliehen.entry(name.clone()).or_insert(q); }
                }
                Stmt::PropertyAssignment { object, value, .. } | Stmt::IndexAssignment { object, value, .. } => {
                    if let (Some(q), Some(wurzel)) = (self.quelle(value), Self::wurzel(object)) {
                        if self.eigener_speicher(wurzel) {
                            self.geliehen.entry(wurzel.to_string()).or_insert(q);
                        }
                    }
                }
                Stmt::If { body, else_body, .. } => { self.markiere(body); self.markiere(else_body); }
                Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } | Stmt::For { body, .. } => self.markiere(body),
                _ => {}
            }
        }
    }

    /// P2-T98 S2: Code, Grund und Zeile der ersten Flucht (M-M002). Der
    /// flussunabhaengige Fixpunkt ist hier gewollt (E5 O2: streng).
    fn pruefe(&self, stmts: &[Stmt]) -> Option<(&'static str, String, usize)> {
        stmts.iter().find_map(|stmt| match stmt {
            Stmt::If { body, else_body, .. } => self.pruefe(body).or_else(|| self.pruefe(else_body)),
            Stmt::While { body, .. } | Stmt::For { body, .. } | Stmt::UnsafeBlock { body, .. } => self.pruefe(body),
            _ => self.pruefe_anweisung(stmt).map(|grund| ("M-M002", grund, stmt.zeile())),
        })
    }

    /// P2-T98 S5 (M-M004), flussgenau (Review 0856b43c): `stand` enthaelt nur
    /// Namen, die an DIESER Stelle sicher einen geliehenen Zeiger halten. Eine
    /// Neuzuweisung mit nicht geliehenem Wert hebt die Markierung auf; nach
    /// wenn/sonst gilt die Schnittmenge beider Zweige; in Schleifen gelten im
    /// Koerper zugewiesene Namen als unsicher; gib_zurück/stopp/weiter beenden
    /// den Block. Unsicher heisst still (Laufzeit), nie Fehlalarm. Markiert wird
    /// nur `setze p auf adresse_von(lokal)`/`puffer_zeiger(b)` (Koordinator nach
    /// ea0e9eae); Felder, Elemente, Aliase und Rechnungen bleiben still.
    fn behalten_fluss(&self, stmts: &[Stmt], stand: &mut HashMap<String, String>) -> Option<(String, usize)> {
        for stmt in stmts {
            let zeile = stmt.zeile();
            match stmt {
                Stmt::If { condition, body, else_body, .. } => {
                    if let Some(g) = self.behalten(condition, stand) { return Some((g, zeile)); }
                    let (mut dann, mut sonst) = (stand.clone(), stand.clone());
                    if let Some(t) = self.behalten_fluss(body, &mut dann) { return Some(t); }
                    if let Some(t) = self.behalten_fluss(else_body, &mut sonst) { return Some(t); }
                    *stand = dann.into_iter().filter(|(n, _)| sonst.contains_key(n)).collect();
                }
                Stmt::While { condition, body, .. } => {
                    Self::schleife_unsicher(body, stand);
                    if let Some(g) = self.behalten(condition, stand) { return Some((g, zeile)); }
                    if let Some(t) = self.behalten_fluss(body, &mut stand.clone()) { return Some(t); }
                }
                Stmt::For { var_name, iterable, body, .. } => {
                    if let Some(g) = self.behalten(iterable, stand) { return Some((g, zeile)); }
                    stand.remove(var_name);
                    Self::schleife_unsicher(body, stand);
                    if let Some(t) = self.behalten_fluss(body, &mut stand.clone()) { return Some(t); }
                }
                Stmt::UnsafeBlock { body, .. } => {
                    if let Some(t) = self.behalten_fluss(body, stand) { return Some(t); }
                }
                Stmt::Assignment { name, value, .. } | Stmt::ConstAssignment { name, value, .. } => {
                    if let Some(g) = self.behalten(value, stand) { return Some((g, zeile)); }
                    match self.quelle_direkt(value) {
                        Some(q) if !self.adressiert.contains(name) => { stand.insert(name.clone(), q); }
                        _ => { stand.remove(name); }
                    }
                }
                Stmt::CompoundAssignment { name, value, .. } => {
                    if let Some(g) = self.behalten(value, stand) { return Some((g, zeile)); }
                    stand.remove(name);
                }
                Stmt::Return(..) | Stmt::Break(_) | Stmt::Continue(_) =>
                    return self.behalten_anweisung(stmt, stand).map(|g| (g, zeile)),
                Stmt::TryCatch { .. } | Stmt::Match { .. } | Stmt::Guard { .. } | Stmt::ParallelFor { .. } => stand.clear(),
                _ => {
                    if let Some(g) = self.behalten_anweisung(stmt, stand) { return Some((g, zeile)); }
                }
            }
        }
        None
    }

    /// Schleifenkopf: im Koerper (auch verschachtelt) zugewiesene Namen koennen
    /// aus einer frueheren Iteration einen anderen Wert tragen.
    fn schleife_unsicher(body: &[Stmt], stand: &mut HashMap<String, String>) {
        let mut namen = HashSet::new();
        if Self::zugewiesen(body, &mut namen) { stand.retain(|n, _| !namen.contains(n)); } else { stand.clear(); }
    }

    /// Sammelt zugewiesene Namen; false bei einer nicht verfolgten Blockform.
    fn zugewiesen(stmts: &[Stmt], namen: &mut HashSet<String>) -> bool {
        stmts.iter().all(|stmt| match stmt {
            Stmt::Assignment { name, .. } | Stmt::ConstAssignment { name, .. }
            | Stmt::CompoundAssignment { name, .. } => { namen.insert(name.clone()); true }
            Stmt::PropertyAssignment { object, .. } | Stmt::IndexAssignment { object, .. } => {
                if let Some(w) = Self::wurzel(object) { namen.insert(w.to_string()); }
                true
            }
            Stmt::If { body, else_body, .. } => Self::zugewiesen(body, namen) && Self::zugewiesen(else_body, namen),
            Stmt::For { var_name, body, .. } => { namen.insert(var_name.clone()); Self::zugewiesen(body, namen) }
            Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } => Self::zugewiesen(body, namen),
            Stmt::TryCatch { .. } | Stmt::Match { .. } | Stmt::Guard { .. } | Stmt::ParallelFor { .. } => false,
            _ => true,
        })
    }

    /// Namen, deren Adresse irgendwo genommen wird (adresse_von(n...)).
    fn adressen(stmts: &[Stmt], out: &mut HashSet<String>) {
        for stmt in stmts {
            match stmt {
                Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
                | Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _)
                | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) | Stmt::Defer(value, _) =>
                    Self::adressen_expr(value, out),
                Stmt::PropertyAssignment { object, value, .. } => {
                    Self::adressen_expr(object, out); Self::adressen_expr(value, out);
                }
                Stmt::IndexAssignment { object, index, value, .. } => {
                    Self::adressen_expr(object, out); Self::adressen_expr(index, out); Self::adressen_expr(value, out);
                }
                Stmt::If { condition, body, else_body, .. } => {
                    Self::adressen_expr(condition, out); Self::adressen(body, out); Self::adressen(else_body, out);
                }
                Stmt::While { condition: e, body, .. } | Stmt::For { iterable: e, body, .. } => {
                    Self::adressen_expr(e, out); Self::adressen(body, out);
                }
                Stmt::UnsafeBlock { body, .. } => Self::adressen(body, out),
                _ => {}
            }
        }
    }

    fn adressen_expr(expr: &Expr, out: &mut HashSet<String>) {
        match expr {
            Expr::FunctionCall { name, args, .. } => {
                if matches!(name.as_str(), "adresse_von" | "address_of") {
                    if let Some(w) = args.first().and_then(Self::wurzel) { out.insert(w.to_string()); }
                }
                for a in args { Self::adressen_expr(a, out); }
            }
            Expr::MethodCall { object, args, .. } => {
                Self::adressen_expr(object, out);
                for a in args { Self::adressen_expr(a, out); }
            }
            Expr::BinaryOp { left, right, .. } => { Self::adressen_expr(left, out); Self::adressen_expr(right, out); }
            Expr::UnaryOp { operand, .. } => Self::adressen_expr(operand, out),
            Expr::PropertyAccess { object, .. } => Self::adressen_expr(object, out),
            Expr::IndexAccess { object, index } => { Self::adressen_expr(object, out); Self::adressen_expr(index, out); }
            Expr::List(items) => for a in items { Self::adressen_expr(a, out); },
            Expr::Ternary { condition, then_val, else_val } => {
                Self::adressen_expr(condition, out); Self::adressen_expr(then_val, out); Self::adressen_expr(else_val, out);
            }
            _ => {}
        }
    }

    /// P2-T98 S5 (M-M004): ein geliehener Zeiger geht an einen extern-Parameter
    /// mit @behaelt(n); C speichert ihn ueber den Aufruf hinaus, danach zeigt er
    /// ins Leere. Ohne @behaelt bleibt die Weitergabe erlaubt (Vertrag: C
    /// behaelt den Zeiger nicht).
    fn behalten(&self, expr: &Expr, geliehen: &HashMap<String, String>) -> Option<String> {
        match expr {
            Expr::FunctionCall { name, args, .. } => args.iter().enumerate().find_map(|(i, a)| {
                if !(self.behaelt)(name.as_str(), i) { return None; }
                self.geliehen_direkt(a, geliehen).map(|q| format!(
                    "geliehener Zeiger ({q}) geht an Parameter {} von '{name}', der ihn laut @behaelt({}) ueber den Aufruf hinaus speichert; Vorschlag: die Daten global, in einem Puffer mit eigenem Besitz oder per Griff halten",
                    i + 1, i + 1))
            }).or_else(|| args.iter().find_map(|a| self.behalten(a, geliehen))),
            Expr::BinaryOp { left, right, .. } => self.behalten(left, geliehen).or_else(|| self.behalten(right, geliehen)),
            Expr::UnaryOp { operand, .. } => self.behalten(operand, geliehen),
            Expr::PropertyAccess { object, .. } => self.behalten(object, geliehen),
            Expr::IndexAccess { object, index } => self.behalten(object, geliehen).or_else(|| self.behalten(index, geliehen)),
            Expr::MethodCall { object, args, .. } =>
                self.behalten(object, geliehen).or_else(|| args.iter().find_map(|a| self.behalten(a, geliehen))),
            Expr::List(items) => items.iter().find_map(|a| self.behalten(a, geliehen)),
            Expr::Ternary { condition, then_val, else_val } => self.behalten(condition, geliehen)
                .or_else(|| self.behalten(then_val, geliehen)).or_else(|| self.behalten(else_val, geliehen)),
            _ => None,
        }
    }

    fn behalten_anweisung(&self, stmt: &Stmt, geliehen: &HashMap<String, String>) -> Option<String> {
        match stmt {
            Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
            | Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _)
            | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) => self.behalten(value, geliehen),
            Stmt::PropertyAssignment { object, value, .. } => self.behalten(object, geliehen).or_else(|| self.behalten(value, geliehen)),
            Stmt::IndexAssignment { object, index, value, .. } =>
                self.behalten(object, geliehen).or_else(|| self.behalten(index, geliehen)).or_else(|| self.behalten(value, geliehen)),
            _ => None,
        }
    }

    fn pruefe_anweisung(&self, stmt: &Stmt) -> Option<String> {
        match stmt {
            Stmt::Return(Some(value), _) => self.quelle(value).map(|q|
                format!("geliehener Zeiger entkommt per gib_zurück ({q}); dauerhafte Zeiger brauchen Speicher mit eigenem Besitz (Griff)")),
            Stmt::PropertyAssignment { object, value, .. } | Stmt::IndexAssignment { object, value, .. } => {
                let q = self.quelle(value)?;
                let wurzel = Self::wurzel(object).unwrap_or("?");
                if self.eigener_speicher(wurzel) || self.geliehen.contains_key(wurzel) { return None; }
                Some(format!("geliehener Zeiger ({q}) wird in Speicher hinter '{wurzel}' geschrieben, der den Aufruf ueberlebt"))
            }
            Stmt::Expression(Expr::FunctionCall { name, args, .. }, _) if matches!(name.as_str(),
                "speicher_schreiben" | "mem_write" | "volatile_schreiben" | "volatile_write") =>
                args.iter().skip(1).find_map(|a| self.quelle(a)).map(|q|
                    format!("geliehener Zeiger ({q}) wird per {name} in fremden Speicher geschrieben")),
            _ => None,
        }
    }
}

/// Eine native Funktion fuer fehlbare_funktionen (codegen.rs forward_declare).
pub(crate) struct FehlbarDef<'a> {
    pub(crate) name: String,
    pub(crate) body: &'a [Stmt],
    pub(crate) ergebnis: Option<&'a TypeRef>,
    pub(crate) params: Vec<(&'a str, Option<&'a TypeRef>)>,
}

/// Zahlart eines annotierten Typs; nur sie entscheidet ueber die Fehlbarkeit.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Zahlart { Signed, Unsigned, Float, Sonst }

fn zahlart_von(ty: NType) -> Zahlart {
    match ty {
        NType::F32 | NType::F64 => Zahlart::Float,
        NType::Bool => Zahlart::Sonst,
        t if t.is_signed_integer() => Zahlart::Signed,
        _ => Zahlart::Unsigned,
    }
}

fn zahlart(ty: &TypeRef) -> Zahlart {
    match &ty.kind {
        TypeKind::Name(name) => NType::from_name(name).map(zahlart_von).unwrap_or(Zahlart::Sonst),
        _ => Zahlart::Sonst,
    }
}

fn lokale_typen<'a>(stmts: &'a [Stmt], typen: &mut HashMap<&'a str, Zahlart>) {
    for stmt in stmts {
        match stmt {
            Stmt::Assignment { name, ty: Some(ty), .. } | Stmt::ConstAssignment { name, ty: Some(ty), .. } => {
                typen.insert(name.as_str(), zahlart(ty));
            }
            Stmt::If { body, else_body, .. } => { lokale_typen(body, typen); lokale_typen(else_body, typen); }
            Stmt::While { body, .. } | Stmt::UnsafeBlock { body, .. } | Stmt::For { body, .. } => lokale_typen(body, typen),
            _ => {}
        }
    }
}

struct FehlbarKontext<'k, 'a> {
    namen: &'k HashSet<&'a str>,
    fehlbar: &'k HashSet<String>,
    typen: &'k HashMap<&'a str, Zahlart>,
    ergebnisse: &'k HashMap<&'a str, Zahlart>,
    fremd: &'k HashSet<String>,
}

/// Zahlart eines Ausdrucks aus Annotationen; None = nicht bestimmbar (z. B.
/// reines Ganzzahlliteral, das sich dem Nachbarn anpasst).
fn expr_art(expr: &Expr, k: &FehlbarKontext<'_, '_>) -> Option<Zahlart> {
    match expr {
        Expr::Identifier(name) => k.typen.get(name.as_str()).copied(),
        Expr::NativeNumber { text, .. } => {
            let hex = text.starts_with("0x") || text.starts_with("0X") || text.starts_with("0b") || text.starts_with("0B");
            (!hex && text.contains(['.', 'e', 'E'])).then_some(Zahlart::Float)
        }
        Expr::FunctionCall { name, args, .. } => {
            if crate::native_arithmetic::cast_art(name).is_some() {
                match args.first() {
                    Some(Expr::Identifier(ziel)) => NType::from_name(ziel).map(zahlart_von),
                    _ => None,
                }
            } else if let Some(ty) = NType::from_name(name) {
                Some(zahlart_von(ty))
            } else {
                k.ergebnisse.get(name.as_str()).copied()
            }
        }
        Expr::BinaryOp { op, left, right } => match op {
            BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod | BinOp::Pow
            | BinOp::BitAnd | BinOp::BitOr | BinOp::BitXor => expr_art(left, k).or_else(|| expr_art(right, k)),
            BinOp::LShift | BinOp::RShift => expr_art(left, k),
            _ => Some(Zahlart::Sonst),
        },
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => expr_art(operand, k),
        _ => None,
    }
}

fn nicht_negatives_literal(expr: &Expr) -> bool {
    match expr {
        Expr::NativeNumber { .. } => true,
        Expr::Number(n) => *n >= 0.0,
        _ => false,
    }
}

fn endet_mit_rueckgabe(body: &[Stmt]) -> bool {
    match body.last() {
        Some(Stmt::Return(_, _)) => true,
        Some(Stmt::UnsafeBlock { body, .. }) => endet_mit_rueckgabe(body),
        _ => false,
    }
}

fn stmts_fehlbar(stmts: &[Stmt], k: &FehlbarKontext<'_, '_>) -> bool {
    let ex = |e: &Expr| expr_fehlbar(e, k);
    stmts.iter().any(|stmt| match stmt {
        Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
        | Stmt::Show(value, _) | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) => ex(value),
        Stmt::CompoundAssignment { op, value, .. } =>
            op.contains('/') || op.contains('%') || op.contains('<') || op.contains('>') || ex(value),
        Stmt::PropertyAssignment { object, value, .. } => ex(object) || ex(value),
        Stmt::IndexAssignment { .. } => true,
        Stmt::Return(None, _) | Stmt::Break(_) | Stmt::Continue(_) => false,
        Stmt::If { condition, body, else_body, .. } =>
            ex(condition) || stmts_fehlbar(body, k) || stmts_fehlbar(else_body, k),
        Stmt::While { condition, body, .. } => ex(condition) || stmts_fehlbar(body, k),
        Stmt::UnsafeBlock { body, .. } => stmts_fehlbar(body, k),
        _ => true,
    })
}

fn expr_fehlbar(expr: &Expr, k: &FehlbarKontext<'_, '_>) -> bool {
    let rec = |e: &Expr| expr_fehlbar(e, k);
    match expr {
        Expr::NativeNumber { .. } | Expr::Number(_) | Expr::Boolean(_) | Expr::Identifier(_)
        | Expr::None | Expr::String(_) => false,
        Expr::BinaryOp { op, left, right } => {
            let art = || expr_art(left, k).or_else(|| expr_art(right, k));
            let selbst = match op {
                BinOp::LShift | BinOp::RShift => true,
                BinOp::Div | BinOp::Mod => art() != Some(Zahlart::Float),
                BinOp::Pow => match art() {
                    Some(Zahlart::Float | Zahlart::Unsigned) => false,
                    _ => !nicht_negatives_literal(right),
                },
                _ => false,
            };
            selbst || rec(left) || rec(right)
        }
        Expr::UnaryOp { operand, .. } => rec(operand),
        Expr::PropertyAccess { object, .. } => rec(object),
        Expr::IndexAccess { .. } => true,
        Expr::List(elements) => elements.iter().any(rec),
        Expr::FunctionCall { name, args, .. } => {
            let cast_fehlbar = matches!(crate::native_arithmetic::cast_art(name),
                Some(crate::native_arithmetic::CastArt::Geprueft | crate::native_arithmetic::CastArt::Saettigend));
            cast_fehlbar || (k.namen.contains(name.as_str()) && k.fehlbar.contains(name))
                || k.fremd.contains(name.as_str())
                || (!k.fremd.is_empty() && k.typen.contains_key(name.as_str()))
                || args.iter().any(rec)
        }
        _ => true,
    }
}

/// Eigene native System-Signatur: lokale Composite-Werte direkt in LLVM,
/// ohne die bewusst engeren Hardware-/FFI-Signaturen umzudeuten.
#[derive(Clone, Debug)]
pub(crate) struct SystemSignature {
    params: Vec<STy>,
    result: Option<STy>,
}

impl SystemSignature {
    /// P2-T66 Stufe 2a: rein skalare Signatur (Int/Float, seit P2-T34 auch
    /// bool) als NSignature fuer den dynamischen Aufruf-Wrapper. Zeiger und
    /// Strukturen -> None.
    pub(crate) fn scalar_signature(&self) -> Option<crate::codegen_native::NSignature> {
        fn scalar(ty: &STy) -> Option<NType> {
            match ty { STy::Int(t) | STy::Float(t) => Some(*t), STy::Bool => Some(NType::Bool), _ => None }
        }
        let params = self.params.iter().map(scalar).collect::<Option<Vec<_>>>()?;
        let result = match &self.result { None => None, Some(ty) => Some(scalar(ty)?) };
        Some(crate::codegen_native::NSignature { params, result })
    }

    pub(crate) fn from_annotations(
        params: &[Option<TypeRef>], result: Option<&TypeRef>, nominals: &NominalTypes,
    ) -> Result<Self, String> {
        let params = params.iter().enumerate().map(|(index, ty)| {
            let ty = ty.as_ref()
                .ok_or_else(|| format!("Typ des nativen Parameters {} fehlt", index + 1))?;
            STy::from_type_ref(ty, nominals)
        }).collect::<Result<Vec<_>, _>>()?;
        let result = result.ok_or("Rueckgabetyp der nativen Funktion fehlt")?;
        let result = match &result.kind {
            TypeKind::Name(name) if name == "void" => None,
            _ => Some(STy::from_type_ref(result, nominals)?),
        };
        if params.iter().any(|ty| matches!(ty, STy::Array { .. })) || matches!(result, Some(STy::Array { .. })) {
            return Err("Feste Arrays sind als Parameter/Rueckgabe by value nicht freigegeben (ptr[T] oder eine Struktur mit Arrayfeld verwenden)".into());
        }
        if matches!(result, Some(STy::Buffer(_) | STy::StrukturPuffer(_))) {
            return Err("Rueckgabetyp puffer[T] ist nicht freigegeben (nativ ist ein Puffer ein geliehener Ausschnitt)".into());
        }
        Ok(Self { params, result })
    }

    /// P2-T50 B4: Signatur fuer den dynamischen Aufruf-Wrapper. Wie
    /// scalar_signature, zusaetzlich puffer[T]-Parameter: an diesen Positionen
    /// traegt die NSignature den Elementtyp NUR als Platzhalter; der zweite Wert
    /// nennt sie (Some(T)). INVARIANTE: der Adapter wandelt diese Positionen nie
    /// ueber die Skalar-Argumentpfade, sondern pinnt den Puffer.
    /// P2-T23/T53 B2: ptr[void]/ptr[Struktur] als Griff - Platzhalter U64 in der
    /// NSignature; dritter Wert: Zeigerergebnis als Griff (Some(ziel)).
    /// P2-T49 B2: Strukturwert-Parameter als PufferArt::Wert (Platzhalter U64),
    /// Strukturwert-Ergebnis als DynErgebnis::Struktur (NSignature ohne Ergebnis,
    /// die Funktion schreibt in den sret-Puffer). Ob die Struktur boxbar ist,
    /// prueft der Aufrufer (NativeStructRegistry::boxbar).
    pub(crate) fn dyn_signature(&self) -> Option<(crate::codegen_native::NSignature, Vec<Option<PufferArt>>, Option<DynErgebnis>)> {
        let mut params = Vec::with_capacity(self.params.len());
        let mut puffer = Vec::with_capacity(self.params.len());
        for ty in &self.params {
            match ty {
                STy::Int(t) | STy::Float(t) => { params.push(*t); puffer.push(None); }
                STy::Bool => { params.push(NType::Bool); puffer.push(None); }
                STy::Buffer(t) => { params.push(*t); puffer.push(Some(PufferArt::Skalar(*t))); }
                STy::StrukturPuffer(NativeTypeId::Nominal { name, .. }) => {
                    params.push(NType::U64);
                    puffer.push(Some(PufferArt::Struktur(name.clone())));
                }
                STy::Struct(NativeTypeId::Nominal { name, .. }) => {
                    params.push(NType::U64);
                    puffer.push(Some(PufferArt::Wert(name.clone())));
                }
                other => {
                    let ziel = griff_ziel(other)?;
                    params.push(NType::U64);
                    puffer.push(Some(PufferArt::Griff { ziel, verbraucht: false }));
                }
            }
        }
        let mut ergebnis = None;
        let result = match &self.result {
            None => None,
            Some(STy::Int(t) | STy::Float(t)) => Some(*t),
            Some(STy::Bool) => Some(NType::Bool),
            Some(STy::Struct(NativeTypeId::Nominal { name, .. })) => {
                ergebnis = Some(DynErgebnis::Struktur(name.clone()));
                None
            }
            Some(other) => {
                ergebnis = Some(DynErgebnis::Griff(griff_ziel(other)?));
                Some(NType::U64)
            }
        };
        Some((crate::codegen_native::NSignature { params, result }, puffer, ergebnis))
    }

    /// P2-T50 B1: Puffer sind nur fuer interne Moo-Aufrufe freigegeben.
    pub(crate) fn uses_buffers(&self) -> bool {
        self.params.iter().any(|ty| matches!(ty, STy::Buffer(_) | STy::StrukturPuffer(_)))
    }

    /// By-value-Nominaltypen sind vorerst fuer interne Moo-Aufrufe freigegeben.
    pub(crate) fn uses_struct_values(&self) -> bool {
        self.params.iter().any(|ty| matches!(ty, STy::Struct(_)))
            || matches!(self.result.as_ref(), Some(STy::Struct(_)))
    }

    /// Struktur-Rueckgabe: der Aufrufer stellt den Puffer (sret, Parameter 0).
    pub(crate) fn returns_struct(&self) -> bool {
        matches!(self.result, Some(STy::Struct(_)))
    }

    /// Interne Moo-zu-Moo-Signatur. Strukturwerte gehen nicht als LLVM-
    /// Aggregat durch Register (teure insertvalue/extractvalue-Ketten und
    /// Stack-Kopien), sondern als Zeiger auf eine exklusive, ABI-ausgerichtete
    /// Aufruferkopie; eine Struktur-Rueckgabe wird in einen sret-Puffer des
    /// Aufrufers geschrieben. Gilt nur fuer __moo_native.*-Funktionen mit
    /// interner Bindung; die C-ABI-Klassifizierung (ffi_types) ist getrennt.
    pub(crate) fn llvm_type<'ctx>(
        &self, context: &'ctx Context, layouts: &NativeStructRegistry<'ctx>,
    ) -> Result<inkwell::types::FunctionType<'ctx>, String> {
        let pointer: BasicMetadataTypeEnum<'ctx> = context.ptr_type(AddressSpace::default()).into();
        let mut params: Vec<BasicMetadataTypeEnum<'ctx>> = Vec::with_capacity(self.params.len() + 1);
        if self.returns_struct() { params.push(pointer); }
        for ty in &self.params {
            params.push(match ty {
                STy::Struct(_) => pointer,
                other => other.llvm(context, layouts)?.into(),
            });
        }
        match &self.result {
            Some(STy::Struct(_)) | None => Ok(context.void_type().fn_type(&params, false)),
            Some(ty) => Ok(ty.llvm(context, layouts)?.fn_type(&params, false)),
        }
    }
}

/// Review c7eddc37: `-<Literal>` (ungleich 0) als Index liegt ausserhalb jedes
/// u64-Bereichs; liefert die Anzeige ("-1") fuer M-M001.
fn negatives_index_literal(index: &Expr) -> Option<String> {
    match index {
        Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
            Expr::NativeNumber { text, legacy, .. } if *legacy != 0.0 => Some(format!("-{text}")),
            _ => None,
        },
        _ => None,
    }
}

/// Review c7eddc37 (#38462): Strukturwerte auch nur als Zwischenwert
/// (vec4(...)[i], vec4(...).y, betrag(vec3(...)), Punkt(...).x) brauchen den
/// System-Emitter; der Pure-Emitter kennt nur Skalare (vorher M-C000).
pub(crate) fn has_struct_values(body: &[Stmt], nominals: &NominalTypes) -> bool {
    has_struct_locals(body, nominals) || body.iter().any(|stmt| stmt_baut_struktur(stmt, nominals))
}

fn stmt_baut_struktur(stmt: &Stmt, n: &NominalTypes) -> bool {
    let e = |x: &Expr| expr_baut_struktur(x, n);
    let b = |body: &[Stmt]| body.iter().any(|s| stmt_baut_struktur(s, n));
    match stmt {
        Stmt::Assignment { value, .. } | Stmt::ConstAssignment { value, .. }
        | Stmt::CompoundAssignment { value, .. } | Stmt::Show(value, _)
        | Stmt::Expression(value, _) | Stmt::Return(Some(value), _) => e(value),
        Stmt::PropertyAssignment { object, value, .. } => e(object) || e(value),
        Stmt::IndexAssignment { object, index, value, .. } => e(object) || e(index) || e(value),
        Stmt::If { condition, body, else_body, .. } => e(condition) || b(body) || b(else_body),
        Stmt::While { condition, body, .. } => e(condition) || b(body),
        Stmt::For { iterable, body, .. } => e(iterable) || b(body),
        Stmt::UnsafeBlock { body, .. } => b(body),
        _ => false,
    }
}

fn expr_baut_struktur(expr: &Expr, n: &NominalTypes) -> bool {
    let e = |x: &Expr| expr_baut_struktur(x, n);
    match expr {
        Expr::FunctionCall { name, args, .. } =>
            n.contains_key(name) || crate::native_struct::vecmat_art_name(name).is_some() || args.iter().any(e),
        Expr::BinaryOp { left, right, .. } => e(left) || e(right),
        Expr::UnaryOp { operand, .. } => e(operand),
        Expr::PropertyAccess { object, .. } => e(object),
        Expr::IndexAccess { object, index } => e(object) || e(index),
        Expr::List(items) => items.iter().any(e),
        _ => false,
    }
}

/// Nur Bestandsformen ausnehmen, die ein engerer Pfad bereits abdeckt.
pub(crate) fn is_system_body(body: &[Stmt], params: &[String]) -> bool {
    let inner = match body {
        [Stmt::UnsafeBlock { body, .. }] => body.as_slice(),
        _ => return false,
    };
    match inner {
        [] => false,
        // P2-T89 C: der enge Zeigerpfad (codegen_native::emit_pointer_load_function)
        // kennt nur p[i] auf einem Parameter; m[0][i], p.feld[i] usw. laufen im
        // Systemkoerper (vorher Compilefehler im engen Pfad).
        [Stmt::Return(Some(Expr::IndexAccess { object, .. }), _)]
            if matches!(object.as_ref(), Expr::Identifier(n) if params.contains(n)) => false,
        [Stmt::Return(Some(Expr::FunctionCall { name, .. }), _)]
        | [Stmt::Expression(Expr::FunctionCall { name, .. }, _)] => !params.contains(name),
        _ => true,
    }
}

/// P2-T12 C1 (Plan 31194239): exakt die Koerperform, die der Pure-Emitter
/// (codegen_native::emit_nontrapping_function) annimmt: nur typisierte Locals,
/// danach hoechstens ein abschliessendes gib_zurück (oder leerer Koerper).
/// Alles andere ohne unsicher: laeuft im System-Emitter im Sicher-Modus.
pub(crate) fn is_pure_slice(body: &[Stmt]) -> bool {
    let locals = match body.split_last() {
        None => return true,
        Some((Stmt::Return(_, _), locals)) => locals,
        Some(_) => body,
    };
    locals.iter().all(|stmt| matches!(stmt,
        Stmt::Assignment { ty: Some(_), .. } | Stmt::ConstAssignment { ty: Some(_), .. }))
}

/// P2-T74 (ADR §3, statisch erkennbar): Hat ein Koerper mit Rueckgabetyp kein
/// einziges gib_zurück und keine Schleife, erreicht jeder Aufruf das Funktions-
/// ende; emit_system_function lehnt ihn als Compilefehler ab. Koerper, die nur
/// auf manchen Pfaden zurueckkehren (wenn ohne sonst) oder Schleifen enthalten,
/// behalten das Laufzeitnetz (emit_hosted_fehler 'endet ohne Rueckgabewert'
/// bzw. Trap), weil der Weg zum Ende dort nicht statisch sicher ist.
fn enthaelt_rueckgabe_oder_schleife(body: &[Stmt]) -> bool {
    body.iter().any(|stmt| match stmt {
        Stmt::Return(_, _) | Stmt::While { .. } | Stmt::For { .. } => true,
        Stmt::UnsafeBlock { body, .. } => enthaelt_rueckgabe_oder_schleife(body),
        Stmt::If { body, else_body, .. } =>
            enthaelt_rueckgabe_oder_schleife(body) || enthaelt_rueckgabe_oder_schleife(else_body),
        _ => false,
    })
}

/// Enger Hosted-Selector: nur Funktionen mit wirklichen lokalen
/// Strukturwerten verwenden den System-Emitter. Andere Hosted-Funktionen
/// behalten ihren bestehenden Scalar-/FFI-Emitter.
pub(crate) fn has_struct_locals(body: &[Stmt], nominals: &NominalTypes) -> bool {
    body.iter().any(|stmt| match stmt {
        Stmt::Assignment { ty: Some(ty), .. }
        | Stmt::ConstAssignment { ty: Some(ty), .. } =>
            matches!(&ty.kind, TypeKind::Name(name) if nominals.contains_key(name)),
        Stmt::UnsafeBlock { body, .. } | Stmt::While { body, .. } | Stmt::For { body, .. } =>
            has_struct_locals(body, nominals),
        Stmt::If { body, else_body, .. } =>
            has_struct_locals(body, nominals) || has_struct_locals(else_body, nominals),
        _ => false,
    })
}

fn conversion(name: &str) -> Option<NType> {
    Some(match name {
        "u8" => NType::U8, "u16" => NType::U16, "u32" => NType::U32, "u64" => NType::U64,
        "i8" => NType::I8, "i16" => NType::I16, "i32" => NType::I32, "i64" => NType::I64,
        _ => return None,
    })
}

fn width_type(expr: &Expr) -> Result<NType, String> {
    match expr {
        Expr::NativeNumber { text, .. } => match text.as_str() {
            "1" => Ok(NType::U8), "2" => Ok(NType::U16), "4" => Ok(NType::U32), "8" => Ok(NType::U64),
            _ => Err(format!("Speicherbreite {text} ungueltig (1, 2, 4 oder 8)")),
        },
        _ => Err("Speicherbreite muss ein Literal 1, 2, 4 oder 8 sein".into()),
    }
}

fn port_bits(name: &str) -> Option<(bool, u32)> {
    Some(match name {
        "port_lese" | "port_in" => (true, 8),
        "port_lese16" | "port_in16" => (true, 16),
        "port_lese32" | "port_in32" => (true, 32),
        "port_schreibe" | "port_out" => (false, 8),
        "port_schreibe16" | "port_out16" => (false, 16),
        "port_schreibe32" | "port_out32" => (false, 32),
        _ => return None,
    })
}

/// P2-T98 (M-I006): Builtins, die in @nativ-Koerpern nur innerhalb von
/// unsicher: stehen duerfen (Rohzeiger, Speicher, Hardware) bzw. ein bewusstes
/// Escape sind (puffer_zeiger). Eigene Funktionen und extern duerfen sie nicht
/// verschatten (main.rs definitionen_registrieren), sonst waere offen, ob
/// `zeiger(x)` die eigene Funktion oder den Rohzugriff meint.
pub(crate) fn ist_unsicheres_builtin(name: &str) -> bool {
    port_bits(name).is_some() || barrier(name).is_some() || matches!(name,
        "zeiger" | "pointer" | "adresse_von" | "address_of" | "speicher_lesen" | "mem_read"
        | "speicher_schreiben" | "mem_write" | "puffer_zeiger" | "buffer_pointer"
        | "volatile_lesen" | "volatile_read" | "volatile_schreiben" | "volatile_write"
        | "strukturfeld_lesen" | "struct_field_read" | "strukturfeld_schreiben" | "struct_field_write"
        | "registerfeld_lesen" | "register_field_read" | "registerfeld_schreiben" | "register_field_write"
        | "assembler")
}

fn barrier(name: &str) -> Option<u8> {
    Some(match name {
        "speicherbarriere" | "memory_barrier" => 0,
        "lesebarriere" | "read_barrier" => 1,
        "schreibbarriere" | "write_barrier" => 2,
        "compilerbarriere" | "compiler_barrier" => 3,
        _ => return None,
    })
}

fn port_type(bits: u32) -> NType {
    match bits { 8 => NType::U8, 16 => NType::U16, _ => NType::U32 }
}

fn bits_of(ty: NType) -> u32 {
    match ty {
        NType::I8 | NType::U8 => 8,
        NType::I16 | NType::U16 => 16,
        NType::I32 | NType::U32 => 32,
        _ => 64,
    }
}

struct Slot<'ctx> {
    ty: STy,
    ptr: PointerValue<'ctx>,
    constant: bool,
}

/// Woher die Adresse eines Strukturwerts stammt. Entscheidet, ob zwei Orte
/// beweisbar disjunkt sind (direkte memcpy) oder ueberlappen koennten.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Origin {
    /// Nur hier referenzierter Zwischenspeicher aus entry_alloca.
    Fresh,
    /// Slot eines Locals/Parameters (eigene Alloca bzw. exklusive Aufruferkopie).
    Slot(String),
    /// Beliebiger Speicher hinter Zeigern, Feldern oder Arrayelementen.
    Memory,
}

/// Speicherort eines Strukturwerts mit der dort bewiesenen Ausrichtung.
#[derive(Clone, Debug)]
struct Place<'ctx> {
    ptr: PointerValue<'ctx>,
    alignment: u32,
    origin: Origin,
}

struct Emitter<'a, 'ctx> {
    context: &'ctx Context,
    module: &'a Module<'ctx>,
    target: &'a TargetData,
    layouts: &'a NativeStructRegistry<'ctx>,
    nominals: &'a NominalTypes,
    triple: &'a str,
    x86: bool,
    /// Hosted Usermode (Linux/Windows): keine privilegierten oder MMIO-Builtins.
    hosted: bool,
    function: FunctionValue<'ctx>,
    /// Eintrittsblock: dort liegen alle Allocas (Slots und Zwischenspeicher),
    /// nur so erkennt LLVM (SROA/MemCpyOpt) sie als wegoptimierbare Stack-Slots.
    entry: BasicBlock<'ctx>,
    /// Puffer des Aufrufers fuer eine Struktur-Rueckgabe (sret, Parameter 0).
    sret: Option<PointerValue<'ctx>>,
    builder: Builder<'ctx>,
    slots: HashMap<String, Slot<'ctx>>,
    loops: Vec<(BasicBlock<'ctx>, BasicBlock<'ctx>)>,
    trap: Option<BasicBlock<'ctx>>,
    /// Nie erreichter Block fuer Operationen ohne Fehlerfall (siehe never_block).
    never: Option<BasicBlock<'ctx>>,
    /// P2-T58 C3: Rueckkehr mit Nullwert nach fehlgeschlagenem Callee (weiter_block).
    weiter: Option<BasicBlock<'ctx>>,
    /// P2-T58 (Review 06d10060 #3): Fehlerblock negativer Exponent (pow_fehler_block).
    pow_fehler: Option<BasicBlock<'ctx>>,
    result: Option<STy>,
    callees: &'a HashMap<String, NativeCallee<'ctx>>,
    externs: &'a crate::ffi_types::ExternRegistry<'ctx>,
    /// P2-T12 A (Plan 31194239): sicherer @nativ-Koerper (nicht ganz in
    /// unsicher:). Unsichere Operationen nur innerhalb von unsicher:-Bloecken.
    sicher: bool,
    unsicher_tiefe: u32,
    /// P2-T84: aus C aufrufbarer Export (@einstieg); seit P2-T56 D behalten
    /// Struktur-Exporte den internen Namen, daher nicht am Namen ablesbar.
    export: bool,
    /// P2-T98: Quellzeile der gerade uebersetzten Anweisung (M-M001/M-M005).
    zeile: usize,
}

fn e(err: impl ToString) -> String { err.to_string() }

/// P2-T16 (Plan 3f03704a E4, Freigabe #38101 O1): eingebaute Funktionen der
/// Vektortypen. Die Norm heisst betrag/magnitude; `länge` bleibt Elementanzahl.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum VmFunktion {
    Punkt,
    Kreuz,
    Betrag,
    Normalisiere,
}

/// Durch Basisausrichtung und Offset bewiesene Ausrichtung einer Feldadresse.
fn field_alignment(base: u32, offset: u64) -> u32 {
    if offset == 0 { return base; }
    let lowest = 1u64 << offset.trailing_zeros().min(31);
    base.min(lowest as u32)
}

/// Attribute eines internen Struktur-Zeigerparameters: der Aufrufer stellt
/// eine exklusive (noalias), ABI-ausgerichtete Kopie voller Groesse. Funktion
/// und Aufrufstelle tragen dieselben Attribute, damit LLVM beide Seiten
/// gleich sieht (Inliner, MemCpyOpt, sret-Lowering).
fn struct_pointer_attributes<'ctx>(
    context: &'ctx Context, llvm: BasicTypeEnum<'ctx>, target: &TargetData, sret: bool,
) -> Vec<Attribute> {
    let mut attributes = vec![
        context.create_enum_attribute(Attribute::get_named_enum_kind_id("noalias"), 0),
        context.create_enum_attribute(
            Attribute::get_named_enum_kind_id("align"), u64::from(target.get_abi_alignment(&llvm)),
        ),
    ];
    let size = target.get_abi_size(&llvm);
    if size > 0 {
        attributes.push(context.create_enum_attribute(
            Attribute::get_named_enum_kind_id("dereferenceable"), size,
        ));
    }
    if sret {
        attributes.push(context.create_type_attribute(
            Attribute::get_named_enum_kind_id("sret"), llvm.as_any_type_enum(),
        ));
    }
    attributes
}

impl<'a, 'ctx> Emitter<'a, 'ctx> {
    fn i64t(&self) -> inkwell::types::IntType<'ctx> { self.context.i64_type() }
    fn ptrt(&self) -> inkwell::types::PointerType<'ctx> { self.context.ptr_type(AddressSpace::default()) }

    fn open(&self) -> bool {
        self.builder.get_insert_block().is_some_and(|block| block.get_terminator().is_none())
    }

    /// Nach return/stopp/weiter: Code im toten Rest bekommt einen eigenen Block.
    fn ensure_open(&self) {
        if !self.open() {
            let dead = self.context.append_basic_block(self.function, "sys.tot");
            self.builder.position_at_end(dead);
        }
    }

    /// Fehlerblock der Funktion (memoisiert). Bare/UEFI: llvm.trap, keine
    /// Rueckkehr. Hosted (P2-T34, ADR §3): moo_native_fehler setzt das Moo-
    /// Fehlerflag, die Funktion kehrt mit Nullwert zurueck; ohne aktiven
    /// Handler beendet moo_throw den Prozess mit Diagnose (moo_error.c).
    /// Nur Moo-interne hosted Funktionen (__moo_native.*, Aufruf aus Moo oder
    /// ueber den dyn-Adapter) melden fangbar. Aus C aufrufbare Exporte
    /// (@einstieg, externer Name) behalten den Trap: Ein Moo-Fehler darf nicht
    /// ueber fremde C-Frames entweichen (ADR §11) und das Objekt bleibt ohne
    /// Moo-Runtime linkbar (ADR §13).
    fn fangbar(&self) -> bool {
        self.hosted && !self.export
            && self.function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."))
    }

    /// P2-T84 (Plan 91033131 §6): hosted C-Export bricht bei Fehlern mit Diagnose
    /// ab (moo_native_abbruch) statt einen erfundenen Wert an C zu liefern.
    fn abbruch(&self) -> bool {
        self.hosted && self.export
    }

    /// P2-T21/T57 C1 (Plan f4655d20 E2/E7): Funktionsname dort, wo ein
    /// C-Funktionszeiger ptr[fn(...)] erwartet wird -> Adresse des Thunks
    /// __moo_rueckruf.<name>, nur bei identischer Funktionsidentitaet.
    fn rueckruf_adresse(&self, name: &str, expected: Option<&STy>) -> Result<SVal<'ctx>, String> {
        let erwartet = match expected {
            Some(STy::Ptr(identity @ NativeTypeId::Pointer { pointee, .. }))
                if matches!(pointee.as_ref(), NativeTypeId::Function { .. }) => identity.clone(),
            _ => return Err(format!(
                "Systemkoerper: Funktion '{name}' ist als Wert nur dort erlaubt, wo ein C-Funktionszeiger ptr[fn(...)] erwartet wird"
            )),
        };
        let NativeTypeId::Pointer { pointee, .. } = &erwartet else { unreachable!() };
        let callee = self.callees.get(name).ok_or("Systemkoerper: Rueckrufziel fehlt")?;
        let Some((thunk, identity)) = &callee.rueckruf else {
            return Err(format!(
                "Systemkoerper: '{name}' ist kein C-Rückruf - die Funktion braucht @rückruf (native Moo-Funktionen sind nicht automatisch C-Funktionszeiger; Zustand ueber user_data)"
            ));
        };
        if identity != pointee.as_ref() {
            return Err(format!(
                "Systemkoerper: Rückruf '{name}' hat die Signatur {identity}, erwartet wird {}", pointee.as_ref()
            ));
        }
        if self.sicher && self.unsicher_tiefe == 0 {
            return Err(crate::diagnose::vormerken("M-M005", self.zeile, format!("Systemkoerper: C-Funktionszeiger auf '{name}' nur innerhalb von unsicher:")));
        }
        Ok(SVal { ty: STy::Ptr(erwartet.clone()), value: thunk.as_global_value().as_pointer_value().into() })
    }

    /// P2-T21/T57 C2: enthaelt das Modul fehlbare Rueckrufe, kann ein Fremdaufruf
    /// (extern C, indirekt) mit anstehendem Moo-Fehler zurueckkehren -> sofort
    /// ohne weitere Seiteneffekte zurueck (weiter_block) bzw. im Export Abbruch.
    fn nach_fremdaufruf_pruefen(&mut self) -> Result<(), String> {
        let moeglich = self.hosted && self.callees.values().any(|c| c.fehlbar && c.rueckruf.is_some());
        if !moeglich || !(self.fangbar() || self.abbruch()) { return Ok(()); }
        let hook = crate::codegen_native::native_fehler_offen_hook(self.context, self.module)?;
        let flag = match self.builder.build_call(hook, &[], "sys.fremd.offen").map_err(e)?.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
            _ => return Err("moo_native_fehler_offen liefert keinen Wert".into()),
        };
        let offen = self.builder.build_int_compare(
            IntPredicate::NE, flag, self.context.i32_type().const_zero(), "sys.fremd.gesetzt",
        ).map_err(e)?;
        let ziel = if self.abbruch() {
            self.abbruch_block("Fehler aus einem Rückruf")?
        } else {
            self.weiter_block()?
        };
        let ok = self.context.append_basic_block(self.function, "sys.fremd.ok");
        self.builder.build_conditional_branch(offen, ziel, ok).map_err(e)?;
        self.builder.position_at_end(ok);
        Ok(())
    }

    fn emit_abbruch(&self, grund: &str) -> Result<(), String> {
        let name = self.function.get_name().to_str().unwrap_or("?");
        let name = name.strip_prefix("__moo_native.").unwrap_or(name);
        crate::codegen_native::emit_abbruch(self.context, &self.builder, self.module, name, grund)
    }

    /// Abbruchblock nach einem fehlgeschlagenen Moo-internen Callee (P2-T84).
    fn abbruch_block(&mut self, grund: &str) -> Result<BasicBlock<'ctx>, String> {
        let current = self.builder.get_insert_block();
        let block = self.context.append_basic_block(self.function, "sys.abbruch");
        self.builder.position_at_end(block);
        self.emit_abbruch(grund)?;
        if let Some(current) = current { self.builder.position_at_end(current); }
        Ok(block)
    }

    /// P2-T58 C3 (Plan 4500717b): Ziel nach einem fehlbaren nativen Aufruf, dessen
    /// Fehler schon gemeldet ist (Flag gesetzt). Rueckkehr mit Nullwert bzw. void
    /// OHNE neue Meldung - der Erstfehler bleibt, keine Seiteneffekte danach.
    /// Memoisiert; nur hosted Moo-intern (fangbar), Bare/C-Exporte trappen ohnehin.
    fn weiter_block(&mut self) -> Result<BasicBlock<'ctx>, String> {
        if let Some(block) = self.weiter { return Ok(block); }
        let current = self.builder.get_insert_block();
        let block = self.context.append_basic_block(self.function, "sys.fehler.weiter");
        self.builder.position_at_end(block);
        match self.function.get_type().get_return_type() {
            Some(ty) => { self.builder.build_return(Some(&ty.const_zero())).map_err(e)?; }
            None => { self.builder.build_return(None).map_err(e)?; }
        }
        if let Some(current) = current { self.builder.position_at_end(current); }
        self.weiter = Some(block);
        Ok(block)
    }

    fn trap_block(&mut self) -> Result<BasicBlock<'ctx>, String> {
        if let Some(block) = self.trap { return Ok(block); }
        let current = self.builder.get_insert_block();
        let fangbar = self.fangbar();
        let name = if fangbar { "sys.fehler" } else { "sys.trap" };
        let block = self.context.append_basic_block(self.function, name);
        self.builder.position_at_end(block);
        if fangbar {
            self.emit_hosted_fehler("Nativer Laufzeitfehler (Division, Shift, Index oder Konversion)")?;
        } else if self.abbruch() {
            self.emit_abbruch("Nativer Laufzeitfehler (Division, Shift, Index oder Konversion)")?;
        } else {
            self.emit_trap()?;
        }
        if let Some(current) = current { self.builder.position_at_end(current); }
        self.trap = Some(block);
        Ok(block)
    }

    /// P2-T58 (Review 06d10060 #3, Plan f40b5980 §2.1): negativer Exponent bei
    /// signed ** meldet hosted `Native Potenz mit negativem Exponenten in '<fn>'`
    /// (wie der Pure-Lowerer); bare/@einstieg trappt wie trap_block.
    fn pow_fehler_block(&mut self) -> Result<BasicBlock<'ctx>, String> {
        if let Some(block) = self.pow_fehler { return Ok(block); }
        if !self.fangbar() { return self.trap_block(); }
        let current = self.builder.get_insert_block();
        let block = self.context.append_basic_block(self.function, "sys.fehler.pow");
        self.builder.position_at_end(block);
        self.emit_hosted_fehler("Native Potenz mit negativem Exponenten")?;
        if let Some(current) = current { self.builder.position_at_end(current); }
        self.pow_fehler = Some(block);
        Ok(block)
    }

    /// Hosted Fehlerpfad: `moo_native_fehler("<grund> in '<fn>'")` (compiler/
    /// runtime/moo_error.c) und Rueckkehr mit Nullwert bzw. void; der sret-Puffer
    /// einer Struktur-Rueckgabe bleibt unberuehrt. Deklaration erst bei Bedarf,
    /// damit Module ohne fehlerfaehige Operationen ihr bisheriges IR behalten.
    /// Aufrufer: trap_block (Operationsfehler) und emit_system_function
    /// (Funktionsende ohne gib_zurück).
    fn emit_hosted_fehler(&self, grund: &str) -> Result<(), String> {
        let hook = crate::codegen_native::native_fehler_hook(self.context, self.module)?;
        let name = self.function.get_name().to_str().unwrap_or("?");
        let name = name.strip_prefix("__moo_native.").unwrap_or(name);
        let text = format!("{grund} in '{name}'");
        let message = self.builder.build_global_string_ptr(&text, "sys.fehler.text").map_err(e)?;
        self.builder.build_call(hook, &[message.as_pointer_value().into()], "").map_err(e)?;
        match self.function.get_type().get_return_type() {
            Some(ty) => { self.builder.build_return(Some(&ty.const_zero())).map_err(e)?; }
            None => { self.builder.build_return(None).map_err(e)?; }
        }
        Ok(())
    }

    /// Nie erreichter Block (memoisiert) fuer Operationen ohne Fehlerfall
    /// (Add/Sub/Mul/Bitops, Wrap-Cast, verlustfreie Konversion): int_binary und
    /// cast verlangen einen Block derselben Funktion, springen dort aber nie hin.
    /// So bleiben hosted Systemkoerper ohne fehlerfaehige Operation frei von
    /// moo_native_fehler und String-Globals (C-Export, llvm-extract im hosted-Gate).
    fn never_block(&mut self) -> Result<BasicBlock<'ctx>, String> {
        if let Some(block) = self.never { return Ok(block); }
        let current = self.builder.get_insert_block();
        let block = self.context.append_basic_block(self.function, "sys.nie");
        self.builder.position_at_end(block);
        self.builder.build_unreachable().map_err(e)?;
        if let Some(current) = current { self.builder.position_at_end(current); }
        self.never = Some(block);
        Ok(block)
    }

    fn emit_trap(&self) -> Result<(), String> {
        let trap = match self.module.get_function("llvm.trap") {
            Some(f) => f,
            None => self.module.add_function("llvm.trap", self.context.void_type().fn_type(&[], false), None),
        };
        self.builder.build_call(trap, &[], "").map_err(e)?;
        self.builder.build_unreachable().map_err(e)?;
        Ok(())
    }

    fn zero(&self, ty: &STy) -> Result<BasicValueEnum<'ctx>, String> {
        Ok(match ty {
            STy::Int(t) => t.llvm_type(self.context).into_int_type().const_zero().into(),
            STy::Float(t) => t.llvm_type(self.context).into_float_type().const_zero().into(),
            STy::Bool => self.context.bool_type().const_zero().into(),
            STy::Ptr(_) => self.ptrt().const_null().into(),
            STy::Struct(_) | STy::Buffer(_) | STy::StrukturPuffer(_) =>
                ty.llvm(self.context, self.layouts)?.into_struct_type().const_zero().into(),
            STy::Array { .. } =>
                ty.llvm(self.context, self.layouts)?.into_array_type().const_zero().into(),
        })
    }

    fn declare_locals(&mut self, body: &[Stmt], entry: &Builder<'ctx>) -> Result<(), String> {
        for stmt in body {
            match stmt {
                Stmt::Assignment { name, ty: Some(ty), .. } | Stmt::ConstAssignment { name, ty: Some(ty), .. } => {
                    if self.slots.contains_key(name) {
                        return Err(format!("Systemkoerper: '{name}' ist bereits deklariert"));
                    }
                    let sty = STy::from_type_ref(ty, self.nominals)
                        .map_err(|err| format!("Systemkoerper: '{name}': {err}"))?;
                    let llvm = sty.llvm(self.context, self.layouts)?;
                    let ptr = entry.build_alloca(llvm, name).map_err(e)?;
                    // Explizit ABI-ausgerichtet: darauf stuetzen sich
                    // local_array_alignment und die Struktur-Speicherkopien.
                    let alignment = self.target.get_abi_alignment(&llvm);
                    ptr.as_instruction().ok_or("Systemkoerper: Alloca fehlt")?
                        .set_alignment(alignment).map_err(e)?;
                    entry.build_store(ptr, self.zero(&sty)?).map_err(e)?
                        .set_alignment(alignment).map_err(e)?;
                    let constant = matches!(stmt, Stmt::ConstAssignment { .. });
                    self.slots.insert(name.clone(), Slot { ty: sty, ptr, constant });
                }
                Stmt::UnsafeBlock { body, .. } | Stmt::While { body, .. } | Stmt::For { body, .. } => self.declare_locals(body, entry)?,
                Stmt::If { body, else_body, .. } => {
                    self.declare_locals(body, entry)?;
                    self.declare_locals(else_body, entry)?;
                }
                _ => {}
            }
        }
        Ok(())
    }

    fn block(&mut self, body: &[Stmt]) -> Result<(), String> {
        for stmt in body {
            self.ensure_open();
            self.stmt(stmt)?;
        }
        Ok(())
    }

    fn store(&mut self, name: &str, value: &Expr, declaring: bool) -> Result<(), String> {
        let (ty, ptr, constant) = match self.slots.get(name) {
            Some(slot) => (slot.ty.clone(), slot.ptr, slot.constant),
            None => return Err(format!("Systemkoerper: '{name}' ist nicht deklariert; Native lokale Variable braucht eine Typannotation (setze {name}: TYP auf ...)")),
        };
        if constant && !declaring {
            return Err(format!("Systemkoerper: Konstante '{name}' kann nicht neu gesetzt werden"));
        }
        if ty.ist_aggregat() {
            // Struktur-/Arraywerte wandern als Speicherkopie, nicht als LLVM-Aggregat.
            let from = self.struct_place(value, &ty, name)?;
            let alignment = self.struct_size_align(&ty)?.1;
            let to = Place { ptr, alignment, origin: Origin::Slot(name.to_string()) };
            return self.copy_struct(&ty, to, from);
        }
        let value = self.expr(value, Some(&ty))?;
        self.expect(&value, &ty, name)?;
        self.builder.build_store(ptr, value.value).map_err(e)?;
        Ok(())
    }

    fn expect(&self, value: &SVal<'ctx>, ty: &STy, what: &str) -> Result<(), String> {
        if &value.ty != ty {
            return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), value.ty.name()));
        }
        Ok(())
    }

    /// ABI-Groesse und -Ausrichtung eines Strukturwerts aus dem Ziel-
    /// Datenlayout (nie aus der dynamischen MooValue-Darstellung).
    fn struct_size_align(&self, ty: &STy) -> Result<(u64, u32), String> {
        let llvm = ty.llvm(self.context, self.layouts)?;
        Ok((self.target.get_abi_size(&llvm), self.target.get_abi_alignment(&llvm)))
    }

    /// Frischer, ABI-ausgerichteter Zwischenspeicher im Eintrittsblock. Nur
    /// dort erkennt LLVM (SROA/MemCpyOpt) ihn als wegoptimierbaren Stack-Slot.
    fn entry_alloca(&self, ty: &STy, name: &str) -> Result<(PointerValue<'ctx>, u32), String> {
        let llvm = ty.llvm(self.context, self.layouts)?;
        let alignment = self.target.get_abi_alignment(&llvm);
        let builder = self.context.create_builder();
        match self.entry.get_terminator() {
            Some(terminator) => builder.position_before(&terminator),
            None => builder.position_at_end(self.entry),
        }
        let ptr = builder.build_alloca(llvm, name).map_err(e)?;
        ptr.as_instruction().ok_or("Systemkoerper: Alloca fehlt")?
            .set_alignment(alignment).map_err(e)?;
        Ok((ptr, alignment))
    }

    /// Strukturwerte bleiben im Speicher (wie C-Structs bei Clang) statt als
    /// LLVM-Aggregat durch insertvalue/extractvalue zu wandern. Liefert fuer
    /// einen Strukturausdruck des Typs `ty` seinen Ort samt bewiesener
    /// Ausrichtung: eigene Allocas sind ABI-ausgerichtet; Felder und Elemente
    /// hinter freien Zeigern bleiben align 1 (gepackt, MMIO, fremder Speicher).
    fn struct_place(&mut self, expr: &Expr, ty: &STy, what: &str) -> Result<Place<'ctx>, String> {
        match expr {
            Expr::Identifier(name) => {
                let slot = self.slots.get(name)
                    .ok_or_else(|| format!("Systemkoerper: Name '{name}' nicht gefunden"))?;
                if &slot.ty != ty {
                    return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), slot.ty.name()));
                }
                let ptr = slot.ptr;
                let alignment = self.struct_size_align(ty)?.1;
                Ok(Place { ptr, alignment, origin: Origin::Slot(name.clone()) })
            }
            Expr::PropertyAccess { object, property } => {
                let alignment = self.local_field_alignment(object, property);
                let (ptr, found) = self.pointer_property(object, property)?;
                if &found != ty {
                    return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), found.name()));
                }
                Ok(Place { ptr, alignment, origin: Origin::Memory })
            }
            Expr::FunctionCall { name, args, .. } if self.layouts.layout(name).is_some() => {
                let built = self.struct_ty(name)?;
                if &built != ty {
                    return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), built.name()));
                }
                let (ptr, alignment) = self.entry_alloca(ty, "sys.struct.neu")?;
                let place = Place { ptr, alignment, origin: Origin::Fresh };
                self.construct_struct_into(name, args, &place)?;
                Ok(place)
            }
            // P2-T48 A4 (Plan a71947f7 E2): Arrayliteral [a, b, ...] fuer ein
            // arraytypisiertes Ziel, direkt in einen frischen Zwischenspeicher.
            Expr::List(items) if matches!(ty, STy::Array { .. }) => {
                let (ptr, alignment) = self.entry_alloca(ty, "sys.array.neu")?;
                let place = Place { ptr, alignment, origin: Origin::Fresh };
                self.array_literal_into(items, ty, &place, what)?;
                Ok(place)
            }
            Expr::IndexAccess { object, index } => {
                let alignment = self.local_array_alignment(object);
                let (ptr, found) = self.pointer_index(object, index)?;
                if &found != ty {
                    return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), found.name()));
                }
                Ok(Place { ptr, alignment, origin: Origin::Memory })
            }
            Expr::FunctionCall { name, args, .. } if self.callees.get(name)
                .is_some_and(|callee| matches!(callee.result, Some(STy::Struct(_)))) =>
            {
                let callee = self.callees.get(name).cloned().ok_or("Systemkoerper: Aufrufziel fehlt")?;
                let (_, sret) = self.call_system_value(name, &callee, args)?;
                let (ptr, found) = sret.ok_or("Systemkoerper: sret-Puffer fehlt")?;
                if &found != ty {
                    return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), found.name()));
                }
                let alignment = self.struct_size_align(ty)?.1;
                Ok(Place { ptr, alignment, origin: Origin::Fresh })
            }
            _ => {
                // P2-T16 E3: vecmat-Rechnungen direkt in einen frischen Ort
                // (SSA-Vektoren, komponentenweise gespeichert; kein insertvalue).
                if let Some(art) = self.vecmat_art(ty) {
                    if let Some((gefunden, werte)) = self.vecmat_rechnung(expr)? {
                        if gefunden != art {
                            return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", art.name(), gefunden.name()));
                        }
                        return self.vecmat_frisch(art, &werte);
                    }
                }
                // Rueckfall: Wert als Aggregat erzeugen und in einen frischen
                // Zwischenspeicher legen.
                let value = self.expr(expr, Some(ty))?;
                self.expect(&value, ty, what)?;
                let (ptr, alignment) = self.entry_alloca(ty, "sys.struct.tmp")?;
                self.builder.build_store(ptr, value.value).map_err(e)?
                    .set_alignment(alignment).map_err(e)?;
                Ok(Place { ptr, alignment, origin: Origin::Fresh })
            }
        }
    }

    /// Wie local_array_alignment, fuer direkte Felder eines lokalen
    /// Strukturwerts: die ABI-Ausrichtung des Slots und der Registry-Offset
    /// beweisen die Feldadresse. Freie ptr[T], gepackte Strukturen
    /// (Ausrichtung 1) und MMIO bleiben align 1.
    fn local_field_alignment(&self, object: &Expr, property: &str) -> u32 {
        let Expr::Identifier(local) = object else { return 1 };
        let Some(slot) = self.slots.get(local) else { return 1 };
        if !matches!(&slot.ty, STy::Struct(_)) { return 1; }
        let Some(name) = self.struct_name_from_type(&slot.ty) else { return 1 };
        let Some(layout) = self.layouts.layout(&name) else { return 1 };
        let Some(index) = layout.fields.iter().position(|(field, _)| field == property) else { return 1 };
        field_alignment(self.target.get_abi_alignment(&layout.llvm_type), layout.offsets[index])
    }

    /// Konstruktor StrukturName(feld1, ...) direkt im Speicher: jedes Feld
    /// wird an seinem Registry-Offset gespeichert (verschachtelte Strukturen
    /// als Speicherkopie), ohne insertvalue-Kette und Aggregat in Registern.
    fn construct_struct_into(
        &mut self, name: &str, args: &[Expr], target: &Place<'ctx>,
    ) -> Result<(), String> {
        let layout = self.layouts.layout(name)
            .ok_or_else(|| format!("Systemkoerper: unbekannte Struktur '{name}'"))?;
        let fields = layout.fields.clone();
        let offsets = layout.offsets.clone();
        // P2-T48 A4: Name() ohne Argumente = Nullwert (wie die 0-Vorbelegung der Slots).
        if args.is_empty() {
            let (size, _) = self.struct_size_align(&self.struct_ty(name)?)?;
            if size > 0 {
                self.builder.build_memset(
                    target.ptr, target.alignment, self.context.i8_type().const_zero(),
                    self.i64t().const_int(size, false),
                ).map_err(e)?;
            }
            return Ok(());
        }
        if fields.len() != args.len() {
            return Err(format!(
                "Systemkoerper: Struktur '{name}' erwartet {} Felder, bekommen {}",
                fields.len(), args.len(),
            ));
        }
        for (index, ((field_name, field_ref), expr)) in fields.iter().zip(args).enumerate() {
            let field_ty = STy::from_type_ref(field_ref, self.nominals).map_err(|_| {
                format!("Systemkoerper: '{name}.{field_name}' hat keinen nativen Werttyp")
            })?;
            let label = format!("{name}.{field_name}");
            let offset = offsets[index];
            let address = unsafe {
                self.builder.build_gep(
                    self.context.i8_type(), target.ptr,
                    &[self.i64t().const_int(offset, false)], "sys.struct.feld",
                )
            }.map_err(e)?;
            let alignment = field_alignment(target.alignment, offset);
            if field_ty.ist_aggregat() {
                let from = self.struct_place(expr, &field_ty, &label)?;
                let to = Place { ptr: address, alignment, origin: target.origin.clone() };
                self.copy_struct(&field_ty, to, from)?;
            } else {
                let value = self.expr(expr, Some(&field_ty))?;
                self.expect(&value, &field_ty, &label)?;
                self.builder.build_store(address, value.value).map_err(e)?
                    .set_alignment(alignment).map_err(e)?;
            }
        }
        Ok(())
    }

    /// Bytegenaue Kopie eines Strukturwerts (inklusive Padding, wie eine
    /// C-Zuweisung). Zwei Orte im allgemeinen Speicher duerfen exakt
    /// ueberlappen (p[0] = q[0] mit p == q, C11 6.5.16.1p3); dann laeuft die
    /// Kopie ueber einen frischen Zwischenspeicher, denn llvm.memcpy verlangt
    /// disjunkte Bereiche und memmove waere im Bare-Profil ein Libcall.
    fn copy_struct(&mut self, ty: &STy, to: Place<'ctx>, from: Place<'ctx>) -> Result<(), String> {
        let (size, _) = self.struct_size_align(ty)?;
        if size == 0 { return Ok(()); }
        let length = self.i64t().const_int(size, false);
        let disjoint = match (&to.origin, &from.origin) {
            (Origin::Fresh, _) | (_, Origin::Fresh) => true,
            (Origin::Slot(a), Origin::Slot(b)) => a != b,
            _ => false,
        };
        if disjoint {
            self.builder.build_memcpy(to.ptr, to.alignment, from.ptr, from.alignment, length).map_err(e)?;
        } else {
            let (tmp, alignment) = self.entry_alloca(ty, "sys.struct.kopie")?;
            self.builder.build_memcpy(tmp, alignment, from.ptr, from.alignment, length).map_err(e)?;
            self.builder.build_memcpy(to.ptr, to.alignment, tmp, alignment, length).map_err(e)?;
        }
        Ok(())
    }

    /// Liefert Offset und statischen Feldtyp, ohne eingebettete Strukturen
    /// vorzeitig zu laden oder in einen dynamischen MooValue zu wandeln.
    fn feld_info(&self, struktur: &str, feld: &str) -> Result<(u64, FieldKind), String> {
        let layout = self.layouts.layout(struktur)
            .ok_or_else(|| format!("Systemkoerper: Struktur '{struktur}' nicht gefunden"))?;
        let position = layout.fields.iter().position(|(name, _)| name == feld)
            .ok_or_else(|| format!("Systemkoerper: Strukturfeld '{struktur}.{feld}' nicht gefunden"))?;
        let field = &layout.fields[position].1;
        let kind = self.native_field_kind(field).map_err(|err| format!(
            "Systemkoerper: Strukturfeld '{struktur}.{feld}': {err}"
        ))?;
        Ok((layout.offsets[position], kind))
    }

    /// Ermittelt die Kind-Hierarchie eines nativen Feldes aus der vorhandenen
    /// Struktur-Registry. Mehrdimensionale Arrays werden rekursiv erfasst.
    fn native_field_kind(&self, field: &TypeRef) -> Result<FieldKind, String> {
        match &field.kind {
            TypeKind::Name(name) if self.layouts.layout(name).is_some() =>
                Ok(FieldKind::Embedded(name.clone())),
            TypeKind::Array { element, .. } => {
                // Die Registry hat die Dimension, das Alignment und die
                // vollständige Array-Groesse bereits gegen TargetData geprüft.
                let array = self.layouts.field_type(self.context, self.target, field)?
                    .into_array_type();
                let length = u64::from(array.len());
                let stride = self.target.get_abi_size(&array.get_element_type());
                if length == 0 || stride == 0 || length.checked_mul(stride).is_none() {
                    return Err("Array hat ungueltige Dimension oder Elementgroesse".into());
                }
                let element = self.native_field_kind(element)?;
                Ok(FieldKind::Array { element: Box::new(element), length, stride })
            }
            _ => Ok(FieldKind::Value(STy::from_type_ref(field, self.nominals)?)),
        }
    }

    /// Historische API fuer explizite strukturfeld_*/registerfeld_*-Aufrufe.
    /// Vollstaendige Strukturwerte bleiben vorerst gesperrt, nur Felder sind
    /// als skalare Werte oder Zeiger les-/schreibbar.
    fn strukturfeld_info(&self, struktur: &str, feld: &str) -> Result<(u64, STy), String> {
        let (offset, kind) = self.feld_info(struktur, feld)?;
        match kind {
            FieldKind::Value(ty) => Ok((offset, ty)),
            FieldKind::Embedded(_) => Err(format!(
                "Systemkoerper: Strukturfeld '{struktur}.{feld}' ist ein eingebetteter Strukturwert"
            )),
            FieldKind::Array { .. } => Err(format!(
                "Systemkoerper: Arrayfeld '{struktur}.{feld}' erfordert einen Index"
            )),
        }
    }

    /// Kanonische Modulidentitaet auch bei lokalen Strukturwerten erhalten.
    fn struct_name_from_type(&self, ty: &STy) -> Option<String> {
        let (module, name) = match ty {
            STy::Ptr(NativeTypeId::Pointer { pointee, .. }) =>
                match pointee.as_ref() {
                    NativeTypeId::Nominal { module, name } => (module, name),
                    _ => return None,
                },
            STy::Struct(NativeTypeId::Nominal { module, name }) => (module, name),
            _ => return None,
        };
        self.nominals.get(name).filter(|(m, n)| m == module && n == name)?;
        Some(name.clone())
    }

    /// Native Strukturwerte teilen ihre Identitaet mit den Pointerfeldern.
    fn struct_ty(&self, name: &str) -> Result<STy, String> {
        let (module, canonical) = self.nominals.get(name)
            .ok_or_else(|| format!("Systemkoerper: unbekannter Strukturtyp '{name}'"))?;
        Ok(STy::Struct(NativeTypeId::Nominal {
            module: module.clone(), name: canonical.clone(),
        }))
    }

    /// Statische Typverfolgung durch eingebettete Strukturen, Zeigerfelder
    /// und Arrays aus Strukturen. Hier wird kein Code erzeugt.
    fn struct_name_from_expr(&self, expr: &Expr) -> Option<String> {
        if let Expr::PropertyAccess { object, property } = expr {
            let owner = self.struct_name_from_expr(object)?;
            let (_, field) = self.feld_info(&owner, property).ok()?;
            return match field {
                FieldKind::Embedded(name) => Some(name),
                FieldKind::Value(ty) => self.struct_name_from_type(&ty),
                FieldKind::Array { .. } => None,
            };
        }
        if let Expr::IndexAccess { object, .. } = expr {
            if let Some(kind) = self.array_kind_from_expr(expr) {
                return match kind {
                    FieldKind::Embedded(name) => Some(name),
                    FieldKind::Value(ty) => self.struct_name_from_type(&ty),
                    FieldKind::Array { .. } => None,
                };
            }
            // P2-T89 C: m[i] ist eine vec4-Spalte, nicht die Matrix selbst.
            if self.vecmat_von(object).is_some() {
                return self.infer(expr).as_ref().and_then(|ty| self.struct_name_from_type(ty));
            }
            if let Some(name) = self.infer(object).as_ref().and_then(|ty| self.struct_name_from_type(ty)) {
                return Some(name);
            }
        }
        self.infer(expr).as_ref().and_then(|ty| self.struct_name_from_type(ty))
    }

    /// Nur Typanalyse: von p.matrix zu p.matrix[i] zu
    /// p.matrix[i][j] ohne IR-/Speicherzugriffe absteigen.
    fn array_kind_from_expr(&self, expr: &Expr) -> Option<FieldKind> {
        match expr {
            Expr::PropertyAccess { object, property } => {
                let owner = self.struct_name_from_expr(object)?;
                self.feld_info(&owner, property).ok().map(|(_, kind)| kind)
            }
            Expr::IndexAccess { object, .. } => {
                match self.array_kind_from_expr(object)? {
                    FieldKind::Array { element, .. } => Some(*element),
                    _ => None,
                }
            }
            _ => None,
        }
    }

    /// Eine hoehere Ausrichtung ist nur bei compiler-eigenen Struktur-Allocas
    /// beweisbar. Ein freier ptr[T], MMIO oder ein gepacktes Feld erhaelt
    /// weiterhin align 1: sein Sprachtyp allein garantiert keine Adresse.
    fn local_array_alignment(&self, object: &Expr) -> u32 {
        // P2-T89 C: Spalte/Bahn eines vecmat-Orts liegt auf einem Vielfachen
        // der Komponentengroesse k; mehr als k nie, weniger, wenn der Ort
        // selbst nichts beweist (freier Zeiger, Zwischenwert: align 1).
        if let Some(art) = self.vecmat_von(object) {
            let k = Self::vecmat_komponente(art);
            let basis = match object {
                Expr::Identifier(name) if self.slots.contains_key(name) => k,
                Expr::PropertyAccess { object: owner, property } => self.local_field_alignment(owner, property),
                Expr::IndexAccess { object: inner, .. } => self.local_array_alignment(inner),
                _ => 1,
            };
            return basis.min(k);
        }
        // P2-T50 B2: Pufferelemente liegen ABI-ausgerichtet (calloc, Start als
        // Vielfaches der Elementgroesse, moo_buffer.c).
        if let Some(STy::Buffer(t)) = self.infer(object) {
            return self.target.get_abi_alignment(&t.llvm_type(self.context));
        }
        // P2-T48 D: Strukturpuffer nur mit Ausrichtung <= MOO_MAX_ALIGN (Compiler).
        if let Some(STy::StrukturPuffer(id)) = self.infer(object) {
            return STy::Struct(id).llvm(self.context, self.layouts)
                .map(|llvm| self.target.get_abi_alignment(&llvm)).unwrap_or(1);
        }
        // P2-T48 A2: lokale Arrays liegen in ABI-ausgerichteten Slots; jedes
        // Element ist so ausgerichtet wie sein Typ (Stride = Vielfaches davon).
        if let Some(STy::Array { element, .. }) = self.infer(object) {
            if self.lokales_array(object) {
                if let Ok(llvm) = element.llvm(self.context, self.layouts) {
                    return self.target.get_abi_alignment(&llvm);
                }
            }
        }
        let Expr::PropertyAccess { object: owner, property } = object else { return 1 };
        let Expr::Identifier(local) = owner.as_ref() else { return 1 };
        let Some(slot) = self.slots.get(local) else { return 1 };
        if !matches!(&slot.ty, STy::Struct(_)) { return 1; }
        let Some(name) = self.struct_name_from_type(&slot.ty) else { return 1 };
        let Some(layout) = self.layouts.layout(&name) else { return 1 };
        let Some(index) = layout.fields.iter().position(|(field, _)| field == property) else { return 1 };
        let field = &layout.fields[index].1;
        if !matches!(&field.kind, TypeKind::Array { .. }) { return 1; }
        let Ok(BasicTypeEnum::ArrayType(array)) =
            self.layouts.field_type(self.context, self.target, field) else { return 1 };
        let element = array.get_element_type();
        let alignment = self.target.get_abi_alignment(&element);
        let offset = layout.offsets[index];
        let stride = self.target.get_abi_size(&element);
        if alignment > 1 && layout.alignment >= alignment
            && offset % u64::from(alignment) == 0
            && stride % u64::from(alignment) == 0 {
            alignment
        } else {
            1
        }
    }

    /// P2-T48 A2: Adresse eines lokalen Arrays bzw. eines Array-Elements eines
    /// lokalen Arrays (2D: a[i] ist selbst ein Array).
    fn array_basis(&mut self, object: &Expr) -> Result<PointerValue<'ctx>, String> {
        match object {
            Expr::Identifier(name) => Ok(self.slots.get(name)
                .ok_or_else(|| format!("Systemkoerper: Name '{name}' nicht gefunden"))?.ptr),
            Expr::IndexAccess { object: inner, index } => Ok(self.pointer_index(inner, index)?.0),
            _ => Err("Systemkoerper: Indexzugriff auf dieses Array wird noch nicht unterstuetzt (lokale Arrays und ihre Elemente)".into()),
        }
    }

    /// P2-T48 A2: Ausdruck bezeichnet ein lokales Array (Slot oder Element davon).
    fn lokales_array(&self, object: &Expr) -> bool {
        match object {
            Expr::Identifier(name) => self.slots.get(name).is_some_and(|slot| matches!(slot.ty, STy::Array { .. })),
            Expr::IndexAccess { object: inner, .. } => self.lokales_array(inner),
            _ => false,
        }
    }

    /// P2-T48 A4: Arrayliteral elementweise in `target` schreiben; die
    /// Elementzahl muss exakt der Laenge entsprechen (keine Teilinitialisierung).
    fn array_literal_into(&mut self, items: &[Expr], ty: &STy, target: &Place<'ctx>, what: &str) -> Result<(), String> {
        let STy::Array { element, length } = ty else {
            return Err(format!("Systemkoerper: {what}: Arrayliteral braucht einen Arraytyp"));
        };
        if items.len() != *length as usize {
            return Err(format!("Systemkoerper: {what} erwartet {length} Elemente, das Arrayliteral hat {}", items.len()));
        }
        let array_llvm = ty.llvm(self.context, self.layouts)?;
        let element_llvm = element.llvm(self.context, self.layouts)?;
        let stride = self.target.get_abi_size(&element_llvm);
        for (i, item) in items.iter().enumerate() {
            let address = unsafe {
                self.builder.build_in_bounds_gep(
                    array_llvm, target.ptr,
                    &[self.i64t().const_zero(), self.i64t().const_int(i as u64, false)], "sys.array.element",
                )
            }.map_err(e)?;
            let alignment = field_alignment(target.alignment, stride * i as u64);
            let label = format!("{what}[{i}]");
            if element.ist_aggregat() {
                let from = self.struct_place(item, element, &label)?;
                self.copy_struct(element, Place { ptr: address, alignment, origin: target.origin.clone() }, from)?;
            } else {
                let value = self.expr(item, Some(element))?;
                self.expect(&value, element, &label)?;
                self.builder.build_store(address, value.value).map_err(e)?
                    .set_alignment(alignment).map_err(e)?;
            }
        }
        Ok(())
    }

    /// P2-T50 B3: `länge(p)` -> u64 und `puffer_zeiger(p)` -> ptr[T] fuer
    /// p: puffer[T] (eigene Funktionen gleichen Namens haben Vorrang, call/infer).
    fn puffer_builtin_typ(&self, name: &str, args: &[Expr]) -> Option<STy> {
        let [arg] = args else { return None };
        let puffer = match self.infer(arg) {
            Some(ty @ (STy::Buffer(_) | STy::StrukturPuffer(_))) => ty,
            _ => return None,
        };
        match name {
            "länge" | "laenge" | "length" | "len" => Some(STy::Int(NType::U64)),
            "puffer_zeiger" | "buffer_pointer" => Some(STy::Ptr(NativeTypeId::Pointer {
                address_space: 0,
                pointee: Box::new(puffer.identity_element()),
            })),
            _ => None,
        }
    }

    /// Indexiert einen typisierten RAM-Zeiger. Anders als volatile_* ist das
    /// ein normaler Zugriff; fuer MMIO ist ausdruecklich volatile_* zu nutzen.
    /// Der Aufrufer im unsicher-Block verantwortet Gueltigkeit und Grenzen.
    fn pointer_index(&mut self, object: &Expr, index: &Expr) -> Result<(PointerValue<'ctx>, STy), String> {
        // P2-T89 C: m[i] (Spalte von mat4/dmat4) und v[i] (Bahn von vecN/dvecN).
        if let Some(art) = self.vecmat_von(object) {
            return self.vecmat_index(object, index, art);
        }
        if let Some((address, field)) = self.array_field_index(object, index)? {
            return match field {
                FieldKind::Value(ty) => Ok((address, ty)),
                FieldKind::Embedded(name) => Ok((address, self.struct_ty(&name)?)),
                FieldKind::Array { .. } => Err("Systemkoerper: verschachtelte Arrays sind noch nicht nutzbar".into()),
            };
        }
        // P2-T50 B2 (Plan bb3878ef E4): Pufferindex ist immer grenzgeprueft
        // (Fehlervertrag trap_block: hosted fangbar, bare/C-Export Trap);
        // ungeprueft nur ausdruecklich ueber puffer_zeiger(p)[i].
        let puffer_element = match self.infer(object) {
            Some(STy::Buffer(t)) => Some((STy::Buffer(t), STy::from_ntype(t)?)),
            Some(STy::StrukturPuffer(id)) => Some((STy::StrukturPuffer(id.clone()), STy::Struct(id))),
            _ => None,
        };
        if let Some((puffer_ty, element)) = puffer_element {
            let puffer = self.expr(object, Some(&puffer_ty))?;
            let aggregat = puffer.value.into_struct_value();
            let daten = self.builder.build_extract_value(aggregat, 0, "sys.puffer.daten").map_err(e)?.into_pointer_value();
            let laenge = self.builder.build_extract_value(aggregat, 1, "sys.puffer.laenge").map_err(e)?.into_int_value();
            let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
            self.expect(&position, &STy::Int(NType::U64), "Pufferindex")?;
            let position = position.value.into_int_value();
            let ausserhalb = self.builder.build_int_compare(
                inkwell::IntPredicate::UGE, position, laenge, "sys.puffer.ausserhalb",
            ).map_err(e)?;
            let fehler = self.trap_block()?;
            let gueltig = self.context.append_basic_block(self.function, "sys.puffer.gueltig");
            self.builder.build_conditional_branch(ausserhalb, fehler, gueltig).map_err(e)?;
            self.builder.position_at_end(gueltig);
            let ptr = unsafe {
                self.builder.build_gep(element.llvm(self.context, self.layouts)?, daten, &[position], "sys.puffer.index")
            }.map_err(e)?;
            return Ok((ptr, element));
        }
        // P2-T48 A2 (Plan a71947f7): lokale feste Arrays (auch [[T; N]; M]) im
        // Slot; Index grenzgeprueft (trap_block), Stride aus dem LLVM-Arraytyp.
        if let Some(array_ty @ STy::Array { .. }) = self.infer(object) {
            let STy::Array { element, length } = &array_ty else { return Err("Systemkoerper: Arraytyp fehlt".into()) };
            if let Some(anzeige) = negatives_index_literal(index) {
                return Err(self.index_ausserhalb(&anzeige, u64::from(*length)));
            }
            let basis = self.array_basis(object)?;
            let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
            self.expect(&position, &STy::Int(NType::U64), "Arrayindex")?;
            let position = position.value.into_int_value();
            self.konstanter_index(position, u64::from(*length))?;
            let ausserhalb = self.builder.build_int_compare(
                inkwell::IntPredicate::UGE, position, self.i64t().const_int(u64::from(*length), false), "sys.array.ausserhalb",
            ).map_err(e)?;
            let fehler = self.trap_block()?;
            let gueltig = self.context.append_basic_block(self.function, "sys.array.gueltig");
            self.builder.build_conditional_branch(ausserhalb, fehler, gueltig).map_err(e)?;
            self.builder.position_at_end(gueltig);
            let array_llvm = array_ty.llvm(self.context, self.layouts)?;
            let ptr = unsafe {
                self.builder.build_in_bounds_gep(
                    array_llvm, basis, &[self.i64t().const_zero(), position], "sys.array.index",
                )
            }.map_err(e)?;
            return Ok((ptr, (**element).clone()));
        }
        let ptr_ty = self.infer(object)
            .ok_or("Systemkoerper: Indexzugriff erfordert einen typisierten ptr[T]")?;
        let element = match &ptr_ty {
            STy::Ptr(NativeTypeId::Pointer { pointee, .. }) =>
                STy::from_identity(pointee)?
                    .ok_or("Systemkoerper: ptr[void] kann nicht indiziert werden")?,
            _ => return Err("Systemkoerper: Indexzugriff erfordert einen typisierten ptr[T]".into()),
        };
        self.verlange_unsicher("Zeigerindex p[i] auf ptr[T]")?;
        let base = self.expr(object, Some(&ptr_ty))?;
        let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
        self.expect(&position, &STy::Int(NType::U64), "Zeigerindex")?;
        let ptr = unsafe {
            self.builder.build_gep(
                element.llvm(self.context, self.layouts)?, base.value.into_pointer_value(),
                &[position.value.into_int_value()], "sys.pointer.index",
            )
        }.map_err(e)?;
        Ok((ptr, element))
    }

    /// P2-T89 C (Plan 0a44c07a, O4): Spaltenindex m[i] auf mat4/dmat4
    /// (Ergebnis vec4/dvec4) und Bahnenindex v[i] auf vecN/dvecN (Ergebnis
    /// f32/f64), lesend und als Zuweisungsziel. Ein konstanter Index ausserhalb
    /// ist ein Compilefehler (M-M001), sonst prueft die Laufzeit wie bei festen
    /// Arrays (u64, UGE -> trap_block). Adresse ueber Bytes wie vecmat_adresse
    /// (Spalte 4*k, Bahn k; k = 4 bzw. 8 Byte).
    fn vecmat_index(
        &mut self, object: &Expr, index: &Expr, art: VecMatArt,
    ) -> Result<(PointerValue<'ctx>, STy), String> {
        let was = if art.ist_matrix() { "Spaltenindex" } else { "Komponentenindex" };
        let ty = self.vecmat_ty(art)?;
        let place = self.struct_place(object, &ty, was)?;
        let k = u64::from(Self::vecmat_komponente(art));
        let (anzahl, schritt, element) = if art.ist_matrix() {
            (4u64, 4 * k, self.vecmat_ty(art.vektor(4))?)
        } else {
            (u64::from(art.bahnen()), k, STy::Float(art.element()))
        };
        // Review c7eddc37: m[-1]/v[-1] ist ein konstanter Index ausserhalb
        // (M-M001 mit Zeile), nicht erst ein u64-Literalfehler (M-C000).
        if let Some(anzeige) = negatives_index_literal(index) {
            return Err(crate::diagnose::vormerken("M-M001", self.zeile, format!(
                "Systemkoerper: konstanter {was} {anzeige} liegt ausserhalb von {} (gueltig 0..{}); der Zugriff wuerde immer fehlschlagen",
                art.name(), anzahl - 1)));
        }
        let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
        self.expect(&position, &STy::Int(NType::U64), was)?;
        let raw = position.value.into_int_value();
        if let Some(konstant) = raw.get_zero_extended_constant() {
            if konstant >= anzahl {
                let anzeige = if (konstant as i64) < 0 { (konstant as i64).to_string() } else { konstant.to_string() };
                return Err(crate::diagnose::vormerken("M-M001", self.zeile, format!(
                    "Systemkoerper: konstanter {was} {anzeige} liegt ausserhalb von {} (gueltig 0..{}); der Zugriff wuerde immer fehlschlagen",
                    art.name(), anzahl - 1)));
            }
        }
        let ausserhalb = self.builder.build_int_compare(
            IntPredicate::UGE, raw, self.i64t().const_int(anzahl, false), "vm.index.ausserhalb",
        ).map_err(e)?;
        let fehler = self.trap_block()?;
        let gueltig = self.context.append_basic_block(self.function, "vm.index.gueltig");
        self.builder.build_conditional_branch(ausserhalb, fehler, gueltig).map_err(e)?;
        self.builder.position_at_end(gueltig);
        let offset = self.builder.build_int_mul(
            raw, self.i64t().const_int(schritt, false), "vm.index.offset",
        ).map_err(e)?;
        let address = unsafe {
            self.builder.build_gep(self.context.i8_type(), place.ptr, &[offset], "vm.index")
        }.map_err(e)?;
        Ok((address, element))
    }

    /// Feldarray an einer typisierten Struktur: Grenzen werden vor dem
    /// Adressieren ueberprueft, Elementgroesse aus Ziel-ABI statt MooValue.
    /// Keine inbounds-Behauptung fuer vom Aufrufer gelieferte Pointer.
    fn array_field_index(
        &mut self, object: &Expr, index: &Expr,
    ) -> Result<Option<(PointerValue<'ctx>, FieldKind)>, String> {
        let (base, field) = match object {
            Expr::PropertyAccess { object: owner, property } => {
                let Some(name) = self.struct_name_from_expr(owner) else { return Ok(None) };
                if !matches!(self.feld_info(&name, property)?.1, FieldKind::Array { .. }) {
                    return Ok(None);
                }
                self.field_address(owner, property)?
            }
            Expr::IndexAccess { object: parent, index: parent_index } => {
                let Some(inner) = self.array_field_index(parent, parent_index)? else {
                    return Ok(None);
                };
                inner
            }
            _ => return Ok(None),
        };
        let FieldKind::Array { element, length, stride } = field else { return Ok(None) };
        if let Some(anzeige) = negatives_index_literal(index) {
            return Err(self.index_ausserhalb(&anzeige, length));
        }
        let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
        self.expect(&position, &STy::Int(NType::U64), "Arrayindex")?;
        let raw = position.value.into_int_value();
        self.konstanter_index(raw, length)?;
        let outside = self.builder.build_int_compare(
            IntPredicate::UGE, raw, self.i64t().const_int(length, false), "sys.array.bounds",
        ).map_err(e)?;
        let trap = self.trap_block()?;
        let valid = self.context.append_basic_block(self.function, "sys.array.ok");
        self.builder.build_conditional_branch(outside, trap, valid).map_err(e)?;
        self.builder.position_at_end(valid);
        let offset = self.builder.build_int_mul(
            raw, self.i64t().const_int(stride, false), "sys.array.stride",
        ).map_err(e)?;
        let address = unsafe {
            self.builder.build_gep(
                self.context.i8_type(), base, &[offset], "sys.array.element",
            )
        }.map_err(e)?;
        Ok(Some((address, *element)))
    }

    /// Ermittelt die echte Basisadresse einer Struktur. p[1] nutzt die
    /// Ziel-ABI-Groesse als Array-Stride statt MooValue- oder Pointergroesse.
    /// Eingebettete Strukturfelder bleiben Adressen, Zeigerfelder werden geladen.
    fn struct_pointer(&mut self, object: &Expr) -> Result<(PointerValue<'ctx>, String), String> {
        if let Expr::PropertyAccess { object: parent, property } = object {
            let (address, kind) = self.field_address(parent, property)?;
            return match kind {
                FieldKind::Embedded(name) => Ok((address, name)),
                FieldKind::Array { .. } => Err(
                    "Systemkoerper: Arrayfeld erfordert einen Index".into()
                ),
                FieldKind::Value(ty) => {
                    self.verlange_unsicher("Feldzugriff ueber ein Zeigerfeld")?;
                    let name = self.struct_name_from_type(&ty)
                        .ok_or("Systemkoerper: Feldzugriff erfordert ptr[Struktur]")?;
                    let value = self.builder.build_load(
                        ty.llvm(self.context, self.layouts)?, address, "sys.feld.zeiger",
                    ).map_err(e)?;
                    value.as_instruction_value().ok_or("Systemkoerper: Zeigerfeld-Load fehlt")?
                        .set_alignment(1).map_err(e)?;
                    Ok((value.into_pointer_value(), name))
                }
            };
        }
        if let Expr::IndexAccess { object: base, index } = object {
            // P2-T89 C: Spalte m[i] einer mat4/dmat4 als vec4-Basis (m[i].x).
            if self.vecmat_von(base).is_some() {
                let (address, ty) = self.pointer_index(base, index)?;
                let name = self.struct_name_from_type(&ty).ok_or(
                    "Systemkoerper: Feldzugriff auf v[i] (eine Komponente ist kein Vektor)")?;
                return Ok((address, name));
            }
            // P2-T48 D: p[i] auf puffer[Struktur] (grenzgeprueft) als Strukturbasis.
            if matches!(self.infer(base), Some(STy::StrukturPuffer(_))) {
                let (address, ty) = self.pointer_index(base, index)?;
                let name = self.struct_name_from_type(&ty)
                    .ok_or("Systemkoerper: Pufferelement ist keine Struktur")?;
                return Ok((address, name));
            }
            if let Some((address, kind)) = self.array_field_index(base, index)? {
                return match kind {
                    FieldKind::Embedded(name) => Ok((address, name)),
                    FieldKind::Value(ty) => {
                        self.verlange_unsicher("Feldzugriff ueber ein Zeiger-Arrayelement")?;
                        let name = self.struct_name_from_type(&ty)
                            .ok_or("Systemkoerper: Arrayelement erfordert ptr[Struktur]")?;
                        let value = self.builder.build_load(
                            ty.llvm(self.context, self.layouts)?, address, "sys.array.ptr",
                        ).map_err(e)?;
                        value.as_instruction_value().ok_or("Systemkoerper: Array-Zeiger-Load fehlt")?
                            .set_alignment(1).map_err(e)?;
                        Ok((value.into_pointer_value(), name))
                    }
                    FieldKind::Array { .. } => Err(
                        "Systemkoerper: verschachtelte Arrays sind noch nicht nutzbar".into()
                    ),
                };
            }
            if let Some(ty) = self.infer(base) {
                if let Some(name) = self.struct_name_from_type(&ty) {
                    self.verlange_unsicher("Strukturindex auf ptr[Struktur]")?;
                    let base_value = self.expr(base, Some(&ty))?;
                    let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
                    self.expect(&position, &STy::Int(NType::U64), "Strukturindex")?;
                    let stride = self.layouts.layout(&name)
                        .ok_or("Systemkoerper: Struktur-Layout fehlt")?.size;
                    if stride == 0 { return Err("Systemkoerper: Struktur mit Groesse 0".into()); }
                    let raw_index = position.value.into_int_value();
                    if stride > 1 {
                        let limit = self.i64t().const_int(u64::MAX / stride, false);
                        let overflow = self.builder.build_int_compare(
                            IntPredicate::UGT, raw_index, limit, "sys.struct.index.overflow",
                        ).map_err(e)?;
                        let failure = self.trap_block()?;
                        let good = self.context.append_basic_block(self.function, "sys.struct.index.ok");
                        self.builder.build_conditional_branch(overflow, failure, good).map_err(e)?;
                        self.builder.position_at_end(good);
                    }
                    let byte_index = self.builder.build_int_mul(
                        raw_index, self.i64t().const_int(stride, false), "sys.struct.stride",
                    ).map_err(e)?;
                    let address = unsafe {
                        self.builder.build_gep(
                            self.context.i8_type(), base_value.value.into_pointer_value(),
                            &[byte_index], "sys.struct.element",
                        )
                    }.map_err(e)?;
                    return Ok((address, name));
                }
            }
        }
        // P2-T48 A2: Element eines lokalen Struktur-Arrays (ps[i].feld) liegt im
        // Slot des Arrays; Adresse grenzgeprueft ueber pointer_index.
        if let Expr::IndexAccess { object: inner, index } = object {
            if self.lokales_array(inner) {
                let (ptr, ty) = self.pointer_index(inner, index)?;
                let name = self.struct_name_from_type(&ty)
                    .ok_or("Systemkoerper: Feldzugriff erfordert ein Strukturelement")?;
                return Ok((ptr, name));
            }
        }
        // Eine lokale Struktur ist bereits im Entry-Slot gespeichert.
        if let Expr::Identifier(local) = object {
            if let Some(slot) = self.slots.get(local) {
                if matches!(slot.ty, STy::Struct(_)) {
                    let name = self.struct_name_from_type(&slot.ty)
                        .ok_or("Systemkoerper: unbekannter nativer Strukturwert")?;
                    return Ok((slot.ptr, name));
                }
            }
        }
        // Review c7eddc37: Strukturwert als Zwischenwert (vec4(...).y, (a + b).x,
        // f().feld) ist kein Zeiger: in einen frischen Ort legen, Feld daraus.
        if let Some(ty @ STy::Struct(_)) = self.infer(object) {
            let name = self.struct_name_from_type(&ty)
                .ok_or("Systemkoerper: unbekannter nativer Strukturwert")?;
            let place = self.struct_place(object, &ty, "Feldzugriff")?;
            return Ok((place.ptr, name));
        }
        self.verlange_unsicher("Feldzugriff ueber ptr[Struktur]")?;
        let ty = self.infer(object)
            .ok_or("Systemkoerper: Feldzugriff erfordert ptr[Struktur]")?;
        let name = self.struct_name_from_type(&ty)
            .ok_or("Systemkoerper: Feldzugriff erfordert ptr[Struktur]")?;
        let value = self.expr(object, Some(&ty))?;
        self.expect(&value, &ty, "Struktur-Zeiger")?;
        Ok((value.value.into_pointer_value(), name))
    }

    /// Typisierte Feldadresse ohne Kopie. Direkte Felder und verschachtelte
    /// Felder verwenden dieselben Registry-Offsets (auch bei @gepackt).
    fn field_address(&mut self, object: &Expr, property: &str) -> Result<(PointerValue<'ctx>, FieldKind), String> {
        let (base, name) = self.struct_pointer(object)?;
        let (offset, kind) = self.feld_info(&name, property)?;
        let address = unsafe {
            self.builder.build_gep(
                self.context.i8_type(), base,
                &[self.i64t().const_int(offset, false)], "sys.property.offset",
            )
        }.map_err(e)?;
        Ok((address, kind))
    }

    /// p.feld ist ein normaler RAM-Zugriff; MMIO erfordert weiterhin die
    /// explizite volatile-Registerfeld-API. Aggregatwerte bleiben gesperrt.
    fn pointer_property(&mut self, object: &Expr, property: &str) -> Result<(PointerValue<'ctx>, STy), String> {
        let (address, kind) = self.field_address(object, property)?;
        match kind {
            FieldKind::Value(ty) => Ok((address, ty)),
            FieldKind::Embedded(name) => Ok((address, self.struct_ty(&name)?)),
            FieldKind::Array { .. } => Err(
                "Systemkoerper: Arrayfeld erfordert einen Index".into()
            ),
        }
    }

    /// P2-T12 A: im sicheren Koerper nur innerhalb von unsicher: erlaubt.
    fn verlange_unsicher(&self, was: &str) -> Result<(), String> {
        if self.sicher && self.unsicher_tiefe == 0 {
            // P2-T98 S3: M-M005 mit Zeile und Vorschlag.
            return Err(crate::diagnose::vormerken("M-M005", self.zeile, format!(
                "Systemkoerper: {was} ist nur innerhalb von unsicher: erlaubt (sicherer @nativ-Koerper); Vorschlag: die Zeile in einen unsicher:-Block einschliessen")));
        }
        Ok(())
    }

    /// P2-T98 S1 (M-M001): ein schon beim Uebersetzen feststehender Index
    /// ausserhalb eines festen Arrays [T; N] ist ein Compilefehler; nicht
    /// konstante Indizes prueft weiter die Laufzeit (trap_block).
    fn konstanter_index(&self, position: inkwell::values::IntValue<'ctx>, length: u64) -> Result<(), String> {
        let Some(k) = position.get_zero_extended_constant() else { return Ok(()) };
        if k < length {
            return Ok(());
        }
        let anzeige = if (k as i64) < 0 { (k as i64).to_string() } else { k.to_string() };
        Err(self.index_ausserhalb(&anzeige, length))
    }

    /// M-M001-Text fuer [T; N]: konstanter_index und negative Literale.
    fn index_ausserhalb(&self, anzeige: &str, length: u64) -> String {
        let gueltig = if length == 0 { "das Array ist leer".to_string() } else { format!("gueltig 0..{}", length - 1) };
        crate::diagnose::vormerken("M-M001", self.zeile, format!(
            "Systemkoerper: konstanter Index {anzeige} liegt ausserhalb von [T; {length}] ({gueltig}); der Zugriff wuerde immer fehlschlagen"))
    }

    fn stmt(&mut self, stmt: &Stmt) -> Result<(), String> {
        if stmt.zeile() > 0 {
            self.zeile = stmt.zeile();
        }
        match stmt {
            Stmt::UnsafeBlock { body, .. } => {
                self.unsicher_tiefe += 1;
                let result = self.block(body);
                self.unsicher_tiefe -= 1;
                result
            }
            Stmt::Assignment { name, ty, value, .. } => self.store(name, value, ty.is_some()),
            Stmt::ConstAssignment { name, ty: Some(_), value, .. } => self.store(name, value, true),
            Stmt::PropertyAssignment { object, property, value, .. } => {
                let alignment = self.local_field_alignment(object, property);
                let (ptr, ty) = self.pointer_property(object, property)?;
                if ty.ist_aggregat() {
                    let from = self.struct_place(value, &ty, property)?;
                    return self.copy_struct(&ty, Place { ptr, alignment, origin: Origin::Memory }, from);
                }
                let v = self.expr(value, Some(&ty))?;
                self.expect(&v, &ty, property)?;
                let store = self.builder.build_store(ptr, v.value).map_err(e)?;
                // Freie Zeiger bleiben align 1; lokale Strukturfelder tragen
                // die aus Slot-Ausrichtung und Offset bewiesene Ausrichtung.
                store.set_alignment(alignment).map_err(e)?;
                Ok(())
            }
            Stmt::IndexAssignment { object, index, value, .. } => {
                let alignment = self.local_array_alignment(object);
                let (ptr, ty) = self.pointer_index(object, index)?;
                if ty.ist_aggregat() {
                    let from = self.struct_place(value, &ty, "Zeiger-Element")?;
                    return self.copy_struct(&ty, Place { ptr, alignment, origin: Origin::Memory }, from);
                }
                let v = self.expr(value, Some(&ty))?;
                self.expect(&v, &ty, "Zeiger-Element")?;
                let store = self.builder.build_store(ptr, v.value).map_err(e)?;
                // Freie ptr[T] bleiben align 1; lokale Struct-Arrays haben
                // nur die aus ABI-Layout und Alloca bewiesene Ausrichtung.
                store.set_alignment(alignment).map_err(e)?;
                Ok(())
            }
            Stmt::If { condition, body, else_body, .. } => {
                let cond = self.condition(condition)?;
                let then_bb = self.context.append_basic_block(self.function, "sys.dann");
                let else_bb = self.context.append_basic_block(self.function, "sys.sonst");
                let merge = self.context.append_basic_block(self.function, "sys.weiter");
                self.builder.build_conditional_branch(cond, then_bb, else_bb).map_err(e)?;
                self.builder.position_at_end(then_bb);
                self.block(body)?;
                crate::native_control_flow::branch_if_open(&self.builder, merge)?;
                self.builder.position_at_end(else_bb);
                self.block(else_body)?;
                crate::native_control_flow::branch_if_open(&self.builder, merge)?;
                self.builder.position_at_end(merge);
                Ok(())
            }
            Stmt::While { condition, body, .. } => {
                let blocks = crate::native_control_flow::LoopBlocks::new(self.context, self.function);
                self.builder.build_unconditional_branch(blocks.condition).map_err(e)?;
                self.builder.position_at_end(blocks.condition);
                let cond = self.condition(condition)?;
                self.builder.build_conditional_branch(cond, blocks.body, blocks.exit).map_err(e)?;
                self.builder.position_at_end(blocks.body);
                self.loops.push((blocks.condition, blocks.exit));
                let result = self.block(body);
                self.loops.pop();
                result?;
                crate::native_control_flow::branch_if_open(&self.builder, blocks.condition)?;
                self.builder.position_at_end(blocks.exit);
                Ok(())
            }
            // P2-T12 B (Plan 31194239 E3): für i in a..b – exklusiver Bereich mit
            // nativem Integer-Zaehler; Ende einmal vor der Schleife ausgewertet;
            // der Zaehler ist im Koerper konstant und nur dort sichtbar; a >= b
            // laeuft null Mal. weiter springt in den Schritt-Block (Zaehler + 1,
            // kein Ueberlauf moeglich, da Zaehler < Ende).
            Stmt::For { var_name, iterable, body, .. } => {
                let Expr::Range { start, end } = iterable else {
                    return Err("Systemkoerper: für erwartet einen Bereich a..b; dynamische Listen/Texte sind in @nativ nicht iterierbar (puffer[T] mit Index und solange verwenden)".into());
                };
                if self.slots.contains_key(var_name) {
                    return Err(format!("Systemkoerper: Schleifenzaehler '{var_name}' ist bereits deklariert"));
                }
                let ty = self.infer(start).or_else(|| self.infer(end)).unwrap_or(STy::Int(NType::I64));
                let STy::Int(zahl) = ty else {
                    return Err(format!("Systemkoerper: für-Bereich braucht Ganzzahlgrenzen, nicht {}", ty.name()));
                };
                let anfang = self.expr(start, Some(&ty))?;
                self.expect(&anfang, &ty, "für-Anfang")?;
                let ende = self.expr(end, Some(&ty))?;
                self.expect(&ende, &ty, "für-Ende")?;
                let (zaehler, _) = self.entry_alloca(&ty, var_name)?;
                let (grenze, _) = self.entry_alloca(&ty, "sys.fuer.ende")?;
                self.builder.build_store(zaehler, anfang.value).map_err(e)?;
                self.builder.build_store(grenze, ende.value).map_err(e)?;
                let llvm = ty.llvm(self.context, self.layouts)?;
                let blocks = crate::native_control_flow::LoopBlocks::new(self.context, self.function);
                let schritt = self.context.append_basic_block(self.function, "native.loop.step");
                self.builder.build_unconditional_branch(blocks.condition).map_err(e)?;
                self.builder.position_at_end(blocks.condition);
                let aktuell = self.builder.build_load(llvm, zaehler, "sys.fuer.i").map_err(e)?;
                let bis = self.builder.build_load(llvm, grenze, "sys.fuer.bis").map_err(e)?;
                let test = crate::native_control_flow::range_condition(self.context, &self.builder,
                    NValue { ty: zahl, value: aktuell }, NValue { ty: zahl, value: bis })?;
                self.builder.build_conditional_branch(test.value.into_int_value(), blocks.body, blocks.exit).map_err(e)?;
                self.builder.position_at_end(blocks.body);
                self.slots.insert(var_name.clone(), Slot { ty: ty.clone(), ptr: zaehler, constant: true });
                self.loops.push((schritt, blocks.exit));
                let result = self.block(body);
                self.loops.pop();
                self.slots.remove(var_name);
                result?;
                crate::native_control_flow::branch_if_open(&self.builder, schritt)?;
                self.builder.position_at_end(schritt);
                let i = self.builder.build_load(llvm, zaehler, "sys.fuer.i").map_err(e)?.into_int_value();
                let naechster = self.builder.build_int_add(i, i.get_type().const_int(1, false), "sys.fuer.naechster").map_err(e)?;
                self.builder.build_store(zaehler, naechster).map_err(e)?;
                self.builder.build_unconditional_branch(blocks.condition).map_err(e)?;
                self.builder.position_at_end(blocks.exit);
                Ok(())
            }
            Stmt::Break(_) | Stmt::Continue(_) => {
                let (condition, exit) = *self.loops.last()
                    .ok_or("Systemkoerper: stopp/weiter ausserhalb einer Schleife")?;
                let target = if matches!(stmt, Stmt::Break(_)) { exit } else { condition };
                self.builder.build_unconditional_branch(target).map_err(e)?;
                Ok(())
            }
            Stmt::Return(value, _) => {
                match (self.result.clone(), value) {
                    (None, None) => { self.builder.build_return(None).map_err(e)?; }
                    (Some(ty @ STy::Struct(_)), Some(expr)) => {
                        // Der sret-Puffer ist ein frischer, exklusiver Aufruferpuffer.
                        let sret = self.sret.ok_or("Systemkoerper: sret-Puffer fehlt")?;
                        let from = self.struct_place(expr, &ty, "gib_zurück")?;
                        let alignment = self.struct_size_align(&ty)?.1;
                        self.copy_struct(&ty, Place { ptr: sret, alignment, origin: Origin::Fresh }, from)?;
                        self.builder.build_return(None).map_err(e)?;
                    }
                    (Some(ty), Some(expr)) => {
                        let value = self.expr(expr, Some(&ty))?;
                        self.expect(&value, &ty, "gib_zurück")?;
                        self.builder.build_return(Some(&value.value)).map_err(e)?;
                    }
                    (None, Some(_)) => return Err("Systemkoerper: void-Funktion gibt einen Wert zurück".into()),
                    (Some(_), None) => return Err("Systemkoerper: gib_zurück ohne Wert in Funktion mit Rückgabetyp".into()),
                }
                Ok(())
            }
            Stmt::Expression(Expr::FunctionCall { name, args, .. }, _) => {
                self.call(name, args, None, true)?;
                Ok(())
            }
            _ => Err("Systemkoerper: Anweisung wird nicht unterstützt".into()),
        }
    }

    fn condition(&mut self, expr: &Expr) -> Result<IntValue<'ctx>, String> {
        let value = self.expr(expr, Some(&STy::Bool))?;
        self.expect(&value, &STy::Bool, "Bedingung")?;
        Ok(value.value.into_int_value())
    }

    /// P2-T9 (Plan 4500717b A4, ADR §2): Standardtyp eines Zahlenliterals
    /// ohne Kontext (Ganzzahl i64, Float f64), auch negiert. Nur als LETZTER
    /// Rueckfall nach infer(): ein typisierter Nachbar behaelt Vorrang.
    fn literal_standard(expr: &Expr) -> Option<STy> {
        let text = match expr {
            Expr::NativeNumber { text, .. } => text,
            Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
                Expr::NativeNumber { text, .. } => text,
                _ => return None,
            },
            _ => return None,
        };
        STy::from_ntype(crate::codegen_native::literal_default_type(text)).ok()
    }

    /// P2-T9 (Review 9b847d8e): Typ eines Operandenpaars ohne typisierten
    /// Nachbarn, reihenfolgeunabhaengig und gleich zum Pure-Pfad
    /// (codegen_native::literal_paar_typ): zwei Literale -> f64, sobald eines ein
    /// Float-Literal ist; sonst der Standardtyp des einzigen Literals.
    fn literal_paar(left: &Expr, right: &Expr) -> Option<STy> {
        let standard = |e: &Expr| match e {
            Expr::NativeNumber { text, .. } => Some(crate::codegen_native::literal_default_type(text)),
            Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => match operand.as_ref() {
                Expr::NativeNumber { text, .. } => Some(crate::codegen_native::literal_default_type(text)),
                _ => None,
            },
            _ => None,
        };
        let ty = match (standard(left), standard(right)) {
            (Some(l), Some(r)) => crate::codegen_native::literal_paar_typ(l, r),
            (l, r) => l.or(r)?,
        };
        STy::from_ntype(ty).ok()
    }

    /// Statischer Typ eines Ausdrucks ohne Kontext (Literale: None).
    fn infer(&self, expr: &Expr) -> Option<STy> {
        match expr {
            Expr::Identifier(name) => self.slots.get(name).map(|slot| slot.ty.clone()),
            Expr::PropertyAccess { object, property } => {
                let name = self.struct_name_from_expr(object)?;
                match self.feld_info(&name, property).ok()?.1 {
                    FieldKind::Value(ty) => Some(ty),
                    FieldKind::Embedded(name) => self.struct_ty(&name).ok(),
                    FieldKind::Array { .. } => None,
                }
            },
            Expr::IndexAccess { object, .. } => {
                if let Some(field) = self.array_kind_from_expr(expr) {
                    return match field {
                        FieldKind::Value(ty) => Some(ty),
                        FieldKind::Embedded(name) => self.struct_ty(&name).ok(),
                        FieldKind::Array { .. } => None,
                    };
                }
                // P2-T89 C: m[i] -> vec4/dvec4, v[i] -> f32/f64.
                if let Some(art) = self.vecmat_von(object) {
                    return if art.ist_matrix() {
                        self.vecmat_ty(art.vektor(4)).ok()
                    } else {
                        Some(STy::Float(art.element()))
                    };
                }
                match self.infer(object) {
                    Some(STy::Ptr(NativeTypeId::Pointer { pointee, .. })) =>
                        STy::from_identity(&pointee).ok().flatten(),
                    Some(STy::Buffer(t)) => STy::from_ntype(t).ok(),
                    Some(STy::StrukturPuffer(id)) => Some(STy::Struct(id)),
                    Some(STy::Array { element, .. }) => Some(*element),
                    _ => None,
                }
            },
            Expr::Boolean(_) => Some(STy::Bool),
            Expr::UnaryOp { op: UnaryOpKind::Not, .. } => Some(STy::Bool),
            Expr::UnaryOp { operand, .. } => self.infer(operand),
            Expr::BinaryOp { op, left, right } => match op {
                BinOp::Eq | BinOp::NotEq | BinOp::Less | BinOp::Greater | BinOp::LessEq
                | BinOp::GreaterEq | BinOp::And | BinOp::Or => Some(STy::Bool),
                BinOp::LShift | BinOp::RShift => self.infer(left),
                _ => {
                    // P2-T16 E4: mat4 * vec4 = vec4; sonst bestimmt der
                    // vecmat-Operand den Typ (auch f32 * vecN).
                    let (l, r) = (self.infer(left), self.infer(right));
                    // P2-T17 E3: p + n / n + p / p - n -> Zeiger, p - q -> i64.
                    if matches!(op, BinOp::Add | BinOp::Sub) {
                        match (&l, &r) {
                            (Some(STy::Ptr(_)), Some(STy::Ptr(_))) if matches!(op, BinOp::Sub) =>
                                return Some(STy::Int(NType::I64)),
                            (Some(STy::Ptr(_)), _) => return l,
                            (_, Some(STy::Ptr(_))) => return r,
                            _ => {}
                        }
                    }
                    let la = l.as_ref().and_then(|t| self.vecmat_art(t));
                    let ra = r.as_ref().and_then(|t| self.vecmat_art(t));
                    match (la, ra) {
                        (Some(m), Some(v)) if m.ist_matrix() && v == m.vektor(4) => self.vecmat_ty(v).ok(),
                        (Some(_), _) => l,
                        (None, Some(_)) => r,
                        (None, None) => l.or(r),
                    }
                }
            },
            Expr::FunctionCall { name, args, .. } => {
                if self.layouts.layout(name).is_some() { return self.struct_ty(name).ok(); }
                // P2-T34: teilbar/verschiebbar liefern bool (ADR §13).
                if crate::native_arithmetic::pruef_art(name).is_some() { return Some(STy::Bool); }
                // P2-T34: als(T, x) / als_wrap / als_saettigend liefern T.
                if crate::native_arithmetic::cast_art(name).is_some() {
                    return match args.first() {
                        Some(Expr::Identifier(ty)) => NType::from_name(ty).and_then(|ty| STy::from_ntype(ty).ok()),
                        _ => None,
                    };
                }
                if let Some(ty) = conversion(name) { return Some(STy::Int(ty)); }
                if matches!(name.as_str(), "strukturfeld_lesen" | "struct_field_read"
                    | "registerfeld_lesen" | "register_field_read")
                {
                    return match args.as_slice() {
                        [Expr::Identifier(struktur), Expr::Identifier(feld), _] =>
                            self.strukturfeld_info(struktur, feld).ok().map(|(_, ty)| ty),
                        _ => None,
                    };
                }
                match name.as_str() {
                    "adresse_von" | "address_of" => return Some(STy::Int(NType::U64)),
                    "speicher_lesen" | "mem_read" => return args.get(1).and_then(|w| width_type(w).ok()).map(STy::Int),
                    "volatile_lesen" | "volatile_read" => return args.first().and_then(|p| match self.infer(p) {
                        Some(STy::Ptr(NativeTypeId::Pointer { pointee, .. })) => STy::from_identity(&pointee).ok().flatten(),
                        _ => None,
                    }),
                    _ => {}
                }
                if let Some((true, bits)) = port_bits(name) { return Some(STy::Int(port_type(bits))); }
                if let Some(callee) = self.callees.get(name) { return callee.result.clone(); }
                if self.trig_funktion(name, args).is_some() {
                    return match args.first().and_then(|a| self.infer(a)) {
                        Some(STy::Float(t)) => Some(STy::Float(t)),
                        _ => None,
                    };
                }
                if let Some((funktion, art)) = self.vecmat_funktion(name, args) {
                    return match funktion {
                        VmFunktion::Punkt | VmFunktion::Betrag => Some(STy::Float(art.element())),
                        VmFunktion::Kreuz | VmFunktion::Normalisiere => self.vecmat_ty(art).ok(),
                    };
                }
                if let Some(ty) = self.puffer_builtin_typ(name, args) { return Some(ty); }
                if let Some(Slot { ty: STy::Ptr(NativeTypeId::Pointer { pointee, .. }), .. }) = self.slots.get(name) {
                    if let NativeTypeId::Function { result, .. } = pointee.as_ref() {
                        return STy::from_identity(result).ok().flatten();
                    }
                }
                if let Some(binding) = self.externs.get_abi(name) {
                    return STy::from_type_ref(binding.result_type(), self.nominals).ok();
                }
                if let Some(binding) = self.externs.get(name) {
                    return STy::from_identity(binding.semantic_types().1).ok().flatten();
                }
                None
            }
            _ => None,
        }
    }

    fn expr(&mut self, expr: &Expr, expected: Option<&STy>) -> Result<SVal<'ctx>, String> {
        match expr {
            Expr::NativeNumber { text, .. } => match expected {
                Some(STy::Int(ty)) => self.literal(*ty, text, false),
                Some(STy::Float(ty)) => self.float_literal(*ty, text, false),
                Some(other) => Err(format!("Systemkoerper: Zahl {text} passt nicht zu {}", other.name())),
                None => {
                    let ty = Self::literal_standard(expr).ok_or("Systemkoerper: Literaltyp fehlt")?;
                    self.expr(expr, Some(&ty))
                }
            },
            Expr::Boolean(value) => Ok(SVal { ty: STy::Bool, value: self.context.bool_type().const_int(u64::from(*value), false).into() }),
            Expr::None => match expected {
                Some(ty @ STy::Ptr(_)) => Ok(SVal { ty: ty.clone(), value: self.ptrt().const_null().into() }),
                _ => Err("Systemkoerper: nichts ist nur als Nullzeiger mit Zeiger-Zieltyp erlaubt".into()),
            },
            // P2-T21/T57 C1: Funktionsname als C-Funktionszeiger (@rückruf-Thunk).
            Expr::Identifier(name) if !self.slots.contains_key(name) && self.callees.contains_key(name) =>
                self.rueckruf_adresse(name, expected),
            Expr::Identifier(name) => {
                let slot = self.slots.get(name)
                    .ok_or_else(|| format!("Systemkoerper: Name '{name}' nicht gefunden"))?;
                let (ty, ptr) = (slot.ty.clone(), slot.ptr);
                let value = self.builder.build_load(ty.llvm(self.context, self.layouts)?, ptr, name).map_err(e)?;
                if matches!(ty, STy::Struct(_)) {
                    // Nur noch Rueckfallpfad (z. B. extern-C-Roundtrip-Argument):
                    // Struktur-Slots sind ABI-ausgerichtet (eigene Alloca).
                    let alignment = self.struct_size_align(&ty)?.1;
                    value.as_instruction_value().ok_or("Systemkoerper: Slot-Load fehlt")?
                        .set_alignment(alignment).map_err(e)?;
                }
                Ok(SVal { ty, value })
            }
            Expr::UnaryOp { op, operand } => match op {
                UnaryOpKind::Neg => {
                    if let Expr::NativeNumber { text, .. } = operand.as_ref() {
                        return match expected {
                            Some(STy::Int(ty)) => self.literal(*ty, text, true),
                            Some(STy::Float(ty)) => self.float_literal(*ty, text, true),
                            None => {
                                let ty = Self::literal_standard(expr).ok_or("Systemkoerper: Literaltyp fehlt")?;
                                self.expr(expr, Some(&ty))
                            }
                            _ => Err(format!("Systemkoerper: Typ von -{text} ist nicht bestimmbar")),
                        };
                    }
                    if self.vecmat_von(operand).is_some() {
                        let (art, werte) = self.vecmat_rechnung(expr)?
                            .ok_or("Systemkoerper: vecmat-Negation fehlt")?;
                        return self.vecmat_aggregat(art, &werte);
                    }
                    let operand_ty = expected.cloned().or_else(|| self.infer(operand));
                    if let Some(STy::Float(ty)) = operand_ty {
                        let value = self.expr(operand, Some(&STy::Float(ty)))?;
                        let neg = self.builder.build_float_neg(
                            value.value.into_float_value(), "sys.fneg",
                        ).map_err(e)?;
                        return Ok(SVal { ty: STy::Float(ty), value: neg.into() });
                    }
                    let value = self.int_operand(operand, expected)?;
                    let result = crate::native_arithmetic::int_neg(self.context, &self.builder, value)?;
                    Ok(SVal { ty: STy::Int(result.ty), value: result.value })
                }
                UnaryOpKind::Not => {
                    let value = self.expr(operand, Some(&STy::Bool))?;
                    self.expect(&value, &STy::Bool, "nicht")?;
                    let result = self.builder.build_not(value.value.into_int_value(), "sys.nicht").map_err(e)?;
                    Ok(SVal { ty: STy::Bool, value: result.into() })
                }
                UnaryOpKind::BitNot => {
                    let value = self.int_operand(operand, expected)?;
                    let result = self.builder.build_not(value.value.into_int_value(), "sys.bitnicht").map_err(e)?;
                    Ok(SVal { ty: STy::Int(value.ty), value: result.into() })
                }
            },
            Expr::BinaryOp { left, op, right } => self.binary(left, op, right, expected),
            Expr::PropertyAccess { object, property } => {
                let alignment = self.local_field_alignment(object, property);
                let (ptr, ty) = self.pointer_property(object, property)?;
                let value = self.builder.build_load(
                    ty.llvm(self.context, self.layouts)?, ptr, "sys.property.load",
                ).map_err(e)?;
                value.as_instruction_value().ok_or("Systemkoerper: Feld-Load fehlt")?
                    .set_alignment(alignment).map_err(e)?;
                Ok(SVal { ty, value })
            },
            Expr::IndexAccess { object, index } => {
                let alignment = self.local_array_alignment(object);
                let (ptr, ty) = self.pointer_index(object, index)?;
                let value = self.builder.build_load(
                    ty.llvm(self.context, self.layouts)?, ptr, "sys.pointer.load",
                ).map_err(e)?;
                value.as_instruction_value().ok_or("Systemkoerper: Zeiger-Load fehlt")?
                    .set_alignment(alignment).map_err(e)?;
                Ok(SVal { ty, value })
            },
            Expr::FunctionCall { name, args, .. } if self.layouts.layout(name).is_some() =>
                self.struct_constructor(name, args, expected),
            Expr::FunctionCall { name, args, .. } if self.vecmat_funktion(name, args).is_some() => {
                match self.vecmat_rechnung(expr)? {
                    Some((art, werte)) => self.vecmat_aggregat(art, &werte),
                    None => self.vecmat_skalar(name, args),
                }
            }
            Expr::FunctionCall { name, args, .. } if self.trig_funktion(name, args).is_some() =>
                self.trig_aufruf(name, args, expected),
            Expr::FunctionCall { name, args, .. } => self.call(name, args, expected, false)?
                .ok_or_else(|| format!("Systemkoerper: '{name}' liefert keinen Wert")),
            _ => Err("Systemkoerper: Ausdruck wird nicht unterstützt".into()),
        }
    }

    /// Gleiche Konstruktorform wie native Konstanten: StrukturName(feld1,...).
    /// Jedes Feld wird im Zieltyp erzeugt und per LLVM-insertvalue eingefuegt.
    /// Arraywerte sind noch nicht Bestandteil dieses lokalen Slices.
    fn struct_constructor(
        &mut self, name: &str, args: &[Expr], expected: Option<&STy>,
    ) -> Result<SVal<'ctx>, String> {
        let layout = self.layouts.layout(name)
            .ok_or_else(|| format!("Systemkoerper: unbekannte Struktur '{name}'"))?;
        let fields = layout.fields.clone();
        let llvm = layout.llvm_type;
        let ty = self.struct_ty(name)?;
        if let Some(want) = expected {
            if want != &ty {
                return Err(format!(
                    "Systemkoerper: Konstruktor '{name}' erwartet {}, erhalten {}",
                    want.name(), ty.name(),
                ));
            }
        }
        // P2-T48 A4: Nullkonstruktor und Arrayfelder ueber den Speicherpfad
        // (construct_struct_into); reine Skalar-/Strukturfelder behalten ihr IR.
        let hat_array = fields.iter().any(|(_, field)| matches!(field.kind, TypeKind::Array { .. }));
        if args.is_empty() || hat_array {
            let (ptr, alignment) = self.entry_alloca(&ty, "sys.struct.neu")?;
            let place = Place { ptr, alignment, origin: Origin::Fresh };
            self.construct_struct_into(name, args, &place)?;
            let value = self.builder.build_load(llvm, ptr, "sys.struct.wert").map_err(e)?;
            value.as_instruction_value().ok_or("Systemkoerper: Struktur-Load fehlt")?
                .set_alignment(alignment).map_err(e)?;
            return Ok(SVal { ty, value });
        }
        if fields.len() != args.len() {
            return Err(format!(
                "Systemkoerper: Struktur '{name}' erwartet {} Felder, bekommen {}",
                fields.len(), args.len(),
            ));
        }
        let mut aggregate: inkwell::values::AggregateValueEnum<'ctx> = llvm.const_zero().into();
        for (index, ((field_name, field_ref), expr)) in fields.iter().zip(args).enumerate() {
            let field_ty = STy::from_type_ref(field_ref, self.nominals).map_err(|_| {
                format!("Systemkoerper: '{name}.{field_name}' hat keinen nativen Werttyp")
            })?;
            let value = self.expr(expr, Some(&field_ty))?;
            self.expect(&value, &field_ty, &format!("{name}.{field_name}"))?;
            aggregate = self.builder.build_insert_value(
                aggregate, value.value, index as u32, "sys.struct.feld",
            ).map_err(e)?;
        }
        Ok(SVal { ty, value: aggregate.into_struct_value().into() })
    }

    // ---- P2-T16 (Plan 3f03704a E3-E5): vec2/vec3/vec4/mat4 ----

    /// Art eines eingebauten Vektor-/Matrixtyps (gleichnamige eigene Struktur: None).
    fn vecmat_art(&self, ty: &STy) -> Option<VecMatArt> {
        match ty {
            STy::Struct(NativeTypeId::Nominal { name, .. }) => self.layouts.vecmat_art(name),
            _ => None,
        }
    }

    fn vecmat_von(&self, expr: &Expr) -> Option<VecMatArt> {
        self.infer(expr).and_then(|ty| self.vecmat_art(&ty))
    }

    fn vecmat_ty(&self, art: VecMatArt) -> Result<STy, String> {
        self.struct_ty(art.name())
    }

    /// (Spalten, Bahnen je Spalte); Matrixspalten liegen 4 Komponenten
    /// auseinander (mat4: 16 Byte, dmat4: 32 Byte).
    fn vecmat_form(art: VecMatArt) -> (u32, u32) {
        if art.ist_matrix() { (4, 4) } else { (1, art.bahnen()) }
    }

    /// Komponentengroesse in Byte (f32: 4, f64: 8).
    fn vecmat_komponente(art: VecMatArt) -> u32 {
        if art.doppelt() { 8 } else { 4 }
    }

    /// Adresse und bewiesene Ausrichtung der Komponente (spalte, bahn).
    fn vecmat_adresse(
        &self, place: &Place<'ctx>, art: VecMatArt, spalte: u32, bahn: u32,
    ) -> Result<(PointerValue<'ctx>, u32), String> {
        let k = Self::vecmat_komponente(art);
        let offset = u64::from(spalte * 4 * k + bahn * k);
        let address = unsafe {
            self.builder.build_gep(
                self.context.i8_type(), place.ptr, &[self.i64t().const_int(offset, false)], "vm.adresse",
            )
        }.map_err(e)?;
        Ok((address, field_alignment(place.alignment, offset)))
    }

    /// E3: Komponenten skalar laden (Ausrichtung aus dem Place) und zu SSA-
    /// Vektoren zusammensetzen; nie ein Laden eines LLVM-Vektortyps (kein
    /// Ueberlesen bei vec3, kein Layout-/ABI-Wechsel).
    fn vecmat_laden(&mut self, place: &Place<'ctx>, art: VecMatArt) -> Result<Vec<VectorValue<'ctx>>, String> {
        let float_t = if art.doppelt() { self.context.f64_type() } else { self.context.f32_type() };
        let (spalten, bahnen) = Self::vecmat_form(art);
        let mut werte = Vec::with_capacity(spalten as usize);
        for spalte in 0..spalten {
            let mut vektor = float_t.vec_type(bahnen).get_undef();
            for bahn in 0..bahnen {
                let (address, alignment) = self.vecmat_adresse(place, art, spalte, bahn)?;
                let wert = self.builder.build_load(float_t, address, "vm.lade").map_err(e)?;
                wert.as_instruction_value().ok_or("Systemkoerper: vecmat-Load fehlt")?
                    .set_alignment(alignment).map_err(e)?;
                vektor = self.builder.build_insert_element(
                    vektor, wert.into_float_value(),
                    self.context.i32_type().const_int(u64::from(bahn), false), "vm.setze",
                ).map_err(e)?;
            }
            werte.push(vektor);
        }
        Ok(werte)
    }

    /// E3: komponentenweise in einen frischen, ABI-ausgerichteten Ort speichern.
    fn vecmat_frisch(&mut self, art: VecMatArt, werte: &[VectorValue<'ctx>]) -> Result<Place<'ctx>, String> {
        let (spalten, bahnen) = Self::vecmat_form(art);
        if werte.len() != spalten as usize {
            return Err("Systemkoerper: vecmat-Spaltenzahl stimmt nicht".into());
        }
        let ty = self.vecmat_ty(art)?;
        let (ptr, alignment) = self.entry_alloca(&ty, "vm.neu")?;
        let place = Place { ptr, alignment, origin: Origin::Fresh };
        for (spalte, vektor) in werte.iter().enumerate() {
            for bahn in 0..bahnen {
                let wert = self.builder.build_extract_element(
                    *vektor, self.context.i32_type().const_int(u64::from(bahn), false), "vm.hole",
                ).map_err(e)?;
                let (address, alignment) = self.vecmat_adresse(&place, art, spalte as u32, bahn)?;
                self.builder.build_store(address, wert).map_err(e)?
                    .set_alignment(alignment).map_err(e)?;
            }
        }
        Ok(place)
    }

    /// Ergebnis als Strukturwert (nur Rueckfallpfad von expr()).
    fn vecmat_aggregat(&mut self, art: VecMatArt, werte: &[VectorValue<'ctx>]) -> Result<SVal<'ctx>, String> {
        let place = self.vecmat_frisch(art, werte)?;
        let ty = self.vecmat_ty(art)?;
        let value = self.builder.build_load(ty.llvm(self.context, self.layouts)?, place.ptr, "vm.wert").map_err(e)?;
        value.as_instruction_value().ok_or("Systemkoerper: vecmat-Load fehlt")?
            .set_alignment(place.alignment).map_err(e)?;
        Ok(SVal { ty, value })
    }

    /// Wert eines vecmat-Ausdrucks des erwarteten Typs als SSA-Vektoren:
    /// Rechnungen direkt, alles andere ueber seinen Ort (struct_place).
    fn vecmat_wert(&mut self, expr: &Expr, art: VecMatArt, what: &str) -> Result<Vec<VectorValue<'ctx>>, String> {
        if let Some((gefunden, werte)) = self.vecmat_rechnung(expr)? {
            if gefunden != art {
                return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", art.name(), gefunden.name()));
            }
            return Ok(werte);
        }
        let ty = self.vecmat_ty(art)?;
        let place = self.struct_place(expr, &ty, what)?;
        self.vecmat_laden(&place, art)
    }

    /// B3: eingebaute Funktion nur, wenn das erste Argument vecmat ist und kein
    /// Local, keine @nativ- und keine extern-C-Funktion gleichen Namens existiert.
    fn vecmat_funktion(&self, name: &str, args: &[Expr]) -> Option<(VmFunktion, VecMatArt)> {
        let funktion = match name {
            "punkt" | "dot" => VmFunktion::Punkt,
            "kreuz" | "cross" => VmFunktion::Kreuz,
            "betrag" | "magnitude" => VmFunktion::Betrag,
            "normalisiere" | "normalize" => VmFunktion::Normalisiere,
            _ => return None,
        };
        if self.callees.contains_key(name) || self.slots.contains_key(name)
            || self.externs.get(name).is_some() || self.externs.get_abi(name).is_some()
        {
            return None;
        }
        Some((funktion, self.vecmat_von(args.first()?)?))
    }

    /// P2-T89 (Freigabe #38178 O2): eingebaute Winkelfunktion, wenn kein Local,
    /// keine @nativ- und keine extern-C-Funktion gleichen Namens existiert.
    fn trig_funktion(&self, name: &str, args: &[Expr]) -> Option<crate::native_arithmetic::TrigArt> {
        let art = crate::native_arithmetic::trig_art(name)?;
        if args.len() != 1 || self.callees.contains_key(name) || self.slots.contains_key(name)
            || self.externs.get(name).is_some() || self.externs.get_abi(name).is_some()
        {
            return None;
        }
        Some(art)
    }

    /// sin/cos/tan auf f32/f64 (Bogenmass) ueber moo_native_math.c, ohne
    /// Fehlerpfad. Typ vom Argument, sonst vom Erwartungstyp, sonst f64
    /// (Literal ohne Kontext, ADR §2). Keine implizite Ganzzahl->Float-Wandlung.
    fn trig_aufruf(&mut self, name: &str, args: &[Expr], expected: Option<&STy>) -> Result<SVal<'ctx>, String> {
        let art = self.trig_funktion(name, args).ok_or("Systemkoerper: Winkelfunktion fehlt")?;
        let arg = &args[0];
        let ty = match self.infer(arg) {
            Some(STy::Float(t)) => t,
            Some(other) => return Err(format!(
                "Systemkoerper: {name}(x) erwartet f32 oder f64, erhalten {}", other.name()
            )),
            None => match expected {
                Some(STy::Float(t)) => *t,
                _ => NType::F64,
            },
        };
        let wert = self.expr(arg, Some(&STy::Float(ty)))?;
        self.expect(&wert, &STy::Float(ty), name)?;
        let ergebnis = crate::native_arithmetic::float_trig(
            self.context, &self.builder, self.module, art, NValue { ty, value: wert.value },
        )?;
        Ok(SVal { ty: STy::Float(ty), value: ergebnis.value })
    }

    /// Argument von betrag/normalisiere: genau ein Vektor (kein mat4).
    fn vecmat_einzelvektor(&mut self, name: &str, args: &[Expr], art: VecMatArt) -> Result<VectorValue<'ctx>, String> {
        let [v] = args else { return Err(format!("Systemkoerper: {name}(v) erwartet genau einen Vektor")) };
        if art.ist_matrix() {
            return Err(format!("Systemkoerper: {name} ist fuer mat4 nicht definiert"));
        }
        Ok(self.vecmat_wert(v, art, name)?[0])
    }

    /// f32-Ergebnisse: punkt(a, b) und betrag(v).
    fn vecmat_skalar(&mut self, name: &str, args: &[Expr]) -> Result<SVal<'ctx>, String> {
        let (funktion, art) = self.vecmat_funktion(name, args).ok_or("Systemkoerper: vecmat-Aufruf fehlt")?;
        let wert = match funktion {
            VmFunktion::Punkt => {
                let [a, b] = args else { return Err(format!("Systemkoerper: {name}(a, b) erwartet genau zwei Vektoren")) };
                if art.ist_matrix() {
                    return Err(format!("Systemkoerper: {name} ist fuer {} nicht definiert", art.name()));
                }
                let a = self.vecmat_wert(a, art, name)?[0];
                let b = self.vecmat_wert(b, art, name)?[0];
                vm::vec_dot(self.context, &self.builder, a, b)?
            }
            VmFunktion::Betrag => {
                let v = self.vecmat_einzelvektor(name, args, art)?;
                vm::vec_betrag(self.context, &self.builder, self.module, v)?
            }
            VmFunktion::Kreuz | VmFunktion::Normalisiere =>
                return Err(format!("Systemkoerper: {name} liefert keinen Skalar")),
        };
        Ok(SVal { ty: STy::Float(art.element()), value: wert.into() })
    }

    /// Rechenausdruecke mit vecmat-Ergebnis (Operatoren, Negation, kreuz,
    /// normalisiere); None fuer alle anderen Ausdruecke, ohne Code zu erzeugen.
    fn vecmat_rechnung(&mut self, expr: &Expr) -> Result<Option<(VecMatArt, Vec<VectorValue<'ctx>>)>, String> {
        match expr {
            Expr::BinaryOp { left, op, right } => self.vecmat_binaer(left, op, right),
            Expr::UnaryOp { op: UnaryOpKind::Neg, operand } => {
                let Some(art) = self.vecmat_von(operand) else { return Ok(None) };
                let werte = self.vecmat_wert(operand, art, "Negation")?;
                let negiert = werte.into_iter().map(|v| vm::vec_neg(&self.builder, v))
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(Some((art, negiert)))
            }
            Expr::FunctionCall { name, args, .. } => match self.vecmat_funktion(name, args) {
                Some((VmFunktion::Kreuz, art)) => {
                    let [a, b] = args.as_slice() else {
                        return Err(format!("Systemkoerper: {name}(a, b) erwartet genau zwei vec3"));
                    };
                    if art.ist_matrix() || art.bahnen() != 3 {
                        return Err(format!("Systemkoerper: {name} ist nur fuer vec3/dvec3 definiert, erhalten {}", art.name()));
                    }
                    let a = self.vecmat_wert(a, art, name)?[0];
                    let b = self.vecmat_wert(b, art, name)?[0];
                    Ok(Some((art, vec![vm::vec_cross(self.context, &self.builder, a, b)?])))
                }
                Some((VmFunktion::Normalisiere, art)) => {
                    let v = self.vecmat_einzelvektor(name, args, art)?;
                    Ok(Some((art, vec![vm::vec_normalisiere(self.context, &self.builder, self.module, v)?])))
                }
                _ => Ok(None),
            },
            _ => Ok(None),
        }
    }

    /// E4/E5: Operatoren mit mindestens einem vecmat-Operanden. Keine stille
    /// Mischung: Skalar nur als f32 bei vecN * f32, f32 * vecN, vecN / f32.
    fn vecmat_binaer(
        &mut self, left: &Expr, op: &BinOp, right: &Expr,
    ) -> Result<Option<(VecMatArt, Vec<VectorValue<'ctx>>)>, String> {
        let (vop, zeichen) = match op {
            BinOp::Add => (vm::VecOp::Add, "+"),
            BinOp::Sub => (vm::VecOp::Sub, "-"),
            BinOp::Mul => (vm::VecOp::Mul, "*"),
            BinOp::Div => (vm::VecOp::Div, "/"),
            _ => match (self.vecmat_von(left), self.vecmat_von(right)) {
                (None, None) => return Ok(None),
                (Some(art), _) | (_, Some(art)) =>
                    return Err(format!("Systemkoerper: Operator ist fuer {} nicht definiert", art.name())),
            },
        };
        let (la, ra) = (self.vecmat_von(left), self.vecmat_von(right));
        if la.is_none() && ra.is_none() {
            return Ok(None);
        }
        let ergebnis = match (la, ra) {
            (Some(l), Some(r)) if l.ist_matrix() && r == l && vop == vm::VecOp::Mul => {
                let was = format!("{0} * {0}", l.name());
                let a = self.vecmat_wert(left, l, &was)?;
                let b = self.vecmat_wert(right, r, &was)?;
                (l, vm::mat4_mul(self.context, &self.builder, &a, &b)?)
            }
            (Some(l), Some(r)) if l.ist_matrix() && r == l.vektor(4) && vop == vm::VecOp::Mul => {
                let was = format!("{} * {}", l.name(), r.name());
                let m = self.vecmat_wert(left, l, &was)?;
                let v = self.vecmat_wert(right, r, &was)?[0];
                (r, vec![vm::mat4_vec4(self.context, &self.builder, &m, v)?])
            }
            (Some(l), Some(r)) if l == r
                && (matches!(vop, vm::VecOp::Add | vm::VecOp::Sub) || !l.ist_matrix()) =>
            {
                let a = self.vecmat_wert(left, l, "Vektorrechnung")?;
                let b = self.vecmat_wert(right, r, "Vektorrechnung")?;
                let werte = a.into_iter().zip(b).map(|(x, y)| vm::vec_binary(&self.builder, vop, x, y))
                    .collect::<Result<Vec<_>, _>>()?;
                (l, werte)
            }
            (Some(l), None) if !l.ist_matrix() && matches!(vop, vm::VecOp::Mul | vm::VecOp::Div) => {
                let skalar = STy::Float(l.element());
                let v = self.vecmat_wert(left, l, "Vektor mit Skalar")?[0];
                let s = self.expr(right, Some(&skalar))?;
                self.expect(&s, &skalar, "Skalar einer Vektorrechnung")?;
                (l, vec![vm::vec_scale(self.context, &self.builder, vop, v, s.value.into_float_value())?])
            }
            (None, Some(r)) if !r.ist_matrix() && vop == vm::VecOp::Mul => {
                let skalar = STy::Float(r.element());
                let s = self.expr(left, Some(&skalar))?;
                self.expect(&s, &skalar, "Skalar einer Vektorrechnung")?;
                let v = self.vecmat_wert(right, r, "Skalar mit Vektor")?[0];
                (r, vec![vm::vec_scale(self.context, &self.builder, vop, v, s.value.into_float_value())?])
            }
            _ => {
                let name = |art: Option<VecMatArt>, x: &Expr| art.map(|a| a.name().to_string())
                    .or_else(|| self.infer(x).map(|t| t.name()))
                    .unwrap_or_else(|| "Zahl".to_string());
                return Err(format!(
                    "Systemkoerper: {} {zeichen} {} ist nicht definiert (keine stille Mischung; Skalar nur als f32 bei vecN * f32, f32 * vecN, vecN / f32 bzw. als f64 bei dvecN; mat4 * vec4, mat4 * mat4, mat4 +/- mat4 und ebenso dmat4 mit dvec4; f32- und f64-Typen nie gemischt)",
                    name(la, left), name(ra, right),
                ));
            }
        };
        Ok(Some(ergebnis))
    }

    fn literal(&self, ty: NType, text: &str, negative: bool) -> Result<SVal<'ctx>, String> {
        let value = ty.integer_literal(self.context, text, negative)?;
        Ok(SVal { ty: STy::Int(ty), value: value.value })
    }

    /// Floats werden in der vom Quelltyp geforderten f32/f64-Breite gerundet.
    fn float_literal(&self, ty: NType, text: &str, negative: bool) -> Result<SVal<'ctx>, String> {
        let (value, underflow) = ty.float_literal(self.context, text, negative)?;
        if underflow {
            crate::diagnose::melden(crate::diagnose::Diagnose::hinweis("M-H004", format!("Floatliteral '{text}' rundet auf null")));
        }
        Ok(SVal { ty: STy::Float(ty), value: value.value })
    }

    fn int_operand(&mut self, expr: &Expr, expected: Option<&STy>) -> Result<NValue<'ctx>, String> {
        let ty = match expected {
            Some(ty @ STy::Int(_)) => ty.clone(),
            _ => self.infer(expr).ok_or("Systemkoerper: Typ des Operanden ist nicht bestimmbar")?,
        };
        let value = self.expr(expr, Some(&ty))?;
        match value.ty {
            STy::Int(t) => Ok(NValue { ty: t, value: value.value }),
            _ => Err(format!("Systemkoerper: Ganzzahl erwartet, erhalten {}", value.ty.name())),
        }
    }

    /// P2-T17 E3 (Plan f5b52a0b): `p + n`, `n + p`, `p - n` mit Schrittweite =
    /// Allokationsgroesse des Zieltyps (GEP ohne inbounds, wickelt wie eine
    /// Adresse), `p - q` -> i64-Elementabstand (exakte Division, nur gleicher
    /// Zieltyp). Nicht fuer ptr[void], Funktionszeiger und opake Ziele; nur in
    /// unsicher:. n darf jede Ganzzahl sein (vorzeichenrichtig auf i64 erweitert).
    fn zeiger_rechnung(
        &mut self, left: &Expr, op: &BinOp, right: &Expr, lt: Option<STy>, rt: Option<STy>,
    ) -> Result<SVal<'ctx>, String> {
        self.verlange_unsicher("Zeigerarithmetik")?;
        let lp = matches!(lt, Some(STy::Ptr(_)));
        let rp = matches!(rt, Some(STy::Ptr(_)));
        let element = |ty: &STy| -> Result<STy, String> {
            let STy::Ptr(NativeTypeId::Pointer { pointee, .. }) = ty else {
                return Err("Systemkoerper: Zeigerarithmetik braucht einen typisierten ptr[T]".into());
            };
            if matches!(pointee.as_ref(), NativeTypeId::Function { .. }) {
                return Err("Systemkoerper: Zeigerarithmetik auf Funktionszeigern ist nicht definiert".into());
            }
            STy::from_identity(pointee)
                .map_err(|_| "Systemkoerper: Zeigerarithmetik braucht einen Zieltyp mit bekannter Groesse".to_string())?
                .ok_or_else(|| "Systemkoerper: Zeigerarithmetik auf ptr[void] ist nicht definiert (erst mit zeiger(p) auf ptr[u8] o. ae. umdeuten)".to_string())
        };
        match (lp, rp, op) {
            (true, true, BinOp::Sub) => {
                let (lt, rt) = (lt.unwrap_or(STy::Bool), rt.unwrap_or(STy::Bool));
                if lt != rt {
                    return Err(format!("Systemkoerper: Zeigerdifferenz nur bei gleichem Zieltyp ({} - {})", lt.name(), rt.name()));
                }
                let elem = element(&lt)?.llvm(self.context, self.layouts)?;
                let a = self.expr(left, Some(&lt))?.value.into_pointer_value();
                let b = self.expr(right, Some(&rt))?.value.into_pointer_value();
                let ai = self.builder.build_ptr_to_int(a, self.i64t(), "sys.zdiff.a").map_err(e)?;
                let bi = self.builder.build_ptr_to_int(b, self.i64t(), "sys.zdiff.b").map_err(e)?;
                let bytes = self.builder.build_int_sub(ai, bi, "sys.zdiff.bytes").map_err(e)?;
                let groesse = elem.size_of().ok_or("Systemkoerper: Zieltyp ohne Groesse")?;
                let groesse = self.builder.build_int_cast(groesse, self.i64t(), "sys.zdiff.groesse").map_err(e)?;
                let abstand = self.builder.build_int_exact_signed_div(bytes, groesse, "sys.zdiff").map_err(e)?;
                Ok(SVal { ty: STy::Int(NType::I64), value: abstand.into() })
            }
            (true, true, _) => Err("Systemkoerper: zwischen zwei Zeigern ist nur die Differenz p - q definiert".into()),
            (true, false, BinOp::Add | BinOp::Sub) | (false, true, BinOp::Add) => {
                let (zeiger_expr, zahl_expr, ptr_ty) = if lp {
                    (left, right, lt.ok_or("Zeigertyp fehlt")?)
                } else {
                    (right, left, rt.ok_or("Zeigertyp fehlt")?)
                };
                let elem = element(&ptr_ty)?.llvm(self.context, self.layouts)?;
                let basis = self.expr(zeiger_expr, Some(&ptr_ty))?.value.into_pointer_value();
                let zahl_ty = match self.infer(zahl_expr) {
                    Some(ty @ STy::Int(_)) => ty,
                    Some(other) => return Err(format!("Systemkoerper: Zeigerarithmetik erwartet eine Ganzzahl, erhalten {}", other.name())),
                    None => STy::Int(NType::I64),
                };
                let zahl = self.expr(zahl_expr, Some(&zahl_ty))?;
                let STy::Int(nt) = zahl.ty else {
                    return Err(format!("Systemkoerper: Zeigerarithmetik erwartet eine Ganzzahl, erhalten {}", zahl.ty.name()));
                };
                let roh = zahl.value.into_int_value();
                let mut schritt = if nt.is_signed_integer() {
                    self.builder.build_int_s_extend_or_bit_cast(roh, self.i64t(), "sys.zarith.n")
                } else {
                    self.builder.build_int_z_extend_or_bit_cast(roh, self.i64t(), "sys.zarith.n")
                }.map_err(e)?;
                if matches!(op, BinOp::Sub) {
                    schritt = self.builder.build_int_neg(schritt, "sys.zarith.neg").map_err(e)?;
                }
                let ptr = unsafe { self.builder.build_gep(elem, basis, &[schritt], "sys.zarith") }.map_err(e)?;
                Ok(SVal { ty: ptr_ty, value: ptr.into() })
            }
            _ => Err("Systemkoerper: mit Zeigern sind nur p + n, n + p, p - n und p - q definiert".into()),
        }
    }

    fn binary(&mut self, left: &Expr, op: &BinOp, right: &Expr, expected: Option<&STy>) -> Result<SVal<'ctx>, String> {
        use crate::native_arithmetic::{CompareOp, IntOp};
        let compare = match op {
            BinOp::Eq => Some(CompareOp::Eq), BinOp::NotEq => Some(CompareOp::Ne),
            BinOp::Less => Some(CompareOp::Lt), BinOp::LessEq => Some(CompareOp::Le),
            BinOp::Greater => Some(CompareOp::Gt), BinOp::GreaterEq => Some(CompareOp::Ge),
            _ => None,
        };
        // P2-T16 E4/E5: Vektor-/Matrixoperanden (== / != komponentenweise).
        if compare.is_some() {
            if let Some(art) = self.vecmat_von(left).or_else(|| self.vecmat_von(right)) {
                if !matches!(op, BinOp::Eq | BinOp::NotEq) {
                    return Err(format!("Systemkoerper: {} nur mit == oder != vergleichen", art.name()));
                }
                let a = self.vecmat_wert(left, art, "Vergleich")?;
                let b = self.vecmat_wert(right, art, "Vergleich")?;
                let result = vm::vecmat_gleich(self.context, &self.builder, &a, &b, matches!(op, BinOp::Eq))?;
                return Ok(SVal { ty: STy::Bool, value: result.into() });
            }
        }
        if let Some((art, werte)) = self.vecmat_binaer(left, op, right)? {
            return self.vecmat_aggregat(art, &werte);
        }
        // P2-T17 E3: Zeigerarithmetik vor dem Ganzzahl-/Floatpfad.
        if matches!(op, BinOp::Add | BinOp::Sub | BinOp::Mul | BinOp::Div | BinOp::Mod) {
            let (lt, rt) = (self.infer(left), self.infer(right));
            if matches!(lt, Some(STy::Ptr(_))) || matches!(rt, Some(STy::Ptr(_))) {
                return self.zeiger_rechnung(left, op, right, lt, rt);
            }
        }
        if let Some(cmp) = compare {
            let ty = self.infer(left).or_else(|| self.infer(right))
                .or_else(|| Self::literal_paar(left, right))
                .ok_or("Systemkoerper: Typ des Vergleichs ist nicht bestimmbar")?;
            let l = self.expr(left, Some(&ty))?;
            let r = self.expr(right, Some(&ty))?;
            self.expect(&r, &l.ty, "Vergleich")?;
            if let STy::Ptr(_) = &ty {
                if !matches!(cmp, CompareOp::Eq | CompareOp::Ne) {
                    return Err("Systemkoerper: Zeiger nur mit == oder != vergleichen".into());
                }
                let a = self.builder.build_ptr_to_int(l.value.into_pointer_value(), self.i64t(), "sys.pa").map_err(e)?;
                let b = self.builder.build_ptr_to_int(r.value.into_pointer_value(), self.i64t(), "sys.pb").map_err(e)?;
                let pred = if matches!(cmp, CompareOp::Eq) { IntPredicate::EQ } else { IntPredicate::NE };
                let result = self.builder.build_int_compare(pred, a, b, "sys.pcmp").map_err(e)?;
                return Ok(SVal { ty: STy::Bool, value: result.into() });
            }
            let nt = match ty {
                STy::Int(t) | STy::Float(t) => t,
                STy::Bool => NType::Bool,
                _ => return Err("Systemkoerper: Struktur- und Zeigerwerte koennen nicht so verglichen werden".into()),
            };
            let result = crate::native_arithmetic::compare(self.context, &self.builder, cmp,
                NValue { ty: nt, value: l.value }, NValue { ty: nt, value: r.value })?;
            return Ok(SVal { ty: STy::Bool, value: result.value });
        }
        if matches!(op, BinOp::And | BinOp::Or) {
            let l = self.condition(left)?;
            let left_end = self.builder.get_insert_block().ok_or("kein Block")?;
            let rhs_bb = self.context.append_basic_block(self.function, "sys.logik.rechts");
            let merge = self.context.append_basic_block(self.function, "sys.logik.ende");
            if matches!(op, BinOp::And) {
                self.builder.build_conditional_branch(l, rhs_bb, merge).map_err(e)?;
            } else {
                self.builder.build_conditional_branch(l, merge, rhs_bb).map_err(e)?;
            }
            self.builder.position_at_end(rhs_bb);
            let r = self.condition(right)?;
            let right_end = self.builder.get_insert_block().ok_or("kein Block")?;
            self.builder.build_unconditional_branch(merge).map_err(e)?;
            self.builder.position_at_end(merge);
            let phi = self.builder.build_phi(self.context.bool_type(), "sys.logik").map_err(e)?;
            let short = self.context.bool_type().const_int(u64::from(matches!(op, BinOp::Or)), false);
            phi.add_incoming(&[(&short, left_end), (&r, right_end)]);
            return Ok(SVal { ty: STy::Bool, value: phi.as_basic_value() });
        }
        // Float-Felder koennen dieselben IEEE-Operationen nutzen wie reine
        // native Moo-Funktionen; keine implizite Integer-/Float-Konversion.
        let float_ty = expected.cloned().or_else(|| self.infer(left)).or_else(|| self.infer(right))
            .or_else(|| Self::literal_paar(left, right));
        if let Some(STy::Float(t)) = float_ty {
            use crate::native_arithmetic::FloatOp;
            let op = match op {
                BinOp::Add => FloatOp::Add, BinOp::Sub => FloatOp::Sub,
                BinOp::Mul => FloatOp::Mul, BinOp::Div => FloatOp::Div,
                BinOp::Mod => FloatOp::Rem, BinOp::Pow => FloatOp::Pow,
                _ => return Err("Systemkoerper: Float-Operator wird nicht unterstuetzt".into()),
            };
            let l = self.expr(left, Some(&STy::Float(t)))?;
            let r = self.expr(right, Some(&STy::Float(t)))?;
            self.expect(&r, &l.ty, "Float-Rechnung")?;
            let (ln, rn) = (NValue { ty: t, value: l.value }, NValue { ty: t, value: r.value });
            let result = if matches!(op, FloatOp::Rem | FloatOp::Pow) {
                crate::native_arithmetic::float_bibliothek(self.context, &self.builder, self.module, op, ln, rn)?
            } else {
                crate::native_arithmetic::float_binary(self.context, &self.builder, op, ln, rn)?
            };
            return Ok(SVal { ty: STy::Float(t), value: result.value });
        }
        let int_op = match op {
            BinOp::Add => IntOp::Add, BinOp::Sub => IntOp::Sub, BinOp::Mul => IntOp::Mul,
            BinOp::Div => IntOp::Div, BinOp::Mod => IntOp::Rem,
            BinOp::BitAnd => IntOp::BitAnd, BinOp::BitOr => IntOp::BitOr, BinOp::BitXor => IntOp::BitXor,
            BinOp::LShift => IntOp::Shl, BinOp::RShift => IntOp::Shr,
            BinOp::Pow => IntOp::Pow,
            _ => return Err("Systemkoerper: Operator wird nicht unterstützt".into()),
        };
        let ty = match expected {
            Some(ty @ STy::Int(_)) => ty.clone(),
            _ => self.infer(left).or_else(|| if matches!(int_op, IntOp::Shl | IntOp::Shr) { None } else { self.infer(right) })
                .or_else(|| Self::literal_paar(left, right))
                .ok_or("Systemkoerper: Typ der Rechnung ist nicht bestimmbar")?,
        };
        let l = self.int_operand(left, Some(&ty))?;
        let right_ty = if matches!(int_op, IntOp::Shl | IntOp::Shr) {
            self.infer(right).unwrap_or_else(|| ty.clone())
        } else { ty.clone() };
        let r = self.int_operand(right, Some(&right_ty))?;
        // Nur Div/Rem/Shift koennen fehlen; andere Operationen bekommen den
        // nie erreichten Block (kein moo_native_fehler im IR).
        // P2-T58: ** kann nur signed fehlschlagen (negativer Exponent).
        let pow_signed = matches!(int_op, IntOp::Pow) && matches!(&ty, STy::Int(t) if t.is_signed_integer());
        let failure = if pow_signed {
            // Review 06d10060 #1: konstant negativer Exponent ist auch hier ein
            // Compilefehler (Plan f40b5980 §2.1, wie pruefe_konstante_operanden).
            if let Some(exponent) = r.value.into_int_value().get_sign_extended_constant() {
                if exponent < 0 {
                    return Err("Native Potenz mit konstant negativem Exponenten".into());
                }
            }
            self.pow_fehler_block()?
        } else if matches!(int_op, IntOp::Div | IntOp::Rem | IntOp::Shl | IntOp::Shr) {
            self.trap_block()?
        } else {
            self.never_block()?
        };
        let result = crate::native_arithmetic::int_binary(self.context, &self.builder, int_op, l, r, failure)?;
        Ok(SVal { ty: STy::Int(result.ty), value: result.value })
    }

    fn address(&mut self, expr: &Expr) -> Result<PointerValue<'ctx>, String> {
        let value = self.expr(expr, Some(&STy::Int(NType::U64)))?;
        self.expect(&value, &STy::Int(NType::U64), "Adresse")?;
        self.builder.build_int_to_ptr(value.value.into_int_value(), self.ptrt(), "sys.adr").map_err(e)
    }

    fn asm(&self, code: &str, constraints: &str, result: Option<BasicTypeEnum<'ctx>>, args: &[BasicMetadataValueEnum<'ctx>]) -> Result<Option<BasicValueEnum<'ctx>>, String> {
        let types: Vec<BasicMetadataTypeEnum<'ctx>> = args.iter().map(|arg| match arg {
            BasicMetadataValueEnum::IntValue(v) => Ok(v.get_type().into()),
            BasicMetadataValueEnum::PointerValue(v) => Ok(v.get_type().into()),
            _ => Err("Assembler-Operand ist weder Ganzzahl noch Zeiger".to_string()),
        }).collect::<Result<_, _>>()?;
        let fn_type = match result {
            Some(ty) => ty.fn_type(&types, false),
            None => self.context.void_type().fn_type(&types, false),
        };
        let asm = self.context.create_inline_asm(fn_type, code.to_string(), constraints.to_string(), true, false, None, false);
        let call = self.builder.build_indirect_call(fn_type, asm, args, "sys.asm").map_err(e)?;
        Ok(match call.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(value) => Some(value),
            _ => None,
        })
    }

    /// P2-T17 E4 (Plan f5b52a0b B2): ptr[T] -> ptr[void] ist als Argument still
    /// erlaubt (C-Konvention, gleicher Adressraum, kein Funktionszeiger); jede
    /// andere Abweichung verlangt Typgleichheit, der Rueckweg nur ausdruecklich
    /// mit zeiger(p).
    fn expect_argument(&self, value: &SVal<'ctx>, ty: &STy, what: &str) -> Result<(), String> {
        if let (
            STy::Ptr(NativeTypeId::Pointer { pointee: ziel, address_space: a }),
            STy::Ptr(NativeTypeId::Pointer { pointee: quelle, address_space: b }),
        ) = (ty, &value.ty) {
            if matches!(ziel.as_ref(), NativeTypeId::Void) && a == b
                && !matches!(quelle.as_ref(), NativeTypeId::Function { .. })
            {
                return Ok(());
            }
        }
        self.expect(value, ty, what)
    }

    fn args_for(&mut self, name: &str, args: &[Expr], params: &[STy]) -> Result<Vec<SVal<'ctx>>, String> {
        if args.len() != params.len() {
            return Err(crate::diagnose::vormerken("M-N004", self.zeile,
                crate::codegen_native::aritaet_meldung(name, params.len(), args.len())));
        }
        let mut values = Vec::with_capacity(args.len());
        for (index, (arg, ty)) in args.iter().zip(params).enumerate() {
            let value = self.expr(arg, Some(ty))?;
            self.expect_argument(&value, ty, &format!("Argument {} von '{name}'", index + 1))?;
            values.push(value);
        }
        Ok(values)
    }

    /// Interner Moo-Aufruf mit Strukturwerten (Signatur: SystemSignature::
    /// llvm_type). Jeder Strukturparameter erhaelt eine exklusive Kopie im
    /// Eintrittsblock; ein frischer Wert (Konstruktor, Rueckgabe) wird ohne
    /// weitere Kopie durchgereicht. Eine Struktur-Rueckgabe landet in einem
    /// sret-Puffer, der als frischer Ort zurueckgegeben wird.
    fn call_system_value(
        &mut self, name: &str, callee: &NativeCallee<'ctx>, args: &[Expr],
    ) -> Result<(inkwell::values::CallSiteValue<'ctx>, Option<(PointerValue<'ctx>, STy)>), String> {
        if args.len() != callee.params.len() {
            return Err(crate::diagnose::vormerken("M-N004", self.zeile,
                crate::codegen_native::aritaet_meldung(name, callee.params.len(), args.len())));
        }
        let mut call_args: Vec<BasicMetadataValueEnum<'ctx>> = Vec::with_capacity(args.len() + 1);
        let mut attributes: Vec<(u32, Vec<Attribute>)> = Vec::new();
        let sret = match &callee.result {
            Some(ty @ STy::Struct(_)) => {
                let (ptr, _) = self.entry_alloca(ty, "sys.sret")?;
                let llvm = ty.llvm(self.context, self.layouts)?;
                attributes.push((0, struct_pointer_attributes(self.context, llvm, self.target, true)));
                call_args.push(ptr.into());
                Some((ptr, ty.clone()))
            }
            _ => None,
        };
        for (index, (arg, ty)) in args.iter().zip(&callee.params).enumerate() {
            let position = call_args.len() as u32;
            let what = format!("Argument {} von '{name}'", index + 1);
            match ty {
                STy::Struct(_) => {
                    let place = self.struct_place(arg, ty, &what)?;
                    let ptr = if place.origin == Origin::Fresh {
                        place.ptr
                    } else {
                        let (copy, alignment) = self.entry_alloca(ty, "sys.arg.kopie")?;
                        self.copy_struct(ty, Place { ptr: copy, alignment, origin: Origin::Fresh }, place)?;
                        copy
                    };
                    let llvm = ty.llvm(self.context, self.layouts)?;
                    attributes.push((position, struct_pointer_attributes(self.context, llvm, self.target, false)));
                    call_args.push(ptr.into());
                }
                other => {
                    let value = self.expr(arg, Some(other))?;
                    self.expect_argument(&value, other, &what)?;
                    call_args.push(value.value.into());
                }
            }
        }
        let named = matches!(callee.result, Some(ref ty) if !matches!(ty, STy::Struct(_)));
        let call = self.builder.build_call(
            callee.function, &call_args, if named { "sys.direkt" } else { "" },
        ).map_err(e)?;
        call.set_call_convention(callee.function.get_call_conventions());
        for (position, list) in attributes {
            for attribute in list { call.add_attribute(AttributeLoc::Param(position), attribute); }
        }
        self.nach_aufruf_pruefen(callee)?;
        Ok((call, sret))
    }

    /// P2-T58 C3: hat ein fehlbarer Moo-interner Callee gemeldet (Flag gesetzt,
    /// Nullwert zurueck), nicht mit dem Nullwert weiterrechnen: sofort ohne neue
    /// Meldung zurueck (weiter_block). Nur hosted fangbar; reine Float-/Bit-
    /// Natives sind nicht fehlbar und bleiben ohne Zusatzkosten. Aufrufer:
    /// call_system_value (Strukturen) und der skalare Pfad in call.
    fn nach_aufruf_pruefen(&mut self, callee: &NativeCallee<'ctx>) -> Result<(), String> {
        let intern = callee.function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."));
        if !(callee.fehlbar && intern && (self.fangbar() || self.abbruch())) { return Ok(()); }
        let hook = crate::codegen_native::native_fehler_offen_hook(self.context, self.module)?;
        let flag = match self.builder.build_call(hook, &[], "sys.fehler.offen").map_err(e)?.try_as_basic_value() {
            inkwell::values::ValueKind::Basic(value) => value.into_int_value(),
            _ => return Err("moo_native_fehler_offen liefert keinen Wert".into()),
        };
        let offen = self.builder.build_int_compare(
            IntPredicate::NE, flag, self.context.i32_type().const_zero(), "sys.fehler.gesetzt",
        ).map_err(e)?;
        let weiter = if self.abbruch() {
            let callee_name = callee.function.get_name().to_str().unwrap_or("?");
            let callee_name = callee_name.strip_prefix("__moo_native.").unwrap_or(callee_name).to_string();
            self.abbruch_block(&format!("Fehler aus '{callee_name}'"))?
        } else {
            self.weiter_block()?
        };
        let ok = self.context.append_basic_block(self.function, "sys.fehler.ok");
        self.builder.build_conditional_branch(offen, weiter, ok).map_err(e)?;
        self.builder.position_at_end(ok);
        Ok(())
    }

    fn call_result(&self, call: inkwell::values::CallSiteValue<'ctx>, result: Option<STy>) -> Result<Option<SVal<'ctx>>, String> {
        match result {
            None => Ok(None),
            Some(ty) => match call.try_as_basic_value() {
                inkwell::values::ValueKind::Basic(value) => Ok(Some(SVal { ty, value })),
                _ => Err("Aufruf liefert keinen Wert".into()),
            },
        }
    }

    fn call(&mut self, name: &str, args: &[Expr], expected: Option<&STy>, statement: bool) -> Result<Option<SVal<'ctx>>, String> {
        // P2-T66: im hosted Usermode gibt es weder MMIO noch privilegierte
        // Hardwarezugriffe; diese Builtins bleiben Bare/UEFI vorbehalten.
        if self.hosted && (barrier(name).is_some() || matches!(name,
            "registerfeld_lesen" | "register_field_read" | "registerfeld_schreiben" | "register_field_write"
            | "volatile_lesen" | "volatile_read" | "volatile_schreiben" | "volatile_write" | "assembler"))
        {
            return Err(format!("Systemkoerper: '{name}' ist nur im Bare-Profil (Bare/UEFI) verfuegbar, nicht im hosted Usermode"));
        }
        // P2-T12 A (Plan 31194239 A2/A3): Positivliste fuer sichere Koerper
        // ausserhalb von unsicher:. Ein neuer oder vergessener Builtin-Zweig ist
        // damit automatisch gesperrt (zeiger, adresse_von, speicher_*, struktur-/
        // registerfeld_*, volatile_*, assembler, port_*, Barrieren, u8(x).., extern C,
        // Funktionszeiger-Slots).
        if self.sicher && self.unsicher_tiefe == 0 {
            let eigener = self.function.get_name().to_str().ok()
                .and_then(|n| n.strip_prefix("__moo_native.")) == Some(name);
            let erlaubt = crate::native_arithmetic::pruef_art(name).is_some()
                || crate::native_arithmetic::cast_art(name).is_some()
                || self.callees.contains_key(name) || eigener
                || self.layouts.layout(name).is_some()
                || self.puffer_builtin_typ(name, args).is_some();
            if !erlaubt {
                // P2-T98 S3: M-M005 mit Zeile.
                return Err(crate::diagnose::vormerken("M-M005", self.zeile, format!("Systemkoerper: Aufruf '{name}' ist nur innerhalb von unsicher: erlaubt (sicherer @nativ-Koerper; ohne unsicher: erlaubt: als/als_wrap/als_saettigend, teilbar/verschiebbar/potenzierbar, andere @nativ-Funktionen, Strukturkonstruktoren, länge/puffer_zeiger)")));
            }
        }
        // P2-T34 / ADR §13: Vorpruefung mit bool-Status statt Trap: teilbar(a, b),
        // verschiebbar(a, n). Erlaubt im Systemkoerper eine Fortsetzung ohne Fehler.
        if let Some(op) = crate::native_arithmetic::pruef_art(name) {
            let [left, right] = args else {
                return Err(format!("Systemkoerper: {name}(a, b) erwartet genau zwei Argumente"));
            };
            let shift = matches!(op, crate::native_arithmetic::IntOp::Shl);
            let ty = self.infer(left).or_else(|| if shift { None } else { self.infer(right) })
                .ok_or_else(|| format!("Systemkoerper: {name}: Typ der Operanden ist nicht bestimmbar"))?;
            let l = self.int_operand(left, Some(&ty))?;
            let right_ty = if shift { self.infer(right).unwrap_or_else(|| ty.clone()) } else { ty.clone() };
            let r = self.int_operand(right, Some(&right_ty))?;
            let result = crate::native_arithmetic::pruefung(self.context, &self.builder, op, l, r)?;
            return Ok(Some(SVal { ty: STy::Bool, value: result.value }));
        }
        // P2-T34 / ADR §4: als(T, x) gepruefter Standardcast, als_wrap(T, x),
        // als_saettigend(T, x). Fehlervertrag des Systemkoerpers wie bei
        // Division/Shift: trap_block (Bare Trap, hosted moo_native_fehler);
        // u8(x) o. Ae. bleibt die bisherige ungepruefte Breitenkonversion.
        if let Some(art) = crate::native_arithmetic::cast_art(name) {
            let [ziel, operand] = args else {
                return Err(format!("Systemkoerper: {name}(Typ, Wert) erwartet genau zwei Argumente"));
            };
            let target = match ziel {
                Expr::Identifier(ty) => NType::from_name(ty)
                    .ok_or_else(|| format!("Systemkoerper: {name}: unbekannter Zieltyp '{ty}'"))?,
                _ => return Err(format!("Systemkoerper: {name}: erstes Argument muss ein Typname sein, z. B. {name}(u8, x)")),
            };
            let literal_ty = |text: &str| STy::from_ntype(crate::codegen_native::literal_default_type(text)).ok();
            let source = self.infer(operand).or_else(|| match operand {
                Expr::NativeNumber { text, .. } => literal_ty(text),
                Expr::UnaryOp { operand: inner, .. } => match inner.as_ref() {
                    Expr::NativeNumber { text, .. } => literal_ty(text),
                    _ => None,
                },
                _ => None,
            }).ok_or_else(|| format!("Systemkoerper: {name}: Typ des Operanden ist nicht bestimmbar"))?;
            let value = self.expr(operand, Some(&source))?;
            let quelle = match &value.ty {
                STy::Int(ty) | STy::Float(ty) => *ty,
                STy::Bool => NType::Bool,
                other => return Err(format!("Systemkoerper: {name} konvertiert nur Zahlen, nicht {}", other.name())),
            };
            let value = NValue { ty: quelle, value: value.value };
            crate::native_arithmetic::pruefe_konstanten_cast(art, &value, target)?;
            let failure = if crate::native_arithmetic::cast_kann_fehlen(art, quelle, target) {
                self.trap_block()?
            } else {
                self.never_block()?
            };
            let result = crate::native_arithmetic::cast(
                self.context, &self.builder, self.module, art, value, target, failure,
            )?;
            return Ok(Some(SVal { ty: STy::from_ntype(target)?, value: result.value }));
        }
        if let Some(target) = conversion(name) {
            let [arg] = args else { return Err(format!("{name}(...) erwartet genau ein Argument")) };
            let source = self.infer(arg).unwrap_or(STy::Int(target));
            let value = self.expr(arg, Some(&source))?;
            let llvm = target.llvm_type(self.context).into_int_type();
            let converted: IntValue<'ctx> = match &value.ty {
                STy::Ptr(_) => {
                    let raw = self.builder.build_ptr_to_int(value.value.into_pointer_value(), self.i64t(), "sys.p2i").map_err(e)?;
                    self.builder.build_int_cast_sign_flag(raw, llvm, false, "sys.konv").map_err(e)?
                }
                STy::Bool => self.builder.build_int_z_extend(value.value.into_int_value(), llvm, "sys.konv").map_err(e)?,
                STy::Struct(_) | STy::Float(_) | STy::Buffer(_) | STy::StrukturPuffer(_) | STy::Array { .. } => return Err(format!(
                    "Systemkoerper: {name}(x) konvertiert nur Ganzzahlen, Zeiger und bool; fuer Floatwerte als({name}, x) (geprueft) oder als_saettigend({name}, x) verwenden, Strukturen per Feldzugriff"
                )),
                STy::Int(source) => {
                    let raw = value.value.into_int_value();
                    if bits_of(*source) == bits_of(target) { raw }
                    else { self.builder.build_int_cast_sign_flag(raw, llvm, source.is_signed_integer(), "sys.konv").map_err(e)? }
                }
            };
            return Ok(Some(SVal { ty: STy::Int(target), value: converted.into() }));
        }
        if matches!(name, "strukturfeld_lesen" | "struct_field_read" | "strukturfeld_schreiben"
            | "struct_field_write" | "registerfeld_lesen" | "register_field_read"
            | "registerfeld_schreiben" | "register_field_write")
        {
            let schreiben = name.ends_with("schreiben") || name.ends_with("write");
            let volatile = name.starts_with("register");
            if args.len() != if schreiben { 4 } else { 3 } {
                return Err(format!("Systemkoerper: {name} erwartet Struktur, Feld, Adresse{}", if schreiben { ", Wert" } else { "" }));
            }
            let (Expr::Identifier(struktur), Expr::Identifier(feld)) = (&args[0], &args[1]) else {
                return Err(format!("Systemkoerper: {name} erwartet Struktur- und Feldnamen"));
            };
            let (offset, ty) = self.strukturfeld_info(struktur, feld)?;
            let basis = self.address(&args[2])?;
            let adresse = unsafe {
                self.builder.build_gep(
                    self.context.i8_type(), basis, &[self.i64t().const_int(offset, false)],
                    "sys.strukturfeld.adresse",
                )
            }.map_err(e)?;
            if schreiben {
                let wert = self.expr(&args[3], Some(&ty))?;
                self.expect(&wert, &ty, name)?;
                let store = self.builder.build_store(adresse, wert.value).map_err(e)?;
                store.set_alignment(1).map_err(e)?;
                if volatile { store.set_volatile(true).map_err(e)?; }
                return Ok(None);
            }
            let wert = self.builder.build_load(
                ty.llvm(self.context, self.layouts)?, adresse, "sys.strukturfeld.lesen",
            ).map_err(e)?;
            let instruction = wert.as_instruction_value().ok_or("Systemkoerper: Strukturfeld-Load fehlt")?;
            instruction.set_alignment(1).map_err(e)?;
            if volatile { instruction.set_volatile(true).map_err(e)?; }
            return Ok(Some(SVal { ty, value: wert }));
        }
        match name {
            "zeiger" | "pointer" => {
                let ty = match expected {
                    Some(ty @ STy::Ptr(_)) => ty.clone(),
                    _ => return Err("zeiger(...) braucht einen Zeiger-Zieltyp aus dem Kontext (z. B. setze p: ptr[u8] auf zeiger(a))".into()),
                };
                let [arg] = args else { return Err("zeiger(...) erwartet genau eine Adresse".into()) };
                // P2-T17 E4: zeiger(p) deutet einen Zeiger auf den Zieltyp aus dem
                // Kontext um (gleiche Adresse, nur in unsicher:).
                if let Some(quelle @ STy::Ptr(_)) = self.infer(arg) {
                    self.verlange_unsicher("Zeigerumdeutung zeiger(p)")?;
                    let wert = self.expr(arg, Some(&quelle))?;
                    return Ok(Some(SVal { ty, value: wert.value }));
                }
                let ptr = self.address(arg)?;
                return Ok(Some(SVal { ty, value: ptr.into() }));
            }
            "adresse_von" | "address_of" => {
                let [target] = args else {
                    return Err("adresse_von(...) erwartet genau eine Variable, ein Feld oder einen Index".into());
                };
                // Bestehende Namenssemantik bleibt unveraendert. Bei Feldern
                // und Arrayelementen wird deren echte Speicheradresse
                // geliefert, ohne Strukturwerte oder Pointerfelder zu laden.
                let pointer = match target {
                    Expr::Identifier(symbol) => {
                        if let Some(slot) = self.slots.get(symbol) {
                            slot.ptr
                        } else if let Some(callee) = self.callees.get(symbol) {
                            // P2-T21/T57 C4 (Plan f4655d20 E8, Koordinator F1): hosted keine
                            // Adresse der internen Moo-Funktion (Moo-Konvention, fangbarer
                            // Fehlervertrag); C-Funktionszeiger nur ueber @rückruf.
                            if self.hosted {
                                return Err(format!(
                                    "adresse_von: '{symbol}' ist eine native Funktion; als C-Funktionszeiger nur ueber @rückruf (Name im ptr[fn(...)]-Kontext)"
                                ));
                            }
                            callee.function.as_global_value().as_pointer_value()
                        } else if let Some(global) = self.module.get_global(symbol) {
                            global.as_pointer_value()
                        } else if let Some(function) = self.module.get_function(symbol) {
                            function.as_global_value().as_pointer_value()
                        } else {
                            return Err(format!(
                                "adresse_von: '{symbol}' ist weder Local, Parameter, native Konstante noch Funktion"
                            ));
                        }
                    }
                    Expr::PropertyAccess { object, property } =>
                        self.field_address(object, property)?.0,
                    Expr::IndexAccess { object, index } => {
                        if let Some((address, _)) = self.array_field_index(object, index)? {
                            address
                        } else if self.struct_name_from_expr(target).is_some() {
                            self.struct_pointer(target)?.0
                        } else {
                            self.pointer_index(object, index)?.0
                        }
                    }
                    _ => return Err(
                        "adresse_von: erwartet Variable, Strukturfeld oder typisierten Index".into()
                    ),
                };
                let value = self.builder.build_ptr_to_int(pointer, self.i64t(), "sys.adresse").map_err(e)?;
                return Ok(Some(SVal { ty: STy::Int(NType::U64), value: value.into() }));
            }
            "speicher_lesen" | "mem_read" => {
                let [adr, width] = args else { return Err("speicher_lesen(adresse, breite) erwartet zwei Argumente".into()) };
                let ty = width_type(width)?;
                let ptr = self.address(adr)?;
                let value = self.builder.build_load(ty.llvm_type(self.context), ptr, "sys.lesen").map_err(e)?;
                value.as_instruction_value().ok_or("Load fehlt")?.set_alignment(1).map_err(e)?;
                return Ok(Some(SVal { ty: STy::Int(ty), value }));
            }
            "speicher_schreiben" | "mem_write" => {
                let [adr, wert, width] = args else { return Err("speicher_schreiben(adresse, wert, breite) erwartet drei Argumente".into()) };
                let ty = width_type(width)?;
                let ptr = self.address(adr)?;
                let value = self.expr(wert, Some(&STy::Int(ty)))?;
                self.expect(&value, &STy::Int(ty), "speicher_schreiben-Wert")?;
                let store = self.builder.build_store(ptr, value.value).map_err(e)?;
                store.set_alignment(1).map_err(e)?;
                return Ok(None);
            }
            "volatile_lesen" | "volatile_read" | "volatile_schreiben" | "volatile_write" => {
                let write = name.contains("schreiben") || name.contains("write");
                let pointer = args.first().ok_or("volatile_*: Zeiger fehlt")?;
                let pty = self.infer(pointer).ok_or("volatile_*: erster Operand muss ein ptr[T]-Ausdruck sein")?;
                let element = match &pty {
                    STy::Ptr(NativeTypeId::Pointer { pointee, .. }) => match STy::from_identity(pointee)? {
                        Some(STy::Int(t)) => t,
                        _ => return Err("Volatile-Zugriff erfordert einen Integer-Registertyp (u8..u64, i8..i64)".into()),
                    },
                    _ => return Err("volatile_*: erster Operand muss ein ptr[T]-Ausdruck sein".into()),
                };
                let base = self.expr(pointer, Some(&pty))?.value.into_pointer_value();
                let (index, rest) = match (write, args.len()) {
                    (false, 1) | (true, 2) => (None, &args[1..]),
                    (false, 2) | (true, 3) => (Some(&args[1]), &args[2..]),
                    _ => return Err(format!("{name}: falsche Argumentzahl")),
                };
                let llvm = element.llvm_type(self.context);
                let ptr = match index {
                    None => base,
                    Some(index) => {
                        let i = self.expr(index, Some(&STy::Int(NType::U64)))?;
                        self.expect(&i, &STy::Int(NType::U64), "Index")?;
                        // Nicht inbounds: Gueltigkeit liegt beim unsicheren Aufrufer.
                        unsafe { self.builder.build_gep(llvm, base, &[i.value.into_int_value()], "sys.element") }.map_err(e)?
                    }
                };
                let alignment = self.target.get_abi_alignment(&llvm);
                if write {
                    let value = self.expr(&rest[0], Some(&STy::Int(element)))?;
                    self.expect(&value, &STy::Int(element), "volatile_schreiben-Wert")?;
                    let store = self.builder.build_store(ptr, value.value).map_err(e)?;
                    store.set_volatile(true).map_err(e)?;
                    store.set_alignment(alignment).map_err(e)?;
                    return Ok(None);
                }
                let value = self.builder.build_load(llvm, ptr, "sys.volatile").map_err(e)?;
                let instruction = value.as_instruction_value().ok_or("Load fehlt")?;
                instruction.set_volatile(true).map_err(e)?;
                instruction.set_alignment(alignment).map_err(e)?;
                return Ok(Some(SVal { ty: STy::Int(element), value }));
            }
            "assembler" => return self.inline_asm(args, expected, statement),
            _ => {}
        }
        if let Some((read, bits)) = port_bits(name) {
            if !self.x86 { return Err("Port-I/O gibt es nur auf x86_64".into()); }
            if self.hosted {
                return Err("Systemkoerper: Privilegiertes Port-I/O ist im hosted Usermode (Linux/Windows) verboten, nur im Bare-Profil".into());
            }
            let ty = port_type(bits);
            if read {
                let [port] = args else { return Err(format!("{name} erwartet (port: u16)")) };
                let port = self.expr(port, Some(&STy::Int(NType::U16)))?;
                self.expect(&port, &STy::Int(NType::U16), "Port")?;
                let (code, constraints) = match bits {
                    8 => ("inb $1, $0", "={al},{dx},~{memory}"),
                    16 => ("inw $1, $0", "={ax},{dx},~{memory}"),
                    _ => ("inl $1, $0", "={eax},{dx},~{memory}"),
                };
                let value = self.asm(code, constraints, Some(ty.llvm_type(self.context)), &[port.value.into()])?
                    .ok_or("Port-Lesen liefert keinen Wert")?;
                return Ok(Some(SVal { ty: STy::Int(ty), value }));
            }
            let [port, value] = args else { return Err(format!("{name} erwartet (port: u16, wert)")) };
            let port = self.expr(port, Some(&STy::Int(NType::U16)))?;
            self.expect(&port, &STy::Int(NType::U16), "Port")?;
            let value = self.expr(value, Some(&STy::Int(ty)))?;
            self.expect(&value, &STy::Int(ty), "Portwert")?;
            let (code, constraints) = match bits {
                8 => ("outb $0, $1", "{al},{dx},~{memory}"),
                16 => ("outw $0, $1", "{ax},{dx},~{memory}"),
                _ => ("outl $0, $1", "{eax},{dx},~{memory}"),
            };
            self.asm(code, constraints, None, &[value.value.into(), port.value.into()])?;
            return Ok(None);
        }
        if let Some(kind) = barrier(name) {
            if !args.is_empty() { return Err("Barrieren erwarten keine Argumente".into()); }
            let code = match (self.x86, kind) {
                (_, 3) => "",
                (true, 0) => "mfence", (true, 1) => "lfence", (true, _) => "sfence",
                (false, 0) => "dmb sy", (false, 1) => "dmb ld", (false, _) => "dmb st",
            };
            self.asm(code, "~{memory}", None, &[])?;
            return Ok(None);
        }
        if let Some(callee) = self.callees.get(name).cloned() {
            if callee.params.iter().any(|ty| matches!(ty, STy::Struct(_)))
                || matches!(callee.result, Some(STy::Struct(_)))
            {
                let (call, sret) = self.call_system_value(name, &callee, args)?;
                return match sret {
                    None => self.call_result(call, callee.result.clone()),
                    Some((ptr, ty)) => {
                        // Als Aggregatwert nur im Rueckfall (struct_place greift
                        // sonst direkt auf den Puffer zu).
                        let alignment = self.struct_size_align(&ty)?.1;
                        let value = self.builder.build_load(
                            ty.llvm(self.context, self.layouts)?, ptr, "sys.direkt",
                        ).map_err(e)?;
                        value.as_instruction_value().ok_or("Systemkoerper: sret-Load fehlt")?
                            .set_alignment(alignment).map_err(e)?;
                        Ok(Some(SVal { ty, value }))
                    }
                };
            }
            let values = self.args_for(name, args, &callee.params)?;
            let call_args: Vec<BasicMetadataValueEnum<'ctx>> = values.iter().map(|v| v.value.into()).collect();
            let call = self.builder.build_call(callee.function, &call_args, if callee.result.is_some() { "sys.direkt" } else { "" }).map_err(e)?;
            call.set_call_convention(callee.function.get_call_conventions());
            self.nach_aufruf_pruefen(&callee)?;
            return self.call_result(call, callee.result.clone());
        }
        // P2-T50 B3: länge(p) / puffer_zeiger(p) lesen das Pufferaggregat.
        if let Some(ty) = self.puffer_builtin_typ(name, args) {
            let puffer_ty = self.infer(&args[0]).ok_or("Systemkoerper: Puffertyp fehlt")?;
            let puffer = self.expr(&args[0], Some(&puffer_ty))?;
            let feld = if matches!(ty, STy::Int(_)) { 1 } else { 0 };
            let wert = self.builder.build_extract_value(
                puffer.value.into_struct_value(), feld, "sys.puffer.feld",
            ).map_err(e)?;
            return Ok(Some(SVal { ty, value: wert }));
        }
        if let Some(STy::Ptr(identity)) = self.slots.get(name).map(|slot| slot.ty.clone()) {
            let NativeTypeId::Pointer { pointee, .. } = &identity else { unreachable!() };
            let NativeTypeId::Function { params, result, .. } = pointee.as_ref() else {
                return Err(format!("Systemkoerper: '{name}' ist kein Funktionszeiger"));
            };
            let param_tys = params.iter().map(|p| STy::from_identity(p)?.ok_or_else(|| "void-Parameter".to_string()))
                .collect::<Result<Vec<_>, _>>()?;
            let result_ty = STy::from_identity(result)?;
            let callee_ptr = self.expr(&Expr::Identifier(name.to_string()), None)?;
            let values = self.args_for(name, args, &param_tys)?;
            let signature = crate::ffi_types::FfiSignature::from_indirect_identity(self.triple, &identity, self.nominals)?;
            let callee = crate::ffi_types::TypedFfiValue { value: callee_ptr.value, identity: identity.clone() };
            let typed: Vec<crate::ffi_types::TypedFfiValue<'ctx>> = values.iter()
                .map(|v| crate::ffi_types::TypedFfiValue { value: v.value, identity: v.ty.identity() }).collect();
            let result = signature.emit_indirect_call(self.context, &self.builder, self.module, &callee, &typed, true)?;
            self.nach_fremdaufruf_pruefen()?;
            return Ok(match (result, result_ty) {
                (Some(value), Some(ty)) => Some(SVal { ty, value: value.value }),
                _ => None,
            });
        }
        // P2-T19/T56 (Plan 91033131): Struktur-ABI-Aufruf nach dem allgemeinen
        // Klassifizierer (ffi_abi); dieselbe Moo-Quelle fuer alle Ziel-ABIs.
        if let Some(binding) = self.externs.get_abi(name) {
            let param_tys = binding.param_types().iter()
                .map(|ty| STy::from_type_ref(ty, self.nominals))
                .collect::<Result<Vec<_>, _>>()?;
            let result_ty = match &binding.result_type().kind {
                TypeKind::Name(n) if n == "void" => None,
                _ => Some(STy::from_type_ref(binding.result_type(), self.nominals)?),
            };
            let values = self.args_for(name, args, &param_tys)?;
            let raw: Vec<BasicValueEnum<'ctx>> = values.iter().map(|value| value.value).collect();
            let binding = self.externs.get_abi(name)
                .ok_or("C-ABI: Deklaration waehrend der Ausgabe verschwunden")?;
            let wert = binding.emit_call(self.context, &self.builder, &raw)?;
            self.nach_fremdaufruf_pruefen()?;
            return Ok(match (result_ty, wert) {
                (Some(ty), Some(value)) => Some(SVal { ty, value }),
                _ => None,
            });
        }
        if let Some(binding) = self.externs.get(name) {
            let (params, result) = binding.semantic_types();
            let (params, result) = (params.to_vec(), result.clone());
            // P2-T66: 8/16-Bit-Ganzzahlen tragen an der Aufrufstelle dieselben
            // Extension-Attribute (signext/zeroext) wie Deklaration und skalarer
            // FFI-Pfad; die Zielregel kommt aus ffi_types (keine zweite ABI-Logik).
            let (param_ext, result_ext) = binding.extension_attributes();
            let param_tys = params.iter().map(|p| match STy::from_identity(p)? {
                Some(ty @ STy::Ptr(_)) => Ok(ty),
                Some(ty @ STy::Float(_)) => Ok(ty),
                Some(ty @ STy::Int(_)) => Ok(ty),
                Some(STy::Bool) => Ok(STy::Bool),
                _ => Err(format!("extern C '{name}': im Systemkoerper nur Ganzzahlen, bool, f32/f64 und Zeiger als Parameter")),
            }).collect::<Result<Vec<_>, _>>()?;
            let result_ty = STy::from_identity(&result)?;
            let function = binding.function();
            let values = self.args_for(name, args, &param_tys)?;
            let call_args: Vec<BasicMetadataValueEnum<'ctx>> = values.iter().map(|v| v.value.into()).collect();
            let call = self.builder.build_call(function, &call_args, if result_ty.is_some() { "sys.extern" } else { "" }).map_err(e)?;
            call.set_call_convention(function.get_call_conventions());
            for (index, ext) in param_ext.iter().enumerate() {
                if let Some(attr) = ext {
                    let kind = Attribute::get_named_enum_kind_id(attr);
                    call.add_attribute(AttributeLoc::Param(index as u32), self.context.create_enum_attribute(kind, 0));
                }
            }
            if let Some(attr) = result_ext {
                let kind = Attribute::get_named_enum_kind_id(attr);
                call.add_attribute(AttributeLoc::Return, self.context.create_enum_attribute(kind, 0));
            }
            self.nach_fremdaufruf_pruefen()?;
            return self.call_result(call, result_ty);
        }
        Err(format!("Systemkoerper: unbekannte Funktion '{name}'"))
    }

    /// assembler(...) wie im Hardwarekoerper, Operanden aber als Ausdruecke.
    fn inline_asm(&mut self, args: &[Expr], expected: Option<&STy>, statement: bool) -> Result<Option<SVal<'ctx>>, String> {
        let spec = crate::native_hardware::AsmSpec::parse(args)?;
        let arch = if self.x86 { "x86_64" } else { "aarch64" };
        if spec.target != arch {
            return Err(format!("assembler: ziel=\"{}\" passt nicht zum Modulziel {arch}", spec.target));
        }
        let mut constraints = Vec::new();
        let result_ty = match (&spec.output, statement, expected) {
            (Some(output), false, Some(STy::Int(ty))) => {
                let register = output.strip_prefix("=&").or_else(|| output.strip_prefix('='));
                if !register.is_some_and(|r| r == "r" || crate::native_hardware::register_constraint(r)) {
                    return Err(format!("assembler: ungültige Ausgangs-Constraint '{output}' (=r, =&r, ={{reg}})"));
                }
                constraints.push(output.clone());
                Some(*ty)
            }
            (Some(_), true, _) => return Err("assembler: ausgang= verlangt eine Verwendung des Ergebnisses".into()),
            (Some(_), false, _) => return Err("assembler: Ausgangstyp aus dem Kontext unbekannt (z. B. setze x: u64 auf assembler(...))".into()),
            (None, false, _) => return Err("assembler ohne ausgang= liefert keinen Wert".into()),
            (None, true, _) => None,
        };
        let outputs = usize::from(result_ty.is_some());
        let mut values: Vec<BasicMetadataValueEnum<'ctx>> = Vec::new();
        for (constraint, expr) in &spec.inputs {
            let tied = constraint.len() == 1 && constraint.parse::<usize>().is_ok_and(|n| n < outputs);
            if !(constraint == "r" || crate::native_hardware::register_constraint(constraint) || tied) {
                return Err(format!("assembler: ungültige Eingangs-Constraint '{constraint}' (r, {{reg}}, Ausgangsnummer)"));
            }
            let ty = if tied { STy::Int(result_ty.unwrap_or(NType::U64)) } else {
                self.infer(expr).ok_or("assembler: Typ eines Eingangs ist nicht bestimmbar (z. B. u64(...) verwenden)")?
            };
            let value = self.expr(expr, Some(&ty))?;
            if !matches!(value.ty, STy::Int(_) | STy::Ptr(_)) {
                return Err("assembler: Eingänge müssen Ganzzahlen oder Zeiger sein".into());
            }
            values.push(value.value.into());
            constraints.push(constraint.clone());
        }
        for clobber in &spec.clobbers {
            if clobber.is_empty() || !clobber.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit()) {
                return Err(format!("assembler: ungültiger Clobber '{clobber}'"));
            }
            constraints.push(format!("~{{{clobber}}}"));
        }
        crate::native_hardware::check_operands(&spec.code, outputs + spec.inputs.len())?;
        let value = self.asm(&spec.code, &constraints.join(","), result_ty.map(|t| t.llvm_type(self.context)), &values)?;
        Ok(match (result_ty, value) {
            (Some(ty), Some(value)) => Some(SVal { ty: STy::Int(ty), value }),
            _ => None,
        })
    }
}

/// Systemkoerper einer `@nativ`-Funktion ausgeben (Erkennung: is_system_body).
#[allow(clippy::too_many_arguments)]
pub(crate) fn emit_system_function<'ctx>(
    context: &'ctx Context,
    module: &Module<'ctx>,
    target: &TargetData,
    layouts: &NativeStructRegistry<'ctx>,
    nominals: &NominalTypes,
    function: FunctionValue<'ctx>,
    signature: &SystemSignature,
    param_names: &[String],
    body: &[Stmt],
    callees: &HashMap<String, NativeCallee<'ctx>>,
    externs: &crate::ffi_types::ExternRegistry<'ctx>,
    sicher: bool,
    export: bool,
) -> Result<(), String> {
    let triple = module.get_triple();
    let triple = triple.as_str().to_str().map_err(e)?.to_string();
    let (x86, hosted) = match triple.as_str() {
        "x86_64-unknown-none" | "x86_64-unknown-uefi" => (true, false),
        // P2-T66: hosted x86_64 Usermode (Windows seit P2-T56, Linux neu):
        // gleiche Sprachmittel, aber ohne privilegierte oder MMIO-Builtins.
        "x86_64-pc-windows-gnu" | "x86_64-pc-windows-msvc"
        | "x86_64-unknown-linux-gnu" | "x86_64-pc-linux-gnu" => (true, true),
        // P2-T66 (c): hosted aarch64 Linux (AAPCS64, keine Ext-Attribute an
        // extern C, FfiType::extension); Port-I/O/assembler/volatile bleiben gesperrt.
        "aarch64-unknown-linux-gnu" => (false, true),
        "aarch64-unknown-none" => (false, false),
        other => return Err(format!("Systemkoerper: Ziel '{other}' noch nicht freigegeben")),
    };
    if function.get_type() != signature.llvm_type(context, layouts)?
        || param_names.len() != signature.params.len()
        || function.count_basic_blocks() != 0
    {
        return Err("Systemkoerper: inkonsistente native Signatur".into());
    }
    let mut seen = HashSet::new();
    if param_names.iter().any(|name| !seen.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let me = NativeCallee::from_system(function, signature);
    // P2-T74: sicher erkennbares Funktionsende ohne Rueckgabe ist ein Compilefehler
    // (vor jeder IR-Emission, damit kein Teilmodul entsteht).
    if me.result.is_some() && !enthaelt_rueckgabe_oder_schleife(body) {
        let name = function.get_name().to_str().unwrap_or("?");
        let name = name.strip_prefix("__moo_native.").unwrap_or(name);
        return Err(format!(
            "Systemkoerper '{name}' hat einen Rueckgabetyp, erreicht aber ohne gib_zurück immer sein Ende"
        ));
    }
    // P2-T51 E5 (Plan f5b52a0b C1): Fluchtpruefung geliehener Zeiger, ebenfalls
    // vor jeder IR-Emission.
    let typisiert: Vec<(String, STy)> = param_names.iter().cloned().zip(signature.params.iter().cloned()).collect();
    let name = function.get_name().to_str().unwrap_or("?");
    let name = name.strip_prefix("__moo_native.").unwrap_or(name);
    if let Some((code, grund, zeile)) = flucht_verletzung(
        &typisiert, body, &|n| layouts.layout(n).is_some(), &|f, i| externs.behaelt(f, i),
    ) {
        // P2-T98 S2/S5: M-M002 Flucht bzw. M-M004 @behaelt, mit Zeile.
        return Err(crate::diagnose::vormerken(code, zeile, format!("Systemkoerper '{name}': {grund}")));
    }
    // P2-T98 S4 (M-M003): Nutzung nach Freigabe durch extern mit @verbraucht(n).
    if let Some((grund, zeile)) = freigabe_verletzung(body, &|f, i| externs.gibt_frei(f, i)) {
        return Err(crate::diagnose::vormerken("M-M003", zeile, format!("Systemkoerper '{name}': {grund}")));
    }
    let entry = context.append_basic_block(function, "entry");
    let entry_builder = context.create_builder();
    entry_builder.position_at_end(entry);
    let mut emitter = Emitter {
        context, module, target, layouts, nominals, triple: &triple, x86, hosted, function, entry,
        sret: None, builder: context.create_builder(), slots: HashMap::new(), loops: Vec::new(),
        trap: None, never: None, weiter: None, pow_fehler: None, result: me.result.clone(), callees, externs,
        sicher, unsicher_tiefe: 0, export, zeile: 0,
    };
    // Struktur-Rueckgabe: Parameter 0 ist der sret-Puffer des Aufrufers.
    let offset = u32::from(signature.returns_struct());
    if let Some(ty @ STy::Struct(_)) = &me.result {
        let llvm = ty.llvm(context, layouts)?;
        for attribute in struct_pointer_attributes(context, llvm, target, true) {
            function.add_attribute(AttributeLoc::Param(0), attribute);
        }
        emitter.sret = Some(function.get_nth_param(0).ok_or("sret-Parameter fehlt")?.into_pointer_value());
    }
    for (index, (name, ty)) in param_names.iter().zip(&me.params).enumerate() {
        let position = index as u32 + offset;
        let llvm = ty.llvm(context, layouts)?;
        let arg = function.get_nth_param(position).ok_or("Nativer Parameter fehlt")?;
        let ptr = if matches!(ty, STy::Struct(_)) {
            // Exklusive, ABI-ausgerichtete Aufruferkopie wird direkt zum Slot;
            // Zugriffe darauf sind daher so ausgerichtet wie auf eigene Allocas.
            for attribute in struct_pointer_attributes(context, llvm, target, false) {
                function.add_attribute(AttributeLoc::Param(position), attribute);
            }
            arg.into_pointer_value()
        } else {
            let ptr = entry_builder.build_alloca(llvm, name).map_err(e)?;
            let alignment = target.get_abi_alignment(&llvm);
            ptr.as_instruction().ok_or("Systemkoerper: Parameter-Alloca fehlt")?
                .set_alignment(alignment).map_err(e)?;
            entry_builder.build_store(ptr, arg).map_err(e)?
                .set_alignment(alignment).map_err(e)?;
            ptr
        };
        emitter.slots.insert(name.clone(), Slot { ty: ty.clone(), ptr, constant: false });
    }
    emitter.declare_locals(body, &entry_builder)?;
    let start = context.append_basic_block(function, "sys.start");
    entry_builder.build_unconditional_branch(start).map_err(e)?;
    emitter.builder.position_at_end(start);
    emitter.block(body)?;
    // Funktionsende ohne gib_zurück bei Rueckgabetyp: derselbe Fehlervertrag wie
    // Operationsfehler (ADR §3). Hosted Moo-intern fangbar bzw. RC 1 mit
    // Diagnose, Bare/UEFI und C-Exporte weiterhin llvm.trap.
    if emitter.open() {
        match emitter.result {
            None => { emitter.builder.build_return(None).map_err(e)?; }
            Some(_) if emitter.fangbar() => emitter.emit_hosted_fehler("Native Funktion endet ohne Rueckgabewert")?,
            Some(_) if emitter.abbruch() => emitter.emit_abbruch("Native Funktion endet ohne Rueckgabewert")?,
            Some(_) => emitter.emit_trap()?,
        }
    }
    Ok(())
}
