//! Conservative extern-C adapters; no C++ ABI crosses the Moo boundary.
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use super::bindgen::{command, jq_filter, make_deps, valid_name};

pub const DEFAULT_TARGET: &str = if cfg!(windows) { "x86_64-pc-windows-msvc" } else { "x86_64-unknown-linux-gnu" };
const FILTER: &str = r#"
def pos($n;$r): [($n.loc.file // ""),($n.loc.offset // -1|tostring),($n.loc.line // 0|tostring),($n.loc.col // 0|tostring)];
.inner[]? | select((.kind=="CXXRecordDecl" and .completeDefinition) or .kind=="ClassTemplateDecl") | . as $r |
(["R",.kind,.name,.tagUsed // "",(.bases // []|length|tostring),
 (.definitionData.isPolymorphic // false|tostring),
 ([.inner[]?|select(.kind|endswith("Attr"))]|length|tostring)] + pos(.;. ) | @tsv),
(.inner[]? | select((.isImplicit // false)|not) |
 if .kind=="AccessSpecDecl" then ["A",.access]|@tsv
 elif (.kind=="CXXConstructorDecl" or .kind=="CXXDestructorDecl" or .kind=="CXXMethodDecl" or .kind=="CXXConversionDecl" or .kind=="FunctionTemplateDecl") then
 (["M",.kind,.name // "",.type.qualType // "",.storageClass // "",
  (.virtual // false|tostring),(.variadic // false|tostring),(.explicitlyDeleted // false|tostring),
  ([.inner[]?|select(.kind|endswith("Attr"))]|length|tostring),
  (.loc.spellingLoc != null or .loc.expansionLoc != null|tostring)] + pos(.;$r)|@tsv),
 (.inner[]?|select(.kind=="ParmVarDecl")|
 ["P",(.type.desugaredQualType // .type.qualType // ""),(.init != null|tostring),
 ([.inner[]?|select(.kind|endswith("Attr"))]|length|tostring)]|@tsv),"Z"
 else empty end),"E"
"#;

#[derive(Clone)]
struct Scalar { cpp: String, moo: &'static str, c: &'static str }
struct Method { name: String, ctor: bool, constant: bool, result: Option<Scalar>, params: Vec<Scalar>, symbol: String }
struct Class { name: String, methods: Vec<Method>, pos: String }
fn portable(path: &Path) -> Result<String,String> {
    let s = path.to_str().ok_or("Pfad ist nicht UTF-8")?;
    let s = s.strip_prefix(r"\\?\").unwrap_or(s).replace('\\', "/");
    if s.contains(['\n','\r','"']) { return Err("nicht unterstützter Pfad".into()); }
    Ok(s)
}
fn dependencies(base: &[String], header: &str) -> Result<Vec<(String,String)>,String> {
    let mut args=base.to_vec(); args.extend(["-M".into(),"-MT".into(),"moo_cpp".into(),header.into()]);
    let mut result=Vec::new();
    for name in make_deps(&command("clang",&args)?)? {
        let path=portable(&std::fs::canonicalize(&name).map_err(|e|format!("{name}: {e}"))?)?;
        let hash=command("sha256sum",&["--".into(),path.clone()])?;
        let hash=hash.split_whitespace().next().ok_or("SHA256 fehlt")?.to_owned();
        if hash.len()!=64 || !hash.bytes().all(|b|b.is_ascii_hexdigit()) { return Err("ungültiger SHA256".into()); }
        result.push((path,hash));
    }
    result.sort(); result.dedup(); Ok(result)
}
fn location(p: &[String], header: &str, name: &str, deps: &[(String,String)]) -> Result<String,String> {
    let offset=p.get(1).and_then(|s|s.parse::<usize>().ok()).ok_or("AST-Quelloffset fehlt")?;
    // Clang elides repeated filenames. Resolve an omitted file against the frozen
    // dependency inventory and the actual token, never against a guessed header.
    let token=if name.starts_with("operator") {"operator"} else {name.split('<').next().unwrap_or(name)};
    let known=p.first().filter(|s|!s.is_empty());
    let candidates:Vec<&str>=if let Some(file)=known {vec![file]} else {deps.iter().map(|(p,_)|p.as_str()).collect()};
    let mut matches=Vec::new();
    for file in candidates {
        let data=std::fs::read(file).map_err(|e|format!("{file}: {e}"))?;
        if data.get(offset..).is_some_and(|tail|tail.starts_with(token.as_bytes())) {
            matches.push((file.to_owned(),data));
        }
    }
    if matches.len()!=1 {
        return Err(format!("{header}:0:0: Quellposition für '{name}' nicht eindeutig auflösbar; keine Positionsangabe geraten"));
    }
    let (file,data)=matches.pop().unwrap();
    let line=1+data[..offset].iter().filter(|b|**b==b'\n').count();
    let column=data[..offset].iter().rposition(|b|*b==b'\n').map_or(offset+1,|n|offset-n);
    Ok(format!("{file}:{line}:{column}"))
}
fn scalar(raw: &str, target: &str) -> Result<Scalar,String> {
    let raw=raw.trim();
    let long=if target.contains("windows") {("i32","int32_t")} else {("i64","int64_t")};
    let (moo,c)=match raw {
        "signed char"=>("i8","int8_t"), "unsigned char"=>("u8","uint8_t"),
        "short"|"short int"=>("i16","int16_t"), "unsigned short"|"unsigned short int"=>("u16","uint16_t"),
        "int"|"signed int"=>("i32","int32_t"), "unsigned int"=>("u32","uint32_t"),
        "long"|"long int"=>long,
        "unsigned long"|"unsigned long int"=>if long.0=="i32" {("u32","uint32_t")} else {("u64","uint64_t")},
        "long long"|"long long int"=>("i64","int64_t"), "unsigned long long"|"unsigned long long int"=>("u64","uint64_t"),
        "float"=>("f32","float"), "double"=>("f64","double"),
        _=>return Err(format!("Typ '{raw}' nicht unterstützt (nur explizite skalare Typen; keine bool/char-, Zeiger-, Referenz-, Aggregat- oder STL-Grenzen)")),
    };
    Ok(Scalar{cpp:raw.into(),moo,c})
}
fn parse(raw: &str, selected: &BTreeSet<String>, prefix: &str, target: &str, header: &str, deps: &[(String,String)]) -> Result<Vec<Class>,String> {
    let mut classes=Vec::new();
    let mut class: Option<Class>=None;
    let mut access=String::new();
    let mut active: Option<(Vec<String>,String,bool)>=None;
    let mut parameters=Vec::new();
    let mut names=BTreeSet::new();
    for line in raw.lines() {
        let p:Vec<String>=line.split('\t').map(str::to_owned).collect();
        if p.iter().any(|s|s.contains('\\')) { return Err("escaped AST-Feld nicht unterstützt".into()); }
        match p.first().map(String::as_str).unwrap_or("") {
            "R" => {
                class=None; active=None;
                if !p.get(2).is_some_and(|n|selected.contains(n)) {continue;}
                if p.len()!=11 {return Err("unvollständiger Klassen-AST".into());}
                let pos=location(&p[7..],header,&p[2],deps)?;
                if p[1]!="CXXRecordDecl" || p[4]!="0" || p[5]!="false" || p[6]!="0" {
                    return Err(format!("{pos}: {}: Templates, Vererbung, virtuelle oder attributierte Klassen nicht unterstützt",p[2]));
                }
                if !names.insert(p[2].clone()) {return Err(format!("{pos}: doppelte Klasse {}",p[2]));}
                access=if p[3]=="struct" {"public"} else {"private"}.into();
                class=Some(Class{name:p[2].clone(),methods:Vec::new(),pos});
            }
            "A" if class.is_some()=>access=p.get(1).cloned().ok_or("Zugriff fehlt")?,
            "M" if class.is_some()=>{
                if p.len()!=14 {return Err("unvollständiger Methoden-AST".into());}
                parameters.clear();
                active=Some((p,access.clone(),false));
            }
            "P" if active.is_some()=>{
                if p.len()!=4 {return Err("unvollständiger Parameter-AST".into());}
                parameters.push(p);
            }
            "Z" if active.is_some()=>{
                let (p,visibility,_)=active.take().unwrap();
                let c=class.as_mut().unwrap();
                if visibility!="public" && p[1]!="CXXDestructorDecl" {continue;}
                let pos=location(&p[10..],header,&p[2],deps)?;
                let fail=|reason:String|format!("{pos}: {}::{}: {reason}",c.name,p[2]);
                if p[1]=="CXXDestructorDecl" {
                    if visibility!="public" || p[7]!="false" || p[5]!="false" || p[8]!="0" {
                        return Err(fail("Destruktor muss öffentlich, nicht gelöscht und nicht virtuell/attributiert sein".into()));
                    }
                    continue;
                }
                if visibility!="public" {continue;}
                let ctor=p[1]=="CXXConstructorDecl";
                if (!ctor && p[1]!="CXXMethodDecl") || p[4]=="static" || p[5]!="false" || p[6]!="false" ||
                    p[7]!="false" || p[8]!="0" || p[9]!="false" || (!ctor && !valid_name(&p[2])) {
                    return Err(fail("nur gewöhnliche öffentliche Konstruktoren/Methoden; keine Templates, Operatoren, statischen, virtuellen, variadischen, gelöschten, attributierten oder makroerzeugten Methoden".into()));
                }
                let (ret,tail)=p[3].split_once('(').ok_or_else(||fail("Funktionssignatur fehlt".into()))?;
                let suffix=tail.rsplit_once(')').ok_or_else(||fail("Funktionssignatur unvollständig".into()))?.1.trim();
                if !matches!(suffix,""|"const"|"noexcept"|"const noexcept") {return Err(fail(format!("Methodenqualifikation '{suffix}' nicht unterstützt")));}
                let constant=suffix.starts_with("const");
                let result=if ctor || ret.trim()=="void" {None} else {Some(scalar(ret,target).map_err(fail)?)};
                let mut params=Vec::new();
                for arg in &parameters {
                    if arg[2]!="false" || arg[3]!="0" {return Err(fail("Defaultargumente oder Parameterattribute nicht unterstützt".into()));}
                    params.push(scalar(&arg[1],target).map_err(fail)?);
                }
                if params.len()>6 {return Err(fail("höchstens sechs skalare Argumente unterstützt".into()));}
                let name=if ctor {"neu"} else {&p[2]};
                let types=if params.is_empty() {"leer".into()} else {params.iter().map(|s|s.moo).collect::<Vec<_>>().join("_")};
                let symbol=format!("{prefix}_{}_{}_{name}_{types}{}",c.name,if ctor {"k"} else {"m"},if constant {"_const"}else{""});
                if c.methods.iter().any(|m|m.symbol==symbol) {return Err(fail("Überladungen kollidieren nach skalarer ABI-Abbildung".into()));}
                c.methods.push(Method{name:p[2].clone(),ctor,constant,result,params,symbol});
            }
            "E"=>{
                if let Some(mut c)=class.take() {
                    if !c.methods.iter().any(|m|m.ctor) {return Err(format!("{}: {}: mindestens ein ausdrücklich deklarierter öffentlicher Konstruktor erforderlich",c.pos,c.name));}
                    c.methods.sort_by(|a,b|a.symbol.cmp(&b.symbol));
                    classes.push(c);
                }
                active=None;
            }
            _=>{}
        }
    }
    for name in selected {
        if !classes.iter().any(|c|&c.name==name) {return Err(format!("{header}:0:0: keine unterstützte globale Definition für '{name}' (Namespaces und verschachtelte Klassen nicht unterstützt)"));}
    }
    classes.sort_by(|a,b|a.name.cmp(&b.name));
    let mut emitted=BTreeSet::from([format!("{prefix}_fehler"),format!("{prefix}_abi_fehler")]);
    for class in &classes {
        let mut symbols=vec![format!("{prefix}_{}",class.name),format!("{prefix}_{}_frei",class.name),format!("{prefix}_{}_frei_abi",class.name)];
        for method in &class.methods {
            symbols.push(method.symbol.clone());
            symbols.push(format!("{}_abi",method.symbol));
        }
        for symbol in symbols {
            if !emitted.insert(symbol.clone()) {
                return Err(format!("{}: Symbolkollision '{symbol}' zwischen generierten Typen/Funktionen; Klassen getrennt mit verschiedenen Präfixen generieren",class.pos));
            }
        }
    }
    Ok(classes)
}
pub fn run(header:&Path, names:&[String], prefix:&str, library:&str, target:&str, emit:&str, includes:&[PathBuf], defines:&[String]) -> Result<(),String> {
    if !matches!(target,"x86_64-unknown-linux-gnu"|"x86_64-pc-windows-msvc") {return Err("nur x86_64 Linux-SysV und Windows-MSVC unterstützt".into());}
    if !matches!(emit,"cpp"|"moo") {return Err("--emit muss cpp oder moo sein".into());}
    if !valid_name(prefix) || names.is_empty() || names.iter().any(|n|!valid_name(n)) {return Err("ungültiges Präfix/Klassenname".into());}
    if library.is_empty() || !library.bytes().all(|b|b.is_ascii_alphanumeric()||b"_-.+".contains(&b)) {return Err("ungültiger Bibliotheksname".into());}
    let header=portable(&std::fs::canonicalize(header).map_err(|e|e.to_string())?)?;
    let selected:BTreeSet<String>=names.iter().cloned().collect();
    if selected.len()!=names.len() {return Err("Klasse mehrfach gewählt".into());}
    let mut base:Vec<String>=["-x","c++","-std=c++17","-fno-builtin","-target",target].iter().map(|s|(*s).into()).collect();
    for dir in includes {base.push("-I".into());base.push(portable(&std::fs::canonicalize(dir).map_err(|e|e.to_string())?)?);}
    for define in defines {
        if define.is_empty() || define.contains(['\n','\r']) {return Err("ungültige Definition".into());}
        base.push(format!("-D{define}"));
    }
    let before=dependencies(&base,&header)?;
    let mut args=base.clone();args.extend(["-Xclang".into(),"-ast-dump=json".into(),"-fsyntax-only".into(),header.clone()]);
    let ast=command("clang",&args)?;
    let classes=parse(&jq_filter(&ast,FILTER)?,&selected,prefix,target,&header,&before)?;
    if before!=dependencies(&base,&header)? {return Err("Header-Abhängigkeiten während Generierung geändert".into());}
    let version=command("clang",&["--version".into()])?;
    let comment=if emit=="cpp" {"//"} else {"#"};
    let mut out=format!("{comment} Moo C++17-Adapter: expliziter Besitz, keine C++-ABI in Moo.\n{comment} target: {target}\n{comment} clang: {}\n",version.lines().next().unwrap_or(""));
    for define in defines {out.push_str(&format!("{comment} define: {define}\n"));}
    for (path,hash) in before {out.push_str(&format!("{comment} dependency sha256 {hash} {path}\n"));}
    if emit=="cpp" {
        out.push_str(&format!("#include <cstdint>\n#include \"{header}\"\nstatic_assert(sizeof(int)==4 && sizeof(long long)==8 && sizeof(float)==4 && sizeof(double)==8);\n"));
        out.push_str(&format!("static thread_local int32_t {prefix}_status = 0;\nextern \"C\" int32_t {prefix}_abi_fehler() noexcept {{ return {prefix}_status; }}\n"));
    } else {
        out.push_str("# Status: 0 Erfolg, -1 Nullargument, -2 C++-Exception, -3 Ergebnis-Pufferlaenge.\n# Ergebnisse bleiben bei Fehler unveraendert; Objekte koennen vor einer Exception mutieren.\n# Konstruktor: nichts bei Fehler, fehler() liefert -2; erfolgreicher Griff wird explizit freigegeben.\n");
        emit_moo_parts(&mut out,library,&format!("{prefix}_abi_fehler"),&format!("{prefix}_fehler"),&[],&[],"i32",false,"",&[],Some(if target.contains("windows") {"msvcprt"} else {"stdc++"}));
    }
    for class in classes {
        let opaque=format!("{prefix}_{}",class.name);
        if emit=="moo" {out.push_str(&format!("\n@opak struktur {opaque}\n"));}
        let free=format!("{prefix}_{}_frei",class.name);
        if emit=="cpp" {
            out.push_str(&format!("extern \"C\" int32_t {free}_abi({}* obj) noexcept {{ {prefix}_status=0; if(!obj) return {prefix}_status=-1; try {{ delete obj; return 0; }} catch (...) {{ return {prefix}_status=-2; }} }}\n",class.name));
        } else {
            emit_moo(&mut out,library,&format!("{free}_abi"),&free,&[format!("obj: ptr[{opaque}]")],"i32",true,"",&["obj".into()]);
        }
        for m in class.methods {
            let mut args:Vec<String>=m.params.iter().enumerate().map(|(i,s)|format!("{} arg{i}",s.c)).collect();
            let mut moo:Vec<String>=m.params.iter().enumerate().map(|(i,s)|format!("arg{i}: {}",s.moo)).collect();
            let calls:Vec<String>=m.params.iter().enumerate().map(|(i,s)|format!("static_cast<{}>(arg{i})",s.cpp)).collect();
            let mut moo_calls:Vec<String>=m.params.iter().enumerate().map(|(i,_)|format!("arg{i}")).collect();
            if !m.ctor {
                args.insert(0,format!("{}{}* obj",if m.constant {"const "}else{""},class.name));
                moo.insert(0,format!("obj: ptr[{opaque}]"));moo_calls.insert(0,"obj".into());
            }
            if let Some(r)=&m.result {args.push(format!("{}* ergebnis",r.c));}
            let ret=if m.ctor {format!("ptr[{opaque}]")}else{"i32".into()};
            if emit=="cpp" {
                let result=if m.ctor {format!("{}*",class.name)}else{"int32_t".into()};
                let expression=if m.ctor {format!("new {}({})",class.name,calls.join(", "))}else{format!("obj->{}({})",m.name,calls.join(", "))};
                let null=if m.ctor {"nullptr"}else{"-2"};
                let guard=if m.ctor {String::new()}else{format!("if(!obj{}) return {prefix}_status=-1; ",if m.result.is_some(){" || !ergebnis"}else{""})};
                let body=if m.ctor {format!("return {expression};")}else if m.result.is_some() {format!("const auto wert={expression}; *ergebnis=wert; return 0;")}else{format!("{expression}; return 0;")};
                out.push_str(&format!("extern \"C\" {result} {}_abi({}) noexcept {{ {prefix}_status=0; {guard}try {{ {body} }} catch (...) {{ {prefix}_status=-2; return {null}; }} }}\n",m.symbol,args.join(", ")));
            } else {
                let mut foreign=moo.clone();
                let mut guard=String::new();
                if let Some(r)=&m.result {
                    foreign.push(format!("ergebnis: ptr[{}]",r.moo));
                    moo.push(format!("ergebnis: puffer[{}]",r.moo));
                    moo_calls.push("puffer_zeiger(ergebnis)".into());
                    guard="    wenn länge(ergebnis) != 1:\n        gib_zurück -3\n".into();
                }
                emit_moo_parts(&mut out,library,&format!("{}_abi",m.symbol),&m.symbol,&foreign,&moo,&ret,false,&guard,&moo_calls,None);
            }
        }
    }
    // Nothing is printed until every class and public method has been validated.
    print!("{out}"); Ok(())
}
fn emit_moo(out:&mut String, library:&str, abi:&str, name:&str, args:&[String], ret:&str, consumes:bool, guard:&str, calls:&[String]) {
    emit_moo_parts(out,library,abi,name,args,args,ret,consumes,guard,calls,None)
}
fn emit_moo_parts(out:&mut String, library:&str, abi:&str, name:&str, foreign:&[String], args:&[String], ret:&str, consumes:bool, guard:&str, calls:&[String], runtime:Option<&str>) {
    out.push_str(&format!("\n@bibliothek(\"{library}\")\n"));
    if let Some(runtime)=runtime {
        out.push_str(&format!("@bibliothek(\"{runtime}\")\n"));
    }
    out.push_str(&format!("extern \"C\" funktion {abi}({}) -> {ret}\n\n@nativ\n{}funktion {name}({}) -> {ret}:\n{guard}    unsicher:\n        setze wert: {ret} auf {abi}({})\n        gib_zurück wert\n",foreign.join(", "),if consumes {"@verbraucht(1)\n"}else{""},args.join(", "),calls.join(", ")));
}
