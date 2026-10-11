#pragma once
namespace Wheel {
    enum class LeftStickMode { Gameplay, HeldDirection, Block };
    inline LeftStickMode ControllerLeftStick(bool split,bool capture,bool open,bool modal,int timeMode,
        bool transitioning) {
        if(!capture)return LeftStickMode::Gameplay;
        if(!split)return LeftStickMode::HeldDirection;
        return (open || transitioning) && !modal && timeMode!=0?LeftStickMode::Gameplay:LeftStickMode::Block;
    }
    inline bool ControllerSelectionStick(bool split,bool left,bool right) {
        return split?right:left;
    }
    // After using the right stick in the UI, return to centre before handing it
    // to the camera. Do not suppress ordinary camera input in the legacy mode.
    class LookStickGate {
        bool untilCentered=false;
    public:
        void Filter(float& x,float& y,bool capture,bool protect) {
            const bool centered=x*x+y*y<=.04f;
            if(centered)untilCentered=false;
            else if(capture && protect)untilCentered=true;
            if(capture || untilCentered)x=y=0;
        }
        void Reset(bool protect=false){untilCentered=protect;}
    };
}
