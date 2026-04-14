# Test-Client fuer Mini-Redis

setze client auf tcp_verbinde("127.0.0.1", 6379)
zeige "Verbunden mit Redis"

client.schreiben("PING\r\n")
setze antwort auf client.lesen(1024)
zeige "PING Antwort:"
zeige antwort

client.schliessen()
