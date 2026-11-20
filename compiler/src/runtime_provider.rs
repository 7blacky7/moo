//! P10-T14: explizite Adapter zwischen Runtime-MooValue und nativen Speicherwerten.
//! Die normale extern-C-/@einstieg-Namensprüfung bleibt davon getrennt.
use std::collections::HashMap;
use inkwell::{AddressSpace, context::Context, module::{Linkage,Module}, targets::TargetData,
    types::{BasicMetadataTypeEnum,BasicType,BasicTypeEnum,FunctionType},
    values::{BasicMetadataValueEnum,BasicValue,FunctionValue}};
use crate::{ast::{DecoratorArg,Program,Stmt,TypeKind,TypeRef},
    codegen_native::NType, native_struct::NativeStructRegistry};

pub(crate) const SECTION: &str = ".data.moo_laufzeit";

pub(crate) fn enabled(program: &Program) -> bool {
    program.statements.iter().any(|s| matches!(s,
        Stmt::NativeConst { value:Some(_), decorators, .. } if decorators.iter().any(|d|
            matches!(d.name.as_str(),"sektion"|"section")
                && matches!(d.args.as_slice(),[DecoratorArg::Text(t)] if t==SECTION))))
}
fn err(s: impl std::fmt::Display) -> String { format!("Bare-Runtime-Provider: {s}") }
fn mv<'c>(c:&'c Context) -> inkwell::types::StructType<'c> {
    c.struct_type(&[c.i64_type().into(),c.i64_type().into()],false)
}
fn slot<'c>(c:&'c Context,l:&NativeStructRegistry<'c>,t:&TypeRef) -> Result<Option<BasicTypeEnum<'c>>,String> {
    match &t.kind {
        TypeKind::Name(n) if n=="void" => Ok(None),
        TypeKind::Name(n) if n=="LaufzeitWert" => {
            let v=l.layout(n).ok_or_else(||err("LaufzeitWert fehlt"))?;
            let fields=v.fields.iter().all(|(_,t)|matches!(&t.kind,TypeKind::Name(n) if n=="u64"));
            if v.size!=16 || v.alignment!=8 || v.offsets!=[0,8] || v.fields.len()!=2 || !fields || !v.bitfelder.is_empty() {
                return Err(err("LaufzeitWert muss exakt {u64,u64}, Größe16/Ausrichtung8/Versätze0,8 sein"));
            }
            Ok(Some(mv(c).into()))
        }
        TypeKind::Name(n) => NType::from_name(n).map(|t|Some(t.llvm_type(c)))
            .ok_or_else(||err(format!("nicht unterstützter Provider-Typ '{n}'"))),
        TypeKind::Generic {name,args} if matches!(name.as_str(),"ptr"|"zeiger") && args.len()==1 =>
            Ok(Some(c.ptr_type(AddressSpace::default()).into())),
        _ => Err(err("Provider-Signatur erlaubt Skalare, Zeiger und LaufzeitWert")),
    }
}
fn shape<'c>(c:&'c Context,l:&NativeStructRegistry<'c>,inputs:&[TypeRef],result:&TypeRef)
    -> Result<(FunctionType<'c>,Vec<BasicTypeEnum<'c>>,Option<BasicTypeEnum<'c>>),String>
{
    let p=inputs.iter().map(|t|slot(c,l,t)?.ok_or_else(||err("void-Parameter")))
        .collect::<Result<Vec<_>,_>>()?;
    let r=slot(c,l,result)?;
    let params=p.iter().map(|t|(*t).into()).collect::<Vec<BasicMetadataTypeEnum>>();
    let f=match r {Some(t)=>t.fn_type(&params,false),None=>c.void_type().fn_type(&params,false)};
    Ok((f,p,r))
}
fn memory_type<'c>(c:&'c Context,p:&[BasicTypeEnum<'c>],r:Option<BasicTypeEnum<'c>>) -> FunctionType<'c> {
    let ptr=c.ptr_type(AddressSpace::default());
    let mut args=Vec::<BasicMetadataTypeEnum>::new();
    if r==Some(mv(c).into()) { args.push(ptr.into()); }
    args.extend(p.iter().map(|t| -> BasicMetadataTypeEnum<'c> {if *t==mv(c).into(){ptr.into()}else{(*t).into()}}));
    match r {
        Some(t) if t!=mv(c).into()=>t.fn_type(&args,false),
        _=>c.void_type().fn_type(&args,false),
    }
}
fn memory_export<'c>(c:&'c Context,name:&str,p:&[BasicTypeEnum<'c>],r:Option<BasicTypeEnum<'c>>) -> bool {
    let ptr:BasicTypeEnum<'c>=c.ptr_type(AddressSpace::default()).into();
    let size:BasicTypeEnum<'c>=c.i64_type().into();
    match name {
        "malloc" => p==[size] && r==Some(ptr),
        "free" => p==[ptr] && r.is_none(),
        "realloc" => p==[ptr,size] && r==Some(ptr),
        "calloc" => p==[size,size] && r==Some(ptr),
        _ => false,
    }
}

fn raw_function<'c>(m:&Module<'c>,name:&str,ty:FunctionType<'c>) -> Result<FunctionValue<'c>,String> {
    if m.get_global(name).is_some(){return Err(err(format!("Datensymbol '{name}' statt Funktion")));}
    if let Some(f)=m.get_function(name) {
        if f.get_type()!=ty || f.get_call_conventions()!=0 {return Err(err(format!("ABI von '{name}' stimmt nicht überein")));}
        Ok(f)
    } else {Ok(m.add_function(name,ty,None))}
}
fn align_slot(v:inkwell::values::PointerValue<'_>) -> Result<(),String> {
    v.as_instruction_value().ok_or_else(||err("Alloca fehlt"))?.set_alignment(8).map_err(err)
}
fn from_native<'c>(c:&'c Context,_m:&Module<'c>,outer:FunctionValue<'c>,inner:FunctionValue<'c>,
    p:&[BasicTypeEnum<'c>],r:Option<BasicTypeEnum<'c>>) -> Result<(),String>
{
    if inner.get_type()!=memory_type(c,p,r) || inner.get_call_conventions()!=0 {
        return Err(err(format!("interne Signatur von '{}' stimmt nicht überein",inner.get_name().to_string_lossy())));
    }
    if outer.count_basic_blocks()!=0 {return Err(err(format!("'{}' ist bereits definiert",outer.get_name().to_string_lossy())));}
    let b=c.create_builder();b.position_at_end(c.append_basic_block(outer,"entry"));
    let mut args=Vec::<BasicMetadataValueEnum>::new();
    let output=if r==Some(mv(c).into()) {
        let a=b.build_alloca(mv(c),"provider.result").map_err(err)?;align_slot(a)?;
        args.push(a.into());Some(a)
    } else {None};
    for (i,t) in p.iter().enumerate() {
        let v=outer.get_nth_param(i as u32).ok_or_else(||err("Argument fehlt"))?;
        if *t==mv(c).into() {
            let a=b.build_alloca(mv(c),"provider.arg").map_err(err)?;align_slot(a)?;
            b.build_store(a,v).map_err(err)?.set_alignment(8).map_err(err)?;
            args.push(a.into());
        } else {args.push(v.into());}
    }
    let call=b.build_call(inner,&args,"provider.call").map_err(err)?;
    match output {
        Some(a)=>{
            let v=b.build_load(mv(c),a,"provider.value").map_err(err)?;
            v.as_instruction_value().ok_or_else(||err("Load fehlt"))?.set_alignment(8).map_err(err)?;
            b.build_return(Some(&v)).map_err(err)?;
        }
        None=>match call.try_as_basic_value(){
            inkwell::values::ValueKind::Basic(v)=>{b.build_return(Some(&v)).map_err(err)?;}
            _=>{b.build_return(None).map_err(err)?;}
        }
    }
    Ok(())
}
fn to_runtime<'c>(c:&'c Context,m:&Module<'c>,raw:FunctionValue<'c>,name:&str,
    p:&[BasicTypeEnum<'c>],r:Option<BasicTypeEnum<'c>>) -> Result<FunctionValue<'c>,String>
{
    let alias=format!("__moo_runtime_helper.{name}");
    if m.get_function(&alias).is_some(){return Err(err(format!("doppelter Helper '{name}'")));}
    let f=m.add_function(&alias,memory_type(c,p,r),Some(Linkage::Internal));
    let b=c.create_builder();b.position_at_end(c.append_basic_block(f,"entry"));
    let output=r==Some(mv(c).into());let mut n=u32::from(output);
    let mut args=Vec::<BasicMetadataValueEnum>::new();
    for t in p {
        let v=f.get_nth_param(n).ok_or_else(||err("Helper-Argument fehlt"))?;n+=1;
        if *t==mv(c).into() {
            let value=b.build_load(mv(c),v.into_pointer_value(),"helper.value").map_err(err)?;
            value.as_instruction_value().ok_or_else(||err("Helper-Load fehlt"))?.set_alignment(8).map_err(err)?;
            args.push(value.into());
        } else {args.push(v.into());}
    }
    let call=b.build_call(raw,&args,"helper.call").map_err(err)?;
    match call.try_as_basic_value(){
        inkwell::values::ValueKind::Basic(v) if output=>{
            b.build_store(f.get_nth_param(0).ok_or_else(||err("Helper-Rückgabepuffer fehlt"))?.into_pointer_value(),v)
                .map_err(err)?.set_alignment(8).map_err(err)?;
            b.build_return(None).map_err(err)?;
        }
        inkwell::values::ValueKind::Basic(v)=>{b.build_return(Some(&v)).map_err(err)?;}
        _=>{b.build_return(None).map_err(err)?;}
    }
    Ok(f)
}

/// Wird nur für das explizit gekennzeichnete x86_64-Bare-Profil aufgerufen.
/// Keine Namensänderungen oder Signatur-Bitcasts; alle Übergänge haben Bodies.
pub(crate) fn bind<'c>(c:&'c Context,m:&Module<'c>,target:&TargetData,program:&Program,
    native:&HashMap<String,FunctionValue<'c>>,layouts:&NativeStructRegistry<'c>) -> Result<(),String>
{
    if !enabled(program){return Ok(());}
    if m.get_triple().as_str().to_string_lossy()!="x86_64-unknown-none" {
        return Err(err("Thread-Träger ist zunächst nur für x86_64-bare freigegeben"));
    }
    for s in &program.statements {
        match s {
            Stmt::FunctionDef{name,param_types,return_type,decorators,..} if name.starts_with("kern_rt_")=>{
                let runtime=name.strip_prefix("kern_rt_").unwrap();
                if (!runtime.starts_with("moo_") && !matches!(runtime,"malloc"|"free"|"realloc"|"calloc")) || !decorators.iter().any(|d|matches!(d.name.as_str(),"nativ"|"native"))
                    || decorators.iter().any(|d|matches!(d.name.as_str(),"einstieg"|"entry")) {
                    return Err(err(format!("Implementierung '{name}' muss intern und @nativ sein")));
                }
                if program.statements.iter().any(|s|matches!(s,Stmt::FunctionDef{name,..} if name==runtime)){
                    return Err(err(format!("zusätzliche Definition von '{runtime}'")));
                }
                let inputs=param_types.iter().cloned().map(|t|t.ok_or_else(||err("Parametertyp fehlt"))).collect::<Result<Vec<_>,_>>()?;
                let result=return_type.as_ref().ok_or_else(||err("Rückgabetyp fehlt"))?;
                let (ty,p,r)=shape(c,layouts,&inputs,result)?;
                if !runtime.starts_with("moo_") && !memory_export(c,runtime,&p,r) {
                    return Err(err(format!("ungültige Speicher-ABI für '{runtime}'")));
                }
                let raw=raw_function(m,runtime,ty)?;
                let implementation=*native.get(name).ok_or_else(||err(format!("native Implementierung '{name}' fehlt")))?;
                from_native(c,m,raw,implementation,&p,r)?;
            }
            Stmt::ExternC{name,params,result,decorators,..} if name.starts_with("kern_c_")=>{
                let runtime=name.strip_prefix("kern_c_").unwrap();
                if !runtime.starts_with("moo_"){return Err(err(format!("Helper '{name}' ist kein Moo-Runtime-Symbol")));}
                let inputs=params.iter().map(|(_,t)|t.clone()).collect::<Vec<_>>();
                let (ty,p,r)=shape(c,layouts,&inputs,result)?;
                let raw=raw_function(m,runtime,ty)?;
                let inner=to_runtime(c,m,raw,name,&p,r)?;
                let declaration=m.get_function(name).ok_or_else(||err(format!("Helper-Deklaration '{name}' fehlt")))?;
                let convention=crate::ffi_types::decorator_call_convention(decorators,"x86_64-unknown-none")?;
                if convention!=0{return Err(err("Runtime-Helper benötigt die Zielstandard-ABI"));}
                crate::ffi_types::emit_abi_export_for_declaration(c,m,target,name,inner,&inputs,result,layouts,
                    convention,Linkage::External,None,declaration)?;
            }
            _=>{}
        }
    }
    Ok(())
}


/// Skalarer C-Einstieg nach dem tatsächlichen GS-/Planerstart.
/// Nur das Vollprofil stellt die beiden nativen Lebensdauer-Endpunkte bereit.
pub(crate) fn entry<'c>(c:&'c Context,m:&Module<'c>) -> Result<(),String> {
    let Some(begin)=m.get_function("__moo_native.laufzeit_programm_beginnen") else { return Ok(()); };
    let end=m.get_function("__moo_native.laufzeit_programm_enden").ok_or_else(||err("Programm-Endpunkt fehlt"))?;
    let main=m.get_function("main").ok_or_else(||err("Moo-main fehlt"))?;
    let i32_fn=c.i32_type().fn_type(&[],false);
    if begin.get_type()!=c.void_type().fn_type(&[],false) || end.get_type()!=i32_fn || main.get_type()!=i32_fn {
        return Err(err("Programm-Endpunkte benötigen void()/i32()"));
    }
    let outer=raw_function(m,"moo_laufzeit_programm",i32_fn)?;
    if outer.count_basic_blocks()!=0 { return Err(err("moo_laufzeit_programm ist bereits definiert")); }
    let b=c.create_builder(); b.position_at_end(c.append_basic_block(outer,"entry"));
    b.build_call(begin,&[],"").map_err(err)?;
    let result=b.build_call(main,&[],"programm").map_err(err)?.try_as_basic_value().basic()
        .ok_or_else(||err("Moo-main ohne Ergebnis"))?.into_int_value();
    let status=b.build_call(end,&[],"laufzeit").map_err(err)?.try_as_basic_value().basic()
        .ok_or_else(||err("Programm-Endpunkt ohne Ergebnis"))?.into_int_value();
    let code=b.build_or(result,status,"status").map_err(err)?;
    b.build_return(Some(&code)).map_err(err)?;
    Ok(())
}
