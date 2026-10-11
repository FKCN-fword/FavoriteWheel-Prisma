#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace Wheel {
    constexpr int slots = 10;
    enum class Category { Weapons, Armor, Potions, Food, Spells, Shouts, Powers, Other, Count };
    constexpr int categoryCount = static_cast<int>(Category::Count);
    inline int Wrap(int value, int count) { return count > 0 ? (value % count + count) % count : 0; }
    inline int PageCount(std::size_t count) { return std::max(1, static_cast<int>((count + slots - 1) / slots)); }
    // Slot zero points north. A neutral centre cancels the selection.
    inline int HitTest(float x, float y) {
        if (x * x + y * y < 0.32f * 0.32f) return -1;
        constexpr float tau = 2.0f * std::numbers::pi_v<float>;
        float angle = std::atan2(x, -y);
        if (angle < 0) angle += tau;
        return static_cast<int>(std::floor(angle / tau * slots + 0.5f)) % slots;
    }
    inline void MovePointer(float& x, float& y, float dx, float dy) {
        x += dx; y += dy;
        const float length = std::sqrt(x * x + y * y);
        if (length > 1.0f) { x /= length; y /= length; }
    }
}
