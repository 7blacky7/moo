# ============================================================
# moo ELF-Reader — ELF64 Binary Inspector in pure moo
#
# Kompilieren: moo-compiler compile elf_reader.moo -o elf_reader
# Starten:     ./elf_reader
#
# Parst:
#   - ELF Header (Magic, Class, Data, Type, Machine, Entry, ...)
#   - Section Headers + Namen aus .shstrtab
#   - Symbol Table (.symtab + .strtab): listet globale Funktionen
#   - Dynamic Section: DT_NEEDED → libsqlite3, libSDL2, ...
#   - .text Section: erste 32 Bytes als Hex-Dump
# ============================================================

konstante DATEI auf "/home/blacky/dev/moo/beispiele/mysql_client"

# --- Byte/Int Helper (Little-Endian) ---
funktion u16(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256

funktion u32(bs, off):
    gib_zurück bs[off] + bs[off + 1] * 256 + bs[off + 2] * 65536 + bs[off + 3] * 16777216

funktion u64(bs, off):
    setze lo auf u32(bs, off)
    setze hi auf u32(bs, off + 4)
    gib_zurück lo + hi * 4294967296

funktion slice_bytes(bs, off, len):
    setze r auf []
    setze i auf 0
    solange i < len:
        r.hinzufügen(bs[off + i])
        setze i auf i + 1
    gib_zurück r

# String aus Byte-Slice bis NUL
funktion cstring_at(bs, off):
    setze ende auf off
    setze n auf bs.länge()
    solange ende < n und bs[ende] != 0:
        setze ende auf ende + 1
    gib_zurück bytes_neu(slice_bytes(bs, off, ende - off))

# Hex-Darstellung einer einzelnen Zahl 0..255 (2 digits)
setze HEX auf "0123456789abcdef"
funktion byte_hex(b):
    setze hi auf boden(b / 16)
    setze lo auf b % 16
    gib_zurück HEX[hi] + HEX[lo]

funktion hex_dump(bs, off, len):
    setze s auf ""
    setze i auf 0
    solange i < len:
        wenn i > 0:
            wenn i % 16 == 0:
                setze s auf s + "\n                 "
            sonst:
                wenn i % 4 == 0:
                    setze s auf s + "  "
                sonst:
                    setze s auf s + " "
        setze s auf s + byte_hex(bs[off + i])
        setze i auf i + 1
    gib_zurück s

# --- ELF Konstanten ---
funktion e_type_name(t):
    wenn t == 1:
        gib_zurück "REL (Relocatable)"
    wenn t == 2:
        gib_zurück "EXEC (Executable)"
    wenn t == 3:
        gib_zurück "DYN (Shared/PIE)"
    wenn t == 4:
        gib_zurück "CORE"
    gib_zurück "OTHER(" + text(t) + ")"

funktion e_machine_name(m):
    wenn m == 0x3E:
        gib_zurück "x86_64"
    wenn m == 0xB7:
        gib_zurück "aarch64"
    wenn m == 0x03:
        gib_zurück "i386"
    wenn m == 0x28:
        gib_zurück "arm"
    gib_zurück "OTHER(" + text(m) + ")"

funktion sh_type_name(t):
    wenn t == 0:
        gib_zurück "NULL"
    wenn t == 1:
        gib_zurück "PROGBITS"
    wenn t == 2:
        gib_zurück "SYMTAB"
    wenn t == 3:
        gib_zurück "STRTAB"
    wenn t == 4:
        gib_zurück "RELA"
    wenn t == 5:
        gib_zurück "HASH"
    wenn t == 6:
        gib_zurück "DYNAMIC"
    wenn t == 7:
        gib_zurück "NOTE"
    wenn t == 8:
        gib_zurück "NOBITS"
    wenn t == 9:
        gib_zurück "REL"
    wenn t == 11:
        gib_zurück "DYNSYM"
    wenn t == 14:
        gib_zurück "INIT_ARRAY"
    wenn t == 15:
        gib_zurück "FINI_ARRAY"
    wenn t == 16:
        gib_zurück "PREINIT_ARRAY"
    wenn t == 17:
        gib_zurück "GROUP"
    wenn t == 18:
        gib_zurück "SYMTAB_SHNDX"
    gib_zurück "OTHER(" + text(t) + ")"

# --- Main ---
zeige "=== moo ELF-Reader ==="
zeige "Datei: " + DATEI

wenn nicht datei_existiert(DATEI):
    zeige "Datei existiert nicht"

setze bs auf datei_lesen_bytes(DATEI)
zeige "Groesse: " + text(bs.länge()) + " bytes"

# ELF-Magic prüfen
wenn bs[0] != 127 oder bs[1] != 69 oder bs[2] != 76 oder bs[3] != 70:
    zeige "Kein ELF-Magic (0x7F 'E' 'L' 'F')"

setze class_b auf bs[4]
setze data_b auf bs[5]
setze ei_ver auf bs[6]
setze osabi auf bs[7]

setze klasse_s auf "ELF32"
wenn class_b == 2:
    setze klasse_s auf "ELF64"

setze endian_s auf "BE"
wenn data_b == 1:
    setze endian_s auf "LE"

zeige ""
zeige "--- ELF Header ---"
zeige "  Class:      " + klasse_s
zeige "  Data:       " + endian_s
zeige "  ELF Vers:   " + text(ei_ver)
zeige "  OS/ABI:     " + text(osabi)

setze e_type auf u16(bs, 16)
setze e_machine auf u16(bs, 18)
setze e_version auf u32(bs, 20)
setze e_entry auf u64(bs, 24)
setze e_phoff auf u64(bs, 32)
setze e_shoff auf u64(bs, 40)
setze e_flags auf u32(bs, 48)
setze e_ehsize auf u16(bs, 52)
setze e_phentsize auf u16(bs, 54)
setze e_phnum auf u16(bs, 56)
setze e_shentsize auf u16(bs, 58)
setze e_shnum auf u16(bs, 60)
setze e_shstrndx auf u16(bs, 62)

zeige "  Type:       " + e_type_name(e_type)
zeige "  Machine:    " + e_machine_name(e_machine)
zeige "  Version:    " + text(e_version)
zeige "  Entry:      0x" + text(e_entry)
zeige "  PhOff:      " + text(e_phoff) + " (" + text(e_phnum) + " entries a " + text(e_phentsize) + " bytes)"
zeige "  ShOff:      " + text(e_shoff) + " (" + text(e_shnum) + " entries a " + text(e_shentsize) + " bytes)"
zeige "  ShStrNdx:   " + text(e_shstrndx)

# --- Section Headers einlesen ---
# Layout pro Section Header (64 bytes):
#   [0..4)   sh_name   u32
#   [4..8)   sh_type   u32
#   [8..16)  sh_flags  u64
#   [16..24) sh_addr   u64
#   [24..32) sh_offset u64
#   [32..40) sh_size   u64
#   [40..44) sh_link   u32
#   [44..48) sh_info   u32
#   [48..56) sh_addralign u64
#   [56..64) sh_entsize u64

funktion read_shdr(bs, base):
    setze h auf {}
    h["name"] = u32(bs, base + 0)
    h["type"] = u32(bs, base + 4)
    h["flags"] = u64(bs, base + 8)
    h["addr"] = u64(bs, base + 16)
    h["offset"] = u64(bs, base + 24)
    h["size"] = u64(bs, base + 32)
    h["link"] = u32(bs, base + 40)
    h["info"] = u32(bs, base + 44)
    h["align"] = u64(bs, base + 48)
    h["entsize"] = u64(bs, base + 56)
    gib_zurück h

setze shdrs auf []
setze i auf 0
solange i < e_shnum:
    setze base auf e_shoff + i * e_shentsize
    shdrs.hinzufügen(read_shdr(bs, base))
    setze i auf i + 1

# Section Header String Table (sh_strtab) Offset
setze sh_shstrtab auf shdrs[e_shstrndx]
setze shstrtab_off auf sh_shstrtab["offset"]

funktion sh_name(bs, strtab_off, name_idx):
    gib_zurück cstring_at(bs, strtab_off + name_idx)

zeige ""
zeige "--- Sections (" + text(e_shnum) + ") ---"
zeige "  IDX  TYPE         FLAGS  ADDR              OFFSET   SIZE      NAME"
setze i auf 0
solange i < e_shnum:
    setze sh auf shdrs[i]
    setze nm auf sh_name(bs, shstrtab_off, sh["name"])
    setze idx_s auf text(i)
    wenn i < 10:
        setze idx_s auf " " + idx_s
    zeige "  " + idx_s + "   " + sh_type_name(sh["type"]) + "    0x" + text(sh["flags"]) + "    0x" + text(sh["addr"]) + "   " + text(sh["offset"]) + "   " + text(sh["size"]) + "   " + nm
    setze i auf i + 1

# --- Finde spezielle Sections ---
setze sym_idx auf -1
setze dyn_idx auf -1
setze dynstr_idx auf -1
setze dynsym_idx auf -1
setze text_idx auf -1
setze i auf 0
solange i < e_shnum:
    setze sh auf shdrs[i]
    setze nm auf sh_name(bs, shstrtab_off, sh["name"])
    wenn sh["type"] == 2:
        setze sym_idx auf i
    wenn sh["type"] == 6:
        setze dyn_idx auf i
    wenn sh["type"] == 11:
        setze dynsym_idx auf i
    wenn nm == ".dynstr":
        setze dynstr_idx auf i
    wenn nm == ".text":
        setze text_idx auf i
    setze i auf i + 1

# --- .text Hex-Dump ---
wenn text_idx >= 0:
    zeige ""
    zeige "--- .text (erste 32 bytes) ---"
    setze th auf shdrs[text_idx]
    setze th_off auf th["offset"]
    setze dump_len auf 32
    wenn th["size"] < dump_len:
        setze dump_len auf th["size"]
    zeige "  Offset 0x" + text(th_off) + ":"
    zeige "                 " + hex_dump(bs, th_off, dump_len)

# --- Symbol Table (.symtab) ---
wenn sym_idx >= 0:
    setze sh auf shdrs[sym_idx]
    setze strtab auf shdrs[sh["link"]]
    setze strtab_off auf strtab["offset"]
    setze sym_count auf boden(sh["size"] / 24)
    zeige ""
    zeige "--- Symbol Table (" + text(sym_count) + " Eintraege) ---"
    zeige "  Zeige erste 30 GLOBAL FUNC-Symbole"
    setze i auf 0
    setze gezeigt auf 0
    solange i < sym_count und gezeigt < 30:
        setze syo auf sh["offset"] + i * 24
        setze st_name_i auf u32(bs, syo + 0)
        setze st_info auf bs[syo + 4]
        setze st_bind auf boden(st_info / 16)
        setze st_type auf st_info % 16
        setze st_value auf u64(bs, syo + 8)
        setze st_size auf u64(bs, syo + 16)
        # BIND=1 (GLOBAL) und TYPE=2 (FUNC)
        wenn st_bind == 1 und st_type == 2:
            setze nm auf cstring_at(bs, strtab_off + st_name_i)
            wenn länge(nm) > 0:
                zeige "  0x" + text(st_value) + "  size=" + text(st_size) + "  " + nm
                setze gezeigt auf gezeigt + 1
        setze i auf i + 1

wenn sym_idx < 0:
    zeige ""
    zeige "Keine .symtab (statische Symbole vermutlich weggestrippt)"

# --- Dynamic Section: DT_NEEDED ---
wenn dyn_idx >= 0 und dynstr_idx >= 0:
    setze dh auf shdrs[dyn_idx]
    setze dstrtab auf shdrs[dynstr_idx]["offset"]
    setze entries auf boden(dh["size"] / 16)
    zeige ""
    zeige "--- Dynamic Section (DT_NEEDED) ---"
    setze i auf 0
    solange i < entries:
        setze eo auf dh["offset"] + i * 16
        setze d_tag auf u64(bs, eo)
        setze d_val auf u64(bs, eo + 8)
        wenn d_tag == 0:
            setze i auf entries
        sonst:
            wenn d_tag == 1:
                setze lib auf cstring_at(bs, dstrtab + d_val)
                zeige "  NEEDED  " + lib
            setze i auf i + 1

zeige ""
zeige "=== Fertig ==="
