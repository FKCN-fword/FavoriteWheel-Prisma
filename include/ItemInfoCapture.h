#pragma once
#include "ItemInfo.h"
namespace RE {class TESBoundObject;class ExtraDataList;class SpellItem;class PlayerCharacter;}
namespace Wheel {
    ItemInfo CaptureItemInfo(RE::TESBoundObject* object,RE::ExtraDataList* instance,RE::PlayerCharacter* player);
    ItemInfo CaptureSpellInfo(RE::SpellItem* spell,RE::PlayerCharacter* player);
}
