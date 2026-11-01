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
5. **MAC-Adressen und Hersteller** (Wunsch des Nutzers, 01.11.2026): MAC-Adressen aus der Nachbartabelle des Routers
   (`/proc/net/arp`, IPv6 über `ip neigh`), Hersteller aus den öffentlichen IEEE-Registern. Die Liste ist einige MB
   groß: auf Knopfdruck laden; wahlweise nur im RAM (`/tmp`, kleine Router) oder dauerhaft auf dem Flash (z. B.
   MX4200 mit 512 MB). Test braucht ein Gerät im selben Netzsegment (Router oder VM an `br0`).

## Später

- **Alle Architekturen bauen:** GitHub-Bau mit dem SDK je Architektur – erst, wenn der Nutzer einen Remote will.
- **Test auf dem Router des Nutzers:** Linksys MX4200 v1 (Ziel `qualcommax/ipq807x`, Paketarchitektur
  `aarch64_cortex-a53`); SDK dafür nach Rückfrage wegen der Größe laden.
- **Rundruf-Verfahren in der VM testen:** braucht einen echten Anschluss der VM an `br0` mit abgeschaltetem DHCP –
  nur nach Rückfrage (Systemeinstellung für qemu-bridge-helper).

## Bewusst nicht (nicht wieder vorschlagen)

- Satellitenbetrieb mit dem Desktop-Programm – zu aufwendig, nicht gewünscht (01.11.2026).
