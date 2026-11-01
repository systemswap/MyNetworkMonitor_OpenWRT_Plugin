# Architektur – MyNetworkMonitor für OpenWrt

Wozu: Wer etwas ändern oder austauschen will, findet hier, wo er ansetzen muss und
warum es so gebaut ist. Bei jeder Struktur- oder Technikentscheidung aktualisieren.

## Bausteine

| Baustein | Ordner | Aufgabe | darf benutzen |
|---|---|---|---|
| Scanprogramm | `scanner/` | führt einen Scan aus und schreibt das Ergebnis als JSON; ohne Oberfläche, ohne Netzdienst | libc, mbedTLS (nur Zertifikate) |
| Verfahren | `scanner/src/methods/` | je Verfahren eine Datei (TCP-Ports, Dienst-Sonden, SSDP, NetBIOS, SNMP, ONVIF, SMB, Zertifikate) | Netzschicht, Auswertung |
| Auswertung | `scanner/src/parse/` | reine Funktionen: Antwort rein, Befund raus – ohne Netz, auf dem PC testbar | nur eigene Typen |
| Netzschicht | `scanner/src/net/` | Sockets ohne Blockieren, Zeitgrenzen, Begrenzung gleichzeitiger Verbindungen | libc |
| Dienstekatalog | `scanner/data/services.json` → `/usr/share/mynetworkmonitor/services.json` | Gruppen, Dienste und Verfahren mit Ports, Vorauswahl (`default`), geplant/gebaut (`available`); später auch die Erkennungspakete | – |
| Backend für LuCI | `openwrt/luci-app-mynetworkmonitor/root/usr/share/rpcd/ucode/` | startet und stoppt das Scanprogramm, liefert Stand und Ergebnis | rpcd, UCI |
| Seite in LuCI | `openwrt/luci-app-mynetworkmonitor/htdocs/` | Einstellungen, Start/Stopp, Ergebnistabelle | rpcd-Aufrufe |
| Pakete | `openwrt/` | eine Paketquelle für das SDK von OpenWrt: je Paket ein Makefile (Ausgabe apk) | – |

## Ablauf eines Scans

1. Die Seite schickt die Werte ihres Formulars mit „Start scan“ an das Backend; es prüft sie, speichert sie in UCI
   (`/etc/config/mynetworkmonitor`), setzt aus den angekreuzten Netzen des Routers und den eigenen Bereichen die
   Zielliste zusammen und startet `mynetworkmonitor-scan` im Hintergrund. Läuft schon einer, lehnt es ab. „Scan this
   device“ scannt nur ein Gerät und lässt die gespeicherten Einstellungen unverändert.
2. Das Scanprogramm sucht zuerst die Geräte (Nachbartabelle, DHCP-Zuteilungen, Herstellerliste, ARP in den eigenen
   Netzen, Ping), dann ihre Namen (Rückwärtsauflösung, wenn „Names from DNS“ gewählt ist), dann ihre TCP-Ports
   und zuletzt die Dienste auf den offenen Ports, und schreibt `/tmp/mynetworkmonitor/scan.json` höchstens einmal je
   Sekunde neu.
3. Die Seite fragt jede Sekunde den Stand ab. Das Backend liefert die Geräteliste mit dem laufenden Scan
   hineingemischt (ohne zu speichern); ist der Scan beendet, mischt es ihn einmal fest in die Liste.

## Geräteliste

- Liegt in `/tmp/mynetworkmonitor/store/devices.json` (RAM) oder, mit „Keep after reboot“, in
  `/etc/mynetworkmonitor/devices.json` (Flash, einmal je Scan geschrieben). Dort liegt auch die Herstellerliste `manuf`.
- Ein Gerät ist dasselbe, wenn die MAC-Adresse gleich ist; ohne MAC (nur per Ping gefunden) zählt die IPv4-Adresse.
- Ein vollständiger Portscan eines Geräts ersetzt seine Ports, ein abgebrochener ergänzt nur.
- „Online“ heißt: im letzten Scan gesehen. Gefundene Geräte bleiben bei „Stop“ und über Scans hinweg; „Clear table“
  leert die Liste.

Es läuft **kein Dienst, der lauscht**: Von außen ist nichts erreichbar, und ohne laufenden Scan verbraucht das
Paket keinen Arbeitsspeicher.

## Plattformunterschiede

Keine im Code: Das Programm nutzt nur POSIX und Linux-Schnittstellen, die auf allen Architekturen von OpenWrt
gleich sind. Byte-Reihenfolge (MIPS und PowerPC sind Big Endian) wird nur über `htons`/`ntohs` bzw. byteweises
Lesen behandelt, nie über das Umdeuten von Speicher.

## Gespeicherte Daten

| Datei | Ort | Formatversion | Übernahme älterer Stände |
|---|---|---|---|
| Einstellungen | `/etc/config/mynetworkmonitor` (UCI) | Option `version` (2) | `uci-defaults/90-luci-mynetworkmonitor` bringt ältere Stände auf das aktuelle Format |
| Laufender Scan | `/tmp/mynetworkmonitor/scan.json` | Feld `format` (2) | ein anderes Format wird nicht gelesen |
| Geräteliste | `…/store/devices.json` bzw. `/etc/mynetworkmonitor/devices.json` | Feld `format` (1) | ein anderes Format gilt als leer |

