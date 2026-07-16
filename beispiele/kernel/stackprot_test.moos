# stackprot_test.moo — P012-A4: Stack-Protector-Fail-Test (Kernel-Panic).
#
# Build (One-Shot-Pipeline):
#   moo-compiler compile beispiele/kernel/stackprot_test.moo \
#       --no-stdlib --target x86_64-bare --kernel -o stackprot_test.elf
# Boot-Test: scripts/sp-smoke.sh (GRUB-ISO + QEMU).
#
# kern_stackprot_selbsttest kippt den globalen Canary innerhalb einer
# instrumentierten C-Funktion (moo_bare_stackprot.c) -> Epilog-Check
# schlaegt fehl -> __stack_chk_fail -> [KERN-PANIK] STACK-PROTECTOR ...
# Das beweist: Stack-Korruption endet als LAUTER Panic, nicht als Silent
# Corruption. Faellt der C-Compiler auf -fno-stack-protector zurueck
# (kein -mstack-protector-guard=global), kehrt der Selbsttest mit der
# Meldung 'kein Canary-Fail' zurueck — sp-smoke.sh wertet das als
# transparenten SKIP, nicht als Erfolg.

# 'MOO-SP-START' + \n
funktion marker_start():
    unsicher:
        kern_seriell_zeichen(77)   # M
        kern_seriell_zeichen(79)   # O
        kern_seriell_zeichen(79)   # O
        kern_seriell_zeichen(45)   # -
        kern_seriell_zeichen(83)   # S
        kern_seriell_zeichen(80)   # P
        kern_seriell_zeichen(45)   # -
        kern_seriell_zeichen(83)   # S
        kern_seriell_zeichen(84)   # T
        kern_seriell_zeichen(65)   # A
        kern_seriell_zeichen(82)   # R
        kern_seriell_zeichen(84)   # T
        kern_seriell_zeichen(10)   # \n

unsicher:
    marker_start()
    # Provoziert den Canary-Fail — kehrt bei aktivem SSP NIE zurueck.
    kern_stackprot_selbsttest()
    # Nur erreicht, wenn SSP inaktiv (Compiler-Fallback) — sauber anhalten.
    solange wahr:
        halt()
