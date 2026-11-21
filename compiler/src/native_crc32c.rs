//! CRC32C-Zustandsupdate: keine Runtime, keine SIMD-Register, keine Featureannahme.
//! Unkomplementierter Castagnoli-Zustand; Daten duerfen beliebig ausgerichtet sein.
use inkwell::context::Context;
use inkwell::memory_buffer::MemoryBuffer;
use inkwell::module::{Linkage, Module};
use inkwell::values::FunctionValue;

/// Vom Compiler erzeugte native Helfer gehoeren auch hosted zum O2-Pfad.
pub(crate) fn ist_helfer(f: FunctionValue<'_>) -> bool {
    matches!(f.get_name().to_bytes(), b"__moo_crc32c_update" | b"__moo_crc32c_fallback"
        | b"__moo_crc32c_fast" | b"__moo_crc32c_shift" | b"__moo_crc32c_shift_64"
        | b"__moo_crc32c_shift_16" | b"__moo_crc32c_shift_8" | b"__moo_crc32c_shift_1024")
}

fn byte(mut crc: u32) -> u32 {
    for _ in 0..8 { crc = (crc >> 1) ^ if crc & 1 != 0 { 0x82f63b78 } else { 0 }; }
    crc
}

fn shift(mut crc: u32, bytes: usize) -> u32 {
    for _ in 0..bytes { crc = byte(crc); }
    crc
}

/// Tabellen sind nur mathematische Konstanten; keine CPU- oder Pufferdaten.
fn table(name: &str, values: impl Iterator<Item = u32>) -> String {
    let entries = values.map(|v| format!("i32 {v}")).collect::<Vec<_>>();
    format!("@{name} = private unnamed_addr constant [{} x i32] [{}], align 64\n",
        entries.len(), entries.join(", "))
}

