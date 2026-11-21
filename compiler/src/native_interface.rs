//! Native Schnittstellen: geprüfte Strukturmethoden, fette Zeiger und SysV-Vtables.
//! Die beiden Zeiger werden über den bestehenden nativen Strukturwert-ABI geführt.
mod strukturtypen;

use std::collections::{BTreeMap, BTreeSet, HashMap};
use crate::ast::{self, Decorator, DecoratorArg, Expr, InterfaceSignature, Program, Stmt, TypeKind, TypeRef};
use crate::native_type_id::{resolve_type, NominalTypes};

#[derive(Default)]
pub(crate) struct NativeInterfaces {
    methods: HashMap<(String, String), String>,
    dispatch: HashMap<(String, String), String>,
    conversions: HashMap<(String, String), String>,
    dynamic: BTreeSet<String>,
    vtables: BTreeSet<String>,
    adapters: BTreeSet<String>,
}

impl NativeInterfaces {
    pub(crate) fn method(&self, ty: &str, method: &str) -> Option<&str> {
        self.methods.get(&(ty.into(), method.into()))
            .or_else(|| self.dispatch.get(&(ty.into(), method.into()))).map(String::as_str)
    }
    pub(crate) fn conversion(&self, target: &str, source: &str) -> Option<&str> {
        self.conversions.get(&(target.into(), source.into())).map(String::as_str)
    }
    pub(crate) fn is_dynamic(&self, ty: &str) -> bool { self.dynamic.contains(ty) }
    pub(crate) fn has_vtables(&self) -> bool { !self.vtables.is_empty() }
    pub(crate) fn is_vtable(&self, name: &str) -> bool { self.vtables.contains(name) }
    pub(crate) fn is_adapter(&self, name: &str) -> bool { self.adapters.contains(name) }
    pub(crate) fn dispatch_targets(&self) -> impl Iterator<Item = &String> { self.dispatch.values() }
}

fn named(name: &str) -> TypeRef {
    TypeRef { kind: TypeKind::Name(name.into()), line: 0, column: 0, end_line: 0, end_column: 0 }
}
fn ptr(ty: TypeRef) -> TypeRef {
    TypeRef { kind: TypeKind::Generic { name: "ptr".into(), args: vec![ty] }, ..named("") }
}
fn fat_name(name: &str) -> String { format!("ptr[dyn {name}]") }
fn table_name(name: &str) -> String { format!("__moo_vtable[{name}]") }
fn id(name: &str) -> Expr { Expr::Identifier(name.into()) }
fn call(name: &str, args: Vec<Expr>) -> Expr {
    Expr::FunctionCall { name: name.into(), args, type_args: Vec::new(), zeile: 0, spalten: (0,0) }
}
fn property(object: Expr, name: &str) -> Expr {
    Expr::PropertyAccess { object: Box::new(object), property: name.into() }
}
fn native() -> Decorator { Decorator { name: "nativ".into(), args: Vec::new() } }
fn definition(name: &str, params: Vec<(String, TypeRef)>, result: TypeRef, body: Vec<Stmt>) -> Stmt {
    Stmt::FunctionDef { zeile: 0, name: name.into(),
        params: params.iter().map(|p| p.0.clone()).collect(),
        param_types: params.iter().map(|p| Some(p.1.clone())).collect(),
        return_type: Some(result), defaults: vec![None; params.len()],
        body: vec![Stmt::UnsafeBlock { zeile: 0, body }], decorators: vec![native()] }
}
fn returned(expr: Expr, result: &TypeRef) -> Vec<Stmt> {
    if matches!(&result.kind, TypeKind::Name(n) if n=="void") {
        vec![Stmt::Expression(expr,0), Stmt::Return(None,0)]
    } else { vec![Stmt::Return(Some(expr),0)] }
}
fn structure(name: &str, fields: Vec<(String,TypeRef)>) -> Stmt {
    Stmt::StructDef { zeile: 0, name: name.into(), fields, decorators: Vec::new(),
        bitfelder: Vec::new(), varianten: Vec::new(), typparameter: Vec::new(), interfaces: Vec::new(), methods: Vec::new() }
}
fn error(line: usize, text: impl std::fmt::Display) -> String {
    crate::diagnose::vormerken("M-C000", line, format!("Native Schnittstelle: {text}"))
}

