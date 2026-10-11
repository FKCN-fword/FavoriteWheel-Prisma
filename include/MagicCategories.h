#pragma once
#include "WheelLogic.h"
#include <optional>

namespace Wheel {
    // SpellType is the engine enum at runtime. Keeping the policy independent
    // of game objects lets tests exercise active/passive filtering directly.
    template<class SpellType> constexpr std::optional<Category> MagicCategory(bool shout,SpellType type) {
        if(shout)return Category::Shouts;
        switch(type) {
        case SpellType::kSpell:return Category::Spells;
        case SpellType::kPower:
        case SpellType::kLesserPower:
        case SpellType::kVoicePower:return Category::Powers;
        default:return std::nullopt; // Passive abilities, diseases and internal effects cannot be equipped.
        }
    }
}
