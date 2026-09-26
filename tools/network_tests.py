#!/usr/bin/env python3
"""
ESPX network integration tests.

Runs against a device that is reachable from this host. Verifies the HTTPS API,
the MQTT integration (against a locally spawned broker), and delta OTA end to
end.

The host must be able to reach the device, and the device must be able to reach
the host (for OTA download and MQTT). Two ways to arrange that:

  A. Put the device on the same LAN as this host, then pass
         --device-ip <device-ip> --local-ip <this-host-ip>
     (the device gets on the LAN either through SoftAP provisioning, or by
      setting network.wifi_ssid in factory data)

  B. Join the device's own provisioning SoftAP from this host, then use
         --device-ip 192.168.4.1 --local-ip 192.168.4.2

Usage:
    tools/network_tests.py --device-ip 192.168.x.y --local-ip 192.168.x.z \
                           --patch dist/fw.patch [--broker-port 1883] \
                           [--skip ota|mqtt|api]

Exit code is non-zero if any check fails.
"""

import argparse
import http.client
import json
import os
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

results = []


def check(label, ok, detail=""):
    results.append((label, ok, detail))
    print(f"  [{'PASS' if ok else 'FAIL'}] {label}" + (f"  — {detail}" if detail and not ok else ""))
    return ok


class Device:
    def __init__(self, ip):
        self.ip = ip
        self.ctx = ssl.create_default_context()
        self.ctx.check_hostname = False
        self.ctx.verify_mode = ssl.CERT_NONE

    # A Wi-Fi link drops the first packet after an idle period often enough that
    # a single attempt produces spurious EHOSTUNREACH/timeouts. Retry those
    # transport-level errors; never retry an HTTP error, which is a real answer.
    RETRIES = 4
    RETRY_DELAY = 1.5

    def _attempt(self, method, path, body, timeout):
        url = f"https://{self.ip}{path}"
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(url, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, context=self.ctx, timeout=timeout) as r:
                raw = r.read()
                return r.status, (json.loads(raw) if raw else None)
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw)
            except Exception:
                return e.code, None

    def _req(self, method, path, body=None, timeout=15):
        last = None
        for attempt in range(self.RETRIES):
            try:
                return self._attempt(method, path, body, timeout)
            except (urllib.error.URLError, OSError) as e:
                last = e
                if attempt < self.RETRIES - 1:
                    time.sleep(self.RETRY_DELAY)
        raise last

    def _req_raw(self, method, path, body, content_type, timeout=20):
        url = f"https://{self.ip}{path}"
        req = urllib.request.Request(url, data=body.encode(), method=method,
                                     headers={"Content-Type": content_type})
        try:
            with urllib.request.urlopen(req, context=self.ctx, timeout=timeout) as r:
                raw = r.read()
                return r.status, (json.loads(raw) if raw else None)
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw)
            except Exception:
                return e.code, None

    def post_raw(self, path, body, content_type="text/yaml", **kw):
        return self._req_raw("POST", path, body, content_type, **kw)

    def get(self, path, **kw):
        return self._req("GET", path, **kw)

    def post(self, path, body=None, **kw):
        return self._req("POST", path, body, **kw)

    def put(self, path, body=None, **kw):
        return self._req("PUT", path, body, **kw)

    def delete(self, path, **kw):
        return self._req("DELETE", path, **kw)


# --------------------------------------------------------------------------- #
# HTTPS / REST API
# --------------------------------------------------------------------------- #

