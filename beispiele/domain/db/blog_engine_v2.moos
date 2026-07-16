# moo Mini-Blog-Engine v2 — Prepared Statements + Cookie-Sessions.
#
# Zeigt die additiven Prio2b+Prio3c APIs:
#   - db_abfrage_mit_params / db_ausführen_mit_params statt sql_escape-Konkatenation
#   - req.antworten_mit_headers mit Set-Cookie (statt ?token=-Query-Workaround)
#   - req["headers"]["cookie"] zum Session-Lookup
#
# Das Original beispiele/blog_engine.moo bleibt als Referenz fuer die alte API.
#
# Kompilieren: moo-compiler compile blog_engine_v2.moo -o /tmp/blog_v2
# Starten:     /tmp/blog_v2   (Port 3002)

zeige "=== moo Blog v2 (prepared + cookies) ==="

setze db auf db_verbinde("sqlite:///tmp/blog_v2.db")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password_hash TEXT, email TEXT)")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS posts (id INTEGER PRIMARY KEY, title TEXT, content TEXT, author TEXT, created_at TEXT)")
db_ausführen(db, "CREATE TABLE IF NOT EXISTS sessions (id INTEGER PRIMARY KEY, user_id INTEGER, token TEXT, expires TEXT)")

# Demo-User anlegen
setze vorhanden auf db_abfrage_mit_params(db, "SELECT id FROM users WHERE username = ?", ["admin"])
wenn länge(vorhanden) == 0:
    db_ausführen_mit_params(db, "INSERT INTO users (username, password_hash, email) VALUES (?, ?, ?)", ["admin", sha256("admin123"), "admin@example.com"])
    zeige "Admin-User angelegt (admin/admin123)"

funktion escape_html(t):
    setze x auf t.ersetzen("&", "&amp;")
    setze x auf x.ersetzen("<", "&lt;")
    setze x auf x.ersetzen(">", "&gt;")
    setze x auf x.ersetzen("\"", "&quot;")
    gib_zurück x

funktion url_decode(t):
    setze x auf t.ersetzen("+", " ")
    setze x auf x.ersetzen("%20", " ")
    setze x auf x.ersetzen("%40", "@")
    setze x auf x.ersetzen("%21", "!")
    gib_zurück x

funktion form_parse(body):
    setze out auf {}
    wenn body == "":
        gib_zurück out
    für paar in body.teilen("&"):
        setze kv auf paar.teilen("=")
        wenn länge(kv) == 2:
            out[url_decode(kv[0])] = url_decode(kv[1])
    gib_zurück out

funktion extract_session(cookie_header):
    wenn cookie_header == nichts:
        gib_zurück ""
    für t in cookie_header.teilen(";"):
        setze t2 auf t.trimmen()
        wenn t2.teilstring(0, 8) == "session=":
            gib_zurück t2.teilstring(8, länge(t2))
    gib_zurück ""

funktion aktuellen_user(db, req):
    setze token auf extract_session(req["headers"]["cookie"])
    wenn token == "":
        gib_zurück nichts
    setze rows auf db_abfrage_mit_params(db, "SELECT user_id FROM sessions WHERE token = ?", [token])
    wenn länge(rows) == 0:
        gib_zurück nichts
    setze users auf db_abfrage_mit_params(db, "SELECT username FROM users WHERE id = ?", [rows[0]["user_id"]])
    wenn länge(users) == 0:
        gib_zurück nichts
    gib_zurück users[0]["username"]

funktion layout_head():
    gib_zurück "<!doctype html><html><head><meta charset='utf-8'><title>moo Blog</title><style>body{font-family:system-ui;max-width:720px;margin:2em auto;padding:0 1em;color:#222}a{color:#06c}nav{border-bottom:1px solid #ddd;padding-bottom:.5em;margin-bottom:1em}.post{border:1px solid #eee;padding:1em;margin:1em 0;border-radius:8px}form input,form textarea{display:block;width:100%;margin:.3em 0;padding:.5em}</style></head><body>"

funktion layout_foot():
    gib_zurück "</body></html>"

funktion navigation(user):
    wenn user == nichts:
        gib_zurück "<nav><a href='/'>Start</a> | <a href='/login'>Login</a></nav>"
    gib_zurück "<nav><a href='/'>Start</a> | <a href='/new'>Neuer Post</a> | <form style='display:inline' method='POST' action='/logout'><button>Logout (" + user + ")</button></form></nav>"

