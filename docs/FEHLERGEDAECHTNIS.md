# Fehlergedächtnis – MyNetworkMonitor_OpenWRT_Plugin

Jeder behobene Fehler bekommt hier einen Eintrag – auch kleine, sobald man aus ihnen
etwas lernen kann. Zweck: Wer später an derselben Stelle arbeitet (Mensch oder KI),
soll wissen, was schon schiefging, warum, und warum die Lösung so aussieht – damit
derselbe Fehler bei einem Umbau nicht wiederkommt.

Regeln:

- Zeitlich geordnet nach der Behebung, neue Einträge ans Ende, fortlaufend nummeriert.
- Versionen und Commits aus `git log` nachsehen, nicht schätzen; Unbekanntes so
  hinschreiben („unbekannt“).
- Einträge bleiben stehen, auch wenn der Code inzwischen anders aussieht.
- Ohne Titel und Namen aus dem Testmaterial und ohne persönliche Daten – Testmaterial
  technisch beschreiben.
- Vor Änderungen an einer Stelle hier nach ihr suchen (Datei- oder Klassenname).

---

## Nr. 1 – <Bereich>: <kurze Beschreibung>

- **Entdeckt:** Version <x.y.z> (Commit <abc1234>, <TT.MM.JJJJ>)
- **Behoben:** Version <x.y.z> (Commit <abc1234>)
- **Wie bemerkt:** <wer oder was hat ihn gefunden – Nutzer beim Arbeiten, ein Test,
  eine Messung, ein Prüfdurchgang, ein Absturz – und woran: Meldung, falsches Ergebnis,
  Hänger, Tempo>
- **Was passiert ist:** <Symptom, so wie der Nutzer es gesehen hat>
- **Wo aufgetreten:** <Ablauf, Eingabe, Gerät, System – z. B. „beim Öffnen einer
  Datei mit Umlaut im Pfad unter Windows“>
- **Warum:** <die eigentliche Ursache, nicht nur der Auslöser>
- **Lösung:** <was geändert wurde>
- **Warum diese Lösung:** <Begründung; welche anderen Wege es gab und warum sie
  verworfen wurden>
- **Nicht tun:** <was den Fehler zurückbringen würde>
- **Im Code:** <Dateien, Klassen, Methoden>
- **Abgesichert durch:** <Test (der mit dem alten Code fehlschlägt) oder anderer
  Nachweis; „kein Test, weil …“>
- **Verweise:** <ausführlicher Abschnitt, betroffene andere Projekte>

## Nr. 1 – Backend für LuCI: rpcd lud das ucode-Skript nicht (Regex mit Wiederholung über 255)

- **Entdeckt:** 01.11.2026, Version 0.0.1-beta (erster Test in der OpenWrt-VM: `ubus list` ohne `luci.mynetworkmonitor`).
  **Behoben:** 0.0.2-beta.
- **Was passiert ist:** Das Backend fehlte am ubus, ohne Meldung im Systemprotokoll. `ucode <datei>` zeigte „Invalid
  contents of {}“ an `/^[0-9.,\/ -]{1,1024}$/`.
- **Warum:** ucode übersetzt Regex mit der Bibliothek von musl; die erlaubt Wiederholungen nur bis 255 (`RE_DUP_MAX`).
  Ein Syntaxfehler macht die ganze Datei ungültig, rpcd überspringt sie still.
- **Lösung:** Muster ohne Mengenangabe (`+`), Länge getrennt mit `length()` geprüft.
- **Nicht tun:** Wiederholungen über 255 in Regex für ucode; ein neues Backend ohne `ucode <datei>` einspielen.
- **Im Code:** `openwrt/luci-app-mynetworkmonitor/root/usr/share/rpcd/ucode/mynetworkmonitor.uc` (`TARGETS_PATTERN`,
  `PORTS_PATTERN`, `MAX_LENGTH`).
- **Abgesichert durch:** Test in der VM (`docs/PRUEFUNGEN.md`); ucode prüft die Syntax beim Laden.

## Nr. 2 – Backend für LuCI: Start mit Zahlen wurde von ubus abgelehnt

