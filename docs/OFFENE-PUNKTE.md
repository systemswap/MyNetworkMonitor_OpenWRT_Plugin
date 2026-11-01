# Offene Punkte – MyNetworkMonitor für OpenWrt

Was als Nächstes ansteht, was bewusst zurückgestellt ist und was nicht wieder
vorgeschlagen werden soll. Erledigtes wird gestrichen (die Geschichte steht in Git und
im Fehlergedächtnis).

## Als Nächstes

Ausbaustufen, jede für sich benutzbar:

1. **Geräteliste im Test bestätigen** (gebaut in 0.0.4-beta): Seite mit Tabelle und Details vom Nutzer ansehen lassen;
   Hersteller und MAC-Adressen auf einem Gerät im selben Netzsegment prüfen (MX4200 oder VM an `br0`).
2. **Dienst-Sonden, Details** (Stufe 3b fertig mit 0.0.23-beta): OPC-UA-Endpunkte, SECS/GEM-Modell (S1F1).
3. **Gerätesuche gründlicher:** eine zweite ARP-Runde für Adressen ohne Antwort (Geräte im WLAN-Stromsparmodus
   verpassen eine einzelne Anfrage). Grund: Sonden gehen nur an Geräte der Tabelle (`docs/Architektur.md`), ein
   übersehenes Gerät bekommt keine. In anderen Netzen hinter einem Router gilt als Gerät, wer auf Ping antwortet
   (Entscheidung des Nutzers, 01.11.2026) – kein zusätzliches Anklopfen an TCP-Ports.

4. **Test im Firmennetz** (Wunsch des Nutzers, 01.11.2026; im Heimnetz gibt es diese Geräte nicht). Die Sonden
   sind gegen nachgespielte Geräte getestet und im Heimnetz ohne Fehlerkennung, aber noch an keinem echten Gerät:
   - MS-SQL auf 1433 (Version, Verschlüsselung) und eine benannte Instanz über den SQL-Server-Browser (UDP 1434),
   - OPC UA (4840),
   - S7 auf 102: eine S7-300/400 (Rack 0, Steckplatz 2) und eine S7-1200/1500 (Suche nach dem Steckplatz),
     Bestellnummer und Firmware,
   - Modbus TCP (502) mit Geräte-ID, ein WAGO-Controller über Modbus und über 6626,
   - SECS/GEM (5000-5002): angenommene Sitzung, belegte Sitzung (nur Ablehnung oder Schweigen),
   - BACnet (UDP 47808) mit Name und Hersteller,
   - DHCP hinter einem Relay (Serverkennung),
   - SMB-Versionen an Windows-Servern (auch SMB 1.0, wo noch an),
   - ONVIF-Kameras (im Heimnetz keine), NetBIOS mit Domänen statt Arbeitsgruppen,
   - SNMP an Switches und Zebra-Druckern, HTTPS an Geräten mit Zertifikaten einer Firmen-CA.
   Dabei Laufzeit des ganzen Scans und Last auf dem Router ansehen. Echte Antworten als Testfälle übernehmen
   (Adressen und Namen erfunden, Mitschnitte nur im lokalen Testordner).

## Später

- **Alle Architekturen bauen:** GitHub-Bau mit dem SDK je Architektur – nur auf Anweisung des Nutzers.
- **Test auf einem echten Router:** Pakete für Linksys MX4200 v1 (`aarch64_cortex-a53`, OpenWrt 25.12) ab 0.0.26-beta
  gebaut; Test auf dem Gerät steht aus (Installation, Scan, Rundrufe über WLAN-Brücke, Laufzeit und Speicher).

## Bewusst nicht (nicht wieder vorschlagen)

- Satellitenbetrieb mit dem Desktop-Programm – zu aufwendig, nicht gewünscht (01.11.2026).
