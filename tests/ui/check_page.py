# SPDX-License-Identifier: GPL-2.0-only
"""Uses the MyNetworkMonitor page in LuCI as a user would, with the mouse, and
reports what does not work, at 1920x1080 and 1366x768. Nothing is saved on the
router: only "Start scan" stores settings, and the test never presses it.
Screenshots go to the test folder (see marionette.py).

    python3 check_page.py <router address> <file with the root password>
"""
import sys

from marionette import Firefox

router, pwfile = sys.argv[1], sys.argv[2]
password = open(pwfile).read().strip()
base = "http://%s/cgi-bin/luci" % router
problems = []


def check(ok, text):
    print(("ok   " if ok else "FAIL ") + text)
    if not ok:
        problems.append(text)


for width, height in [(1920, 1080), (1366, 768)]:
    print("== %dx%d" % (width, height))
    ff = Firefox(width, height)
    try:
        ff.go(base + "/")
        ff.wait("const i=document.querySelector('input[name=luci_password]'); return i && i.getClientRects().length > 0")
        print("     login page:", ff.js("const i=document.querySelector('input[name=luci_password]');"
                                     "const r=i.getBoundingClientRect(); return [r.x, r.y, r.width, r.height, window.innerWidth, window.innerHeight]"))
        ff.type(ff.find("input[name=luci_password]"), password)
        ff.click(ff.find("button.cbi-button-positive, .btn.cbi-button-positive, button[type=submit]"))
        ff.wait("return !!document.querySelector('#maincontent')")
        ff.go(base + "/admin/network/mynetworkmonitor")
        check(ff.wait("return !!document.querySelector('#mnm-table table')"), "page loads with the device table")

        overflow = ff.js("return document.documentElement.scrollWidth - document.documentElement.clientWidth")
        check(overflow <= 0, "no horizontal scrolling of the page (overflow %d px)" % overflow)
        table_over = ff.js("const b=document.getElementById('mnm-table'); return b.scrollWidth - b.clientWidth")
        check(table_over <= 0, "device table fits its area (overflow %d px)" % table_over)
        details_visible = ff.js("const r=document.getElementById('mnm-details').getBoundingClientRect();"
                                "return r.width > 0 && r.right <= window.innerWidth")
        check(details_visible, "details area fully inside the window")
        ff.shot("page-%dx%d.png" % (width, height))

        # Settings open/close
        summary = ff.find("details.cbi-section > summary")
        is_open = ff.js("return document.querySelector('details.cbi-section').open")
        if not is_open:
            ff.click(summary)
        check(ff.js("return document.querySelector('details.cbi-section').open"), "settings open on click")

        # Each group opens and closes with its button
        toggles = ff.find_all(".mnm-group-head button")
        check(len(toggles) == 9, "nine groups (%d)" % len(toggles))
        for i, t in enumerate(toggles):
            ff.click(t)
            opened = ff.js("return !document.querySelectorAll('.mnm-group-body')[arguments[0]].hidden", i)
            ff.click(t)
            closed = ff.js("return document.querySelectorAll('.mnm-group-body')[arguments[0]].hidden", i)
            check(opened and closed, "group %d opens and closes" % (i + 1))
        cols = ff.js("const g=[...document.querySelectorAll('.mnm-group')].map(e=>Math.round(e.getBoundingClientRect().top));"
                     "return g.filter(t=>t==g[0]).length")
        check(cols == 3 or width < 900, "three groups side by side (%d)" % cols)

        # Every service tick can be switched on and off by clicking its label
        for t in toggles:
            ff.click(t)
        labels = ff.find_all(".mnm-group-body label")
        before = ff.js("return [...document.querySelectorAll('.mnm-svc')].map(c=>c.checked)")
        failed = 0
        for i, label in enumerate(labels):
            ff.click(label)
            after = ff.js("return document.querySelectorAll('.mnm-svc')[arguments[0]].checked", i)
            ff.click(label)
            back = ff.js("return document.querySelectorAll('.mnm-svc')[arguments[0]].checked", i)
            if after == before[i] or back != before[i]:
                failed += 1
                print("     service %d does not switch" % i)
        check(failed == 0, "all %d services switch on and off" % len(labels))

        # Group tick switches its whole group and shows the count
        box = ff.find_all(".mnm-group-box")[0]
        ff.click(box)
        state = ff.js("const g=document.querySelector('.mnm-group-box').dataset.group;"
                      "return [...document.querySelectorAll('.mnm-svc[data-group=\"'+g+'\"]')].map(c=>c.checked)")
        all_same = len(set(state)) == 1
        ff.click(box)
        check(all_same, "group tick switches all services of the group")
        ff.shot("settings-%dx%d.png" % (width, height), full=True)

        version = ff.js("const s=document.querySelector('h2 small'); return s ? s.textContent : ''")
        check(version.startswith("v0."), "version behind the name (%s)" % version)

        # Selecting a device shows its details - also with a slow click held over
        # one refresh of the table (refresh every second).
        ff.js("window.scrollTo(0, 0)")                         # back to the top, as a user would
        ff.click(ff.find("details.cbi-section > summary"))   # fold the settings away again
        rows = ff.find_all("tr.mnm-row")
        check(len(rows) > 0, "devices in the table (%d)" % len(rows))
        if rows:
            ff.click(ff.find_all("tr.mnm-row td:nth-child(2)")[0])
            check(ff.wait("return !!document.querySelector('#mnm-details h3')", 5), "click on a row shows its details")
            if len(rows) > 1:
                ff.slow_click(ff.find_all("tr.mnm-row td:nth-child(2)")[1], 1500)
                check(ff.wait("return document.querySelectorAll('tr.mnm-row')[1].classList.contains('selected')", 3),
                      "slow click (1.5 s held) selects the row")
            button = ff.find("#mnm-details button")
            check(ff.js("return !!document.querySelector('#mnm-details button')"), "details have the scan button")
            # No object turned into text anywhere on the page ("[object HTMLElement]").
            for i in range(len(rows)):
                ff.click(ff.find_all("tr.mnm-row td:nth-child(2)")[i])
                ff.wait("return true", 0.3)
                if "[object" in ff.js("return document.getElementById('mnm-details').textContent"):
                    break
            junk = ff.js("return document.body.textContent.includes('[object')")
            check(not junk, "no '[object ...]' text in table or details")
            stable = ff.js("const b=document.querySelector('#mnm-details button');"
                           "return new Promise(r=>setTimeout(()=>r(b.isConnected), 2500))")
            check(stable, "details button stays the same element over refreshes")
            ff.shot("details-%dx%d.png" % (width, height))

            # Scrolling the table keeps the details, and the device name, below the header bar.
            ff.js("window.scrollTo(0, 700)")
            ff.wait("return true", 1)
            gap = ff.js("const hd=document.querySelector('header'), t=document.querySelector('#mnm-details h3');"
                        "return Math.round(t.getBoundingClientRect().top - (hd ? hd.getBoundingClientRect().bottom : 0))")
            check(gap >= 0, "device name stays visible below the header while scrolling (%d px)" % gap)
            bottom = ff.js("return Math.round(document.getElementById('mnm-details').getBoundingClientRect().bottom - innerHeight)")
            check(bottom <= 0, "details end inside the window while scrolling (%d px)" % bottom)
            ff.shot("scrolled-%dx%d.png" % (width, height))
            ff.js("window.scrollTo(0, 0)")

        # Filters above the table, typed and clicked as a user would
        count = "return document.querySelectorAll('tr.mnm-row').length"
        n_all = ff.js(count)
        if n_all:
            ip = ff.js("return document.querySelector('tr.mnm-row td:nth-child(3)').textContent.trim()")
            ff.click(ff.find("#mnm-search"))
            ff.type(ff.find("#mnm-search"), ip)
            ff.wait("return true", 0.3)
            shown = ff.js("return [...document.querySelectorAll('tr.mnm-row')].map(r=>r.textContent.toLowerCase())")
            check(len(shown) >= 1 and all(ip in t for t in shown), "search field shows only matching devices (%d of %d)" % (len(shown), n_all))
            ff.wait("return true", 2.5)
            check(ff.js(count) == len(shown) and ff.js("return document.getElementById('mnm-search').value") == ip,
                  "filter stays over refreshes")
            ff.type(ff.find("#mnm-search2"), "zz-no-such-device-zz")
            ff.wait("return true", 0.3)
            check(ff.js(count) == 0 and "No device matches" in ff.js("return document.getElementById('mnm-table').textContent"),
                  "second field narrows down, with a note when nothing matches")
            ff.click(ff.find("#mnm-reset-filters"))
            ff.wait("return true", 0.3)
            check(ff.js(count) == n_all and ff.js("return document.getElementById('mnm-search').value") == "",
                  "reset filters shows all devices again")
            port = ff.js("for (const td of document.querySelectorAll('tr.mnm-row td:nth-child(7)')) {"
                         "const m = td.textContent.match(/\\d+/); if (m) return m[0]; } return null")
            if port:
                ff.type(ff.find("#mnm-port"), port)
                ff.wait("return true", 0.3)
                cells = ff.js("return [...document.querySelectorAll('tr.mnm-row td:nth-child(7)')].map(td=>td.textContent)")
                check(len(cells) >= 1 and all(port in [x.strip() for x in c.replace("\n", ",").split(",")] for c in cells),
                      "port field shows only devices with that open port (%d, port %s)" % (len(cells), port))
                ff.type(ff.find("#mnm-port"), "80-")
                ff.wait("return true", 0.3)
                check(ff.js(count) == n_all and ff.js("return document.getElementById('mnm-port').classList.contains('mnm-invalid')"),
                      "unfinished port list is marked and filters nothing")
            ff.click(ff.find("#mnm-online"))
            ff.wait("return true", 0.3)
            offline = ff.js("return document.querySelectorAll('tr.mnm-row .mnm-dot:not(.online)').length")
            check(offline == 0, "only reachable hides devices not seen in the last scan")
            ff.click(ff.find("#mnm-reset-filters"))
            ff.wait("return true", 0.3)
            check(ff.js(count) == n_all and not ff.js("return document.getElementById('mnm-online').checked"),
                  "reset filters clears all fields")

        # At most four services side by side, ports in at most two lines
        widest = ff.js("let m=0; document.querySelectorAll('.mnm-chips').forEach(c=>{const t=[...c.children].map(x=>Math.round(x.getBoundingClientRect().top));"
                       "const n=t.filter(v=>v==t[0]).length; if(n>m) m=n}); return m")
        check(widest <= 4, "at most four services in a row (%d)" % widest)
        lines = ff.js("let m=0; document.querySelectorAll('tr.mnm-row td:nth-child(7)').forEach(td=>{m=Math.max(m,td.querySelectorAll('br').length+1)}); return m")
        check(lines <= 2, "ports in at most two lines (%d)" % lines)
    finally:
        ff.quit()

print("\n%d problem(s)" % len(problems))
sys.exit(1 if problems else 0)
