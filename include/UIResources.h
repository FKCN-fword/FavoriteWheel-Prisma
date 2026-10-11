#pragma once
#include "Settings.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace Wheel {
    struct Language { std::string id, name, font; std::unordered_map<std::string,std::string> text; };
    enum class ThemeStyle { Etched, Skyrim };
    enum class FrameStyle { Plain, Nordic };
    struct Theme {
        std::string id="classic", name="Classic", font;
        ThemeStyle style=ThemeStyle::Etched; // Component shapes, independent of colors and layout.
        FrameStyle frameStyle=FrameStyle::Plain;
        std::string surfaceTexture; // Resolved local PNG; empty means the solid-color fallback.
        float materialStrength=.85f, materialZoom=1.f;
        std::uint32_t accent=0xFF8BC9E7, text=0xFFEAEEED, muted=0xFFB5A89E;
        std::uint32_t sector=0xF02C2017, empty=0xBE1F160F, hover=0xEB3D5B6B;
        std::uint32_t panel=0xFC241C15, background=0xFF000000, border=0xFF70604B;
        // Optional visual tokens. Old themes inherit these; no game behavior lives here.
        float borderWidth=1.f, cornerRadius=10.f, ornament=.65f, relief=.5f;
        float textShadow=.32f, iconScale=1.f, titleScale=1.f, labelScale=1.f;
        float hoverDuration=.10f, pageDuration=.12f;
    };
    void LoadResources(const std::string& root="Data/SKSE/Plugins/FavoriteWheel");
    const std::vector<Language>& Languages();
    std::string ResolveLanguage(const std::string& requested,const std::string& systemLocale);
    std::string ActiveLanguage(const Settings& config);
    const std::string& SystemLanguage();
    std::string LanguageLabel(const Settings& config);
    const std::vector<Theme>& Themes();
    const Theme& Style(const Settings& config);
    std::string Tr(const Settings& config, const std::string& key);
    std::string UIGlyphs(const Settings& config);
    std::string FontPath(const Settings& config);
    const std::vector<std::string>& FallbackFontPaths(); // Installed Windows fonts, resolved at resource load.
    std::string KeyLabel(const Settings& config);
    std::string ModifierLabel(const Settings& config,int modifier);
    std::string PadLabel(const Settings& config,int key,bool follow=false);
    std::string CycleLanguage(const std::string& id,int delta);
    std::string CycleTheme(const std::string& id,int delta);
}
