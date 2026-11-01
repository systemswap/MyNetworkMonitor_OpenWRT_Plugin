# Prüfungen und Prüfwerkzeuge – MyNetworkMonitor_OpenWRT_Plugin

Alles, womit MyNetworkMonitor_OpenWRT_Plugin geprüft und gemessen wird. Neue Prüfungen hier ergänzen und
vorhandene Werkzeuge erweitern statt neu schreiben. Erzeugte Daten (Messläufe,
Fuzz-Funde, Vergleichsbilder) kommen nie ins Git, sondern in den lokalen Testordner außerhalb
des Projekts (im Folgenden `<testordner>`).

## Tests

    dotnet test <…> -p:UseArtifactsOutput=true -p:ArtifactsPath=<testordner>/artifacts

Prüfläufe, die nur mit Umgebungsvariable laufen:

| Variable | Was |
|---|---|
| `<PROJEKT>_MESSEN=1` | <Messtests im Release-Modus> |

## Messbefehl

| Aufruf | Wofür |
|---|---|
| `<programm> leistung <ordner>` | ganze Kette über viele Dateien: Zeit, Speicherspitze, GC, Zwischenspeicher |

A/B-Vergleich: `git worktree add <testordner>/vergleich-<name> <commit>`,
eigener `ArtifactsPath`, Läufe im Wechsel.

## Testbestand

| Fall | Beschreibung (ohne Titel) | Warum schwierig |
|---|---|---|
| <…> | <z. B. Archiv mit 100 000 Dateien, 4 GB> | <…> |

## Rundlauf, Referenzen, Fuzzing

- Rundlauf: <schreiben → lesen → vergleichen; Befehl>
- Referenz: <Vergleichsprogramm/Bibliothek; Befehl>
- Fuzzing: <Werkzeug, Zeit-/Speichergrenze je Datei, Ort der Funde>

## Letzter Stand

| Datum | Version | Kennzahlen |
|---|---|---|
| <…> | <…> | <…> |

## Stand dieses Projekts

### Tests auf dem PC

    cd scanner && make BUILD=<testordner>/host MBEDTLS=<testordner>/host/mbedtls-3.6.7 test

Eigene kleine Testsammlung (`scanner/tests/`), gebaut mit AddressSanitizer und UBSan: Ziel- und Portlisten,
JSON-Ausgabe mit ungültigem UTF-8, TCP-Scan gegen lauschende und geschlossene Ports auf 127.0.0.1, alle Sonden und
Rundruf-Verfahren an erfundenen Antworten und gegen nachgespielte Geräte, TLS gegen einen Testserver. `MBEDTLS`
zeigt auf mbedTLS, gebaut aus dem Quellarchiv des SDK (`dl/mbedtls-3.6.7.tar.bz2`) mit `make lib`, vorher
`scripts/config.py set MBEDTLS_THREADING_C` und `MBEDTLS_THREADING_PTHREAD` wie bei OpenWrt; ohne `MBEDTLS` wird
ein installiertes mbedTLS gesucht. Für das SDK muss `package/mbedtls` aus dem OpenWrt-Repository (gleicher Stand
wie das SDK) im SDK liegen und `CONFIG_PACKAGE_libmbedtls=m` gesetzt sein.

### Pakete bauen

SDK von OpenWrt im Testordner, Paketquelle `src-link mynetworkmonitor <testordner>/feed` – `feed` ist ein Verweis auf
`openwrt/` dieses Repositorys ohne Leerzeichen im Pfad (das SDK trennt `feeds.conf` am Leerzeichen):

    ./scripts/feeds update mynetworkmonitor && ./scripts/feeds install -a -p mynetworkmonitor
    make package/mynetworkmonitor-scan/clean package/luci-app-mynetworkmonitor/clean
    make package/mynetworkmonitor-scan/compile package/luci-app-mynetworkmonitor/compile

Immer erst `clean`: Ohne baut das SDK ein Paket nicht neu, wenn sich nur Dateien wie `scan.js` geändert haben
(Fehlergedächtnis Nr. 10). Nach dem Einspielen nachsehen, ob die Änderung im Gerät angekommen ist.

### Test in der OpenWrt-VM

QEMU mit dem Abbild `x86-64-generic-ext4-combined-efi` im Testordner, Netz `-netdev user,restrict=on` (die VM erreicht
weder das Heimnetz noch das Internet), nur `127.0.0.1:8080` → LuCI und `127.0.0.1:2222` → SSH weitergeleitet. Pakete
per `ssh … "cat > /tmp/x.apk"` und `apk add --allow-untrusted`.

