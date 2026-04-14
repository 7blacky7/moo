# moo Mini-Blog-Engine
# Stresstest: HTTP-Server + SQLite + Sessions + Templates + Passwort-Hashing
#
# Kompilieren:  compiler/target/release/moo-compiler compile beispiele/blog_engine.moo -o /tmp/blog
# Starten:      /tmp/blog
# Testen:       curl http://localhost:3000/
#
# HINWEIS: Aktuell kennt die moo-Runtime keine HTTP-Header (Cookies!) beim
# Request und kann auch keine Set-Cookie-Header senden. Sessions werden daher
# uebergangsweise ueber ein "?token=..." Query-Parameter gefuehrt. Das ist als
# Bug im Channel "stresstest" dokumentiert.

zeige "=== moo Mini-Blog-Engine ==="

# ---------------------------------------------------------------
# Datenbank aufsetzen
# ---------------------------------------------------------------
setze db auf db_verbinde("sqlite:///tmp/blog.db")

db_ausführen(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password_hash TEXT, email TEXT)")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS posts (id INTEGER PRIMARY KEY, title TEXT, content TEXT, author TEXT, created_at TEXT)")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS sessions (id INTEGER PRIMARY KEY, user_id INTEGER, token TEXT, expires TEXT)")

# Demo-User anlegen (nur wenn nicht vorhanden)
setze vorhanden auf db_abfrage(db, "SELECT id FROM users WHERE username = 'admin'")
wenn länge(vorhanden) == 0:
    setze admin_hash auf sha256("admin123")
    db_ausführen(db, "INSERT INTO users (username, password_hash, email) VALUES ('admin', '" + admin_hash + "', 'admin@example.com')")
    zeige "Admin-User angelegt (admin/admin123)"

# ---------------------------------------------------------------
# Hilfsfunktionen
# ---------------------------------------------------------------

funktion escape_html(text):
    setze t auf text
    setze t auf t.ersetzen("&", "&amp;")
    setze t auf t.ersetzen("<", "&lt;")
    setze t auf t.ersetzen(">", "&gt;")
    setze t auf t.ersetzen("\"", "&quot;")
    gib_zurück t

funktion sql_escape(text):
    gib_zurück text.ersetzen("'", "''")

# URL-decode (sehr reduziert: + -> space, %20 -> space)
funktion url_decode(text):
    setze t auf text
    setze t auf t.ersetzen("+", " ")
    setze t auf t.ersetzen("%20", " ")
    setze t auf t.ersetzen("%40", "@")
    setze t auf t.ersetzen("%21", "!")
    gib_zurück t

# Form-Body parsen: "a=1&b=2" -> dict
funktion form_parse(body):
    setze out auf {}
    wenn body == "":
        gib_zurück out
    setze teile auf body.teilen("&")
    für paar in teile:
        setze kv auf paar.teilen("=")
        wenn länge(kv) == 2:
            setze schluessel auf url_decode(kv[0])
            setze wert auf url_decode(kv[1])
            out[schluessel] = wert
    gib_zurück out

# Query-String parsen (gleiche Logik)
funktion query_parse(q):
    gib_zurück form_parse(q)

funktion make_token():
    setze t auf zeit()
    gib_zurück sha256("sess_" + t + "_moo")

# Session aus Query-Parameter "token" nachschlagen
funktion aktuellen_user(db, req):
    setze q auf req["query"]
    wenn q == nichts:
        gib_zurück nichts
    setze params auf query_parse(q)
    wenn nicht params.hat("token"):
        gib_zurück nichts
    setze token auf params["token"]
    setze rows auf db_abfrage(db, "SELECT user_id FROM sessions WHERE token = '" + sql_escape(token) + "'")
    wenn länge(rows) == 0:
        gib_zurück nichts
    setze uid auf rows[0]["user_id"]
    setze users auf db_abfrage(db, "SELECT username FROM users WHERE id = " + uid)
    wenn länge(users) == 0:
        gib_zurück nichts
    gib_zurück users[0]["username"]

# ---------------------------------------------------------------
# Templates
# ---------------------------------------------------------------

