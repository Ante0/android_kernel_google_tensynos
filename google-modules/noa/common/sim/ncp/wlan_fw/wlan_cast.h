#ifndef WLAN_CAST_H
#define WLAN_CAST_H

#if defined(__cplusplus)
#define WLAN_CONST_CAST(type, addr) (const_cast<type>(addr))
#define WLAN_REINTERPRET_CAST(type, addr) (reinterpret_cast<type>(addr))
#define WLAN_STATIC_CAST(type, addr) (static_cast<type>(addr))
#else /* __cplusplus */
#define WLAN_C_STYLE_CAST(type, addr) ((type)(addr))
#define WLAN_CONST_CAST(type, addr) WLAN_C_STYLE_CAST(type, addr)
#define WLAN_REINTERPRET_CAST(type, addr) WLAN_C_STYLE_CAST(type, addr)
#define WLAN_STATIC_CAST(type, addr) WLAN_C_STYLE_CAST(type, addr)
#endif /* __cplusplus */

#endif /* WLAN_CAST_H */
