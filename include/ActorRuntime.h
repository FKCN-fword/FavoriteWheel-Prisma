#pragma once
#include <RE/Skyrim.h>

namespace Wheel::ActorRuntime
{
    // Matches pinned CommonLibSSE-NG Actor.cpp: flat runtimes (including 1.7) / VR.
    inline bool IsDead(const RE::Actor *actor)
    {
        return !actor || REL::RelocateVirtual<decltype(&RE::Actor::IsDead)>(0x99, 0x9A, actor, true);
    }
    inline bool DrinkPotion(RE::Actor *actor, RE::AlchemyItem *potion, RE::ExtraDataList *extra)
    {
        return actor && potion &&
               REL::RelocateVirtual<decltype(&RE::Actor::DrinkPotion)>(0x10F, 0x111, actor, potion, extra);
    }
} // namespace Wheel::ActorRuntime
