# Unsafe-Block Test
setze x auf 42

# Normaler Code — sicher
zeige x

# Unsafe-Block
unsicher:
    zeige "In unsicherem Block!"
    setze y auf x * 2
    zeige y

# Wieder sicherer Code
zeige "Wieder sicher"
