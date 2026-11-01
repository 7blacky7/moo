//! Native Kontrollfluss-Bausteine (P2-T12).
//!
//! Noch nicht an AST-Ausdruecke, native Funktionsrahmen oder Cleanup angebunden.
//! Der Aufrufer erzeugt Bedingungen und fuehrt vor Return/Fehler/Scope-Exit
//! notwendige Cleanup-Arbeit aus. Ein expliziter Fehlerblock darf nie in einen
//! Erfolgsblock zurueckfallen, der einen ungueltigen Wert verwendet.

use inkwell::basic_block::BasicBlock;
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::values::{BasicValue, FunctionValue};

use crate::codegen_native::{NType, NValue};

/// Die drei Bloecke einer kopfgesteuerten Schleife. Der Aufrufer generiert im
/// condition-Block nach jeder Iteration eine neue Bool-Bedingung. break fuehrt
/// nach exit, continue nach condition; Cleanup bleibt Aufgabe des Aufrufers.
#[derive(Clone, Copy, Debug)]
pub(crate) struct LoopBlocks<'ctx> {
    pub(crate) condition: BasicBlock<'ctx>,
    pub(crate) body: BasicBlock<'ctx>,
    pub(crate) exit: BasicBlock<'ctx>,
}

impl<'ctx> LoopBlocks<'ctx> {
    pub(crate) fn new(context: &'ctx Context, function: FunctionValue<'ctx>) -> Self {
        Self {
            condition: context.append_basic_block(function, "native.loop.condition"),
            body: context.append_basic_block(function, "native.loop.body"),
            exit: context.append_basic_block(function, "native.loop.exit"),
        }
    }
}

/// Verzweigung nur bei exakt nativem bool; Zahlen und dynamische Truthiness
/// werden nicht still in bool konvertiert. Beide Zielbloecke muessen zur selben
/// Funktion wie der aktive Block gehoeren.
pub(crate) fn branch_bool<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    condition: NValue<'ctx>,
    when_true: BasicBlock<'ctx>,
    when_false: BasicBlock<'ctx>,
) -> Result<(), String> {
    if condition.ty != NType::Bool || condition.value.get_type() != NType::Bool.llvm_type(context) {
        return Err("Native Bedingung muss bool sein".into());
    }
    let block = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
    let function = block.get_parent().ok_or("LLVM-Block ohne Funktion")?;
    if block.get_terminator().is_some() {
        return Err("Native Verzweigung nach Terminator".into());
    }
    if when_true.get_parent() != Some(function) || when_false.get_parent() != Some(function) {
        return Err("Verzweigungsziel gehoert zu anderer Funktion".into());
    }
    builder.build_conditional_branch(condition.value.into_int_value(), when_true, when_false)
        .map_err(|e| e.to_string())?;
    Ok(())
}

/// Haengt nur an einen offenen Block einen Sprung an. Ein bereits beendeter
/// Block (etwa nach return/break/continue) bleibt unveraendert; false bedeutet
/// dass kein neuer Sprung eingefuegt wurde.
pub(crate) fn branch_if_open<'ctx>(
    builder: &Builder<'ctx>,
    target: BasicBlock<'ctx>,
) -> Result<bool, String> {
    let block = builder.get_insert_block().ok_or("Kein aktiver LLVM-Block")?;
    let function = block.get_parent().ok_or("LLVM-Block ohne Funktion")?;
    if target.get_parent() != Some(function) {
        return Err("Sprungziel gehoert zu anderer Funktion".into());
    }
    if block.get_terminator().is_some() {
        return Ok(false);
    }
    builder.build_unconditional_branch(target).map_err(|e| e.to_string())?;
    Ok(true)
}

/// Exklusiver Bereich [current, end): keine Liste/MooValue-Konversion.
/// Signedness beider Integerwerte muss uebereinstimmen. Der Aufrufer darf
/// den Zaehler nur im true-Zweig erhoehen und prueft seinen Funktionsvertrag.
pub(crate) fn range_condition<'ctx>(
    context: &'ctx Context,
    builder: &Builder<'ctx>,
    current: NValue<'ctx>,
    end: NValue<'ctx>,
) -> Result<NValue<'ctx>, String> {
    let is_integer = matches!(current.ty,
        NType::I8 | NType::I16 | NType::I32 | NType::I64 |
        NType::U8 | NType::U16 | NType::U32 | NType::U64);
    if !is_integer || current.ty != end.ty
        || current.value.get_type() != current.ty.llvm_type(context)
        || end.value.get_type() != end.ty.llvm_type(context)
    {
        return Err("Range-Grenzen brauchen denselben nativen Integer-Typ".into());
    }
    let predicate = if current.ty.is_signed_integer() {
        inkwell::IntPredicate::SLT
    } else {
        inkwell::IntPredicate::ULT
    };
    let result = builder.build_int_compare(predicate,
        current.value.into_int_value(), end.value.into_int_value(), "native.range.test")
        .map_err(|e| e.to_string())?;
    NValue::checked(context, NType::Bool, result.into())
}
