#pragma once
#include "Favorites.h"
#include "Settings.h"
#include "NameEditor.h"
#include "FaceLightClient.h"
#include "CategoryNavigation.h"
#include <vector>
namespace Wheel {
    inline constexpr const char* menuName = "FavoriteWheelMenu";
    inline constexpr const char* pauseMenuName = "FavoriteWheelPauseMenu";
    struct View {
        bool functions=false;
        bool faceLightAvailable=false;
        FaceLight::Section functionSection=FaceLight::Section::Outfits;
        int outfitDialog=0; // 1 new name, 2 rename, 3 manage, 4 overwrite confirmation, 5 delete confirmation
        std::uint32_t outfitId=0;
        NameEditor outfitName;
        std::string inventoryGlyphs;
        bool open = false;
        bool animateClose = false;
        bool settingsOpen = false, capturingKey = false, gamepad=false, saveError = false;
        bool appliedGamepadMove=false;
        int settingsTab=0; // 0 appearance, 1 keyboard, 2 gameplay, 3 controller.
        int captureBinding=0; // favorites, actions, switch, controller favorites
        Settings config;
        Category category = Category::Weapons;
        VisibleCategories visibleCategories;
        int page = 0;
        float x = 0, y = 0;
        std::vector<Item> items;
        // Runtime views carry only the visible page. Legacy/synthetic previews may carry a full list.
        int itemOffset=0, totalItems=-1;
        bool inventoryWide=false,inventoryLoading=false;
    };
    inline std::size_t ItemCount(const View& v){return v.totalItems<0?v.items.size():static_cast<std::size_t>(v.totalItems);}
    inline int PageItemIndex(const View& v,int slot){return v.page*slots+slot-v.itemOffset;}
    View Snapshot();
    bool IsOpen();
    bool InstallWheel();
    void SetGameActive(bool active);
    void Cancel(bool effects=false);
    bool InstallRenderer();
    bool RendererReady();
    void DrawWheel(const View& view,float opacity=1.f,float expansion=1.f);
    void ResetVisualFeedback(); // Render thread only; new open/session must not reuse old hover state.
    void SetViewport(float width, float height);
    void AdvanceGamepadPointer(float elapsed);
}
