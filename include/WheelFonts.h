#pragma once
#include "Wheel.h"
#include <algorithm>
#include <cmath>
// ImGui-free subset: the PrismaUI port keeps only the layout math used by input logic.
namespace Wheel {
    inline float LayoutScale(float width, float height, float userScale) {
        return std::min(std::min(width / 1280.0f, height / 900.0f) * userScale,
            std::min(width / 800.0f, height / 830.0f));
    }
    inline float ViewScale(float width,float height,const View& view) {
        if(view.settingsOpen || view.outfitDialog) return LayoutScale(width,height,view.config.scale);
        return std::min(std::min(width/1280.f,height/900.f)*view.config.wheelScale*.82f,
            std::min(width/810.f,height/820.f));
    }
}
