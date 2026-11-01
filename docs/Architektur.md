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
- **Sonden mit mehreren Schritten** (eigene Prozedur `run`, für Datenbanken und Steuerungen in `probes_steps.c`) laufen vor den Sonden mit einem Gruß in
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

### 01.11.2026 – Sonden für Steuerungen und MS-SQL (Stufe 3b, TCP)

- **Entschieden:** MS-SQL, OPC UA, S7, Modbus, WAGO und SECS/GEM folgen dem Ablauf des Desktop-Programms Schritt für
  Schritt, mit denselben Paketen. Die Pakete stehen in `scanner/src/probes_steps.c`; ein Skript im lokalen Testordner
  liest sie aus dem C#-Quellcode und vergleicht sie vor jedem Commit (`docs/PRUEFUNGEN.md`). Gebaute Pakete entstehen
  aus diesen Vorlagen: die S7-Verbindungsanfrage mit Verbindungsart und Rack/Steckplatz in den letzten zwei Bytes, die
  Modbus-Registerabfrage mit Register und Anzahl in den Bytes 8 bis 11.
- **S7:** Bestätigt die Steuerung die übliche Anfrage (PG, Rack 0, Steckplatz 2) nicht, sucht die Sonde den
  Steckplatz: erst 23 übliche, dann – nur wenn die Steuerung ablehnt statt zu schweigen – alle 744 übrigen, höchstens
  5 s, je sechs gleichzeitig. Gewinner ist die erste bestätigte in der Reihenfolge der Liste. Danach Bestellnummer,
  Firmware und Namen aus den Systemzustandslisten 0x0011 und 0x001C.
- **SECS/GEM:** Eine angenommene Sitzung wird mit Separate.req wieder beendet; die Anlage erlaubt nur eine, und die
  gehört ihrem Leitrechner. S1F13 (Establish Communication) wird nie gesendet – es versetzt Anlagen in den
  Kommunikationszustand.
- **Abweichungen:** Ein Versuch je Sonde statt bis zu drei. Die S7-Suche hört nach der ersten Sechsergruppe mit einer
  Bestätigung auf, statt alle Anfragen zu senden (gleiches Ergebnis, weniger Verbindungen). Noch nicht übernommen, weil
  nur für die Details: OPC-UA-Endpunkte (die Anfragen enthalten Zeitstempel und Kanalnummern der Laufzeit),
  SECS/GEM-Abfrage von Modell und Softwarestand (S1F1) und das Warten auf die Selbstvorstellung der Anlage.

### 01.11.2026 – Sonden über UDP und Rundruf (Stufe 3b, UDP)

- **Entschieden:** Jede Sonde trägt ihren Transport (`transport` in `struct mnm_probe`): TCP-Sonden gehen an offene
  Ports, UDP-Sonden (DNS, BACnet, SQL-Server-Browser) an jedes Gerät der Tabelle – UDP hat keine Portsuche, die sagt,
  wo ein Dienst lauscht –, Rundrufe (DHCP) einmal je eigenem Netz der Ziele. Die UDP-Runde läuft nach der TCP-Runde,
  damit eine Sonde entfallen kann, wenn eine andere den Dienst schon fand (`unless`: der SQL-Server-Browser nur, wo
  1433 nicht als MS-SQL antwortete – wie im Desktop-Programm). Beide Runden zählen in der Seite als ein Fortschritt.
- **Katalog:** `udp` und `udp_probes` neben `tcp` und `probes`; das Backend gibt beide als `--probe id=ports` mit, das
  Scanprogramm erkennt am Transport der Sonde, was es damit tut. Die Ausgabe kennzeichnet UDP-Funde (`"udp": true`),
  die Seite zeigt sie als „53/udp“.
- **Wartezeiten** wie im Desktop-Programm fest: DNS dreimal 1 s mit 200 ms Pause, BACnet 2 s je Anfrage,
  SQL-Server-Browser höchstens dreimal 2 s in 3 s, DHCP 2 s.
