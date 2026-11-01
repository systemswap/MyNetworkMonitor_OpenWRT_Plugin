# Welche Datei für welches Gerät?

Jeder Router braucht zwei Dateien:

- `luci-app-mynetworkmonitor_<version>_all.apk` – die Seite in der Router-Oberfläche, für **jedes** Gerät gleich,
- `mynetworkmonitor-scan_<version>_<architektur>.apk` – den Scanner, passend zum **Chipsatz** des Geräts.

Welche Scanner-Datei passt, hängt am Chipsatz bzw. am OpenWrt-Ziel des Geräts. Die Tabelle nennt Beispiele; alle
Geräte stehen so in den offiziellen Gerätelisten von OpenWrt 25.12.6. Ein Gerät, das hier fehlt, findet man in der
Firmware-Auswahl von OpenWrt (https://firmware-selector.openwrt.org) – dort steht sein Ziel, z. B.
`qualcommax/ipq807x` – oder per SSH auf dem Router mit `apk --print-arch`.

## Scanner-Dateien in diesem Release

| Scanner-Datei | Chipsatz (OpenWrt-Ziel) | Beispielgeräte |
|---|---|---|
| `…_aarch64_cortex-a53.apk` | Qualcomm IPQ807x / IPQ8174 (`qualcommax/ipq807x`) | Linksys MX4200 v1/v2, Linksys MX5300, Dynalink DL-WRX36, Xiaomi AX3600, Xiaomi AX9000, QNAP 301w |
| | Qualcomm IPQ60xx (`qualcommax/ipq60xx`) | GL.iNet GL-AX1800, GL.iNet GL-AXT1800, Linksys MR7350 |
| | MediaTek Filogic – MT7981, MT7986, MT7988 (`mediatek/filogic`) | OpenWrt One, GL.iNet GL-MT3000, GL.iNet GL-MT6000, Xiaomi Mi Router AX3000T, Cudy WR3000, ASUS TUF-AX4200, Zyxel EX5601-T0, Bananapi BPi-R3, Bananapi BPi-R4 |
| | MediaTek MT7622 (`mediatek/mt7622`) | Linksys E8450, Belkin RT3200, Bananapi BPi-R64 |
| | Marvell Armada 3700 (`mvebu/cortexa53`) | Marvell ESPRESSObin, CZ.NIC Turris MOX |
| | Broadcom BCM2837, 64 Bit (`bcm27xx/bcm2710`) | Raspberry Pi 3A+/3B/3B+/CM3, Zero 2/Zero 2 W, Raspberry Pi 2B 1.2 – jeweils mit 64-Bit-Abbild |
| `…_aarch64_cortex-a72.apk` | Broadcom BCM2711 (`bcm27xx/bcm2711`) | Raspberry Pi 4B, 400, CM4 – mit 64-Bit-Abbild |
| | Marvell Armada 7k/8k, CN913x (`mvebu/cortexa72`) | SolidRun MACCHIATObin, SolidRun ClearFog GT-8K |
| `…_aarch64_cortex-a76.apk` | Broadcom BCM2712 (`bcm27xx/bcm2712`) | Raspberry Pi 5, 500, CM5 |
| `…_arm_cortex-a7_neon-vfpv4.apk` | Broadcom BCM2836/2837/2711, 32 Bit (`bcm27xx/bcm2709`) | Raspberry Pi 2B, Raspberry Pi 3/Zero 2 und Raspberry Pi 4B/400/CM4 – jeweils mit 32-Bit-Abbild |
| | Qualcomm IPQ40xx (`ipq40xx/generic`) | AVM FRITZ!Box 7530, AVM FRITZ!Box 4040, GL.iNet GL-B1300, Linksys EA6350 v3, ASUS RT-AC58U, Zyxel NBG6617 |
| | MediaTek MT7623 (`mediatek/mt7623`) | Bananapi BPi-R2, UniElec U7623-02 |
| | Allwinner A20, H3 u. a. (`sunxi/cortexa7`) | LeMaker Banana Pi, Bananapi BPi-R1, Xunlong Orange Pi PC, Xunlong Orange Pi One |

Beim Raspberry Pi entscheidet das installierte OpenWrt-Abbild: Mit dem 32-Bit-Abbild braucht auch ein Pi 3 oder Pi 4
die Datei `…_arm_cortex-a7_neon-vfpv4.apk`.

Voraussetzung ist OpenWrt 25.12.x; ältere Versionen nutzen ein anderes Paketformat. Für weitere Chipsätze kann der
Scanner auf Anfrage gebaut werden – OpenWrt kennt 35 Paketarchitekturen.
