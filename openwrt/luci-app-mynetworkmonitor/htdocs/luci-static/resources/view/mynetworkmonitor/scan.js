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
/* The theme keeps the content to a fixed width; the table needs the whole window. */
#maincontent, #maincontent.container, .main .container { max-width:none !important; width:auto !important; }
.mnm-main { display:flex; gap:1em; align-items:flex-start; }
.mnm-table { flex:1 1 auto; min-width:0; overflow-x:auto; }
.mnm-details { flex:0 0 340px; position:sticky; top:1em; }
@media (max-width: 1100px) { .mnm-main { flex-direction:column; } .mnm-details { position:static; width:100%; flex-basis:auto; } }
.mnm-table td { white-space:nowrap; }
.mnm-groups { display:grid; grid-template-columns:repeat(auto-fill, minmax(280px, 1fr)); gap:.6em; }
.mnm-group { border:1px solid rgba(128,128,128,.3); border-radius:4px; padding:.4em .6em; }
.mnm-group summary { cursor:pointer; }
.mnm-group label { display:block; margin:.2em 0 .2em 1.4em; }
.mnm-group label.mnm-off { opacity:.5; }
.mnm-table table { width:100%; }
.mnm-table tr.mnm-row { cursor:pointer; }
.mnm-table tr.mnm-row.selected td { background:rgba(11,124,139,.15); }
.mnm-dot { display:inline-block; width:.7em; height:.7em; border-radius:50%; background:#9aa; }
.mnm-dot.online { background:#2a9d4b; }
.mnm-sub { font-size:85%; opacity:.7; }
.mnm-chip { display:inline-block; padding:0 .4em; margin:0 .2em .2em 0; border-radius:3px; background:rgba(11,124,139,.12); font-size:85%; }
.mnm-mono { font-family:monospace; }
.mnm-details h4 { margin:1em 0 .3em; font-size:85%; letter-spacing:.05em; opacity:.8; }
.mnm-details dl { display:grid; grid-template-columns:max-content 1fr; gap:.2em .8em; margin:0; }
.mnm-details dt { opacity:.7; }
.mnm-details dd { margin:0; word-break:break-word; }
.mnm-toolbar { display:flex; flex-wrap:wrap; gap:.5em; align-items:center; margin:.5em 0; }
.mnm-networks label { display:inline-block; margin-right:1.2em; }
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
	return (d.tcp || []).map(p => E('span', { 'class': 'mnm-chip' }, SERVICES[p] || String(p)));
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
						_('Only the ports of the ticked services are tried on the devices found. Greyed entries are planned and not built yet.'))
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
			const items = g.items.map(item => {
				const off = item.available === false;
				const ports = (item.tcp || []).join(', ');
				return E('label', { 'class': off ? 'mnm-off' : '' }, [
					E('input', { 'type': 'checkbox', 'class': 'mnm-svc', 'value': item.id, 'data-group': g.id,
						'checked': chosen.indexOf(item.id) >= 0 ? '' : null, 'disabled': off ? '' : null,
						'change': () => this.updateGroupBoxes() }),
					' ', item.name,
					ports ? E('span', { 'class': 'mnm-sub' }, ' · ' + ports) : '',
					item.hint ? E('span', { 'class': 'mnm-sub' }, ' · ' + item.hint) : '',
					off ? E('span', { 'class': 'mnm-sub' }, ' · ' + _('coming')) : ''
				]);
			});
			return E('details', { 'class': 'mnm-group' }, [
				E('summary', {}, [
					E('input', { 'type': 'checkbox', 'class': 'mnm-group-box', 'data-group': g.id,
						'click': ev => ev.stopPropagation(),
						'change': ev => {
							document.querySelectorAll('.mnm-svc[data-group="%s"]:not([disabled])'.format(g.id))
								.forEach(c => c.checked = ev.target.checked);
							this.updateGroupBoxes();
						} }),
					' ', (g.icon || '') + ' ', E('strong', {}, g.name), ' ',
					E('span', { 'class': 'mnm-sub mnm-count', 'data-group': g.id })
				]),
				...items
			]);
		});
		requestAnimationFrame(() => this.updateGroupBoxes());
		return E('div', { 'class': 'mnm-groups' }, boxes);
	},

	// Group tick: on when all its services are on, half when some are.
	updateGroupBoxes() {
		document.querySelectorAll('.mnm-group-box').forEach(box => {
			const all = document.querySelectorAll('.mnm-svc[data-group="%s"]:not([disabled])'.format(box.dataset.group));
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
		// Planned entries keep their tick: they count as soon as they are built.
		const services = Array.prototype.map.call(document.querySelectorAll('.mnm-svc:checked'), n => n.value);
		const val = id => (document.getElementById(id)?.value || '').trim();
		return [ nets, val('mnm-ranges'), services, val('mnm-extra-ports'), val('mnm-timeout') || '800', val('mnm-parallel') || '64' ];
	},

	// --------------------------------------------------------------- table

	renderTable(st) {
		const online = d => st.last_scan && d.last_seen >= st.last_scan;
		const devices = (st.devices || []).slice().sort(ipOrder);

		const head = E('tr', { 'class': 'tr table-titles' }, [
			E('th', { 'class': 'th' }, ''),
			E('th', { 'class': 'th' }, _('DEVICE')),
			E('th', { 'class': 'th' }, _('IPV4')),
			E('th', { 'class': 'th' }, _('IPV6')),
			E('th', { 'class': 'th' }, _('VENDOR')),
			E('th', { 'class': 'th' }, _('RUNNING SERVICES')),
			E('th', { 'class': 'th' }, _('PORTS')),
			E('th', { 'class': 'th' }, _('SEEN'))
		]);

		const rows = devices.map(d => {
			const key = this.keyOf(d);
			const v6 = d.ipv6 || [];
			return E('tr', {
				'class': 'tr mnm-row' + (key == this.selected ? ' selected' : ''),
				'click': () => { this.selected = key; this.update(); }
			}, [
				E('td', { 'class': 'td' }, E('span', { 'class': 'mnm-dot' + (online(d) ? ' online' : ''),
					'title': online(d) ? _('seen in the last scan') : _('not seen in the last scan') })),
				E('td', { 'class': 'td' }, [ E('strong', {}, displayName(d)), E('br'), E('span', { 'class': 'mnm-sub' }, d.network || '') ]),
				E('td', { 'class': 'td mnm-mono' }, d.ipv4 || '-'),
				E('td', { 'class': 'td mnm-mono', 'title': v6.join('\n') }, v6.length ? [ shortV6(v6[0]), v6.length > 1 ? E('span', { 'class': 'mnm-sub' }, ' +' + (v6.length - 1)) : '' ] : '-'),
				E('td', { 'class': 'td' }, d.vendor || '-'),
				E('td', { 'class': 'td' }, serviceChips(d)),
				E('td', { 'class': 'td mnm-mono' }, (d.tcp || []).join(', ') || '-'),
				E('td', { 'class': 'td' }, ago(d.last_seen))
			]);
		});

		return E('table', { 'class': 'table' }, [ head, ...(rows.length ? rows :
			[ E('tr', { 'class': 'tr' }, E('td', { 'class': 'td', 'colspan': 8 }, E('em', {}, _('No devices yet - start a scan.')))) ]) ]);
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
			ports.length ? E('div', {}, ports) : E('em', {}, d.ports_checked ? _('No open ports.') : _('Ports not scanned yet.')),
			d.ports_checked ? E('p', { 'class': 'mnm-sub' }, _('Checked %s: %s').format(when(d.ports_checked), d.ports_list || '')) : ''
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
		const scroll = box.scrollTop;
		dom.content(box, this.renderTable(st));
		box.scrollTop = scroll;
		dom.content(document.getElementById('mnm-details'), this.renderDetails(st));
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
		this.catalog = catalog || { groups: [] };
		const node = E('div', {}, [
			E('style', {}, STYLE),
			E('h2', {}, _('MyNetworkMonitor')),
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
