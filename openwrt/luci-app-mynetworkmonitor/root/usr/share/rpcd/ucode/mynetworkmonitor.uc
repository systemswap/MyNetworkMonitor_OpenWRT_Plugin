// SPDX-License-Identifier: GPL-2.0-only
//
// rpcd backend of the LuCI page, object "luci.mynetworkmonitor"; who may call
// what is set in /usr/share/rpcd/acl.d/luci-app-mynetworkmonitor.json.
//
// - start/stop run the scanner as a separate background process, so a scan
//   never blocks rpcd. All values are checked here before they reach a shell
//   command line, and the scanner checks them again.
// - The scanner writes the devices of the running scan to scan.json. This
//   backend keeps the device list over all scans and merges each finished scan
//   into it once (merge()); while a scan runs, status() returns the list with
//   the running scan merged in, without saving it.
// - The device list and the vendor list lie in RAM (/tmp) by default; with the
//   option "persist" on the flash, which is then written once per scan only.

'use strict';

import { readfile, writefile, unlink, mkdir, stat, rename } from 'fs';
import { cursor } from 'uci';
import * as ubus from 'ubus';

const SCANNER = '/usr/bin/mynetworkmonitor-scan';
const WORK = '/tmp/mynetworkmonitor';
const PIDFILE = WORK + '/scan.pid';
const SCAN = WORK + '/scan.json';
const ERRORS = WORK + '/error.txt';
const VENDOR_PIDFILE = WORK + '/vendors.pid';
const RAM_STORE = WORK + '/store';
const FLASH_STORE = '/etc/mynetworkmonitor';
const VENDOR_URL = 'https://www.wireshark.org/download/automated/data/manuf';
const CATALOG = '/usr/share/mynetworkmonitor/services.json';

const STORE_FORMAT = 1;
const SCAN_FORMAT = 2;

// Only the characters the scanner accepts; nothing a shell would interpret.
// The length is checked apart from the pattern: musl allows repetition counts
// only up to 255 (RE_DUP_MAX), "{1,1024}" would make the whole file invalid.
const RANGES_PATTERN = /^[0-9.,\/ -]*$/;
const PORTS_PATTERN = /^[0-9, -]*$/;
const NAME_PATTERN = /^[A-Za-z0-9_.-]+$/;
const IPV4_PATTERN = /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/;
const MAX_LENGTH = 1024;

function shell_quote(text) {
	return "'" + replace(text, "'", "'\\''") + "'";
}

function read_json(path) {
	const text = readfile(path);
	if (text == null)
		return null;
	try {
		return json(text);
	}
	catch (e) {
		return null;
	}
}

// Writes under a temporary name and renames, so that a reader never sees half a file.
function write_json(path, value) {
	writefile(path + '.tmp', sprintf('%J', value));
	return rename(path + '.tmp', path);
}

// The process ID behind a pid file, or null. Only trusted if the process with
// that ID really is the expected program: IDs are reused once it has ended.
function running_pid(pidfile, name) {
	const pid = +trim(readfile(pidfile) ?? '');
	if (!(pid > 1))
		return null;
	const cmdline = readfile(sprintf('/proc/%d/cmdline', pid));
	return (cmdline != null && index(cmdline, name) >= 0) ? pid : null;
}

function int_option(value, min, max, fallback) {
	const n = +value;
	return (type(n) == 'int' && n >= min && n <= max) ? n : fallback;
}

function config() {
	const uci = cursor();
	uci.load('mynetworkmonitor');
	return uci;
}

function store_dir() {
	return (config().get('mynetworkmonitor', 'scan', 'persist') == '1') ? FLASH_STORE : RAM_STORE;
}

function load_store() {
	const store = read_json(store_dir() + '/devices.json');
	return (store != null && store.format == STORE_FORMAT && type(store.devices) == 'array')
		? store : { format: STORE_FORMAT, devices: [], merged_scan: 0, last_scan: 0 };
}

