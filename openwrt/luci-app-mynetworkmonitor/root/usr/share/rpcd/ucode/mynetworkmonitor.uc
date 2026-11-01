// SPDX-License-Identifier: GPL-2.0-only
//
// rpcd backend of the LuCI page: starts and stops the scanner and hands out
// progress and result. Object "luci.mynetworkmonitor"; who may call what is
// set in /usr/share/rpcd/acl.d/luci-app-mynetworkmonitor.json.
//
// The scanner runs as a separate process in the background, so a scan never
// blocks rpcd. All values from UCI are checked here before they reach a shell
// command line, and the scanner checks them again.

'use strict';

import { readfile, unlink, mkdir, stat } from 'fs';
import { cursor } from 'uci';

const SCANNER = '/usr/bin/mynetworkmonitor-scan';
const WORK = '/tmp/mynetworkmonitor';
const PIDFILE = WORK + '/scan.pid';
const PROGRESS = WORK + '/progress.json';
const RESULT = WORK + '/result.json';

// Only the characters the scanner accepts; nothing a shell would interpret.
const TARGETS_PATTERN = /^[0-9.,\/ -]{1,1024}$/;
const PORTS_PATTERN = /^[0-9, -]{1,1024}$/;

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

// The process ID of the running scan, or null. The ID is only trusted if the
// process with that ID really is the scanner: IDs are reused after a reboot or
// once a scan has ended.
function running_pid() {
	const pid = +trim(readfile(PIDFILE) ?? '');
	if (!(pid > 1))
		return null;
	const cmdline = readfile(sprintf('/proc/%d/cmdline', pid));
	return (cmdline != null && index(cmdline, 'mynetworkmonitor-scan') >= 0) ? pid : null;
}

function int_option(value, min, max, fallback) {
	const n = +value;
	return (type(n) == 'int' && n >= min && n <= max) ? n : fallback;
}

const methods = {
	status: {
		call: function() {
			const running = running_pid() != null;
			// What the scanner wrote to its error output, e.g. a refused range.
			const error = running ? null : trim(readfile(WORK + '/error.txt') ?? '');
			return {
				running: running,
				progress: read_json(PROGRESS),
				has_result: stat(RESULT) != null,
				error: (error != null && length(error) > 0) ? error : null
			};
		}
	},

	result: {
		call: function() {
			const result = read_json(RESULT);
			return (result != null && result.format == 1) ? result : { error: 'no result' };
		}
	},

	// The page sends the values of its form; they are checked, stored in UCI
	// (so the page shows them next time) and used for this scan. Reading them
	// from UCI instead would miss values the user saved but did not apply yet.
	start: {
		args: { targets: 'targets', ports: 'ports', timeout_ms: 800, parallel: 64 },
		call: function(req) {
			if (running_pid() != null)
				return { error: 'A scan is already running.' };

			const targets = trim('' + (req.args.targets ?? ''));
			const ports = trim('' + (req.args.ports ?? ''));
			const timeout = int_option(req.args.timeout_ms, 50, 10000, 800);
			const parallel = int_option(req.args.parallel, 1, 256, 64);

			if (!match(targets, TARGETS_PATTERN))
				return { error: 'Ranges may only contain addresses, ranges (a-b) and networks (a/n), separated by commas.' };
			if (!match(ports, PORTS_PATTERN))
				return { error: 'Ports may only contain numbers and ranges (a-b), separated by commas.' };

			const uci = cursor();
			uci.load('mynetworkmonitor');
			uci.set('mynetworkmonitor', 'scan', 'targets', targets);
			uci.set('mynetworkmonitor', 'scan', 'ports', ports);
			uci.set('mynetworkmonitor', 'scan', 'timeout_ms', '' + timeout);
			uci.set('mynetworkmonitor', 'scan', 'parallel', '' + parallel);
			uci.commit('mynetworkmonitor');

			mkdir(WORK, 0700);
			unlink(PROGRESS);
			unlink(WORK + '/error.txt');

			const scan = sprintf('%s --targets %s --ports %s --timeout-ms %d --parallel %d --output %s --progress %s',
				SCANNER, shell_quote(targets), shell_quote(ports), timeout, parallel,
				shell_quote(RESULT), shell_quote(PROGRESS));
			// The shell writes its own ID and then becomes the scanner (exec), so the
			// saved ID is the scanner's. Errors of the scanner go to a file for the page.
			const inner = sprintf('echo $$ > %s; exec %s 2> %s', shell_quote(PIDFILE), scan,
				shell_quote(WORK + '/error.txt'));
			const code = system(sprintf('sh -c %s > /dev/null 2>&1 &', shell_quote(inner)));

			return (code == 0) ? { started: true } : { error: 'The scanner could not be started.' };
		}
	},

	stop: {
		call: function() {
			const pid = running_pid();
			if (pid == null)
				return { stopped: false };
			// SIGTERM: the scanner finishes the attempts in flight and writes a partial result.
			system(sprintf('kill -TERM %d', pid));
			return { stopped: true };
		}
	}
};

return { 'luci.mynetworkmonitor': methods };
