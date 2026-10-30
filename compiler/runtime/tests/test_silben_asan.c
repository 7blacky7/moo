/* SIL-10: unabhängige Black-Box-Tests für Silben-Tokenisierung. */
#include "../moo_silben.h"
#include "../moo_krypto.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#define unlink _unlink
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) { moo_release(error); moo_error_flag = 1; }
void moo_socket_free(void *p) { (void)p; }
void moo_thread_free(void *p) { (void)p; }
void moo_channel_free(void *p) { (void)p; }
void moo_db_free(void *p) { (void)p; }
void moo_db_stmt_free(void *p) { (void)p; }
void moo_window_free(void *p) { (void)p; }
void moo_web_free(void *p) { (void)p; }
void moo_voxel_free(void *p) { (void)p; }
void moo_frame_free(void *p) { (void)p; }
void moo_gif_handle_free(void *p) { (void)p; }
void moo_video_handle_free(void *p) { (void)p; }
void moo_kamera_free(void *p) { (void)p; }
void moo_mikro_free(void *p) { (void)p; }

static int checks;
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr, "FAIL: %s (%d)\n", msg, __LINE__); return 1; } checks++; } while (0)
static MooValue str(const char *s) { return moo_string_new(s); }
static int same(MooValue a, MooValue b) {
    if (a.tag != MOO_STRING || b.tag != MOO_STRING) return 0;
    MooString *x = MV_STR(a), *y = MV_STR(b);
    return x->length == y->length && memcmp(x->chars, y->chars, (size_t)x->length) == 0;
}
static int same_ids(MooValue a, MooValue b) {
    if (a.tag != MOO_TENSOR || b.tag != MOO_TENSOR) return 0;
    MooTensor *x = MV_TENSOR(a), *y = MV_TENSOR(b);
    if (x->size != y->size) return 0;
    for (int64_t i = 0; i < x->size; ++i) if (x->data[i] != y->data[i]) return 0;
    return 1;
}
static MooValue ids1(float n) {
    int32_t shape[1] = { 1 };
    MooTensor *t = moo_tensor_raw(1, shape);
    t->data[0] = n;
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t); return v;
}
static int test_roundtrip(MooValue tok, const char *input) {
    MooValue x = str(input), ids = moo_silben_kodiere(tok, x);
    if (moo_error_flag || ids.tag != MOO_TENSOR) { moo_release(x); moo_release(ids); return 0; }
    MooValue back = moo_silben_dekodiere(tok, ids);
    int ok = !moo_error_flag && same(x, back);
    moo_release(x); moo_release(ids); moo_release(back); return ok;
}
static MooValue read_pattern(void) {
    FILE *f = fopen("compiler/runtime/data/silben/dehyphn-x-2024-02-28.pat", "rb");
    if (!f) return moo_none();
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return moo_none(); }
    long n = ftell(f);
    if (n <= 0 || n > 10000000 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return moo_none(); }
    char *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return moo_none(); }
    size_t got = fread(buf, 1, (size_t)n, f);
    int closed = fclose(f);
    if (got != (size_t)n || closed != 0) { free(buf); return moo_none(); }
    MooValue value = moo_string_new_len(buf, (int32_t)n);
    free(buf);
    return value;
}
int main(void) {
    MooValue pattern = read_pattern();
    CHECK(pattern.tag == MOO_STRING, "versionierte Musterdatei lesbar");
    MooValue corpus = str("aaaa bbbb ä ä Wald Wald\n"), limit = moo_number(260);
    MooValue a = moo_silben_trainiere(corpus, limit, pattern);
    MooValue b = moo_silben_trainiere(corpus, limit, pattern);
    CHECK(!moo_error_flag && a.tag == MOO_STRING && b.tag == MOO_STRING, "trainiert");
    CHECK(same(a, b), "deterministisches Artefakt");
    MooValue h = moo_silben_hash(a), h2 = moo_silben_hash(b);
    CHECK(!moo_error_flag && h.tag == MOO_STRING && MV_STR(h)->length == 64, "SHA256");
    CHECK(same(h, h2), "stabiler Hash");
    CHECK(test_roundtrip(a, "Straße, Ähre!\r\n"), "Umlaute und Zeilenenden");
    CHECK(test_roundtrip(a, "e\xCC\x81 vs. é"), "keine stille Normalisierung");
    CHECK(test_roundtrip(a, "😀 👩‍👩‍👧‍👦"), "unbekanntes Unicode");
    CHECK(test_roundtrip(a, "Thal und Thür!"), "historische Schreibungen");
    MooValue input = str("äX");
    MooValue before = moo_silben_kodiere(a, input);
    CHECK(!moo_error_flag && before.tag == MOO_TENSOR, "encode");
    MooValue z = str("Z");
    MooValue probe = moo_silben_kodiere(a, z);
    moo_release(z);
    CHECK(!moo_error_flag && probe.tag == MOO_TENSOR, "unbekanntes ASCII");
    CHECK(MV_TENSOR(probe)->size == 1 && MV_TENSOR(probe)->data[0] == 90.0f, "Bytefallback ID");
    moo_release(probe);
    MooValue after = moo_silben_kodiere(a, input);
    CHECK(!moo_error_flag && same_ids(before, after), "encode verändert IDs nicht");
    moo_release(after);

    MooValue other_corpus = str("öööö cccc öööö dddd");
    MooValue other = moo_silben_trainiere(other_corpus, limit, pattern);
    MooValue other_hash = moo_silben_hash(other);
    CHECK(!moo_error_flag && !same(h, other_hash), "unterschiedliche Tokenbelegung verändert Hash");
    moo_release(other_hash); moo_release(other); moo_release(other_corpus);

    MooValue empty = str("");
    MooValue out = moo_silben_kodiere(a, empty);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "leere Eingabe abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(empty);
    const char bad_utf8[] = { (char)0xc0, (char)0xaf };
    MooValue bad = moo_string_new_len(bad_utf8, 2);
    out = moo_silben_kodiere(a, bad);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "überlanges UTF-8 abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(bad);
    const char with_nul[] = { 'a', 0, 'b' };
    bad = moo_string_new_len(with_nul, 3);
    out = moo_silben_kodiere(a, bad);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "NUL abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(bad);
    const float invalid[] = { -1.0f, 0.5f, NAN, INFINITY, 16777216.0f };
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        MooValue v = ids1(invalid[i]); out = moo_silben_dekodiere(a, v);
        CHECK(moo_error_flag && out.tag == MOO_NONE, "ungültige Token-ID abgelehnt");
        moo_error_flag = 0; moo_release(out); moo_release(v);
    }
    out = moo_silben_trainiere(corpus, moo_number(255), pattern);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "Vokabulargrenze unter 256 abgelehnt");
    moo_error_flag = 0; moo_release(out);
    out = moo_silben_trainiere(corpus, moo_number(256.5), pattern);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "gebrochene Vokabulargrenze abgelehnt");
    moo_error_flag = 0; moo_release(out);
    bad = moo_string_new_len(MV_STR(a)->chars, 12);
    out = moo_silben_info(bad);
    CHECK(moo_error_flag && out.tag == MOO_NONE, "abgeschnittenes Artefakt abgelehnt");
    moo_error_flag = 0; moo_release(out); moo_release(bad);

    /* Amtliches Regelwerk 2024 §88, https://grammis.ids-mannheim.de/rechtschreibung/6216
     * Positive Teilorakel: Zu|cker und Bag|ger, ohne Trennstrich im Text.
     * Vokabular enthält die Segmente, daher ist keine Bytefallback-Grenze
     * mit einer Sprachgrenze zu verwechseln. */
    {
        MooValue source = str("Zucker Bagger Eltern Hopfen Wüste singen sitzen Welle Pappe lachen Zucker Bagger Eltern Hopfen Wüste singen sitzen Welle Pappe lachen");
        MooValue t = moo_silben_trainiere(source, moo_number(512), pattern);
        CHECK(!moo_error_flag && t.tag == MOO_STRING, "Sprachorakel trainiert");
        const char *words[] = { "Zucker", "Bagger", "Eltern", "Hopfen", "Wüste", "singen", "sitzen", "Welle", "Pappe", "lachen" };
        const char *parts[][2] = { { "Zu", "cker" }, { "Bag", "ger" }, { "El", "tern" }, { "Hop", "fen" }, { "Wüs", "te" }, { "sin", "gen" }, { "sit", "zen" }, { "Wel", "le" }, { "Pap", "pe" }, { "la", "chen" } };
        for (int j = 0; j < 10; ++j) {
            MooValue w = str(words[j]);
            MooValue v = moo_silben_kodiere(t, w);
            CHECK(!moo_error_flag && v.tag == MOO_TENSOR && MV_TENSOR(v)->size == 2,
                  "bekannte Silben erzeugen zwei Tokens");
            for (int k = 0; k < 2; ++k) {
                float id = MV_TENSOR(v)->data[k];
                CHECK(id >= 256.0f, "kein Bytefallback im Sprachorakel");
                MooValue one = ids1(id), decoded = moo_silben_dekodiere(t, one);
                MooValue expected = str(parts[j][k]);
                CHECK(!moo_error_flag && same(decoded, expected), "belegte Trennstelle");
                moo_release(expected); moo_release(decoded); moo_release(one);
            }
            moo_release(v); moo_release(w);
        }
        moo_release(t); moo_release(source);
    }

    /* Referenzstellen als Teilorakel, kein Anspruch auf alle Trennstellen.
     * A: amtliches Regelwerk §86-90:
     * https://grammis.ids-mannheim.de/rechtschreibung/6216
     * https://grammis.ids-mannheim.de/rechtschreibung/6217
     * Übrige einfache Wörter unabhängig zusammengestellt und nach §87/88
     * bewertet; keine Übernahme von Quellen mit eingeschränktem Kopierrecht.
     * 2/2-Mindestgrenzen: nur Fälle mit mindestens zwei Zeichen je Seite.
     * Alternative Trennungen sind disjunktiv, keine kombinierte Pflicht. */
    {
        struct Ref { const char *word, *left, *alternative; };
        const struct Ref refs[] = {
            {"Auge","Au",NULL}, {"Brezel","Bre",NULL}, {"Hexe","He",NULL},
            {"Eltern","El",NULL}, {"Hopfen","Hop",NULL}, {"Wüste","Wüs",NULL},
            {"singen","sin",NULL}, {"sitzen","sit",NULL}, {"Bagger","Bag",NULL},
            {"Welle","Wel",NULL}, {"Komma","Kom",NULL}, {"rennen","ren",NULL},
            {"Pappe","Pap",NULL}, {"müssen","müs",NULL}, {"Rechner","Rech",NULL},
            {"freundlich","freund",NULL}, {"Karpfen","Karp",NULL},
            {"lachen","la",NULL}, {"waschen","wa",NULL}, {"Saphir","Sa",NULL},
            {"blicken","bli",NULL}, {"Zyklus","Zyk","Zy"}, {"Februar","Feb","Fe"},
            {"hinauf","hin","hi"}, {"darum","dar","da"}, {"warum","war","wa"},
            {"Helikopter","Heliko","Helikop"}, {"interessant","inter","inte"},
            {"Banane","Ba",NULL},
            {"Kamel","Ka",NULL},
            {"Hose","Ho",NULL},
            {"Tafel","Ta",NULL},
            {"Tomate","To",NULL},
            {"Taxi","Ta",NULL},
            {"Limonade","Li",NULL},
            {"Hasel","Ha",NULL},
            {"Nudel","Nu",NULL},
            {"Salat","Sa",NULL},
            {"Melone","Me",NULL},
            {"Pilot","Pi",NULL},
            {"Kanine","Ka",NULL},
            {"Paket","Pa",NULL},
            {"Vogel","Vo",NULL},
            {"Nagel","Na",NULL},
            {"Wagen","Wa",NULL},
            {"Rosen","Ro",NULL},
            {"Hafen","Ha",NULL},
            {"Hofen","Ho",NULL},
            {"Boden","Bo",NULL},
            {"Laden","La",NULL},
            {"Gabel","Ga",NULL},
            {"Nebel","Ne",NULL},
            {"Segel","Se",NULL},
            {"Regen","Re",NULL},
            {"Reden","Re",NULL},
            {"Lager","La",NULL},
            {"Feder","Fe",NULL},
            {"Leber","Le",NULL},
            {"Linie","Li",NULL},
            {"Losen","Lo",NULL},
            {"Sofa","So",NULL},
            {"Lupe","Lu",NULL},
            {"Motor","Mo",NULL},
            {"Maler","Ma",NULL},
            {"Rotor","Ro",NULL},
            {"Lokal","Lo",NULL},
            {"Kabel","Ka",NULL},
            {"Nicke","Ni",NULL},
            {"Jacke","Ja",NULL},
            {"Tasche","Ta",NULL},
            {"Küche","Kü",NULL},
            {"Backe","Ba",NULL},
            {"Packen","Pa",NULL},
            {"Suche","Su",NULL},
            {"Buche","Bu",NULL},
            {"Blüte","Blü",NULL},
            {"Flöte","Flö",NULL},
            {"Käse","Kä",NULL},
            {"Hobel","Ho",NULL},
            {"Sonne","Son",NULL},
            {"Tonne","Ton",NULL},
            {"Tanne","Tan",NULL},
            {"Tasse","Tas",NULL},
            {"Kette","Ket",NULL},
            {"Betten","Bet",NULL},
            {"Bettel","Bet",NULL},
            {"Mitte","Mit",NULL},
            {"Lippe","Lip",NULL},
            {"Mappe","Map",NULL},
            {"Rippe","Rip",NULL},
            {"Ratte","Rat",NULL},
            {"Retten","Ret",NULL},
            {"Rutsche","Rut",NULL},
            {"Katze","Kat",NULL},
            {"Pflanze","Pflan",NULL},
            {"Pfütze","Pfüt",NULL},
            {"Schätze","Schät",NULL},
            {"Putzen","Put",NULL},
            {"Schwester","Schwes",NULL},
            {"Fenster","Fens",NULL},
            {"Kasten","Kas",NULL},
            {"Muster","Mus",NULL},
            {"Liste","Lis",NULL},
            {"Westen","Wes",NULL},
            {"Kosten","Kos",NULL},
            {"Wespe","Wes",NULL},
            {"Kasko","Kas",NULL},
            {"Hunde","Hun",NULL},
            {"Kunde","Kun",NULL},
            {"Bunte","Bun",NULL},
            {"Kante","Kan",NULL},
            {"Welten","Wel",NULL},
            {"Helden","Hel",NULL},
            {"Wolken","Wol",NULL},
            {"Kinder","Kin",NULL},
            {"Bilder","Bil",NULL},
            {"Finden","Fin",NULL},
            {"Binden","Bin",NULL},
            {"Rinde","Rin",NULL},
            {"Wilde","Wil",NULL},
            {"Halle","Hal",NULL},
            {"Falle","Fal",NULL},
            {"Kolle","Kol",NULL},
            {"Rolle","Rol",NULL},
            {"Volle","Vol",NULL},
            /* Unabhängig zusammengestellte Teilorakel zu §87–88:
             * https://grammis.ids-mannheim.de/rechtschreibung/6216
             * Nur die angegebene Stelle, keine vollständige Zerlegung. */
            {"Mutter","Mut",NULL},
            {"Butter","But",NULL},
            {"Hammer","Ham",NULL},
            {"Sommer","Som",NULL},
            {"Zimmer","Zim",NULL},
            {"Wasser","Was",NULL},
            {"Messer","Mes",NULL},
            {"Kissen","Kis",NULL},
            {"Koffer","Kof",NULL},
            {"Nüsse","Nüs",NULL},
            {"Nacken","Na",NULL},
            {"Socken","So",NULL},
            {"Decke","De",NULL},
            {"Wecker","We",NULL},
            {"Brücke","Brü",NULL},
            {"Strecke","Stre",NULL},
            {"Kirche","Kir",NULL},
            {"Wolke","Wol",NULL},
            {"Futter","Fut",NULL},
            {"Teller","Tel",NULL},
            {"Tinte","Tin",NULL},
            {"Lampe","Lam",NULL},
            {"Hummel","Hum",NULL},
            {"Gummi","Gum",NULL},
            {"Nummer","Num",NULL},
            {"Summe","Sum",NULL},
            {"kommen","kom",NULL},
            {"fallen","fal",NULL},
            {"rollen","rol",NULL},
            {"bellen","bel",NULL},
            {"stellen","stel",NULL},
            {"sammeln","sam",NULL},
            {"brennen","bren",NULL},
            {"hoffen","hof",NULL},
            {"messen","mes",NULL},
            {"wissen","wis",NULL},
            {"lassen","las",NULL},
            {"Kanne","Kan",NULL},
            {"Wanne","Wan",NULL},
            {"Panne","Pan",NULL},
            {"Pfanne","Pfan",NULL},
            {"Wolle","Wol",NULL},
            {"Rinne","Rin",NULL},
            {"Wippe","Wip",NULL},
            {"Löffel","Löf",NULL},
            {"Würfel","Wür",NULL},
            {"Watte","Wat",NULL},
            {"Hütte","Hüt",NULL},
            {"Latte","Lat",NULL},
            {"Puppe","Pup",NULL},
            {"Suppe","Sup",NULL},
            {"Treppe","Trep",NULL},
            {"Gruppe","Grup",NULL},
            {"Kappe","Kap",NULL},
            {"Klappe","Klap",NULL},
            {"Ente","En",NULL},
            {"Landen","Lan",NULL},
            {"Hände","Hän",NULL},
            {"Wände","Wän",NULL},
            {"Länder","Län",NULL},
            {"Bänder","Bän",NULL},
            {"Felder","Fel",NULL},
            {"Wälder","Wäl",NULL},
            {"Wächter","Wäch",NULL},
            {"Lichter","Lich",NULL},
            {"Löcher","Lö",NULL},
            {"Bücher","Bü",NULL},
            {"Fächer","Fä",NULL},
            {"Becher","Be",NULL},
            {"Dächer","Dä",NULL},
            {"Flasche","Fla",NULL},
            {"Flächen","Flä",NULL},
            {"Taschen","Ta",NULL},
            {"Sachen","Sa",NULL},
            {"Drachen","Dra",NULL},
            {"machen","ma",NULL},
            {"kochen","ko",NULL},
            {"backen","ba",NULL},
            {"nicken","ni",NULL},
            {"pflücken","pflü",NULL},
            {"bücken","bü",NULL},
            {"drücken","drü",NULL},
            {"Brötchen","Bröt",NULL},
            {"Mädchen","Mäd",NULL},
        };
        int hit = 0, missed = 0;
        for (size_t j = 0; j < sizeof(refs)/sizeof(refs[0]); ++j) {
            MooValue w = str(refs[j].word), spans = moo_silben_grenzen(a, w);
            CHECK(!moo_error_flag && spans.tag == MOO_LIST, "Grenzen-API");
            int found = 0, offset = 0;
            MooList *ls = MV_LIST(spans);
            for (int32_t k = 0; k < ls->length; ++k) {
                MooValue pair = ls->items[k];
                CHECK(pair.tag == MOO_LIST && MV_LIST(pair)->length == 3, "Grenzenpaar");
                MooValue l = MV_LIST(pair)->items[0], r = MV_LIST(pair)->items[1], art = MV_LIST(pair)->items[2];
                CHECK(art.tag == MOO_NUMBER && MV_NUM(art) == 1.0, "Wortsegment klassifiziert");
                CHECK(l.tag == MOO_NUMBER && r.tag == MOO_NUMBER, "Grenzenwerte");
                CHECK(MV_NUM(l) == offset && MV_NUM(r) > offset &&
                      MV_NUM(r) <= MV_STR(w)->length, "lückenlose Byteintervalle");
                offset = (int)MV_NUM(r);
                if (offset == (int)strlen(refs[j].left) ||
                    (refs[j].alternative && offset == (int)strlen(refs[j].alternative))) found = 1;
            }
            CHECK(offset == MV_STR(w)->length, "vollständige Abdeckung");
            if (found) ++hit;
            else { ++missed; fprintf(stderr, "REFERENZ_ABWEICHUNG %s\n", refs[j].word); }
            moo_release(spans); moo_release(w);
        }
        fprintf(stderr, "REFERENZ_TEILORAKEL gesamt=%d treffer=%d abweichungen=%d (2/2)\n",
                hit + missed, hit, missed);
    }

    /* Technische Klassifikation unsicherer Unicode-Läufe:
     * a [0,1) Wort, Leerzeichen Literal, e+Akzent und Emoji byteweise
     * Fallback. Fallback-Spannen sind keine sprachlichen Grenzen. */
    {
        MooValue w = str("a é 😀");
        MooValue spans = moo_silben_grenzen(a, w);
        CHECK(!moo_error_flag && spans.tag == MOO_LIST, "Unicode-Grenzen");
        const int want_start[] = {0,1,2,3,4,5,6,7,8,9};
        const int want_kind[]  = {1,0,2,2,2,0,2,2,2,2};
        MooList *ls = MV_LIST(spans);
        CHECK(ls->length == 10, "Unicode-Fallback spannt jedes Byte");
        for (int i=0; i<10; ++i) {
            MooValue p = ls->items[i];
            CHECK(p.tag == MOO_LIST && MV_LIST(p)->length == 3, "Unicode-Trippel");
            MooList *triple = MV_LIST(p);
            CHECK(MV_NUM(triple->items[0]) == want_start[i] &&
                  MV_NUM(triple->items[1]) == want_start[i] + 1 &&
                  MV_NUM(triple->items[2]) == want_kind[i], "Unicode-Fallbackklassifikation");
        }
        moo_release(spans); moo_release(w);
    }
    /* Eigenes künstliches Regelwerk prüft die Wirkung einer Regeländerung. */
    {
        MooValue p1 = str("\\patterns{ab1cd}"), p2 = str("\\patterns{abcd}");
        MooValue input2 = str("abcd abcd");
        MooValue t1 = moo_silben_trainiere(input2, moo_number(512), p1);
        MooValue t2 = moo_silben_trainiere(input2, moo_number(512), p2);
        CHECK(!moo_error_flag && t1.tag == MOO_STRING && t2.tag == MOO_STRING, "synthetische Regeln");
        MooValue h1 = moo_silben_hash(t1), h2s = moo_silben_hash(t2);
        CHECK(!moo_error_flag && !same(h1, h2s), "Regelinhalt in Artefaktidentität");
        MooValue w = str("abcd"), spans1 = moo_silben_grenzen(t1, w), spans2 = moo_silben_grenzen(t2, w);
        CHECK(!moo_error_flag && spans1.tag == MOO_LIST && spans2.tag == MOO_LIST, "synthetische Grenzen");
        CHECK(MV_LIST(spans1)->length == 2 && MV_LIST(spans2)->length == 1, "synthetische Regel ändert Grenze");
        MooList *first = MV_LIST(MV_LIST(spans1)->items[0]);
        CHECK(first->length == 3 && MV_NUM(first->items[1]) == 2 &&
              MV_NUM(first->items[2]) == 1, "ab|cd Kandidat");
        moo_release(spans1); moo_release(spans2); moo_release(w);
        moo_release(h1); moo_release(h2s); moo_release(t1); moo_release(t2);
        moo_release(input2); moo_release(p1); moo_release(p2);
    }
    {
        MooValue changed = moo_string_new_len(MV_STR(a)->chars, MV_STR(a)->length);
        MV_STR(changed)->chars[MV_STR(changed)->length - 1] ^= 1;
        MooValue bad_result = moo_silben_info(changed);
        CHECK(moo_error_flag && bad_result.tag == MOO_NONE, "Artefakt-Checksumme schützt vor Mutation");
        moo_error_flag = 0; moo_release(bad_result); moo_release(changed);
    }

    /* Ein Angreifer kann die Prüfsumme neu berechnen. Das Artefakt muss
     * unabhängig davon die eingebetteten Regeln validieren. */
    {
        MooValue changed = moo_string_new_len(MV_STR(a)->chars, MV_STR(a)->length);
        MooString *s = MV_STR(changed);
        CHECK(s->length > 56, "Artefakt ausreichend lang");
        char *marker = strstr(s->chars + 24, "\\patterns{");
        CHECK(marker != NULL, "Mustermarker vorhanden");
        marker[0] = '!'; /* Markierung des Musterblocks beschädigen. */
        moo_krypto_sha256((const uint8_t *)s->chars, (size_t)s->length - 32,
                          (uint8_t *)s->chars + s->length - 32);
        MooValue bad_result = moo_silben_info(changed);
        CHECK(moo_error_flag && bad_result.tag == MOO_NONE, "Muster trotz gültigem SHA abgelehnt");
        moo_error_flag = 0; moo_release(bad_result);
        bad_result = moo_silben_hash(changed);
        CHECK(moo_error_flag && bad_result.tag == MOO_NONE, "Hash validiert Regeln");
        moo_error_flag = 0; moo_release(bad_result);
        MooValue w = str("Wort"); bad_result = moo_silben_kodiere(changed, w);
        CHECK(moo_error_flag && bad_result.tag == MOO_NONE, "Encode validiert Regeln");
        moo_error_flag = 0; moo_release(bad_result); moo_release(w); moo_release(changed);
    }

    {
        MooValue altered = moo_string_new_len(MV_STR(a)->chars, MV_STR(a)->length);
        MooString *s = MV_STR(altered);
        CHECK(s->length > 56, "Musterkopf vorhanden");
        s->chars[24] = 0;
        moo_krypto_sha256((const uint8_t *)s->chars, (size_t)s->length - 32,
                          (uint8_t *)s->chars + s->length - 32);
        MooValue r = moo_silben_info(altered);
        CHECK(moo_error_flag && r.tag == MOO_NONE, "NUL im Musterkommentar trotz gültiger Prüfsumme abgelehnt");
        moo_error_flag = 0; moo_release(r); moo_release(altered);
    }

