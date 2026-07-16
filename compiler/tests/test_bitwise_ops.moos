# Regression-Tests (Audit Block A4): bitweise Operatoren.
# Audit-HIGH war falscher Alarm — code gen routet korrekt auf
# moo_bitand/bitor/bitxor/bitnot/lshift/rshift. Dieser Test
# verhindert, dass die Route je wieder auf die logischen Ops faellt.

zeige 12 & 10       # 0b1100 & 0b1010 = 0b1000 = 8
zeige 12 | 10       # 0b1100 | 0b1010 = 0b1110 = 14
zeige 12 ^ 10       # 0b1100 ^ 0b1010 = 0b0110 = 6
zeige 1 << 4        # 16
zeige 256 >> 4      # 16
zeige ~5            # -6 (two's complement)
zeige 0xFF & 0x0F   # 15
zeige 0xF0 | 0x0F   # 255