**Test im Heimnetz:** dieselbe VM über libvirt (`mnm-openwrt-test`, Abbild im Speicherbereich `default`) an der
Netzbrücke des Rechners. OpenWrt ist dafür reines Endgerät: `lan` holt die Adresse per DHCP, `dhcp.lan` mit
`ignore=1`, `dhcpv4`, `dhcpv6` und `ra` auf `disabled` – sonst verteilte es Adressen und IPv6-Ankündigungen im Heimnetz.
Root hat ein zufälliges Passwort (LuCI), SSH mit einem eigenen Schlüssel; beides nur im lokalen Testordner.

### Erkennungspakete

Die Pakete in `scanner/src/probes.c` müssen Byte für Byte denen des Desktop-Programms entsprechen. Vor jedem Commit,
der sie berührt, vergleicht ein Skript im lokalen Testordner jedes Array mit dem C#-Quellcode (Länge und jedes Byte,
gebaute Pakete wie Oracle-TNS und DNS nachgerechnet); nur bei „all identical“ wird committet. Für die Pakete in
`scanner/src/probes_steps.c`, `probes_udp.c`, `probes_smb.c` und `bc_packets.c` macht das ein zweites Skript ebenso (jedes Array `pkt_*` gegen das Paket gleichen
Namens, das ein Extraktor aus dem C#-Quellcode liest, gebaute Pakete aus dem Builder-Code nachgerechnet). Die Prüfregeln haben
Tests in `scanner/tests/tests.c` (je Sonde eine passende und fremde Antworten, alle Prüfungen auf Antworten von 0 bis
64 Byte unter AddressSanitizer).

### Zusammenführung im Backend

    python3 tests/backend/check_merge.py <adresse des routers> [ssh-optionen]

Spielt erfundene Scans (Adressen aus dem Dokumentationsbereich) dort ein, wo das Scanprogramm sein Ergebnis schreibt,
fragt das Backend nach dem Stand und vergleicht den Bestand. Bestand und letzter Scan des Routers werden vorher
gesichert und danach zurückgespielt.

### Bedienung wie ein Nutzer (Maustest)

    python3 tests/ui/check_page.py <adresse des routers> <datei mit dem root-passwort>

Steuert Firefox ohne sichtbares Fenster über seine eingebaute Fernsteuerung (Marionette, kein Zusatzpaket) mit eigenem
Profil im Testordner; Telemetrie, Berichte und Updates sind in diesem Profil aus. Klicks sind echte Maus-Ereignisse auf
der Seite, der Bildschirm des Rechners bleibt unberührt. Geprüft wird bei 1920×1080 und 1366×768: kein Querscrollen,
Details im Fenster, jede Gruppe auf und zu, jeder Haken an und aus, Gruppenhaken, Version, Zeile wählen (auch mit
gehaltener Taste über eine Aktualisierung), Knopf in den Details stabil, höchstens vier Dienste nebeneinander, Ports
auf höchstens zwei Zeilen, Filter (Suchfeld, zweites Feld, Portfeld samt unfertiger Eingabe, „only reachable“,
„Reset filters“, Filter bleibt über Aktualisierungen). Bildschirmfotos im Testordner.

Einmal im Ruhezustand und einmal **während laufender Scans**: dazu im Hintergrund fortlaufend Einzelgerät-Scans eines
Geräts über `ubus call luci.mynetworkmonitor start` mit `target` starten (die verändern die Einstellungen des Nutzers
nicht). Vorher prüfen, dass die Geräteliste nicht leer ist; sonst ein Gerät mit offenen Ports einzeln scannen.

| Datum | Version | Ergebnis |
|---|---|---|
| 01.11.2026 | 0.0.1-beta | 58 Prüfungen grün; Pakete gebaut (apk 7,8 KB und 6 KB, Programm 16 KB); in der VM zwei Fehler im Backend gefunden (Fehlergedächtnis Nr. 1 und 2) |
| 01.11.2026 | 0.0.2-beta | Scan der VM über ubus: Ports 22, 53, 80, 443 gefunden; Stopp mit Teilergebnis; Einschleusen über Bereich und Ports abgelehnt |
| 01.11.2026 | 0.0.9-beta | Maustest bei 1920×1080 und 1366×768 ohne Befund; 129 Prüfungen grün |
| 01.11.2026 | 0.0.13-beta | Stufe 3a: 16 Pakete identisch mit dem Desktop-Programm; 156 Prüfungen grün; im Heimnetz erkannt: SSH mit Version, HTTP mit Server, DNS, RDP mit Sicherheitsstufe, VNC mit Protokoll; Maustest ohne Befund |
| 01.11.2026 | 0.0.18-beta | Maustest im Ruhezustand und während laufender Scans ohne Befund (beide Auflösungen) |
| 01.11.2026 | 0.0.19-beta | 166 Prüfungen grün; Gerätenamen im Heimnetz (VM als Endgerät): alle Namen, die das Gateway kennt, gefunden – der Namensserver aus DHCP kannte keinen; Spalte „Device“ zeigt den Namen; Maustest ohne Befund |
| 01.11.2026 | 0.0.20-beta | Filter über der Tabelle: Maustest im Ruhezustand und während laufender Scans ohne Befund (beide Auflösungen); dabei Nr. 13 gefunden und behoben |
| 01.11.2026 | 0.0.21-beta | Stufe 3b, TCP: 16 Pakete identisch; 200 Prüfungen grün, darunter vollständige Läufe gegen nachgespielte Geräte (S7 mit Steckplatzsuche und Kennung, Modbus mit Geräte-ID, WAGO, SECS/GEM mit Separate.req); im Heimnetz keine Fehlerkennung (SECS/GEM auf den Webports 5000/5001 eines NAS) |
| 01.11.2026 | 0.0.22-beta | Stufe 3b, UDP: 5 Pakete identisch; 222 Prüfungen grün (Läufe gegen nachgespielten Namensserver, BACnet-Gerät, SQL-Server-Browser, geschlossenen Port); im Heimnetz: DNS über UDP an Gateway, NAS und VM, DHCP-Server am Gateway erkannt, kein BACnet oder MS-SQL fälschlich; Maustest ohne Befund, Details zeigen „53/udp“ |
| 01.11.2026 | 0.0.23-beta | SMB-Versionen: 6 Pakete identisch (3.1.1 mit Verhandlungsangabe, wie Desktop 7.3.0.35); 228 Prüfungen grün (nachgespielter Dateiserver nimmt 3.1.1 nur mit Verhandlungsangabe an); im Heimnetz an zwei Geräten 2.0.2 bis 3.1.1 erkannt, 1.0 abgeschaltet. Maustest auf Wunsch des Nutzers zurückgestellt bis zum Abschluss der Stufe |
| 01.11.2026 | 0.0.24-beta | Zusammenführung im Backend: `check_merge.py` mit altem Stand 4 Fehler, mit neuem ohne Befund; Bestand des Routers danach unverändert |
| 01.11.2026 | 0.0.25-beta | Suche per Multicast und NetBIOS: 32 Pakete identisch (Prüfskript erfasst jetzt auch nicht-statische Arrays – vorher wären 5 ungeprüft geblieben); 268 Prüfungen grün (Auswertung aller fünf Verfahren an erfundenen Antworten, HTTP-Abruf mit chunked-Antwort gegen Testserver, Anfrage genau die des Desktop-Programms); im Heimnetz: 7 Geräte über SSDP mit Name, Hersteller und Modell, 13 über mDNS, 3 über WS-Discovery (Drucker/Scanner, Windows-Rechner), 3 über NetBIOS; ganzer Lauf 18,5 s |
| 01.11.2026 | 0.0.26-beta | TLS-Grundlage: 286 Prüfungen grün (Handschlag, Zertifikatsfelder, Anfrage über TLS gegen Testserver mit selbst signiertem Zertifikat; Server ohne TLS; stummer Server); Paket hängt von `libmbedtls21` ab, in der VM vorhanden, Scanner startet und findet 30 Geräte |
| 01.11.2026 | 0.0.27-beta | HTTPS, Webseite, SNMP: 57 Pakete bzw. Anfragen identisch mit dem Desktop-Programm; 307 Prüfungen grün (HTTPS gegen TLS-Testserver, Webseite gegen HTTP-Server, SNMP gegen nachgespielten Agenten mit Abbruch bei der ersten Frage ohne Antwort); im Heimnetz: HTTPS an 12 Ports mit TLS-Version, Webseiten an 17 Ports mit Titel, Server und Zertifikat (Inhaber, Aussteller, Ablauf, selbst signiert, weitere Namen), 2 Geräte nur mit alter TLS-Version, SNMP am Drucker mit Name, Beschreibung, Seriennummer und MAC; ganzer Lauf 12 s |
| 01.11.2026 | 0.0.28-beta | Maustest zum Abschluss ohne Befund (beide Auflösungen, 30 Geräte), neue Prüfung „Adressen einzeilig“ (mit 0.0.27 10 Treffer, Nr. 17) |