def test_api(dev, args):
    print("HTTPS API")

    st, node = dev.get("/api/node")
    check("GET /api/node", st == 200 and node and "device_id" in node,
          f"status={st} body={node}")

    st, info = dev.get("/api/system/info")
    check("GET /api/system/info (uptime, heap, ip)",
          st == 200 and info and "uptime" in info and "free_heap" in info,
          f"status={st} body={info}")
    if info:
        print(f"        uptime={info.get('uptime')}s free_heap={info.get('free_heap')} "
              f"ip={info.get('ip')} rssi={info.get('wifi_rssi')}")

    st, types = dev.get("/api/device-types")
    names = [t["name"] for t in types] if isinstance(types, list) else []
    check("GET /api/device-types lists every driver",
          st == 200 and {"dht11", "button", "relay", "shiftreg_595", "ws2812"} <= set(names),
          f"status={st} types={names}")
    check("device types carry default configs",
          isinstance(types, list) and all("default_config" in t for t in types))

    # add / read / write / enable / delete
    st, _ = dev.delete("/api/devices/nettest")
    st, r = dev.post("/api/devices",
                     {"id": "nettest", "type": "relay",
                      "config": {"gpio": 5, "active_level": 1}})
    check("POST /api/devices (add relay)", st == 200 and r and r.get("success"), f"{st} {r}")

    st, r = dev.post("/api/devices/nettest/write", True)
    check("POST /api/devices/{id}/write", st == 200 and r and r.get("success"), f"{st} {r}")

    st, r = dev.post("/api/devices/nettest/read")
    check("POST /api/devices/{id}/read returns the state",
          st == 200 and r is not None and r.get("state") is True, f"{st} {r}")

    st, r = dev.post("/api/devices/nettest/enable", {"enabled": False})
    check("POST /api/devices/{id}/enable", st == 200 and r and r.get("success"), f"{st} {r}")

    st, devs = dev.get("/api/devices")
    nettest = next((d for d in devs if d["id"] == "nettest"), None) if isinstance(devs, list) else None
    check("a disabled device reports enabled=false and no value",
          nettest is not None and nettest.get("enabled") is False and "value" not in nettest,
          f"{nettest}")

    # Re-enable so the next check sees a live value (a disabled device has none
    # by design: the driver is de-initialised).
    dev.post("/api/devices/nettest/enable", {"enabled": True})
    st, devs = dev.get("/api/devices")
    nettest = next((d for d in devs if d["id"] == "nettest"), None) if isinstance(devs, list) else None
    check("GET /api/devices includes live value and config",
          nettest is not None and "value" in nettest and "config" in nettest,
          f"{nettest}")

    st, r = dev.post("/api/devices/reload")
    check("POST /api/devices/reload", st == 200 and r and r.get("success"), f"{st} {r}")

    st, r = dev.delete("/api/devices/nettest")
    check("DELETE /api/devices/{id}", st == 200 and r and r.get("success"), f"{st} {r}")

    st, r = dev.delete("/api/devices/nosuchdev")
    check("DELETE of an unknown device returns 404", st == 404, f"status={st}")

    st, net = dev.get("/api/network")
    check("GET /api/network", st == 200 and isinstance(net, dict), f"{st} {net}")

    # ---- YAML configuration over HTTPS -----------------------------------
    print("HTTPS YAML configuration")

    st, cur = dev.get("/api/config")
    check("GET /api/config exports the configuration",
          st == 200 and isinstance(cur, dict) and "devices" in cur, f"{st} {cur}")

    # Pre-create the device that the document will remove, so "removed" is a
    # real transition rather than a no-op.
    dev.delete("/api/devices/y1_doomed")
    dev.post("/api/devices", {"id": "y1_doomed", "type": "relay",
                              "config": {"gpio": 5, "active_level": 1}})

    yaml_doc = (
        "node:\n"
        "  name: YAML-Test-Node\n"
        "devices:\n"
        "  - id: y1_strip\n"
        "    type: ws2812\n"
        "    config: {data_gpio: 48, count: 1, brightness: 32}\n"
        "remove_devices: [y1_doomed]\n"
    )
    st, r = dev.post_raw("/api/config", yaml_doc, "text/yaml")
    check("POST /api/config accepts YAML", st == 200 and r and r.get("ok"), f"{st} {r}")
    if r:
        ap = r.get("applied", {})
        check("YAML: one device added", ap.get("added") == 1, f"{ap}")
        check("YAML: one device removed", ap.get("removed") == 1, f"{ap}")

    st, node = dev.get("/api/node")
    check("YAML: node.name applied", node and node.get("name") == "YAML-Test-Node", f"{node}")

    st, devs = dev.get("/api/devices")
    ids = [d["id"] for d in devs] if isinstance(devs, list) else []
    check("YAML: y1_strip bound, y1_doomed removed",
          "y1_strip" in ids and "y1_doomed" not in ids, f"ids={ids}")

    # Genuinely malformed: an unclosed flow collection. Inconsistent (but
    # self-consistent) indentation is legal YAML and is deliberately accepted.
    st, r = dev.post_raw("/api/config",
                         "devices:\n  - id: x\n    type: relay\n    config: {gpio: 5\n",
                         "text/yaml")
    check("malformed YAML rejected with ok=false",
          st in (200, 400) and r is not None and r.get("ok") is False, f"{st} {r}")

    st, devs2 = dev.get("/api/devices")
    check("rejected YAML changed nothing",
          [d["id"] for d in devs2] == ids, f"{[d['id'] for d in devs2]}")

    dev.delete("/api/devices/y1_strip")
    return net or {}


# --------------------------------------------------------------------------- #
# MQTT
# --------------------------------------------------------------------------- #

class BrokerProcess:
    def __init__(self, host, port, verbose=False):
        cmd = [sys.executable, os.path.join(ROOT, "tools", "mini_mqtt_broker.py"),
               "--host", host, "--port", str(port)]
        if verbose:
            cmd.append("-v")
        self.p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  text=True)
        self.log = []

    def wait_ready(self, timeout=5):
        end = time.time() + timeout
        while time.time() < end:
            try:
                s = socket.create_connection(("127.0.0.1", 1883), timeout=0.5)
                s.close()
                return True
            except OSError:
                time.sleep(0.1)
        return False

    def stop(self):
        self.p.send_signal(signal.SIGINT)
        try:
            self.p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.p.kill()


