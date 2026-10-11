#pragma once
#include "WheelLogic.h"
#include <cmath>
namespace Wheel {
    struct Rect {
        float x,y,w,h;
        bool Contains(float px,float py) const {return px>=x && px<=x+w && py>=y && py<=y+h;}
    };
    inline constexpr Rect applyButton{100,235,185,42}, cancelButton{-95,235,175,42}, defaultsButton{-285,235,170,42};
    inline constexpr Rect outfitNameButton{-270,-180,540,48};
    inline constexpr Rect outfitOverwriteButton{-270,-95,260,44}, outfitDeleteButton{10,-95,260,44};
    inline constexpr Rect outfitExportButton{-270,-30,540,44};
    inline constexpr Rect generalTab{-300,-210,144,28}, controlsTab{-148,-210,144,28}, gamepadTab{4,-210,144,28}, gameplayTab{156,-210,144,28};
    inline constexpr int GeneralRows[]{0,1,2,3,4,6,7,8,9,10};
    inline constexpr int ControlRows[]{5,12,13,14,11};
    inline constexpr int GamepadRows[]{15,16,17,21,23};
    inline constexpr int GameplayRows[]{18,19,20,22,24};
    inline int SettingRow(int tab,int slot){return tab==2?GameplayRows[slot]:tab==3?GamepadRows[slot]:tab==1?ControlRows[slot]:GeneralRows[slot];}
    inline int SettingCount(int tab){return tab==2 || tab==3 || tab==1?5:10;}
    inline constexpr int settingsTabOrder[]{0,1,3,2};
    inline int NextSettingsTab(int tab,int direction){for(int i=0;i<4;++i)if(settingsTabOrder[i]==tab)return settingsTabOrder[Wrap(i+direction,4)];return 0;}
    inline bool BindingRow(int row){return row==5 || row==11 || row==13 || row==15;}
    inline Rect MinusButton(int row){return {40,-173.f+row*31,34,28};}
    inline Rect PlusButton(int row){return {252,-173.f+row*31,34,28};}
    inline Rect ValueButton(int row){return {80,-173.f+row*31,166,28};}
    inline int WheelSlot(float x,float y) {return x*x+y*y>(264.f/224)*(264.f/224)?-1:HitTest(x,y);}
}
