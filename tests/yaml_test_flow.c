#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cJSON.h>
#include "yaml.h"
static int fail = 0;
static void expect(const char *name, const char *yaml, const char *want) {
    int line=0; const char *msg=NULL;
    cJSON *j = yaml_parse_ex(yaml, &line, &msg);
    char *got = j ? cJSON_PrintUnformatted(j) : NULL;
    int ok = j && strcmp(got, want) == 0;
    printf("  [%s] %s\n", ok?"PASS":"FAIL", name);
    if (!ok) { printf("        want: %s\n        got : %s\n        err : line %d %s\n",
                     want, got?got:"<null>", line, msg?msg:""); fail++; }
    free(got); if (j) cJSON_Delete(j);
}
static void expect_err(const char *name, const char *yaml) {
    int line=0; const char *msg=NULL;
    cJSON *j = yaml_parse_ex(yaml, &line, &msg);
    int ok = (j == NULL);
    printf("  [%s] %s  (line %d: %s)\n", ok?"PASS":"FAIL", name, line, msg?msg:"");
    if (j) cJSON_Delete(j);
    if (!ok) fail++;
}
int main(void){
    puts("flow collections");
    expect("flow map", "config: {gpio: 5, active_level: 1}\n",
           "{\"config\":{\"gpio\":5,\"active_level\":1}}");
    expect("flow seq", "ids: [a, b, c]\n", "{\"ids\":[\"a\",\"b\",\"c\"]}");
    expect("nested flow", "d: {id: x, cfg: {gpio: 4, on: true}}\n",
           "{\"d\":{\"id\":\"x\",\"cfg\":{\"gpio\":4,\"on\":true}}}");
    expect("flow in list item", "devs:\n  - {id: a, type: relay}\n  - {id: b, type: dht11}\n",
           "{\"devs\":[{\"id\":\"a\",\"type\":\"relay\"},{\"id\":\"b\",\"type\":\"dht11\"}]}");
    expect("flow map with quoted key", "m: {\"a b\": 1}\n", "{\"m\":{\"a b\":1}}");
    expect("flow seq of nums", "n: [1, 2.5, -3]\n", "{\"n\":[1,2.5,-3]}");
    expect("empty flow map", "e: {}\n", "{\"e\":{}}");
    expect("empty flow seq", "e: []\n", "{\"e\":[]}");
    expect("real config from the docs",
        "node:\n  device_id: espx-0001\n  name: Line-1\n"
        "network:\n  mqtt_broker: mqtt://192.168.4.2:1883\n  mqtt_topic_prefix: plant/line1\n"
        "devices:\n  - id: temp_in\n    type: dht11\n    config:\n      gpio: 4\n      interval_ms: 5000\n"
        "  - id: relay_a\n    type: relay\n    config: {gpio: 5, active_level: 1}\n"
        "remove_devices: [old1, old2]\nreplace_devices: true\n",
        "{\"node\":{\"device_id\":\"espx-0001\",\"name\":\"Line-1\"},"
        "\"network\":{\"mqtt_broker\":\"mqtt://192.168.4.2:1883\",\"mqtt_topic_prefix\":\"plant/line1\"},"
        "\"devices\":[{\"id\":\"temp_in\",\"type\":\"dht11\",\"config\":{\"gpio\":4,\"interval_ms\":5000}},"
        "{\"id\":\"relay_a\",\"type\":\"relay\",\"config\":{\"gpio\":5,\"active_level\":1}}],"
        "\"remove_devices\":[\"old1\",\"old2\"],\"replace_devices\":true}");
    puts("malformed flow must error, not silently become a string");
    expect_err("unclosed flow map", "a: {b: 1\n");
    expect_err("missing value", "a: {b:}\n");
    expect_err("trailing junk", "a: {b: 1} xxx\n");
    printf("\n%s (%d failure(s))\n", fail?"FAILURES":"ALL PASS", fail);
    return fail?1:0;
}
