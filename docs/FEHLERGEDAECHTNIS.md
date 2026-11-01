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
