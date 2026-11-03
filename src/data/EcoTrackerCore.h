#ifndef ECOTRACKER_CORE_H
#define ECOTRACKER_CORE_H

// Hardware independent logic for the Everhome EcoTracker emulation and the
// ioBroker (simple-API) input. This header only depends on ArduinoJson and the
// C standard library so it can be unit tested on a PC (see test/host).

#include <ArduinoJson.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ecocore {

// ============================================================================
// Moving average of the total power (EcoTracker "powerAvg")
// ============================================================================

// One sample per second, the window can be 1..MAX_AVG_SAMPLES seconds.
constexpr size_t MAX_AVG_SAMPLES = 120;

class PowerAverager {
 public:
  void setWindow(uint32_t seconds) {
    if (seconds < 1) seconds = 60;  // unset / 0 -> EcoTracker default of 60s
    if (seconds > MAX_AVG_SAMPLES) seconds = MAX_AVG_SAMPLES;
    window_ = seconds;
  }

  // Call as often as you like with the current millis(); at most one sample
  // per second is stored.
  void tick(uint32_t nowMs, float power) {
    if (started_ && (uint32_t)(nowMs - lastMs_) < 1000u) return;
    started_ = true;
    lastMs_ = nowMs;
    samples_[head_] = power;
    head_ = (head_ + 1) % MAX_AVG_SAMPLES;
    if (count_ < MAX_AVG_SAMPLES) count_++;
  }

  // Average over the newest samples within the window. Returns false as long
  // as no sample has been recorded yet.
  bool average(double &out) const {
    size_t n = count_ < window_ ? count_ : window_;
    if (n == 0) return false;
    double sum = 0.0;
    size_t idx = head_;
    for (size_t i = 0; i < n; i++) {
      idx = (idx + MAX_AVG_SAMPLES - 1) % MAX_AVG_SAMPLES;
      sum += samples_[idx];
    }
    out = sum / (double)n;
    return true;
  }

  size_t count() const { return count_; }
  size_t window() const { return window_; }

 private:
  float samples_[MAX_AVG_SAMPLES] = {0};
  size_t head_ = 0;
  size_t count_ = 0;
  size_t window_ = 60;
  uint32_t lastMs_ = 0;
  bool started_ = false;
};

// ============================================================================
// EcoTracker JSON (GET /v1/json)
// ============================================================================

struct EcoReading {
  double power = 0;        // W, positive = grid import, negative = feed-in
  double powerAvg = 0;     // W
  double phase[3] = {0, 0, 0};
  bool withPhases = true;  // emit powerPhase1..3
  double energyInKWh = 0;  // grid import counter
  double energyOutKWh = 0; // grid feed-in counter
};

inline double roundTo(double v, int decimals) {
  double f = 1.0;
  for (int i = 0; i < decimals; i++) f *= 10.0;
  return round(v * f) / f;
}

// Writes the JSON document into buf. Returns the length or 0 if it did not fit.
inline size_t buildEcoTrackerJson(const EcoReading &r, char *buf, size_t cap) {
  JsonDocument doc;
  doc["power"] = (long)lround(r.power);
  doc["powerAvg"] = (long)lround(r.powerAvg);
  if (r.withPhases) {
    doc["powerPhase1"] = roundTo(r.phase[0], 2);
    doc["powerPhase2"] = roundTo(r.phase[1], 2);
    doc["powerPhase3"] = roundTo(r.phase[2], 2);
  }
  doc["energyCounterIn"] = roundTo(r.energyInKWh, 3);
  doc["energyCounterOut"] = roundTo(r.energyOutKWh, 3);
  size_t n = serializeJson(doc, buf, cap);
  return (n == 0 || n >= cap) ? 0 : n;
}

// ============================================================================
// ioBroker simple-API input (GET /getBulk/<id>,<id>/?json)
// ============================================================================

struct IoBrokerIds {
  const char *power = nullptr;      // total power in W
  const char *l1 = nullptr;         // optional per phase power in W
  const char *l2 = nullptr;
  const char *l3 = nullptr;
  const char *energyIn = nullptr;   // optional grid import counter
  const char *energyOut = nullptr;  // optional grid feed-in counter
};

struct IoBrokerValues {
  bool hasPower = false, hasL1 = false, hasL2 = false, hasL3 = false;
  bool hasIn = false, hasOut = false;
  double power = 0, l1 = 0, l2 = 0, l3 = 0, in = 0, out = 0;
};

inline bool hasId(const char *s) { return s != nullptr && *s != '\0'; }

inline bool usePhases(const IoBrokerIds &ids) {
  return hasId(ids.l1) && hasId(ids.l2) && hasId(ids.l3);
}

// Reads a datapoint value which may be a number, a numeric string or a bool.
inline bool readNumber(JsonVariantConst v, double &out) {
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    char *end = nullptr;
    double d = strtod(s, &end);
    if (end == s) return false;
    while (*end == ' ') end++;
    if (*end != '\0') return false;
    out = d;
    return true;
  }
  if (v.is<bool>()) {
    out = v.as<bool>() ? 1.0 : 0.0;
    return true;
  }
  if (v.is<double>()) {  // true for integers and floats
    out = v.as<double>();
    return true;
  }
  return false;
}

