# Fremde Bestandteile und ihre Lizenzen

Mit dem Programm werden die folgenden Bestandteile anderer Projekte weitergegeben; ihre Rechte liegen
bei den jeweiligen Autoren. Die vollständigen Lizenztexte liegen im Ordner `licenses/` neben dem
Programm. Neue Bestandteile werden im selben Commit eingetragen, in dem sie hinzukommen.

## Bibliotheken und Werkzeuge

Das Paket bringt keine fremden Bestandteile mit. Das Scanprogramm nutzt Bibliotheken, die OpenWrt selbst als
eigene Pakete installiert; sie werden zur Laufzeit geladen und nicht mit diesem Paket weitergegeben:

| Bestandteil | Lizenz | Wofür | Quelle |
|---|---|---|---|
| mbedTLS 3.6 (über das OpenWrt-Paket libmbedtls; gebaut und getestet mit 3.6.7) | Apache-2.0 OR GPL-2.0-or-later (Text: `licenses/mbedtls-LICENSE.txt`) | HTTPS erkennen und Zertifikate lesen; nur in `scanner/src/tlsconn.c` | https://github.com/Mbed-TLS/mbedtls |

Nur beim Bauen auf GitHub: die SDK-Container von OpenWrt (`openwrt/sdk:<architektur>-25.12.6`, GPL-2.0,
https://hub.docker.com/r/openwrt/sdk), daraus geholt die Paketbeschreibung von mbedTLS (`package/libs/mbedtls`
aus https://github.com/openwrt/openwrt, GPL-2.0), die Aktionen `actions/checkout`, `actions/upload-artifact`,
`actions/download-artifact` (MIT, https://github.com/actions) und `softprops/action-gh-release` (MIT,
https://github.com/softprops/action-gh-release), je auf einen Commit festgelegt. Nichts davon wird weitergegeben.

Nur beim Entwickeln: das SDK von OpenWrt (GPL-2.0, https://openwrt.org), für die Tests auf dem PC der C-Compiler
des Systems und mbedTLS aus demselben Quellarchiv, das das SDK benutzt (fest in das Testprogramm gebunden, wird
nicht weitergegeben).

## Daten

| Daten | Herkunft | Lizenz |
|---|---|---|
| MAC-Herstellerliste (wird nicht mitgeliefert, auf Knopfdruck auf den Router geladen) | Datei `manuf` des Wireshark-Projekts (https://www.wireshark.org/download/automated/data/manuf), zusammengestellt aus den öffentlichen Registern der IEEE Registration Authority (MA-L, MA-M, MA-S) | Fakten aus einem öffentlichen Register; die Datei nennt keine eigenen Bedingungen |

## Übertragener Code

Keiner.
