#pragma once
#include "Favorites.h"
#include "FunctionNavigation.h"
#include <vector>
namespace Wheel::FaceLight {

    void Discover(); // Only GetAPI; safe at PostPostLoad.
    bool Available(); // Compatible provider detected; readiness is reported separately.
    void Capture(); // Chained player-update callback, before requesting the pause menu.
    void Clear();
    std::vector<Item> Entries(Section section);
    std::string Names();
    void Execute(const Item& item); // Chained player-update callback, after pause ends.
}