funktion layout_head():
    gib_zurück "<!doctype html><html><head><meta charset='utf-8'><title>moo Blog</title><style>body{font-family:system-ui;max-width:720px;margin:2em auto;padding:0 1em;color:#222}a{color:#06c}nav{border-bottom:1px solid #ddd;padding-bottom:.5em;margin-bottom:1em}.post{border:1px solid #eee;padding:1em;margin:1em 0;border-radius:8px}form input,form textarea{display:block;width:100%;margin:.3em 0;padding:.5em}</style></head><body>"

funktion layout_foot():
    gib_zurück "</body></html>"

funktion navigation(user, token):
    wenn user == nichts:
        gib_zurück "<nav><a href='/'>Start</a> | <a href='/login'>Login</a></nav>"
    setze t auf token
    gib_zurück "<nav><a href='/?token=" + t + "'>Start</a> | <a href='/new?token=" + t + "'>Neuer Post</a> | <form style='display:inline' method='POST' action='/logout'><input type='hidden' name='token' value='" + t + "'/><button>Logout (" + user + ")</button></form></nav>"

# ---------------------------------------------------------------
# Routen
# ---------------------------------------------------------------

funktion route_index(db, req):
    setze user auf aktuellen_user(db, req)
    setze token auf ""
    wenn req["query"] != nichts:
        setze p auf query_parse(req["query"])
        wenn p.hat("token"):
            setze token auf p["token"]
    setze posts auf db_abfrage(db, "SELECT id, title, author, created_at FROM posts ORDER BY id DESC")
    setze html auf layout_head() + navigation(user, token) + "<h1>moo Blog</h1>"
    wenn länge(posts) == 0:
        setze html auf html + "<p>Noch keine Posts.</p>"
    für p in posts:
        setze html auf html + "<div class='post'><h3><a href='/post/" + p["id"] + "?token=" + token + "'>" + escape_html(p["title"]) + "</a></h3><small>von " + escape_html(p["author"]) + " am " + p["created_at"] + "</small></div>"
    setze html auf html + layout_foot()
    web_antworten(req, html, 200)

funktion route_post_show(db, req, id):
    setze user auf aktuellen_user(db, req)
    setze token auf ""
    wenn req["query"] != nichts:
        setze p auf query_parse(req["query"])
        wenn p.hat("token"):
            setze token auf p["token"]
    setze rows auf db_abfrage(db, "SELECT title, content, author, created_at FROM posts WHERE id = " + id)
    wenn länge(rows) == 0:
        web_antworten(req, "Post nicht gefunden", 404)
        gib_zurück nichts
    setze post auf rows[0]
    setze html auf layout_head() + navigation(user, token) + "<article><h1>" + escape_html(post["title"]) + "</h1><small>von " + escape_html(post["author"]) + " am " + post["created_at"] + "</small><p>" + escape_html(post["content"]) + "</p></article>" + layout_foot()
    web_antworten(req, html, 200)

funktion route_login_get(req):
    setze html auf layout_head() + "<h1>Login</h1><form method='POST' action='/login'><input name='username' placeholder='Username'/><input name='password' type='password' placeholder='Passwort'/><button>Anmelden</button></form>" + layout_foot()
    web_antworten(req, html, 200)

funktion route_login_post(db, req):
    setze form auf form_parse(req["body"])
    wenn nicht form.hat("username") oder nicht form.hat("password"):
        web_antworten(req, "Bad request", 400)
        gib_zurück nichts
    setze u auf form["username"]
    setze p auf form["password"]
    setze hash auf sha256(p)
    setze rows auf db_abfrage(db, "SELECT id FROM users WHERE username = '" + sql_escape(u) + "' AND password_hash = '" + hash + "'")
    wenn länge(rows) == 0:
        web_antworten(req, layout_head() + "<h1>Login fehlgeschlagen</h1><a href='/login'>Erneut</a>" + layout_foot(), 401)
        gib_zurück nichts
    setze uid auf rows[0]["id"]
    setze token auf make_token()
    db_ausführen(db, "INSERT INTO sessions (user_id, token, expires) VALUES (" + uid + ", '" + token + "', '2099-01-01')")
    setze body auf layout_head() + "<h1>Willkommen " + escape_html(u) + "</h1><p>Dein Token: " + token + "</p><p><a href='/?token=" + token + "'>Zur Startseite</a></p>" + layout_foot()
    web_antworten(req, body, 200)

