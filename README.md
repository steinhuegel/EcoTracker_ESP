# Energy2EcoTracker

Ein ESP8266/ESP32 gibt sich als **Everhome EcoTracker** aus und liefert Zählerdaten an Speicher und Wechselrichter, die
einen EcoTracker abfragen (Nulleinspeisung). Die Messwerte holt er aus dem **ioBroker**.

Das Projekt ist ein Fork von [Energy2Shelly_ESP](https://github.com/TheRealMoeder/Energy2Shelly_ESP) (dort emuliert der ESP einen
Shelly Pro 3EM). Das EcoTracker-Protokoll ist nach [uni-meter](https://github.com/sdeigm/uni-meter) umgesetzt.
Lizenz: Apache-2.0, wie beide Originale.

> **Status:** Der Code wird per GitHub Actions für ESP8266, ESP32, ESP32-S3 und ESP32-C3 gebaut. Ein ausgiebiger Test im
> Dauerbetrieb an einem Speicher steht noch aus.

## Was es kann

| | |
|---|---|
| **Ausgang `ECOTRACKER`** (Standard) | `GET http://<IP>/v1/json` auf Port 80 und mDNS-Service `_everhome._tcp` |
| **Ausgang `SHELLY`** (optional) | Shelly-Pro-3EM-Emulation wie im Originalprojekt |
| **Eingang `IOBROKER`** (Standard) | Datenpunkte per ioBroker-Adapter *simple-api* (`getBulk`) |
| weitere Eingänge | MQTT, HTTP, SMA, SHRDZM, SUNSPEC, Tibber Pulse, SML seriell, siehe [Original-README](docs/README_Energy2Shelly_ESP_original.md) |

Unterstützte Chips: ESP8266, ESP32, ESP32-S3, ESP32-C3.

## 1. Firmware besorgen

Jeder Push auf `main` startet einen Build. Die fertigen Dateien stehen danach auf der Seite **Releases** (rechts auf der
Startseite des Repos, Eintrag „Neueste Firmware“) unter **Assets**. Dort die passende Datei anklicken, sie wird heruntergeladen:


| Chip | Erstes Flashen (per USB) | Update über das Web-Menü |
|---|---|---|
| ESP8266 | `esp8266.bin` | `esp8266.bin` |
| ESP32 | `esp32.bin` | `esp32-ota.bin` |
| ESP32-S3 (4 MB) | `esp32-s3.bin` | `esp32-s3-ota.bin` |
| ESP32-S3 (8 MB) | `esp32-s3-N8.bin` | `esp32-s3-N8-ota.bin` |
| ESP32-C3 | `esp32-c3.bin` | `esp32-c3-ota.bin` |

Der Build dauert etwa 6 Minuten. Solange der Lauf unter **Actions** noch läuft, ist die Datei noch nicht aktuell.
Alternativ hängt die Firmware auch als Paket **firmware** unten auf der Übersichtsseite (Summary) des jeweiligen Laufs.

Alternativ selbst bauen mit [PlatformIO](https://platformio.org/): `pio run -e esp8266` (bzw. `esp32`, `esp32-s3`, `esp32-c3`).

## 2. Flashen (erstes Mal)

ESP per USB anschließen und mit [esptool](https://github.com/espressif/esptool) auf Adresse `0x0` schreiben
(alle Dateien aus der Spalte „Erstes Flashen" sind komplett und werden an `0x0` geschrieben):

```
python -m esptool --port <COMx oder /dev/ttyUSBx> write_flash 0x0 esp8266.bin
```

Bei Verbindungsproblemen den ESP in den Flash-Modus versetzen (GPIO0 beim Reset auf Masse; bei ESP32-S3/C3 BOOT gedrückt
halten und einmal RESET drücken). Beim ESP8266 sollte vorher der Flash gelöscht werden: `python -m esptool erase_flash`.

## 3. Einrichten

1. ESP einschalten und auf den WLAN-Hotspot **Energy2EcoTracker** warten, verbinden.
2. Im Portal (oder unter `http://192.168.4.1/`) das eigene WLAN eintragen.
3. Die IP-Adresse des ESP im Router nachsehen und `http://<IP>/config` öffnen.
   (Ist ein Reset-Passwort gesetzt, fragt der ESP danach: Benutzername leer lassen.)
4. Einstellungen prüfen bzw. anpassen, **speichern** und den ESP neu starten (Strom aus/an oder Reset-Taste).
   Der mDNS-Name wird nur beim Start gesetzt.
5. Prüfen: `http://<IP>/v1/json` liefert die Messwerte. Meldungen stehen unter `http://<IP>/console`.

Die Standardwerte (siehe unten) gelten nach einem frischen Flash bzw. Zurücksetzen. Danach gespeicherte Änderungen bleiben im
Flash erhalten.

### Späteres Reconfigurieren und Updates

Nach der Einrichtung öffnet der ESP keinen Hotspot mehr, er ist nur im Heimnetz erreichbar.

1. `http://<IP>/update` öffnen: der ESP startet in den Konfigurationsmodus neu (ca. 20 s).
2. Dort die Datei aus der Spalte „Update über das Web-Menü" hochladen oder die Parameter ändern.

Tipp: Zeigt der Browser danach einen Fehler, die Seite neu laden. Das WLAN zurücksetzen geht über `http://<IP>/reset`.

## Eingang: ioBroker

Im ioBroker den Adapter **simple-api** installieren (Standard-Port 8087). Test im Browser, die IDs anpassen:

```
http://<ioBroker-IP>:8087/getBulk/<ID1>,<ID2>/?json
```

Erwartet wird ein Array wie `[{"id":"...","val":123.4,...}, ...]`. Der ESP stellt pro Abfrageintervall genau eine Anfrage.

| Feld | Bedeutung |
|---|---|
| Host / Port | ioBroker-IP und simple-api-Port |
| Total Power | Gesamtleistung in W, **positiv = Bezug, negativ = Einspeisung** |
| Power L1 / L2 / L3 | optional. Sind alle drei gesetzt, werden sie statt der Gesamtleistung verwendet |
| Energy Import / Export | optional: Zähler Bezug / Einspeisung. Es müssen beide gesetzt sein |
| Energy Factor | 1, wenn die Datenpunkte in Wh vorliegen, **1000**, wenn in kWh |
| Query Period | Abfrageintervall in ms (mindestens 1000) |

Das **Power Offset** und die **Phase Number** aus den Basic Settings gelten auch hier (`3` verteilt auf drei Phasen, `1` legt alles
auf Phase 1).

## Ausgang: EcoTracker

`GET /v1/json` liefert:

```json
{"power":124,"powerAvg":-45,"powerPhase1":41.2,"powerPhase2":41.2,"powerPhase3":41.2,
 "energyCounterIn":13849.623,"energyCounterOut":717.875}
```

* `power` und `powerAvg` in W. `powerAvg` ist ein gleitender Mittelwert (Standard 60 s, einstellbar 1–120 s).
* Die Energiezähler sind in **kWh**.
* mDNS: Hostname `ecotracker-<MAC>.local`, Service `_everhome._tcp` auf Port 80, TXT-Einträge `ip`, `serial`, `productid`.
* Sind die Daten veraltet (ioBroker seit mehr als 30 s bzw. drei Abfrageintervallen nicht erreichbar oder seit dem Start noch nie
  erfolgreich abgefragt), antwortet `/v1/json` mit **HTTP 503**, damit der Speicher nicht mit alten Werten regelt.
* Der Port ist fest 80.
* mDNS prüfen: `avahi-browse -r _everhome._tcp` (Linux) oder `dns-sd -B _everhome._tcp` (macOS).

Weitere Seiten: `http://<IP>/` (Übersicht), `/plot` (Verlauf), `/console` (Log), `/config`, `/status`.
Mit **Output Type = SHELLY** verhält sich das Gerät wie im Originalprojekt (Shelly Pro 3EM, `/rpc/...`, mDNS `_shelly._tcp`).

## Voreingestellte Werte

Hinterlegt in `DEFAULT_CONFIG_JSON` (`src/config/Configuration.cpp`), übernommen aus einer uni-meter-Konfiguration:

| Einstellung | Wert |
|---|---|
| Output / Input | `ECOTRACKER` / `IOBROKER`, Phase Number 3 |
| ioBroker | `192.168.178.41:8087` |
| Leistung L1 / L2 / L3 | `homee.0.EnergyMeter-38.CurrentEnergyUse-282` / `-286` / `-290` |
| Energie Bezug / Einspeisung | `mqtt.0.Zaehlerstand` / `mqtt.0.PVGesamt` |
| Energy Factor | 1000 (Datenpunkte in kWh) |
| Abfrageintervall | 2000 ms |
| EcoTracker | `serial` 293d45273262, `productid` 1137 |

Zugangsdaten (WLAN) stehen bewusst nicht im Code. Wer das Projekt für ein anderes Netz nutzt, ändert diese Werte in `/config`
oder im Quellcode.

## Entwicklung und Tests

Die hardwareunabhängige Logik (Mittelwert, EcoTracker-JSON, ioBroker-Parser, URL-Bau) liegt in `src/data/EcoTrackerCore.h` und
wird auf dem PC getestet:

```
git clone --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson.git
ARDUINOJSON_DIR=$PWD/ArduinoJson ./test/host/run.sh
```

Neu gegenüber Energy2Shelly_ESP: `src/data/EcoTrackerCore.h`, `src/output/EcoTracker.{h,cpp}`, `src/parsers/IoBrokerParser.cpp`,
`test/host/*`. Geändert: `src/config/Configuration.{h,cpp}`, `src/main.cpp`, `src/parsers/Parsers.h`,
`src/data/DataProcessing.cpp` (Phasen- und Energieaufteilung ohne Rundungsverlust), `src/web/html_home.h`.

## Danksagung

* [TheRealMoeder/Energy2Shelly_ESP](https://github.com/TheRealMoeder/Energy2Shelly_ESP): die Firmware-Grundlage
* [sdeigm/uni-meter](https://github.com/sdeigm/uni-meter): Vorlage für das EcoTracker-Protokoll und die ioBroker-Anbindung
