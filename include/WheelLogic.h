#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace Wheel {
    constexpr int slots = 8;
    enum class Category { Weapons, Armor, Potions, Food, Spells, Shouts, Powers, Other, Count };
    constexpr int categoryCount = static_cast<int>(Category::Count);
    inline int Wrap(int value, int count) { return count > 0 ? (value % count + count) % count : 0; }
    inline int PageCount(std::size_t count) { return std::max(1, static_cast<int>((count + slots - 1) / slots)); }
    // Half-ring (DOM style): the right semicircle, slot 0 at the top, clockwise.
    // The pointer must be outside the inner radius (88/190 of the outer radius);
    // the empty left half and the neutral centre cancel the selection.
    inline int HitTest(float x, float y) {
        constexpr float inner = 88.0f / 190.0f;
        if (x * x + y * y < inner * inner) return -1;
        if (x <= 0.0f) return -1;
        const float angle = std::atan2(x, -y); // 0 at the top, pi at the bottom (x > 0)
        const int slot = static_cast<int>(std::floor(angle / std::numbers::pi_v<float> * slots));
        return std::clamp(slot, 0, slots - 1);
    }
    inline void MovePointer(float& x, float& y, float dx, float dy) {
        x += dx; y += dy;
        const float length = std::sqrt(x * x + y * y);
        if (length > 1.0f) { x /= length; y /= length; }
    }
}
