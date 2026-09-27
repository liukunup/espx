#!/usr/bin/env python3
"""
ESPX Kafka -> TDengine bridge.

Pipeline: device -> EMQX (MQTT) -> Kafka -> **this** -> TDengine -> Grafana

Why a bridge process at all: EMQX has no TDengine sink, and TDengine's own
Kafka connector needs taosX (Enterprise). A small consumer keeps the two
decoupled — Kafka absorbs broker/DB outages and this process can be restarted
without losing data.

Multi-node model
----------------
Every row is tagged with `device_id` (the MQTT client id). Each metric family
is one super table, so a fleet lands in three stable tables and queries
aggregate across nodes:

  espx_env    ts, temperature, humidity                TAG device_id, fw, model
  espx_node   ts, uptime, free_heap, ... , wifi_rssi   TAG device_id, wifi_ssid, ip, fw, model
  espx_state  ts, online, uptime                       TAG device_id, fw

Kafka message envelope (produced by the EMQX rules espx_kafka_*):

  {"kind":"env|node|state", "device_id":"...", "fw":"...", "model":"...",
   "ts": <epoch_ms>, "topic":"...", "body": {...original MQTT payload...}}

Offsets are checkpointed to a local file, so restarting resumes where it left
off. No consumer group is used: the broker's advertised hostname is not
resolvable outside the homelab and group joins fail, so partitions are assigned
explicitly.

Usage
-----
  tools/kafka_to_tdengine.py --once        # drain what is available, then exit
  tools/kafka_to_tdengine.py               # run as a service
  tools/kafka_to_tdengine.py --from-beginning --reset-offsets
"""

import argparse
import json
import os
import signal
import socket
import sys
import time
import urllib.error
import urllib.request

# --------------------------------------------------------------------------- #
# Kafka broker hostname workaround
# --------------------------------------------------------------------------- #
# The broker advertises "1Panel-kafka-cq1x" (its container name), which is not
# resolvable from outside the homelab. Every connection ends up at the same
# broker, so rewrite that name to the routable address.

ADVERTISED_HOSTS = {"1Panel-kafka-cq1x"}
REAL_KAFKA_IP = "192.168.100.101"

_orig_getaddrinfo = socket.getaddrinfo


def _patched_getaddrinfo(host, port, *args, **kwargs):
    if host in ADVERTISED_HOSTS:
        host = REAL_KAFKA_IP
    return _orig_getaddrinfo(host, port, *args, **kwargs)


socket.getaddrinfo = _patched_getaddrinfo

from kafka import KafkaConsumer, TopicPartition  # noqa: E402

# --------------------------------------------------------------------------- #
# TDengine REST client
# --------------------------------------------------------------------------- #


class TDengine:
    """Minimal REST client. taosAdapter lives on 6041 (6060 is the Explorer UI)."""

    def __init__(self, url, user, password, database, verbose=False):
        self.url = url.rstrip("/") + "/rest/sql"
        self.auth = f"{user}:{password}"
        self.database = database
        self.verbose = verbose

    def execute(self, sql, db=None):
        target = self.url + (f"/{db}" if db else "")
        req = urllib.request.Request(target, data=sql.encode())
        import base64
        req.add_header("Authorization",
                       "Basic " + base64.b64encode(self.auth.encode()).decode())
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                body = json.loads(r.read() or b"{}")
        except urllib.error.HTTPError as e:
            raise RuntimeError(f"TDengine HTTP {e.code}: {e.read()[:300]}") from None
        if body.get("code") != 0:
            raise RuntimeError(f"TDengine error: {body}")
        return body

    def insert(self, sql):
        return self.execute(sql, db=self.database)


def sql_str(v):
    if v is None:
        return "NULL"
    return "'" + str(v).replace("'", "''") + "'"


def sql_num(v):
    if v is None:
        return "NULL"
    return str(v)


def sql_bool(v):
    if v is None:
        return "NULL"
    return "true" if v else "false"


# --------------------------------------------------------------------------- #
# message -> row mapping
# --------------------------------------------------------------------------- #


def _get(d, *path, default=None):
    cur = d
    for p in path:
        if not isinstance(cur, dict) or p not in cur:
            return default
        cur = cur[p]
    return cur