- **Entdeckt:** 01.11.2026, Version 0.0.1-beta (Test in der VM: `ubus call … start` mit `timeout_ms: 300` → „Invalid
  argument“). **Behoben:** 0.0.2-beta.
- **Warum:** rpcd leitet die Typen der Argumente aus den Beispielwerten in `args` ab; ucode-Zahlen werden zu 64-Bit-
  Zahlen, LuCI und die ubus-Befehlszeile schicken aber 32-Bit-Zahlen – ubus lehnt den Aufruf ab.
- **Lösung:** `timeout_ms` und `parallel` als Zeichenketten; das Backend wandelt und prüft sie selbst (`int_option`).
- **Nicht tun:** Zahlen als Beispielwerte in `args` eines ucode-Backends.
- **Im Code:** `mynetworkmonitor.uc` (`start`), `htdocs/…/scan.js` (`handleStart`).
- **Abgesichert durch:** Test in der VM (Start, Stand, Ergebnis, Stopp, Versuche mit `;` und `$(…)` abgelehnt).

## Nr. 3 – Geräteliste: qsort mit Nullzeiger bei Geräten ohne offene Ports

- **Entdeckt:** 01.11.2026 vor 0.0.4-beta (UBSan in den Tests: „null pointer passed as argument 1“). **Behoben:**
  0.0.4-beta, vor dem ersten Commit des Codes.
- **Was:** `mnm_devices_write_json` sortierte die Ports jedes Geräts mit `qsort(d->tcp, 0, …)`; bei einem Gerät ohne
  offene Ports ist `d->tcp` NULL. Laut C-Norm undefiniert, auch für null Elemente – meist harmlos, aber ein
  Optimierer darf daraus schließen, der Zeiger sei nie NULL.
- **Lösung:** nur sortieren, wenn mindestens zwei Ports da sind.
- **Nicht tun:** Bibliotheksfunktionen mit NULL aufrufen, „weil die Länge 0 ist“ (auch `memcpy`, `memcmp`).
- **Im Code:** `scanner/src/devices.c` (`mnm_devices_write_json`).
- **Abgesichert durch:** `test_devices_json` unter UBSan.

## Nr. 4 – Update: alte Einstellungen bleiben im alten Format

- **Entdeckt:** 01.11.2026, beim Update von 0.0.3-beta auf 0.0.4-beta in der VM. **Behoben:** 0.0.4-beta.
- **Was:** OpenWrt behält eine geänderte `/etc/config/mynetworkmonitor` beim Update (Konfigurationsdatei des Pakets).
  Die neue Fassung erwartet `networks` und `ranges`, die alte hatte `targets` – der eingetragene Bereich wäre
  stillschweigend nicht mehr benutzt worden.
- **Lösung:** `root/etc/uci-defaults/90-luci-mynetworkmonitor` bringt Format 1 auf 2 (`targets` → `ranges`, `lan`
  angekreuzt, `persist` aus); `postinst` ruft es nach einem Update im laufenden Betrieb auf.
- **Nicht tun:** Optionen umbenennen ohne Übernahme; die Option `version` vergessen hochzuzählen.
- **Im Code:** `openwrt/luci-app-mynetworkmonitor/root/etc/uci-defaults/90-luci-mynetworkmonitor`, `Makefile` (`postinst`).
- **Abgesichert durch:** Lauf in der VM auf einer Einstellung im Format 1.

## Nr. 5 – Seite: Klicks gingen verloren, weil die Tabelle jede Sekunde neu gebaut wurde

- **Entdeckt:** 01.11.2026, Version 0.0.7-beta (Maustest im ferngesteuerten Firefox: Klick auf eine Zeile traf ein
  schon ersetztes Element; der Nutzer berichtete, manches lasse sich nicht anklicken). **Behoben:** 0.0.9-beta.
- **Was:** `update()` ersetzte jede Sekunde die ganze Tabelle und den Detailbereich. Fiel der Neuaufbau zwischen
  Drücken und Loslassen der Maustaste, gab es keinen Klick – die Zeile oder der Knopf „Scan this device“ war in dem
  Moment ein anderes Element.
