#pragma once
#include <string>
namespace Wheel {
    struct Settings {
        bool enabled = true;
        bool allInventory = false;
        bool hideEmptyCategories = true;
        bool keepOpen = true; // Equipment/magic/actions stay open; consumables close.
        int timeMode = 0; // 0 pause, 1 slow, 2 unchanged game speed.
        int slowPercent = 50; // Relative slowdown, with an absolute 50% physics floor.
        std::string language = "auto"; // Windows display language, or an explicit language-file ID.
        std::string theme = "classic";
        bool showHints = true;
        float scale = 1.0f;
        float wheelScale = 1.0f;
        int positionX = 72, positionY = 46;
        int overlayOpacity = 35;
        bool sounds = true, animations = true;
        float sensitivity = 1.0f;
        int switchKey = 19; // R, wheel mode switch while open.
        int hotkey = -1; // -1 follows the game's Favorites binding.
        int hotkeyModifier = 0; // Shift=1, Ctrl=2, Alt=4, combinable.
        int actionHotkey = -1; // -1 follows the effective favorites-wheel key.
        int actionModifier = 1;
        int gamepadHotkey = -1; // -1 follows the game's controller Favorites binding.
        int gamepadModifier = -1;
        int gamepadActionModifier = 274; // LB, SKSE macro keycode.
        int gamepadCategoryButtons = 0; // 0 LB/RB categories, LT/RT use; 1 swaps the pairs.
        bool gamepadMoveWhileOpen = false; // Left stick moves in live modes; right stick selects.
        std::string font = "C:/Windows/Fonts/msyh.ttc";
        bool operator==(const Settings&) const = default;
    };
    Settings Config();
    bool AppliedGamepadMoveWhileOpen(); // Input scheme changes only after successful Apply.
    void LoadSettings();
    void BeginSettings();
    void EditSettings(Settings values);
    void RevertSettings();
    bool SaveSettings();
    void DefaultSettings();
    void SetSettingsPath(const std::string& path);
    std::string SettingsDiagnostic(); // Last load/save path, stage and Win32 error.
}
