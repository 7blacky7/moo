# ============================================================
# moo Neural Network from Scratch — XOR-Lernproblem
#
# Kompilieren: moo-compiler compile neuralnet.moo -o neuralnet
# Starten:     ./neuralnet
#
# Netz-Architektur:
#   Input 2 → Hidden 4 (sigmoid) → Output 1 (sigmoid)
# Trainingsdaten:
#   XOR: [0,0]→0, [0,1]→1, [1,0]→1, [1,1]→0
# Training:
#   Mini-Batch Gradient Descent (alle 4 Samples pro Step)
#   Loss: MSE
#   Learning rate: 0.5, Epochen: 10 000
# ============================================================

konstante LR auf 0.5
konstante EPOCHS auf 10000
konstante E auf 2.7182818284590452

# --- Aktivierung ---
funktion sigmoid(x):
    gib_zurück 1 / (1 + E ** (0 - x))

# --- Matrix als {r, c, d=flache Liste, row-major} ---
funktion mat_zero(r, c):
    setze m auf {}
    m["r"] = r
    m["c"] = c
    setze d auf []
    setze i auf 0
    solange i < r * c:
        d.hinzufügen(0.0)
        setze i auf i + 1
    m["d"] = d
    gib_zurück m

funktion mat_rand(r, c, scale):
    setze m auf mat_zero(r, c)
    setze d auf m["d"]
    setze i auf 0
    solange i < r * c:
        d[i] = (zufall() - 0.5) * 2 * scale
        setze i auf i + 1
    m["d"] = d
    gib_zurück m

funktion mat_get(m, i, j):
    gib_zurück m["d"][i * m["c"] + j]

funktion mat_set(m, i, j, v):
    setze d auf m["d"]
    d[i * m["c"] + j] = v
    m["d"] = d