- **Lösung:** Zeilen werden mit einer Signatur ihres Geräts zwischengespeichert und nur neu gebaut, wenn sich etwas
  geändert hat; die Tabelle selbst bleibt dasselbe Element, ihre Kinder werden nur getauscht, wenn sich die Reihenfolge
  ändert. Die Details nur bei Änderung am gewählten Gerät oder am Laufzustand.
- **Nicht tun:** Bedienelemente in einer Abfrageschleife ersetzen; `dom.content()` für Bereiche mit Klickzielen bei jeder
  Abfrage.
- **Im Code:** `htdocs/…/scan.js` (`renderTable`, `update`).
- **Abgesichert durch:** `tests/ui/check_page.py` – langsamer Klick (Taste 1,5 s gehalten, über eine Aktualisierung
  hinweg) wählt die Zeile; der Knopf in den Details bleibt über Aktualisierungen dasselbe Element.

## Nr. 6 – Herstellerliste: nur die zwölfstelligen Kurznamen

- **Entdeckt:** 01.11.2026, Version 0.0.8-beta (Bildschirmfoto aus dem Maustest: „HewlettPacka“, „RealtekSemic“).
  **Behoben:** 0.0.9-beta.
- **Warum:** Die Datei `manuf` richtet ihre Spalten mit Leerzeichen aus (`00:01:E6         ⇥HewlettPacka⇥Hewlett
  Packard`). Der Parser nahm die Leerzeichen hinter dem Präfix als Kurznamen und den Kurznamen als vollen Namen. Die
  Tests hatten nur Zeilen ohne Füllzeichen.
- **Lösung:** Felder sind der Text zwischen zwei Tabulatoren, Leerzeichen außen herum werden entfernt.
- **Nicht tun:** Testdaten für Formate selbst ausdenken, ohne eine echte Zeile der Quelle anzusehen.
- **Im Code:** `scanner/src/textdata.c` (`mnm_vendor_parse_line`, `trim`).
- **Abgesichert durch:** `test_vendors` mit Zeilen im echten Format (schlagen mit dem alten Parser fehl).

## Nr. 7 – Seite: Gruppenhaken klappte die Gruppe mit auf und zu

- **Entdeckt:** 01.11.2026, Version 0.0.5-beta (Nutzer: Auf- und Zuklappen funktioniere nicht richtig, manche Haken
  ließen sich nicht setzen). **Behoben:** 0.0.6-beta.
- **Warum:** Gruppenhaken und Gruppenname lagen zusammen in einem `<summary>`; ein Klick auf den Haken schaltete
  zugleich das `<details>`. Geplante Einträge („coming“) waren gesperrt und ließen sich gar nicht anhaken.
- **Lösung:** eigener Knopf zum Auf- und Zuklappen neben dem Gruppenhaken; geplante Einträge anhakbar, die Auswahl
  gilt, sobald sie gebaut sind.
- **Im Code:** `htdocs/…/scan.js` (`renderCatalog`, `updateGroupBoxes`).
- **Abgesichert durch:** `tests/ui/check_page.py` (jede Gruppe auf und zu, jeder Haken an und aus, Gruppenhaken).

## Nr. 8 – Seite: Gerätename im Detailbereich verschwand beim Scrollen unter der Kopfleiste

- **Entdeckt:** 01.11.2026, Version 0.0.9-beta (Nutzer: beim Scrollen wandert der Name aus dem Sichtfeld; im Maustest
  nachgestellt – Name 13 px vom oberen Rand, Kopfleiste bis 40 px). **Behoben:** 0.0.10-beta.
- **Warum:** Der Detailbereich bleibt beim Scrollen stehen (`position: sticky`), aber 1em unter dem Fensterrand. Die
  Kopfleiste von LuCI bleibt ebenfalls oben stehen und deckte den Anfang des Bereichs zu.
