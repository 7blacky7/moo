# ============================================================
# moo x86-64 Mini-Disassembler
#
# Kompilieren: moo-compiler compile x86dis.moo -o x86dis
# Starten:     ./x86dis
#
# Subset:
#   NOP/RET/LEAVE/HLT/INT3/CLI/STI/CLD/STD/SYSCALL
#   PUSH/POP r64 (inkl REX.B fuer r8-r15)
#   MOV r64, imm64 (0x48 0xB8+r)
#   MOV r64, r64   (REX.W 0x89 ModRM)
#   LEA r64, [r/m] (REX.W 0x8D ModRM)
#   ADD/SUB/XOR/AND/OR/CMP reg-reg (REX.W 0x01/29/31/21/09/39)
#   0x48 0x83 /op imm8 — ALU Immediate
#   CALL / JMP rel32
#   Jcc rel8 (0x70–0x7F)
#   Unbekannte Bytes werden als 'db 0x..' emittiert und der
#   Decoder macht weiter.
# ============================================================

konstante DATEI auf "/home/blacky/dev/moo/beispiele/elf_reader"
konstante MAX_INSN auf 100

# --- Byte/Int Helper ---
funktion u16(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256

funktion u32(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256 + bs[off + 2] * 65536 + bs[off + 3] * 16777216

funktion u64(bs, off):
    setze lo auf u32(bs, off)
    setze hi auf u32(bs, off + 4)
    gib_zurück lo + hi * 4294967296

# Signed 32-bit Interpretation (2er-Komplement)
funktion s32(bs, off):
    setze v auf u32(bs, off)
    wenn v >= 2147483648:
        gib_zurück v - 4294967296
    gib_zurück v

funktion s8(b):
    wenn b >= 128:
        gib_zurück b - 256
    gib_zurück b

setze HEX auf "0123456789abcdef"
funktion hex2(b):
    setze hi auf boden(b / 16)
    setze lo auf b % 16
    gib_zurück HEX[hi] + HEX[lo]

funktion hex8(n):
    # Formatiert eine Zahl als 8-stelliger Hex-String (ohne 0x)
    setze s auf ""
    setze v auf n
    wenn v < 0:
        setze v auf v + 4294967296
    setze i auf 0
    solange i < 8:
        setze nibble auf v % 16
        setze s auf HEX[nibble] + s
        setze v auf boden(v / 16)
        setze i auf i + 1
    gib_zurück s

funktion hex_full(n):
    setze v auf n
    wenn v < 0:
        gib_zurück "-" + hex_full(-v)
    wenn v == 0:
        gib_zurück "0"
    setze s auf ""
    solange v > 0:
        setze nibble auf v % 16
        setze s auf HEX[nibble] + s
        setze v auf boden(v / 16)
    gib_zurück s

# --- Register-Tabelle (Bit-Index 0..15) ---
setze REG64 auf ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]

# --- Jcc Namen 0x70-0x7F ---
setze JCC auf ["jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja", "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"]

# --- Decoder: gibt [bytes_consumed, mnemonic_string] zurueck ---
# Startet bei off, ueberschreitet max. max_bytes
funktion decode_one(bs, off, addr):
    setze start auf off
    setze rex_w auf 0
    setze rex_r auf 0
    setze rex_x auf 0
    setze rex_b auf 0

    # --- endbr64: f3 0f 1e fa (CET) ---
    wenn bs[off] == 0xF3 und bs[off + 1] == 0x0F und bs[off + 2] == 0x1E und bs[off + 3] == 0xFA:
        gib_zurück [4, "endbr64"]

    # --- 66 2e 0f 1f 84 00 00 00 00 00: multi-byte NOP (mit CS prefix) ---
    wenn bs[off] == 0x66 und bs[off + 1] == 0x2E und bs[off + 2] == 0x0F und bs[off + 3] == 0x1F:
        setze modrm auf bs[off + 4]
        setze mod_f auf boden(modrm / 64)
        setze rm_f auf modrm % 8
        setze extra auf 1
        wenn mod_f == 1:
            setze extra auf extra + 1
        wenn mod_f == 2:
            setze extra auf extra + 4
        wenn rm_f == 4:
            setze extra auf extra + 1
        gib_zurück [4 + extra, "nop [cs:..]"]

    # --- 2E/36/3E/26/64/65/66/67 Segment/Operand/Address Prefixes (schlucken) ---
    wenn bs[off] == 0x66:
        setze off auf off + 1
    wenn bs[off] == 0x2E:
        setze off auf off + 1

    # --- REX-Prefix ---
    wenn bs[off] >= 64 und bs[off] < 80:
        setze rex auf bs[off]
        setze rex_w auf boden(rex / 8) % 2
        setze rex_r auf boden(rex / 4) % 2
        setze rex_x auf boden(rex / 2) % 2
        setze rex_b auf rex % 2
        setze off auf off + 1

    setze op auf bs[off]

    # --- Single-byte Opcodes ---
    wenn op == 0x90:
        gib_zurück [off + 1 - start, "nop"]
    wenn op == 0xC3:
        gib_zurück [off + 1 - start, "ret"]
    wenn op == 0xC9:
        gib_zurück [off + 1 - start, "leave"]
    wenn op == 0xCC:
        gib_zurück [off + 1 - start, "int3"]
    wenn op == 0xF4:
        gib_zurück [off + 1 - start, "hlt"]
    wenn op == 0xFA:
        gib_zurück [off + 1 - start, "cli"]
    wenn op == 0xFB:
        gib_zurück [off + 1 - start, "sti"]
    wenn op == 0xFC:
        gib_zurück [off + 1 - start, "cld"]
    wenn op == 0xFD:
        gib_zurück [off + 1 - start, "std"]

    # --- Two-byte 0x0F .. ---
    wenn op == 0x0F:
        setze op2 auf bs[off + 1]
        wenn op2 == 0x05:
            gib_zurück [off + 2 - start, "syscall"]
        wenn op2 == 0x1F:
            # NOP mit ModRM (multi-byte NOP)
            setze modrm auf bs[off + 2]
            setze mod_f auf boden(modrm / 64)
            setze rm_f auf modrm % 8
            setze extra auf 1
            wenn mod_f == 1:
                setze extra auf extra + 1
            wenn mod_f == 2:
                setze extra auf extra + 4
            wenn rm_f == 4:
                setze extra auf extra + 1  # SIB Naeherung
            gib_zurück [off + 2 + extra - start, "nop [...]"]
        # Jcc long (0x0F 0x80-0x8F + rel32)
        wenn op2 >= 0x80 und op2 <= 0x8F:
            setze cc auf op2 - 0x80
            setze rel auf s32(bs, off + 2)
            setze target auf addr + (off + 6 - start) + rel
            gib_zurück [off + 6 - start, JCC[cc] + " 0x" + hex_full(target)]

    # --- PUSH r64: 0x50+r (plus optional REX.B) ---
    wenn op >= 0x50 und op <= 0x57:
        setze r auf op - 0x50 + rex_b * 8
        gib_zurück [off + 1 - start, "push " + REG64[r]]
    wenn op >= 0x58 und op <= 0x5F:
        setze r auf op - 0x58 + rex_b * 8
        gib_zurück [off + 1 - start, "pop " + REG64[r]]

    # --- CALL rel32 (0xE8) ---
    wenn op == 0xE8:
        setze rel auf s32(bs, off + 1)
        setze target auf addr + (off + 5 - start) + rel
        gib_zurück [off + 5 - start, "call 0x" + hex_full(target)]
    # --- JMP rel32 (0xE9) ---
    wenn op == 0xE9:
        setze rel auf s32(bs, off + 1)
        setze target auf addr + (off + 5 - start) + rel
        gib_zurück [off + 5 - start, "jmp 0x" + hex_full(target)]
    # --- JMP rel8 (0xEB) ---
    wenn op == 0xEB:
        setze rel auf s8(bs[off + 1])
        setze target auf addr + (off + 2 - start) + rel
        gib_zurück [off + 2 - start, "jmp short 0x" + hex_full(target)]

    # --- Jcc rel8 (0x70-0x7F) ---
    wenn op >= 0x70 und op <= 0x7F:
        setze cc auf op - 0x70
        setze rel auf s8(bs[off + 1])
        setze target auf addr + (off + 2 - start) + rel
        gib_zurück [off + 2 - start, JCC[cc] + " 0x" + hex_full(target)]

    # --- MOV r64, imm64: REX.W 0xB8+r + imm64 ---
    wenn rex_w == 1 und op >= 0xB8 und op <= 0xBF:
        setze r auf op - 0xB8 + rex_b * 8
        setze imm auf u64(bs, off + 1)
        gib_zurück [off + 9 - start, "mov " + REG64[r] + ", 0x" + hex_full(imm)]

    # --- MOV r32, imm32: 0xB8+r + imm32 (ohne REX.W) ---
    wenn rex_w == 0 und op >= 0xB8 und op <= 0xBF:
        setze r auf op - 0xB8 + rex_b * 8
        setze imm auf u32(bs, off + 1)
        setze name auf REG64[r]
        # e-Alias (rax->eax) weglassen, wir zeigen r32 als rN/eN nicht
        gib_zurück [off + 5 - start, "mov " + name + ", 0x" + hex_full(imm)]

    # --- REX.W 0x8B ModRM: MOV r64, r/m64 ---
    wenn rex_w == 1 und op == 0x8B:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, "mov " + REG64[reg] + ", " + REG64[rm]]
        wenn mod_f == 0 und (rm % 8) == 5:
            setze rel auf s32(bs, off + 2)
            setze target auf addr + (off + 6 - start) + rel
            gib_zurück [off + 6 - start, "mov " + REG64[reg] + ", [rip + 0x" + hex_full(target) + "]"]
        gib_zurück [off + 2 - start, "mov " + REG64[reg] + ", [" + REG64[rm] + "]"]

    # --- REX.W 0x85 ModRM: TEST r/m64, r64 ---
    wenn rex_w == 1 und op == 0x85:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, "test " + REG64[rm] + ", " + REG64[reg]]
        gib_zurück [off + 2 - start, "test [" + REG64[rm] + "], " + REG64[reg]]

    # --- 0xFF /n ModRM: CALL/JMP indirect, PUSH r/m64 ---
    wenn op == 0xFF:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze sub auf boden(modrm / 8) % 8
        setze rm auf modrm % 8 + rex_b * 8
        setze name auf "?"
        wenn sub == 2:
            setze name auf "call"
        wenn sub == 4:
            setze name auf "jmp"
        wenn sub == 6:
            setze name auf "push"
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, name + " " + REG64[rm]]
        # RIP-relative
        wenn mod_f == 0 und (rm % 8) == 5:
            setze rel auf s32(bs, off + 2)
            setze target auf addr + (off + 6 - start) + rel
            gib_zurück [off + 6 - start, name + " [rip + 0x" + hex_full(target) + "]"]
        setze extra auf 0
        wenn mod_f == 1:
            setze extra auf 1
        wenn mod_f == 2:
            setze extra auf 4
        gib_zurück [off + 2 + extra - start, name + " [" + REG64[rm] + "]"]

    # --- 0x31 ModRM: XOR r/m32, r32 (ohne REX, 32-bit) ---
    wenn rex_w == 0 und op == 0x31:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, "xor " + REG64[rm] + ", " + REG64[reg]]
        gib_zurück [off + 2 - start, "xor [" + REG64[rm] + "], " + REG64[reg]]

    # --- 0x89 ModRM: MOV r/m32, r32 (ohne REX) ---
    wenn rex_w == 0 und op == 0x89:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, "mov " + REG64[rm] + ", " + REG64[reg]]
        gib_zurück [off + 2 - start, "mov [" + REG64[rm] + "], " + REG64[reg]]

    # --- 0x48 0x89 ModRM: MOV r/m64, r64 (reg-reg wenn mod=3) ---
    wenn rex_w == 1 und op == 0x89:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, "mov " + REG64[rm] + ", " + REG64[reg]]
        gib_zurück [off + 2 - start, "mov [" + REG64[rm] + "], " + REG64[reg]]

    # --- REX.W 0x01/29/31/21/09/39: ALU r/m64, r64 (reg-reg mod=3) ---
    wenn rex_w == 1 und (op == 0x01 oder op == 0x29 oder op == 0x31 oder op == 0x21 oder op == 0x09 oder op == 0x39):
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        setze name auf "add"
        wenn op == 0x29:
            setze name auf "sub"
        wenn op == 0x31:
            setze name auf "xor"
        wenn op == 0x21:
            setze name auf "and"
        wenn op == 0x09:
            setze name auf "or"
        wenn op == 0x39:
            setze name auf "cmp"
        wenn mod_f == 3:
            gib_zurück [off + 2 - start, name + " " + REG64[rm] + ", " + REG64[reg]]
        gib_zurück [off + 2 - start, name + " [" + REG64[rm] + "], " + REG64[reg]]

    # --- REX.W 0x8D ModRM: LEA r64, m ---
    wenn rex_w == 1 und op == 0x8D:
        setze modrm auf bs[off + 1]
        setze mod_f auf boden(modrm / 64)
        setze reg auf boden(modrm / 8) % 8 + rex_r * 8
        setze rm auf modrm % 8 + rex_b * 8
        setze extra auf 0
        setze disp_s auf ""
        wenn mod_f == 1:
            setze extra auf 1
            setze disp_s auf " + 0x" + hex_full(s8(bs[off + 2]))
        wenn mod_f == 2:
            setze extra auf 4
            setze disp_s auf " + 0x" + hex_full(s32(bs, off + 2))
        # RIP-relativ (mod=0 rm=5)
        wenn mod_f == 0 und (rm % 8) == 5:
            setze rel auf s32(bs, off + 2)
            setze target auf addr + (off + 6 - start) + rel
            gib_zurück [off + 6 - start, "lea " + REG64[reg] + ", [rip + 0x" + hex_full(target) + "]"]
        gib_zurück [off + 2 + extra - start, "lea " + REG64[reg] + ", [" + REG64[rm] + disp_s + "]"]

    # --- REX.W 0xC1 /n imm8: SHIFT r/m64, imm8 ---
    wenn rex_w == 1 und op == 0xC1:
        setze modrm auf bs[off + 1]
        setze sub auf boden(modrm / 8) % 8
        setze rm auf modrm % 8 + rex_b * 8
        setze imm auf bs[off + 2]
        setze name auf "?"
        wenn sub == 0:
            setze name auf "rol"
        wenn sub == 1:
            setze name auf "ror"
        wenn sub == 4:
            setze name auf "shl"
        wenn sub == 5:
            setze name auf "shr"
        wenn sub == 7:
            setze name auf "sar"
        gib_zurück [off + 3 - start, name + " " + REG64[rm] + ", 0x" + hex_full(imm)]

    # --- REX.W 0xD1 /n: SHIFT r/m64, 1 ---
    wenn rex_w == 1 und op == 0xD1:
        setze modrm auf bs[off + 1]
        setze sub auf boden(modrm / 8) % 8
        setze rm auf modrm % 8 + rex_b * 8
        setze name auf "?"
        wenn sub == 4:
            setze name auf "shl"
        wenn sub == 5:
            setze name auf "shr"
        wenn sub == 7:
            setze name auf "sar"
        gib_zurück [off + 2 - start, name + " " + REG64[rm] + ", 1"]

    # --- REX.W 0x83 /n imm8: ALU r/m64, imm8 ---
    wenn rex_w == 1 und op == 0x83:
        setze modrm auf bs[off + 1]
        setze sub auf boden(modrm / 8) % 8
        setze rm auf modrm % 8 + rex_b * 8
        setze imm auf s8(bs[off + 2])
        setze name auf "add"
        wenn sub == 1:
            setze name auf "or"
        wenn sub == 4:
            setze name auf "and"
        wenn sub == 5:
            setze name auf "sub"
        wenn sub == 6:
            setze name auf "xor"
        wenn sub == 7:
            setze name auf "cmp"
        gib_zurück [off + 3 - start, name + " " + REG64[rm] + ", 0x" + hex_full(imm)]

    # --- Unbekannt: als "db" emittieren ---
    gib_zurück [off + 1 - start, "db 0x" + hex2(op) + " (unbekannt)"]

