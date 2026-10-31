#define main moo_existing_tokenizer_main
#include "test_tokenizer_asan.c"
#undef main
#include "../moo_tok_segment.h"

int main(void) {
    /* Parser-BPE: Determinismus, Byte-Roundtrip und keine Grenzueberschreitung. */
    {
      const char input[] = "alpha alpha\n```moo\n  x = alpha + alpha\n```\n";
      MooValue x = strn_(input, (int32_t)(sizeof(input) - 1));
      MooValue p = moo_tok_trainiere_parser(x, num_(300));
      MooValue q = moo_tok_trainiere_parser(x, num_(300));
      CHECK(!moo_error_flag && str_eq_bytes(p, q), "parser deterministisch");
      CHECK(roundtrip(p, x, 300, "parser") == 0, "parser byte-roundtrip");
      char all[256]; for (int i = 0; i < 256; i++) all[i] = (char)i;
      MooValue bytes = strn_(all, 256);
      CHECK(roundtrip(p, bytes, 300, "parser alle Bytes") == 0, "parser NUL und ungueltige UTF-8 Bytes");
      moo_release(bytes);
      MooValue corrupt = strn_(MV_STR(p)->chars, MV_STR(p)->length);
      MV_STR(corrupt)->chars[MV_STR(corrupt)->length - 1] ^= 1;
      MooValue bad = moo_tok_info(corrupt);
      CHECK(moo_error_flag && bad.tag == MOO_NONE, "parser SHA-Manipulation erkannt");
      fehler_reset(); moo_release(corrupt);
      size_t bounds[sizeof(input)];
      size_t nb = moo_tok_segmente((const uint8_t *)input, sizeof(input)-1, bounds, sizeof(input));
      MooValue ids = moo_tok_kodiere(p, x);
      CHECK(!moo_error_flag && ids.tag == MOO_TENSOR, "parser encode");
      int32_t shape[] = {1};
      MooTensor *one = moo_tensor_raw(1, shape);
      MooValue single; single.tag = MOO_TENSOR; moo_val_set_ptr(&single, one);
      size_t off = 0, bi = 0;
      for (int64_t i = 0; i < MV_TENSOR(ids)->size; i++) {
        one->data[0] = MV_TENSOR(ids)->data[i];
        MooValue piece = moo_tok_dekodiere(p, single);
        CHECK(!moo_error_flag && piece.tag == MOO_STRING, "parser token dekodiert");
        while (bi < nb && off == bounds[bi]) bi++;
        off += (size_t)MV_STR(piece)->length;
        CHECK(bi < nb && off <= bounds[bi], "token bleibt innerhalb Stueckgrenze");
        moo_release(piece);
      }
      CHECK(off == sizeof(input)-1, "parser Grenzen vollstaendig");
      char path[] = "/tmp/moo_parser_bpe_XXXXXX";
      int fd = mkstemp(path); CHECK(fd >= 0, "parser tempdatei"); close(fd);
      MooValue pv = str_(path), ok = moo_tok_speichern(p, pv);
      CHECK(!moo_error_flag && ok.tag == MOO_BOOL && MV_BOOL(ok), "parser speichern");
      MooValue loaded = moo_tok_laden(pv);
      CHECK(!moo_error_flag && str_eq_bytes(p, loaded), "parser laden identisch");
      CHECK(roundtrip(loaded, x, 300, "parser geladen") == 0, "parser geladen roundtrip");
      MooValue loaded_ids = moo_tok_kodiere(loaded, x);
      CHECK(loaded_ids.tag == MOO_TENSOR && MV_TENSOR(loaded_ids)->size == MV_TENSOR(ids)->size,
            "parser IDs nach Laden gleiche Laenge");
      for (int64_t i = 0; i < MV_TENSOR(ids)->size; i++)
        CHECK(MV_TENSOR(ids)->data[i] == MV_TENSOR(loaded_ids)->data[i], "parser IDs nach Laden gleich");
      moo_release(loaded_ids);
      remove(path); moo_release(loaded); moo_release(pv); moo_release(single);
      moo_release(ids); moo_release(p); moo_release(q); moo_release(x);
    }

    return 0;
}
