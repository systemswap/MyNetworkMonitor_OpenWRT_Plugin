# SPDX-License-Identifier: GPL-2.0-only
"""Checks how the backend on a router merges finished scans into the stored
device list (docs/FEHLERGEDAECHTNIS.md, No. 15): made-up scans are written
where the scanner writes its result, the backend is asked for the status, and
the stored list is compared. The stored list and the last scan of the router
are saved before and put back afterwards. Addresses from the documentation
range, made-up MAC addresses.

    python3 check_merge.py <router address> [ssh options ...]
"""
import json
import subprocess
import sys

router, ssh_options = sys.argv[1], sys.argv[2:]
problems = []


def remote(command, stdin=None):
    return subprocess.run(["ssh", *ssh_options, "-o", "BatchMode=yes", "root@" + router, command],
                          input=stdin, capture_output=True, text=True, check=True).stdout


def check(ok, text):
    print(("ok   " if ok else "FAIL ") + text)
    if not ok:
        problems.append(text)


def device(last, mac):
    return {"ipv4": "192.0.2.%d" % last, "mac": mac, "seen_by": ["arp"], "tcp": [], "ipv6": [],
            "ports_scanned": False, "services_checked": False, "services": []}


def merge(started, targets, devices):
    scan = {"format": 2, "version": "test", "state": "done", "phase": "finished", "started": started,
            "finished": started + 1, "done": 0, "total": 0, "targets": targets, "ports": "", "devices": devices}
    remote("cat > /tmp/mynetworkmonitor/scan.json", json.dumps(scan))
    status = json.loads(remote("ubus call luci.mynetworkmonitor status"))
    return sorted((d["ipv4"], d.get("mac")) for d in status["devices"])


AA, BB, CC = "02:00:00:00:00:aa", "02:00:00:00:00:bb", "02:00:00:00:00:cc"
store = "/etc/mynetworkmonitor" if remote("uci -q get mynetworkmonitor.scan.persist || true").strip() == "1" \
    else "/tmp/mynetworkmonitor/store"
remote("mkdir -p /tmp/mnm-test-backup && cp -p %s/devices.json /tmp/mnm-test-backup/ 2>/dev/null; "
       "cp -p /tmp/mynetworkmonitor/scan.json /tmp/mnm-test-backup/ 2>/dev/null; true" % store)
try:
    remote("mkdir -p %s && echo '{\"format\":1,\"devices\":[],\"merged_scan\":0,\"last_scan\":0}' > %s/devices.json"
           % (store, store))

    # A router with two addresses and a device behind it (a repeater answers
    # ARP with its own MAC address), and one device of its own.
    got = merge(1000, "192.0.2.0/24", [device(1, AA), device(207, AA), device(10, AA), device(20, BB)])
    check(got == [("192.0.2.1", AA), ("192.0.2.10", AA), ("192.0.2.20", BB), ("192.0.2.207", AA)],
          "several addresses with one MAC address stay separate rows (%d)" % len(got))

    # The device of its own gets a new address by DHCP: its row moves.
    got = merge(2000, "192.0.2.0/24", [device(1, AA), device(207, AA), device(10, AA), device(21, BB)])
    check(("192.0.2.21", BB) in got and ("192.0.2.20", BB) not in got and len(got) == 4,
          "a device that changed its address keeps its row")

    # A single-device scan cannot see whether the MAC address is shared: no move.
    got = merge(3000, "192.0.2.30", [device(30, BB)])
    check(("192.0.2.30", BB) in got and ("192.0.2.21", BB) in got and len(got) == 5,
          "a single-device scan does not move a row")

    # Another device now uses an old address: a new row, the old one stays.
    got = merge(4000, "192.0.2.0/24", [device(1, AA), device(207, AA), device(10, AA), device(21, CC)])
    check(("192.0.2.21", CC) in got and ("192.0.2.21", BB) in got and len(got) == 6,
          "a new device on an old address gets its own row")
finally:
    remote("if [ -f /tmp/mnm-test-backup/devices.json ]; then cp -p /tmp/mnm-test-backup/devices.json %s/; "
           "else rm -f %s/devices.json; fi; "
           "if [ -f /tmp/mnm-test-backup/scan.json ]; then cp -p /tmp/mnm-test-backup/scan.json /tmp/mynetworkmonitor/; "
           "else rm -f /tmp/mynetworkmonitor/scan.json; fi; rm -rf /tmp/mnm-test-backup" % (store, store))
    print("stored list of the router put back")

print("\n%d problem(s)" % len(problems))
sys.exit(1 if problems else 0)