- **Lösung:** `placeDetails()` misst die Höhe der Kopfleiste (nur wenn sie selbst stehen bleibt) und setzt den Abstand
  als CSS-Variable `--mnm-top`, auch bei Größenänderung des Fensters; der Bereich ist höchstens so hoch wie der Rest des
  Fensters und scrollt sonst in sich.
- **Nicht tun:** feste Abstände für Kopfleisten annehmen – Höhe und Verhalten hängen vom Theme und der Fensterbreite ab.
- **Im Code:** `htdocs/…/scan.js` (`placeDetails`, Stil `.mnm-details`).
- **Abgesichert durch:** `tests/ui/check_page.py` – nach dem Scrollen liegt der Name unter der Kopfleiste und der
  Bereich endet im Fenster, bei 1920×1080 und 1366×768.

## Nr. 9 – Seite: nach „Scan this device“ galten alle anderen Geräte als offline

- **Entdeckt:** 01.11.2026, Version 0.0.12-beta (Bildschirmfoto aus dem Maustest: alle Punkte grau). **Behoben:**
  0.0.13-beta.
- **Warum:** „Online“ heißt „im letzten Scan gesehen“, und auch ein Scan eines einzelnen Geräts zählte als letzter
  Scan. Alle Geräte, die er nicht gefragt hatte, galten danach als offline.
- **Lösung:** Ein Scan, dessen Ziel eine einzelne Adresse ist, setzt den Zeitpunkt des letzten Scans nicht
  (`single_device`); das gescannte Gerät bekommt trotzdem seine neue Zeit „zuletzt gesehen“.
- **Nicht tun:** Teil-Scans wie vollständige behandeln, wenn daraus Aussagen über nicht gefragte Geräte folgen.
- **Im Code:** `root/usr/share/rpcd/ucode/mynetworkmonitor.uc` (`single_device`, `merge_finished_scan`, `status`).
- **Abgesichert durch:** Ablauf in der VM (Einzelgerät-Scan nach einem vollständigen Scan).

## Nr. 10 – Seite: „[object HTMLElement]“ statt der erkannten Dienste in den Details

- **Entdeckt:** 01.11.2026, Version 0.0.13-beta (Nutzer: unter SERVICES & PORTS steht „object html.element“; während
  des Scans stimmte es, danach nicht mehr). **Behoben:** 0.0.15-beta, im zweiten Anlauf.
- **Warum:** Die Bauhilfe `E()` von LuCI löst eine Ebene von Listen auf; die Zeile eines Ports enthielt eine Liste mit
  einer weiteren Liste darin (je erkannter Dienst Name und Version). Die innere wurde als Text ausgegeben. Während des
  Scans waren noch keine Dienste erkannt, dann stand dort „open, not recognised“ ohne Verschachtelung – deshalb trat
  der Fehler erst nach dem Scan auf. Der erste Anlauf (`flatMap`) ließ die Liste noch eine Ebene zu tief stehen.
- **Lösung:** Die Teile jeder Zeile werden in eine flache Liste ausgebreitet (`...what`).
- **Zweite Ursache für das Ausbleiben:** Das SDK baute das Paket der Seite bei gleicher Version nicht neu, obwohl sich
  `scan.js` geändert hatte – in der VM lag noch die alte Datei. Seitdem vor dem Einspielen immer
  `make package/<paket>/clean` vor `compile`, und nachsehen, ob die Änderung im Gerät angekommen ist.
