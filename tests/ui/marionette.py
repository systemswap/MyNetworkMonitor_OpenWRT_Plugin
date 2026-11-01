# SPDX-License-Identifier: GPL-2.0-only
"""Minimal client for the remote control built into Firefox (Marionette protocol).

Starts Firefox headless with its own profile in this folder, so neither the
screen nor the user's profile are touched. Clicks go through WebDriver
commands, which Firefox turns into real mouse events on the page (moving
there, pressing, releasing) - the page reacts as to a user.
"""
import base64
import json
import os
import shutil
import socket
import subprocess
import time

# Profile and screenshots go to the test folder outside the repository.
HERE = os.environ.get("MNM_UITEST_DIR", os.path.expanduser("~/.cache/mynetworkmonitor-openwrt-plugin-build/uitest"))
os.makedirs(HERE, exist_ok=True)
PORT = 28311

# The test profile sends nothing to Mozilla: no telemetry, reports, studies,
# updates, safe browsing lookups or captive portal checks.
PREFS = {
    "marionette.port": PORT,
    "toolkit.telemetry.enabled": False, "toolkit.telemetry.unified": False,
    "toolkit.telemetry.archive.enabled": False, "toolkit.telemetry.reportingpolicy.firstRun": False,
    "datareporting.healthreport.uploadEnabled": False, "datareporting.policy.dataSubmissionEnabled": False,
    "datareporting.policy.firstRunURL": "", "app.shield.optoutstudies.enabled": False,
    "app.normandy.enabled": False, "app.normandy.api_url": "", "app.update.auto": False,
    "app.update.enabled": False, "extensions.update.enabled": False, "extensions.getAddons.cache.enabled": False,
    "browser.safebrowsing.malware.enabled": False, "browser.safebrowsing.phishing.enabled": False,
    "browser.safebrowsing.downloads.enabled": False, "browser.safebrowsing.downloads.remote.enabled": False,
    "network.captive-portal-service.enabled": False, "network.connectivity-service.enabled": False,
    "browser.startup.page": 0, "browser.startup.homepage": "about:blank", "browser.newtabpage.enabled": False,
    "browser.newtabpage.activity-stream.feeds.telemetry": False, "browser.newtabpage.activity-stream.telemetry": False,
    "browser.ping-centre.telemetry": False, "browser.discovery.enabled": False, "browser.aboutwelcome.enabled": False,
    "services.settings.server": "http://127.0.0.1:9/", "media.gmp-manager.url": "http://127.0.0.1:9/",
    "browser.region.network.url": "", "geo.enabled": False, "dom.push.enabled": False,
    "network.dns.disablePrefetch": True, "network.prefetch-next": False, "intl.locale.requested": "en-US",
}


def eid(element):
    """The ID of an element reference (a dict with one W3C key)."""
    return next(iter(element.values()))


class Firefox:
    def __init__(self, width=1920, height=1080):
        self.profile = os.path.join(HERE, "profile")
        shutil.rmtree(self.profile, ignore_errors=True)
        os.makedirs(self.profile)
        with open(os.path.join(self.profile, "user.js"), "w") as f:
            for k, v in PREFS.items():
                f.write("user_pref(%s, %s);\n" % (json.dumps(k), json.dumps(v)))
        env = dict(os.environ, MOZ_HEADLESS="1", MOZ_CRASHREPORTER_DISABLE="1", MOZ_DISABLE_TELEMETRY="1")
        self.proc = subprocess.Popen(
            ["firefox", "--headless", "--marionette", "--no-remote", "--profile", self.profile, "about:blank"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
        self.sock = None
        for _ in range(100):
            try:
                self.sock = socket.create_connection(("127.0.0.1", PORT), timeout=2)
                break
            except OSError:
                time.sleep(0.2)
        if not self.sock:
            raise RuntimeError("Marionette did not start")
        self.sock.settimeout(60)
        self._buf = b""
        self._id = 0
        self._read()  # greeting
        self.cmd("WebDriver:NewSession", {"capabilities": {"alwaysMatch": {"acceptInsecureCerts": True}}})
        self.size(width, height)

    def _read(self):
        while b":" not in self._buf:
            self._buf += self.sock.recv(65536)
        n, rest = self._buf.split(b":", 1)
        n = int(n)
        while len(rest) < n:
            rest += self.sock.recv(65536)
        self._buf = rest[n:]
        return json.loads(rest[:n])

    def cmd(self, name, params=None):
        self._id += 1
        msg = json.dumps([0, self._id, name, params or {}]).encode()
        self.sock.sendall(str(len(msg)).encode() + b":" + msg)
        while True:
            r = self._read()
            if r[0] == 1 and r[1] == self._id:
                if r[2]:
                    raise RuntimeError("%s: %s" % (name, r[2].get("message", r[2])))
                return r[3]

    def size(self, width, height):
        self.cmd("WebDriver:SetWindowRect", {"width": width, "height": height})

    def go(self, url):
        self.cmd("WebDriver:Navigate", {"url": url})

    def find(self, css):
        r = self.cmd("WebDriver:FindElement", {"using": "css selector", "value": css})
        return r["value"]

    def find_all(self, css):
        return self.cmd("WebDriver:FindElements", {"using": "css selector", "value": css})

    def click(self, element):
        """Scrolls the element into view and clicks its centre with the mouse."""
        self.cmd("WebDriver:ElementClick", {"id": eid(element)})

    def slow_click(self, element, hold_ms):
        """Presses the mouse button on the element, holds it, then releases - like a user who clicks slowly."""
        self.cmd("WebDriver:PerformActions", {"actions": [{
            "type": "pointer", "id": "mouse", "parameters": {"pointerType": "mouse"},
            "actions": [
                {"type": "pointerMove", "origin": {"element-6066-11e4-a52e-4f735466cecf": eid(element)}, "x": 0, "y": 0},
                {"type": "pointerDown", "button": 0},
                {"type": "pause", "duration": hold_ms},
                {"type": "pointerUp", "button": 0},
            ]}]})
        self.cmd("WebDriver:ReleaseActions")

    def type(self, element, text):
        self.cmd("WebDriver:ElementClear", {"id": eid(element)})
        self.cmd("WebDriver:ElementSendKeys", {"id": eid(element), "text": text})

    def js(self, script, *args):
        return self.cmd("WebDriver:ExecuteScript", {"script": script, "args": list(args)})["value"]

    def wait(self, script, timeout=15):
        end = time.time() + timeout
        while time.time() < end:
            if self.js(script):
                return True
            time.sleep(0.3)
        return False

    def shot(self, name, full=False):
        r = self.cmd("WebDriver:TakeScreenshot", {"full": full, "hash": False})
        path = os.path.join(HERE, name)
        with open(path, "wb") as f:
            f.write(base64.b64decode(r["value"]))
        return path

    def quit(self):
        try:
            self.cmd("Marionette:Quit", {"flags": ["eForceQuit"]})
        except Exception:
            pass
        try:
            self.proc.wait(10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        shutil.rmtree(self.profile, ignore_errors=True)
