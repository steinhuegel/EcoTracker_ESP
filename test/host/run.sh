#!/usr/bin/env bash
# Builds and runs the host unit tests for the hardware independent core.
# Usage: ARDUINOJSON_DIR=/path/to/ArduinoJson ./test/host/run.sh
set -euo pipefail
cd "$(dirname "$0")"
AJ="${ARDUINOJSON_DIR:-../../../ArduinoJson}"
g++ -std=c++17 -Wall -Wextra -O1 -I"$AJ/src" test_core.cpp -o /tmp/ecotracker_test_core
/tmp/ecotracker_test_core