#ifndef _WIN32
    /* Existing destination survives a failed same-directory temporary write. */
    if (geteuid() != 0) {
        char dir[] = "/tmp/moo-p4-atomic-XXXXXX", dst[128];
        CHECK(mkdtemp(dir) != NULL, "Testverzeichnis");
        CHECK(snprintf(dst, sizeof(dst), "%s/old.bin", dir) > 0, "Testpfad");
        MooValue dest = str(dst);
        MooValue saved = moo_silben_speichern(a, dest);
        CHECK(!moo_error_flag && saved.tag == MOO_BOOL, "Startdatei");
        moo_release(saved);
        CHECK(chmod(dir, 0500) == 0, "Schreibzugriff entziehen");
        saved = moo_silben_speichern(b, dest);
        int denied = moo_error_flag && saved.tag == MOO_NONE;
        moo_error_flag = 0; moo_release(saved);
        CHECK(chmod(dir, 0700) == 0, "Schreibzugriff wiederherstellen");
        CHECK(denied, "Schreibfehler signalisiert");
        MooValue existing = moo_silben_laden(dest);
        CHECK(!moo_error_flag && same(a, existing), "alte Datei bei Schreibfehler erhalten");
        moo_release(existing); moo_release(dest);
        CHECK(unlink(dst) == 0 && rmdir(dir) == 0, "Testdatei entfernt");
    } else {
        fprintf(stderr, "SKIP atomic-write-fault: root kann Verzeichnissperre umgehen\n");
    }
