#include "ItemInfoCapture.h"
#include <RE/Skyrim.h>

namespace Wheel {
    namespace {
        void Effects(ItemInfo& info,const RE::MagicItem* magic) {
            if(!magic)return;
            for(const auto effect:magic->effects) {
                if(!effect || !effect->baseEffect)continue;
                const auto base=effect->baseEffect;const auto& flags=base->data.flags;
                using Flag=RE::EffectSetting::EffectSettingData::Flag;
                const char* name=base->GetName();
                AddEffect(info,name?name:"",effect->effectItem.magnitude,effect->effectItem.duration,effect->effectItem.area,
                    flags.all(Flag::kHideInUI),flags.all(Flag::kNoMagnitude),flags.all(Flag::kNoDuration),flags.all(Flag::kNoArea));
            }
        }
    }
    ItemInfo CaptureItemInfo(RE::TESBoundObject* object,RE::ExtraDataList* extra,RE::PlayerCharacter* player) {
        ItemInfo info;if(!object || !player)return info;
        // Own only the temporary list container. It borrows this exact instance;
        // the entry destructor deletes list nodes, never the game's extra data.
        // A grouped inventory entry could select another item's temper/enchantment.
        RE::InventoryEntryData instance(object,1);instance.AddExtraList(extra);
        info.weight=ValidStat(instance.GetWeight());
        const auto value=instance.GetValue();if(value>=0)info.value=value;
        if(const auto weapon=object->As<RE::TESObjectWEAP>();weapon && !weapon->IsStaff())
            info.damage=ValidStat(player->GetDamage(&instance));
        if(object->As<RE::TESObjectARMO>())info.armor=ValidStat(player->GetArmorValue(&instance));
        if(const auto ammo=object->As<RE::TESAmmo>()) {
            info.damage=ValidStat(ammo->GetRuntimeData().data.damage);info.baseDamage=true;
        }
        if(const auto potion=object->As<RE::AlchemyItem>())Effects(info,potion);
        else if(const auto scroll=object->As<RE::ScrollItem>())Effects(info,scroll);
        else if(const auto enchantment=instance.GetEnchantment()) {
            info.enchantment=true;Effects(info,enchantment);
        }
        return info;
    }
    ItemInfo CaptureSpellInfo(RE::SpellItem* spell,RE::PlayerCharacter* player) {
        ItemInfo info;if(!spell || !player)return info;
        if(spell->data.spellType==RE::MagicSystem::SpellType::kSpell) {
            info.magicka=ValidStat(spell->CalculateMagickaCost(player));
            info.costPerSecond=spell->data.castingType==RE::MagicSystem::CastingType::kConcentration;
        }
        Effects(info,spell);return info;
    }
}
