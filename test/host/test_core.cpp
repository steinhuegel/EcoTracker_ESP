// Host unit tests for src/data/EcoTrackerCore.h
// Build & run:  ./test/host/run.sh   (needs g++ and ArduinoJson 7 checked out)

#include <math.h>
#include <stdio.h>
#include <string>

#include "../../src/data/EcoTrackerCore.h"

using namespace ecocore;

static int failures = 0;
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);             \
      failures++;                                                        \
    }                                                                    \
  } while (0)
#define CHECK_NEAR(a, b) CHECK(fabs((a) - (b)) < 1e-6)

static void test_averager() {
  PowerAverager a;
  a.setWindow(3);
  double avg;
  CHECK(!a.average(avg));

  a.tick(1000, 100);
  a.tick(1500, 999);  // < 1 s since last sample -> ignored
  a.tick(2000, 200);
  a.tick(3000, 300);
  CHECK(a.count() == 3);
  CHECK(a.average(avg));
  CHECK_NEAR(avg, 200.0);

  a.tick(4000, 400);  // oldest (100) drops out of the 3s window
  CHECK(a.average(avg));
  CHECK_NEAR(avg, 300.0);

  // millis() wrap around must not stall sampling
  PowerAverager w;
  w.setWindow(2);
  w.tick(0xFFFFFC00u, 10);
  w.tick(0x00000400u, 30);  // 2048 ms later across the wrap
  CHECK(w.count() == 2);

  // window clamp and ring buffer wrap
  PowerAverager c;
  c.setWindow(100000);
  CHECK(c.window() == MAX_AVG_SAMPLES);
  for (uint32_t i = 0; i < 300; i++) c.tick(i * 1000u, (float)i);
  CHECK(c.count() == MAX_AVG_SAMPLES);
  CHECK(c.average(avg));
  CHECK_NEAR(avg, (299.0 + 180.0) / 2.0);  // newest 120 samples: 180..299

  c.setWindow(0);  // 0 -> default 60
  CHECK(c.window() == 60);
}

static void test_eco_json() {
  EcoReading r;
  r.power = 123.6;
  r.powerAvg = -45.4;
  r.phase[0] = 41.2;
  r.phase[1] = 41.2;
  r.phase[2] = 41.2;
  r.energyInKWh = 13849.6234;
  r.energyOutKWh = 717.8749;

  char buf[320];
  size_t n = buildEcoTrackerJson(r, buf, sizeof(buf));
  CHECK(n > 0);
  printf("eco json: %s\n", buf);

  JsonDocument d;
  CHECK(!deserializeJson(d, buf));
  CHECK(d["power"].as<long>() == 124);
  CHECK(d["powerAvg"].as<long>() == -45);
  CHECK_NEAR(d["powerPhase1"].as<double>(), 41.2);
  CHECK_NEAR(d["energyCounterIn"].as<double>(), 13849.623);
  CHECK_NEAR(d["energyCounterOut"].as<double>(), 717.875);

  r.withPhases = false;
  n = buildEcoTrackerJson(r, buf, sizeof(buf));
  CHECK(n > 0);
  JsonDocument d2;
  CHECK(!deserializeJson(d2, buf));
  CHECK(d2["powerPhase1"].isNull());
  CHECK(d2["power"].as<long>() == 124);

  char tiny[16];
  CHECK(buildEcoTrackerJson(r, tiny, sizeof(tiny)) == 0);  // too small -> 0
}

