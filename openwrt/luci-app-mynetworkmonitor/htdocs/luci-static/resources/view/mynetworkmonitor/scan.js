// SPDX-License-Identifier: GPL-2.0-only
//
// The page Network -> MyNetworkMonitor.
//
// Layout: the settings fold away at the top, then the buttons and the
// progress, then the device table with the details of the selected device on
// the right (below the table on narrow screens). The table is refreshed every
// second from the backend while the page is open; rows are rebuilt, the
// selection and the scroll position stay.

'use strict';
'require view';
'require rpc';
'require poll';
'require ui';
'require dom';
'require uci';

const callStatus = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'status' });
const callNetworks = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'networks', expect: { networks: [] } });
const callCatalog = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'catalog' });
const callStart = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'start',
	params: [ 'networks', 'ranges', 'services', 'extra_ports', 'timeout_ms', 'parallel', 'target' ] });
const callStop = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'stop' });
const callClear = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'clear' });
const callUpdateVendors = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'update_vendors' });
const callSetPersist = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'set_persist', params: [ 'persist' ] });

// Names for well-known ports (IANA service names, shortened where common).
const SERVICES = {
	21: 'FTP', 22: 'SSH', 23: 'Telnet', 25: 'SMTP', 53: 'DNS', 80: 'HTTP', 110: 'POP3', 135: 'MS-RPC',
	139: 'NetBIOS', 143: 'IMAP', 443: 'HTTPS', 445: 'SMB', 548: 'AFP', 631: 'IPP', 993: 'IMAPS', 995: 'POP3S',
	1433: 'MS-SQL', 1883: 'MQTT', 3306: 'MySQL', 3389: 'RDP', 5000: 'UPnP', 5432: 'PostgreSQL', 5900: 'VNC',
	8080: 'HTTP-alt', 8443: 'HTTPS-alt', 8883: 'MQTTS', 9100: 'Printer'
};

const STYLE = `
/* The theme keeps the content to a fixed width; the table needs the whole window.
   Everything is laid out to fit 1366 pixels; below that the details move under the table. */
#maincontent, #maincontent.container, .main .container { max-width:none !important; width:auto !important;
  margin-left:1em !important; margin-right:1em !important; }
.mnm-main { display:flex; gap:1em; align-items:flex-start; }
.mnm-table { flex:1 1 auto; min-width:0; overflow-x:auto; }
.mnm-details { flex:0 0 300px; width:300px; position:sticky; top:1em; overflow-wrap:anywhere; }
@media (max-width: 1250px) { .mnm-main { flex-direction:column; } .mnm-details { position:static; width:auto; flex-basis:auto; } }
.mnm-table table { width:100%; }
.mnm-table td { vertical-align:top; }
.mnm-table td.mnm-nowrap { white-space:nowrap; }
.mnm-table td.mnm-vendor { max-width:12em; }
.mnm-table tr.mnm-row { cursor:pointer; }
.mnm-table tr.mnm-net-head td { background:rgba(128,128,128,.12); font-weight:bold; padding-top:.6em; }
.mnm-table tr.mnm-row.selected td { background:rgba(11,124,139,.15); }
.mnm-dot { display:inline-block; width:.7em; height:.7em; border-radius:50%; background:#9aa; }
.mnm-dot.online { background:#2a9d4b; }
.mnm-sub { font-size:85%; opacity:.7; }
/* At most four services side by side, the rest in further rows. */
.mnm-chips { display:inline-grid; grid-template-columns:repeat(4, auto); gap:.2em; justify-content:start; }
.mnm-chip { padding:0 .4em; border-radius:3px; background:rgba(11,124,139,.12); font-size:85%; white-space:nowrap; }
.mnm-mono { font-family:monospace; }
.mnm-details h4 { margin:1em 0 .3em; font-size:85%; letter-spacing:.05em; opacity:.8; }
.mnm-details dl { display:grid; grid-template-columns:max-content 1fr; gap:.2em .8em; margin:0; }
.mnm-details dt { opacity:.7; }
.mnm-details dd { margin:0; }
.mnm-toolbar { display:flex; flex-wrap:wrap; gap:.5em; align-items:center; margin:.5em 0; }
.mnm-networks label { display:inline-block; margin-right:1.2em; }
/* The groups of "What to look for": three side by side. */
.mnm-groups { display:grid; grid-template-columns:repeat(3, minmax(0, 1fr)); gap:.6em; }
@media (max-width: 900px) { .mnm-groups { grid-template-columns:1fr; } }
.mnm-group { border:1px solid rgba(128,128,128,.3); border-radius:4px; padding:.4em .6em; }
.mnm-group-head { display:flex; align-items:center; gap:.4em; }
.mnm-group-head button { flex:1; text-align:left; background:none; border:none; padding:.2em 0; cursor:pointer; color:inherit; font:inherit; }
.mnm-group-body { margin:.3em 0 .2em 1.6em; }
.mnm-group-body[hidden] { display:none; }
.mnm-group-body label { display:block; margin:.15em 0; cursor:pointer; }
`;

