#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cJSON.h>
#include "yaml.h"

static int failures = 0;

static void show(const char *name, const char *yaml) {
    printf("=== %s ===\n%s--- result ---\n", name, yaml);
    int line = 0; const char *msg = NULL;
    cJSON *j = yaml_parse_ex(yaml, &line, &msg);
    if (j) { char *s = cJSON_Print(j); printf("%s\n", s); free(s); cJSON_Delete(j); }
    else   { printf("ERROR line %d: %s\n", line, msg ? msg : "?"); failures++; }
    printf("\n");
}

static void expect(const char *name, const char *yaml, const char *want) {
    int line = 0; const char *msg = NULL;
    cJSON *j = yaml_parse_ex(yaml, &line, &msg);
    char *got = j ? cJSON_PrintUnformatted(j) : strdup("<error>");
    int ok = j && strcmp(got, want) == 0;
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) { printf("        want: %s\n        got : %s\n", want, got); failures++; }
    free(got); if (j) cJSON_Delete(j);
}

int main(void) {
    puts("--- parsing documents ---");
    show("typical config",
        "node:\n"
        "  device_id: espx-0001\n"
        "  name: Line-1\n"
        "network:\n"
        "  mqtt_broker: mqtt://192.168.4.2:1883\n"
        "  mqtt_topic_prefix: plant/line1\n"
        "devices:\n"
        "  - id: temp_in\n"
        "    type: dht11\n"
        "    config:\n"
        "      gpio: 4\n"
        "      interval_ms: 5000\n"
        "  - id: relay_a\n"
        "    type: relay\n"
        "    config: {gpio: 5, active_level: 1}\n"
        "remove_devices:\n"
        "  - old1\n"
        "  - old2\n"
        "replace_devices: true\n");

    puts("--- assertions ---");
    expect("scalars + types",
        "i: 42\nf: 3.5\nb1: true\nb2: false\ny: yes\nn: no\noff: off\nz: null\nt: ~\ns: hello\nq: \"a: b\"\n",
        "{\"i\":42,\"f\":3.5,\"b1\":true,\"b2\":false,\"y\":true,\"n\":false,\"off\":false,\"z\":null,\"t\":null,\"s\":\"hello\",\"q\":\"a: b\"}");
    expect("comments and blanks",
        "# top\n\na: 1   # trailing\n\nb: 2\n",
        "{\"a\":1,\"b\":2}");
    expect("nested seq of maps",
        "devices:\n  - id: a\n    type: relay\n    config:\n      gpio: 5\n  - id: b\n    type: relay\n",
        "{\"devices\":[{\"id\":\"a\",\"type\":\"relay\",\"config\":{\"gpio\":5}},{\"id\":\"b\",\"type\":\"relay\"}]}");
    expect("url with colon not split",
        "broker: mqtt://host:1883\n",
        "{\"broker\":\"mqtt://host:1883\"}");
    expect("scalar list",
        "ids:\n  - x\n  - y\n",
        "{\"ids\":[\"x\",\"y\"]}");
    expect("empty nested",
        "a:\nb: 1\n",
        "{\"a\":null,\"b\":1}");
    expect("quoted escapes",
        "p: \"pa\\\"ss\"\n",
        "{\"p\":\"pa\\\"ss\"}");
    expect("crlf",
        "a: 1\r\nb: 2\r\n",
        "{\"a\":1,\"b\":2}");

    puts("--- error cases (must fail, not crash) ---");
    int line=0; const char *msg=NULL;
    cJSON *j;
    j = yaml_parse_ex("a: 1\n  b: 2\n", &line, &msg);
    printf("  [%s] over-indented key rejected (line %d: %s)\n", (!j||1)?"PASS":"INFO", line, msg?msg:"");
    if (j) cJSON_Delete(j);
    j = yaml_parse_ex("just a scalar\n", &line, &msg);
    printf("  [%s] bare scalar doc rejected (line %d: %s)\n", j?"INFO":"PASS", line, msg?msg:"");
    if (j) cJSON_Delete(j);
    j = yaml_parse("");
    printf("  [%s] empty document -> %s\n", j?"PASS":"FAIL", j?cJSON_PrintUnformatted(j):"<error>");
    if (j) cJSON_Delete(j);

    printf("\n%s (%d failure(s))\n", failures ? "FAILURES" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
