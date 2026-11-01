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

| Datum | Version | Ergebnis |
|---|---|---|
| 01.11.2026 | 0.0.1-beta | 58 Prüfungen grün; Pakete gebaut (apk 7,8 KB und 6 KB, Programm 16 KB); in der VM zwei Fehler im Backend gefunden (Fehlergedächtnis Nr. 1 und 2) |
| 01.11.2026 | 0.0.2-beta | Scan der VM über ubus: Ports 22, 53, 80, 443 gefunden; Stopp mit Teilergebnis; Einschleusen über Bereich und Ports abgelehnt |