#else
    fprintf(stderr, "SKIP atomic-write-fault: POSIX-Berechtigungsprobe auf Windows nicht anwendbar\n");
#endif

    /* BF16-Storage muss den veralteten f32-Puffer ersetzen. 0x4282 ist
     * BF16 für 65 ('A'); der Hostpuffer enthält absichtlich 66 ('B'). */
    {
        MooValue v = ids1(66.0f);
        MooTensor *t = MV_TENSOR(v);
        t->store = malloc(sizeof(uint16_t));
        CHECK(t->store != NULL, "BF16 Testallokation");
        ((uint16_t *)t->store)[0] = 0x4282u;
        t->dtype = MOO_DT_BF16;
        t->valid = MOO_V_STORE;
        MooValue decoded = moo_silben_dekodiere(a, v), expected = str("A");
        CHECK(!moo_error_flag && same(decoded, expected), "BF16 statt veraltetem Hostbuffer");
        moo_release(decoded); moo_release(expected); moo_release(v);
    }
    {
        MooValue v = ids1(65.0f);
        MV_TENSOR(v)->valid = 0;
        MooValue decoded = moo_silben_dekodiere(a, v);
        CHECK(moo_error_flag && decoded.tag == MOO_NONE, "fehlgeschlagene Host-Sicherung");
        moo_error_flag = 0; moo_release(decoded); moo_release(v);
    }

    {
        MooValue info = moo_silben_info(a);
        CHECK(!moo_error_flag && info.tag == MOO_DICT, "Info-Dict");
        MooValue ih = moo_dict_get(info, str("hash"));
        MooValue ih64 = moo_dict_get(info, str("hash64"));
        MooValue h64 = moo_silben_hash64(a);
        CHECK(!moo_error_flag && same(ih, h) && same(ih64, h64), "Info-Hashes stimmen mit direkten Hashes");
        moo_release(ih); moo_release(ih64); moo_release(h64); moo_release(info);
    }

    char pathbuf[128];
    #ifdef _WIN32
    const char *tmpdir = getenv("TEMP");
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
#endif
    CHECK(tmpdir && *tmpdir, "temporäres Verzeichnis");
    int path_len = snprintf(pathbuf, sizeof(pathbuf), "%s/moo-p4-silben-%ld.bin",
                   tmpdir, (long)getpid());
    CHECK(path_len > 0 && (size_t)path_len < sizeof(pathbuf), "temporärer Pfad");
    MooValue path = str(pathbuf);
    out = moo_silben_speichern(a, path);
    CHECK(!moo_error_flag, "Speichern erfolgreich"); moo_release(out);
    MooValue loaded = moo_silben_laden(path);
    CHECK(!moo_error_flag && same(a, loaded), "Save/Load Artefakt");
    after = moo_silben_kodiere(loaded, input);
    CHECK(!moo_error_flag && same_ids(before, after), "Save/Load stabile IDs");
    MooValue loaded_hash = moo_silben_hash(loaded);
    CHECK(!moo_error_flag && same(h, loaded_hash), "Save/Load Hash");
    unlink(pathbuf);