def test_mqtt(dev, args, net):
    print("MQTT")
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        check("paho-mqtt available", False, "pip install paho-mqtt")
        return

    broker = BrokerProcess("0.0.0.0", args.broker_port)
    if not broker.wait_ready():
        broker.stop()
        check("local broker started", False)
        return
    check("local broker started", True, f"0.0.0.0:{args.broker_port}")

    received = []
    lock = threading.Lock()

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="espx-test")
    def on_msg(_c, _u, msg):
        with lock:
            received.append((msg.topic, msg.payload.decode(errors="replace")))

    cli.on_message = on_msg
    cli.connect("127.0.0.1", args.broker_port, 30)
    cli.loop_start()
    time.sleep(0.3)

    # point the device at our broker
    prefix = f"espx/test/{int(time.time())}"
    st, r = dev.put("/api/network", {
        "mqtt_broker": f"mqtt://{args.local_ip}:{args.broker_port}",
        "mqtt_topic_prefix": prefix,
    })
    check("PUT /api/network (broker + prefix)", st == 200 and r and r.get("success"), f"{st} {r}")

    st, r = dev.post("/api/system/reboot")
    print("        rebooting device to apply MQTT settings…")
    time.sleep(12)

    cli.subscribe(prefix + "/#")
    time.sleep(1)

    # wait for the retained online state
    online = None
    end = time.time() + 60
    while time.time() < end and online is None:
        with lock:
            for t, p in received:
                if t == prefix + "/state":
                    online = p
        time.sleep(0.5)

    check(f"device publishes {prefix}/state on connect", online is not None, f"got {received[:3]}")
    if online:
        print(f"        state: {online}")

    # command: set a relay through MQTT
    dev.post("/api/devices", {"id": "mq1", "type": "relay",
                              "config": {"gpio": 5, "active_level": 1}})
    time.sleep(1)
    with lock:
        received.clear()

    cli.publish(f"{prefix}/cmd/control/mq1", json.dumps({"action": "set", "value": True}), qos=1)
    time.sleep(3)

    st, r = dev.post("/api/devices/mq1/read")
    check("MQTT cmd/control actuates the device", st == 200 and r and r.get("state") is True,
          f"device state={r}")

    with lock:
        received.clear()
    cli.publish(f"{prefix}/cmd/query/mq1", json.dumps({"action": "get"}), qos=1)
    time.sleep(3)
    with lock:
        got = list(received)
    check("MQTT cmd/query produces a reply", any("attrs" in t for t, _ in got), f"{got}")

    with lock:
        received.clear()
    cli.publish(f"{prefix}/cmd/config", json.dumps({"action": "get_devices"}), qos=1)
    time.sleep(3)
    with lock:
        got = list(received)
    check("MQTT cmd/config get_devices replies with the device list",
          any("devices" in t for t, _ in got), f"{got}")

    # ---- YAML configuration pushed over MQTT -----------------------------
    with lock:
        received.clear()

    yaml_doc = (
        "node:\n"
        "  name: MQTT-YAML-Node\n"
        "devices:\n"
        "  - id: mq_yaml_relay\n"
        "    type: relay\n"
        "    config: {gpio: 5, active_level: 1}\n"
        "  - id: mq_yaml_sr\n"
        "    type: shiftreg_595\n"
        "    config: {data_gpio: 16, clock_gpio: 17, latch_gpio: 18, count: 2}\n"
        "remove_devices: [mq1]\n"
    )
    cli.publish(f"{prefix}/cmd/config", yaml_doc, qos=1)
    time.sleep(6)

    with lock:
        got = list(received)
    result = None
    for t, p in got:
        if t.endswith("/config/result"):
            try:
                result = json.loads(p)
            except Exception:
                result = {"ok": False, "error": f"unparseable: {p[:80]}"}

    check("MQTT YAML push answered on <prefix>/config/result", result is not None, f"{got}")
    if result:
        print(f"        result: {result}")
        check("MQTT YAML push reported ok", result.get("ok") is True, f"{result}")
        ap = result.get("applied", {})
        check("MQTT YAML: 2 devices added", ap.get("added") == 2, f"{ap}")
        check("MQTT YAML: 1 device removed", ap.get("removed") == 1, f"{ap}")

    # verify through HTTPS that the MQTT push really took effect
    st, devs = dev.get("/api/devices")
    ids = [d["id"] for d in devs] if isinstance(devs, list) else []
    check("MQTT YAML: bindings visible over HTTPS",
          "mq_yaml_relay" in ids and "mq_yaml_sr" in ids and "mq1" not in ids, f"ids={ids}")

    st, node = dev.get("/api/node")
    check("MQTT YAML: node.name applied", node and node.get("name") == "MQTT-YAML-Node", f"{node}")

    # a malformed YAML document over MQTT must be reported, not crash the node
    with lock:
        received.clear()
    cli.publish(f"{prefix}/cmd/config",
                "devices:\n  - id: x\n    type: relay\n    config: {gpio: 5\n", qos=1)
    time.sleep(5)
    with lock:
        got = list(received)
    badresult = next((json.loads(p) for t, p in got if t.endswith("/config/result")), None)
    check("MQTT bad YAML reported as ok=false",
          badresult is not None and badresult.get("ok") is False, f"{badresult}")

    cli.publish(f"{prefix}/cmd/config", json.dumps({"action": "get_config"}), qos=1)
    time.sleep(4)
    with lock:
        got = list(received)
    check("MQTT action=get_config returns the configuration",
          any("attrs/config" in t for t, _ in got), f"{[t for t,_ in got]}")

    for d in ("mq1", "mq_yaml_relay", "mq_yaml_sr"):
        dev.delete(f"/api/devices/{d}")
    cli.loop_stop()
    cli.disconnect()
    broker.stop()


