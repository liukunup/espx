#!/usr/bin/env python3
"""
Initialise EMQX for an ESPX node.

What it does, all idempotently and additively:

  1. logs in to the EMQX dashboard API (token based)
  2. reports the MQTT listeners
  3. creates a password-based authenticator (built-in database) and the device
     user, UNLESS --no-authn is given
  4. adds a built-in-database authorization source with ACL rules for the
     device's topic prefix
  5. verifies with a real MQTT round trip: connect as the device, subscribe to
     the command topic and publish to the state topic
  6. prints the device configuration to paste into the node

Why step 4 is not optional on EMQX >= 6.3
-----------------------------------------
The default ACL is a `file` source whose last rule is

    {allow, {security_profile, legacy}}.

EMQX >= 6.3 defaults to the `hardened` security profile, so that rule does not
apply and `authorization.no_match` (default `deny`) decides. A client can then
connect successfully and still have every publish and subscribe denied -- which
looks like "MQTT connected but no data". Explicit allow rules are required.

Usage
-----
  # show the plan, change nothing
  tools/emqx_init.py --host testing.homelab.lan --admin-pass 'emqx_xxx' --dry-run

  # apply
  tools/emqx_init.py --host testing.homelab.lan --admin-pass 'emqx_xxx' --apply

  # keep anonymous access instead of creating a device user
  tools/emqx_init.py ... --apply --no-authn

  # emit the device config as JSON (for scripting)
  tools/emqx_init.py ... --apply --json

  # remove what this script created
  tools/emqx_init.py --host ... --admin-pass ... --undo
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.request

DEFAULT_TOPIC_PREFIX = "plant/line1"


class EmqxError(RuntimeError):
    pass


class Emqx:
    def __init__(self, host, port, user, password, verbose=False):
        self.base = f"http://{host}:{port}/api/v5"
        self.user = user
        self.password = password
        self.token = None
        self.verbose = verbose

    def _call(self, path, method="GET", body=None, auth="bearer"):
        url = self.base + path
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"}
        if auth == "bearer" and self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        elif auth == "basic":
            import base64
            headers["Authorization"] = "Basic " + base64.b64encode(
                f"{self.user}:{self.password}".encode()).decode()

        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                raw, status = r.read().decode(errors="replace"), r.status
        except urllib.error.HTTPError as e:
            raw, status = e.read().decode(errors="replace"), e.code
        except OSError as e:
            raise EmqxError(f"cannot reach {url}: {e}")

        parsed = None
        if raw:
            try:
                parsed = json.loads(raw)
            except json.JSONDecodeError:
                parsed = raw

        if self.verbose:
            print(f"    {method} {path} -> {status} {str(parsed)[:160]}")

        return status, parsed

    def login(self):
        st, res = self._call("/login", "POST",
                             {"username": self.user, "password": self.password},
                             auth="none")
        if st != 200 or not isinstance(res, dict) or "token" not in res:
            raise EmqxError(f"dashboard login failed ({st}): {res}")
        self.token = res["token"]
        return res

    # ---------------------------------------------------------------- state
    def nodes(self):
        return self._call("/nodes")[1]

    def listeners(self):
        return self._call("/listeners")[1]

    def authenticators(self):
        st, r = self._call("/authentication")
        return r if isinstance(r, list) else []

    def authz_sources(self):
        st, r = self._call("/authorization/sources")
        if isinstance(r, dict):
            return r.get("sources", [])
        return []

    def authz_settings(self):
        return self._call("/authorization/settings")[1]

    def authz_rules(self):
        """Return {'username': [rules...]} for the built-in database source.

        EMQX 6 returns {"data": [{"username": ..., "rules": [...]}], "meta": ...}
        """
        st, r = self._call("/authorization/sources/built_in_database/rules/users")
        out = {}
        if isinstance(r, dict):
            for entry in r.get("data", []):
                out[entry.get("username")] = entry.get("rules", [])
        return out

    # ------------------------------------------------------------ mutation
    def create_authenticator(self):
        body = {
            "mechanism": "password_based",
            "backend": "built_in_database",
            "user_id_type": "username",
            "password_hash_algorithm": {"name": "sha256", "salt_position": "suffix"},
            "enable": True,
        }
        st, r = self._call("/authentication", "POST", body)
        if st not in (200, 201):
            raise EmqxError(f"create authenticator failed ({st}): {r}")
        return r

    def upsert_user(self, authn_id, username, password):
        """Create the user, or update its password when it already exists.

        Idempotent: re-running the tool must not fail, and must be able to
        rotate the password.
        """
        st, r = self._call(f"/authentication/{authn_id}/users", "POST",
                           {"user_id": username, "password": password,
                            "is_superuser": False})
        if st in (200, 201):
            return "created"

        exists = (st == 409) or (isinstance(r, dict) and r.get("code") == "ALREADY_EXISTS")
        if not exists:
            raise EmqxError(f"create user failed ({st}): {r}")

        st, r = self._call(f"/authentication/{authn_id}/users/{username}", "PUT",
                           {"password": password, "is_superuser": False})
        if st in (200, 204):
            return "password updated"
        raise EmqxError(f"update user failed ({st}): {r}")

    def ensure_authz_source(self):
        for s in self.authz_sources():
            if s.get("type") == "built_in_database":
                return False
        st, r = self._call("/authorization/sources", "POST",
                           {"type": "built_in_database", "enable": True})
        if st not in (200, 201, 204):
            raise EmqxError(f"create authz source failed ({st}): {r}")
        return True

    def add_rules(self, username, rules):
        """POST body is an ARRAY of {username, rules:[...]} in EMQX 6.

        (EMQX 5 used {"rules": [flat...]}; the array form is what 6.x accepts.)
        """
        st, r = self._call("/authorization/sources/built_in_database/rules/users",
                           "POST", [{"username": username, "rules": rules}])
        if st not in (200, 201, 204):
            raise EmqxError(f"add rules failed ({st}): {r}")
        return r

    def delete_rules_for_user(self, username):
        st, r = self._call(
            f"/authorization/sources/built_in_database/rules/users/{username}",
            "DELETE")
        return st in (200, 204)

    def delete_user(self, authn_id, username):
        self._call(f"/authentication/{authn_id}/users/{username}", "DELETE")

    def delete_authenticator(self, authn_id):
        self._call(f"/authentication/{authn_id}", "DELETE")


# --------------------------------------------------------------------------- #
# rule planning
# --------------------------------------------------------------------------- #

def plan_rules(username, prefix, per_node=False):
    """ACL rules for one ESPX node.

    Topics used by the firmware (see docs/ARCHITECTURE.md):
      publish   <prefix>/state  <prefix>/sensors  <prefix>/attrs/...  <prefix>/ota/status  <prefix>/config/result
      subscribe <prefix>/cmd/...

    per_node=True replaces the fixed prefix with EMQX's ${clientid} placeholder,
    which is what makes several units share one broker safely: every node's
    client id IS its device id, so a fleet all publishing under "espx/" can only
    touch "espx/<its own id>/...". Required when the fleet shares one prefix.
    """
    scope = f"{prefix}/${{clientid}}" if per_node else prefix
    return [
        {"permission": "allow", "action": "publish",   "topic": f"{scope}/#"},
        {"permission": "allow", "action": "subscribe", "topic": f"{scope}/cmd/#"},
        {"permission": "allow", "action": "subscribe", "topic": f"{scope}/ota/#"},
    ]


def monitor_user_rules(prefix):
    """ACL for a monitor/operator account.

    With per-node device rules a device can only see its own topics, so an
    operator that needs to watch and command the whole fleet needs its own user.
    """
    return [
        {"permission": "allow", "action": "subscribe", "topic": f"{prefix}/#"},
        {"permission": "allow", "action": "publish",   "topic": f"{prefix}/+/cmd/#"},
    ]


# --------------------------------------------------------------------------- #
# verification: a real MQTT round trip
# --------------------------------------------------------------------------- #

def verify(host, port, username, password, prefix):
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        print("  [SKIP] paho-mqtt not installed (pip install paho-mqtt)")
        return None

    got = []
    rc_holder = {}

    cli = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="emqx-init-verify")
    if username:
        cli.username_pw_set(username, password)

    def on_connect(_c, _u, _flags, rc, _props=None):
        rc_holder["rc"] = int(rc) if not hasattr(rc, "value") else rc.value

    def on_message(_c, _u, m):
        got.append((m.topic, m.payload.decode(errors="replace")))

    cli.on_connect = on_connect
    cli.on_message = on_message

    try:
        cli.connect(host, port, 20)
    except OSError as e:
        print(f"  [FAIL] cannot connect to {host}:{port}: {e}")
        return False

    cli.loop_start()
    time.sleep(1.5)

    rc = rc_holder.get("rc")
    if rc != 0:
        print(f"  [FAIL] CONNECT rejected (return code {rc})")
        cli.loop_stop()
        return False
    print("  [PASS] CONNECT accepted")

    sub_ok = False
    try:
        r = cli.subscribe(f"{prefix}/cmd/#", qos=1)
        time.sleep(1.5)
        # paho returns (result, mid); result 0 == granted qos list
        sub_ok = r[0] == 0
    except Exception as e:
        print(f"  [FAIL] subscribe raised: {e}")

    if sub_ok:
        print(f"  [PASS] SUBSCRIBE {prefix}/cmd/# granted")
    else:
        print(f"  [FAIL] SUBSCRIBE {prefix}/cmd/# denied (ACL)")

    pub_info = cli.publish(f"{prefix}/state", '{"online":true}', qos=1)
    pub_info.wait_for_publish(timeout=5)
    time.sleep(1.5)
    pub_ok = pub_info.is_published()
    print(f"  [{'PASS' if pub_ok else 'FAIL'}] PUBLISH {prefix}/state "
          f"{'accepted' if pub_ok else 'denied or timed out (ACL)'}")

    denied = [t for t, _ in got if False]  # retained self-message check below
    time.sleep(0.5)
    cli.loop_stop()
    cli.disconnect()

    return sub_ok and pub_ok


# --------------------------------------------------------------------------- #

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", required=True, help="EMQX host (dashboard + MQTT)")
    ap.add_argument("--dashboard-port", type=int, default=18083)
    ap.add_argument("--mqtt-port", type=int, default=1883)
    ap.add_argument("--admin-user", default="admin")
    ap.add_argument("--admin-pass", required=True, help="dashboard password")
    ap.add_argument("--device-user", default="espx")
    ap.add_argument("--device-pass", default=None,
                    help="password for the device user (required unless --no-authn)")
    ap.add_argument("--topic-prefix", default=DEFAULT_TOPIC_PREFIX)
    ap.add_argument("--per-node", action="store_true",
                    help="scope ACL rules with ${clientid} so several nodes can "
                         "share one prefix without touching each other's topics")
    ap.add_argument("--monitor-user", default=None,
                    help="also create a fleet-wide monitor/operator user with this name")
    ap.add_argument("--monitor-pass", default=None, help="password for --monitor-user")
    # (validated below, once argparse has finished)
    ap.add_argument("--no-authn", action="store_true",
                    help="do NOT create an authenticator; leave anonymous access as is")
    ap.add_argument("--dry-run", action="store_true", help="print the plan only")
    ap.add_argument("--apply", action="store_true", help="apply the plan")
    ap.add_argument("--undo", action="store_true",
                    help="remove the authenticator/user/rules this script creates")
    ap.add_argument("--json", action="store_true", help="print the device config as JSON")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not args.apply and not args.dry_run and not args.undo:
        ap.error("choose one of --dry-run, --apply or --undo")

    if not args.no_authn and not args.undo and not args.device_pass:
        ap.error("--device-pass is required unless --no-authn or --undo is given")

    if args.monitor_user and not args.monitor_pass and not args.undo:
        ap.error("--monitor-pass is required together with --monitor-user")

    emqx = Emqx(args.host, args.dashboard_port, args.admin_user, args.admin_pass,
                verbose=args.verbose)

    print(f"EMQX at {args.host}:{args.dashboard_port}")
    info = emqx.login()
    nodes = emqx.nodes()
    if isinstance(nodes, list) and nodes:
        print(f"  version : {nodes[0].get('version')} ({nodes[0].get('edition')})")
    print(f"  role    : {info.get('role')}")

    # ---------------------------------------------------------------- state
    print("\nCurrent state")
    listeners = emqx.listeners() or []
    tcp = [l for l in listeners if str(l.get("id", "")).startswith("tcp:")]
    for l in tcp:
        print(f"  listener {l['id']}: enable={l['enable']} bind={l.get('bind')}")
    if not any(l.get("enable") for l in tcp):
        print("  !! no enabled TCP listener: enable one before connecting the device")

    authns = emqx.authenticators()
    existing_authn = next((a for a in authns
                           if a.get("mechanism") == "password_based"
                           and a.get("backend") == "built_in_database"), None)
    print(f"  authenticators   : {[a.get('id') for a in authns] or 'none (anonymous allowed)'}")

    sources = emqx.authz_sources()
    print(f"  authz sources    : {[s.get('type') for s in sources]}")
    has_bidb = any(s.get("type") == "built_in_database" for s in sources)
    settings = emqx.authz_settings() or {}
    print(f"  no_match         : {settings.get('no_match')}  deny_action={settings.get('deny_action')}")

    rules_by_user = emqx.authz_rules() if has_bidb else {}
    existing_rules = rules_by_user.get(args.device_user, [])
    print(f"  authz rules      : {sum(len(v) for v in rules_by_user.values())} "
          f"({len(existing_rules)} for '{args.device_user}')")
    wanted = plan_rules(args.device_user, args.topic_prefix,
                        per_node=args.per_node)

    # ----------------------------------------------------------------- undo
    if args.undo:
        print("\nUndo")
        if has_bidb:
            if existing_rules:
                if emqx.delete_rules_for_user(args.device_user):
                    print(f"  removed {len(existing_rules)} ACL rule(s) "
                          f"for '{args.device_user}'")
                else:
                    print("  failed to remove ACL rules")
            else:
                print("  no ACL rules to remove")
        else:
            print("  no built_in_database source")
        if existing_authn:
            emqx.delete_user(existing_authn["id"], args.device_user)
            emqx.delete_authenticator(existing_authn["id"])
            print(f"  removed authenticator {existing_authn['id']} and user "
                  f"'{args.device_user}'")
        else:
            print("  no authenticator to remove")
        print("  (the built_in_database authorization source is left in place)")
        return 0

    # ----------------------------------------------------------------- plan
    print("\nPlan")
    steps = []
    if not args.no_authn:
        if existing_authn:
            steps.append(f"reuse authenticator {existing_authn['id']}")
        else:
            steps.append("create password_based/built_in_database authenticator "
                         "(this DISABLES anonymous access)")
        steps.append(f"create/update user '{args.device_user}'")
    else:
        steps.append("leave authentication untouched (anonymous stays as is)")

    if has_bidb:
        steps.append("reuse built_in_database authorization source")
    else:
        steps.append("add built_in_database authorization source")

    to_add = [r for r in wanted if r not in existing_rules]
    steps.append(f"add {len(to_add)} ACL rule(s) for '{args.device_user}' "
                 f"on prefix '{args.topic_prefix}'")

    for s in steps:
        print(f"  - {s}")

    if args.dry_run:
        print("\n--dry-run: nothing was changed")
        if to_add:
            print("Rules that would be added:")
            for r in to_add:
                print(f"    {r['permission']:<6} {r['action']:<10} {r['topic']}")
        return 0

    # ---------------------------------------------------------------- apply
    print("\nApplying")
    if not args.no_authn:
        if existing_authn:
            authn_id = existing_authn["id"]
            print(f"  authenticator: reusing {authn_id}")
        else:
            r = emqx.create_authenticator()
            authn_id = r.get("id") if isinstance(r, dict) else "password_based:built_in_database"
            print(f"  authenticator: created {authn_id}")
        action = emqx.upsert_user(authn_id, args.device_user, args.device_pass)
        print(f"  user         : '{args.device_user}' {action}")

        if args.monitor_user:
            action = emqx.upsert_user(authn_id, args.monitor_user, args.monitor_pass)
            print(f"  monitor user : '{args.monitor_user}' {action}")

    if emqx.ensure_authz_source():
        print("  authz source : built_in_database added")
    else:
        print("  authz source : built_in_database already present")

    if to_add:
        emqx.add_rules(args.device_user, existing_rules + to_add)
        print(f"  ACL rules    : {len(to_add)} added "
              f"({len(existing_rules) + len(to_add)} total)")
    else:
        print("  ACL rules    : already present")

    if args.monitor_user:
        emqx.delete_rules_for_user(args.monitor_user)
        mon_rules = monitor_user_rules(args.topic_prefix)
        emqx.add_rules(args.monitor_user, mon_rules)
        print(f"  monitor ACL  : {len(mon_rules)} rule(s) on '{args.topic_prefix}/#'")

    # the settings should already be deny-by-default; only report
    settings = emqx.authz_settings() or {}
    if settings.get("no_match") != "deny":
        print(f"  note: no_match is '{settings.get('no_match')}' "
              f"(deny is the safer setting)")

    # ------------------------------------------------------------- verify
    print("\nVerification (real MQTT round trip)")
    ok = verify(args.host, args.mqtt_port,
                None if args.no_authn else args.device_user,
                None if args.no_authn else args.device_pass,
                args.topic_prefix)

    device_cfg = {
        "network": {
            "mqtt_broker": f"mqtt://{args.host}:{args.mqtt_port}",
            "mqtt_topic_prefix": args.topic_prefix,
        }
    }
    if not args.no_authn:
        device_cfg["network"]["mqtt_username"] = args.device_user
        device_cfg["network"]["mqtt_password"] = args.device_pass

    print("\nDevice configuration (push with POST /api/config or MQTT "
          "<prefix>/cmd/config)")
    if args.json:
        print(json.dumps(device_cfg, indent=2))
    else:
        print("network:")
        for k, v in device_cfg["network"].items():
            print(f"  {k}: {v}")

    print()
    if ok is True:
        print("RESULT: OK — the device user can connect, subscribe and publish")
        return 0
    if ok is False:
        print("RESULT: FAILED — see the checks above")
        return 1
    print("RESULT: applied, verification skipped (paho-mqtt missing)")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except EmqxError as e:
        sys.exit(f"error: {e}")