- **DHCP:** Der Socket hängt an der Schnittstelle (`SO_BINDTODEVICE`) und an keiner Adresse – Linux gibt
  Rundruf-Antworten nur an solche Sockets weiter; das Desktop-Programm bindet an die Adresse der Schnittstelle.
- **Abweichungen in der Auswertung** (gesendet wird dasselbe): DHCP liest die Server-Kennung (Option 54) aus der
  Optionsliste nach RFC 2132 statt das erste Byte 54 irgendwo in der Antwort zu nehmen. BACnet liest Werte nur aus
  einer ComplexACK (ASHRAE 135, 20.1.5) – sonst würde aus einer Ablehnung eine „VendorID“; eine zu kurze Antwort
  oder eine ausbleibende Antwort auf eine einzelne Eigenschaft lässt nur diese Angabe weg, statt den Fund zu
  verwerfen. UDP-Sockets sind verbunden, nehmen also nur Antworten des gefragten Geräts an.

### 01.11.2026 – SMB-Versionen (Stufe 3b, SMB)

- **Entschieden:** Für jeden Dialekt eine eigene Verbindung zu Port 445 (1.0, 2.0.2, 2.1, 3.0, 3.0.2, 3.1.1), ein
  Lesen bis 1024 Byte; ein Server nimmt einen angefragten Dialekt im Status seiner Antwort an oder lehnt ihn ab, nur
  einzelne Anfragen zeigen also, was er anbietet. Angenommen heißt: SMB1 mit einer Dialektnummer ungleich 0xFFFF,
  SMB2/3 mit Status 0 ([MS-SMB2] 2.2.1). Wartezeiten wie im Desktop-Programm: 5 s für die Verbindung, 2 s für die
  Antwort. Auf Port 139 läuft die Prüfung nicht – dort braucht SMB zuerst eine NetBIOS-Sitzung.
- **Pakete** Byte für Byte die des Desktop-Programms. Die Anfrage für 3.1.1 trägt die Angabe zur Vorab-Integrität
  (2.2.3.1.1); ohne sie lehnen Server den Dialekt ab (`STATUS_INVALID_PARAMETER`) – im Desktop-Programm mit 7.3.0.35
  ebenso korrigiert, mit Zustimmung des Nutzers.
- **Abweichung:** Die siebte Anfrage des Desktop-Programms mit allen Dialekten, deren Antwort es nicht auswertet,
  entfällt.

### 01.11.2026 – Suche per Multicast und NetBIOS-Namen

- **Entschieden:** SSDP/UPnP, WS-Discovery, ONVIF und mDNS laufen in der Gerätesuche nach ARP und Ping, gleichzeitig
  in jedem eigenen Netz der Ziele, mit den Paketen, Wiederholungen und Abständen des Desktop-Programms: SSDP einmal
  M-SEARCH an 239.255.255.250:1900; WS-Discovery dreimal die Probe nach 2009, dann dreimal nach 2005 im Abstand von
  200 ms an 239.255.255.250:3702, jede mit neuer Nachrichtenkennung; ONVIF dreimal dieselbe Probe im Abstand von
  500 ms; mDNS 27 PTR-Anfragen im Abstand von 100 ms an 224.0.0.251:5353. Zugehört wird insgesamt 6,5 s. Geräte, die
  nur darauf antworten, kommen in die Liste – nur aus den gewählten Bereichen – und damit in die Portsuche.
  NetBIOS (Knotenstatus, UDP 137) fragt danach jedes Gerät der Liste einzeln, bis zu zweimal 5 s, 50 zugleich.
- **Pakete** Byte für Byte die des Desktop-Programms (`scanner/src/bc_packets.c`, abgeglichen mit einem Skript); die
  Nachrichtenkennung (GUID nach .NET-Form „D“) und die NetBIOS-Transaktionsnummer sind wie dort zufällig.
- **Namen:** mDNS (Adresseintrag des Absenders) und NetBIOS füllen den Gerätenamen, wenn die DHCP-Zuteilungen keinen
  kennen; NetBIOS läuft nach der DNS-Abfrage und füllt nur, was dann noch fehlt.
