//! Native raw pointers: non-owning, with their pointee kept at the language level.
//! Nullness, allocation, provenance and lifetime remain the unsafe caller's duty.
use inkwell::builder::Builder;
use inkwell::context::Context;
use inkwell::targets::TargetData;
use inkwell::types::{BasicType, BasicTypeEnum, IntType};
use inkwell::values::{BasicValue, BasicValueEnum, PointerValue};
use crate::ast::TypeRef;
use crate::codegen_native::{NType, NValue};
use crate::native_type_id::NativeTypeId;

/// Pointer slot with source-level type identity; neither slot nor pointer owns
/// the pointed-to storage. The caller controls the lifetime and insertion point.
pub(crate) struct NativePointerSlot<'ctx> {
    slot: PointerValue<'ctx>,
    identity: NativeTypeId,
}

impl<'ctx> NativePointerSlot<'ctx> {
    pub(crate) fn new(
        context: &'ctx Context, builder: &Builder<'ctx>,
        identity: NativeTypeId, value: PointerValue<'ctx>, name: &str,
    ) -> Result<Self, String> {
        if !matches!(&identity, NativeTypeId::Pointer { address_space: 0, .. }) {
            return Err("Nativer Zeigerslot erfordert einen Zeiger im Adressraum 0".into());
        }
        let ptr_type = context.ptr_type(inkwell::AddressSpace::default());
        if value.get_type() != ptr_type {
            return Err("Nativer Zeigerwert hat den falschen Adressraum".into());
        }
        let slot = builder.build_alloca(ptr_type, name).map_err(|e| e.to_string())?;
        builder.build_store(slot, value).map_err(|e| e.to_string())?;
        Ok(Self { slot, identity })
    }

    pub(crate) fn identity(&self) -> &NativeTypeId { &self.identity }

    pub(crate) fn load(
        &self, context: &'ctx Context, builder: &Builder<'ctx>, name: &str,
    ) -> Result<PointerValue<'ctx>, String> {
        let value = builder.build_load(context.ptr_type(inkwell::AddressSpace::default()), self.slot, name)
            .map_err(|e| e.to_string())?;
        match value {
            BasicValueEnum::PointerValue(pointer) => Ok(pointer),
            _ => Err("Nativer Zeigerslot lieferte keinen Zeiger".into()),
        }
    }

    pub(crate) fn store(
        &self, context: &'ctx Context, builder: &Builder<'ctx>,
        identity: &NativeTypeId, value: PointerValue<'ctx>,
    ) -> Result<(), String> {
        if identity != &self.identity {
            return Err("Zeigerzuweisung hat einen anderen sprachlichen Zieltyp".into());
        }
        if value.get_type() != context.ptr_type(inkwell::AddressSpace::default()) {
            return Err("Nativer Zeigerwert hat den falschen Adressraum".into());
        }
        builder.build_store(self.slot, value).map_err(|e| e.to_string())?;
        Ok(())
    }
}

pub(crate) struct NativePointer<'ctx> {
    pointer: PointerValue<'ctx>,
    pointee: BasicTypeEnum<'ctx>,
    alignment: u32,
    /// Full language type must not be lost to LLVM's opaque ptr.
    pub(crate) pointee_type: TypeRef,
}

impl<'ctx> NativePointer<'ctx> {
    /// Caller must supply the LLVM pointee corresponding to the full TypeRef;
    /// `guaranteed_alignment` is a caller-proven guarantee for this address, not
    /// merely the alignment of its pointee type. This does not establish ownership,
    /// non-nullness or valid provenance. The caller must also validate address
    /// space and language-level type identity: LLVM types alone cannot distinguish
    /// e.g. i8 and u8, including on loads and stores. LLVM volatile preserves the
    /// access but provides no atomicity, memory barriers or device bus contract.
    pub(crate) fn new(
        pointer: PointerValue<'ctx>,
        pointee: BasicTypeEnum<'ctx>,
        pointee_type: TypeRef,
        guaranteed_alignment: u32,
    ) -> Result<Self, String> {
        if !guaranteed_alignment.is_power_of_two() {
            return Err("Zeigeralignment muss eine positive Zweierpotenz sein".into());
        }
        if !pointee.is_sized() {
            return Err("Zeigerziel hat keine feste Größe".into());
        }
        Ok(Self { pointer, pointee, alignment: guaranteed_alignment, pointee_type })
    }

