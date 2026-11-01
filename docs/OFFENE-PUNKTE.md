# Offene Punkte – MyNetworkMonitor für OpenWrt

Was als Nächstes ansteht, was bewusst zurückgestellt ist und was nicht wieder
vorgeschlagen werden soll. Erledigtes wird gestrichen (die Geschichte steht in Git und
im Fehlergedächtnis).

## Als Nächstes

Ausbaustufen, jede für sich benutzbar:

1. **Geräteliste im Test bestätigen** (gebaut in 0.0.4-beta): Seite mit Tabelle und Details vom Nutzer ansehen lassen;
   Hersteller und MAC-Adressen auf einem Gerät im selben Netzsegment prüfen (MX4200 oder VM an `br0`).
2. **Dienst-Sonden, Teil b** (Teil a mit 16 Sonden in 0.0.13-beta): die Verfahren mit mehreren Schritten oder UDP –
   MS-SQL, OPC UA, S7, Modbus, WAGO, SECS/GEM, DHCP, BACnet, DNS über UDP – und die SMB-Versionen. Pakete wieder Byte
   für Byte aus dem Desktop-Programm, mit Abgleich-Skript.
3. **Rundrufe:** SSDP/UPnP samt Gerätebeschreibung, NetBIOS-Namensabfrage, ONVIF (WS-Discovery).
4. **SNMP, SMB, Zertifikate:** SNMP v2c (Systemname, Beschreibung), angebotene SMB-Versionen, Zertifikate von
   Webseiten (Aussteller, Gültigkeit, Namen) über mbedTLS.

## Später

- **Alle Architekturen bauen:** GitHub-Bau mit dem SDK je Architektur – erst, wenn der Nutzer einen Remote will.
- **Test auf dem Router des Nutzers:** Linksys MX4200 v1 (Ziel `qualcommax/ipq807x`, Paketarchitektur
  `aarch64_cortex-a53`); SDK dafür nach Rückfrage wegen der Größe laden.
- **Rundruf-Verfahren in der VM testen:** braucht einen echten Anschluss der VM an `br0` mit abgeschaltetem DHCP –
  nur nach Rückfrage (Systemeinstellung für qemu-bridge-helper).

## Bewusst nicht (nicht wieder vorschlagen)

- Satellitenbetrieb mit dem Desktop-Programm – zu aufwendig, nicht gewünscht (01.11.2026).