- **Nicht tun:** verschachtelte Listen an `E()` geben; einem Paket glauben, ohne die eingespielte Datei zu prüfen.
- **Im Code:** `htdocs/…/scan.js` (`renderDetails`).
- **Abgesichert durch:** `tests/ui/check_page.py` – klickt jede Zeile an und prüft, dass nirgends „[object“ steht
  (schlug mit 0.0.13- und 0.0.14-beta an).

## Nr. 11 – Seite: während eines Scans gingen Klicks auf Zeilen und „Scan this device“ verloren

- **Entdeckt:** 01.11.2026, Version 0.0.16-beta (Maustest, während der Nutzer einen Scan laufen hatte: langsamer Klick
  wählte die Zeile nicht, Knopf in den Details wurde ersetzt). **Behoben:** 0.0.18-beta.
- **Warum:** Während eines Scans mischte das Backend dessen Geräte mit der aktuellen Uhrzeit als „zuletzt gesehen“ in
  die Liste. Jede Sekunde änderte sich damit jedes Gerät des Scans, und die Seite baute all diese Zeilen neu
  (Nr. 5 half nur bei unveränderten Daten). Die Details wurden zusätzlich bei jedem Wechsel zwischen „läuft“ und
  „fertig“ neu gebaut, nur um den Knopf zu sperren.
- **Lösung:** Während des Scans gilt dessen Beginn als Zeit; eine Zeile ändert sich nur, wenn sich am Gerät etwas
  ändert. Zeilen werden schon beim Drücken der Maustaste gewählt. Der Knopf in den Details wird nur gesperrt oder
  freigegeben, die Details werden nur bei Änderungen am Gerät neu gebaut.
- **Nicht tun:** in Abfragen, die die Seite jede Sekunde holt, Werte mitgeben, die sich ohne Grund jede Sekunde ändern.
- **Im Code:** `root/usr/share/rpcd/ucode/mynetworkmonitor.uc` (`status`), `htdocs/…/scan.js` (`renderTable`, `update`).
- **Abgesichert durch:** `tests/ui/check_page.py`, auch während laufender Scans (im Hintergrund fortlaufend
  Einzelgerät-Scans eines Geräts, siehe `docs/PRUEFUNGEN.md`).

## Nr. 12 – Seite/Scanprogramm: in der Spalte „Device“ stand der Hersteller statt des Gerätenamens

- **Entdeckt:** 01.11.2026, Version 0.0.18-beta (Nutzer: „bei Device steht der Hersteller anstatt des Hostnamen“).
  **Behoben:** 0.0.19-beta.
- **Was und wo:** Bei Geräten ohne Namen zeigte die Tabelle den Hersteller, und Namen hatten fast nur Geräte, denen
  der Router selbst eine Adresse gab. In einem Netz, in dem ein anderer Router die Adressen vergibt, stand damit fast
  überall der Hersteller.
- **Warum:** Das Scanprogramm kannte Namen nur aus den DHCP-Zuteilungen von dnsmasq; die Seite nahm ersatzweise den
  Hersteller.
- **Lösung:** Rückwärtsauflösung (PTR) für alle gefundenen Geräte, nacheinander beim eigenen Namensserver, beim
  Namensserver des Routers und beim Standard-Gateway (nur private Adressen); die Seite zeigt den Namen ohne Domain,
  sonst die Adresse. Warum diese Reihenfolge: `docs/Architektur.md`, „Gerätenamen über Rückwärtsauflösung“.
- **Falle bei der Suche:** Im Testnetz verteilt DHCP als Namensserver einen Zwischenspeicher, der Rückwärtsfragen für
  private Adressen mit NXDOMAIN beantwortet; erst der Router davor (das Gateway) kennt die Namen. Eine Abfrage aus der
  VM sah deshalb so aus, als beantworte „der Router“ der VM die Frage anders als dem PC – gefragt war aber ein anderer
  Server.
- **Nicht tun:** den Namensserver aus DHCP für allwissend halten; bei Vergleichen immer ausdrücklich angeben, welcher
  Server gefragt wird. Den Hersteller nicht als Gerätenamen ausgeben.
- **Im Code:** `scanner/src/dnsname.c` (`mnm_ptr_query`, `mnm_ptr_answer`, `mnm_default_gateway`,
  `mnm_discover_dns_names`), `scanner/src/main.c` (Namensphase), `htdocs/…/scan.js` (`displayName`), Katalogeintrag
  `dnsnames` in `scanner/data/services.json`, Übernahme in bestehende Einstellungen in `uci-defaults` (Version 4).
- **Abgesichert durch:** `scanner/tests/tests.c` (`test_dns_names`: Frage, Antwort mit Kompression, falsche Kennung,
  NXDOMAIN, Zeigerschleife, jede abgeschnittene Länge, Standard-Gateway aus der Routentabelle); Test in der VM im
  Heimnetz: alle Namen, die das Gateway kennt, kamen an.

## Nr. 13 – Seite: lange Gerätenamen machten die Tabelle bei 1366 Pixeln zu breit

- **Entdeckt:** 01.11.2026, Version 0.0.19-beta (Maustest bei 1366×768 nach dem ersten Scan mit Namen: Tabelle 11 Pixel
  breiter als ihr Bereich). **Behoben:** 0.0.20-beta.
- **Warum:** Mit der Rückwärtsauflösung kamen Namen wie „Gerät-mit-MAC-im-Namen“ in die Spalte „Device“, die keine
  Grenze hatte; ein Name ohne Trennstelle bestimmte die Spaltenbreite.
- **Lösung:** Spalte „Device“ höchstens 13em breit, lange Namen brechen an jeder Stelle um (`overflow-wrap:anywhere`);
  der volle Name steht im Tooltip und in den Details.
- **Nicht tun:** Spalten mit Text aus dem Netz (Namen, Hersteller, Dienstangaben) ohne Höchstbreite lassen – die
  Tests mit dem eigenen Testbestand zeigen nicht jeden Namen, der vorkommt.
- **Im Code:** `htdocs/…/scan.js` (`STYLE`, `.mnm-name`).
- **Abgesichert durch:** `tests/ui/check_page.py` („device table fits its area“ bei 1366×768).


## Nr. 14 – Repository: kompilierte Python-Datei mit dem Home-Pfad eingecheckt

- **Entdeckt:** 01.11.2026, vor dem ersten Push (Durchsicht aller Dateien der Historie von Hand; die Prüfung der
  Historie sucht nicht in Binärdateien). Eingecheckt seit 0.0.10-beta. **Behoben:** im aktuellen Stand mit dem Commit
  „Kompilierte Python-Datei aus dem Repository genommen“, in der Historie am 01.11.2026 umgeschrieben (vor dem ersten
  Push, also ohne Force-Push).
- **Was und wo:** `tests/ui/__pycache__/marionette.cpython-313.pyc` – Python legt sie beim Start des Maustests an; sie
  enthält den vollständigen Pfad der Quelldatei samt Benutzername.
- **Warum:** `.gitignore` schloss `__pycache__/` nicht aus, und `git add -A` nahm die Datei mit.
- **Lösung:** `__pycache__/` und `*.pyc` in `.gitignore`; Datei mit dem Bereinigungs-Werkzeug der gemeinsamen Vorlage
  (`--entfernen "tests/ui/__pycache__"`) aus allen Commits entfernt, vorher an einer Kopie geprüft: nur diese Datei
  fehlt in den 9 betroffenen Commits, Nachrichten und aktueller Dateibaum unverändert. Sicherung des alten Stands als
  Bundle im lokalen Testordner.
- **Nicht tun:** Hilfsprogramme im Repository starten, ohne ihre Ausgaben auszuschließen; vor dem ersten Push nur der
  Textprüfung trauen – Binärdateien der Historie von Hand ansehen (`git log --all --name-only`).
- **Im Code:** `.gitignore`.
- **Abgesichert durch:** `.gitignore`; Durchsicht der Dateiliste der Historie vor jedem ersten Push (`Regeln/GitHub.md`
  der gemeinsamen Vorlage, Schritt 2).

## Nr. 15 – Backend: Geräte mit gemeinsamer MAC-Adresse fielen im Bestand zu einer Zeile zusammen

- **Entdeckt:** 01.11.2026, Version 0.0.23-beta (Nutzer: das Gateway fehlte
  nach einem Scan in der Tabelle). **Behoben:** 0.0.24-beta.
- **Was:** Der Scan fand 30 Geräte, die Tabelle zeigte 24. Sieben Adressen hatten dieselbe MAC-Adresse: der Router
  mit zwei Adressen und die Geräte hinter seinem WLAN bzw. einem Repeater, der ARP für sie mit seiner eigenen
  MAC-Adresse beantwortet. Im Bestand blieb davon eine Zeile, mit der zuletzt verarbeiteten Adresse.
- **Warum:** Das Backend ordnete jedes gescannte Gerät dem gespeicherten mit derselben MAC-Adresse zu – gedacht für
  Geräte, die per DHCP eine neue Adresse bekommen. Dass mehrere Adressen eine MAC-Adresse teilen, war nicht bedacht;
  auch die Seite unterschied Zeilen an der MAC-Adresse.
- **Lösung:** Zugeordnet wird über Adresse und MAC-Adresse zusammen. Eine Zeile wandert nur dann zu einer neuen
  Adresse, wenn die MAC-Adresse im Scan genau einmal vorkommt und die alte Adresse im Scan fehlt – und nie bei einem
  Einzelgerät-Scan, der das nicht sehen kann. Nutzt ein anderes Gerät eine alte Adresse, bekommt es eine eigene Zeile.
  Die Seite nimmt Adresse und MAC-Adresse als Schlüssel einer Zeile.
- **Nicht tun:** die MAC-Adresse allein als Kennung eines Geräts nehmen.
- **Im Code:** `root/usr/share/rpcd/ucode/mynetworkmonitor.uc` (`find_device`, `merge`), `htdocs/…/scan.js` (`keyOf`).
- **Abgesichert durch:** `tests/backend/check_merge.py` (gegen das Backend eines Routers: Router mit zwei Adressen
  und Gerät dahinter, Adresswechsel per DHCP, Einzelgerät-Scan, neues Gerät auf alter Adresse; mit dem alten Code
  alle vier fehlgeschlagen).

## Nr. 16 – Repository: Netzangaben aus dem Testnetz in Doku und Testdaten

- **Entdeckt:** 01.11.2026, nach dem Push von 0.0.24-beta (die Prüfung vor dem Commit zeigte die Zeile an, der Befehl
  committete trotzdem). **Behoben:** am selben Tag, Historie umgeschrieben, Force-Push durch den Nutzer.
- **Was:** Ein Zitat im Eintrag Nr. 15 enthielt eine Adresse aus dem Heimnetz des Nutzers; die Testdaten des Scanners
  (DNS-Test, Test der privaten Bereiche) nutzten seit 0.0.1-beta Adressen und einen Namen aus dem Testnetz.
- **Warum:** Beispieldaten aus der echten Umgebung übernommen; die Prüfung vor dem Commit lief im selben Befehl wie
  der Commit, ihr Fund hielt ihn nicht auf.
- **Lösung:** Adressen durch einen anderen privaten Bereich und den Namen durch `nas.home.arpa` ersetzt (gleiche
  Längen, damit die Byte-Zählungen der Tests in allen alten Commits stimmen), das Zitat entfernt – im aktuellen Stand
  und mit dem Bereinigungs-Werkzeug der gemeinsamen Vorlage in allen Commits. Vorher an einer Kopie geprüft: nur
  Zeilen mit den ersetzten Angaben geändert, zwei Korrektur-Commits fielen als leer weg, aktueller Dateibaum gleich,
  Tests eines alten Stands grün. Sicherung des alten Stands als Bundle im lokalen Testordner, die Ersetzungsliste
  ebenda.
- **Nicht tun:** echte Adressen, Gerätenamen oder Zitate mit solchen Angaben in Doku, Tests oder Commits übernehmen;
  die Prüfung auf persönliche Angaben und den Commit in einem Befehl verketten – erst prüfen, dann committen.
- **Abgesichert durch:** Durchsicht der vorgemerkten Änderungen in einem eigenen Schritt vor jedem Commit.

## Nr. 17 – Seite: Adressen in der Spalte „Device“ mitten in der Zahl umgebrochen

- **Entdeckt:** 01.11.2026, Version 0.0.27-beta (Bildschirmfoto des Maustests bei 1366×768, sinngemäß „192.0.2.2“ / „01“ für 192.0.2.201 –
  hier mit einer Dokumentationsadresse beschrieben). **Behoben:** 0.0.28-beta.
- **Was:** Geräte ohne Namen zeigen in „Device“ ihre Adresse; bei schmaler Tabelle brach sie an beliebiger Stelle um.
- **Warum:** Die Regel aus Nr. 13 (`overflow-wrap:anywhere`, damit lange Namen die Tabelle nicht verbreitern) galt
  auch für Adressen; deren kleinste Breite war damit ein Zeichen.
- **Lösung:** Eine Adresse an Stelle des Namens bricht nie um (`strong.mnm-address`, `white-space:nowrap`); Namen
  weiter wie in Nr. 13. Den Platz dafür gibt ein etwas kleinerer Innenabstand der Zellen unserer Tabelle.
- **Nicht tun:** eine Umbruchregel für Namen ohne Blick auf die anderen Werte der Spalte setzen; nach einer Änderung
  der Breiten nur eine Auflösung prüfen.
- **Im Code:** `htdocs/…/scan.js` (`STYLE`, Zeile der Tabelle).
- **Abgesichert durch:** `tests/ui/check_page.py` („addresses in the device column stay on one line“, vergleicht die
  Angabe in „Device“ mit der Spalte IPV4; mit dem alten Stand 10 Treffer bei 1366×768) und „device table fits its
  area“.

## Nr. 18 – Anleitung: Installation über die Router-Oberfläche bricht mit Fehlercode 99 ab

- **Entdeckt:** 08.11.2026, Version 0.0.28-beta (Meldung des Nutzers vom ersten echten Router: „beendet mit
  errorcode 99“). Die falsche Angabe stand seit 0.0.28-beta (Commit 1a8e96a) in der Anleitung. **Behoben:** in
  0.0.28-beta (nur Doku, Release-Begleitdatei ersetzt).
- **Was:** `packaging/ANLEITUNG.txt` und `README.md` nannten als ersten Weg LuCI *System → Software → Paket
  hochladen…* mit „die Frage nach dem nicht signierten Paket bestätigen“. Die Installation brach dort immer ab.
- **Warum:** Unsere Pakete sind nicht mit einem Schlüssel aus `/etc/apk/keys` signiert. LuCI fragt zwar nach, ruft
  dann aber `/usr/libexec/package-manager-call install /tmp/upload.apk` auf. Für apk reicht dieses Skript nur `-r` und
  `--force-overwrite` durch, alles andere verwirft es. apk läuft also ohne `--allow-untrusted` und endet mit
  „UNTRUSTED signature“, Code 99. Getestet worden war vorher nur der Weg über SSH.
- **Wie belegt:** in der Test-VM (OpenWrt 25.12.6) mit den Paketen des Releases: ohne `--allow-untrusted` Code 99
  („UNTRUSTED signature“), mit Code 0. Eine Scanner-Datei fremder Architektur gibt dagegen Code 2
  („uninstallable“, „arch: …“). Danach das LuCI-Skript in der VM nachgelesen.
- **Lösung:** Installation nur noch per SSH beschrieben: Weg A lädt die Dateien auf dem Router mit `wget` (der Link
  wird aus dem Release kopiert, `apk add` selbst nimmt keine Adressen an), Weg B kopiert sie mit `scp -O` vom
  Rechner (der Router hat keinen SFTP-Server; neueres OpenSSH nutzt ohne `-O` SFTP und bricht ab). Beide Wege in der
  VM geprüft. Dazu eine kurze Liste der Fehlercodes. Verworfen: eigene Signatur – den öffentlichen Schlüssel müsste
  man ohnehin einmal per SSH nach `/etc/apk/keys` legen, und ein Schlüsselpaket wäre selbst unsigniert.
- **Nicht tun:** einen Installationsweg in die Anleitung schreiben, der nicht auf einem OpenWrt mit genau diesem
  Paketformat ausprobiert wurde; auf eine Rückfrage in der Oberfläche als Beleg vertrauen, dass ein Schalter
  ankommt.
- **Wo:** `packaging/ANLEITUNG.txt`, `README.md` (Abschnitt „Installation“).
- **Abgesichert durch:** keinen automatischen Test. Die Wege A und B und die beiden Fehlercodes wurden von Hand in
  der Test-VM geprüft.