# --- ELF-Parser: finde .text section ---
funktion finde_text(bs):
    setze e_shoff auf u64(bs, 40)
    setze e_shentsize auf u16(bs, 58)
    setze e_shnum auf u16(bs, 60)
    setze e_shstrndx auf u16(bs, 62)

    # .shstrtab offset
    setze str_hdr auf e_shoff + e_shstrndx * e_shentsize
    setze shstrtab_off auf u64(bs, str_hdr + 24)

    setze i auf 0
    solange i < e_shnum:
        setze base auf e_shoff + i * e_shentsize
        setze name_idx auf u32(bs, base + 0)
        # cstring bei shstrtab + name_idx
        setze p auf shstrtab_off + name_idx
        setze ende auf p
        solange bs[ende] != 0:
            setze ende auf ende + 1
        setze nm auf bytes_neu(slice_b(bs, p, ende - p))
        wenn nm == ".text":
            setze erg auf {}
            erg["offset"] = u64(bs, base + 24)
            erg["addr"] = u64(bs, base + 16)
            erg["size"] = u64(bs, base + 32)
            gib_zurück erg
        setze i auf i + 1
    gib_zurück nichts

funktion slice_b(bs, off, len):
    setze r auf []
    setze i auf 0
    solange i < len:
        r.hinzufügen(bs[off + i])
        setze i auf i + 1
    gib_zurück r