/// Gemeinsame Absenkung nativer generischer Werttypen vor Layout/Signaturen.
#[derive(Default)]
struct LoweredTypes {
    interfaces: BTreeSet<String>,
    strukturen: BTreeMap<String, Vec<(String, TypeRef)>>,
    strukturtypen: strukturtypen::Strukturtypen,
    nominals: NominalTypes,
}
impl LoweredTypes {
    fn insert(&mut self, name: String) { self.interfaces.insert(name); }
    fn extend(&mut self, names: impl IntoIterator<Item=String>) { self.interfaces.extend(names); }
}

/// Native generische Wertformen werden vor der Layoutprüfung nominalisiert.
/// Der interne Name ist kein deklarierbarer Bezeichner und bewahrt die Diagnoseform.
fn lower_type(ty: &mut TypeRef, used: &mut LoweredTypes) -> Result<(),String> {
    if strukturtypen::typ(ty, used)? { return Ok(()); }
    if let TypeKind::Generic { name, args } = &mut ty.kind {
        if name == "ergebnis" {
            if args.len() != 2 { return Err(crate::diagnose::vormerken("M-C000", ty.line, "Nativer Ergebnistyp: ergebnis[T, F] benötigt genau zwei Typen".into())); }
            for arg in args.iter_mut() { lower_type(arg, used)?; }
            let value = args[0].clone();
            let failure = args[1].clone();
            let identity = resolve_type(ty, &used.nominals).map_err(|e|
                crate::diagnose::vormerken("M-C000", ty.line, format!("Nativer Ergebnistyp: {e}")))?;
            let name = identity.to_string();
            let mut fields = vec![("__tag".into(), named("u64"))];
            for (label, field) in [("__wert", value), ("__fehler", failure)] {
                if !matches!(&field.kind, TypeKind::Name(n) if n == "void") {
                    fields.push((label.into(), field));
                }
            }
            used.strukturen.entry(name.clone()).or_insert(fields);
            used.nominals.insert(name.clone(), ("native_generic".into(), name.clone()));
            ty.kind = TypeKind::Name(name);
            return Ok(());
        }
    }
    if let TypeKind::Generic { name, args } = &ty.kind {
        if matches!(name.as_str(),"ptr"|"zeiger") {
            if let [target] = args.as_slice() {
                if let TypeKind::Generic { name: dyn_name, args: dyn_args } = &target.kind {
                    if dyn_name=="dyn" {
                        let [TypeRef { kind: TypeKind::Name(interface), .. }] = dyn_args.as_slice() else {
                            return Err(error(ty.line,"ptr[dyn S] erwartet genau einen Schnittstellennamen"));
                        };
                        used.insert(interface.clone());
                        let fat = fat_name(interface);
                        used.nominals.insert(fat.clone(), ("native_generic".into(), fat.clone()));
                        ty.kind=TypeKind::Name(fat);
                        return Ok(());
                    }
                }
            }
        }
        if name=="dyn" { return Err(error(ty.line,"dyn S ist nur als ptr[dyn S] erlaubt")); }
    }
    match &mut ty.kind {
        TypeKind::Generic { args, .. } => for arg in args { lower_type(arg,used)?; },
        TypeKind::Array { element,length } => { lower_type(element,used)?; lower_expr(length,used)?; }
        TypeKind::Function { params,return_type,.. } => {
            for param in params { lower_type(param,used)?; }
            lower_type(return_type,used)?;
        }
        TypeKind::Name(_) => {}
    }
    Ok(())
}
fn optional(ty: &mut Option<TypeRef>, used: &mut LoweredTypes) -> Result<(),String> {
    if let Some(ty)=ty { lower_type(ty,used)?; } Ok(())
}
fn lower_expr(expr: &mut Expr, used: &mut LoweredTypes) -> Result<(),String> {
    match expr {
        Expr::FunctionCall { name,type_args,args,.. } => {
            strukturtypen::layout_argument(name, args, used)?;
            for ty in type_args { lower_type(ty,used)?; }
            for arg in args { lower_expr(arg,used)?; }
        }
        Expr::MethodCall { object,args,.. } => {
            lower_expr(object,used)?; for arg in args { lower_expr(arg,used)?; }
        }
        Expr::BinaryOp { left,right,.. } | Expr::Range { start:left,end:right }
        | Expr::NullishCoalesce { left,right } | Expr::Pipe { left,right } => {
            lower_expr(left,used)?; lower_expr(right,used)?;
        }
        Expr::UnaryOp { operand,.. } | Expr::Spread(operand)
        | Expr::PropertyAccess { object:operand,.. } | Expr::OptionalChain { object:operand,.. } => lower_expr(operand,used)?,
        Expr::IndexAccess { object,index } => { lower_expr(object,used)?; lower_expr(index,used)?; }
        Expr::List(items) | Expr::New { args:items,.. } => for e in items { lower_expr(e,used)?; },
        Expr::Dict(items) => for (k,v) in items { lower_expr(k,used)?; lower_expr(v,used)?; },
        Expr::ListComprehension { expr,iterable,condition,.. } => {
            lower_expr(expr,used)?; lower_expr(iterable,used)?;
            if let Some(e)=condition { lower_expr(e,used)?; }
        }
        Expr::Lambda { param_types,body,.. } => {
            for ty in param_types { optional(ty,used)?; } lower_expr(body,used)?;
        }
        Expr::Ternary { condition,then_val,else_val } => {
            lower_expr(condition,used)?; lower_expr(then_val,used)?; lower_expr(else_val,used)?;
        }
        Expr::MatchExpr { value,cases } => {
            lower_expr(value,used)?;
            for (p,g,e) in cases {
                if let Some(p)=p { lower_expr(p,used)?; }
                if let Some(g)=g { lower_expr(g,used)?; }
                lower_expr(e,used)?;
            }
        }
        Expr::QueryExpr { source,where_cond,order_expr,select_expr,.. } => {
            lower_expr(source,used)?;
            if let Some(e)=where_cond { lower_expr(e,used)?; }
            if let Some(e)=order_expr { lower_expr(e,used)?; }
            lower_expr(select_expr,used)?;
        }
        _=>{}
    }
    Ok(())
}
fn lower_statements(statements: &mut [Stmt], used: &mut LoweredTypes) -> Result<(),String> {
    for stmt in statements {
        match stmt {
            Stmt::FunctionDef { param_types,return_type,defaults,body,.. } => {
                for ty in param_types { optional(ty,used)?; }
                optional(return_type,used)?;
                for e in defaults.iter_mut().flatten() { lower_expr(e,used)?; }
                lower_statements(body,used)?;
            }
            Stmt::StructDef { typparameter, fields,methods,interfaces,zeile,.. } => {
                if !typparameter.is_empty() { return Err(error(*zeile, "Strukturvorlage gehört auf Modulebene")); }
                used.extend(interfaces.iter().cloned());
                for (_,ty) in fields { lower_type(ty,used)?; }
                lower_statements(methods,used)?;
            }
            Stmt::InterfaceDef { signatures,.. } => {
                for sig in signatures.iter_mut().flatten() {
                    for ty in &mut sig.param_types { optional(ty,used)?; }
                    optional(&mut sig.return_type,used)?;
                    for e in sig.defaults.iter_mut().flatten() { lower_expr(e,used)?; }
                }
            }
            Stmt::ExternC { params,result,.. } => {
                for (_,ty) in params { lower_type(ty,used)?; } lower_type(result,used)?;
            }
            Stmt::Assignment { ty,value,.. } | Stmt::ConstAssignment { ty,value,.. }
            | Stmt::RawDataU32 { ty,value,.. } => { optional(ty,used)?; lower_expr(value,used)?; }
            Stmt::NativeConst { ty,value,.. } => {
                lower_type(ty,used)?; if let Some(e)=value { lower_expr(e,used)?; }
            }
            Stmt::Show(e,_) | Stmt::Throw(e,_) | Stmt::Expression(e,_) | Stmt::Defer(e,_)
            | Stmt::Expect(e,_) | Stmt::CompoundAssignment { value:e,.. } => lower_expr(e,used)?,
            Stmt::Return(Some(e),_) => lower_expr(e,used)?,
            Stmt::PropertyAssignment { object,value,.. } => { lower_expr(object,used)?; lower_expr(value,used)?; }
            Stmt::IndexAssignment { object,index,value,.. } => {
                lower_expr(object,used)?; lower_expr(index,used)?; lower_expr(value,used)?;
            }
            Stmt::If { condition,body,else_body,.. } => {
                lower_expr(condition,used)?; lower_statements(body,used)?; lower_statements(else_body,used)?;
            }
            Stmt::While { condition,body,.. } => { lower_expr(condition,used)?; lower_statements(body,used)?; }
            Stmt::For { iterable,body,.. } | Stmt::ParallelFor { iterable,body,.. } => {
                lower_expr(iterable,used)?; lower_statements(body,used)?;
            }
            Stmt::ClassDef { body,.. } | Stmt::UnsafeBlock { body,.. } | Stmt::TestDef { body,.. } => lower_statements(body,used)?,
            Stmt::TryCatch { try_body,catch_body,.. } => {
                lower_statements(try_body,used)?; lower_statements(catch_body,used)?;
            }
            Stmt::Match { value,cases,.. } => {
                lower_expr(value,used)?;
                for (p,g,b) in cases {
                    if let Some(p)=p { lower_expr(p,used)?; }
                    if let Some(g)=g { lower_expr(g,used)?; }
                    lower_statements(b,used)?;
                }
            }
            Stmt::Guard { condition,else_body,.. } => { lower_expr(condition,used)?; lower_statements(else_body,used)?; }
            Stmt::Precondition { condition,.. } | Stmt::Postcondition { condition,.. } => lower_expr(condition,used)?,
            Stmt::DataClassDef { field_types,.. } => for ty in field_types { optional(ty,used)?; },
            Stmt::Export(inner,_) => lower_statements(std::slice::from_mut(inner.as_mut()),used)?,
            _=>{}
        }
    }
    Ok(())
}

