#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace Wheel {
    constexpr int slots = 6;
    enum class Category { Weapons, Armor, Potions, Food, Spells, Shouts, Powers, Other, Count };
    constexpr int categoryCount = static_cast<int>(Category::Count);
    inline int Wrap(int value, int count) { return count > 0 ? (value % count + count) % count : 0; }
    inline int PageCount(std::size_t count) { return std::max(1, static_cast<int>((count + slots - 1) / slots)); }
    // Main groups shown on the inner half-ring. Each group is represented by its first category.
    constexpr int groupCount = 3;
    constexpr Category groupCategory[groupCount] = {Category::Weapons, Category::Potions, Category::Spells};
    constexpr std::uint32_t groupMask = (1u << static_cast<int>(Category::Weapons)) | (1u << static_cast<int>(Category::Potions)) |
                                        (1u << static_cast<int>(Category::Spells));
    // Weapons+Apparel, Potions+Food, Spells+Shouts+Powers. Other items are not shown on the wheel.
    inline int GroupOf(Category category) {
        switch (category) {
        case Category::Weapons:
        case Category::Armor: return 0;
        case Category::Potions:
        case Category::Food: return 1;
        case Category::Spells:
        case Category::Shouts:
        case Category::Powers: return 2;
        default: return -1;
        }
    }

    // Two-level half-ring (DOM style). Pointer unit radius 1 == 282 design units (outer edge of the item ring).
    // Angles: 0 = +x axis, -pi/2 = top, +pi/2 = bottom; only the right half (x > 0) selects.
    constexpr float ringScale = 282.0f;
    constexpr float groupInner = 88.0f / ringScale, groupOuter = 196.0f / ringScale, itemInner = 200.0f / ringScale;
    // Outer ring: the whole right half split evenly between the items of the current page.
    inline int ItemHit(float x, float y, int count) {
        if (count <= 0 || x <= 0.0f) return -1;
        if (std::hypot(x, y) < itemInner) return -1;
        const float angle = std::atan2(x, -y); // 0 at the top, pi at the bottom
        return std::clamp(static_cast<int>(std::floor(angle / std::numbers::pi_v<float> * count)), 0, count - 1);
    }
    // Inner ring: index of the group under the pointer, or -1.
    inline int GroupHit(float x, float y, int groups) {
        if (groups <= 0 || x <= 0.0f) return -1;
        const float r = std::hypot(x, y);
        if (r < groupInner || r > groupOuter) return -1;
        const float angle = std::atan2(x, -y);
        return std::clamp(static_cast<int>(std::floor(angle / std::numbers::pi_v<float> * groups)), 0, groups - 1);
    }
    inline void MovePointer(float& x, float& y, float dx, float dy) {
        x += dx; y += dy;
        const float length = std::sqrt(x * x + y * y);
        if (length > 1.0f) { x /= length; y /= length; }
    }
}