## Entscheidungen

Zeitlich geordnet, neue ans Ende.

### 01.11.2026 – Eigenständiges Paket statt Satellit

- **Entschieden:** Ein eigenes Scanprogramm auf dem Router mit Seite in LuCI; es tauscht nichts mit dem
  Desktop-Programm aus.
- **Warum:** Wunsch des Nutzers; einfacher als ein Satellitenbetrieb, und der Router ist allein schon nützlich.

### 01.11.2026 – C mit dem SDK von OpenWrt

- **Entschieden:** Das Scanprogramm ist in C geschrieben und wird mit dem SDK von OpenWrt für jede
  Paketarchitektur gebaut.
- **Warum:** Es soll auf allen Geräten laufen. OpenWrt 25.12.6 baut für 35 Architekturen, darunter 32-Bit-PowerPC,
  armeb und alte MIPS-Kerne. C mit dem SDK geht überall, das Programm bleibt klein (einige hundert Kilobyte) und
  nutzt die Bibliotheken, die OpenWrt ohnehin mitbringt.
- **Verworfen:** .NET (kein MIPS, 40–70 MB); Go (kein 32-Bit-PowerPC und kein armeb, 5–10 MB je Programm –
  zu groß für Geräte mit 8 oder 16 MB Flash); Rust (MIPS nur noch ohne fertige Standardbibliothek); nur ucode
  (keine TLS-Anbindung für die Zertifikate, wenig geeignet für viele gleichzeitige Verbindungen).

### 01.11.2026 – Lizenz GPL-2.0-only

- **Entschieden:** GPL-2.0-only (Entscheidung des Nutzers).
- **Warum:** Üblich für Programme von OpenWrt; offen für die offiziellen Paketquellen, und niemand kann eine
  geschlossene Fassung daraus machen.

### 01.11.2026 – Scanprogramm ohne libubox

- **Entschieden:** Das Scanprogramm ist reines POSIX-C: eigene Ereignisschleife mit `poll`, eigene JSON-Ausgabe,
  Einstellungen als Aufrufparameter (das Backend in LuCI liest sie aus UCI und baut den Aufruf).
- **Warum:** Es baut und läuft auf dem PC genauso wie auf dem Router; Tests und Fuzzing brauchen nichts von
  OpenWrt, und das Paket hängt von einer Bibliothek weniger ab.
- **Verworfen:** libubox (uloop, blobmsg) – gibt es außerhalb von OpenWrt nicht als Paket, Tests auf dem PC
  bräuchten einen eigenen Bau davon.

### 01.11.2026 – Nur eigene Netze scannen

- **Entschieden:** Das Scanprogramm nimmt ab Werk nur private und lokale Adressen an (10/8, 172.16/12, 192.168/16,
  169.254/16, 127/8); ein Eintrag mit einer öffentlichen Adresse lässt den ganzen Scan ablehnen. `--allow-public`
  hebt das auf, die Seite bietet es nicht an (Wunsch des Nutzers: nur das Heimnetz scannen).
- **Warum:** Ein Tippfehler im Bereich soll nie Rechner im Internet treffen. Das Internet bleibt für andere
  Funktionen erreichbar (z. B. Herstellerliste laden) – gesperrt ist nur das Scannen.

### 01.11.2026 – Dienste in Gruppen wählen

- **Entschieden:** Statt einer Portliste wählt man Dienste und Verfahren in Gruppen wie im Desktop-Programm (Geräte
  finden, Geräte erkennen, Netzwerk-Dienste, Fernwartung, Dateidienste, Kameras, SQL, NoSQL, Industrie), dazu
  „Additional ports“. Probiert werden nur die Ports der angehakten Dienste. Ab Werk sind SQL, NoSQL und Industrie aus
  (Wunsch des Nutzers: im Heimnetz nichts suchen, was dort nicht vorkommt).
- **Eine Stelle:** Katalog und Vorauswahl stehen nur in `services.json`; das Backend rechnet daraus die Ports, das
  Skript `uci-defaults/90-luci-mynetworkmonitor` liest die Vorauswahl mit `jsonfilter`. `services_chosen` merkt sich,
  dass eine Auswahl gespeichert wurde – auch eine leere bleibt leer.

### 01.11.2026 – Dienst-Sonden wie im Desktop-Programm

- **Entschieden:** Die Erkennungspakete sind Byte für Byte die des Desktop-Programms (`scanner/src/probes.c`), auch
  die Teile, die beliebig aussehen – manche Dienste antworten nur auf genau diese Folge. Die Prüfregeln sind dessen
  Regeln. Wie dort wird die Antwort genau einmal gelesen (ein `recv` bis 4096 Byte, sobald Daten da sind); Prüfungen
  wie die von BigFix (genau 12 Byte) hängen davon ab.