function save_store(store) {
	const dir = store_dir();
	mkdir(WORK, 0700);
	mkdir(dir, 0700);
	return write_json(dir + '/devices.json', store);
}

function load_scan() {
	const scan = read_json(SCAN);
	return (scan != null && scan.format == SCAN_FORMAT && type(scan.devices) == 'array') ? scan : null;
}

// ---------------------------------------------------------------- merging

function union(a, b, limit) {
	const out = [ ...(a ?? []) ];
	for (let x in (b ?? []))
		if (index(out, x) < 0 && (limit == null || length(out) < limit))
			push(out, x);
	return out;
}

// The stored device a scanned one belongs to: same MAC address, or the same
// IPv4 address for an entry that has no MAC address yet (seen by ping only).
function find_device(devices, d) {
	for (let s in devices)
		if (d.mac != null && s.mac == d.mac)
			return s;
	for (let s in devices)
		if (s.ipv4 == d.ipv4 && (s.mac == null || d.mac == null))
			return s;
	return null;
}

// Merges one scan into a copy of the list. "when" is the time the devices were seen.
function merge(store, scan, when) {
	const devices = map(store.devices, s => ({ ...s }));
	for (let d in scan.devices) {
		let s = find_device(devices, d);
		if (s == null) {
			s = { first_seen: when, tcp: [], ipv6: [], seen_by: [] };
			push(devices, s);
		}
		s.ipv4 = d.ipv4;
		if (d.mac != null) s.mac = d.mac;
		if (d.hostname != null) s.hostname = d.hostname;
		if (d.vendor != null) s.vendor = d.vendor;
		if (d.ttl != null) { s.ttl = d.ttl; s.os_guess = d.os_guess; }
		s.ipv6 = union(s.ipv6, d.ipv6, 8);
		s.seen_by = union(s.seen_by, d.seen_by);
		// A finished port scan of this device replaces the old ports; an
		// interrupted one can only add what it found.
		if (d.ports_scanned) {
			s.tcp = d.tcp;
			s.ports_checked = when;
			s.ports_list = scan.ports;
		}
		else {
			s.tcp = sort(union(s.tcp, d.tcp), (a, b) => a - b);
		}
		// Recognised services: a finished probe run replaces them, an
		// interrupted one adds what it found.
		if (d.services_checked)
			s.services = d.services ?? [];
		else {
			const all = [ ...(s.services ?? []) ];
			for (let x in (d.services ?? []))
				if (length(filter(all, y => y.id == x.id && y.port == x.port)) == 0)
					push(all, x);
			s.services = all;
		}
		s.last_seen = when;
	}
	return { ...store, devices: devices };
}

// Whether a scan covered only one device ("Scan this device"). Such a scan
// must not decide which devices count as online: all others were not asked
// (docs/FEHLERGEDAECHTNIS.md, No. 9).
function single_device(scan) {
	return match(scan.targets ?? '', IPV4_PATTERN) != null;
}

// Merges a finished scan into the stored list, once per scan.
function merge_finished_scan() {
	const scan = load_scan();
	if (scan == null || scan.state == 'running' || running_pid(PIDFILE, 'mynetworkmonitor-scan') != null)
		return;
	const store = load_store();
	if (store.merged_scan == scan.started)
		return;
	const merged = merge(store, scan, scan.finished || scan.started);
	merged.merged_scan = scan.started;
	if (!single_device(scan))
		merged.last_scan = scan.started;
	save_store(merged);
}

// ---------------------------------------------------------------- catalog

function load_catalog() {
	const c = read_json(CATALOG);
	return (c != null && c.format == 1 && type(c.groups) == 'array') ? c : { format: 1, groups: [] };
}

// The TCP ports of the chosen services, ascending, without duplicates.
function ports_of(catalog, chosen) {
	const ports = [];
	for (let g in catalog.groups)
		for (let item in g.items)
			if (index(chosen, item.id) >= 0)
				for (let p in (item.tcp ?? []))
					if (index(ports, p) < 0)
						push(ports, p);
	return sort(ports, (a, b) => a - b);
}

