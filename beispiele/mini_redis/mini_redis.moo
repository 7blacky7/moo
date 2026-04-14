# Mini-Redis — KV-Store Server in moo
# TCP-Server auf Port 6379
# Commands: PING, SET, GET, DEL, KEYS, EXISTS, FLUSHALL
# EHRLICH: Inline ohne Funktionen wegen "globale Variablen in Funktionen"-Bug.

setze server auf tcp_server(6379)
zeige "Mini-Redis laeuft auf Port 6379"

setze store auf {}

solange wahr:
    setze client auf server.annehmen()

    # Ein Kommando pro Verbindung (einfach fuer redis-cli)
    setze buf auf client.lesen(4096)

    wenn buf != nichts:
        wenn länge(buf) > 0:
            setze zeile auf buf.trim()
            setze teile auf zeile.split(" ")
            setze cmd auf teile[0].upper()

            setze antwort auf "-ERR unknown command\r\n"

            wenn cmd == "PING":
                setze antwort auf "+PONG\r\n"

            wenn cmd == "SET":
                wenn länge(teile) >= 3:
                    store[teile[1]] = teile[2]
                    setze antwort auf "+OK\r\n"

            wenn cmd == "GET":
                wenn länge(teile) >= 2:
                    wenn store.has(teile[1]):
                        setze wert auf store[teile[1]]
                        wenn wert == nichts:
                            setze antwort auf "$-1\r\n"
                        sonst:
                            setze antwort auf "$" + text(länge(wert)) + "\r\n" + wert + "\r\n"
                    sonst:
                        setze antwort auf "$-1\r\n"

            wenn cmd == "DEL":
                wenn länge(teile) >= 2:
                    wenn store.has(teile[1]):
                        wenn store[teile[1]] == nichts:
                            setze antwort auf ":0\r\n"
                        sonst:
                            store[teile[1]] = nichts
                            setze antwort auf ":1\r\n"
                    sonst:
                        setze antwort auf ":0\r\n"

            wenn cmd == "EXISTS":
                wenn länge(teile) >= 2:
                    wenn store.has(teile[1]):
                        wenn store[teile[1]] == nichts:
                            setze antwort auf ":0\r\n"
                        sonst:
                            setze antwort auf ":1\r\n"
                    sonst:
                        setze antwort auf ":0\r\n"

            wenn cmd == "KEYS":
                setze k auf store.keys()
                setze antwort auf "*" + text(länge(k)) + "\r\n"
                für key in k:
                    setze antwort auf antwort + "$" + text(länge(key)) + "\r\n" + key + "\r\n"

            wenn cmd == "FLUSHALL":
                setze store auf {}
                setze antwort auf "+OK\r\n"

            client.schreiben(antwort)
