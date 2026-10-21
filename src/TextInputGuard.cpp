// Detects a focused/edited widget of the ImGui overlay used by SKSE Menu Framework (or a compatible framework).
// Those text fields do not tell the game that text is being typed, so the wheel hotkey would still open.
// The framework exports the Dear ImGui API; we call ImGui::IsAnyItemActive() through its export table.
#include "ExportFinder.h"
#include "Wheel.h"
#include <atomic>

namespace Wheel {
    namespace {
        using BoolFn = bool (*)();
        std::atomic<BoolFn> activeFn{nullptr};
        std::atomic<bool> searched{false};

        void Resolve() {
            static constexpr const char* modules[] = {"SKSEMenuFramework.dll", "ApocryphaMenuFramework.dll"};
            for (const auto* moduleName : modules) {
                const HMODULE module = GetModuleHandleA(moduleName);
                if (!module) continue;
                const auto image = reinterpret_cast<const std::uint8_t*>(module);
                const auto list = exports::List(image);
                if (const auto* hit = exports::FindBoolVoid(list, "IsAnyItemActive")) {
                    activeFn = reinterpret_cast<BoolFn>(const_cast<std::uint8_t*>(image) + hit->rva);
                    SKSE::log::info("Text input guard: using {} from {}", hit->name, moduleName);
                    return;
                }
                SKSE::log::warn("Text input guard: {} has no usable IsAnyItemActive export ({} exports). Related names:", moduleName, list.size());
                int shown = 0;
                for (const auto& item : list) {
                    if ((item.name.find("Active") != std::string::npos || item.name.find("WantText") != std::string::npos ||
                            item.name.find("Blocking") != std::string::npos) && shown++ < 20)
                        SKSE::log::warn("  {}", item.name);
                }
            }
        }
    }

    bool OverlayWidgetActive() {
        if (!searched.exchange(true)) Resolve();
        const auto fn = activeFn.load();
        return fn && fn();
    }
}
