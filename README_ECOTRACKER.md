# Energy2EcoTracker: EcoTracker-Ausgang und ioBroker-Eingang für ESP8266/ESP32

Erweiterung von [Energy2Shelly_ESP](https://github.com/TheRealMoeder/Energy2Shelly_ESP) (läuft auf ESP8266 und ESP32).
Das Protokoll des EcoTrackers ist aus [uni-meter](https://github.com/sdeigm/uni-meter) übernommen
(`output/device/eco_tracker`), Lizenz wie das Original: Apache-2.0.

## Was neu ist

| | |
|---|---|
| **Output Type** `ECOTRACKER` | Emuliert einen Everhome EcoTracker: `GET /v1/json` und mDNS-Service `_everhome._tcp`. Standard bleibt `SHELLY` (unverändert). |
| **Input Type** `IOBROKER` | Liest Datenpunkte per ioBroker-Adapter *simple-API* (`getBulk`). |
| Exakte Summen | Die Aufteilung auf 3 Phasen verliert keine 0,01 % mehr (vorher 3 × 0,3333), wichtig für die Summe der Energiezähler. |

## Einrichtung

1. Firmware flashen (ESP8266: eine einzelne `.bin` ab Adresse 0, es gibt dort keine Partitionstabelle) und wie gewohnt
   per Hotspot „Energy2EcoTracker“ ins WLAN bringen.
2. `http://<IP>/config` öffnen:
   * **Basic Settings → Output Type** = `ECOTRACKER`
   * **Basic Settings → Input Type** = `IOBROKER`
   * **ioBroker Settings**: Host/IP, simple-API-Port (Standard 8087) und die Datenpunkt-IDs
   * Speichern und den ESP neu starten (der mDNS-Name wird nur beim Start gesetzt).
3. Prüfen: `http://<IP>/v1/json`

### ioBroker-Voraussetzung

Adapter **simple-api** installieren. Test im Browser (IDs anpassen):

```
http://<iobroker>:8087/getBulk/smartmeter.0.1-0:16_7_0__255.value,smartmeter.0.1-0:1_8_0__255.value/?json
```

Erwartet wird ein Array wie `[{"id":"...","val":123.4,...}, ...]`. Beim Eingabetyp `IOBROKER` wird genau eine Anfrage pro
Abfrageintervall (Standard 5 s) gestellt.

| Feld | Bedeutung |
|---|---|
| Total Power Datapoint | Gesamtleistung in W, **positiv = Bezug, negativ = Einspeisung** |
| Power L1/L2/L3 (optional) | Sind alle drei gesetzt, werden sie anstelle der Gesamtleistung verwendet |
| Energy Import / Export (optional) | Zähler Bezug / Einspeisung. Beide müssen gesetzt sein. |
| Energy Factor | 1, wenn die Werte in Wh vorliegen; 1000, wenn in kWh |

Das globale **Power Offset** und die **Phase Number** aus den Basic Settings gelten auch hier.

## EcoTracker-Ausgabe

`GET /v1/json` liefert, wie in uni-meter:

```json
{"power":124,"powerAvg":-45,"powerPhase1":41.2,"powerPhase2":41.2,"powerPhase3":41.2,
 "energyCounterIn":13849.623,"energyCounterOut":717.875}
```

* `powerAvg`: gleitender Mittelwert über 60 s (einstellbar 1–120 s, ein Messpunkt pro Sekunde).
* Energiezähler in **kWh** (intern Wh).
* mDNS: Hostname `ecotracker-<MAC>.local`, Service `_everhome._tcp` auf Port 80, TXT `ip`, `serial`, `productid`
  (Standardwerte wie in uni-meter, in den Einstellungen änderbar).
* Wenn die Daten nicht mehr aktuell sind (ioBroker seit mehr als 30 s bzw. 3 Abfrageintervallen, je nachdem was größer ist,
  nicht erfolgreich abgefragt, oder noch kein erfolgreicher Abruf seit dem Start), antwortet `/v1/json` mit **HTTP 503**,
  damit der Speicher nicht mit alten Werten regelt.
  Das gilt nur für den Eingang `IOBROKER`; bei den anderen Eingängen verhält sich die Firmware wie bisher.
* Der Port ist fest 80 (manche Speicher, z. B. Growatt NOAH/NEXA und Hoymiles, erwarten das ohnehin).
* Die Shelly-Endpunkte bleiben zusätzlich erreichbar, `/v1/json` ist auch im Shelly-Modus abrufbar (nur ohne mDNS-Ankündigung).

mDNS prüfen: `avahi-browse -r _everhome._tcp` (Linux) oder `dns-sd -B _everhome._tcp` (macOS).

## Tests

Die hardwareunabhängige Logik (Mittelwert, JSON, ioBroker-Parser, URL-Bau) liegt in `src/data/EcoTrackerCore.h` und wird auf
dem PC getestet:

```
git clone --depth 1 --branch v7.4.2 https://github.com/bblanchon/ArduinoJson.git
ARDUINOJSON_DIR=$PWD/ArduinoJson ./test/host/run.sh
```

## Geänderte / neue Dateien

Neu: `src/data/EcoTrackerCore.h`, `src/output/EcoTracker.{h,cpp}`, `src/parsers/IoBrokerParser.cpp`, `test/host/*`.
Geändert: `src/config/Configuration.{h,cpp}`, `src/main.cpp`, `src/parsers/Parsers.h`, `src/data/DataProcessing.cpp`,
`src/web/html_home.h`.

## Voreingestellte Werte (aus der uni-meter.conf)

Nach einem frischen Flash bzw. Reset der Einstellungen gilt ohne weitere Eingabe (`DEFAULT_CONFIG_JSON` in `src/config/Configuration.cpp`):

| Einstellung | Wert |
|---|---|
| Output / Input | `ECOTRACKER` / `IOBROKER`, Phase Number 3 |
| ioBroker | `192.168.178.41:8087` |
| Leistung L1 / L2 / L3 | `homee.0.EnergyMeter-38.CurrentEnergyUse-282` / `-286` / `-290` |
| Energie Bezug / Einspeisung | `mqtt.0.Zaehlerstand` / `mqtt.0.PVGesamt` |
| Energy Factor | 1000 (die Datenpunkte werden wie in uni-meter als kWh behandelt) |
| Abfrageintervall | 2000 ms |
| EcoTracker | Port 80, `serial` 293d45273261, `productid` 1137 |

Nur das WLAN muss beim ersten Start über den Hotspot „Energy2EcoTracker“ eingetragen werden (Passwörter stehen bewusst nicht im Code).
Der Port 4711 und der uni-meter-HTTP-Port 8080 aus der Konfiguration werden nicht übernommen, der ESP nutzt Port 80.