# --------------------------------------------------------------------------- #
# Delta OTA
# --------------------------------------------------------------------------- #

def serve_directory(directory, port):
    handler = subprocess.Popen(
        [sys.executable, "-m", "http.server", str(port), "--bind", "0.0.0.0",
         "--directory", directory],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.8)
    return handler


def test_ota(dev, args):
    print("Delta OTA")

    st, before = dev.get("/api/ota/status")
    check("GET /api/ota/status", st == 200 and before is not None, f"{st} {before}")
    running = (before or {}).get("running_version")
    print(f"        running version: {running}")

    if not args.patch:
        print("        --patch not given; skipping the download test")
        return

    srv = serve_directory(os.path.dirname(os.path.abspath(args.patch)), args.http_port)
    url = f"http://{args.local_ip}:{args.http_port}/{os.path.basename(args.patch)}"
    print(f"        serving {url}")

    try:
        st, r = dev.post("/api/ota/start", {"url": url})
        check("POST /api/ota/start accepted", st == 200 and r and r.get("started"), f"{st} {r}")

        # follow progress
        seen = set()
        failed = None
        end = time.time() + 120
        while time.time() < end:
            st, s = dev.get("/api/ota/status")
            if s:
                seen.add(s.get("state"))
                if s.get("state") == "FAILED":
                    failed = s.get("error")
                    break
            time.sleep(1)

        if failed:
            check("OTA applied", False, f"FAILED: {failed}")
            return

        check("OTA reached APPLYING/REBOOTING", bool(seen & {"APPLYING", "REBOOTING"}), f"states={seen}")

        # wait for the reboot and come back
        print("        waiting for reboot…")
        time.sleep(18)
        end = time.time() + 60
        after = None
        while time.time() < end:
            try:
                st, after = dev.get("/api/ota/status", timeout=5)
                if st == 200 and after:
                    break
            except Exception:
                pass
            time.sleep(2)

        check("device came back after OTA", after is not None)
        if after:
            print(f"        version now: {after.get('running_version')} (was {running})")

        st, devs = dev.get("/api/devices")
        check("device configuration survived the OTA", isinstance(devs, list), f"{devs}")
    finally:
        srv.terminate()


# --------------------------------------------------------------------------- #

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--device-ip", required=True)
    ap.add_argument("--local-ip", required=True,
                    help="this host's IP as seen by the device")
    ap.add_argument("--patch", help="delta patch to apply during the OTA test")
    ap.add_argument("--broker-port", type=int, default=1883)
    ap.add_argument("--http-port", type=int, default=8000)
    ap.add_argument("--skip", action="append", default=[],
                    choices=["api", "mqtt", "ota"])
    args = ap.parse_args()

    print(f"device {args.device_ip}  host {args.local_ip}\n")
    dev = Device(args.device_ip)

    try:
        st, _ = dev.get("/api/node", timeout=8)
        check("device reachable over HTTPS", st == 200, f"status={st}")
    except Exception as e:
        check("device reachable over HTTPS", False, str(e))
        return 1

    if "api" not in args.skip:
        test_api(dev, args)
    if "mqtt" not in args.skip:
        test_mqtt(dev, args, {})
    if "ota" not in args.skip:
        test_ota(dev, args)

    failed = [l for l, ok, _ in results if not ok]
    print()
    print(f"{len(results) - len(failed)}/{len(results)} checks passed")
    if failed:
        print("failed:", ", ".join(failed))
        return 1
    print("RESULT: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
