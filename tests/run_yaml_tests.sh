#!/usr/bin/env bash
#
# Host unit tests for the YAML parser (main/config/yaml.c) and the configuration
# document semantics. Runs natively: yaml.c deliberately has no ESP-IDF
# dependency, so it is testable without a device.
#
# Usage: tests/run_yaml_tests.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CJSON="$ROOT/managed_components/espressif__cjson/cJSON"
OUT="$(mktemp -d)"
CFLAGS="-Wall -Wextra -Werror -Wno-unused-parameter -I$ROOT/main/config -I$CJSON"

echo "== yaml: core syntax =="
gcc $CFLAGS "$ROOT/main/config/yaml.c" "$CJSON/cJSON.c" "$ROOT/tests/yaml_test_core.c" -o "$OUT/core" -lm
"$OUT/core" > "$OUT/core.log" 2>&1 && tail -3 "$OUT/core.log" || { cat "$OUT/core.log"; exit 1; }

echo
echo "== yaml: flow collections =="
gcc $CFLAGS "$ROOT/main/config/yaml.c" "$CJSON/cJSON.c" "$ROOT/tests/yaml_test_flow.c" -o "$OUT/flow" -lm
"$OUT/flow" > "$OUT/flow.log" 2>&1 && tail -3 "$OUT/flow.log" || { cat "$OUT/flow.log"; exit 1; }

echo
echo "ALL YAML TESTS PASSED"
rm -rf "$OUT"
