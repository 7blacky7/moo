/*
 * regerror.c — Kompakte Standalone-Implementierung fuer das win_regex-
 * Vendoring (P013). musl's Original haengt an musl-internen Locale-Mechanismen
 * (locale_impl.h / LCTRANS) und ist nicht standalone-faehig; die Meldungs-
 * texte hier entsprechen musl v1.2.5.
 */
#include <stdio.h>
#include <string.h>
#include <regex.h>

static const char* const moo_re_messages[] = {
    [REG_OK]       = "No error",
    [REG_NOMATCH]  = "No match",
    [REG_BADPAT]   = "Invalid regexp",
    [REG_ECOLLATE] = "Unknown collating element",
    [REG_ECTYPE]   = "Unknown character class name",
    [REG_EESCAPE]  = "Trailing backslash",
    [REG_ESUBREG]  = "Invalid back reference",
    [REG_EBRACK]   = "Missing ']'",
    [REG_EPAREN]   = "Missing ')'",
    [REG_EBRACE]   = "Missing '}'",
    [REG_BADBR]    = "Invalid contents of {}",
    [REG_ERANGE]   = "Invalid character range",
    [REG_ESPACE]   = "Out of memory",
    [REG_BADRPT]   = "Repetition not preceded by valid expression",
};

size_t regerror(int e, const regex_t* restrict preg, char* restrict buf, size_t size) {
    (void)preg;
    const char* msg = "Unknown error";
    if (e >= 0 && (size_t)e < sizeof(moo_re_messages) / sizeof(moo_re_messages[0])
        && moo_re_messages[e]) {
        msg = moo_re_messages[e];
    }
    return 1 + (size_t)snprintf(buf, size, "%s", msg);
}