// Parses the answer of getBulk: [{"id":"..","val":..,"ts":..,"ack":..}, ...]
// Only id and val are kept while parsing (ArduinoJson filter) to save RAM.
// Returns false if the answer is not a valid JSON array.
template <typename TInput>
bool parseIoBrokerBulk(TInput &&input, const IoBrokerIds &ids, IoBrokerValues &out) {
  out = IoBrokerValues();

  JsonDocument filter;
  filter[0]["id"] = true;
  filter[0]["val"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, input, DeserializationOption::Filter(filter));
  if (err || !doc.is<JsonArray>()) return false;

  for (JsonVariantConst el : doc.as<JsonArrayConst>()) {
    const char *id = el["id"];
    if (!id) continue;  // e.g. {"error":"datapoint ... not found"}
    double v;
    if (!readNumber(el["val"], v)) continue;

    if (hasId(ids.power) && strcmp(id, ids.power) == 0) { out.power = v; out.hasPower = true; }
    if (hasId(ids.l1) && strcmp(id, ids.l1) == 0) { out.l1 = v; out.hasL1 = true; }
    if (hasId(ids.l2) && strcmp(id, ids.l2) == 0) { out.l2 = v; out.hasL2 = true; }
    if (hasId(ids.l3) && strcmp(id, ids.l3) == 0) { out.l3 = v; out.hasL3 = true; }
    if (hasId(ids.energyIn) && strcmp(id, ids.energyIn) == 0) { out.in = v; out.hasIn = true; }
    if (hasId(ids.energyOut) && strcmp(id, ids.energyOut) == 0) { out.out = v; out.hasOut = true; }
  }
  return true;
}

// Is the power information needed for the configured mode complete?
// If L1..L3 are all configured they take precedence over the total.
inline bool ioBrokerPowerComplete(const IoBrokerIds &ids, const IoBrokerValues &v) {
  if (usePhases(ids)) return v.hasL1 && v.hasL2 && v.hasL3;
  if (hasId(ids.power)) return v.hasPower;
  return false;
}

inline bool ioBrokerEnergyComplete(const IoBrokerIds &ids, const IoBrokerValues &v) {
  return hasId(ids.energyIn) && hasId(ids.energyOut) && v.hasIn && v.hasOut;
}

// Appends s percent-encoded (keeps A-Z a-z 0-9 - . _ ~ and ':').
inline bool appendEncoded(char *buf, size_t cap, size_t &pos, const char *s) {
  static const char HEX_DIGITS[] = "0123456789ABCDEF";
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
                 c == '~' || c == ':';
    size_t need = plain ? 1 : 3;
    if (pos + need >= cap) return false;
    if (plain) {
      buf[pos++] = (char)c;
    } else {
      buf[pos++] = '%';
      buf[pos++] = HEX_DIGITS[c >> 4];
      buf[pos++] = HEX_DIGITS[c & 0x0F];
    }
  }
  return true;
}

inline bool appendRaw(char *buf, size_t cap, size_t &pos, const char *s) {
  size_t l = strlen(s);
  if (pos + l >= cap) return false;
  memcpy(buf + pos, s, l);
  pos += l;
  return true;
}

// Builds http://<host>:<port>/getBulk/<id1>,<id2>,.../?json
// Returns the length, or 0 if no datapoint is configured / the buffer is too small.
inline size_t buildIoBrokerUrl(char *buf, size_t cap, const char *host, uint16_t port,
                               const IoBrokerIds &ids) {
  if (!buf || cap == 0 || !hasId(host)) return 0;
  if (strncmp(host, "http://", 7) == 0) host += 7;

  char hostPart[96];
  size_t hl = strlen(host);
  while (hl > 0 && host[hl - 1] == '/') hl--;
  if (hl == 0 || hl >= sizeof(hostPart)) return 0;
  memcpy(hostPart, host, hl);
  hostPart[hl] = '\0';

  size_t pos = 0;
  char head[160];
  snprintf(head, sizeof(head), "http://%s:%u/getBulk/", hostPart, (unsigned)port);
  if (!appendRaw(buf, cap, pos, head)) return 0;

  const char *list[] = {ids.power, ids.l1, ids.l2, ids.l3, ids.energyIn, ids.energyOut};
  int count = 0;
  for (const char *id : list) {
    if (!hasId(id)) continue;
    if (count > 0 && !appendRaw(buf, cap, pos, ",")) return 0;
    if (!appendEncoded(buf, cap, pos, id)) return 0;
    count++;
  }
  if (count == 0) return 0;
  if (!appendRaw(buf, cap, pos, "/?json")) return 0;
  buf[pos] = '\0';
  return pos;
}

}  // namespace ecocore

#endif  // ECOTRACKER_CORE_H
