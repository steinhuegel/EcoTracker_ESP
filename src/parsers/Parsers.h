#ifndef PARSERS_H
#define PARSERS_H

#include <Arduino.h>

void queryHTTP();
void mqtt_callback(char *topic, byte *payload, unsigned int length);
void mqtt_reconnect();
void parseSHRDZM();
void parseSMA();
void parseSUNSPEC();
void parseTibberPulse();
void parseRCT();
void setupSMLSerial();
void parseSMLSerial();
void queryIoBroker();
// false until the first successful ioBroker query and again if the ioBroker has
// not delivered valid data for a longer time (used to stop serving stale values)
bool ioBrokerDataFresh();

#endif // PARSERS_H