def rows_from_message(msg):
    """Turn one Kafka envelope into (stable, tags, columns, ts) tuples.

    Returns a list because one envelope can yield several rows (a sensors
    payload carries one entry per sensor device).
    """
    kind = msg.get("kind")
    body = msg.get("body")
    # Older envelopes carried body as a JSON string (json_encode in the rule).
    # Accept both shapes so a rule change does not silently drop data.
    if isinstance(body, str):
        try:
            body = json.loads(body)
        except Exception:
            body = {}
    if not isinstance(body, dict):
        body = {}
    dev = msg.get("device_id")
    fw = msg.get("fw") or ""
    model = msg.get("model") or ""
    ts = msg.get("ts")
    if not dev or ts is None:
        return []

    out = []
    if kind == "env":
        # body = {"<sensor_id>": {"temperature":..,"humidity":..}, ...}
        for sid, reading in body.items():
            if not isinstance(reading, dict):
                continue
            if "temperature" not in reading and "humidity" not in reading:
                continue
            out.append((
                "espx_env",
                [dev, fw, model],
                [ts, sql_num(reading.get("temperature")), sql_num(reading.get("humidity"))],
            ))
    elif kind == "node":
        ram = body.get("ram") or {}
        cpu = body.get("cpu") or {}
        out.append((
            "espx_node",
            [dev, body.get("wifi_ssid") or "", body.get("ip") or "", fw, model],
            [
                ts,
                sql_num(body.get("uptime")),
                sql_num(body.get("free_heap")),
                sql_num(body.get("min_free_heap")),
                sql_num(body.get("free_heap_internal")),
                sql_num(body.get("min_free_heap_internal")),
                sql_num(cpu.get("usage")),
                sql_num(cpu.get("tasks")),
                sql_num(ram.get("internal_used_pct")),
                sql_num(ram.get("psram_used_pct")),
                sql_num(body.get("wifi_rssi")),
                sql_num(body.get("ws_clients")),
                sql_bool(body.get("time_synced")),
            ],
        ))
    elif kind == "state":
        out.append((
            "espx_state",
            [dev, fw],
            [ts, sql_bool(body.get("online")), sql_num(body.get("uptime"))],
        ))
    return out


def sub_table_name(stable, device_id):
    """Deterministic sub-table name; device ids are hex so this stays simple."""
    safe = "".join(ch if ch.isalnum() else "_" for ch in str(device_id))
    return f"{stable}_{safe}"


def build_insert(db, stable, tag_cols, batch):
    """One INSERT covering many rows; USING/TAGS auto-creates sub-tables."""
    parts = []
    for dev_tags, cols in batch:
        sub = sub_table_name(stable, dev_tags[0])
        tags = ", ".join(sql_str(t) for t in dev_tags)
        vals = ", ".join(str(c) for c in cols)
        parts.append(f"{sub} USING {db}.{stable} TAGS ({tags}) VALUES ({vals})")
    return "INSERT INTO " + " ".join(parts)


# --------------------------------------------------------------------------- #
# offset checkpoint
# --------------------------------------------------------------------------- #


def load_offsets(path):
    if path and os.path.exists(path):
        try:
            with open(path) as f:
                return {int(k): int(v) for k, v in json.load(f).items()}
        except Exception:
            pass
    return {}


def save_offsets(path, offsets):
    if not path:
        return
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump({str(k): v for k, v in offsets.items()}, f)
    os.replace(tmp, path)


