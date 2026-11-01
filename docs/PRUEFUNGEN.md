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

    cd scanner && make BUILD=<testordner>/host test

Eigene kleine Testsammlung (`scanner/tests/tests.c`), gebaut mit AddressSanitizer und UBSan: Ziel- und Portlisten,
JSON-Ausgabe mit ungültigem UTF-8, TCP-Scan gegen lauschende und geschlossene Ports auf 127.0.0.1.

### Pakete bauen

SDK von OpenWrt im Testordner, Paketquelle `src-link mynetworkmonitor <testordner>/feed` – `feed` ist ein Verweis auf
`openwrt/` dieses Repositorys ohne Leerzeichen im Pfad (das SDK trennt `feeds.conf` am Leerzeichen):

    ./scripts/feeds update mynetworkmonitor && ./scripts/feeds install -a -p mynetworkmonitor
    make package/mynetworkmonitor-scan/compile package/luci-app-mynetworkmonitor/compile

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
gebaute Pakete wie Oracle-TNS und DNS nachgerechnet); nur bei „all identical“ wird committet. Die Prüfregeln haben
Tests in `scanner/tests/tests.c` (je Sonde eine passende und fremde Antworten, alle Prüfungen auf Antworten von 0 bis
64 Byte unter AddressSanitizer).

### Bedienung wie ein Nutzer (Maustest)

    python3 tests/ui/check_page.py <adresse des routers> <datei mit dem root-passwort>

Steuert Firefox ohne sichtbares Fenster über seine eingebaute Fernsteuerung (Marionette, kein Zusatzpaket) mit eigenem
Profil im Testordner; Telemetrie, Berichte und Updates sind in diesem Profil aus. Klicks sind echte Maus-Ereignisse auf
der Seite, der Bildschirm des Rechners bleibt unberührt. Geprüft wird bei 1920×1080 und 1366×768: kein Querscrollen,
Details im Fenster, jede Gruppe auf und zu, jeder Haken an und aus, Gruppenhaken, Version, Zeile wählen (auch mit
gehaltener Taste über eine Aktualisierung), Knopf in den Details stabil, höchstens vier Dienste nebeneinander, Ports
auf höchstens zwei Zeilen. Bildschirmfotos im Testordner.

| Datum | Version | Ergebnis |
|---|---|---|
| 01.11.2026 | 0.0.1-beta | 58 Prüfungen grün; Pakete gebaut (apk 7,8 KB und 6 KB, Programm 16 KB); in der VM zwei Fehler im Backend gefunden (Fehlergedächtnis Nr. 1 und 2) |
| 01.11.2026 | 0.0.2-beta | Scan der VM über ubus: Ports 22, 53, 80, 443 gefunden; Stopp mit Teilergebnis; Einschleusen über Bereich und Ports abgelehnt |
| 01.11.2026 | 0.0.9-beta | Maustest bei 1920×1080 und 1366×768 ohne Befund; 129 Prüfungen grün |
| 01.11.2026 | 0.0.13-beta | Stufe 3a: 16 Pakete identisch mit dem Desktop-Programm; 156 Prüfungen grün; im Heimnetz erkannt: SSH mit Version, HTTP mit Server, DNS, RDP mit Sicherheitsstufe, VNC mit Protokoll; Maustest ohne Befund |
