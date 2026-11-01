//! P2 C-header binding drafts, with a strict, explicit supported subset.
//! Clang and jq are invoked as argument arrays, never through a shell.
use std::collections::{BTreeMap, BTreeSet};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const TARGET: &str = "x86_64-unknown-linux-gnu";
const FILTER: &str = r#"
.inner[]? |
if .kind == "TypedefDecl" then
  (["T", (.name // ""), (.type.qualType // ""), ([.inner[]? | select((.kind // "") | endswith("Attr"))] | length | tostring)] | @tsv)
elif .kind == "RecordDecl" and (.completeDefinition // false) then
  (["R", (.name // ""), (.tagUsed // ""), ([.inner[]? | select(.kind != "FieldDecl" and ((.kind != "PackedAttr") or (.isImplicit // false) or (.implicit // false) or (.inherited // false)))] | length | tostring), ([.inner[]? | select(.kind == "PackedAttr" and ((.isImplicit // false) == false) and ((.implicit // false) == false) and ((.inherited // false) == false))] | length | tostring)] | @tsv),
  (.inner[]? | select(.kind == "FieldDecl") |
    ["F", (.name // ""), (.type.qualType // ""), (.isBitfield // false | tostring), (.inner // [] | length | tostring)] | @tsv),
  "Z"
elif .kind == "EnumDecl" then
  (["E", (.name // "")] | @tsv),
  (.inner[]? | select(.kind == "EnumConstantDecl") |
    ["C", (.name // ""), ([.inner[]? | select(.kind == "ConstantExpr") | .value] | first // "")] | @tsv),
  "Z"
elif .kind == "FunctionDecl" and ((.isImplicit // false) == false) then
  (["D", (.name // ""), (.type.qualType // ""), (.storageClass // ""), (.inline // false | tostring),
    (.variadic // false | tostring), ([.inner[]? | select(.kind != "ParmVarDecl" and .kind != "NoThrowAttr" and .kind != "ConstAttr")] | length | tostring)] | @tsv),
  (.inner[]? | select(.kind == "ParmVarDecl") |
    ["P", (.type.qualType // ""), (.inner // [] | length | tostring)] | @tsv),
  "Z"
elif .kind == "VarDecl" then (["V", (.name // "")] | @tsv)
else empty end
"#;

#[derive(Default)]
struct Node { kind: String, name: String, fields: Vec<Vec<String>>, meta: Vec<String> }

fn command(program: &str, args: &[String]) -> Result<String, String> {
    let out = Command::new(program).args(args).output().map_err(|e| format!("{program}: {e}"))?;
    if !out.status.success() {
        return Err(format!("{program}: {}: {}", out.status, String::from_utf8_lossy(&out.stderr)));
    }
    String::from_utf8(out.stdout).map_err(|_| format!("{program}: output is not UTF-8"))
}
fn jq(ast: &str) -> Result<String, String> {
    let mut child = Command::new("jq").args(["-r", FILTER]).stdin(Stdio::piped())
        .stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().map_err(|e| format!("jq: {e}"))?;
    let mut input = child.stdin.take().ok_or("jq stdin unavailable")?;
    let bytes = ast.as_bytes().to_vec();
    let writer = std::thread::spawn(move || input.write_all(&bytes));
    let out = child.wait_with_output().map_err(|e| e.to_string())?;
    writer.join().map_err(|_| "jq input thread panicked")?.map_err(|e| e.to_string())?;
    if !out.status.success() { return Err(format!("jq: {}", String::from_utf8_lossy(&out.stderr))); }
    String::from_utf8(out.stdout).map_err(|e| e.to_string())
}
fn parse_nodes(raw: &str) -> Result<Vec<Node>, String> {
    let mut nodes = Vec::new();
    let mut active: Option<Node> = None;
    for line in raw.lines() {
        let parts: Vec<String> = line.split('\t').map(str::to_owned).collect();
        if parts.iter().any(|p| p.contains('\\')) { return Err("escaped AST field unsupported".into()); }
        match parts.first().map(String::as_str).unwrap_or("") {
            "T" | "V" => nodes.push(Node { kind: parts[0].clone(), name: parts.get(1).cloned().unwrap_or_default(), meta: parts[2..].to_vec(), ..Node::default() }),
            "R" | "E" | "D" => {
                if active.is_some() { return Err("malformed AST stream".into()); }
                active = Some(Node { kind: parts[0].clone(), name: parts.get(1).cloned().unwrap_or_default(), meta: parts[2..].to_vec(), ..Node::default() });
            }
            "F" | "C" | "P" => active.as_mut().ok_or("orphan AST field")?.fields.push(parts),
            "Z" => nodes.push(active.take().ok_or("orphan AST terminator")?),
            _ => return Err("unknown AST record".into()),
        }
    }
    if active.is_some() { return Err("unterminated AST record".into()); }
    Ok(nodes)
}
fn valid_name(s: &str) -> bool {
    let mut chars = s.chars();
    let first = chars.next().is_some_and(|c| c.is_ascii_alphabetic() || c == '_');
    first && chars.all(|c| c.is_ascii_alphanumeric() || c == '_') &&
    !matches!(s, "setze"|"auf"|"wenn"|"sonst"|"solange"|"fuer"|"in"|"funktion"|"zeige"|"und"|"oder"|"nicht"|"wahr"|"falsch"|"nichts"|"klasse"|"neu"|"selbst"|"versuche"|"fange"|"wirf"|"stopp"|"weiter"|"konstante"|"fall"|"standard"|"importiere"|"aus"|"exportiere"|"als"|"daten"|"garantiere"|"schnittstelle"|"implementiert"|"unsicher"|"setze_variable"|"zeige_auf_bildschirm"|"wenn_bedingung"|"sonst_alternative"|"solange_wiederhole"|"fuer_jedes"|"funktion_definiere"|"neue_klasse"|"importiere_modul"|"versuche_ausfuehrung"|"fange_fehler"|"teste"|"erwarte"|"parallel"|"vorbedingung"|"nachbedingung"|"von"|"wo"|"sortiere"|"set"|"to"|"if"|"else"|"while"|"for"|"func"|"return"|"show"|"and"|"or"|"not"|"true"|"false"|"none"|"class"|"new"|"this"|"try"|"catch"|"throw"|"break"|"continue"|"const"|"match"|"case"|"default"|"import"|"from"|"export"|"as"|"defer"|"data"|"guard"|"interface"|"implements"|"unsafe"|"test"|"expect"|"precondition"|"postcondition"|"where"|"select"|"order"|"extern"|"struktur"|"struct"|"void")
}
fn moo_type(raw: &str, aliases: &BTreeMap<String,String>, records: &BTreeSet<String>, record: bool, depth: u8) -> Result<String,String> {
    if depth > 12 { return Err("cyclic or deeply nested type".into()); }
    let raw = raw.trim();
    if let Some(open) = raw.find('[') {
        if !record || !raw.ends_with(']') { return Err("array by value unsupported".into()); }
        let close = raw[open+1..].find(']').ok_or("unterminated array")? + open + 1;
        let count: usize = raw[open+1..close].trim().parse().map_err(|_| "non-constant array length")?;
        if count == 0 { return Err("zero-length array unsupported".into()); }
        // C's first dimension is outermost: int[2][3] -> [[i32; 3]; 2].
        let element = format!("{}{}", &raw[..open], &raw[close+1..]);
        return Ok(format!("[{}; {count}]", moo_type(&element, aliases, records, true, depth+1)?));
    }
    if raw.contains('*') {
        if !raw.ends_with('*') || ["(",")","const","volatile","restrict","_Atomic"].iter().any(|x| raw.contains(x)) {
            return Err("qualified/function pointer requires explicit contract".into());
        }
        return Ok(format!("zeiger[{}]", moo_type(&raw[..raw.len()-1], aliases, records, true, depth+1)?));
    }
    let mapped = match raw {
        "void"=>"void", "char"|"signed char"=>"i8", "unsigned char"=>"u8",
        "short"|"short int"|"signed short"=>"i16", "unsigned short"|"unsigned short int"=>"u16",
        "int"|"signed"|"signed int"=>"i32", "unsigned"|"unsigned int"=>"u32",
        "long"|"long int"|"long long"|"long long int"=>"i64",
        "unsigned long"|"unsigned long int"|"unsigned long long"|"unsigned long long int"=>"u64",
        "float"=>"f32", "double"=>"f64", _=>"",
    };
    if !mapped.is_empty() { return Ok(mapped.into()); }
    if let Some(alias) = aliases.get(raw) { return moo_type(alias, aliases, records, record, depth+1); }
    if let Some(name) = raw.strip_prefix("struct ") {
        if record && records.contains(name) { return Ok(name.into()); }
    }
    Err(format!("unsupported C type {raw:?}"))
}
fn make_deps(raw: &str) -> Result<Vec<String>,String> {
    let tail = raw.split_once(':').ok_or("invalid clang dependency output")?.1;
    let mut result = Vec::new(); let mut item = String::new(); let mut chars = tail.chars();
    while let Some(c) = chars.next() {
        if c == '\\' {
            match chars.next() { Some('\n') => {}, Some(x) => item.push(x), None => return Err("invalid dependency escape".into()) }
        } else if c.is_whitespace() {
            if !item.is_empty() { result.push(std::mem::take(&mut item)); }
        } else { item.push(c); }
    }
    if !item.is_empty() { result.push(item); }
    if result.is_empty() { return Err("empty dependency list".into()); }
    Ok(result)
}
fn dependency_hashes(base: &[String], header: &str) -> Result<Vec<(String,String)>,String> {
    let mut args = base.to_vec(); args.extend(["-M".into(), "-MT".into(), "p2_binding".into(), header.into()]);
    let deps = make_deps(&command("clang", &args)?)?;
    let mut out = Vec::new();
    for dep in deps {
        let path = std::fs::canonicalize(&dep).map_err(|e| format!("{dep}: {e}"))?;
        let path = path.to_str().ok_or("non-UTF-8 path")?.to_owned();
        if path.contains('\n') || path.contains('\r') || path.contains('\\') { return Err("unsupported dependency path".into()); }
        let sha = command("sha256sum", &["--".into(), path.clone()])?;
        let value = sha.split_whitespace().next().ok_or("invalid sha256sum output")?;
        if value.len() != 64 || !value.bytes().all(|c| c.is_ascii_hexdigit()) { return Err("invalid SHA256".into()); }
        out.push((path, value.to_owned()));
    }
    out.sort(); out.dedup(); Ok(out)
}
fn number_literal(s: &str) -> Result<String,String> {
    let s = s.trim();
    if s.is_empty() || s.chars().any(|c| !(c.is_ascii_alphanumeric() || ".+-".contains(c))) {
        return Err("non-literal/function-like macro".into());
    }
    let signless = s.trim_start_matches(['+','-']);
    if signless.starts_with("0x") || signless.starts_with("0X") {
        let n = i128::from_str_radix(&signless[2..],16).map_err(|_|"unsupported hexadecimal macro")?;
        let n = if s.starts_with('-') {-n} else {n};
        if n.unsigned_abs() > (1u128<<53) { return Err("integer macro exceeds exact legacy range".into()); }
        return Ok(n.to_string());
    }
    if !signless.contains(['.','e','E']) {
        if signless.len()>1 && signless.starts_with('0') { return Err("octal/leading-zero macro unsupported".into()); }
        let n: i128 = s.parse().map_err(|_| "unsupported integer macro")?;
        if n.unsigned_abs() > (1u128<<53) { return Err("integer macro exceeds exact legacy range".into()); }
        return Ok(n.to_string());
    }
    let v: f64 = s.parse().map_err(|_| "unsupported float macro")?;
    let significant = signless.split(['e','E']).next().unwrap_or("").chars().any(|c| ('1'..='9').contains(&c));
    if !v.is_finite() || (v == 0.0 && significant) { return Err("float macro overflows or underflows f64".into()); }
    Ok(s.into())
}
pub fn run(header: &Path, prefixes: &[String], includes: &[PathBuf], partial: bool) -> Result<i32,String> {
    if prefixes.is_empty() || prefixes.iter().any(|p| p.is_empty() || !p.chars().all(|c| c.is_ascii_alphanumeric() || c=='_')) {
        return Err("at least one nonempty identifier prefix required".into());
    }
    let header = std::fs::canonicalize(header).map_err(|e| e.to_string())?;
    let header = header.to_str().ok_or("non-UTF-8 header")?.to_owned();
    let mut base: Vec<String> = ["-x","c","-std=c11","-fno-builtin","-target",TARGET].iter().map(|s| (*s).into()).collect();
    for dir in includes { base.push("-I".into()); base.push(std::fs::canonicalize(dir).map_err(|e| e.to_string())?.to_str().ok_or("non-UTF-8 include")?.into()); }
    let before = dependency_hashes(&base,&header)?;
    let mut ast_args=base.clone(); ast_args.extend(["-Xclang".into(),"-ast-dump=json".into(),"-fsyntax-only".into(),header.clone()]);
    let ast=command("clang",&ast_args)?;
    let nodes=parse_nodes(&jq(&ast)?)?;
    let mut macro_args=base.clone(); macro_args.extend(["-E".into(),"-dM".into(),header.clone()]);
    let macros=command("clang",&macro_args)?;
    let after=dependency_hashes(&base,&header)?;
    if before != after { return Err("header dependencies changed during generation".into()); }
    let version=command("clang",&["--version".into()])?.lines().next().unwrap_or("").to_owned();
    let selected = |n: &str| prefixes.iter().any(|p| n.starts_with(p));
    let aliases: BTreeMap<String,String> = nodes.iter().filter(|n| n.kind=="T" && !n.name.is_empty() && n.meta.get(1).is_some_and(|x| x=="0"))
        .filter_map(|n| n.meta.first().map(|v|(n.name.clone(),v.clone()))).collect();
    let records: BTreeSet<String> = nodes.iter().filter(|n| n.kind=="R" && n.meta.first().is_some_and(|x|x=="struct") && selected(&n.name)).map(|n|n.name.clone()).collect();
    let mut output=Vec::<String>::new(); let mut diagnostics=Vec::<String>::new(); let mut seen=BTreeSet::new();
    let mut emit = |name: &str, item: String| -> Result<(),String> {
        if !valid_name(name) { return Err("reserved or invalid Moo identifier".into()); }
        if !seen.insert(name.to_owned()) { return Err("duplicate emitted symbol".into()); }
        output.push(item); Ok(())
    };
    for node in &nodes {
        let selected_enum = node.kind=="E" && node.fields.iter().any(|f|f.get(1).is_some_and(|s|selected(s)));
        if !selected_enum && (node.name.is_empty() || !selected(&node.name)) { continue; }
        let result: Result<(),String> = (|| {
            match node.kind.as_str() {
                "R" => {
                    if node.meta.first().is_none_or(|v|v!="struct") || node.meta.get(1).is_none_or(|v|v!="0") || node.meta.get(2).is_none_or(|v|v!="0" && v!="1") || node.fields.is_empty() {
                        return Err("union, empty or attributed structure unsupported".into());
                    }
                    let mut fields=Vec::new();
                    for f in &node.fields {
                        if f.len()!=5 || !valid_name(&f[1]) || f[3]!="false" || f[4]!="0" { return Err("bitfield/field attribute/name unsupported".into()); }
                        fields.push(format!("    {}: {}",f[1],moo_type(&f[2],&aliases,&records,true,0)?));
                    }
                    let packed = if node.meta[2] == "1" { "@gepackt\n" } else { "" };
                    emit(&node.name,format!("{packed}struktur {}:\n{}",node.name,fields.join("\n")))?;
                }
                "T" => {
                    if node.meta.first()!=Some(&format!("struct {}",node.name)) || node.meta.get(1).is_none_or(|v| v!="0") || !records.contains(&node.name) {
                        return Err("standalone typedef output unsupported".into());
                    }
                }
                "V" => return Err("external variables unsupported".into()),
                "E" => {
                    let mut last: i128 = -1;
                    for f in &node.fields {
                        if f.len()!=3 { return Err("invalid enum field".into()); }
                        last=if f[2].is_empty(){last.checked_add(1).ok_or("enum overflow")?}else{f[2].parse().map_err(|_|"invalid enum value")?};
                        if last.unsigned_abs()>(1u128<<53) {return Err("enum exceeds exact legacy number".into());}
                        if selected(&f[1]) {emit(&f[1],format!("konstante {} auf {last}",f[1]))?;}
                    }
                }
                "D" => {
                    if node.meta.len()!=5 || node.meta[1]=="static" || node.meta[2]=="true" || node.meta[3]=="true" || node.meta[4]!="0" {
                        return Err("static, inline, variadic or attributed function unsupported".into());
                    }
                    let ty=&node.meta[0];
                    let (ret,args)=ty.split_once('(').ok_or("invalid function type")?;
                    if ty.contains("(*") || !ty.ends_with(')') || (args==")" && node.fields.is_empty()) {
                        return Err("non-prototype or function pointer unsupported".into());
                    }
                    let result=moo_type(ret,&aliases,&records,false,0)?;
                    let mut params=Vec::new();
                    for (i,p) in node.fields.iter().enumerate() {
                        if p.len()!=3 || p[2]!="0" {return Err("parameter attribute unsupported".into());}
                        let ty=moo_type(&p[1],&aliases,&records,false,0)?;
                        if ty=="void" { return Err("void parameter".into()); }
                        params.push(format!("arg{i}: {ty}"));
                    }
                    emit(&node.name,format!("extern \"C\" funktion {}({}) -> {result}",node.name,params.join(", ")))?;
                }
                _ => {}
            }
            Ok(())
        })();
        if let Err(e)=result {diagnostics.push(format!("{}: {e}",node.name));}
    }
    // Clang does not promise macro-map iteration order.
    let mut macro_lines: Vec<&str> = macros.lines().collect();
    macro_lines.sort_unstable();
    for line in macro_lines {
        let Some(rest)=line.strip_prefix("#define ") else {continue};
        let split=rest.find(|c:char| !(c.is_ascii_alphanumeric() || c=='_')).unwrap_or(rest.len());
        let (name,value)=rest.split_at(split);
        if !selected(name) || value.trim().is_empty() {continue;}
        let result = if !value.starts_with(' ') {Err("function-like macro unsupported".into())}
            else {number_literal(value).and_then(|v| emit(name,format!("konstante {name} auf {v}")))};
        if let Err(e)=result {diagnostics.push(format!("{name}: {e}"));}
    }
    drop(emit);
    if output.is_empty() && diagnostics.is_empty() {diagnostics.push("prefix: no selected declarations or constants".into());}
    diagnostics.sort();
    for d in &diagnostics {eprintln!("UNTERSTUETZT NICHT: {d}");}
    if diagnostics.is_empty() || partial {
        println!("# P2 bindgen: ENTWURF; ABI/Ownership manuell pruefen\n# target: {TARGET}\n# clang: {version}");
        for (name,hash) in &before {println!("# dependency sha256 {hash} {name}");}
        if !diagnostics.is_empty() {println!("# UNVOLLSTAENDIG: Diagnosen auf stderr");}
        for item in output {println!("\n{item}");}
    }
    Ok(if diagnostics.is_empty(){0}else{2})
}