static void test_iobroker_parse() {
  // Sample taken from the uni-meter ioBroker documentation
  const char *answer =
      "[{\"id\":\"smartmeter.0.1-0:1_8_0__255.value\",\"val\":16464.7379,\"ts\":1740054549023,\"ack\":true},"
      "{\"id\":\"smartmeter.0.1-0:2_8_0__255.value\",\"val\":16808.0592,\"ts\":1740054549029,\"ack\":true},"
      "{\"id\":\"smartmeter.0.1-0:16_7_0__255.value\",\"val\":-4.9,\"ts\":1740054549072,\"ack\":true}]";

  IoBrokerIds ids;
  ids.power = "smartmeter.0.1-0:16_7_0__255.value";
  ids.energyIn = "smartmeter.0.1-0:1_8_0__255.value";
  ids.energyOut = "smartmeter.0.1-0:2_8_0__255.value";

  IoBrokerValues v;
  CHECK(parseIoBrokerBulk(answer, ids, v));
  CHECK(v.hasPower && v.hasIn && v.hasOut);
  CHECK(!v.hasL1);
  CHECK_NEAR(v.power, -4.9);
  CHECK_NEAR(v.in, 16464.7379);
  CHECK_NEAR(v.out, 16808.0592);
  CHECK(ioBrokerPowerComplete(ids, v));
  CHECK(ioBrokerEnergyComplete(ids, v));

  // order of the answer must not matter, unknown ids are ignored
  const char *shuffled =
      "[{\"id\":\"other.0.x\",\"val\":1},"
      "{\"id\":\"smartmeter.0.1-0:16_7_0__255.value\",\"val\":\"230.5\"}]";
  CHECK(parseIoBrokerBulk(shuffled, ids, v));
  CHECK(v.hasPower && !v.hasIn && !v.hasOut);
  CHECK_NEAR(v.power, 230.5);  // numeric string accepted
  CHECK(ioBrokerPowerComplete(ids, v));
  CHECK(!ioBrokerEnergyComplete(ids, v));

  // missing datapoint / error entry -> power not complete
  const char *missing =
      "[{\"error\":\"datapoint \\\"x\\\" not found\"},"
      "{\"id\":\"smartmeter.0.1-0:16_7_0__255.value\",\"val\":null}]";
  CHECK(parseIoBrokerBulk(missing, ids, v));
  CHECK(!v.hasPower);
  CHECK(!ioBrokerPowerComplete(ids, v));

  // not an array / broken JSON
  CHECK(!parseIoBrokerBulk("{\"error\":\"unauthorized\"}", ids, v));
  CHECK(!parseIoBrokerBulk("<html>nope</html>", ids, v));
  CHECK(!parseIoBrokerBulk("[{\"id\":\"smartmeter", ids, v));

  // three phase mode takes precedence over the total
  IoBrokerIds p;
  p.power = "pm.0.total";
  p.l1 = "pm.0.l1";
  p.l2 = "pm.0.l2";
  p.l3 = "pm.0.l3";
  const char *ph =
      "[{\"id\":\"pm.0.total\",\"val\":600},{\"id\":\"pm.0.l1\",\"val\":100},"
      "{\"id\":\"pm.0.l2\",\"val\":200},{\"id\":\"pm.0.l3\",\"val\":300}]";
  CHECK(parseIoBrokerBulk(ph, p, v));
  CHECK(usePhases(p));
  CHECK(ioBrokerPowerComplete(p, v));
  CHECK_NEAR(v.l1 + v.l2 + v.l3, 600.0);
  const char *ph2 = "[{\"id\":\"pm.0.l1\",\"val\":100},{\"id\":\"pm.0.l2\",\"val\":200}]";
  CHECK(parseIoBrokerBulk(ph2, p, v));
  CHECK(!ioBrokerPowerComplete(p, v));  // l3 missing

  // nothing configured
  IoBrokerIds none;
  CHECK(!ioBrokerPowerComplete(none, v));
}

static void test_iobroker_url() {
  IoBrokerIds ids;
  ids.power = "smartmeter.0.1-0:16_7_0__255.value";
  ids.energyIn = "smartmeter.0.1-0:1_8_0__255.value";
  ids.energyOut = "smartmeter.0.1-0:2_8_0__255.value";

  char url[512];
  size_t n = buildIoBrokerUrl(url, sizeof(url), "192.168.1.10", 8087, ids);
  CHECK(n > 0);
  printf("url: %s\n", url);
  CHECK(std::string(url) ==
        "http://192.168.1.10:8087/getBulk/smartmeter.0.1-0:16_7_0__255.value,"
        "smartmeter.0.1-0:1_8_0__255.value,smartmeter.0.1-0:2_8_0__255.value/?json");

  // scheme / trailing slash in the host field are tolerated
  n = buildIoBrokerUrl(url, sizeof(url), "http://iobroker.local/", 8082, ids);
  CHECK(n > 0);
  CHECK(std::string(url).rfind("http://iobroker.local:8082/getBulk/", 0) == 0);

  // special characters are percent-encoded
  IoBrokerIds sp;
  sp.power = "javascript.0.My Power#1";
  n = buildIoBrokerUrl(url, sizeof(url), "h", 1, sp);
  CHECK(n > 0);
  CHECK(std::string(url) == "http://h:1/getBulk/javascript.0.My%20Power%231/?json");

  // errors
  IoBrokerIds none;
  CHECK(buildIoBrokerUrl(url, sizeof(url), "h", 1, none) == 0);
  CHECK(buildIoBrokerUrl(url, sizeof(url), "", 1, ids) == 0);
  char tiny[32];
  CHECK(buildIoBrokerUrl(tiny, sizeof(tiny), "192.168.1.10", 8087, ids) == 0);
}

int main() {
  test_averager();
  test_eco_json();
  test_iobroker_parse();
  test_iobroker_url();
  if (failures == 0) printf("ALL TESTS PASSED\n");
  else printf("%d FAILURE(S)\n", failures);
  return failures ? 1 : 0;
}