# --------------------------------------------------------------------------- #
# main loop
# --------------------------------------------------------------------------- #


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bootstrap", default=os.environ.get("KAFKA_BOOTSTRAP",
                                                          "192.168.100.101:9092"))
    ap.add_argument("--topic", default=os.environ.get("KAFKA_TOPIC", "espx-telemetry"))
    ap.add_argument("--td-url", default=os.environ.get("TDENGINE_URL",
                                                       "http://testing.homelab.lan:6041"))
    ap.add_argument("--td-user", default=os.environ.get("TDENGINE_USER", "root"))
    ap.add_argument("--td-pass", default=os.environ.get("TDENGINE_PASS", "taosdata"))
    ap.add_argument("--db", default=os.environ.get("TDENGINE_DB", "espx"))
    ap.add_argument("--offsets", default=os.environ.get("BRIDGE_OFFSETS",
                                                        "/tmp/espx-bridge-offsets.json"))
    ap.add_argument("--batch-max", type=int, default=200,
                    help="flush after this many rows")
    ap.add_argument("--flush-sec", type=float, default=1.0,
                    help="flush at least this often")
    ap.add_argument("--from-beginning", action="store_true",
                    help="when no checkpoint exists, start at the oldest offset")
    ap.add_argument("--reset-offsets", action="store_true",
                    help="ignore/clear the checkpoint file and start fresh")
    ap.add_argument("--once", action="store_true",
                    help="process what is available, flush, and exit")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if args.reset_offsets and os.path.exists(args.offsets):
        os.remove(args.offsets)

    td = TDengine(args.td_url, args.td_user, args.td_pass, args.db, args.verbose)
    try:
        td.execute(f"USE {args.db}")
    except Exception as e:
        print(f"TDengine not reachable: {e}", file=sys.stderr)
        return 1

    consumer = KafkaConsumer(bootstrap_servers=args.bootstrap,
                             enable_auto_commit=False,
                             consumer_timeout_ms=1000,
                             value_deserializer=None)
    parts = sorted(consumer.partitions_for_topic(args.topic) or [])
    if not parts:
        print(f"topic '{args.topic}' has no partitions (does it exist?)", file=sys.stderr)
        return 1
    tps = [TopicPartition(args.topic, p) for p in parts]
    consumer.assign(tps)

    saved = load_offsets(args.offsets)
    beginning = consumer.beginning_offsets(tps)
    end = consumer.end_offsets(tps)
    for tp in tps:
        if tp.partition in saved:
            consumer.seek(tp, saved[tp.partition])
        else:
            consumer.seek(tp, beginning[tp] if args.from_beginning else end[tp])
    start = {tp.partition: consumer.position(tp) for tp in tps}
    print(f"bridge: {args.topic} -> {args.db} @ {args.td_url}")
    print(f"  partitions {parts}, starting offsets {start}, "
          f"end { {tp.partition: end[tp] for tp in tps} }")

    running = {"on": True}

    def stop(*_):
        running["on"] = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    # per-stable batches: stable -> list of (tags, columns)
    batches = {}
    counts = {"rows": 0, "inserts": 0, "errors": 0, "skipped": 0}
    last_flush = time.time()

    def flush():
        nonlocal batches
        for stable, batch in batches.items():
            if not batch:
                continue
            tag_cols = batch[0][0]
            try:
                td.insert(build_insert(args.db, stable, tag_cols, batch))
                counts["inserts"] += 1
            except Exception as e:
                counts["errors"] += 1
                print(f"insert {stable} failed ({len(batch)} rows): {e}", file=sys.stderr)
        batches = {}
        save_offsets(args.offsets, {tp.partition: consumer.position(tp) for tp in tps})

    try:
        while running["on"]:
            records = consumer.poll(timeout_ms=800, max_records=500)
            for tp, msgs in records.items():
                for m in msgs:
                    if not m.value:
                        counts["skipped"] += 1
                        continue
                    try:
                        env = json.loads(m.value)
                    except Exception:
                        counts["skipped"] += 1
                        continue
                    if not isinstance(env, dict):
                        counts["skipped"] += 1
                        continue
                    rows = rows_from_message(env)
                    if not rows:
                        counts["skipped"] += 1
                        continue
                    for stable, tags, cols in rows:
                        batches.setdefault(stable, []).append((tags, cols))
                        counts["rows"] += 1

            pending = sum(len(b) for b in batches.values())
            if pending >= args.batch_max or (pending and time.time() - last_flush >= args.flush_sec):
                flush()
                last_flush = time.time()
                if args.verbose:
                    print(f"  flushed: {counts}")

            if args.once and pending == 0:
                break
    finally:
        flush()
        consumer.close()
        print(f"bridge stopped. {counts}")


if __name__ == "__main__":
    sys.exit(main() or 0)
