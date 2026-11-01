# SPDX-License-Identifier: GPL-2.0-only
#
# OpenWrt package of the scanner. The source is the folder scanner/ of this
# repository; this directory is used as a feed of the OpenWrt SDK
# ("src-link mynetworkmonitor <repository>/openwrt" in feeds.conf).

include $(TOPDIR)/rules.mk

PKG_NAME:=mynetworkmonitor-scan
# VERSION holds x.y.z-beta; apk allows no "-" inside the version, "_beta" is its form of a pre-release.
# The repository may lie below a path with spaces; every use of it is quoted.
MNM_SOURCE:=$(CURDIR)/../..
MNM_VERSION:=$(shell cat "$(MNM_SOURCE)/VERSION")
PKG_VERSION:=$(subst -,_,$(MNM_VERSION))
PKG_RELEASE:=1

PKG_LICENSE:=GPL-2.0-only
PKG_LICENSE_FILES:=LICENSE

include $(INCLUDE_DIR)/package.mk

define Package/mynetworkmonitor-scan
  SECTION:=net
  CATEGORY:=Network
  TITLE:=MyNetworkMonitor network scanner
endef

define Package/mynetworkmonitor-scan/description
  Scans networks for open TCP ports and the services behind them. Started by
  the LuCI page for each scan; it does not listen on any port.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) "$(MNM_SOURCE)/scanner/src" "$(MNM_SOURCE)/scanner/data" "$(MNM_SOURCE)/scanner/Makefile" \
		"$(MNM_SOURCE)/LICENSE" $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" LDFLAGS="$(TARGET_LDFLAGS)" \
		VERSION="$(MNM_VERSION)" BUILD="$(PKG_BUILD_DIR)/build" all
endef

define Package/mynetworkmonitor-scan/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/build/mynetworkmonitor-scan $(1)/usr/bin/
	# What the scanner can look for, in groups; read by the LuCI page and its backend.
	$(INSTALL_DIR) $(1)/usr/share/mynetworkmonitor
	$(INSTALL_DATA) $(PKG_BUILD_DIR)/data/services.json $(1)/usr/share/mynetworkmonitor/
	# The version for the LuCI page, from the same file VERSION as the package version.
	echo "$(MNM_VERSION)" > $(1)/usr/share/mynetworkmonitor/version
endef

$(eval $(call BuildPackage,mynetworkmonitor-scan))
