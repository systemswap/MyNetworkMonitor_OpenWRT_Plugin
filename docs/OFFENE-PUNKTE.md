# Offene Punkte – MyNetworkMonitor für OpenWrt

Was als Nächstes ansteht, was bewusst zurückgestellt ist und was nicht wieder
vorgeschlagen werden soll. Erledigtes wird gestrichen (die Geschichte steht in Git und
im Fehlergedächtnis).

## Als Nächstes

Ausbaustufen, jede für sich benutzbar:

1. **Gerüst und TCP-Portscan:** Scanprogramm mit Netzschicht (gleichzeitige Verbindungen, Zeitgrenzen), Ergebnis als
   JSON; Paket-Makefiles; Seite in LuCI mit Bereich, Portliste, Start/Stopp und Ergebnistabelle; Test in der
   OpenWrt-VM.
2. **Dienst-Sonden:** `probes.json` mit allen Erkennungspaketen; zuerst die Sonden mit einfacher Antwortprüfung,
   dann die mit eigenem Verfahren (OPC UA, S7, SECS/GEM, Modbus, MS-SQL, BACnet, DHCP, DNS, WAGO, Web).
3. **Rundrufe:** SSDP/UPnP samt Gerätebeschreibung, NetBIOS-Namensabfrage, ONVIF (WS-Discovery).
4. **SNMP, SMB, Zertifikate:** SNMP v2c (Systemname, Beschreibung), angebotene SMB-Versionen, Zertifikate von
   Webseiten (Aussteller, Gültigkeit, Namen) über mbedTLS.

## Später

- **Alle Architekturen bauen:** GitHub-Bau mit dem SDK je Architektur – erst, wenn der Nutzer einen Remote will.
- **Test auf dem Router des Nutzers:** Modell nennt der Nutzer später.

## Bewusst nicht (nicht wieder vorschlagen)

- Satellitenbetrieb mit dem Desktop-Programm – zu aufwendig, nicht gewünscht (01.11.2026).
