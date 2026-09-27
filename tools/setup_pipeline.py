#!/usr/bin/env python3
"""
Provision the ESPX data pipeline (idempotent).

    device -> EMQX --(MQTT)--> EMQX rule+action --(Kafka)--> bridge -> TDengine

This script sets up everything except the bridge process itself
(see tools/kafka_to_tdengine.py) and Grafana:

  1. Kafka: create the telemetry topic if missing
  2. EMQX : create/update the Kafka action and the three rules that feed it
  3. TDengine: create the database and the three super tables

Run it again after changing a topic name or schema; every step is upsert.

Usage
-----
  tools/setup_pipeline.py --dry-run
  tools/setup_pipeline.py --apply
"""

import argparse
import base64
import json
import os
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request

# The Kafka broker advertises its container name, which does not resolve from
# outside the homelab. All connections land on the same broker.
ADVERTISED_HOSTS = {"1Panel-kafka-cq1x"}
REAL_KAFKA_IP = "192.168.100.101"

_orig_getaddrinfo = socket.getaddrinfo


def _patched(host, port, *a, **kw):
    if host in ADVERTISED_HOSTS:
        host = REAL_KAFKA_IP
    return _orig_getaddrinfo(host, port, *a, **kw)


socket.getaddrinfo = _patched

# --------------------------------------------------------------------------- #
# HTTP helpers
# --------------------------------------------------------------------------- #


def http_json(method, url, body=None, auth=None, timeout=20):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if auth:
        req.add_header("Authorization",
                       "Basic " + base64.b64encode(auth.encode()).decode())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            raw = r.read()
            return r.status, (json.loads(raw) if raw else {})
    except urllib.error.HTTPError as e:
        raw = e.read()
        try:
            return e.code, json.loads(raw)
        except Exception:
            return e.code, {"raw": raw[:300].decode(errors="replace")}


class Emqx:
    def __init__(self, host, port, user, password):
        self.base = f"http://{host}:{port}/api/v5"
        self.user, self.password, self.token = user, password, None

    def login(self):
        st, r = http_json("POST", f"{self.base}/login",
                          {"username": self.user, "password": self.password})
        if st != 200:
            raise RuntimeError(f"EMQX login failed ({st}): {r}")
        self.token = r["token"]
        return r

    def api(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method)
        req.add_header("Content-Type", "application/json")
        req.add_header("Authorization", "Bearer " + self.token)
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                raw = r.read()
                return r.status, (json.loads(raw) if raw else {})
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw)
            except Exception:
                return e.code, {"raw": raw[:300].decode(errors="replace")}


class TDengine:
    def __init__(self, host, port, user, password):
        self.url = f"http://{host}:{port}/rest/sql"
        self.auth = f"{user}:{password}"

    def sql(self, statement, db=None):
        target = self.url + (f"/{db}" if db else "")
        req = urllib.request.Request(target, data=statement.encode())
        req.add_header("Authorization",
                       "Basic " + base64.b64encode(self.auth.encode()).decode())
        try:
            with urllib.request.urlopen(req, timeout=20) as r:
                return json.loads(r.read() or b"{}")
        except urllib.error.HTTPError as e:
            raise RuntimeError(f"TDengine HTTP {e.code}: {e.read()[:300]}") from None


# --------------------------------------------------------------------------- #
# schema definitions
# --------------------------------------------------------------------------- #

# Every metric family is one super table; device_id is the tag that makes a
# fleet aggregate into a single table (and lets each node have its own
# sub-table underneath).
SUPER_TABLES = {
    "espx_env": (
        "ts TIMESTAMP, temperature FLOAT, humidity FLOAT",
        "device_id NCHAR(32), fw NCHAR(16), model NCHAR(16)",
    ),
    "espx_node": (
        "ts TIMESTAMP, uptime BIGINT, free_heap BIGINT, min_free_heap BIGINT, "
        "free_heap_internal BIGINT, min_free_heap_internal BIGINT, "
        "cpu_usage FLOAT, cpu_tasks INT, ram_internal_used_pct FLOAT, "
        "ram_psram_used_pct FLOAT, wifi_rssi INT, ws_clients INT, time_synced BOOL",
        "device_id NCHAR(32), wifi_ssid NCHAR(64), ip NCHAR(16), "
        "fw NCHAR(16), model NCHAR(16)",
    ),
    "espx_state": (
        "ts TIMESTAMP, online BOOL, uptime BIGINT",
        "device_id NCHAR(32), fw NCHAR(16)",
    ),
}

# EMQX rule -> (MQTT topic filter, envelope "kind")
RULES = {
    "espx_kafka_env":   ("espx/+/sensors", "env"),
    "espx_kafka_node":  ("espx/+/status",  "node"),
    "espx_kafka_state": ("espx/+/state",   "state"),
}

