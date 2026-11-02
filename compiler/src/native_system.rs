//! P7-T5: Systemkoerper fuer `@nativ`-Funktionen im Bare-/UEFI-Profil.
//!
//! Ein Systemkoerper ist ein `unsicher:`-Block mit allgemeinen Anweisungen
//! (Bisher konnten `@nativ`-Funktionen nur genau einen Aufruf, einen
//! Zeigerzugriff oder eine Folge von Hardware-Intrinsics enthalten.)
//! Er entsteht nur im Bare-Profil und nur, wenn kein engerer Bestandspfad
//! (Hardware, indirekter Ein-Aufruf ueber Parameter, Zeigerlesen) passt.
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
//!   extern "C"-Funktionen mit 32/64-Bit-Ganzzahlen und Zeigern.
//! Erreicht der Kontrollfluss das Ende einer Funktion mit Rueckgabetyp, folgt
//! ein Trap. Speicherzugriffe tragen keine Ausrichtungsannahme (align 1).

use std::collections::{HashMap, HashSet};

use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::module::Module;
use inkwell::targets::TargetData;
use inkwell::types::{BasicMetadataTypeEnum, BasicType, BasicTypeEnum};
use inkwell::values::{BasicMetadataValueEnum, BasicValue, BasicValueEnum, FunctionValue, IntValue, PointerValue};
use inkwell::{AddressSpace, IntPredicate};

use crate::ast::{BinOp, Expr, Stmt, TypeKind, TypeRef, UnaryOpKind};
use crate::codegen_native::{NType, NValue, NativeParameter, NativePointerSignature};
use crate::native_type_id::{resolve_type, NativeTypeId, NominalTypes, ScalarType};

/// Typ eines Wertes im Systemkoerper.
#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) enum STy {
    Int(NType),
    Bool,
    Ptr(NativeTypeId),
}

impl STy {
    fn from_type_ref(ty: &TypeRef) -> Result<Self, String> {
        match &ty.kind {
            TypeKind::Generic { name, .. } if name == "ptr" || name == "zeiger" => {
                let identity = resolve_type(ty, &NominalTypes::new())?;
                Ok(STy::Ptr(identity))
            }
            _ => Self::from_ntype(NType::from_type_ref(ty)?),
        }
    }

    fn from_ntype(ty: NType) -> Result<Self, String> {
        match ty {
            NType::Bool => Ok(STy::Bool),
            NType::F32 | NType::F64 => Err("Systemkoerper: Fliesskommatypen sind nicht freigegeben".into()),
            other => Ok(STy::Int(other)),
        }
    }

    fn from_identity(identity: &NativeTypeId) -> Result<Option<Self>, String> {
        Ok(Some(match identity {
            NativeTypeId::Void => return Ok(None),
            NativeTypeId::Pointer { .. } => STy::Ptr(identity.clone()),
            NativeTypeId::Scalar(scalar) => match scalar {
                ScalarType::I8 => STy::Int(NType::I8), ScalarType::I16 => STy::Int(NType::I16),
                ScalarType::I32 => STy::Int(NType::I32), ScalarType::I64 => STy::Int(NType::I64),
                ScalarType::U8 => STy::Int(NType::U8), ScalarType::U16 => STy::Int(NType::U16),
                ScalarType::U32 => STy::Int(NType::U32), ScalarType::U64 => STy::Int(NType::U64),
                ScalarType::Bool => STy::Bool,
                _ => return Err("Systemkoerper: Typ nicht freigegeben".into()),
            },
            _ => return Err("Systemkoerper: Typ nicht freigegeben".into()),
        }))
    }

    fn identity(&self) -> NativeTypeId {
        match self {
            STy::Ptr(identity) => identity.clone(),
            STy::Bool => NativeTypeId::Scalar(ScalarType::Bool),
            STy::Int(ty) => NativeTypeId::Scalar(match ty {
                NType::I8 => ScalarType::I8, NType::I16 => ScalarType::I16,
                NType::I32 => ScalarType::I32, NType::I64 => ScalarType::I64,
                NType::U8 => ScalarType::U8, NType::U16 => ScalarType::U16,
                NType::U32 => ScalarType::U32, _ => ScalarType::U64,
            }),
        }
    }

