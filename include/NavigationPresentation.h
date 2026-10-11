#pragma once
#include "WheelLogic.h"
#include <array>
namespace Wheel
{
    struct CategoryLabel
    {
        int offset = 0, index = 0;
    };
    struct CategoryRibbon
    {
        std::array<CategoryLabel, 5> labels{};
        int size = 0;
    };
    // Keep the selected label at offset zero; show each available type at most once.
    inline CategoryRibbon RibbonLabels(int current, int count)
    {
        CategoryRibbon result;
        if (count <= 0)
            return result;
        result.size = std::min(count, 5);
        const int first = -(result.size - 1) / 2;
        for (int i = 0; i < result.size; ++i)
        {
            const int offset = first + i;
            result.labels[i] = {offset, Wrap(current + offset, count)};
        }
        return result;
    }
    struct PageWindow
    {
        int first = 0, size = 1;
    };
    inline PageWindow VisiblePages(int total, int current)
    {
        total = std::max(1, total);
        const int size = std::min(total, 7);
        return {std::clamp(current - size / 2, 0, total - size), size};
    }
} // namespace Wheel
