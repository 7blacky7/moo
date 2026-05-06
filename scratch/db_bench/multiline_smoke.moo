funktion summe(a, b, c, d):
    gib_zurück a + b + c + d

# Multi-line call
setze s auf summe(
    1,
    2,
    3,
    4
)
zeige "summe = " + text(s)

# Multi-line list literal
setze l auf [
    10,
    20,
    30,
]
zeige "list = " + text(l)

# Multi-line dict literal
setze d auf {
    "a": 1,
    "b": 2,
}
zeige "dict[a] = " + text(d["a"])

# Multi-line function definition
funktion lang(
    erstes,
    zweites,
    drittes
):
    gib_zurück erstes * 100 + zweites * 10 + drittes

zeige "lang = " + text(lang(1, 2, 3))
