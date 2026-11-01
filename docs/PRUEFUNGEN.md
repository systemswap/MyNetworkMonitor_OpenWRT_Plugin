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
