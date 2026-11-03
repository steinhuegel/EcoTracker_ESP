#include "../config/Configuration.h"
#include "../data/DataProcessing.h"
#include "../data/EcoTrackerCore.h"
#include "Parsers.h"

// ioBroker input via the simple-API adapter (https://github.com/ioBroker/ioBroker.simple-api):
// one request  GET http://<host>:<port>/getBulk/<id>,<id>,.../?json  per query period.

static unsigned long lastOkMs = 0;
static bool everOk = false;

bool ioBrokerDataFresh()
{
  if (!everOk)
  {
    return false;
  }
  uint32_t period = config_doc["iobroker"]["query_period"].as<uint32_t>();
  if (period == 0)
  {
    period = 5000;
  }
  uint32_t maxAge = period * 3;
  if (maxAge < 30000)
  {
    maxAge = 30000;
  }
  return (uint32_t)(millis() - lastOkMs) <= maxAge;
}

void queryIoBroker()
{
  const char *host = config_doc["iobroker"]["server"] | "";
  uint16_t port = config_doc["iobroker"]["port"].as<uint16_t>();
  if (port == 0)
  {
    port = 8087;
  }

  ecocore::IoBrokerIds ids;
  ids.power = config_doc["iobroker"]["power_id"] | "";
  ids.l1 = config_doc["iobroker"]["power_l1_id"] | "";
  ids.l2 = config_doc["iobroker"]["power_l2_id"] | "";
  ids.l3 = config_doc["iobroker"]["power_l3_id"] | "";
  ids.energyIn = config_doc["iobroker"]["energy_in_id"] | "";
  ids.energyOut = config_doc["iobroker"]["energy_out_id"] | "";

  char *url = (char *)malloc(512);
  if (url == nullptr)
  {
    DEBUG_SERIAL.println(F("ioBroker: out of memory"));
    return;
  }
  if (ecocore::buildIoBrokerUrl(url, 512, host, port, ids) == 0)
  {
    DEBUG_SERIAL.println(F("ioBroker: set server and at least one power datapoint ID"));
    free(url);
    return;
  }

  DEBUG_SERIAL.println(F("Querying ioBroker"));
  http.useHTTP10(true);  // no chunked transfer encoding, the body can be parsed from the stream
  http.setTimeout(3000);
  http.begin(wifi_client, url);
  int code = http.GET();

  bool ok = false;
  if (code == HTTP_CODE_OK)
  {
    ecocore::IoBrokerValues v;
    if (ecocore::parseIoBrokerBulk(http.getStream(), ids, v) && ecocore::ioBrokerPowerComplete(ids, v))
    {
      if (ecocore::usePhases(ids))
      {
        setPowerData(v.l1, v.l2, v.l3);
      }
      else
      {
        setPowerData(v.power);
      }
      if (ecocore::ioBrokerEnergyComplete(ids, v))
      {
        double factor = config_doc["iobroker"]["energy_factor"].as<double>();
        if (factor == 0.0)
        {
          factor = 1.0;
        }
        setEnergyData(v.in * factor, v.out * factor);
      }
      ok = true;
    }
    else
    {
      DEBUG_SERIAL.println(F("ioBroker: answer invalid or a configured datapoint is missing"));
    }
  }
  else
  {
    DEBUG_SERIAL.print(F("ioBroker: HTTP error "));
    DEBUG_SERIAL.println(code);
  }

  http.end();
  free(url);

  if (ok)
  {
    lastOkMs = millis();
    everOk = true;
  }
}
