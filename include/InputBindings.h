#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <array>
namespace Wheel {
// DirectInput codes. Modifier bits: Shift=1, Ctrl=2, Alt=4.
inline int ModifierBit(std::uint32_t code) {
  switch (code) {
  case 42:
  case 54:
    return 1;
  case 29:
  case 157:
    return 2;
  case 56:
  case 184:
    return 4;
  default:
    return 0;
  }
}
enum class Opening { None, Favorites, Actions };
enum class PadWheelCommand { None, PreviousCategory, NextCategory, UseRight, UseLeft, Back, PreviousPage, NextPage };
inline PadWheelCommand ControllerCommand(std::uint32_t code,int categoryButtons) {
  if(code==266)return PadWheelCommand::PreviousPage;
  if(code==267)return PadWheelCommand::NextPage;
  if(code==268)return PadWheelCommand::PreviousCategory;
  if(code==269)return PadWheelCommand::NextCategory;
  if(code==276)return PadWheelCommand::UseRight;
  if(code==278)return PadWheelCommand::UseLeft;
  if(code==277)return PadWheelCommand::Back;
  const bool triggers=categoryButtons==1;
  if(code==(triggers?280u:274u))return PadWheelCommand::PreviousCategory;
  if(code==(triggers?281u:275u))return PadWheelCommand::NextCategory;
  if(code==(triggers?274u:280u))return PadWheelCommand::UseLeft;
  if(code==(triggers?275u:281u))return PadWheelCommand::UseRight;
  return PadWheelCommand::None;
}
inline Opening KeyboardOpening(std::uint32_t code, int held, int favoriteKey,
                               int favoriteModifier, int actionKey,
                               int actionModifier) {
  // Exact chords avoid Shift+Q accidentally opening the unmodified Q wheel.
  // If both bindings coincide, actions take priority deterministically.
  if (actionKey >= 0 && code == static_cast<std::uint32_t>(actionKey) &&
      held == actionModifier)
    return Opening::Actions;
  if (favoriteKey >= 0 && code == static_cast<std::uint32_t>(favoriteKey) &&
      held == favoriteModifier)
    return Opening::Favorites;
  return Opening::None;
}
inline Opening GamepadOpening(std::uint32_t code, int favoriteKey,
                              bool favoriteModifier, bool actionModifier) {
  if (favoriteKey < 266 || favoriteKey > 281 ||
      code != static_cast<std::uint32_t>(favoriteKey))
    return Opening::None;
  if (actionModifier)
    return Opening::Actions;
  return favoriteModifier ? Opening::Favorites : Opening::None;
}
// D-pad navigation follows physical edges, independent of HeldDuration().
// Observe outside the wheel too, so the held opening key cannot also page.
class ControllerPageButtons {
  std::array<bool, 2> held{}, blocked{};
  std::uint32_t first;
public:
  explicit ControllerPageButtons(std::uint32_t firstCode=266):first(firstCode){}
  bool Observe(std::uint32_t code, bool pressed, bool nativeDown) {
    if (code < first || code > first+1) return false;
    const auto at = code - first;
    if (!pressed || nativeDown) blocked[at] = false;
    const bool down = pressed && !held[at] && !blocked[at];
    held[at] = pressed;
    return down;
  }
  // A held key after focus/load/device reset needs release or a new native down.
  void Reset() { held.fill(false); blocked.fill(true); }
};
inline float StickAxis(float value) {
  constexpr float deadzone = .2f;
  return std::abs(value) <= deadzone
             ? 0.f
             : std::copysign((std::min(std::abs(value), 1.f) - deadzone) /
                                 (1.f - deadzone),
                             value);
}
inline void AimStick(float &x, float &y, float sx, float sy) {
  const float length = std::hypot(sx, sy);
  if (length <= .2f)
    return; // Keep the selected entry when releasing the stick to press
            // confirm.
  x = sx / length;
  y = -sy / length;
}
} // namespace Wheel