#[derive(Clone)]
struct Method {
    name: String,
    inputs: Vec<TypeRef>,
    result: TypeRef,
    line: usize,
}
/// Bytepuffer bleiben in der Moo-Signatur geliehene Sichten. Nur die
/// interne Vtable-ABI zerlegt den neuen Parameter in Daten und Länge.
fn byte_buffer(ty: &TypeRef) -> bool {
    matches!(&ty.kind, TypeKind::Generic { name, args }
        if matches!(name.as_str(), "puffer" | "buffer") && args.len() == 1
            && matches!(&args[0].kind, TypeKind::Name(n) if n == "u8"))
}
impl Method {
    fn pointer_type(&self) -> TypeRef {
        let mut params=vec![ptr(named("u8"))];
        for ty in &self.inputs {
            if byte_buffer(ty) {
                params.extend([ptr(named("u8")), named("u64")]);
            } else { params.push(ty.clone()); }
        }
        ptr(TypeRef { kind: TypeKind::Function { params,return_type:Box::new(self.result.clone()),
            convention:Some("sysv64".into()) },..named("") })
    }
    fn params(&self, receiver: TypeRef) -> Vec<(String,TypeRef)> {
        let mut params=vec![("__empfaenger".into(),receiver)];
        params.extend(self.inputs.iter().enumerate().map(|(i,t)|(format!("__arg{i}"),t.clone())));
        params
    }
    fn abi_params(&self, receiver: TypeRef) -> Vec<(String,TypeRef)> {
        let mut params=vec![("__empfaenger".into(),receiver)];
        for (i, ty) in self.inputs.iter().enumerate() {
            if byte_buffer(ty) {
                params.extend([(format!("__arg{i}_daten"), ptr(named("u8"))),
                    (format!("__arg{i}_laenge"), named("u64"))]);
            } else { params.push((format!("__arg{i}"), ty.clone())); }
        }
        params
    }
    fn abi_arguments(&self, receiver: Expr) -> Vec<Expr> {
        let mut args=vec![receiver];
        for (i, ty) in self.inputs.iter().enumerate() {
            let arg=id(&format!("__arg{i}"));
            if byte_buffer(ty) {
                args.extend([call("puffer_zeiger",vec![arg.clone()]), call("länge",vec![arg])]);
            } else { args.push(arg); }
        }
        args
    }
    fn method_arguments(&self, receiver: Expr) -> Vec<Expr> {
        let mut args=vec![receiver];
        for (i, ty) in self.inputs.iter().enumerate() {
            args.push(if byte_buffer(ty) {
                call("byteansicht",vec![id(&format!("__arg{i}_daten")),id(&format!("__arg{i}_laenge"))])
            } else { id(&format!("__arg{i}")) });
        }
        args
    }
}
fn prototype(interface: &str, sig: &InterfaceSignature, line: usize) -> Result<Method,String> {
    if sig.params.is_empty() || sig.params.len()!=sig.param_types.len()
        || sig.defaults.iter().any(Option::is_some) {
        return Err(error(line,format!("{interface}.{} braucht einen Empfänger und keine Defaultargumente",sig.name)));
    }
    if let Some(receiver)=&sig.param_types[0] {
        let self_pointer=matches!(&receiver.kind,TypeKind::Generic { name,args }
            if matches!(name.as_str(),"ptr"|"zeiger") && args.len()==1
                && matches!(&args[0].kind,TypeKind::Name(n) if n=="Self"||n=="selbst"));
        if !self_pointer {
            return Err(error(receiver.line,format!("{interface}.{}: Empfänger ohne Typ oder als ptr[Self] deklarieren",sig.name)));
        }
    }
    let inputs=sig.param_types.iter().skip(1).enumerate().map(|(i,t)|t.clone()
        .ok_or_else(||error(line,format!("{interface}.{}: Typ von Argument {} fehlt",sig.name,i+2))))
        .collect::<Result<Vec<_>,_>>()?;
    let result=sig.return_type.clone().ok_or_else(||error(line,format!("{interface}.{}: Rückgabetyp fehlt",sig.name)))?;
    if inputs.iter().any(|t|!byte_buffer(t) && !crate::ffi_types::rueckruf_wert_zulaessig(t))
        || (!matches!(&result.kind,TypeKind::Name(n) if n=="void") && !crate::ffi_types::rueckruf_wert_zulaessig(&result)) {
        return Err(error(line,format!("{interface}.{}: Vtable benötigt eine fn[sysv64]-Wertsignatur (Skalare, Zeiger oder Strukturwerte)",sig.name)));
    }
    Ok(Method { name:sig.name.clone(),inputs,result,line })
}