# --- Main ---
zeige "=== moo x86-64 Mini-Disassembler ==="
zeige "Datei: " + DATEI

setze bs auf datei_lesen_bytes(DATEI)
zeige "Groesse: " + text(bs.länge()) + " bytes"

setze text auf finde_text(bs)
wenn text == nichts:
    zeige ".text nicht gefunden"

setze text_off auf text["offset"]
setze text_addr auf text["addr"]
setze text_size auf text["size"]
zeige ".text: offset=" + text(text_off) + " addr=0x" + hex_full(text_addr) + " size=" + text(text_size)
zeige ""
zeige "--- Disassembly (bis zu " + text(MAX_INSN) + " Instruktionen) ---"

setze pos auf 0
setze count auf 0
solange pos < text_size und count < MAX_INSN:
    setze abs_off auf text_off + pos
    setze abs_addr auf text_addr + pos
    setze res auf decode_one(bs, abs_off, abs_addr)
    setze len auf res[0]
    setze mne auf res[1]

    # Byte-Darstellung
    setze by auf ""
    setze k auf 0
    solange k < len und k < 7:
        wenn k > 0:
            setze by auf by + " "
        setze by auf by + hex2(bs[abs_off + k])
        setze k auf k + 1
    wenn len > 7:
        setze by auf by + " .."

    # Addr-Formatierung
    setze addr_s auf "0x" + hex_full(abs_addr)

    # Kleine rechtsbuendige Pad-Anleihen via Hilfsvariable
    setze pad auf ""
    solange länge(by) < 24:
        setze by auf by + " "

    zeige addr_s + ":  " + by + "  " + mne

    setze pos auf pos + len
    setze count auf count + 1

zeige ""
zeige "=== Ende (" + text(count) + " Instruktionen) ==="