- **Abweichungen in der Auswertung** (gesendet wird dasselbe):
  - SSDP: Die Gerätebeschreibung (LOCATION) wird einmal je Gerät geladen und ihr Name, Hersteller und Modell
    gemeldet – das Desktop-Programm lädt sie je Antwort und verwirft sie. Geladen wird nur über HTTP und nur von der
    Adresse des Geräts selbst, die Anfrage ist die des Desktop-Programms (nur `Host`).
  - ONVIF: Die zwei Folgeabrufe des Desktop-Programms (`…/media_service`, `…/network_service`) entfallen – sie gehen
    an Adressen, die ONVIF nicht kennt, und ihr Ergebnis wird nie gemeldet. Erkannt werden auch Antworten der Form
    2009; von mehreren Dienstadressen gilt die erste.
  - mDNS: nur Antworten (QR-Bit), Fragen über ihre Anzahl übersprungen, die eigenen Anfragen des Routers zählen
    nicht; die Adresse ist die des Absenders, nicht die eines Adresseintrags.
  - SSDP-Kopfzeilen werden nur am Zeilenanfang erkannt (`X-SERVER:` ist kein `SERVER:`).
  - Empfangen wird mit `poll` statt wechselnder, liegengelassener Empfangsaufträge; der Router sieht mindestens alle
    Antworten, die das Desktop-Programm sieht.

### 01.11.2026 – TLS über mbedTLS

- **Entschieden (mit Zustimmung des Nutzers):** HTTPS-Erkennung und Zertifikate über mbedTLS, die TLS-Bibliothek von
  OpenWrt (`libmbedtls`, auf jedem Abbild mit LuCI über HTTPS oder Paket-Downloads schon da; fehlt sie, holt `apk`
  sie beim Installieren). Gebunden wird dynamisch; das Paket wächst nicht. mbedTLS kennt nur `scanner/src/tlsconn.c`
  (Verbindung, Lesen, Schreiben, Zertifikatsfelder); alles andere sieht nur dessen schmale Schnittstelle.
- **Zertifikate werden gelesen, nicht geprüft** (`MBEDTLS_SSL_VERIFY_NONE`): Es geht darum, wer ein Gerät zu sein
  behauptet; Geräte im eigenen Netz haben meist selbst signierte Zertifikate. Nichts davon wird für etwas vertraut.
- **Grenzen der OpenWrt-Fassung:** TLS 1.2 und 1.3; kein reiner RSA- und kein DHE-RSA-Schlüsselaustausch, kein
  TLS 1.0/1.1 (mbedTLS 3). Sehr alte Geräte, die nur das können, werden trotzdem als TLS erkannt – ihre Antwort ist
  ein TLS-Datensatz –, ihr Zertifikat bleibt aber unbekannt (`MNM_TLS_REFUSED`).
- **Tests:** auf dem PC mit mbedTLS aus demselben Quellarchiv wie das SDK, gebaut im lokalen Testordner
  (`make test MBEDTLS=<ordner>`), mit Thread-Unterstützung wie bei OpenWrt; der TLS-Test erzeugt zur Laufzeit ein
  selbst signiertes Zertifikat.

### 01.11.2026 – HTTPS, Webseite mit Zertifikat und SNMP

- **HTTPS** (`webservices_tls`, Dienst „HTTPS“): die zweite Hälfte der Web-Sonde des Desktop-Programms – derselbe
  Port über TLS mit dessen kurzer Anfrage (`Host`, `Connection: close`), ohne SNI wie dort (Ziel ist eine Adresse).
  Erkannt wie über HTTP (Statuszeile oder `<html`); die Angabe nennt den `Server`-Kopf und die TLS-Version. Antwortet
  ein Port mit TLS, ohne dass ein Handschlag mit TLS 1.2/1.3 gelingt, wird er als TLS gemeldet („only with older
  versions or ciphers“) – das Desktop-Programm zählt ihn als Fehler.