    fn llvm<'ctx>(&self, context: &'ctx Context) -> BasicTypeEnum<'ctx> {
        match self {
            STy::Int(ty) => ty.llvm_type(context),
            STy::Bool => context.bool_type().into(),
            STy::Ptr(_) => context.ptr_type(AddressSpace::default()).into(),
        }
    }

    fn name(&self) -> String {
        match self {
            STy::Int(ty) => format!("{ty:?}").to_lowercase(),
            STy::Bool => "bool".into(),
            STy::Ptr(identity) => format!("{identity:?}"),
        }
    }
}

#[derive(Clone, Debug)]
struct SVal<'ctx> {
    ty: STy,
    value: BasicValueEnum<'ctx>,
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

struct Emitter<'a, 'ctx> {
    context: &'ctx Context,
    module: &'a Module<'ctx>,
    target: &'a TargetData,
    triple: &'a str,
    x86: bool,
    function: FunctionValue<'ctx>,
    builder: Builder<'ctx>,
    slots: HashMap<String, Slot<'ctx>>,
    loops: Vec<(BasicBlock<'ctx>, BasicBlock<'ctx>)>,
    trap: Option<BasicBlock<'ctx>>,
    result: Option<STy>,
    callees: &'a HashMap<String, NativeCallee<'ctx>>,
    externs: &'a crate::ffi_types::ExternRegistry<'ctx>,
}

