/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef WLAN_CONFIG_H
#define WLAN_CONFIG_H
/* cfg80211 core */
#define CONFIG_CFG80211 1

/* Defaults defined by net/wireless/Kconfig */
#define CONFIG_CFG80211_DEFAULT_PS 1
#define CONFIG_CFG80211_CRDA_SUPPORT 1
#define CONFIG_CFG80211_REQUIRE_SIGNED_REGDB 1
#define CONFIG_CFG80211_USE_KERNEL_REGDB_KEYS 1

/* mac80211 Core */
#define CONFIG_MAC80211 1

/* Rate Control (Minstrel is the default and required for soft-mac) */
#define CONFIG_MAC80211_HAS_RC 1
#define CONFIG_MAC80211_RC_MINSTREL 1
#define CONFIG_MAC80211_RC_DEFAULT_MINSTREL 1
#define CONFIG_MAC80211_RC_DEFAULT "minstrel_ht"
#define CONFIG_MAC80211_STA_HASH_MAX_SIZE 0
#endif // WLAN_CONFIG_H