function displayName(d) {
	return d.hostname || d.vendor || d.ipv4 || '?';
}

function ago(seconds) {
	if (!seconds)
		return '-';
	const diff = Math.max(0, Math.floor(Date.now() / 1000) - seconds);
	if (diff < 60) return _('just now');
	if (diff < 3600) return _('%d min ago').format(Math.floor(diff / 60));
	if (diff < 86400) return _('%d h ago').format(Math.floor(diff / 3600));
	return new Date(seconds * 1000).toLocaleDateString();
}

function when(seconds) {
	return seconds ? new Date(seconds * 1000).toLocaleString() : '-';
}

function serviceChips(d) {
	return E('span', { 'class': 'mnm-chips' }, (d.tcp || []).map(p => E('span', { 'class': 'mnm-chip' }, SERVICES[p] || String(p))));
}

// The open ports in two lines of about the same length.
function portLines(d) {
	const ports = d.tcp || [];
	if (!ports.length)
		return '-';
	const half = Math.ceil(ports.length / 2);
	return ports.length < 4 ? ports.join(', ')
		: [ ports.slice(0, half).join(', ') + ',', E('br'), ports.slice(half).join(', ') ];
}

// Long IPv6 addresses shortened for the table: the prefix and the last block.
// The full address is in the tooltip and the details.
function shortV6(a) {
	if (a.length <= 22)
		return a;
	const parts = a.split(':');
	return parts.slice(0, 2).join(':') + ':…:' + parts[parts.length - 1];
}

// Compares IPv4 addresses numerically, so that .10 comes after .9.
function ipOrder(a, b) {
	const x = (a.ipv4 || '').split('.').map(Number), y = (b.ipv4 || '').split('.').map(Number);
	for (let i = 0; i < 4; i++)
		if ((x[i] || 0) != (y[i] || 0))
			return (x[i] || 0) - (y[i] || 0);
	return 0;
}