    fn access_alignment(&self, target: &TargetData, volatile: bool) -> Result<u32, String> {
        if !volatile { return Ok(1); }
        let width = match self.pointee {
            BasicTypeEnum::IntType(ty) => ty.get_bit_width(),
            _ => return Err("Volatile-MMIO erfordert einen Integer-Registertyp".into()),
        };
        if !matches!(width, 8 | 16 | 32 | 64) || self.alignment < target.get_abi_alignment(&self.pointee) {
            return Err("Volatile-MMIO erfordert passende Breite und nachgewiesenes Alignment".into());
        }
        Ok(self.alignment)
    }

    fn load(
        &self, builder: &Builder<'ctx>, target: &TargetData, unsafe_context: bool, volatile: bool, name: &str,
    ) -> Result<BasicValueEnum<'ctx>, String> {
        if !unsafe_context { return Err("Zeiger dereferenzieren erfordert unsicher".into()); }
        let alignment = self.access_alignment(target, volatile)?;
        let result = builder.build_load(self.pointee, self.pointer, name)
            .map_err(|e| e.to_string())?;
        let inst = result.as_instruction_value()
            .ok_or_else(|| "Zeiger-Ladevorgang ist keine LLVM-Instruktion".to_string())?;
        inst.set_alignment(alignment).map_err(|e| e.to_string())?;
        if volatile { inst.set_volatile(true).map_err(|e| e.to_string())?; }
        Ok(result)
    }

    /// Resolve and validate the full language pointee for the scalar slice.
    fn scalar_type(&self, context: &'ctx Context) -> Result<NType, String> {
        let ty = NType::from_type_ref(&self.pointee_type)?;
        if ty.llvm_type(context) != self.pointee {
            return Err("Zeigerziel stimmt nicht mit seinem Sprachtyp überein".into());
        }
        Ok(ty)
    }

    pub(crate) fn load_scalar(
        &self, context: &'ctx Context, builder: &Builder<'ctx>, target: &TargetData,
        unsafe_context: bool, volatile: bool, name: &str,
    ) -> Result<NValue<'ctx>, String> {
        let ty = self.scalar_type(context)?;
        let value = self.load(builder, target, unsafe_context, volatile, name)?;
        NValue::checked(context, ty, value)
    }

    pub(crate) fn store_scalar(
        &self, context: &'ctx Context, builder: &Builder<'ctx>, target: &TargetData,
        unsafe_context: bool, volatile: bool, value: NValue<'ctx>,
    ) -> Result<(), String> {
        let ty = self.scalar_type(context)?;
        if value.ty != ty {
            return Err("Zeigerwert passt nicht zum sprachlichen Zieltyp".into());
        }
        self.store(builder, target, unsafe_context, volatile, value.value)
    }

    fn store(
        &self, builder: &Builder<'ctx>, target: &TargetData, unsafe_context: bool, volatile: bool,
        value: BasicValueEnum<'ctx>,
    ) -> Result<(), String> {
        if !unsafe_context { return Err("Zeiger dereferenzieren erfordert unsicher".into()); }
        if value.get_type() != self.pointee {
            return Err("Zeigerwert passt nicht zum Zieltyp".into());
        }
        let alignment = self.access_alignment(target, volatile)?;
        let inst = builder.build_store(self.pointer, value).map_err(|e| e.to_string())?;
        inst.set_alignment(alignment).map_err(|e| e.to_string())?;
        if volatile { inst.set_volatile(true).map_err(|e| e.to_string())?; }
        Ok(())
    }
}

/// usize/isize use the selected target's default-address-space pointer width.
/// Other address spaces need their own explicit target contract.
pub(crate) fn target_pointer_int<'ctx>(
    context: &'ctx Context, target: &TargetData,
) -> Result<IntType<'ctx>, String> {
    let bytes = target.get_pointer_byte_size(None);
    if bytes != 4 && bytes != 8 {
        return Err(format!("Nicht unterstützte Ziel-Pointerbreite: {bytes} Bytes"));
    }
    Ok(if bytes == 4 { context.i32_type() } else { context.i64_type() })
}
