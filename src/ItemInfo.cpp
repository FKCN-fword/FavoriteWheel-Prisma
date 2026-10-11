#include "ItemInfo.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace Wheel {
    std::optional<float> ValidStat(float value) {
        return std::isfinite(value) && value>=0?std::optional<float>(value):std::nullopt;
    }
    std::string InfoNumber(float value) {
        if(!std::isfinite(value))return "";
        if(std::abs(value)<.05f)value=0;
        std::ostringstream stream;stream.imbue(std::locale::classic());
        stream<<std::fixed<<std::setprecision(1)<<value;
        auto text=stream.str();if(text.ends_with(".0"))text.resize(text.size()-2);
        return text;
    }
    void AddEffect(ItemInfo& target,std::string name,float magnitude,std::uint32_t duration,std::uint32_t area,
        bool hidden,bool noMagnitude,bool noDuration,bool noArea) {
        if(hidden)return;
        if(target.effects.size()>=maxItemEffects){++target.moreEffects;return;}
        // Trim only on a UTF-8 boundary; mod-provided names have no fixed size.
        if(name.size()>256) {
            std::size_t end=256;while(end>0 && (static_cast<unsigned char>(name[end])&0xC0)==0x80)--end;
            name.resize(end);name+="...";
        }
        EffectInfo row;row.name=std::move(name);
        if(!noMagnitude && std::isfinite(magnitude))row.magnitude=magnitude;
        row.duration=noDuration?0:duration;row.area=noArea?0:area;
        target.effects.push_back(std::move(row));
    }
    InfoText DescribeItem(const ItemInfo& info,const InfoTranslation& tr) {
        InfoText result;
        auto stat=[&](const char* key,const std::optional<float>& n,const std::string& suffix="") {
            if(n && ValidStat(*n))result.stats.push_back({tr(key),InfoNumber(*n)+suffix});
        };
        stat(info.baseDamage?"infoBaseDamage":"infoDamage",info.damage);
        stat("infoArmor",info.armor);
        stat("infoMagicka",info.magicka,info.costPerSecond?tr("infoPerSecond"):"");
        stat("infoWeight",info.weight);
        if(info.value && *info.value>=0)result.stats.push_back({tr("infoValue"),std::to_string(*info.value)});
        if(!info.effects.empty() || info.moreEffects)result.heading=tr(info.enchantment?"infoEnchantment":"infoEffects");
        for(const auto& effect:info.effects) {
            std::string values;
            auto append=[&](std::string text){if(!values.empty())values+=" · ";values+=text;};
            if(effect.magnitude && std::isfinite(*effect.magnitude))append(tr("infoMagnitude")+" "+InfoNumber(*effect.magnitude));
            if(effect.duration)append(std::to_string(effect.duration)+" "+tr("infoSeconds"));
            if(effect.area)append(tr("infoArea")+" "+std::to_string(effect.area)+" "+tr("infoFeet"));
            result.effects.push_back({effect.name.empty()?tr("infoUnnamedEffect"):effect.name,std::move(values)});
        }
        if(info.moreEffects)result.more="+"+std::to_string(info.moreEffects)+" "+tr("infoMoreEffects");
        return result;
    }
    std::string ItemInfoGlyphs(const ItemInfo& info) {
        std::string text;for(const auto& effect:info.effects)text+=effect.name;
        return text;
    }
}
