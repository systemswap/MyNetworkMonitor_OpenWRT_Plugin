#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Installs or updates MyNetworkMonitor on a router with OpenWrt 25.12. Run it on the router (SSH):
#
#   wget -O - https://raw.githubusercontent.com/systemswap/MyNetworkMonitor_OpenWRT_Plugin/main/packaging/install.sh | sh
#
# It picks the scanner for the router's package architecture from the newest release that has one, checks both
# packages against SHA256SUMS and installs them. Without internet on the router: copy this script together with the
# two packages and SHA256SUMS into one folder on the router (e.g. /tmp) and run  sh /tmp/install.sh  - it then takes
# the packages from that folder.
#
# The packages are not signed by OpenWrt, so apk needs --allow-untrusted; the upload page of LuCI does not pass it on.
#
# Everything is in functions and only the last line starts it: piped into sh, a script cut off during the download
# does nothing.

REPOSITORY=systemswap/MyNetworkMonitor_OpenWRT_Plugin
# MNM_RELEASES only for tests against a local copy of the release list
RELEASES=${MNM_RELEASES:-https://api.github.com/repos/$REPOSITORY/releases}
WORK=/tmp/mynetworkmonitor-install

say() { echo "MyNetworkMonitor: $*"; }
fail() { echo "MyNetworkMonitor: $*" >&2; exit 1; }

check_system() {
	command -v apk > /dev/null ||
		fail "this router has no apk - the packages need OpenWrt 25.12 or later. Please update OpenWrt first."
	release=$(. /etc/openwrt_release 2> /dev/null && echo "$DISTRIB_RELEASE")
	case "$release" in
		25.12*) ;;
		*) say "note: built for OpenWrt 25.12, this router runs ${release:-an unknown version}." ;;
	esac
	ARCH=$(apk --print-arch) || fail "apk --print-arch failed."
	say "router: OpenWrt ${release:-?}, package architecture $ARCH"
}

# Sets the variable $1 to the one file in the folder $2 matching the pattern $3, or to nothing; more than one is an
# error.
only_file() {
	var=$1 pattern=$2/$3
	set -- $pattern
	[ -e "$1" ] || set --
	[ $# -le 1 ] || fail "several files $pattern - keep only one version in the folder."
	eval "$var=\${1:-}"
}

# Packages next to the script (copied there without internet); sets SCAN and LUCI when both are there.
find_local() {
	case "$0" in
		*install.sh) [ -f "$0" ] || return 1 ;;
		*) return 1 ;;
	esac
	dir=$(cd "$(dirname "$0")" && pwd)
	only_file SCAN "$dir" "mynetworkmonitor-scan_*_$ARCH.apk"
	only_file LUCI "$dir" "luci-app-mynetworkmonitor_*_all.apk"
	[ -n "$SCAN" ] && [ -n "$LUCI" ] || return 1
	say "using the packages in $dir"
	SUMS=$dir/SHA256SUMS
	[ -f "$SUMS" ] || SUMS=
}

# Newest release with a scanner for $ARCH: downloads both packages and SHA256SUMS to $WORK.
download() {
	rm -rf "$WORK" && mkdir -p "$WORK" || fail "cannot create $WORK."
	wget -q -O "$WORK/releases.json" "$RELEASES" ||
		fail "cannot reach GitHub. Does the router have internet? Otherwise copy the files by hand (ANLEITUNG.txt)."
	urls=$(jsonfilter -i "$WORK/releases.json" -e '@[*].assets[*].browser_download_url') ||
		fail "the release list from GitHub cannot be read."
	# newest release first; the first scanner for this architecture decides the release
	scan_url=$(echo "$urls" | grep "/mynetworkmonitor-scan_[^/]*_$ARCH\.apk\$" | head -n 1)
	if [ -z "$scan_url" ]; then
		available=$(echo "$urls" | sed -n 's|.*/mynetworkmonitor-scan_[^_/]*_\(.*\)\.apk$|\1|p' | sort -u | tr '\n' ' ')
		fail "no scanner for $ARCH yet. Available: ${available:-none}"
	fi
	base=${scan_url%/*}
	luci_url=$(echo "$urls" | grep "^$base/luci-app-mynetworkmonitor_[^/]*_all\.apk\$" | head -n 1)
	sums_url=$(echo "$urls" | grep -x "$base/SHA256SUMS")
	[ -n "$luci_url" ] && [ -n "$sums_url" ] || fail "the release ${base##*/} is incomplete - please try again later."
	say "downloading release ${base##*/} ..."
	for url in "$scan_url" "$luci_url" "$sums_url"; do
		wget -q -O "$WORK/${url##*/}" "$url" || fail "download of ${url##*/} failed."
	done
	SCAN=$WORK/${scan_url##*/}
	LUCI=$WORK/${luci_url##*/}
	SUMS=$WORK/SHA256SUMS
}

# Both packages against their lines in SHA256SUMS.
verify() {
	if [ -z "$SUMS" ]; then
		say "note: no SHA256SUMS next to the packages, checksums not checked."
		return
	fi
	for file in "$SCAN" "$LUCI"; do
		name=${file##*/}
		expected=$(awk -v n="$name" '$2 == n || $2 == "*" n { print $1 }' "$SUMS")
		[ -n "$expected" ] || fail "$name is not listed in SHA256SUMS."
		actual=$(sha256sum "$file" | cut -d ' ' -f 1)
		[ "$actual" = "$expected" ] || fail "$name is damaged (checksum differs) - please download it again."
	done
	say "checksums ok"
}

install_packages() {
	# stdin is this script when piped into sh; apk must not read it
	apk add --allow-untrusted "$SCAN" "$LUCI" < /dev/null || fail "apk could not install the packages (messages above)."
	version=${LUCI##*/luci-app-mynetworkmonitor_}
	say "installed ${version%_all.apk}. Reload the router page and open Network -> MyNetworkMonitor."
}

main() {
	check_system
	find_local || download
	verify
	install_packages
	[ "${SCAN#$WORK/}" = "$SCAN" ] || rm -rf "$WORK"
}

main "$@"