return view.extend({
	selected: null,     // key of the selected device (MAC, else IPv4)
	status: null,

	keyOf(d) {
		return d.mac || d.ipv4;
	},

	load() {
		return Promise.all([ callStatus(), callNetworks(), callCatalog(), uci.load('mynetworkmonitor') ]);
	},

	// ------------------------------------------------------------ settings

	renderSettings(networks) {
		const chosen = L.toArray(uci.get('mynetworkmonitor', 'scan', 'networks'));
		const value = (name, fallback) => uci.get('mynetworkmonitor', 'scan', name) || fallback;

		const netBoxes = networks.length
			? networks.map(n => E('label', {}, [
				E('input', { 'type': 'checkbox', 'class': 'mnm-net', 'value': n.name, 'checked': chosen.indexOf(n.name) >= 0 ? '' : null }),
				' ', E('strong', {}, n.name), ' ', E('span', { 'class': 'mnm-sub' }, n.cidr)
			]))
			: [ E('em', {}, _('The router has no private IPv4 network.')) ];

		const field = (id, label, hint, val) => E('div', { 'class': 'cbi-value' }, [
			E('label', { 'class': 'cbi-value-title', 'for': id }, label),
			E('div', { 'class': 'cbi-value-field' }, [
				E('input', { 'id': id, 'type': 'text', 'class': 'cbi-input-text', 'value': val }),
				E('div', { 'class': 'cbi-value-description' }, hint)
			])
		]);

		return E('details', { 'class': 'cbi-section', 'open': this.status.devices.length ? null : '' }, [
			E('summary', {}, E('strong', {}, _('Settings'))),
			E('div', { 'class': 'cbi-value' }, [
				E('label', { 'class': 'cbi-value-title' }, _('Networks')),
				E('div', { 'class': 'cbi-value-field mnm-networks' }, [
					...netBoxes,
					E('div', { 'class': 'cbi-value-description' }, _('The networks of this router that are scanned.'))
				])
			]),
			field('mnm-ranges', _('Own ranges'),
				_('Optional, separated by commas - for example 192.168.5.0/24 or 192.168.5.1-50. Only private networks are scanned (10.x, 172.16-31.x, 192.168.x), never addresses on the internet.'),
				value('ranges', '')),
			E('div', { 'class': 'cbi-value' }, [
				E('label', { 'class': 'cbi-value-title' }, _('What to look for')),
				E('div', { 'class': 'cbi-value-field' }, [
					this.renderCatalog(),
					E('div', { 'class': 'cbi-value-description' },
						_('Only the ports of the ticked services are tried on the devices found. Entries marked "coming" are planned; their tick counts as soon as they are built.'))
				])
			]),
			field('mnm-extra-ports', _('Additional ports'),
				_('Optional TCP ports and ranges, separated by commas, e.g. 8123,9000-9010.'), value('extra_ports', '')),
			field('mnm-timeout', _('Timeout (ms)'),
				_('How long to wait for an answer. Low values are faster but miss slow or distant devices.'),
				value('timeout_ms', '800')),
			field('mnm-parallel', _('Connections at once'),
				_('More is faster; small routers and some devices cope better with fewer.'), value('parallel', '64')),
			E('div', { 'class': 'cbi-value' }, [
				E('label', { 'class': 'cbi-value-title' }, _('Keep after reboot')),
				E('div', { 'class': 'cbi-value-field' }, [
					E('input', { 'type': 'checkbox', 'id': 'mnm-persist', 'checked': this.status.persist ? '' : null,
						'change': ev => callSetPersist(ev.target.checked) }),
					E('div', { 'class': 'cbi-value-description' },
						_('Stores the device list and the vendor list on the flash instead of in RAM. The flash is written once per scan.'))
				])
			]),
			E('div', { 'class': 'cbi-value' }, [
				E('label', { 'class': 'cbi-value-title' }, _('Vendor list')),
				E('div', { 'class': 'cbi-value-field' }, [
					E('span', { 'id': 'mnm-vendors' }, this.vendorText(this.status.vendors)), ' ',
					E('button', { 'class': 'cbi-button', 'click': ui.createHandlerFn(this, 'handleVendors') }, _('Download / update')),
					E('div', { 'class': 'cbi-value-description' },
						_('Names the manufacturer of each device by its MAC address. Downloads the public IEEE register (as compiled by the Wireshark project, a few MB) from www.wireshark.org.'))
				])
			])
		]);
	},

	// The catalog as one box per group: a tick for the whole group with a count,
	// unfolded the single services with their ports.
	renderCatalog() {
		const chosen = L.toArray(uci.get('mynetworkmonitor', 'scan', 'services'));
		const boxes = (this.catalog.groups || []).map(g => {
			// Planned entries can be ticked like the others: the choice is kept and
			// counts as soon as they are built.
			const items = g.items.map(item => E('label', {}, [
				E('input', { 'type': 'checkbox', 'class': 'mnm-svc', 'value': item.id, 'data-group': g.id,
					'checked': chosen.indexOf(item.id) >= 0 ? '' : null,
					'change': () => this.updateGroupBoxes() }),
				' ', item.name,
				(item.tcp || []).length ? E('span', { 'class': 'mnm-sub' }, ' · ' + item.tcp.join(', ')) : '',
				item.hint ? E('span', { 'class': 'mnm-sub' }, ' · ' + item.hint) : '',
				item.available === false ? E('span', { 'class': 'mnm-sub' }, ' · ' + _('coming')) : ''
			]));
			const body = E('div', { 'class': 'mnm-group-body', 'hidden': '' }, items);
			// A button of its own opens and closes the group; the tick beside it
			// only switches the services. Both inside one <summary> made a click on
			// the tick also fold the group.
			const toggle = E('button', { 'type': 'button', 'aria-expanded': 'false',
				'click': ev => {
					body.hidden = !body.hidden;
					ev.currentTarget.setAttribute('aria-expanded', String(!body.hidden));
					ev.currentTarget.querySelector('.mnm-arrow').textContent = body.hidden ? '▸' : '▾';
				} }, [
				E('span', { 'class': 'mnm-arrow' }, '▸'), ' ', (g.icon || '') + ' ', E('strong', {}, g.name), ' ',
				E('span', { 'class': 'mnm-sub mnm-count', 'data-group': g.id })
			]);
			const groupBox = E('input', { 'type': 'checkbox', 'class': 'mnm-group-box', 'data-group': g.id,
				'title': _('all services of this group'),
				'change': ev => {
					document.querySelectorAll('.mnm-svc[data-group="%s"]'.format(g.id)).forEach(c => c.checked = ev.target.checked);
					this.updateGroupBoxes();
				} });
			return E('div', { 'class': 'mnm-group' }, [ E('div', { 'class': 'mnm-group-head' }, [ groupBox, toggle ]), body ]);
		});
		requestAnimationFrame(() => this.updateGroupBoxes());
		return E('div', { 'class': 'mnm-groups' }, boxes);
	},

	// Group tick: on when all its services are on, half when some are.
	updateGroupBoxes() {
		document.querySelectorAll('.mnm-group-box').forEach(box => {
			const all = document.querySelectorAll('.mnm-svc[data-group="%s"]'.format(box.dataset.group));
			const on = Array.prototype.filter.call(all, c => c.checked).length;
			box.checked = all.length > 0 && on == all.length;
			box.indeterminate = on > 0 && on < all.length;
			box.disabled = all.length == 0;
			const count = document.querySelector('.mnm-count[data-group="%s"]'.format(box.dataset.group));
			if (count)
				count.textContent = _('%d of %d').format(on, all.length);
		});
	},

	vendorText(v) {
		if (!v) return '';
		if (v.updating) return _('Downloading...');
		return v.present ? _('present, %.1f MB, from %s').format(v.size / 1048576, when(v.updated)) : _('not downloaded yet');
	},

	formValues() {
		const nets = Array.prototype.map.call(document.querySelectorAll('.mnm-net:checked'), n => n.value);
		const services = Array.prototype.map.call(document.querySelectorAll('.mnm-svc:checked'), n => n.value);
		const val = id => (document.getElementById(id)?.value || '').trim();
		return [ nets, val('mnm-ranges'), services, val('mnm-extra-ports'), val('mnm-timeout') || '800', val('mnm-parallel') || '64' ];
	},

	// --------------------------------------------------------------- table

	// The table is updated every second. Rows whose device did not change keep
	// their element: replacing a row between pressing and releasing the mouse
	// button would swallow the click (docs/FEHLERGEDAECHTNIS.md, No. 5).
	renderTable(st) {
		const online = d => st.last_scan && d.last_seen >= st.last_scan;
		const devices = (st.devices || []).slice().sort(ipOrder);
		const cache = this.rowCache || (this.rowCache = new Map());
		const used = new Map();
		const cached = (key, signature, build) => {
			const hit = cache.get(key);
			const el = (hit && hit.signature == signature) ? hit.el : build();
			used.set(key, { signature: signature, el: el });
			return el;
		};

		const head = this.tableHead || (this.tableHead = E('tr', { 'class': 'tr table-titles' }, [
			E('th', { 'class': 'th' }, ''),
			E('th', { 'class': 'th' }, _('DEVICE')),
			E('th', { 'class': 'th' }, _('IPV4')),
			E('th', { 'class': 'th' }, _('IPV6')),
			E('th', { 'class': 'th' }, _('VENDOR')),
			E('th', { 'class': 'th' }, _('RUNNING SERVICES')),
			E('th', { 'class': 'th' }, _('PORTS')),
			E('th', { 'class': 'th' }, _('SEEN'))
		]));

		const row = d => {
			const key = this.keyOf(d);
			const v6 = d.ipv6 || [];
			const signature = JSON.stringify([ d, online(d), key == this.selected, ago(d.last_seen) ]);
			return cached('row:' + key, signature, () => E('tr', {
				'class': 'tr mnm-row' + (key == this.selected ? ' selected' : ''),
				'click': () => { this.selected = key; this.update(); }
			}, [
				E('td', { 'class': 'td mnm-nowrap' }, E('span', { 'class': 'mnm-dot' + (online(d) ? ' online' : ''),
					'title': online(d) ? _('seen in the last scan') : _('not seen in the last scan') })),
				E('td', { 'class': 'td' }, [ E('strong', {}, displayName(d)), E('br'), E('span', { 'class': 'mnm-sub' }, d.network || '') ]),
				E('td', { 'class': 'td mnm-mono mnm-nowrap' }, d.ipv4 || '-'),
				E('td', { 'class': 'td mnm-mono mnm-nowrap', 'title': v6.join('\n') }, v6.length ? [ shortV6(v6[0]), v6.length > 1 ? E('span', { 'class': 'mnm-sub' }, ' +' + (v6.length - 1)) : '' ] : '-'),
				E('td', { 'class': 'td mnm-vendor' }, d.vendor || '-'),
				E('td', { 'class': 'td' }, serviceChips(d)),
				E('td', { 'class': 'td mnm-mono mnm-nowrap' }, portLines(d)),
				E('td', { 'class': 'td mnm-nowrap' }, ago(d.last_seen))
			]));
		};

		// Grouped by the router networks in the order of the settings; devices of
		// own ranges (outside these networks) come last.
		const rows = [];
		const groups = (this.networks || []).map(n => ({ name: n.name, label: '%s · %s'.format(n.name, n.cidr) }));
		groups.push({ name: null, label: _('Own ranges') });
		for (const g of groups) {
			const members = devices.filter(d => (d.network || null) == g.name);
			if (!members.length)
				continue;
			rows.push(cached('net:' + g.label, String(members.length), () => E('tr', { 'class': 'tr mnm-net-head' },
				E('td', { 'class': 'td', 'colspan': 8 }, [ g.label, ' ', E('span', { 'class': 'mnm-sub' }, _('%d devices').format(members.length)) ]))));
			members.forEach(d => rows.push(row(d)));
		}
		if (!rows.length)
			rows.push(cached('empty', '', () => E('tr', { 'class': 'tr' },
				E('td', { 'class': 'td', 'colspan': 8 }, E('em', {}, _('No devices yet - start a scan.'))))));
		this.rowCache = used;

		const table = this.tableEl || (this.tableEl = E('table', { 'class': 'table' }));
		const wanted = [ head, ...rows ];
		const current = Array.prototype.slice.call(table.children);
		if (wanted.length != current.length || wanted.some((el, i) => el !== current[i]))
			table.replaceChildren(...wanted);
		return table;
	},

	// ------------------------------------------------------------- details

	renderDetails(st) {
		const d = (st.devices || []).find(x => this.keyOf(x) == this.selected);
		if (!d)
			return E('div', { 'class': 'cbi-section' }, E('em', {}, _('Select a row to see details')));

		const row = (label, value) => value ? [ E('dt', {}, label), E('dd', {}, value) ] : [];
		const ports = (d.tcp || []).map(p => E('div', {}, [ E('span', { 'class': 'mnm-mono' }, String(p)), ' ', SERVICES[p] || '' ]));

		return E('div', { 'class': 'cbi-section' }, [
			E('h3', { 'style': 'margin-top:0' }, displayName(d)),
			E('div', { 'class': 'mnm-sub' }, [ d.vendor || _('unknown vendor'), d.mac ? ' · ' + d.mac : '' ]),
			E('div', { 'class': 'mnm-toolbar' }, E('button', { 'class': 'cbi-button cbi-button-action', 'disabled': st.running ? '' : null,
				'click': ui.createHandlerFn(this, 'handleScanDevice', d.ipv4) }, _('Scan this device'))),

			E('h4', {}, _('IDENTITY')),
			E('dl', {}, [
				...row(_('Hostname'), d.hostname),
				...row(_('MAC'), d.mac),
				...row(_('Probably'), d.os_guess ? '%s (TTL %d)'.format(d.os_guess, d.ttl) : null),
				...row(_('Found by'), (d.seen_by || []).join(', ')),
				...row(_('First seen'), when(d.first_seen)),
				...row(_('Last seen'), when(d.last_seen))
			]),

			E('h4', {}, _('ADDRESSES')),
			E('dl', {}, [
				...row(_('IPv4'), d.ipv4),
				...row(_('Network'), d.network),
				...row(_('IPv6'), (d.ipv6 || []).length ? E('div', { 'class': 'mnm-mono' }, (d.ipv6 || []).map(a => E('div', {}, a))) : null)
			]),

			E('h4', {}, _('SERVICES & PORTS')),
			ports.length ? E('div', {}, ports) : E('em', {}, d.ports_checked ? _('No open ports.') : _('Ports not scanned yet.'))
		]);
	},

	// ------------------------------------------------------------ progress

	renderProgress(st) {
		const s = st.scan;
		if (st.error && !st.running)
			return E('p', { 'class': 'alert-message warning' }, _('The scan ended with an error: %s').format(st.error));
		if (!st.running || !s)
			return E('span', { 'class': 'mnm-sub' }, s ? _('Last scan %s: %s').format(when(s.finished || s.started),
				s.state == 'stopped' ? _('stopped') : _('done')) : _('No scan yet.'));
		const devices = (st.devices || []).length;
		if (s.phase == 'discovery')
			return E('span', {}, _('Finding devices... %d so far').format(devices));
		const percent = s.total ? Math.floor(s.done * 100 / s.total) : 0;
		return E('span', {}, _('Checking ports: %d of %d (%d%%), %d devices').format(s.done, s.total, percent, devices));
	},

	// ------------------------------------------------------------- actions

	update() {
		const st = this.status;
		dom.content(document.getElementById('mnm-progress'), this.renderProgress(st));
		const box = document.getElementById('mnm-table');
		const table = this.renderTable(st);
		if (table.parentNode !== box)
			dom.content(box, table);
		// The details only when something in them changed: they hold a button.
		const selected = (st.devices || []).find(x => this.keyOf(x) == this.selected);
		const signature = JSON.stringify([ selected, st.running ]);
		if (signature != this.detailsSignature) {
			this.detailsSignature = signature;
			dom.content(document.getElementById('mnm-details'), this.renderDetails(st));
		}
		dom.content(document.getElementById('mnm-vendors'), this.vendorText(st.vendors));
		document.getElementById('mnm-start').disabled = st.running;
		document.getElementById('mnm-stop').disabled = !st.running;
		document.getElementById('mnm-clear').disabled = st.running || !(st.devices || []).length;
	},

	async refresh() {
		this.status = await callStatus();
		this.update();
	},

	async report(answer) {
		if (answer && answer.error)
			ui.addNotification(null, E('p', {}, answer.error), 'error');
		return this.refresh();
	},

	async handleStart() {
		return this.report(await callStart(...this.formValues(), ''));
	},

	async handleScanDevice(address) {
		return this.report(await callStart(...this.formValues(), address));
	},

	async handleStop() {
		return this.report(await callStop());
	},

	async handleClear() {
		if (!confirm(_('Remove all devices from the table?')))
			return;
		this.selected = null;
		return this.report(await callClear());
	},

	async handleVendors() {
		return this.report(await callUpdateVendors());
	},

	render([ st, networks, catalog ]) {
		this.status = st;
		this.networks = networks;
		this.catalog = catalog || { groups: [] };
		const node = E('div', {}, [
			E('style', {}, STYLE),
			E('h2', {}, [ _('MyNetworkMonitor'), st.version ? E('small', { 'class': 'mnm-sub', 'style': 'margin-left:.5em' }, 'v' + st.version) : '' ]),
			E('p', { 'class': 'cbi-section-descr' },
				_('Finds the devices in your networks and what runs on them. Scan only networks you are permitted to scan - port scans can disturb sensitive devices such as controllers.')),
			this.renderSettings(networks),
			E('div', { 'class': 'mnm-toolbar' }, [
				E('button', { 'id': 'mnm-start', 'class': 'cbi-button cbi-button-action', 'click': ui.createHandlerFn(this, 'handleStart') }, _('Start scan')),
				E('button', { 'id': 'mnm-stop', 'class': 'cbi-button cbi-button-reset', 'click': ui.createHandlerFn(this, 'handleStop') }, _('Stop')),
				E('button', { 'id': 'mnm-clear', 'class': 'cbi-button cbi-button-remove', 'click': ui.createHandlerFn(this, 'handleClear') }, _('Clear table')),
				E('span', { 'id': 'mnm-progress' })
			]),
			E('div', { 'class': 'mnm-main' }, [
				E('div', { 'id': 'mnm-table', 'class': 'mnm-table' }),
				E('div', { 'id': 'mnm-details', 'class': 'mnm-details' })
			])
		]);
		requestAnimationFrame(() => this.update());
		poll.add(() => this.refresh(), 1);
		return node;
	},

	// The page has its own buttons instead of the Save/Apply row of LuCI.
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
