# SPDX-License-Identifier: GPL-2.0-only
#
# The LuCI page. A plain package instead of luci.mk from the LuCI sources: the
# SDK then needs no LuCI feed, and the files are installed exactly as they lie
# below htdocs/ and root/. Independent of the processor (PKGARCH all).

include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-mynetworkmonitor
MNM_VERSION:=$(shell cat "$(CURDIR)/../../VERSION")
PKG_VERSION:=$(subst -,_,$(MNM_VERSION))
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0-only
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk

define Package/luci-app-mynetworkmonitor
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=LuCI page for the MyNetworkMonitor network scanner
  DEPENDS:=+luci-base +rpcd-mod-ucode +mynetworkmonitor-scan
  PKGARCH:=all
endef

define Package/luci-app-mynetworkmonitor/description
  Page under Network -> MyNetworkMonitor: choose ranges and ports, start and
  stop a scan, and see which devices answer on which ports.
endef

define Package/luci-app-mynetworkmonitor/conffiles
/etc/config/mynetworkmonitor
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile
endef

define Package/luci-app-mynetworkmonitor/install
	$(INSTALL_DIR) $(1)/www
	$(CP) ./htdocs/* $(1)/www/
	$(CP) ./root/* $(1)/
endef

define Package/luci-app-mynetworkmonitor/postinst
#!/bin/sh
# After installing on a running system: reload rpcd for the new backend and
# drop the cached menu, so that the page appears without a reboot.
[ -n "$${IPKG_INSTROOT}" ] && exit 0
# uci-defaults run only at boot by themselves; after an update on a running
# system the settings are brought to the current format here.
[ -x /etc/uci-defaults/90-luci-mynetworkmonitor ] && /etc/uci-defaults/90-luci-mynetworkmonitor && rm -f /etc/uci-defaults/90-luci-mynetworkmonitor
rm -rf /tmp/luci-indexcache* /tmp/luci-modulecache 2>/dev/null
/etc/init.d/rpcd reload 2>/dev/null
exit 0
endef

$(eval $(call BuildPackage,luci-app-mynetworkmonitor))