- **Webseite** (`webidentity`, Dienst „Web page“, Katalog „Web page and certificate“, Ports 80, 443, 8080, 8443): die
  Anfrage des Desktop-Programms (`User-Agent: MyNetworkMonitor`, `Accept: text/html`), TLS nur auf 443 und 8443, je
  2,5 s für Verbindung und Antwort, gelesen bis `</title>`, Verbindungsende oder 64 KiB. Angaben wie dort: Titel
  (HTML-Entitäten entschlüsselt, Leerraum zusammengefasst), Server, für wen das Zertifikat ist, wer es ausstellte,
  bis wann es gilt („ - EXPIRED“), selbst signiert, weitere Namen. Abweichungen: Jeder Port wird gemeldet, nicht nur
  der erste mit Ergebnis; die weiteren Namen werden immer gelesen (das Desktop-Programm findet sie unter Linux nie,
  weil es nach „=“ in der Textform sucht); ein Port, der ohne Inhalt schließt, ist kein Fund.
- **SNMP** (`snmp`, UDP 161 an jedes Gerät der Tabelle): die Pakete der SNMP-Bibliothek des Desktop-Programms, Byte
  für Byte (54, abgeglichen); Kennung der Anfrage aus einem Zähler mit zufälligem Start im Bereich, den die
  Bibliothek mit vier Bytes kodiert – so behalten die Pakete ihre Länge. Ablauf wie dort: Systemgruppe (v1, 2 s),
  Seriennummer, Hardware-Adressen der Schnittstellen 1 bis 50, Zusatzfragen für Zebra und WAGO (v2c). Abweichungen:
  Die 50 Adressfragen enden bei der ersten ohne jede Antwort (das Desktop-Programm wartet bis zu 100 s); Werte, die
  keine druckbaren Zeichen sind, aber UTF-8, werden als Text gezeigt, andere als Hex – nie wird ein Text wie „12“
  als Hex gelesen; v2c-Ausnahmen (noSuchObject) bleiben leer statt als Text zu erscheinen. Der Systemname füllt einen
  fehlenden Gerätenamen (Angabe „Name: …“ am Anfang, allgemein in `main.c`).

### 01.11.2026 – Bau aller Architekturen und Releases auf GitHub

- **Entschieden (Wunsch des Nutzers):** `.github/workflows/build-and-release.yml` baut für die Paketarchitekturen
  aus `packaging/architectures` (dort alle 35 von OpenWrt 25.12.6; aktiv sind auf Wunsch des Nutzers vom 01.11.2026
  `aarch64_cortex-a53` für den MX4200 und den Raspberry Pi 3 mit 64 Bit, `arm_cortex-a7_neon-vfpv4` für Pi 2/3 mit
  32 Bit, `aarch64_cortex-a72` für Pi 4, `aarch64_cortex-a76` für Pi 5) im offiziellen SDK-Container (`openwrt/sdk:<architektur>-25.12.6`) und legt
  ein Release mit allen Paketen an – einmal je Version in `VERSION`, wenn es dafür noch keins gibt, und ergänzt es,
  wenn später eine Architektur dazukommt (Text des Releases bleibt der des Versions-Commits); Beta-Versionen als
  Vorabversion. Begleitdateien (Anleitung, Geräteseite, Lizenz, Herkunftsangaben) werden mit dem Release verglichen
  und bei Änderung allein hochgeladen, ohne Tests und Bau. Davor laufen die Tests (mbedTLS aus dem Quellarchiv, das auch das SDK nimmt, mit Prüfsumme).
- **mbedTLS im SDK:** Es liegt in der Basis-Paketquelle von OpenWrt. Statt sie in jedem Bau ganz zu klonen, holt der
  Bau nur `package/libs/mbedtls` aus genau dem Commit, aus dem das SDK gebaut ist (aus `feeds.conf.default`).
- **Dateinamen im Release:** `mynetworkmonitor-scan_<version>_<architektur>.apk` (die Pakete selbst heißen in allen
  Architekturen gleich), `luci-app-mynetworkmonitor_<version>_all.apk` einmal, dazu `ANLEITUNG.txt`, `LICENSE`,
  `THIRD-PARTY-NOTICES.md` und `SHA256SUMS`. Die Pakete sind nicht signiert; die Installation bestätigt das.
- **Aktionen** auf Commits festgelegt, nicht auf Tags (ein Tag kann auf anderen Code zeigen).

