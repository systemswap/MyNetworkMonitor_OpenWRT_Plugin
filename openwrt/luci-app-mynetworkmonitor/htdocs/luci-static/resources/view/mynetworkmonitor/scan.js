// SPDX-License-Identifier: GPL-2.0-only
//
// The page Network -> MyNetworkMonitor: settings of the scan (stored in UCI),
// start and stop, progress while it runs, and the result table.

'use strict';
'require view';
'require form';
'require rpc';
'require poll';
'require ui';
'require dom';
'require uci';

const callStatus = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'status' });
const callResult = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'result' });
const callStart = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'start', params: [ 'targets', 'ports', 'timeout_ms', 'parallel' ] });
const callStop = rpc.declare({ object: 'luci.mynetworkmonitor', method: 'stop' });

// Ranges as the scanner accepts them; checked here for a quick hint, the
// backend and the scanner check again.
function validateTargets(sectionId, value) {
	if (!/^[0-9.,\/ -]+$/.test(value || ''))
		return _('Use addresses (192.0.2.7), ranges (192.0.2.1-254) or networks (192.0.2.0/24), separated by commas.');
	return true;
}

function validatePorts(sectionId, value) {
	if (!/^[0-9, -]+$/.test(value || ''))
		return _('Use ports (80) and ranges (8000-8100), separated by commas.');
	return true;
}

function formatTime(seconds) {
	return seconds ? new Date(seconds * 1000).toLocaleString() : '-';
}

return view.extend({
	load() {
		return Promise.all([callStatus(), callResult()]);
	},

	renderProgress(status) {
		const p = status && status.progress;
		if (status && !status.running && status.error)
			return E('p', { 'class': 'alert-message warning' }, _('The scan ended with an error: %s').format(status.error));
		if (!status || !status.running)
			return E('em', {}, p && p.state == 'stopped' ? _('Stopped.') : (p && p.state == 'done' ? _('Done.') : _('No scan running.')));
		const done = p ? p.done : 0, total = p ? p.total : 0;
		const percent = total ? Math.floor(done * 100 / total) : 0;
		return E('div', {}, [
			E('div', { 'class': 'cbi-progressbar', 'title': '%d%%'.format(percent) }, E('div', { 'style': 'width:%d%%'.format(percent) })),
			E('p', {}, _('%d of %d attempts, %d devices with open ports so far').format(done, total, p ? p.hosts : 0))
		]);
	},

	renderResult(result) {
		if (!result || result.error || !Array.isArray(result.hosts))
			return E('p', {}, E('em', {}, _('No result yet.')));

		const rows = result.hosts.map(h => [ h.address, (h.tcp || []).join(', ') ]);
		const table = E('table', { 'class': 'table' }, [
			E('tr', { 'class': 'tr table-titles' }, [
				E('th', { 'class': 'th' }, _('Address')),
				E('th', { 'class': 'th' }, _('Open TCP ports'))
			])
		]);
		cbi_update_table(table, rows, E('em', {}, _('No open ports found.')));

		return E('div', {}, [
			E('p', {}, [
				_('Scan of %s, ports %s, finished %s').format(result.targets, result.ports, formatTime(result.finished)),
				result.partial ? E('strong', {}, ' ' + _('(stopped early - devices may be missing)')) : ''
			]),
			table
		]);
	},

	async refresh() {
		const [status, result] = await Promise.all([callStatus(), callResult()]);
		dom.content(document.getElementById('mnm-progress'), this.renderProgress(status));
		dom.content(document.getElementById('mnm-result'), this.renderResult(result));
		document.getElementById('mnm-start').disabled = status.running;
		document.getElementById('mnm-stop').disabled = !status.running;
	},

	async handleStart(map) {
		// Take the values from the form (parse() checks them and puts them into the
		// local copy of the configuration); the backend stores them itself.
		await map.parse();
		const get = (name, fallback) => uci.get('mynetworkmonitor', 'scan', name) || fallback;
		const answer = await callStart(get('targets', ''), get('ports', ''),
			+get('timeout_ms', 800), +get('parallel', 64));
		if (answer.error)
			ui.addNotification(null, E('p', {}, answer.error), 'error');
		return this.refresh();
	},

	async handleStop() {
		await callStop();
		return this.refresh();
	},

	render([status, result]) {
		const m = new form.Map('mynetworkmonitor', _('MyNetworkMonitor'),
			_('Finds the devices in your networks and the TCP ports they answer on. Scan only networks you are permitted to scan - port scans can disturb sensitive devices such as controllers.'));

		const s = m.section(form.NamedSection, 'scan', 'scan', _('Scan'));
		s.addremove = false;

		let o = s.option(form.Value, 'targets', _('Ranges'),
			_('Addresses, ranges or networks, separated by commas - for example 192.168.1.0/24 or 192.168.1.1-254. At most 65536 addresses.'));
		o.rmempty = false;
		o.validate = validateTargets;

		o = s.option(form.Value, 'ports', _('TCP ports'),
			_('Ports and ranges, separated by commas. More ports find more services but take longer.'));
		o.rmempty = false;
		o.validate = validatePorts;

		o = s.option(form.Value, 'timeout_ms', _('Timeout per port (ms)'),
			_('How long a port may take to answer. Low values are faster but miss slow or distant devices.'));
		o.datatype = 'range(50,10000)';
		o.placeholder = '800';

		o = s.option(form.Value, 'parallel', _('Connections at once'),
			_('More is faster; small routers and some devices cope better with fewer.'));
		o.datatype = 'range(1,256)';
		o.placeholder = '64';

		return m.render().then(node => {
			const self = this;
			node.appendChild(E('div', { 'class': 'cbi-section' }, [
				E('div', { 'class': 'cbi-page-actions', 'style': 'text-align:left' }, [
					E('button', { 'id': 'mnm-start', 'class': 'cbi-button cbi-button-action', 'click': ui.createHandlerFn(this, 'handleStart', m), 'disabled': status.running ? '' : null }, _('Save and start scan')),
					' ',
					E('button', { 'id': 'mnm-stop', 'class': 'cbi-button cbi-button-reset', 'click': ui.createHandlerFn(this, 'handleStop'), 'disabled': status.running ? null : '' }, _('Stop'))
				]),
				E('h3', {}, _('Progress')),
				E('div', { 'id': 'mnm-progress' }, this.renderProgress(status)),
				E('h3', {}, _('Result')),
				E('div', { 'id': 'mnm-result' }, this.renderResult(result))
			]));
			poll.add(() => self.refresh(), 2);
			return node;
		});
	},

	// The buttons above replace the usual Save/Apply row of the page.
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
