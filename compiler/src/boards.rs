//! boards.rs — Minimale Board-Profil-Registry (P012-C1).
//!
//! FORMAT-ENTSCHEIDUNG: Rust-Tabelle statt JSON/TOML — null externe
//! Parser-Abhaengigkeiten, typsicher/compile-gerueft, und die Profile sind
//! eng mit der Compiler-Logik verzahnt (Triple-Routing B1, Toolchain-Wahl
//! B4, Linker-Script-Auswahl). Ein dateibasiertes Format kann spaeter
//! ADDITIV kommen (P012-C2), diese Tabelle bleibt die eingebaute Registry.
//!
//! Adress-Quellen: QEMU virt-Memmap (hw/arm/virt.c, stabil dokumentiert);
//! x86_64-Referenz = bestehender Multiboot2-Kernel-Pfad (Plan-010/011).
//!
//! WARTBARKEIT — neues Board hinzufuegen (P012-C2-Checkliste):
//!   1. Profil unten in BOARDS ergaenzen; NUR verifizierbare Werte
//!      eintragen (Datenblatt/QEMU-Memmap-Quelle im Kommentar nennen).
//!   2. Unverifizierte Boards (keine Hardware/kein Smoke) MUESSEN
//!      'DRAFT' in der beschreibung tragen und duerfen KEIN Gate sein.
//!   3. target muss ein resolve_triple-Alias sein (B1) und die
//!      Kernel-Toolchain kennen (B4, KernelArch) — sonst klare Diagnose.
//!   4. Smoke-Script pro Boot-Schiene ergaenzen (Konvention: transparenter
//!      Tooling-Skip, Final-Gate nur mit echtem Boot).
//! BEWUSST NICHT in P012: Device-Tree-Parser (keine libfdt-Abhaengigkeit;
//! der DT-Pointer in x0 wird ab D5 nur durchgereicht/geloggt), Jetson
//! (H2 DEFERRED bis User-Go + Hardware).

pub struct BoardProfile {
    pub name: &'static str,
    pub beschreibung: &'static str,
    /// Kurzer Target-Name (Input fuer CodeGen::resolve_triple), z.B. "aarch64-bare".
    pub target: &'static str,
    /// Kernel-Link-/Load-Adresse (Linker-Script-Basis, D5/E1).
    pub load_addr: u64,
    /// "pl011" (MMIO) | "16550-port" (x86 Port-I/O — uart_base ist dann ein PORT).
    pub uart_kind: &'static str,
    pub uart_base: u64,
    /// "generic-timer" (ARM cntfrq/cntpct) | "pit-8254" (x86).
    pub timer_kind: &'static str,
    /// Interrupt-Controller-Art: "gicv2" (ARM) | "pic-8259" (x86 Legacy).
    pub intc_kind: &'static str,
    /// GICv2-Adressen (nur ARM; QEMU virt mit gic-version=2).
    pub gic_dist_base: Option<u64>,
    pub gic_cpu_base: Option<u64>,
    /// Linker-Script relativ zum Quellbaum-Root; None = Pipeline-Default.
    pub linker_script: Option<&'static str>,
}

pub static BOARDS: &[BoardProfile] = &[
    BoardProfile {
        name: "qemu-virt-aarch64",
        beschreibung: "QEMU -machine virt (AArch64): PL011-UART, Generic Timer, GICv2",
        target: "aarch64-bare",
        // RAM ab 0x4000_0000; +0x80000 Text-Offset-Konvention. Wird beim
        // echten Link/Boot (P012-D5/E1) verifiziert.
        load_addr: 0x4008_0000,
        uart_kind: "pl011",
        uart_base: 0x0900_0000,
        timer_kind: "generic-timer",
        intc_kind: "gicv2",
        // Gilt fuer -machine virt,gic-version=2 — P012-D4 prueft den
        // QEMU-Default und ergaenzt ggf. GICv3-Redistributor-Adressen.
        gic_dist_base: Some(0x0800_0000),
        gic_cpu_base: Some(0x0801_0000),
        linker_script: Some("beispiele/kernel/linker-arm64-virt.ld"), // P012-D5
    },
    BoardProfile {
        name: "raspi4",
        // DRAFT-Konvention (C2-Checkliste Regel 2): unverifiziert -> kein
        // Gate, kein Smoke, Build warnt. Quellen: BCM2711 ARM Peripherals
        // Datenblatt (UART0/GIC-400, Low-Peripheral-Mode) + Raspi-Firmware-
        // Konvention (kernel8.img @ 0x80000 bei arm_64bit=1).
        beschreibung: "DRAFT (unverifiziert, kein Gate): Raspberry Pi 4 / BCM2711 — echte Hardware-Verifikation steht aus (P012-H1)",
        target: "aarch64-bare",
        load_addr: 0x0008_0000,       // Firmware laedt kernel8.img hierher
        uart_kind: "pl011",
        uart_base: 0xFE20_1000,       // UART0 (PL011), Low-Peripheral-Mode
        timer_kind: "generic-timer",  // cntfrq setzt die Firmware (54 MHz)
        intc_kind: "gicv2",           // GIC-400 ist eine GICv2-Implementierung
        gic_dist_base: Some(0xFF84_1000),
        gic_cpu_base: Some(0xFF84_2000),
        linker_script: Some("beispiele/kernel/linker-arm64-raspi4-draft.ld"),
    },
    BoardProfile {
        name: "qemu-pc-x86_64",
        beschreibung: "QEMU PC (x86_64): Multiboot2-Referenzpfad, 16550-COM1, PIT",
        target: "x86_64-bare",
        // 1-MB-Multiboot2-Konvention (konsistent mit beispiele/kernel/linker.ld;
        // A3-#PF-Beweis zeigte RIP 0x1019a0).
        load_addr: 0x0010_0000,
        uart_kind: "16550-port",
        uart_base: 0x3F8, // PORT, keine MMIO-Adresse (uart_kind unterscheidet das)
        timer_kind: "pit-8254",
        intc_kind: "pic-8259",
        gic_dist_base: None,
        gic_cpu_base: None,
        linker_script: None, // Pipeline-Default: beispiele/kernel/linker.ld
    },
];

pub fn find_board(name: &str) -> Option<&'static BoardProfile> {
    BOARDS.iter().find(|b| b.name == name)
}

pub fn board_names() -> Vec<&'static str> {
    BOARDS.iter().map(|b| b.name).collect()
}
