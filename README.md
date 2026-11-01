# MyNetworkMonitor für OpenWrt

Findet auf dem Router heraus, was in deinen Netzen läuft, und zeigt es als Seite in LuCI: offene
TCP-Ports, erkannte Dienste (Datenbanken, Fernwartung, Steuerungen und mehr), Geräte über SSDP/UPnP,
NetBIOS, SNMP und ONVIF, SMB-Versionen und die Zertifikate von Webseiten.

Der Router ist dafür ein guter Ort: Er steht in jedem deiner Netze und VLANs und erreicht dort auch die
Geräte, die sich nur auf Rundrufe melden.

Für OpenWrt 25.12 und neuer, auf allen Prozessorarten, für die OpenWrt Pakete baut.

**Stand:** im Aufbau, noch nicht benutzbar.

## Aufbau

- `mynetworkmonitor-scan` – das Scanprogramm (C). Es läuft nur, wenn du in LuCI einen Scan startest, und
  lauscht auf keinem Port.
- `luci-app-mynetworkmonitor` – die Seite in LuCI unter *Netzwerk → MyNetworkMonitor*.

## Privatsphäre

Das Programm sendet keine Nutzungsdaten und verbindet sich nur mit den Geräten in den Netzen, die du
zum Scannen auswählst – nur private Netze, nie Adressen im Internet. Ins Internet geht es nur, wenn du
die Herstellerliste lädst (von www.wireshark.org). Geräteliste und Herstellerliste liegen ab Werk im
Arbeitsspeicher des Routers (`/tmp`) und sind nach einem Neustart weg; mit „Keep after reboot“ bleiben sie auf
dem Flash. Scanne nur Netze, für die du die Erlaubnis hast – Portscans und Abfragen können empfindliche
Geräte wie Steuerungen stören.

## Lizenz

GNU General Public License, Version 2 (`LICENSE`). Fremde Bestandteile und ihre Lizenzen:
`THIRD-PARTY-NOTICES.md`.

Mit KI-Unterstützung entwickelt.
