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
use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, FunctionValue, IntValue, PointerValue};
use inkwell::{AddressSpace, IntPredicate};

use crate::ast::{BinOp, Expr, Stmt, TypeKind, TypeRef, UnaryOpKind};
use crate::codegen_native::{NType, NValue, NativeParameter, NativePointerSignature};
use crate::native_struct::NativeStructRegistry;
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
            _ => Self::from_ntype(NType::from_type_ref(ty)?),
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

    fn identity(&self) -> NativeTypeId {
        match self {
            STy::Ptr(identity) | STy::Struct(identity) => identity.clone(),
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
            STy::Struct(NativeTypeId::Nominal { name, .. }) => name.clone(),
            STy::Struct(identity) => format!("{identity:?}"),
        }
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
}

impl<'ctx> NativeCallee<'ctx> {
    pub(crate) fn new(function: FunctionValue<'ctx>, signature: &NativePointerSignature) -> Result<Self, String> {
        let params = signature.params.iter().map(|param| match param {
            NativeParameter::Scalar(ty) => STy::from_ntype(*ty),
            NativeParameter::Pointer { identity, .. } => Ok(STy::Ptr(identity.clone())),
        }).collect::<Result<Vec<_>, _>>()?;
        let result = signature.result.map(STy::from_ntype).transpose()?;
        Ok(Self { function, params, result })
    }

