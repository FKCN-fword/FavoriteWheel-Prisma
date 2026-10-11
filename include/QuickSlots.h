#pragma once
#include <cstdint>
#include <string_view>
namespace Wheel {
inline constexpr int nativeQuickSlots = 8;
inline bool ValidQuickSlot(int slot) {
  return slot >= 0 && slot < nativeQuickSlots;
}
inline int QuickSlotFromKey(std::uint32_t scanCode, std::string_view event) {
  // Gameplay remaps may name a slot even while our menu context is active.
  if (event.starts_with("Hotkey"))
    return event.size() == 7 && event[6] >= '1' && event[6] <= '8'
               ? event[6] - '1'
               : -1;
  return scanCode >= 2 && scanCode <= 9 ? static_cast<int>(scanCode) - 2 : -1;
}
inline int AssignedQuickSlot(int current, int requested) {
  return !ValidQuickSlot(requested) || current == requested ? -1 : requested;
}
inline int ReassignedQuickSlot(int current, bool selected, int requested,
                               int next) {
  if (!ValidQuickSlot(requested))
    return current;
  if (selected)
    return next;
  return current == requested ? -1 : current;
}
} // namespace Wheel