# a (r x k) @ b (k x c) = out (r x c)
funktion mat_mul(a, b):
    setze r auf a["r"]
    setze k auf a["c"]
    setze c auf b["c"]
    setze out auf mat_zero(r, c)
    setze ad auf a["d"]
    setze bd auf b["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r:
        setze j auf 0
        solange j < c:
            setze sum auf 0.0
            setze kk auf 0
            solange kk < k:
                setze sum auf sum + ad[i * k + kk] * bd[kk * c + j]
                setze kk auf kk + 1
            od[i * c + j] = sum
            setze j auf j + 1
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# a + b (gleiches shape)
funktion mat_add(a, b):
    setze r auf a["r"]
    setze c auf a["c"]
    setze out auf mat_zero(r, c)
    setze ad auf a["d"]
    setze bd auf b["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r * c:
        od[i] = ad[i] + bd[i]
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# a - b
funktion mat_sub(a, b):
    setze r auf a["r"]
    setze c auf a["c"]
    setze out auf mat_zero(r, c)
    setze ad auf a["d"]
    setze bd auf b["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r * c:
        od[i] = ad[i] - bd[i]
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# Elementwise Produkt (Hadamard)
funktion mat_had(a, b):
    setze r auf a["r"]
    setze c auf a["c"]
    setze out auf mat_zero(r, c)
    setze ad auf a["d"]
    setze bd auf b["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r * c:
        od[i] = ad[i] * bd[i]
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# Scale
funktion mat_scale(m, s):
    setze r auf m["r"]
    setze c auf m["c"]
    setze out auf mat_zero(r, c)
    setze md auf m["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r * c:
        od[i] = md[i] * s
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# Transponieren
funktion mat_t(m):
    setze r auf m["r"]
    setze c auf m["c"]
    setze out auf mat_zero(c, r)
    setze md auf m["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r:
        setze j auf 0
        solange j < c:
            od[j * r + i] = md[i * c + j]
            setze j auf j + 1
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# Bias-Broadcast: Addiere Zeilenvektor b (1 x c) zu jeder Zeile von m (r x c)
funktion mat_add_bias(m, b):
    setze r auf m["r"]
    setze c auf m["c"]
    setze out auf mat_zero(r, c)
    setze md auf m["d"]
    setze bd auf b["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r:
        setze j auf 0
        solange j < c:
            od[i * c + j] = md[i * c + j] + bd[j]
            setze j auf j + 1
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# Sum axis=0 → 1 x c Row-Vektor
funktion mat_sum_rows(m):
    setze r auf m["r"]
    setze c auf m["c"]
    setze out auf mat_zero(1, c)
    setze md auf m["d"]
    setze od auf out["d"]
    setze j auf 0
    solange j < c:
        setze s auf 0.0
        setze i auf 0
        solange i < r:
            setze s auf s + md[i * c + j]
            setze i auf i + 1
        od[j] = s
        setze j auf j + 1
    out["d"] = od
    gib_zurück out

# Elementweise sigmoid
funktion mat_sigmoid(m):
    setze r auf m["r"]
    setze c auf m["c"]
    setze out auf mat_zero(r, c)
    setze md auf m["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r * c:
        od[i] = sigmoid(md[i])
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# sigmoid_deriv(y) = y * (1 - y) fuer y = sigmoid(z)
funktion mat_sigmoid_deriv(a):
    setze r auf a["r"]
    setze c auf a["c"]
    setze out auf mat_zero(r, c)
    setze ad auf a["d"]
    setze od auf out["d"]
    setze i auf 0
    solange i < r * c:
        setze y auf ad[i]
        od[i] = y * (1 - y)
        setze i auf i + 1
    out["d"] = od
    gib_zurück out

# MSE-Loss ueber (A - Y): 0.5 * mean(sum((A-Y)^2))
funktion mse(a, y):
    setze diff auf mat_sub(a, y)
    setze r auf diff["r"]
    setze c auf diff["c"]
    setze dd auf diff["d"]
    setze s auf 0.0
    setze i auf 0
    solange i < r * c:
        setze s auf s + dd[i] * dd[i]
        setze i auf i + 1
    gib_zurück 0.5 * s / r

# --- Trainingsdaten: XOR ---
setze X auf mat_zero(4, 2)
mat_set(X, 0, 0, 0.0)
mat_set(X, 0, 1, 0.0)
mat_set(X, 1, 0, 0.0)
mat_set(X, 1, 1, 1.0)
mat_set(X, 2, 0, 1.0)
mat_set(X, 2, 1, 0.0)
mat_set(X, 3, 0, 1.0)
mat_set(X, 3, 1, 1.0)

setze Y auf mat_zero(4, 1)
mat_set(Y, 0, 0, 0.0)
mat_set(Y, 1, 0, 1.0)
mat_set(Y, 2, 0, 1.0)
mat_set(Y, 3, 0, 0.0)

# --- Parameter-Init ---
setze W1 auf mat_rand(2, 4, 1.0)
setze b1 auf mat_zero(1, 4)
setze W2 auf mat_rand(4, 1, 1.0)
setze b2 auf mat_zero(1, 1)

zeige "=== moo Neural Network — XOR ==="
zeige "Architektur: 2 → 4 → 1 (sigmoid)"
zeige "LR=" + text(LR) + " Epochs=" + text(EPOCHS)
zeige ""

# --- Training ---
setze t0 auf zeit_ms()
setze epoch auf 0
solange epoch < EPOCHS:
    # Forward
    setze Z1 auf mat_add_bias(mat_mul(X, W1), b1)
    setze A1 auf mat_sigmoid(Z1)
    setze Z2 auf mat_add_bias(mat_mul(A1, W2), b2)
    setze A2 auf mat_sigmoid(Z2)

    # Loss-Print
    wenn epoch % 1000 == 0:
        setze loss auf mse(A2, Y)
        zeige "Epoch " + text(epoch) + "  loss=" + text(loss)

    # Backward
    # dZ2 = (A2 - Y) * A2*(1-A2)
    setze err auf mat_sub(A2, Y)
    setze dZ2 auf mat_had(err, mat_sigmoid_deriv(A2))
    setze dW2 auf mat_mul(mat_t(A1), dZ2)
    setze db2 auf mat_sum_rows(dZ2)

    setze dA1 auf mat_mul(dZ2, mat_t(W2))
    setze dZ1 auf mat_had(dA1, mat_sigmoid_deriv(A1))
    setze dW1 auf mat_mul(mat_t(X), dZ1)
    setze db1 auf mat_sum_rows(dZ1)

    # Update (gradient / N fuer stabilere Skalen)
    setze n_inv auf 1 / 4
    setze W1 auf mat_sub(W1, mat_scale(dW1, LR * n_inv))
    setze b1 auf mat_sub(b1, mat_scale(db1, LR * n_inv))
    setze W2 auf mat_sub(W2, mat_scale(dW2, LR * n_inv))
    setze b2 auf mat_sub(b2, mat_scale(db2, LR * n_inv))

    setze epoch auf epoch + 1
setze t1 auf zeit_ms()

zeige ""
zeige "Training fertig in " + text(boden(t1 - t0)) + " ms"
zeige ""

# --- Final Prediction ---
setze Z1 auf mat_add_bias(mat_mul(X, W1), b1)
setze A1 auf mat_sigmoid(Z1)
setze Z2 auf mat_add_bias(mat_mul(A1, W2), b2)
setze A2 auf mat_sigmoid(Z2)

zeige "--- XOR Vorhersagen ---"
setze i auf 0
solange i < 4:
    setze x0 auf boden(mat_get(X, i, 0))
    setze x1 auf boden(mat_get(X, i, 1))
    setze yhat auf mat_get(A2, i, 0)
    setze erwartet auf boden(mat_get(Y, i, 0))
    zeige "  (" + text(x0) + " XOR " + text(x1) + ") = " + text(yhat) + "  (erwartet " + text(erwartet) + ")"
    setze i auf i + 1

zeige ""
zeige "Finale Loss: " + text(mse(A2, Y))
