#include "EcoTracker.h"

#include "../config/Configuration.h"
#include "../data/DataStructures.h"
#include "../data/EcoTrackerCore.h"
#include "../parsers/Parsers.h"

static ecocore::PowerAverager averager;

static double totalPower()
{
  return PhasePower[0].power + PhasePower[1].power + PhasePower[2].power;
}

// Reads a text setting; also copes with values that were stored as a number
static String cfgString(JsonVariantConst v, const char *def)
{
  if (v.isNull())
  {
    return String(def);
  }
  String s = v.as<String>();
  s.trim();
  return s.length() > 0 ? s : String(def);
}

void ecoTrackerConfigure()
{
  averager.setWindow(config_doc["ecotracker"]["avg_interval"].as<uint32_t>());
}

void ecoTrackerLoop(unsigned long nowMs)
{
  averager.tick((uint32_t)nowMs, (float)totalPower());
}

bool ecoTrackerBuildJson(char *buf, size_t cap)
{
  // Do not hand stale values to the storage if the ioBroker is not reachable
  if (dataIOBROKER && !ioBrokerDataFresh())
  {
    return false;
  }

  ecocore::EcoReading r;
  r.power = totalPower();
  if (!averager.average(r.powerAvg))
  {
    r.powerAvg = r.power;  // first second after boot
  }
  for (int i = 0; i < 3; i++)
  {
    r.phase[i] = PhasePower[i].power;
  }
  String suppress = config_doc["ecotracker"]["suppress_phases"] | "false";
  r.withPhases = !suppress.equalsIgnoreCase("true");

  // internal counters are in Wh, the EcoTracker reports kWh
  r.energyInKWh = (PhaseEnergy[0].consumption + PhaseEnergy[1].consumption + PhaseEnergy[2].consumption) / 1000.0;
  r.energyOutKWh = (PhaseEnergy[0].gridfeedin + PhaseEnergy[1].gridfeedin + PhaseEnergy[2].gridfeedin) / 1000.0;

  return ecocore::buildEcoTrackerJson(r, buf, cap) > 0;
}

static void handleV1Json(AsyncWebServerRequest *request)
{
  char buf[320];
  if (ecoTrackerBuildJson(buf, sizeof(buf)))
  {
    request->send(200, "application/json", buf);
  }
  else
  {
    request->send(503, "text/plain", "no data available");
  }
}

void ecoTrackerRegisterRoutes()
{
  server.on("/v1/json", AsyncWebRequestMethod::HTTP_GET, handleV1Json);
}

String ecoTrackerHostname()
{
  String host = cfgString(config_doc["ecotracker"]["hostname"], "");
  if (host.length() == 0)
  {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();
    host = "ecotracker-" + mac;
  }
  return host;
}

void ecoTrackerSetupMdns()
{
  String host = ecoTrackerHostname();
  String ip = WiFi.localIP().toString();
  String serial = cfgString(config_doc["ecotracker"]["serial"], "293d45273261");
  String productid = cfgString(config_doc["ecotracker"]["productid"], "1137");

  if (!MDNS.begin(host.c_str()))
  {
    DEBUG_SERIAL.println(F("Error setting up MDNS responder!"));
    return;
  }

#ifdef ESP32
  MDNS.addService("http", "tcp", 80);
  MDNS.addService("everhome", "tcp", 80);
  MDNS.addServiceTxt("everhome", "tcp", "ip", ip.c_str());
  MDNS.addServiceTxt("everhome", "tcp", "serial", serial.c_str());
  MDNS.addServiceTxt("everhome", "tcp", "productid", productid.c_str());
  mdns_service_instance_name_set("_everhome", "_tcp", host.c_str());
#else
  hMDNSService = MDNS.addService(0, "http", "tcp", 80);
  hMDNSService2 = MDNS.addService(0, "everhome", "tcp", 80);
  if (hMDNSService)
  {
    MDNS.setServiceName(hMDNSService, host.c_str());
  }
  if (hMDNSService2)
  {
    MDNS.setServiceName(hMDNSService2, host.c_str());
    MDNS.addServiceTxt(hMDNSService2, "ip", ip.c_str());
    MDNS.addServiceTxt(hMDNSService2, "serial", serial.c_str());
    MDNS.addServiceTxt(hMDNSService2, "productid", productid.c_str());
  }
#endif

  DEBUG_SERIAL.print(F("mDNS responder started: "));
  DEBUG_SERIAL.print(host);
  DEBUG_SERIAL.println(F(".local (_everhome._tcp)"));
}