pub(crate) fn prepare(source: &Program) -> Result<(Program,NativeInterfaces),String> {
    let mut program=source.clone();
    let mut used=LoweredTypes::default();
    used.strukturtypen.vorlagen_sammeln(&mut program.statements)?;
    for stmt in &program.statements {
        if let Stmt::StructDef { name, .. } = stmt {
            used.nominals.insert(name.clone(), ("native_generic".into(), name.clone()));
        }
    }
    for name in crate::native_struct::VECMAT_NAMEN.iter().chain(crate::native_simd::NAMEN.iter()) {
        used.nominals.entry((*name).into()).or_insert_with(|| ("native_generic".into(), (*name).into()));
    }
    lower_statements(&mut program.statements,&mut used)?;
    program.statements.extend(used.strukturtypen.instanzen());
    for (name, fields) in &used.strukturen {
        program.statements.push(structure(name, fields.clone()));
    }
    let mut registry=NativeInterfaces::default();
    let mut lifted=Vec::new();
    let mut implementations=Vec::new();
    let mut method_defs=BTreeMap::<(String,String),Stmt>::new();
    for stmt in &mut program.statements {
        if let Stmt::StructDef { name,fields,interfaces,methods,.. }=stmt {
            let mut seen=BTreeSet::new();
            for interface in interfaces.iter() {
                if !seen.insert(interface.clone()) { return Err(error(0,format!("{name} implementiert {interface} mehrfach"))); }
                implementations.push((name.clone(),interface.clone()));
            }
            for mut method in std::mem::take(methods) {
                let Stmt::FunctionDef { name:method_name,params,param_types,decorators,zeile,.. }=&mut method else { unreachable!() };
                let original=method_name.clone();
                if params.is_empty() || param_types.len()!=params.len() {
                    return Err(error(*zeile,format!("{name}.{original}: Empfänger fehlt")));
                }
                if fields.iter().any(|(f,_)| f==&original) || method_defs.contains_key(&(name.clone(),original.clone())) {
                    return Err(error(*zeile,format!("{name}.{original}: Feld/Methode mehrfach deklariert")));
                }
                if decorators.iter().any(|d|matches!(d.name.as_str(),"einstieg"|"entry"|"rueckruf"|"rückruf"|"callback")) {
                    return Err(error(*zeile,format!("{name}.{original}: ABI-Exporte gehören an freie Funktionen")));
                }
                if param_types[0].is_none() { param_types[0]=Some(ptr(named(name))); }
                *method_name=ast::native_method_name(name,&original);
                registry.methods.insert((name.clone(),original.clone()),method_name.clone());
                method_defs.insert((name.clone(),original),method.clone());
                lifted.push(method);
            }
        }
    }
    let mut definitions=Vec::new();
    let mut nominal=used.nominals.clone();
    for stmt in &program.statements {
        if let Stmt::StructDef { name,.. }=stmt { nominal.insert(name.clone(),("native_interface".into(),name.clone())); }
    }
    for name in &used.interfaces {
        for n in [fat_name(name),table_name(name)] { nominal.insert(n.clone(),("native_interface".into(),n)); }
    }
    for ((owner,method),definition) in &method_defs {
        let Stmt::FunctionDef { param_types,zeile,.. }=definition else { unreachable!() };
        let receiver=param_types[0].as_ref().ok_or_else(||error(*zeile,"Empfängertyp fehlt"))?;
        let actual=resolve_type(receiver,&nominal).map_err(|e|error(receiver.line,e))?;
        let expected=resolve_type(&ptr(named(owner)),&nominal).map_err(|e|error(*zeile,e))?;
        if actual!=expected {
            return Err(error(receiver.line,format!("{owner}.{method} Empfänger: erwartet {expected}, erhalten {actual}")));
        }
    }
    let mut prototypes=BTreeMap::<String,Vec<Method>>::new();
    for interface in &used.interfaces {
        let defs=program.statements.iter().filter_map(|s|match s {
            Stmt::InterfaceDef { name,signatures,zeile,.. } if name==interface=>Some((signatures,*zeile)),_=>None
        }).collect::<Vec<_>>();
        if defs.len()!=1 { return Err(error(0,format!("'{interface}' ist keine eindeutig deklarierte Schnittstelle"))); }
        let (signatures,line)=defs[0];
        if signatures.is_empty() { return Err(error(line,format!("{interface} braucht mindestens eine native Methode"))); }
        let mut methods=Vec::new(); let mut names=BTreeSet::new();
        for sig in signatures {
            let sig=sig.as_ref().ok_or_else(||error(line,format!("{interface}: native Methode benötigt eine vollständige Signatur")))?;
            if !names.insert(sig.name.clone()) { return Err(error(line,format!("{interface}.{} mehrfach deklariert",sig.name))); }
            methods.push(prototype(interface,sig,line)?);
        }
        let fat=fat_name(interface); let table=table_name(interface);
        registry.dynamic.insert(fat.clone());
        definitions.push(structure(&fat,vec![("__daten".into(),ptr(named("u8"))),("__tabelle".into(),ptr(named(&table)))]));
        definitions.push(structure(&table,methods.iter().map(|m|(m.name.clone(),m.pointer_type())).collect()));
        for method in &methods {
            let dispatch=format!("__moo_dispatch.{interface}.{}",method.name);
            registry.dispatch.insert((fat.clone(),method.name.clone()),dispatch.clone());
            let mut body=vec![Stmt::Assignment { zeile:0,name:"__aufruf".into(),ty:Some(method.pointer_type()),
                value:property(property(id("__empfaenger"),"__tabelle"),&method.name) }];
            body.extend(returned(call("__aufruf",method.abi_arguments(property(id("__empfaenger"),"__daten"))),&method.result));
            definitions.push(definition(&dispatch,method.params(named(&fat)),method.result.clone(),body));
        }
        prototypes.insert(interface.clone(),methods);
    }
    for (ty,interface) in implementations {
        let methods=prototypes.get(&interface).ok_or_else(||error(0,format!("Schnittstelle '{interface}' fehlt")))?;
        let fat=fat_name(&interface); let table=table_name(&interface);
        let global=format!("__moo_vtable.{ty}.{interface}");
        let mut slots=Vec::new();
        for prototype in methods {
            let original=method_defs.get(&(ty.clone(),prototype.name.clone()))
                .ok_or_else(||error(prototype.line,format!("{ty} implementiert {interface}, aber Methode '{}' fehlt",prototype.name)))?;
            let Stmt::FunctionDef { param_types,return_type,name,zeile,.. }=original else { unreachable!() };
            let expected=std::iter::once(ptr(named(&ty))).chain(prototype.inputs.clone()).collect::<Vec<_>>();
            if param_types.len()!=expected.len() {
                return Err(error(*zeile,format!("{ty}.{}: {} Parameter erwartet, {} erhalten",prototype.name,expected.len(),param_types.len())));
            }
            for (index,(actual,expected)) in param_types.iter().zip(&expected).enumerate() {
                let actual=actual.as_ref().ok_or_else(||error(*zeile,format!("{ty}.{}: Parametertyp {} fehlt",prototype.name,index+1)))?;
                let a=resolve_type(actual,&nominal).map_err(|e|error(actual.line,e))?;
                let e=resolve_type(expected,&nominal).map_err(|e|error(expected.line,e))?;
                if a!=e { return Err(error(actual.line,format!("{ty}.{} Parameter {}: erwartet {e}, erhalten {a}",prototype.name,index+1))); }
            }
            let actual=return_type.as_ref().ok_or_else(||error(*zeile,format!("{ty}.{}: Rückgabetyp fehlt",prototype.name)))?;
            let a=resolve_type(actual,&nominal).map_err(|e|error(actual.line,e))?;
            let e=resolve_type(&prototype.result,&nominal).map_err(|e|error(prototype.line,e))?;
            if a!=e { return Err(error(actual.line,format!("{ty}.{} Rückgabe: erwartet {e}, erhalten {a}",prototype.name))); }
            let bridge=format!("__moo_vtable_adapter.{ty}.{interface}.{}",prototype.name);
            registry.adapters.insert(bridge.clone());
            let receiver=call("zeiger",vec![call("u64",vec![id("__empfaenger")])]);
            let body=returned(call(name,prototype.method_arguments(receiver)),&prototype.result);
            let mut function=definition(&bridge,prototype.abi_params(ptr(named("u8"))),prototype.result.clone(),body);
            if let Stmt::FunctionDef { decorators,.. }=&mut function {
                decorators.push(Decorator { name:"rueckruf".into(),args:Vec::new() });
                decorators.push(Decorator { name:"aufrufkonvention".into(),args:vec![DecoratorArg::Text("sysv64".into())] });
            }
            definitions.push(function);
            slots.push(id(&format!("__moo_rueckruf.{bridge}")));
        }
        registry.vtables.insert(global.clone());
        definitions.push(Stmt::NativeConst { zeile:0,name:global.clone(),ty:named(&table),
            value:Some(call(&table,slots)),decorators:vec![native()] });
        let conversion=format!("__moo_dynzeiger.{ty}.{interface}");
        let construct=call(&fat,vec![
            call("zeiger",vec![call("u64",vec![id("__empfaenger")])]),
            call("zeiger",vec![call("adresse_von",vec![id(&global)])]) ]);
        definitions.push(definition(&conversion,vec![("__empfaenger".into(),ptr(named(&ty)))],
            named(&fat),vec![Stmt::Return(Some(construct),0)]));
        registry.conversions.insert((fat,ty),conversion);
    }
    program.statements.extend(lifted);
    program.statements.extend(definitions);
    Ok((program,registry))
}