# The Kafka message body. Built from rule output fields: EMQX 6.3's SQL engine
# rejects map_put()/concat() here, so the envelope is assembled by the action's
# template instead of by json_encode in the SELECT.
MESSAGE_TEMPLATE = (
    '{"kind":"${kind}","device_id":"${device_id}","fw":"${fw}",'
    '"model":"${model}","ts":${ts},"topic":"${topic}","body":${body}}'
)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--emqx-host", default="testing.homelab.lan")
    ap.add_argument("--emqx-port", type=int, default=18083)
    ap.add_argument("--emqx-user", default=os.environ.get("EMQX_USER", "admin"))
    # No baked-in password: the dashboard credential is deployment-specific.
    ap.add_argument("--emqx-pass", default=os.environ.get("EMQX_PASS"))
    ap.add_argument("--kafka-bootstrap",
                    default=os.environ.get("KAFKA_BOOTSTRAP", "192.168.100.101:9092"))
    ap.add_argument("--kafka-topic", default=os.environ.get("KAFKA_TOPIC", "espx-telemetry"))
    ap.add_argument("--kafka-partitions", type=int, default=3)
    ap.add_argument("--td-host", default=os.environ.get("TDENGINE_HOST", "testing.homelab.lan"))
    ap.add_argument("--td-port", type=int,
                    default=int(os.environ.get("TDENGINE_PORT", "6041")))
    ap.add_argument("--td-user", default=os.environ.get("TDENGINE_USER", "root"))
    # taosdata is TDengine's public factory default, kept as a fallback.
    ap.add_argument("--td-pass", default=os.environ.get("TDENGINE_PASS", "taosdata"))
    ap.add_argument("--db", default=os.environ.get("TDENGINE_DB", "espx"))
    ap.add_argument("--keep-days", type=int, default=365)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    if not args.apply and not args.dry_run:
        ap.error("choose --dry-run or --apply")

    if not args.emqx_pass:
        ap.error("--emqx-pass (or EMQX_PASS) is required")

    print(f"EMQX     : {args.emqx_host}:{args.emqx_port}")
    print(f"Kafka    : {args.kafka_bootstrap}  topic={args.kafka_topic}")
    print(f"TDengine : {args.td_host}:{args.td_port}  db={args.db}")

    if args.dry_run:
        print("\nPlan")
        print(f"  - ensure Kafka topic '{args.kafka_topic}' ({args.kafka_partitions} partitions)")
        print(f"  - ensure EMQX action 'espx_to_kafka' (kafka_producer -> Kafka connector)")
        for rid, (tf, kind) in RULES.items():
            print(f"  - ensure EMQX rule {rid}: {tf}  (kind={kind})")
        print(f"  - ensure TDengine database '{args.db}'")
        for name in SUPER_TABLES:
            print(f"  - ensure stable {args.db}.{name}")
        return 0

    # ------------------------------------------------------------ Kafka topic
    from kafka.admin import KafkaAdminClient, NewTopic
    admin = KafkaAdminClient(bootstrap_servers=args.kafka_bootstrap, request_timeout_ms=10000)
    existing = set(admin.list_topics())
    if args.kafka_topic in existing:
        print(f"  kafka topic : '{args.kafka_topic}' already present")
    else:
        admin.create_topics([NewTopic(name=args.kafka_topic,
                                      num_partitions=args.kafka_partitions,
                                      replication_factor=1)])
        print(f"  kafka topic : created '{args.kafka_topic}'")
    admin.close()

    # ----------------------------------------------------------------- EMQX
    emqx = Emqx(args.emqx_host, args.emqx_port, args.emqx_user, args.emqx_pass)
    emqx.login()

    st, r = emqx.api("GET", "/connectors/kafka_producer:Kafka")
    if st != 200:
        print(f"  EMQX        : connector 'Kafka' missing ({st}). "
              f"Create it in the dashboard first.", file=sys.stderr)
        return 1
    print(f"  emqx conn   : Kafka ({r.get('status')})")

    action = {
        "connector": "Kafka",
        "parameters": {
            "topic": args.kafka_topic,
            "message": {"key": "${device_id}", "value": MESSAGE_TEMPLATE},
            "partition_strategy": "key_dispatch",
        },
        "description": "ESPX telemetry -> Kafka",
    }
    st, r = emqx.api("PUT", "/actions/kafka_producer:espx_to_kafka", action)
    print(f"  emqx action : {'ok' if st == 200 else f'FAILED {st} {r}'}")

    for rid, (topic_filter, kind) in RULES.items():
        sql = (f"SELECT '{kind}' AS kind, clientid AS device_id, "
               f"user_properties.fw AS fw, user_properties.model AS model, "
               f"timestamp AS ts, topic AS topic, payload AS body "
               f"FROM \"{topic_filter}\"")
        body = {"id": rid, "sql": sql, "actions": ["kafka_producer:espx_to_kafka"]}
        st, _ = emqx.api("PUT", f"/rules/{urllib.parse.quote(rid)}", body)
        if st == 404:
            st, _ = emqx.api("POST", "/rules", body)
        print(f"  emqx rule   : {rid} {'ok' if st in (200, 201) else f'FAILED {st}'}")

    # ------------------------------------------------------------- TDengine
    td = TDengine(args.td_host, args.td_port, args.td_user, args.td_pass)
    td.sql(f"CREATE DATABASE IF NOT EXISTS {args.db} KEEP {args.keep_days} "
           f"DURATION 10 WAL_LEVEL 1")
    print(f"  tdengine db : {args.db} ready")
    for name, (cols, tags) in SUPER_TABLES.items():
        td.sql(f"CREATE STABLE IF NOT EXISTS {args.db}.{name} ({cols}) TAGS ({tags})")
        print(f"  tdengine st : {name} ready")

    print("\nDone. Start the bridge with tools/kafka_to_tdengine.py")
    return 0


if __name__ == "__main__":
    sys.exit(main() or 0)