function known_ids(catalog) {
	const ids = [];
	for (let g in catalog.groups)
		for (let item in g.items)
			push(ids, item.id);
	return ids;
}

// ---------------------------------------------------------------- networks

function ipv4_value(text) {
	const p = split(text, '.');
	return (((+p[0]) << 24) | ((+p[1]) << 16) | ((+p[2]) << 8) | (+p[3])) & 0xffffffff;
}

function ipv4_text(v) {
	return sprintf('%d.%d.%d.%d', (v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255);
}

function is_private(v) {
	return ((v >> 24) & 255) == 10 || (((v >> 24) & 255) == 172 && ((v >> 16) & 0xf0) == 16)
		|| (((v >> 24) & 255) == 192 && ((v >> 16) & 255) == 168);
}

// The private IPv4 networks of the router, one entry per interface and address.
function router_networks() {
	const conn = ubus.connect();
	const dump = conn ? conn.call('network.interface', 'dump', {}) : null;
	const nets = [];
	for (let iface in (dump?.interface ?? [])) {
		if (iface.interface == 'loopback' || !iface.up)
			continue;
		for (let a in (iface['ipv4-address'] ?? [])) {
			const v = ipv4_value(a.address);
			if (!is_private(v) || a.mask < 16 || a.mask > 30)
				continue;
			const mask = (0xffffffff << (32 - a.mask)) & 0xffffffff;
			push(nets, {
				name: iface.interface,
				device: iface.l3_device ?? iface.device,
				address: a.address,
				first: (v & mask) + 1,
				last: (v | (~mask & 0xffffffff)) - 1,
				cidr: sprintf('%s/%d', ipv4_text(v & mask), a.mask)
			});
		}
	}
	return nets;
}

// The name of the router network an address belongs to, for the table.
function network_of(nets, address) {
	if (address == null)
		return null;
	const v = ipv4_value(address);
	for (let n in nets)
		if (v >= n.first && v <= n.last)
			return n.name;
	return null;
}

// ---------------------------------------------------------------- vendors

function vendor_info(dir) {
	const st = stat(dir + '/manuf');
	return {
		present: st != null,
		size: st?.size ?? 0,
		updated: st?.mtime ?? 0,
		updating: running_pid(VENDOR_PIDFILE, 'uclient-fetch') != null
	};
}

// ---------------------------------------------------------------- methods

const methods = {
	status: {
		call: function() {
			merge_finished_scan();
			const running = running_pid(PIDFILE, 'mynetworkmonitor-scan') != null;
			const scan = load_scan();
			let store = load_store();
			// While a scan runs, its devices are shown merged in, without saving.
			// The start of the scan is their time: with the current time every
			// device of the scan would change every second, and the page would
			// rebuild all its rows (docs/FEHLERGEDAECHTNIS.md, No. 11).
			if (running && scan != null)
				store = merge(store, scan, scan.started);
			const nets = router_networks();
			for (let d in store.devices)
				d.network = network_of(nets, d.ipv4);
			const error = running ? null : trim(readfile(ERRORS) ?? '');
			return {
				running: running,
				scan: scan ? { state: scan.state, phase: scan.phase, done: scan.done, total: scan.total,
					started: scan.started, finished: scan.finished, targets: scan.targets } : null,
				error: (error != null && length(error) > 0) ? error : null,
				last_scan: (running && scan && !single_device(scan)) ? scan.started : store.last_scan,
				persist: store_dir() == FLASH_STORE,
				version: trim(readfile('/usr/share/mynetworkmonitor/version') ?? ''),
				devices: store.devices,
				vendors: vendor_info(store_dir())
			};
		}
	},

	catalog: {
		call: function() {
			return load_catalog();
		}
	},

	networks: {
		call: function() {
			return { networks: map(router_networks(), n => ({ name: n.name, device: n.device, address: n.address, cidr: n.cidr })) };
		}
	},

	// Starts a scan. The page sends the values of its form; they are checked,
	// stored in UCI (so the page shows them next time) and used for this scan.
	// With "target" set, only that one device is scanned and the stored
	// settings stay as they are.
	start: {
		args: { networks: [ 'lan' ], ranges: '', services: [ 'ping' ], extra_ports: '', timeout_ms: '800', parallel: '64', target: '' },
		call: function(req) {
			if (running_pid(PIDFILE, 'mynetworkmonitor-scan') != null)
				return { error: 'A scan is already running.' };
			merge_finished_scan();

			const target = trim('' + (req.args.target ?? ''));
			const ranges = trim('' + (req.args.ranges ?? ''));
			const extra_ports = trim('' + (req.args.extra_ports ?? ''));
			const services = (type(req.args.services) == 'array') ? req.args.services : [];
			const timeout = int_option(req.args.timeout_ms, 50, 10000, 800);
			const parallel = int_option(req.args.parallel, 1, 256, 64);
			const wanted = (type(req.args.networks) == 'array') ? req.args.networks : [];

			if (length(ranges) > MAX_LENGTH || length(extra_ports) > MAX_LENGTH)
				return { error: 'The list of ranges or ports is too long.' };
			if (!match(ranges, RANGES_PATTERN))
				return { error: 'Own ranges may only contain addresses, ranges (a-b) and networks (a/n), separated by commas.' };
			if (!match(extra_ports, PORTS_PATTERN))
				return { error: 'Ports may only contain numbers and ranges (a-b), separated by commas.' };
			for (let n in wanted)
				if (type(n) != 'string' || !match(n, NAME_PATTERN))
					return { error: 'Invalid network name.' };
			const catalog = load_catalog();
			const known = known_ids(catalog);
			for (let s in services)
				if (type(s) != 'string' || index(known, s) < 0)
					return { error: 'Unknown service: ' + s };

			// The ports of the chosen services plus the additional ones; the scanner
			// merges duplicates and overlapping ranges itself.
			const parts = map(ports_of(catalog, services), p => '' + p);
			if (length(extra_ports) > 0)
				push(parts, extra_ports);
			const ports = join(',', parts);

			// The probes of each chosen service ("probes" in the catalog), on the
			// ports of that service.
			const probes = [];
			for (let g in catalog.groups)
				for (let item in g.items)
					if (index(services, item.id) >= 0 && length(item.tcp ?? []) > 0)
						for (let id in (item.probes ?? []))
							push(probes, sprintf('%s=%s', id, join(',', map(item.tcp, p => '' + p))));

			let targets;
			if (length(target) > 0) {
				if (!match(target, IPV4_PATTERN))
					return { error: 'Invalid device address.' };
				targets = target;
			}
			else {
				const nets = [];
				for (let n in router_networks())
					if (index(wanted, n.name) >= 0)
						push(nets, n.cidr);
				if (length(ranges) > 0)
					push(nets, ranges);
				if (length(nets) == 0)
					return { error: 'Choose at least one network or enter own ranges.' };
				targets = join(',', nets);

				const uci = config();
				uci.set('mynetworkmonitor', 'scan', 'networks', wanted);
				uci.set('mynetworkmonitor', 'scan', 'ranges', ranges);
				uci.set('mynetworkmonitor', 'scan', 'services', services);
				uci.set('mynetworkmonitor', 'scan', 'services_chosen', '1');
				uci.set('mynetworkmonitor', 'scan', 'extra_ports', extra_ports);
				uci.set('mynetworkmonitor', 'scan', 'timeout_ms', '' + timeout);
				uci.set('mynetworkmonitor', 'scan', 'parallel', '' + parallel);
				uci.commit('mynetworkmonitor');
			}

			mkdir(WORK, 0700);
			unlink(ERRORS);

			let scan = sprintf('%s --targets %s --timeout-ms %d --parallel %d --output %s',
				SCANNER, shell_quote(targets), timeout, parallel, shell_quote(SCAN));
			if (length(ports) > 0)
				scan += ' --ports ' + shell_quote(ports);
			if (index(services, 'ping') < 0)
				scan += ' --no-ping';
			for (let p in probes)
				scan += ' --probe ' + shell_quote(p);
			const vendors = store_dir() + '/manuf';
			if (stat(vendors) != null)
				scan += ' --vendors ' + shell_quote(vendors);
			// The shell writes its own ID and then becomes the scanner (exec), so the
			// saved ID is the scanner's. Errors of the scanner go to a file for the page.
			const inner = sprintf('echo $$ > %s; exec %s 2> %s', shell_quote(PIDFILE), scan, shell_quote(ERRORS));
			const code = system(sprintf('sh -c %s > /dev/null 2>&1 &', shell_quote(inner)));

			return (code == 0) ? { started: true } : { error: 'The scanner could not be started.' };
		}
	},

	stop: {
		call: function() {
			const pid = running_pid(PIDFILE, 'mynetworkmonitor-scan');
			if (pid == null)
				return { stopped: false };
			// SIGTERM: the scanner finishes the attempts in flight and writes what it found.
			system(sprintf('kill -TERM %d', pid));
			return { stopped: true };
		}
	},

	clear: {
		call: function() {
			if (running_pid(PIDFILE, 'mynetworkmonitor-scan') != null)
				return { error: 'Stop the running scan first.' };
			// The last scan counts as merged, so that it does not come back.
			const scan = load_scan();
			save_store({ format: STORE_FORMAT, devices: [], merged_scan: scan?.started ?? 0, last_scan: 0 });
			return { cleared: true };
		}
	},

	// Downloads the vendor list in the background (a few MB). The file is
	// checked before it replaces the old one: a cut-off or wrong download must
	// not leave the router without a list.
	update_vendors: {
		call: function() {
			if (running_pid(VENDOR_PIDFILE, 'uclient-fetch') != null)
				return { error: 'The vendor list is already being downloaded.' };
			const dir = store_dir();
			mkdir(WORK, 0700);
			mkdir(dir, 0700);
			const file = dir + '/manuf';
			const part = WORK + '/manuf.download';
			// No "exec" here: the checks run after the download in the same shell,
			// whose command line names uclient-fetch for running_pid().
			const inner = sprintf(
				'echo $$ > %s; uclient-fetch -q -T 60 -O %s %s && [ $(wc -c < %s) -gt 500000 ] && grep -q "^00:00:0C" %s && mv %s %s; rm -f %s',
				shell_quote(VENDOR_PIDFILE), shell_quote(part), shell_quote(VENDOR_URL), shell_quote(part),
				shell_quote(part), shell_quote(part), shell_quote(file), shell_quote(part));
			const code = system(sprintf('sh -c %s > /dev/null 2>&1 &', shell_quote(inner)));
			return (code == 0) ? { started: true } : { error: 'The download could not be started.' };
		}
	},

	// Moves device and vendor list between RAM and flash when the option changes.
	set_persist: {
		args: { persist: false },
		call: function(req) {
			const want = req.args.persist ? '1' : '0';
			const from = store_dir();
			const uci = config();
			uci.set('mynetworkmonitor', 'scan', 'persist', want);
			uci.commit('mynetworkmonitor');
			const to = store_dir();
			if (from != to) {
				mkdir(WORK, 0700);
				mkdir(to, 0700);
				for (let f in [ 'devices.json', 'manuf' ])
					if (stat(from + '/' + f) != null)
						system(sprintf('mv %s %s', shell_quote(from + '/' + f), shell_quote(to + '/' + f)));
			}
			return { persist: want == '1' };
		}
	}
};

return { 'luci.mynetworkmonitor': methods };