funktion route_logout(db, req):
    setze form auf form_parse(req["body"])
    wenn form.hat("token"):
        db_ausführen(db, "DELETE FROM sessions WHERE token = '" + sql_escape(form["token"]) + "'")
    web_antworten(req, layout_head() + "<h1>Abgemeldet</h1><a href='/'>Start</a>" + layout_foot(), 200)

funktion route_new_get(db, req):
    setze user auf aktuellen_user(db, req)
    wenn user == nichts:
        web_antworten(req, "Login erforderlich", 401)
        gib_zurück nichts
    setze token auf query_parse(req["query"])["token"]
    setze html auf layout_head() + navigation(user, token) + "<h1>Neuer Post</h1><form method='POST' action='/new?token=" + token + "'><input name='title' placeholder='Titel'/><textarea name='content' rows='6' placeholder='Inhalt'></textarea><button>Speichern</button></form>" + layout_foot()
    web_antworten(req, html, 200)

funktion route_new_post(db, req):
    setze user auf aktuellen_user(db, req)
    wenn user == nichts:
        web_antworten(req, "Login erforderlich", 401)
        gib_zurück nichts
    setze form auf form_parse(req["body"])
    wenn nicht form.hat("title") oder nicht form.hat("content"):
        web_antworten(req, "Bad request", 400)
        gib_zurück nichts
    setze titel auf sql_escape(form["title"])
    setze inhalt auf sql_escape(form["content"])
    setze autor auf sql_escape(user)
    db_ausführen(db, "INSERT INTO posts (title, content, author, created_at) VALUES ('" + titel + "', '" + inhalt + "', '" + autor + "', datetime('now'))")
    setze token auf query_parse(req["query"])["token"]
    web_antworten(req, layout_head() + "<h1>Gespeichert</h1><a href='/?token=" + token + "'>Zurueck</a>" + layout_foot(), 200)

funktion route_api_posts_get(db, req):
    setze rows auf db_abfrage(db, "SELECT id, title, author, created_at FROM posts ORDER BY id DESC")
    web_json(req, {"posts": rows, "anzahl": länge(rows)})

funktion route_api_posts_post(db, req):
    setze user auf aktuellen_user(db, req)
    wenn user == nichts:
        web_json(req, {"fehler": "auth erforderlich"})
        gib_zurück nichts
    setze eingang auf json_parse(req["body"])
    wenn eingang == nichts:
        web_json(req, {"fehler": "ungueltiges JSON"})
        gib_zurück nichts
    setze titel auf sql_escape(eingang["title"])
    setze inhalt auf sql_escape(eingang["content"])
    setze autor auf sql_escape(user)
    db_ausführen(db, "INSERT INTO posts (title, content, author, created_at) VALUES ('" + titel + "', '" + inhalt + "', '" + autor + "', datetime('now'))")
    web_json(req, {"ok": wahr})

# ---------------------------------------------------------------
# Server-Loop
# ---------------------------------------------------------------

setze server auf web_server(3000)
zeige "Blog laeuft auf http://localhost:3000"

solange wahr:
    setze req auf server.web_annehmen()
    wenn req == nichts:
        weiter

    versuche:
        setze pfad auf req["pfad"]
        setze methode auf req["methode"]

        wenn pfad == "/" und methode == "GET":
            route_index(db, req)
            weiter
        wenn pfad == "/login" und methode == "GET":
            route_login_get(req)
            weiter
        wenn pfad == "/login" und methode == "POST":
            route_login_post(db, req)
            weiter
        wenn pfad == "/logout" und methode == "POST":
            route_logout(db, req)
            weiter
        wenn pfad == "/new" und methode == "GET":
            route_new_get(db, req)
            weiter
        wenn pfad == "/new" und methode == "POST":
            route_new_post(db, req)
            weiter
        wenn pfad == "/api/posts" und methode == "GET":
            route_api_posts_get(db, req)
            weiter
        wenn pfad == "/api/posts" und methode == "POST":
            route_api_posts_post(db, req)
            weiter

        # /post/:id
        wenn pfad.enthält("/post/") und methode == "GET":
            setze teile auf pfad.teilen("/")
            wenn länge(teile) >= 3:
                route_post_show(db, req, teile[2])
                weiter

        web_antworten(req, "404 Not Found", 404)
    fange fehler:
        zeige f"Fehler in Request: {fehler}"
        web_antworten(req, "500 Internal Error", 500)