#ifdef _WIN32
    {
        char utf8_path[128];
        wchar_t wide_path[128];
        snprintf(utf8_path, sizeof(utf8_path), "moo-p4-%ld-\xC3\xA4.bin", (long)getpid());
        swprintf(wide_path, 128, L"moo-p4-%ld-\u00e4.bin", (long)getpid());
        MooValue unicode_path = str(utf8_path);
        MooValue first = moo_silben_speichern(a, unicode_path);
        CHECK(!moo_error_flag && first.tag == MOO_BOOL, "Unicode-Pfad erstes Speichern");
        moo_release(first);
        MooValue different_corpus = str("cccc dddd cccc dddd");
        MooValue different = moo_silben_trainiere(different_corpus, limit, pattern);
        CHECK(!moo_error_flag && !same(a, different), "Ersetzungsartefakt unterscheidet sich");
        MooValue second = moo_silben_speichern(different, unicode_path);
        CHECK(!moo_error_flag && second.tag == MOO_BOOL, "Unicode-Pfad bestehende Datei ersetzen");
        moo_release(second);
        MooValue replaced = moo_silben_laden(unicode_path);
        CHECK(!moo_error_flag && same(different, replaced), "Unicode-Pfad Ersetzung korrekt geladen");
        moo_release(replaced);
        moo_release(different);
        moo_release(different_corpus);
        moo_release(unicode_path);
        CHECK(_wremove(wide_path) == 0, "Unicode-Testdatei entfernen");
    }
#endif
    moo_release(loaded_hash); moo_release(after); moo_release(loaded); moo_release(path);
    moo_release(before); moo_release(input); moo_release(h); moo_release(h2);
    moo_release(a); moo_release(b); moo_release(corpus); moo_release(pattern);
    fprintf(stderr, "silben: %d checks bestanden\n", checks);
    return 0;
}
