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
