# P011-A2 Regressionstest: Fixed-width-Cast-Builtins als_u8..als_u64.
# Semantik: llvm.fptosi.sat (NaN->0, poison-frei) + trunc-Wrap (Zweierkomplement)
# + unsigned zurueck nach double. DE- und EN-Aliase.
# als_u64 nahe 2^64 ist hier bewusst NICHT geprueft (53-bit-Rundungsdarstellung).

zeige als_u8(256)
zeige als_u8(-1)
zeige als_u8(255)
zeige als_u16(65535.7)
zeige als_u32(4294967296)
zeige als_u32(4294967295)
zeige als_u16(3.9)
zeige as_u8(257)
zeige als_u64(12345678)
