#ifndef ECOTRACKER_H
#define ECOTRACKER_H

#include <Arduino.h>

// Everhome EcoTracker emulation (HTTP GET /v1/json + mDNS service _everhome._tcp)

// Apply the configuration (averaging window); called from interprete_parameters()
void ecoTrackerConfigure();

// Register GET /v1/json on the web server
void ecoTrackerRegisterRoutes();

// Feed the moving average, call from the main loop (cheap, samples once per second)
void ecoTrackerLoop(unsigned long nowMs);

// Fills buf with the EcoTracker JSON. Returns false if no valid data is available
// (the HTTP handler then answers 503 so the consumer does not act on stale data).
bool ecoTrackerBuildJson(char *buf, size_t cap);

// Hostname used for mDNS: "ecotracker-<MAC>" unless overridden in the configuration
String ecoTrackerHostname();

// Start the mDNS responder announcing the EcoTracker service
void ecoTrackerSetupMdns();

#endif  // ECOTRACKER_H