- **Welche Sonde auf welchen Ports:** Der Katalog nennt je Dienst seine Sonden (`probes`), das Backend gibt sie mit den
  Ports des Dienstes als `--probe id=ports` mit. Ports stehen damit nur im Katalog.
- **Abweichungen:** Die Web-Sonde prüft nur unverschlüsseltes HTTP; die HTTPS-Prüfung kommt mit TLS (mbedTLS) in
  Stufe 4. Sie liest ebenfalls einmal, statt bis zum Verbindungsende (ein Server, der die Verbindung offen hält, wird
  damit erkannt). DNS über TCP fragt einmal statt bis zu dreimal.
- **Prüfung:** ein lokales Skript vergleicht vor jedem Commit an `probes.c` jedes Array mit dem C#-Quellcode
  (`docs/PRUEFUNGEN.md`).
- **Sonden mit mehreren Schritten** (eigene Prozedur `run` in `probes.c`) laufen vor den Sonden mit einem Gruß in
  höchstens 16 Threads mit kleinem Stapel (128 KiB); jede wartet blockierend mit eigenen Zeitgrenzen (`netio.c`).
  Fundmeldungen und Fortschritt laufen unter einer Sperre, weil sie die Geräteliste ändern und Dateien schreiben.

### 01.11.2026 – Gerätenamen über Rückwärtsauflösung (DNS)

- **Entschieden:** Die Spalte „Device“ zeigt den Namen des Geräts (ohne Domain), sonst seine Adresse – nie den
  Hersteller, der hat eine eigene Spalte. Namen kommen aus den DHCP-Zuteilungen des Routers und, wenn der Dienst
  „Names from DNS“ gewählt ist (ab Werk an), aus PTR-Abfragen.
- **Welche Namensserver, in welcher Reihenfolge:** zuerst der eigene des Routers (`/etc/resolv.conf`, dnsmasq – ist
  OpenWrt der Hauptrouter, kennt er alle Geräte, denen er Adressen gab), dann der, den der Router selbst benutzt
  (`/tmp/resolv.conf.d/resolv.conf.auto`), dann das Standard-Gateway (`/proc/net/route`). Jeder wird nur nach den
  Geräten gefragt, die noch keinen Namen haben. Der zweite und dritte sind nötig, weil dnsmasq Rückwärtsfragen für
  private Adressen nicht weitergibt (Option `boguspriv`) und weil der Namensserver aus DHCP oft ein Filter oder
  Zwischenspeicher ist, der die Geräte nicht kennt – der Router davor kennt sie dann meist.
- **Datenschutz:** Der zweite und dritte Server wird nur gefragt, wenn seine Adresse privat ist; Fragen nach eigenen
  Geräten gehen nie an einen Server im Internet.
- **Verworfen:** `nslookup` je Gerät aufrufen – ein Prozess je Adresse und Ausgabe, die man zerlegen müsste; das
  Scanprogramm schickt alle Fragen über einen UDP-Socket (höchstens 1000 je Sekunde) und ordnet die Antworten über
  ihre Kennung zu. Geräte, die kein Namensserver kennt, bekommen Namen später über NetBIOS und mDNS.

### 01.11.2026 – Sonden nur an Geräte der Tabelle

- **Entschieden:** Gesucht wird in Stufen: erst die Geräte, dann deren TCP-Ports, dann die Dienste. TCP-Sonden gehen nur
  an offene Ports gefundener Geräte, und nur an die Ports ihres Dienstes. UDP-Sonden (DNS über UDP, BACnet,
  SQL-Server-Browser) gehen nur an Geräte der Tabelle, nie an einen ganzen Bereich. Rundrufe (WS-Discovery für ONVIF,
  SSDP, mDNS, NetBIOS) werden einmal je Netz gesendet; wer antwortet, kommt in die Tabelle oder wird dort ergänzt,
  und Abfragen per Unicast danach (z. B. die Gerätebeschreibung) gehen nur an Geräte der Tabelle.
- **Warum:** weniger Last im Netz und kürzere Scans; empfindliche Geräte bekommen keine Pakete für Dienste, die sie
  nicht anbieten.

### 01.11.2026 – Filter über der Tabelle

- **Entschieden:** zwei Suchfelder, die beide passen müssen (Name, Adressen, MAC auch ohne Trennzeichen, Hersteller,
  Netz, vermutetes System, erkannte Dienste mit ihren Angaben), ein Portfeld (`445`, `80,443`, `5000-5100`: Geräte mit
  mindestens einem dieser offenen Ports) und „only reachable“. Die Filter wirken nur auf die Anzeige, laufen im
  Browser und werden nicht gespeichert. Ein unfertiges Portfeld (`80-`) wird rot umrandet und filtert nichts.
- **Nicht übernommen** aus dem Desktop-Programm: Auswahl nach Dienst und Bereich als Klappliste, IPv4/IPv6-Schalter,
  Doppelte – auf dem Router reichen Suchfeld (findet auch Dienstnamen) und Gruppierung nach Netz.