pub(crate) fn update<'ctx>(context: &'ctx Context, module: &Module<'ctx>) -> Result<FunctionValue<'ctx>, String> {
    const NAME: &str = "__moo_crc32c_update";
    if let Some(f) = module.get_function(NAME) { return Ok(f); }
    let triple = module.get_triple();
    let x86 = triple.as_str().to_bytes().starts_with(b"x86_64");
    let mut ir = table("__moo_crc32c_table", (0..256).map(byte));
    ir.push_str(r#"
define internal i32 @__moo_crc32c_fallback(i32 %state, ptr %data, i64 %len) {
entry:
  br label %head
head:
  %i = phi i64 [ 0, %entry ], [ %next, %body ]
  %crc = phi i32 [ %state, %entry ], [ %value, %body ]
  %more = icmp ult i64 %i, %len
  br i1 %more, label %body, label %done
body:
  %p = getelementptr i8, ptr %data, i64 %i
  %v = load i8, ptr %p, align 1
  %ext = zext i8 %v to i32
  %mixed = xor i32 %crc, %ext
  %low = and i32 %mixed, 255
  %tp = getelementptr [256 x i32], ptr @__moo_crc32c_table, i32 0, i32 %low
  %tv = load i32, ptr %tp, align 4
  %high = lshr i32 %crc, 8
  %value = xor i32 %high, %tv
  %next = add i64 %i, 1
  br label %head
done:
  ret i32 %crc
}
"#);
    if x86 {
        for size in [1024usize, 256, 64, 16, 8] {
            ir.push_str(&table(&format!("__moo_crc32c_shift{size}"), (0..1024).map(|v| shift((v % 256) << (8 * (v / 256)), size))));
            let name = if size == 256 { "__moo_crc32c_shift".into() } else { format!("__moo_crc32c_shift_{size}") };
            ir.push_str(&format!("define internal i32 @{name}(i32 %crc) alwaysinline {{\nentry:\n"));
            for lane in 0..4 {
                ir.push_str(&format!("  %s{lane} = lshr i32 %crc, {}\n  %b{lane} = and i32 %s{lane}, 255\n  %j{lane} = add i32 %b{lane}, {}\n  %p{lane} = getelementptr [1024 x i32], ptr @__moo_crc32c_shift{size}, i32 0, i32 %j{lane}\n  %v{lane} = load i32, ptr %p{lane}, align 4\n", lane * 8, lane * 256));
            }
            ir.push_str("  %a = xor i32 %v0, %v1\n  %b = xor i32 %v2, %v3\n  %r = xor i32 %a, %b\n  ret i32 %r\n}\n");
        }
        ir.push_str("define internal i32 @__moo_crc32c_fast(i32 %state, ptr %data, i64 %len) noinline {\nentry:\n  br label %groups1024\n");
        // Vier Ketten je Potenz-zwei-Kachel; auch Reste bleiben parallel (<32 Byte seriell).
        for (level, size) in [1024usize, 256, 64, 16, 8].into_iter().enumerate() {
            let (incoming_offset, incoming_crc, from) = if level == 0 {
                ("0".to_string(), "%state".to_string(), "entry".to_string())
            } else {
                let previous = [1024, 256, 64, 16][level - 1];
                (format!("%offset{previous}"), format!("%current{previous}"), format!("groups{previous}"))
            };
            let next = match size { 1024 => "groups256", 256 => "groups64", 64 => "groups16", 16 => "groups8", _ => "words" };
            let group_bytes = 4 * size;
            ir.push_str(&format!("groups{size}:\n  %offset{size} = phi i64 [ {incoming_offset}, %{from} ], [ %after{size}, %group{size} ]\n  %current{size} = phi i32 [ {incoming_crc}, %{from} ], [ %combined{size}, %group{size} ]\n  %remaining{size} = sub i64 %len, %offset{size}\n  %whole{size} = icmp uge i64 %remaining{size}, {group_bytes}\n  br i1 %whole{size}, label %group{size}, label %{next}\ngroup{size}:\n  %start{size} = zext i32 %current{size} to i64\n"));
            for step in 0..size / 8 {
                for lane in 0..4 {
                    let prev = if step == 0 {
                        if lane == 0 { format!("%start{size}") } else { "0".into() }
                    } else { format!("%c{lane}_{}_{size}", step - 1) };
                    ir.push_str(&format!("  %i{lane}_{step}_{size} = add i64 %offset{size}, {}\n  %p{lane}_{step}_{size} = getelementptr i8, ptr %data, i64 %i{lane}_{step}_{size}\n  %v{lane}_{step}_{size} = load i64, ptr %p{lane}_{step}_{size}, align 1\n  %c{lane}_{step}_{size} = call i64 asm \"crc32q $2, $0\", \"=r,0,r,~{{flags}}\"(i64 {prev}, i64 %v{lane}_{step}_{size})\n", lane * size + step * 8));
                }
            }
            let last = size / 8 - 1;
            let name = if size == 256 { "__moo_crc32c_shift".into() } else { format!("__moo_crc32c_shift_{size}") };
            ir.push_str(&format!("  %a0_{size} = trunc i64 %c0_{last}_{size} to i32\n  %a1_{size} = trunc i64 %c1_{last}_{size} to i32\n  %a2_{size} = trunc i64 %c2_{last}_{size} to i32\n  %shift0_{size} = call i32 @{name}(i32 %a0_{size})\n  %joined{size} = xor i32 %shift0_{size}, %a1_{size}\n  %shift1_{size} = call i32 @{name}(i32 %joined{size})\n  %joined2_{size} = xor i32 %shift1_{size}, %a2_{size}\n  %a3_{size} = trunc i64 %c3_{last}_{size} to i32\n  %shift2_{size} = call i32 @{name}(i32 %joined2_{size})\n  %combined{size} = xor i32 %shift2_{size}, %a3_{size}\n  %after{size} = add i64 %offset{size}, {group_bytes}\n  br label %groups{size}\n"));
        }
        ir.push_str(r#"
words:
  %pos = phi i64 [ %offset8, %groups8 ], [ %nextword, %word ]
  %acc32 = phi i32 [ %current8, %groups8 ], [ %word32, %word ]
  %left = sub i64 %len, %pos
  %hasword = icmp uge i64 %left, 8
  br i1 %hasword, label %word, label %tail
word:
  %wp = getelementptr i8, ptr %data, i64 %pos
  %wv = load i64, ptr %wp, align 1
  %acc64 = zext i32 %acc32 to i64
  %word64 = call i64 asm "crc32q $2, $0", "=r,0,r,~{flags}"(i64 %acc64, i64 %wv)
  %word32 = trunc i64 %word64 to i32
  %nextword = add i64 %pos, 8
  br label %words
tail:
  %tp = getelementptr i8, ptr %data, i64 %pos
  %result = call i32 @__moo_crc32c_fallback(i32 %acc32, ptr %tp, i64 %left)
  ret i32 %result
}
define i32 @__moo_crc32c_update(i32 %state, ptr %data, i64 %len, i64 %cpu_features) {
entry:
  %scope_bit = and i64 %cpu_features, 256
  %scope_valid = icmp ne i64 %scope_bit, 0
  br i1 %scope_valid, label %scope, label %length
scope:
  %sse42 = and i64 %cpu_features, 512
  %scope_hardware = icmp ne i64 %sse42, 0
  br i1 %scope_hardware, label %fast, label %fallback
length:
  %long = icmp uge i64 %len, 64
  br i1 %long, label %feature, label %fallback
feature:
  %cpuid = call {i32,i32,i32,i32} asm sideeffect "cpuid", "={ax},={bx},={cx},={dx},0,2,~{flags}"(i32 1, i32 0)
  %ecx = extractvalue {i32,i32,i32,i32} %cpuid, 2
  %bit = and i32 %ecx, 1048576
  %available = icmp ne i32 %bit, 0
  br i1 %available, label %fast, label %fallback
fast:
  %a = call i32 @__moo_crc32c_fast(i32 %state, ptr %data, i64 %len)
  ret i32 %a
fallback:
  %b = call i32 @__moo_crc32c_fallback(i32 %state, ptr %data, i64 %len)
  ret i32 %b
}
"#);
    } else {
        ir.push_str(r#"
define i32 @__moo_crc32c_update(i32 %state, ptr %data, i64 %len, i64 %cpu_features) {
  %r = call i32 @__moo_crc32c_fallback(i32 %state, ptr %data, i64 %len)
  ret i32 %r
}
"#);
    }
    // Inkwell 22 verlangt auch fuer eine kopierte IR-Ansicht den NUL-Abschluss.
    ir.push('\0');
    let buffer = MemoryBuffer::create_from_memory_range_copy(ir.as_bytes(), "moo-crc32c");
    let helper = context.create_module_from_ir(buffer).map_err(|err| err.to_string())?;
    module.link_in_module(helper).map_err(|err| err.to_string())?;
    let f = module.get_function(NAME).ok_or("CRC32C-Helfer fehlt")?;
    f.set_linkage(Linkage::Internal);
    Ok(f)
}