    /// Fuer reine Moo-zu-Moo-Aufrufe mit kompletten Strukturwerten.
    pub(crate) fn from_system(function: FunctionValue<'ctx>, signature: &SystemSignature) -> Self {
        Self { function, params: signature.params.clone(), result: signature.result.clone() }
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
        Ok(Self { params, result })
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

/// Nur Bestandsformen ausnehmen, die ein engerer Pfad bereits abdeckt.
pub(crate) fn is_system_body(body: &[Stmt], params: &[String]) -> bool {
    let inner = match body {
        [Stmt::UnsafeBlock { body }] => body.as_slice(),
        _ => return false,
    };
    match inner {
        [] => false,
        [Stmt::Return(Some(Expr::IndexAccess { .. }))] => false,
        [Stmt::Return(Some(Expr::FunctionCall { name, .. }))]
        | [Stmt::Expression(Expr::FunctionCall { name, .. })] => !params.contains(name),
        _ => true,
    }
}

/// P2-T74 (ADR §3, statisch erkennbar): Hat ein Koerper mit Rueckgabetyp kein
/// einziges gib_zurück und keine Schleife, erreicht jeder Aufruf das Funktions-
/// ende; emit_system_function lehnt ihn als Compilefehler ab. Koerper, die nur
/// auf manchen Pfaden zurueckkehren (wenn ohne sonst) oder Schleifen enthalten,
/// behalten das Laufzeitnetz (emit_hosted_fehler 'endet ohne Rueckgabewert'
/// bzw. Trap), weil der Weg zum Ende dort nicht statisch sicher ist.
fn enthaelt_rueckgabe_oder_schleife(body: &[Stmt]) -> bool {
    body.iter().any(|stmt| match stmt {
        Stmt::Return(_) | Stmt::While { .. } => true,
        Stmt::UnsafeBlock { body } => enthaelt_rueckgabe_oder_schleife(body),
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
        Stmt::UnsafeBlock { body } | Stmt::While { body, .. } =>
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
    result: Option<STy>,
    callees: &'a HashMap<String, NativeCallee<'ctx>>,
    externs: &'a crate::ffi_types::ExternRegistry<'ctx>,
}

fn e(err: impl ToString) -> String { err.to_string() }

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
        self.hosted && self.function.get_name().to_str().is_ok_and(|n| n.starts_with("__moo_native."))
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
        } else {
            self.emit_trap()?;
        }
        if let Some(current) = current { self.builder.position_at_end(current); }
        self.trap = Some(block);
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
            STy::Struct(_) =>
                ty.llvm(self.context, self.layouts)?.into_struct_type().const_zero().into(),
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
                Stmt::UnsafeBlock { body } | Stmt::While { body, .. } => self.declare_locals(body, entry)?,
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
            None => return Err(format!("Systemkoerper: '{name}' ist nicht deklariert (setze {name}: TYP auf ...)")),
        };
        if constant && !declaring {
            return Err(format!("Systemkoerper: Konstante '{name}' kann nicht neu gesetzt werden"));
        }
        if matches!(ty, STy::Struct(_)) {
            // Strukturwerte wandern als Speicherkopie, nicht als LLVM-Aggregat.
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
            Expr::FunctionCall { name, args } if self.layouts.layout(name).is_some() => {
                let built = self.struct_ty(name)?;
                if &built != ty {
                    return Err(format!("Systemkoerper: {what} erwartet {}, erhalten {}", ty.name(), built.name()));
                }
                let (ptr, alignment) = self.entry_alloca(ty, "sys.struct.neu")?;
                let place = Place { ptr, alignment, origin: Origin::Fresh };
                self.construct_struct_into(name, args, &place)?;
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
            Expr::FunctionCall { name, args } if self.callees.get(name)
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
            if matches!(field_ty, STy::Struct(_)) {
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

    /// Indexiert einen typisierten RAM-Zeiger. Anders als volatile_* ist das
    /// ein normaler Zugriff; fuer MMIO ist ausdruecklich volatile_* zu nutzen.
    /// Der Aufrufer im unsicher-Block verantwortet Gueltigkeit und Grenzen.
    fn pointer_index(&mut self, object: &Expr, index: &Expr) -> Result<(PointerValue<'ctx>, STy), String> {
        if let Some((address, field)) = self.array_field_index(object, index)? {
            return match field {
                FieldKind::Value(ty) => Ok((address, ty)),
                FieldKind::Embedded(name) => Ok((address, self.struct_ty(&name)?)),
                FieldKind::Array { .. } => Err("Systemkoerper: verschachtelte Arrays sind noch nicht nutzbar".into()),
            };
        }
        let ptr_ty = self.infer(object)
            .ok_or("Systemkoerper: Indexzugriff erfordert einen typisierten ptr[T]")?;
        let element = match &ptr_ty {
            STy::Ptr(NativeTypeId::Pointer { pointee, .. }) =>
                STy::from_identity(pointee)?
                    .ok_or("Systemkoerper: ptr[void] kann nicht indiziert werden")?,
            _ => return Err("Systemkoerper: Indexzugriff erfordert einen typisierten ptr[T]".into()),
        };
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
        let position = self.expr(index, Some(&STy::Int(NType::U64)))?;
        self.expect(&position, &STy::Int(NType::U64), "Arrayindex")?;
        let raw = position.value.into_int_value();
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
            if let Some((address, kind)) = self.array_field_index(base, index)? {
                return match kind {
                    FieldKind::Embedded(name) => Ok((address, name)),
                    FieldKind::Value(ty) => {
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

    fn stmt(&mut self, stmt: &Stmt) -> Result<(), String> {
        match stmt {
            Stmt::UnsafeBlock { body } => self.block(body),
            Stmt::Assignment { name, ty, value } => self.store(name, value, ty.is_some()),
            Stmt::ConstAssignment { name, ty: Some(_), value } => self.store(name, value, true),
            Stmt::PropertyAssignment { object, property, value } => {
                let alignment = self.local_field_alignment(object, property);
                let (ptr, ty) = self.pointer_property(object, property)?;
                if matches!(ty, STy::Struct(_)) {
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
            Stmt::IndexAssignment { object, index, value } => {
                let alignment = self.local_array_alignment(object);
                let (ptr, ty) = self.pointer_index(object, index)?;
                if matches!(ty, STy::Struct(_)) {
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
            Stmt::If { condition, body, else_body } => {
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
            Stmt::While { condition, body } => {
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
            Stmt::Break | Stmt::Continue => {
                let (condition, exit) = *self.loops.last()
                    .ok_or("Systemkoerper: stopp/weiter ausserhalb einer Schleife")?;
                let target = if matches!(stmt, Stmt::Break) { exit } else { condition };
                self.builder.build_unconditional_branch(target).map_err(e)?;
                Ok(())
            }
            Stmt::Return(value) => {
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
            Stmt::Expression(Expr::FunctionCall { name, args }) => {
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
                match self.infer(object) {
                    Some(STy::Ptr(NativeTypeId::Pointer { pointee, .. })) =>
                        STy::from_identity(&pointee).ok().flatten(),
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
                _ => self.infer(left).or_else(|| self.infer(right)),
            },
            Expr::FunctionCall { name, args } => {
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
                if let Some(Slot { ty: STy::Ptr(NativeTypeId::Pointer { pointee, .. }), .. }) = self.slots.get(name) {
                    if let NativeTypeId::Function { result, .. } = pointee.as_ref() {
                        return STy::from_identity(result).ok().flatten();
                    }
                }
                if let Some(binding) = self.externs.get_struct(name) {
                    return self.struct_ty(binding.struct_name()).ok();
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
                None => Err(format!("Systemkoerper: Typ der Zahl {text} ist aus dem Kontext nicht bestimmbar (u64(...) o. Ä. verwenden)")),
            },
            Expr::Boolean(value) => Ok(SVal { ty: STy::Bool, value: self.context.bool_type().const_int(u64::from(*value), false).into() }),
            Expr::None => match expected {
                Some(ty @ STy::Ptr(_)) => Ok(SVal { ty: ty.clone(), value: self.ptrt().const_null().into() }),
                _ => Err("Systemkoerper: nichts ist nur als Nullzeiger mit Zeiger-Zieltyp erlaubt".into()),
            },
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
                            _ => Err(format!("Systemkoerper: Typ von -{text} ist nicht bestimmbar")),
                        };
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
            Expr::FunctionCall { name, args } if self.layouts.layout(name).is_some() =>
                self.struct_constructor(name, args, expected),
            Expr::FunctionCall { name, args } => self.call(name, args, expected, false)?
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

    fn literal(&self, ty: NType, text: &str, negative: bool) -> Result<SVal<'ctx>, String> {
        let value = ty.integer_literal(self.context, text, negative)?;
        Ok(SVal { ty: STy::Int(ty), value: value.value })
    }

    /// Floats werden in der vom Quelltyp geforderten f32/f64-Breite gerundet.
    fn float_literal(&self, ty: NType, text: &str, negative: bool) -> Result<SVal<'ctx>, String> {
        let (value, underflow) = ty.float_literal(self.context, text, negative)?;
        if underflow {
            eprintln!("Warnung: Floatliteral '{text}' rundet auf null");
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

    fn binary(&mut self, left: &Expr, op: &BinOp, right: &Expr, expected: Option<&STy>) -> Result<SVal<'ctx>, String> {
        use crate::native_arithmetic::{CompareOp, IntOp};
        let compare = match op {
            BinOp::Eq => Some(CompareOp::Eq), BinOp::NotEq => Some(CompareOp::Ne),
            BinOp::Less => Some(CompareOp::Lt), BinOp::LessEq => Some(CompareOp::Le),
            BinOp::Greater => Some(CompareOp::Gt), BinOp::GreaterEq => Some(CompareOp::Ge),
            _ => None,
        };
        if let Some(cmp) = compare {
            let ty = self.infer(left).or_else(|| self.infer(right))
                .ok_or("Systemkoerper: Vergleich zweier Literale ist nicht bestimmbar")?;
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
        let float_ty = expected.cloned().or_else(|| self.infer(left)).or_else(|| self.infer(right));
        if let Some(STy::Float(t)) = float_ty {
            use crate::native_arithmetic::FloatOp;
            let op = match op {
                BinOp::Add => FloatOp::Add, BinOp::Sub => FloatOp::Sub,
                BinOp::Mul => FloatOp::Mul, BinOp::Div => FloatOp::Div,
                _ => return Err("Systemkoerper: Float-Operator wird nicht unterstuetzt".into()),
            };
            let l = self.expr(left, Some(&STy::Float(t)))?;
            let r = self.expr(right, Some(&STy::Float(t)))?;
            self.expect(&r, &l.ty, "Float-Rechnung")?;
            let result = crate::native_arithmetic::float_binary(
                self.context, &self.builder, op,
                NValue { ty: t, value: l.value }, NValue { ty: t, value: r.value },
            )?;
            return Ok(SVal { ty: STy::Float(t), value: result.value });
        }
        let int_op = match op {
            BinOp::Add => IntOp::Add, BinOp::Sub => IntOp::Sub, BinOp::Mul => IntOp::Mul,
            BinOp::Div => IntOp::Div, BinOp::Mod => IntOp::Rem,
            BinOp::BitAnd => IntOp::BitAnd, BinOp::BitOr => IntOp::BitOr, BinOp::BitXor => IntOp::BitXor,
            BinOp::LShift => IntOp::Shl, BinOp::RShift => IntOp::Shr,
            _ => return Err("Systemkoerper: Operator wird nicht unterstützt".into()),
        };
        let ty = match expected {
            Some(ty @ STy::Int(_)) => ty.clone(),
            _ => self.infer(left).or_else(|| if matches!(int_op, IntOp::Shl | IntOp::Shr) { None } else { self.infer(right) })
                .ok_or("Systemkoerper: Typ der Rechnung ist nicht bestimmbar")?,
        };
        let l = self.int_operand(left, Some(&ty))?;
        let right_ty = if matches!(int_op, IntOp::Shl | IntOp::Shr) {
            self.infer(right).unwrap_or_else(|| ty.clone())
        } else { ty.clone() };
        let r = self.int_operand(right, Some(&right_ty))?;
        // Nur Div/Rem/Shift koennen fehlen; andere Operationen bekommen den
        // nie erreichten Block (kein moo_native_fehler im IR).
        let failure = if matches!(int_op, IntOp::Div | IntOp::Rem | IntOp::Shl | IntOp::Shr) {
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

    fn args_for(&mut self, name: &str, args: &[Expr], params: &[STy]) -> Result<Vec<SVal<'ctx>>, String> {
        if args.len() != params.len() {
            return Err(format!("Systemkoerper: '{name}' erwartet {} Argumente, bekommen {}", params.len(), args.len()));
        }
        let mut values = Vec::with_capacity(args.len());
        for (index, (arg, ty)) in args.iter().zip(params).enumerate() {
            let value = self.expr(arg, Some(ty))?;
            self.expect(&value, ty, &format!("Argument {} von '{name}'", index + 1))?;
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
            return Err(format!("Systemkoerper: '{name}' erwartet {} Argumente, bekommen {}", callee.params.len(), args.len()));
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
                    self.expect(&value, other, &what)?;
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
        Ok((call, sret))
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
                STy::Struct(_) | STy::Float(_) => return Err(format!(
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
            return self.call_result(call, callee.result.clone());
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
            let signature = crate::ffi_types::FfiSignature::from_indirect_identity(self.triple, &identity, &NominalTypes::new())?;
            let callee = crate::ffi_types::TypedFfiValue { value: callee_ptr.value, identity: identity.clone() };
            let typed: Vec<crate::ffi_types::TypedFfiValue<'ctx>> = values.iter()
                .map(|v| crate::ffi_types::TypedFfiValue { value: v.value, identity: v.ty.identity() }).collect();
            let result = signature.emit_indirect_call(self.context, &self.builder, self.module, &callee, &typed, true)?;
            return Ok(match (result, result_ty) {
                (Some(value), Some(ty)) => Some(SVal { ty, value: value.value }),
                _ => None,
            });
        }
        // Identische Moo-Quellargumente fuer SysV und Win64. Die ABI-Klasse
        // (Register, sret, byval) wird aus dem Zieltriple entschieden.
        if let Some((struct_name, param_kinds)) = self.externs.get_struct(name)
            .map(|binding| (binding.struct_name().to_string(), binding.param_kinds().to_vec()))
        {
            let ty = self.struct_ty(&struct_name)?;
            let input_types: Vec<STy> = param_kinds.iter().map(|kind| match kind {
                crate::ffi_types::StructAbiParam::U64 => STy::Int(NType::U64),
                crate::ffi_types::StructAbiParam::Struct => ty.clone(),
            }).collect();
            let values = self.args_for(name, args, &input_types)?;
            let raw_values: Vec<BasicValueEnum<'ctx>> = values.iter()
                .map(|value| value.value).collect();
            let binding = self.externs.get_struct(name)
                .ok_or("C-ABI: Deklaration waehrend der Ausgabe verschwunden")?;
            let raw = binding.emit_roundtrip(self.context, &self.builder, &raw_values)?;
            return Ok(Some(SVal { ty, value: raw }));
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
                _ => Err(format!("extern C '{name}': im Systemkoerper nur Ganzzahlen, f32/f64 und Zeiger als Parameter")),
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
    let entry = context.append_basic_block(function, "entry");
    let entry_builder = context.create_builder();
    entry_builder.position_at_end(entry);
    let mut emitter = Emitter {
        context, module, target, layouts, nominals, triple: &triple, x86, hosted, function, entry,
        sret: None, builder: context.create_builder(), slots: HashMap::new(), loops: Vec::new(),
        trap: None, never: None, result: me.result.clone(), callees, externs,
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
            Some(_) => emitter.emit_trap()?,
        }
    }
    Ok(())
}
