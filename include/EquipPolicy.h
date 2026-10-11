#pragma once
namespace Wheel {
    inline bool UnequipWeapon(bool twoHanded,bool left,bool wornRight,bool wornLeft) {
        return twoHanded ? wornRight||wornLeft : left?wornLeft:wornRight;
    }
}
