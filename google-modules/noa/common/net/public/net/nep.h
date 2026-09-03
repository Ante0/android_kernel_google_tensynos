#pragma once

// standard c/c++ headers
#include <cinttypes>
#include <cstddef>
#include <memory>
#include <vector>

#include "map_def.h"

// This is a temporary workaround to include the correct header file for the vpn_controller.
// TODO(b/378030778): Remove this conditional include after the refactoring in NOA core is merged.
#if __has_include("vpn/vpn_controller.h")
#include "vpn/vpn_controller.h"
#else
#include "vpn_controller/vpn_controller.h"
#endif

namespace noa::apps::nep::netengine {
namespace internal {
// TODO(b/378030778): Remove this conditional include after the refactoring in NOA core is merged.
#if __has_include("vpn/vpn_controller.h")
using VpnController = module::vpn::VpnController;
#else
using VpnController = module::vpn_controller::VpnController;
#endif
}

enum CallbackCmdType {
  kCallbackCmdMax,
};

class Nep {
 public:
  // May need to allow configure mailbox id in parameter for APC and NCP.
  Nep() {
    memset(&empty_4key, 0, sizeof(Tether4Key));
    memset(&empty_upstream_6key, 0, sizeof(TetherUpstream6Key));
    memset(&empty_downstream_6key, 0, sizeof(TetherDownstream6Key));
  };

  Nep(const Nep&) = delete;
  Nep(Nep&&) = default;

  // Get Nep instance.
  static Nep* GetService();

  // Handle mailbox interrupt from NCP, NCP may update wifi flow id table into
  // netengine.
  int32_t NcpMailboxNotifierHandler();

  // Handle mailbox interrupt from APC, APC may update ipv4/ipv6 offload rules
  // into netengine.
  int32_t ApcMailboxNotifierHandler();

  // The implementation for handling RPC nep command service.
  int32_t Command(uint32_t cmd, const void* msg, void* res_buf);

  // The implementation for handling RPC vpn command service.
  int32_t LargeCommand(uint32_t cmd, const void* msg);

  /// @brief Returns the active VpnController instance.
  ///
  /// @return Pointer to the active VpnController instance. May be nullptr if
  /// VPN offload is permanently disabled.
  internal::VpnController* GetVpnController() {
    return vpn_controller_;
  }

  // The implementation for callback to APC
  int32_t Callback(CallbackCmdType cmd, const std::byte* msg);

  ~Nep() = default;

  /// @brief Initializes the Nep.
  ///
  /// Injects the VpnController instance and sets up internal Nep state.
  ///
  /// The injected VpnController can be nullptr to permanently disable VPN
  /// offload.
  ///
  /// @param vpn_controller Pointer to the VpnController instance to use (may be nullptr).
  /// @return 0 on success, a non-zero error code on failure.
  int32_t Init(internal::VpnController* vpn_controller = nullptr);

  int32_t Deinit();

  int32_t nep_put_upstream4_entry(const Tether4Entry* entry);
  int32_t nep_remove_upstream4_entry(const Tether4Key* key);
  int32_t nep_get_upstream4_value(const Tether4Key* key, Tether4Value* value);
  int32_t nep_get_upstream6_value(const TetherUpstream6Key* key,
                                  Tether6Value* result);
  int32_t nep_get_downstream6_value(const TetherDownstream6Key* key,
                                    Tether6Value* result);
  uint32_t GetResponseSize(uint32_t cmd);

 private:
  void RegisterMailboxNotifierHandler();
  Tether4Key empty_4key;
  TetherUpstream6Key empty_upstream_6key;
  TetherDownstream6Key empty_downstream_6key;
  TetherStatsValue empty_stats = {0, 0, 0, 0, 0, 0};

  // Pointer to the active VPN controller. Would be nullptr if VPN offload is
  // permanently disabled.
  // Initialized by the Init() function.
  internal::VpnController* vpn_controller_ = nullptr;
};

}  // namespace noa::apps::nep::netengine
