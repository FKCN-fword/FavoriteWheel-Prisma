#pragma once
#include <algorithm>
namespace Wheel
{
    inline float SmoothPhase(float progress, float start = 0.f, float finish = 1.f)
    {
        const float x = std::clamp((progress - start) / (finish - start), 0.f, 1.f);
        return x * x * (3.f - 2.f * x);
    }
    // Sweep clockwise from the top. Each blade has the same travel time;
    // reversing this mapping folds the last blade first without changing pose.
    inline float BladeExpansion(float progress, int slot, int count)
    {
        const float stagger = count > 1 ? .48f : 0.f;
        const float delay = stagger * std::clamp(slot, 0, std::max(0, count - 1)) / std::max(1, count - 1);
        const float x = std::clamp((progress - delay) / (1.f - stagger), 0.f, 1.f);
        // Ease at both ends instead of spending most travel in the first frame.
        return SmoothPhase(x);
    }
    inline float TransitionOpacity(float progress)
    {
        // The sweep remains visible; global fading only softens its endpoints.
        return SmoothPhase(progress, 0.f, .28f);
    }
    // Render-only: never postpones unpausing or item activation. Raw value also
    // drives geometry so reversing halfway reuses exactly the same visual pose.
    struct Transition
    {
        float value = 0;
        float Update(bool open, bool enabled, bool allowClose, float seconds)
        {
            if (!enabled || (!open && !allowClose))
                value = open ? 1.f : 0.f;
            else
                value = std::clamp(value + (open ? std::max(0.f, seconds) / .22f : -std::max(0.f, seconds) / .22f), 0.f,
                                   1.f);
            return TransitionOpacity(value);
        }
    };
} // namespace Wheel
