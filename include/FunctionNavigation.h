#pragma once
namespace Wheel::FaceLight {
    enum class Section { Outfits, Lighting, Followers };
    constexpr int TypeCount(bool faceLightAvailable) { return faceLightAvailable?2:1; }
    constexpr Section VisibleSection(Section section,bool faceLightAvailable) {
        return faceLightAvailable?section:Section::Outfits;
    }
    // A/D switches the two top-level types, including from the followers child list.
    constexpr Section ChangeType(Section current, int direction,bool faceLightAvailable=true) {
        if(!faceLightAvailable)return Section::Outfits;
        if (!direction) return current;
        return current == Section::Outfits ? Section::Lighting : Section::Outfits;
    }
}
