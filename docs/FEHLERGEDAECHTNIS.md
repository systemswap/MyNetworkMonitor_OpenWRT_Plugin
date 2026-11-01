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
