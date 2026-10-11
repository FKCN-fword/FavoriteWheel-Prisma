#pragma once
namespace Wheel::Outfits {
    enum class StepDecision { Issue, Wait, Advance, Timeout };
    inline StepDecision DecideStep(bool equipped,bool requestedEquipped,bool issued,double elapsedSeconds) {
        if(equipped==requestedEquipped)return StepDecision::Advance;
        if(!issued)return StepDecision::Issue;
        return elapsedSeconds>.75?StepDecision::Timeout:StepDecision::Wait;
    }
}
