#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Wheel {
    // Display-only values: no Form/ExtraDataList/Actor pointer crosses to Present.
    struct EffectInfo {
        std::string name;
        std::optional<float> magnitude;
        std::uint32_t duration=0, area=0;
    };
    struct ItemInfo {
        std::optional<float> weight, damage, armor, magicka;
        std::optional<std::int32_t> value;
        bool baseDamage=false, costPerSecond=false, enchantment=false;
        std::vector<EffectInfo> effects;
        std::uint32_t moreEffects=0;
        bool HasData() const {return weight || value || damage || armor || magicka || !effects.empty() || moreEffects;}
    };
    inline constexpr std::size_t maxItemEffects=6;
    struct InfoRow {std::string label,value;};
    struct InfoText {std::vector<InfoRow> stats,effects;std::string heading,more;};
    using InfoTranslation=std::function<std::string(const char*)>;
    std::optional<float> ValidStat(float value);
    std::string InfoNumber(float value);
    // Applies UI flags and caps saved text; no guess at script/Perk-adjusted effects.
    void AddEffect(ItemInfo& target,std::string name,float magnitude,std::uint32_t duration,std::uint32_t area,
        bool hidden,bool noMagnitude,bool noDuration,bool noArea);
    InfoText DescribeItem(const ItemInfo& info,const InfoTranslation& translate);
    std::string ItemInfoGlyphs(const ItemInfo& info);
}