funktion route_index(db, req):
    setze user auf aktuellen_user(db, req)
    setze posts auf db_abfrage(db, "SELECT id, title, author, created_at FROM posts ORDER BY id DESC")
    setze html auf layout_head() + navigation(user) + "<h1>moo Blog</h1>"
    wenn länge(posts) == 0:
        setze html auf html + "<p>Noch keine Posts.</p>"
    für p in posts:
        setze html auf html + "<div class='post'><h3><a href='/post/" + text(p["id"]) + "'>" + escape_html(p["title"]) + "</a></h3><small>von " + escape_html(p["author"]) + " am " + p["created_at"] + "</small></div>"
    req.antworten(html + layout_foot(), 200)

funktion route_post_show(db, req, id):
    setze user auf aktuellen_user(db, req)
    setze rows auf db_abfrage_mit_params(db, "SELECT title, content, author, created_at FROM posts WHERE id = ?", [zahl(id)])
    wenn länge(rows) == 0:
        req.antworten("Post nicht gefunden", 404)
        gib_zurück nichts
    setze post auf rows[0]
    setze html auf layout_head() + navigation(user) + "<article><h1>" + escape_html(post["title"]) + "</h1><small>von " + escape_html(post["author"]) + " am " + post["created_at"] + "</small><p>" + escape_html(post["content"]) + "</p></article>" + layout_foot()
    req.antworten(html, 200)

funktion route_login_get(req):
    setze html auf layout_head() + "<h1>Login</h1><form method='POST' action='/login'><input name='username' placeholder='Username'/><input name='password' type='password' placeholder='Passwort'/><button>Anmelden</button></form>" + layout_foot()
    req.antworten(html, 200)

funktion route_login_post(db, req):
    setze form auf form_parse(req["body"])
    wenn nicht form.hat("username") oder nicht form.hat("password"):
        req.antworten("Bad request", 400)
        gib_zurück nichts
    setze rows auf db_abfrage_mit_params(db, "SELECT id FROM users WHERE username = ? AND password_hash = ?", [form["username"], sha256(form["password"])])
    wenn länge(rows) == 0:
        req.antworten(layout_head() + "<h1>Login fehlgeschlagen</h1><a href='/login'>Erneut</a>" + layout_foot(), 401)
        gib_zurück nichts
    setze token auf sha256("sess_" + text(zeit()) + "_moo")
    db_ausführen_mit_params(db, "INSERT INTO sessions (user_id, token, expires) VALUES (?, ?, ?)", [rows[0]["id"], token, "2099-01-01"])
    req.antworten_mit_headers(layout_head() + "<h1>Willkommen " + escape_html(form["username"]) + "</h1><p><a href='/'>Zur Startseite</a></p>" + layout_foot(), 200, {"Set-Cookie": "session=" + token + "; Path=/; HttpOnly", "Content-Type": "text/html; charset=utf-8"})

funktion route_logout(db, req):
    setze token auf extract_session(req["headers"]["cookie"])
    wenn token != "":
        db_ausführen_mit_params(db, "DELETE FROM sessions WHERE token = ?", [token])
    req.antworten_mit_headers(layout_head() + "<h1>Abgemeldet</h1><a href='/'>Start</a>" + layout_foot(), 200, {"Set-Cookie": "session=; Path=/; Max-Age=0", "Content-Type": "text/html; charset=utf-8"})

funktion route_new_get(db, req):
    setze user auf aktuellen_user(db, req)
    wenn user == nichts:
        req.antworten("Login erforderlich", 401)
        gib_zurück nichts
    req.antworten(layout_head() + navigation(user) + "<h1>Neuer Post</h1><form method='POST' action='/new'><input name='title' placeholder='Titel'/><textarea name='content' rows='6' placeholder='Inhalt'></textarea><button>Speichern</button></form>" + layout_foot(), 200)

funktion route_new_post(db, req):
    setze user auf aktuellen_user(db, req)
    wenn user == nichts:
        req.antworten("Login erforderlich", 401)
        gib_zurück nichts
    setze form auf form_parse(req["body"])
    wenn nicht form.hat("title") oder nicht form.hat("content"):
        req.antworten("Bad request", 400)
        gib_zurück nichts
    db_ausführen_mit_params(db, "INSERT INTO posts (title, content, author, created_at) VALUES (?, ?, ?, datetime('now'))", [form["title"], form["content"], user])
    req.antworten(layout_head() + "<h1>Gespeichert</h1><a href='/'>Zurueck</a>" + layout_foot(), 200)

setze server auf web_server(3002)
zeige "Blog v2 laeuft auf http://localhost:3002"

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

        wenn pfad.enthält("/post/") und methode == "GET":
            setze teile auf pfad.teilen("/")
            wenn länge(teile) >= 3:
                route_post_show(db, req, teile[2])
                weiter

        req.antworten("404 Not Found", 404)
    fange fehler:
        zeige f"Fehler: {fehler}"
        req.antworten("500 Internal Error", 500)
