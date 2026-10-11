#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
namespace Wheel {
    // Partition physical instance counts before filtering, so excluded instances
    // never donate their counts to a favorite or produce phantom spare objects.
    struct InventoryBudget {
        std::int64_t remaining;
        int Take(int count) {
            const int used=static_cast<int>(std::min<std::int64_t>(std::max(1,count),std::max<std::int64_t>(0,remaining)));
            remaining-=used;return used;
        }
        int Spare() const {return static_cast<int>(std::clamp<std::int64_t>(remaining,0,std::numeric_limits<int>::max()));}
    };
    inline bool InventoryVisible(bool all,bool favorited){return all || favorited;}
    // Double Favorite As Important preserves ExtraHotkey for item protection,
    // using 0xFA for an Important item that must be excluded from favorites.
    // Interpret that private marker only while its provider is actually loaded.
    inline bool InventoryFavorite(bool hasHotkey,int slot,bool importantProvider) {
        return hasHotkey && !(importantProvider && slot==0xFA);
    }
}