fn e(err: impl ToString) -> String { err.to_string() }

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

    fn trap_block(&mut self) -> Result<BasicBlock<'ctx>, String> {
        if let Some(block) = self.trap { return Ok(block); }
        let current = self.builder.get_insert_block();
        let block = self.context.append_basic_block(self.function, "sys.trap");
        self.builder.position_at_end(block);
        self.emit_trap()?;
        if let Some(current) = current { self.builder.position_at_end(current); }
        self.trap = Some(block);
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

    fn zero(&self, ty: &STy) -> BasicValueEnum<'ctx> {
        match ty {
            STy::Int(t) => t.llvm_type(self.context).into_int_type().const_zero().into(),
            STy::Bool => self.context.bool_type().const_zero().into(),
            STy::Ptr(_) => self.ptrt().const_null().into(),
        }
    }

    fn declare_locals(&mut self, body: &[Stmt], entry: &Builder<'ctx>) -> Result<(), String> {
        for stmt in body {
            match stmt {
                Stmt::Assignment { name, ty: Some(ty), .. } | Stmt::ConstAssignment { name, ty: Some(ty), .. } => {
                    if self.slots.contains_key(name) {
                        return Err(format!("Systemkoerper: '{name}' ist bereits deklariert"));
                    }
                    let sty = STy::from_type_ref(ty).map_err(|err| format!("Systemkoerper: '{name}': {err}"))?;
                    let ptr = entry.build_alloca(sty.llvm(self.context), name).map_err(e)?;
                    entry.build_store(ptr, self.zero(&sty)).map_err(e)?;
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

    fn stmt(&mut self, stmt: &Stmt) -> Result<(), String> {
        match stmt {
            Stmt::UnsafeBlock { body } => self.block(body),
            Stmt::Assignment { name, ty, value } => self.store(name, value, ty.is_some()),
            Stmt::ConstAssignment { name, ty: Some(_), value } => self.store(name, value, true),
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
                if let Some(ty) = conversion(name) { return Some(STy::Int(ty)); }
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
                let value = self.builder.build_load(slot.ty.llvm(self.context), slot.ptr, name).map_err(e)?;
                Ok(SVal { ty: slot.ty.clone(), value })
            }
            Expr::UnaryOp { op, operand } => match op {
                UnaryOpKind::Neg => {
                    if let Expr::NativeNumber { text, .. } = operand.as_ref() {
                        return match expected {
                            Some(STy::Int(ty)) => self.literal(*ty, text, true),
                            _ => Err(format!("Systemkoerper: Typ von -{text} ist nicht bestimmbar")),
                        };
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
            Expr::FunctionCall { name, args } => self.call(name, args, expected, false)?
                .ok_or_else(|| format!("Systemkoerper: '{name}' liefert keinen Wert")),
            _ => Err("Systemkoerper: Ausdruck wird nicht unterstützt".into()),
        }
    }

    fn literal(&self, ty: NType, text: &str, negative: bool) -> Result<SVal<'ctx>, String> {
        let value = ty.integer_literal(self.context, text, negative)?;
        Ok(SVal { ty: STy::Int(ty), value: value.value })
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
            let nt = match ty { STy::Int(t) => t, _ => NType::Bool };
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
        let failure = self.trap_block()?;
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
                STy::Int(source) => {
                    let raw = value.value.into_int_value();
                    if bits_of(*source) == bits_of(target) { raw }
                    else { self.builder.build_int_cast_sign_flag(raw, llvm, source.is_signed_integer(), "sys.konv").map_err(e)? }
                }
            };
            return Ok(Some(SVal { ty: STy::Int(target), value: converted.into() }));
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
                let [Expr::Identifier(symbol)] = args else { return Err("adresse_von(...) erwartet genau einen Namen".into()) };
                let pointer = if let Some(slot) = self.slots.get(symbol) {
                    slot.ptr
                } else if let Some(callee) = self.callees.get(symbol) {
                    callee.function.as_global_value().as_pointer_value()
                } else if let Some(global) = self.module.get_global(symbol) {
                    global.as_pointer_value()
                } else if let Some(function) = self.module.get_function(symbol) {
                    function.as_global_value().as_pointer_value()
                } else {
                    return Err(format!("adresse_von: '{symbol}' ist weder Local, Parameter, native Konstante noch Funktion"));
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
        if let Some(binding) = self.externs.get(name) {
            let (params, result) = binding.semantic_types();
            let (params, result) = (params.to_vec(), result.clone());
            let param_tys = params.iter().map(|p| match STy::from_identity(p)? {
                Some(ty @ STy::Ptr(_)) => Ok(ty),
                Some(STy::Int(t)) if bits_of(t) >= 32 => Ok(STy::Int(t)),
                _ => Err(format!("extern C '{name}': im Systemkoerper nur 32/64-Bit-Ganzzahlen und Zeiger als Parameter")),
            }).collect::<Result<Vec<_>, _>>()?;
            let result_ty = STy::from_identity(&result)?;
            let function = binding.function();
            let values = self.args_for(name, args, &param_tys)?;
            let call_args: Vec<BasicMetadataValueEnum<'ctx>> = values.iter().map(|v| v.value.into()).collect();
            let call = self.builder.build_call(function, &call_args, if result_ty.is_some() { "sys.extern" } else { "" }).map_err(e)?;
            call.set_call_convention(function.get_call_conventions());
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
            if matches!(value.ty, STy::Bool) {
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
    function: FunctionValue<'ctx>,
    signature: &NativePointerSignature,
    param_names: &[String],
    body: &[Stmt],
    callees: &HashMap<String, NativeCallee<'ctx>>,
    externs: &crate::ffi_types::ExternRegistry<'ctx>,
) -> Result<(), String> {
    let triple = module.get_triple();
    let triple = triple.as_str().to_str().map_err(e)?.to_string();
    let x86 = match triple.as_str() {
        "x86_64-unknown-none" | "x86_64-unknown-uefi" => true,
        "aarch64-unknown-none" => false,
        other => return Err(format!("Systemkoerper: Ziel '{other}' noch nicht freigegeben")),
    };
    if function.get_type() != signature.llvm_type(context)
        || param_names.len() != signature.params.len()
        || function.count_basic_blocks() != 0
    {
        return Err("Systemkoerper: inkonsistente native Signatur".into());
    }
    let mut seen = HashSet::new();
    if param_names.iter().any(|name| !seen.insert(name)) {
        return Err("Doppelter nativer Parametername".into());
    }
    let me = NativeCallee::new(function, signature)?;
    let entry = context.append_basic_block(function, "entry");
    let entry_builder = context.create_builder();
    entry_builder.position_at_end(entry);
    let mut emitter = Emitter {
        context, module, target, triple: &triple, x86, function,
        builder: context.create_builder(), slots: HashMap::new(), loops: Vec::new(),
        trap: None, result: me.result.clone(), callees, externs,
    };
    for (index, (name, ty)) in param_names.iter().zip(&me.params).enumerate() {
        let ptr = entry_builder.build_alloca(ty.llvm(context), name).map_err(e)?;
        let arg = function.get_nth_param(index as u32).ok_or("Nativer Parameter fehlt")?;
        entry_builder.build_store(ptr, arg).map_err(e)?;
        emitter.slots.insert(name.clone(), Slot { ty: ty.clone(), ptr, constant: false });
    }
    emitter.declare_locals(body, &entry_builder)?;
    let start = context.append_basic_block(function, "sys.start");
    entry_builder.build_unconditional_branch(start).map_err(e)?;
    emitter.builder.position_at_end(start);
    emitter.block(body)?;
    if emitter.open() {
        match emitter.result {
            None => { emitter.builder.build_return(None).map_err(e)?; }
            Some(_) => emitter.emit_trap()?,
        }
    }
    Ok(())
}
