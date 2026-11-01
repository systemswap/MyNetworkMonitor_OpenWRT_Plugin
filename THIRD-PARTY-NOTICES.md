# Fremde Bestandteile und ihre Lizenzen

Mit dem Programm werden die folgenden Bestandteile anderer Projekte weitergegeben; ihre Rechte liegen
bei den jeweiligen Autoren. Die vollständigen Lizenztexte liegen im Ordner `licenses/` neben dem
Programm. Neue Bestandteile werden im selben Commit eingetragen, in dem sie hinzukommen.

## Bibliotheken und Werkzeuge

Das Paket bringt keine fremden Bestandteile mit. Das Scanprogramm nutzt Bibliotheken, die OpenWrt selbst als
eigene Pakete installiert; sie werden zur Laufzeit geladen und nicht mit diesem Paket weitergegeben:

| Bestandteil | Lizenz | Wofür | Quelle |
|---|---|---|---|
| mbedTLS (über das OpenWrt-Paket libmbedtls) | Apache-2.0 | Zertifikate von Webseiten lesen (Stufe 4) | https://github.com/Mbed-TLS/mbedtls |

Nur beim Entwickeln: das SDK von OpenWrt (GPL-2.0, https://openwrt.org), für die Tests auf dem PC der C-Compiler
des Systems.

## Daten

| Daten | Herkunft | Lizenz |
|---|---|---|
| MAC-Herstellerliste (wird nicht mitgeliefert, auf Knopfdruck auf den Router geladen) | Datei `manuf` des Wireshark-Projekts (https://www.wireshark.org/download/automated/data/manuf), zusammengestellt aus den öffentlichen Registern der IEEE Registration Authority (MA-L, MA-M, MA-S) | Fakten aus einem öffentlichen Register; die Datei nennt keine eigenen Bedingungen |

## Übertragener Code

Keiner.
