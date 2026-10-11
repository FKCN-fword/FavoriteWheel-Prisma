#pragma once
#include <mutex>
#include <cmath>
#include <string>
#include <vector>

namespace Wheel {
    struct NameHit {float x;std::size_t offset;};
    inline std::mutex nameHitMutex;
    inline std::string nameHitText;
    inline std::vector<NameHit> nameHits;
    inline void PublishNameHits(const std::string& text,std::vector<NameHit> hits) {
        std::lock_guard lock(nameHitMutex);nameHitText=text;nameHits=std::move(hits);
    }
    inline std::size_t HitName(const std::string& text,float x) {
        std::lock_guard lock(nameHitMutex);
        if(nameHitText!=text || nameHits.empty())return text.size();
        auto best=nameHits.front();
        for(const auto& hit:nameHits)if(std::abs(hit.x-x)<std::abs(best.x-x))best=hit;
        return best.offset;
    }
}
