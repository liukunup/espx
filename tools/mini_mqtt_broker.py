#!/usr/bin/env python3
"""
Minimal MQTT 3.1.1 broker for ESPX integration testing.

Supports only what the device needs:
  CONNECT / CONNACK, PUBLISH (QoS 0 and 1) both directions,
  SUBSCRIBE / SUBACK, PINGREQ / PINGRESP, DISCONNECT

Not a production broker: no persistence, no QoS 2, no auth, no wildcard-subscription
edge cases beyond '#' and '+' prefix matching needed for topic inspection.

Usage:
    tools/mini_mqtt_broker.py --host 0.0.0.0 --port 1883 [--verbose]
"""

import argparse
import socket
import struct
import sys
import threading
import time

CONNECT, CONNACK = 1, 2
PUBLISH, PUBACK = 3, 4
SUBSCRIBE, SUBACK = 8, 9
UNSUBSCRIBE, UNSUBACK = 10, 11
PINGREQ, PINGRESP = 12, 13
DISCONNECT = 14

NAMES = {CONNECT: "CONNECT", CONNACK: "CONNACK", PUBLISH: "PUBLISH", PUBACK: "PUBACK",
         SUBSCRIBE: "SUBSCRIBE", SUBACK: "SUBACK", UNSUBSCRIBE: "UNSUBSCRIBE",
         UNSUBACK: "UNSUBACK", PINGREQ: "PINGREQ", PINGRESP: "PINGRESP",
         DISCONNECT: "DISCONNECT"}


def encode_remaining_length(n):
    out = bytearray()
    while True:
        b = n % 128
        n //= 128
        if n:
            b |= 0x80
        out.append(b)
        if not n:
            return bytes(out)


def encode_string(s):
    b = s.encode() if isinstance(s, str) else s
    return struct.pack("!H", len(b)) + b


class Client:
    def __init__(self, conn, addr):
        self.conn = conn
        self.addr = addr
        self.client_id = "?"
        self.subs = []
        self.lock = threading.Lock()

    def send_raw(self, data):
        with self.lock:
            try:
                self.conn.sendall(data)
            except OSError:
                pass

    def send_publish(self, topic, payload, qos=0, retain=False):
        flags = (qos & 0x03) << 1
        if retain:
            flags |= 0x01
        body = encode_string(topic) + (struct.pack("!H", 1) if qos else b"") + payload
        self.send_raw(bytes([(PUBLISH << 4) | flags]) + encode_remaining_length(len(body)) + body)

    def matches(self, topic):
        for f in self.subs:
            if f == "#" or f == topic:
                return True
            if f.endswith("/#") and topic.startswith(f[:-2]):
                return True
            if f.endswith("/+") and topic.count("/") == f.count("/") \
                    and topic.rsplit("/", 1)[0] == f[:-2]:
                return True
        return False


class Broker:
    def __init__(self, verbose=False):
        self.clients = []
        self.lock = threading.Lock()
        self.verbose = verbose
        self.retained = {}

    def log(self, *a):
        if self.verbose:
            print("[broker]", *a, flush=True)

    def publish(self, topic, payload, exclude=None, qos=0, retain=False):
        if retain:
            self.retained[topic] = payload
        with self.lock:
            targets = [c for c in self.clients if c.matches(topic)]
        for c in targets:
            c.send_publish(topic, payload, qos=0)
        self.log("PUBLISH ->", topic, payload[:120])

    def handle(self, cl, pkt_type, flags, payload):
        if pkt_type == CONNECT:
            i = 0
            vlen = struct.unpack_from("!H", payload, i)[0]; i += 2 + vlen
            lvl = payload[i]; i += 1
            cflags = payload[i]; i += 1
            keepalive = struct.unpack_from("!H", payload, i)[0]; i += 2
            clen = struct.unpack_from("!H", payload, i)[0]; i += 2
            cl.client_id = payload[i:i + clen].decode(errors="replace")
            self.log(f"CONNECT id={cl.client_id} keepalive={keepalive} clean={bool(cflags & 2)}")
            cl.send_raw(bytes([CONNACK << 4, 2, 0, 0]))

        elif pkt_type == PUBLISH:
            qos = (flags >> 1) & 0x03
            tlen = struct.unpack_from("!H", payload, 0)[0]
            topic = payload[2:2 + tlen].decode(errors="replace")
            i = 2 + tlen
            if qos:
                i += 2
            body = payload[i:]
            self.log(f"PUBLISH id={cl.client_id} topic={topic} qos={qos} len={len(body)}")
            if qos == 1:
                mid = struct.unpack_from("!H", payload, 2 + tlen)[0]
                cl.send_raw(bytes([PUBACK << 4, 2]) + struct.pack("!H", mid))
            self.publish(topic, body, exclude=cl, qos=qos)
            if self.verbose:
                print(f"    payload: {body[:200]!r}", flush=True)

        elif pkt_type == SUBSCRIBE:
            mid = struct.unpack_from("!H", payload, 0)[0]
            i, codes = 2, []
            while i < len(payload):
                tlen = struct.unpack_from("!H", payload, i)[0]
                i += 2
                topic = payload[i:i + tlen].decode(errors="replace")
                i += tlen
                qos = payload[i]; i += 1
                cl.subs.append(topic)
                codes.append(qos)
                self.log(f"SUBSCRIBE id={cl.client_id} topic={topic} qos={qos}")
                # deliver retained messages
                for t, p in list(self.retained.items()):
                    if cl.matches(t):
                        cl.send_publish(t, p)
            body = struct.pack("!H", mid) + bytes(codes)
            cl.send_raw(bytes([SUBACK << 4]) + encode_remaining_length(len(body)) + body)

        elif pkt_type == UNSUBSCRIBE:
            mid = struct.unpack_from("!H", payload, 0)[0]
            cl.send_raw(bytes([UNSUBACK << 4, 2]) + struct.pack("!H", mid))

        elif pkt_type == PINGREQ:
            cl.send_raw(bytes([PINGRESP << 4, 0]))
            self.log("PINGREQ from", cl.client_id)

        elif pkt_type == DISCONNECT:
            self.log("DISCONNECT", cl.client_id)
            cl.conn.close()

    def serve_client(self, conn, addr):
        cl = Client(conn, addr)
        with self.lock:
            self.clients.append(cl)
        self.log("client connected", addr)
        try:
            while True:
                header = conn.recv(1)
                if not header:
                    break
                pkt_type = header[0] >> 4
                flags = header[0] & 0x0F
                mult, value = 1, 0
                while True:
                    b = conn.recv(1)
                    if not b:
                        return
                    value += (b[0] & 127) * mult
                    if not b[0] & 128:
                        break
                    mult *= 128
                payload = b""
                while len(payload) < value:
                    chunk = conn.recv(value - len(payload))
                    if not chunk:
                        break
                    payload += chunk
                self.handle(cl, pkt_type, flags, payload)
        except OSError:
            pass
        finally:
            with self.lock:
                if cl in self.clients:
                    self.clients.remove(cl)
            self.log("client disconnected", addr, cl.client_id)
            try:
                conn.close()
            except OSError:
                pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    broker = Broker(verbose=args.verbose)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen(8)
    print(f"mini MQTT broker listening on {args.host}:{args.port}", flush=True)

    try:
        while True:
            conn, addr = srv.accept()
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=broker.serve_client, args=(conn, addr), daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()


if __name__ == "__main__":
    sys.exit(main())
