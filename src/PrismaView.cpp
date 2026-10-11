// PrismaUI display layer for Favorite Wheel.
//
// All wheel logic (input hook, favorites, actions, time control, quick slots)
// stays in Wheel.cpp. This file only replaces the former ImGui renderer: it hooks
// the game swap-chain Present purely as a per-frame pump, turns Wheel::Snapshot()
// into JSON and forwards it to a PrismaUI view (PrismaUI/views/FavoriteWheel).
// Nothing here draws pixels itself and no input is captured by the view.
#include "Wheel.h"
#include "Settings.h"
#include "UIResources.h"
#include "ItemInfo.h"
#include "NavigationPresentation.h"
#include "QuickSlots.h"
#include "UILayout.h"
#include "PrismaUI_API.h"
#include <dxgi.h>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <string>

namespace Wheel {
    namespace {
        using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
        Present previousPresent = nullptr;
        IDXGISwapChain* targetSwapChain = nullptr;

        PRISMA_UI_API::IVPrismaUI1* prisma = nullptr;
        PrismaView prismaView = 0;
        std::atomic<bool> domReady{false};
        bool shown = false; // game-thread only

        std::mutex pendingMutex;
        std::string pendingJson;
        bool pendingOpen = false;
        std::atomic<bool> taskQueued{false};

        constexpr const char* categoryKeys[] = {"weapons", "armor", "potions", "food", "spells", "shouts", "powers", "other"};
        static_assert(std::size(categoryKeys) == categoryCount);

        // ---------------------------------------------------------------- JSON helpers
        std::string Esc(const std::string& s) {
            std::string out;
            out.reserve(s.size() + 8);
            for (unsigned char c : s) {
                switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char b[8];
                        std::snprintf(b, sizeof b, "\\u%04x", c);
                        out += b;
                    } else out += static_cast<char>(c);
                }
            }
            return out;
        }
        std::string Q(const std::string& s) { return "\"" + Esc(s) + "\""; }
        std::string Num(float v) {
            char b[40];
            std::snprintf(b, sizeof b, "%.4f", static_cast<double>(v));
            return b;
        }
        std::string Css(std::uint32_t c) {
            // Theme colors are packed like ImGui ImU32: 0xAABBGGRR.
            char b[64];
            std::snprintf(b, sizeof b, "rgba(%u,%u,%u,%.3f)", c & 255u, (c >> 8) & 255u, (c >> 16) & 255u,
                static_cast<double>((c >> 24) & 255u) / 255.0);
            return b;
        }

        // ---------------------------------------------------------------- item text (same rules as the old Draw.cpp)
        std::string State(const View& v, const Item& item) {
            auto tr = [&](const char* key) { return Tr(v.config, key); };
            switch (item.action) {
            case ActionKind::FaceLightCommand: return tr(item.equipped ? "lightOn" : "lightOff");
            case ActionKind::FaceLightFollowers: return std::to_string(item.count) + " " + tr("lightPeople");
            case ActionKind::FunctionBack: return tr("lightBack");
            case ActionKind::FaceLightMenu: return tr(item.usable ? "lightOpen" : "lightUnavailableShort");
            case ActionKind::SaveOutfit:
            case ActionKind::ImportOutfits: return "";
            default: break;
            }
            if (!item.usable) return tr(v.functions ? "outfitUnavailable" : "inventory");
            if (item.equipped)
                return (item.magic ? std::string() : "x" + std::to_string(item.count) + " \xC2\xB7 ") + tr("equipped");
            return item.magic ? std::string() : "x" + std::to_string(item.count);
        }
        std::string ActionHint(const View& v, const Item& item) {
            if (!item.detail.empty()) return item.detail;
            const char* key = "select";
            switch (item.action) {
            case ActionKind::Outfit:
                key = item.usable ? (v.gamepad ? (v.config.gamepadCategoryButtons == 1 ? "padUseBumpers" : "padUseTriggers") : "outfitSelect")
                                  : "outfitUnavailable";
                break;
            case ActionKind::SaveOutfit: key = "outfitSave"; break;
            case ActionKind::ImportOutfits: key = "outfitImport"; break;
            case ActionKind::FaceLightCommand: key = "lightUse"; break;
            case ActionKind::FaceLightFollowers:
            case ActionKind::FaceLightMenu: key = "lightOpen"; break;
            case ActionKind::FunctionBack: key = "lightBack"; break;
            default: key = item.usable ? (v.gamepad ? "padSelect" : "select") : "inventory"; break;
            }
            return Tr(v.config, key);
        }
        const char* IconName(const Item& item) {
            switch (item.icon) {
            case IconKind::Sword: return "sword";
            case IconKind::Dagger: return "dagger";
            case IconKind::Axe: return "axe";
            case IconKind::Mace: return "mace";
            case IconKind::Bow:
            case IconKind::Crossbow: return "bow";
            case IconKind::Staff: return "staff";
            case IconKind::Arrow: return "arrow";
            case IconKind::Armor:
            case IconKind::Robe:
            case IconKind::Gloves:
            case IconKind::Boots: return "armor";
            case IconKind::Helmet: return "helmet";
            case IconKind::Ring: return "ring";
            case IconKind::Amulet: return "amulet";
            case IconKind::Shield: return "shield";
            case IconKind::Potion: return "potion";
            case IconKind::Food: return "food";
            case IconKind::Magic: return "magic";
            case IconKind::Scroll: return "scroll";
            case IconKind::Torch: return "torch";
            case IconKind::LightPlayer:
            case IconKind::LightTarget:
            case IconKind::LightGroup: return "torch";
            case IconKind::Back: return "back";
            case IconKind::Other: return "other";
            case IconKind::Auto: break;
            }
            switch (item.category) {
            case Category::Weapons: return "sword";
            case Category::Armor: return "armor";
            case Category::Potions: return "potion";
            case Category::Food: return "food";
            case Category::Spells: return "magic";
            case Category::Shouts: return "shout";
            case Category::Powers: return "magic";
            default: return "other";
            }
        }

        // ---------------------------------------------------------------- snapshot -> JSON
        std::string BuildOpenJson(const View& v) {
            const auto& cfg = v.config;
            const auto& t = Style(cfg);
            auto tr = [&](const char* key) { return Tr(cfg, key); };
            const int selected = WheelSlot(v.x, v.y);
            const bool panel = v.outfitDialog != 0 || v.settingsOpen;

            std::string j;
            j.reserve(6144);
            j += "{\"open\":true";
            j += ",\"panel\":" + std::string(panel ? "true" : "false");
            j += ",\"x\":" + Num(v.x) + ",\"y\":" + Num(v.y) + ",\"sel\":" + std::to_string(selected);

            // Theme and layout settings
            j += ",\"theme\":{\"accent\":" + Q(Css(t.accent)) + ",\"text\":" + Q(Css(t.text)) + ",\"muted\":" + Q(Css(t.muted)) +
                 ",\"sector\":" + Q(Css(t.sector)) + ",\"empty\":" + Q(Css(t.empty)) + ",\"hover\":" + Q(Css(t.hover)) +
                 ",\"panel\":" + Q(Css(t.panel)) + ",\"border\":" + Q(Css(t.border)) + ",\"borderWidth\":" + Num(t.borderWidth) +
                 ",\"ornament\":" + Num(t.ornament) + ",\"labelScale\":" + Num(t.labelScale) + ",\"titleScale\":" + Num(t.titleScale) + "}";
            j += ",\"cfg\":{\"scale\":" + Num(cfg.scale) + ",\"wheelScale\":" + Num(cfg.wheelScale) + ",\"px\":" + std::to_string(cfg.positionX) +
                 ",\"py\":" + std::to_string(cfg.positionY) + ",\"overlay\":" + std::to_string(cfg.overlayOpacity) +
                 ",\"hints\":" + (cfg.showHints ? "true" : "false") + ",\"anim\":" + (cfg.animations ? "true" : "false") + "}";

            if (panel) {
                j += ",\"message\":" + Q("This screen is not available in the PrismaUI build. Press Esc to close.");
                j += "}";
                return j;
            }

            // Title and category ribbon
            const auto section = v.functionSection;
            const std::string typeTitle = v.functions
                ? tr(section == FaceLight::Section::Outfits ? "outfitTitle" : section == FaceLight::Section::Lighting ? "lightTitle" : "lightFollowers")
                : tr(v.visibleCategories.Count() ? categoryKeys[static_cast<int>(v.category)] : "title");
            j += ",\"title\":" + Q(typeTitle);

            const int current = v.functions ? (section == FaceLight::Section::Outfits ? 0 : 1) : v.visibleCategories.Index(v.category);
            const int ribbonCount = v.functions ? FaceLight::TypeCount(v.faceLightAvailable) : v.visibleCategories.Count();
            const auto ribbon = RibbonLabels(current, ribbonCount);
            j += ",\"ribbon\":[";
            for (int i = 0; i < ribbon.size; ++i) {
                const auto label = ribbon.labels[static_cast<std::size_t>(i)];
                const char* key = v.functions ? (label.index == 0 ? "outfitTitle" : "lightTitle")
                                              : categoryKeys[static_cast<int>(v.visibleCategories.At(label.index))];
                if (i) j += ",";
                j += "{\"o\":" + std::to_string(label.offset) + ",\"n\":" + Q(tr(key)) + "}";
            }
            j += "]";

            // Page rail
            const int total = PageCount(ItemCount(v));
            const int curPage = std::clamp(v.page, 0, total - 1);
            const auto pages = VisiblePages(total, curPage);
            j += ",\"pages\":{\"total\":" + std::to_string(total) + ",\"cur\":" + std::to_string(curPage) +
                 ",\"first\":" + std::to_string(pages.first) + ",\"size\":" + std::to_string(pages.size) + "}";
            j += ",\"count\":" + Q(std::to_string(ItemCount(v)) + " " +
                 tr(v.functions ? "functionCount" : v.inventoryWide ? "inventoryCount" : "count"));
            j += ",\"gamepad\":" + std::string(v.gamepad ? "true" : "false");
            j += ",\"padPair\":" + std::string(cfg.gamepadCategoryButtons == 1 ? "\"LT\"" : "\"LB\"");
            j += ",\"padPairR\":" + std::string(cfg.gamepadCategoryButtons == 1 ? "\"RT\"" : "\"RB\"");

            // Slots
            j += ",\"items\":[";
            for (int slot = 0; slot < slots; ++slot) {
                if (slot) j += ",";
                const int index = PageItemIndex(v, slot);
                if (index < 0 || index >= static_cast<int>(v.items.size())) {
                    j += "null";
                    continue;
                }
                const auto& item = v.items[static_cast<std::size_t>(index)];
                j += "{\"n\":" + Q(item.name) + ",\"st\":" + Q(State(v, item)) + ",\"eq\":" + (item.equipped ? "true" : "false") +
                     ",\"us\":" + (item.usable ? "true" : "false") + ",\"qs\":" +
                     std::to_string(ValidQuickSlot(item.quickSlot) ? item.quickSlot + 1 : 0) + ",\"ic\":" + Q(IconName(item)) + "}";
            }
            j += "]";

            // Centre
            const int index = PageItemIndex(v, selected);
            if (selected >= 0 && index >= 0 && index < static_cast<int>(v.items.size())) {
                const auto& item = v.items[static_cast<std::size_t>(index)];
                const auto state = State(v, item);
                std::string info = !item.detail.empty() ? item.detail : state.empty() ? ActionHint(v, item) : state;
                if (item.info.HasData()) {
                    const auto data = DescribeItem(item.info, [&](const char* key) { return tr(key); });
                    if (!data.stats.empty()) info = data.stats[0].label + " " + data.stats[0].value;
                    else if (!data.effects.empty()) info = data.effects[0].label;
                }
                j += ",\"centre\":{\"n\":" + Q(item.name) + ",\"info\":" + Q(info) + ",\"eq\":" + (item.equipped ? "true" : "false") +
                     ",\"us\":" + (item.usable ? "true" : "false") + ",\"ic\":" + Q(IconName(item)) + "}";
            } else {
                const char* key = v.inventoryLoading && !v.functions ? "inventoryLoading"
                                  : v.items.empty() ? (v.inventoryWide && !v.functions ? "inventoryEmpty" : "empty")
                                                    : "move";
                j += ",\"centre\":null,\"idle\":" + Q(tr(key));
            }

            // Hints
            if (cfg.showHints) {
                const bool quickHint = !v.functions && !v.gamepad;
                const std::string l1 = tr(v.gamepad ? (cfg.gamepadCategoryButtons == 1 ? "padUseBumpers" : "padUseTriggers")
                                                    : v.functions ? (section == FaceLight::Section::Outfits ? "functionUse" : "lightUse") : "compactUse");
                std::string l2;
                if (v.gamepad) {
                    l2 = tr(v.appliedGamepadMove ? (cfg.timeMode == 0 ? "padNavigationRight" : "padNavigationMoving") : "padNavigation");
                } else {
                    auto copy = cfg;
                    copy.hotkey = cfg.switchKey;
                    l2 = KeyLabel(copy) + " " + tr(v.functions && section == FaceLight::Section::Followers ? "lightNavigation" : "dualWheelHint");
                }
                j += ",\"hints\":[" + Q(l1) + "," + Q(l2);
                if (quickHint) j += "," + Q(tr(v.inventoryWide ? "allQuickSlotHint" : "quickSlotHint"));
                j += "]";
            }
            j += "}";
            return j;
        }

        // ---------------------------------------------------------------- game-thread delivery
        bool ViewUsable() { return prisma && domReady.load() && prisma->IsValid(prismaView); }

        void Deliver(const std::string& json, bool open) {
            if (!ViewUsable()) return;
            if (open) {
                if (!shown) {
                    prisma->Show(prismaView);
                    shown = true;
                }
                prisma->InteropCall(prismaView, "fwUpdate", json.c_str());
            } else {
                prisma->InteropCall(prismaView, "fwUpdate", json.c_str());
                prisma->Hide(prismaView);
                shown = false;
            }
        }

        void Queue(std::string json, bool open) {
            {
                std::lock_guard lock(pendingMutex);
                pendingJson = std::move(json);
                pendingOpen = open;
            }
            if (taskQueued.exchange(true)) return;
            auto tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                taskQueued = false;
                return;
            }
            tasks->AddTask([] {
                std::string json;
                bool open = false;
                {
                    std::lock_guard lock(pendingMutex);
                    json = std::move(pendingJson);
                    open = pendingOpen;
                }
                taskQueued = false;
                Deliver(json, open);
            });
        }

        // ---------------------------------------------------------------- render-thread pump
        void Pump(IDXGISwapChain* chain) {
            static bool wasOpen = false;
            static std::string last;
            static auto tick = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            const float elapsed = std::clamp(std::chrono::duration<float>(now - tick).count(), 0.f, .25f);
            tick = now;
            AdvanceGamepadPointer(elapsed);
            const auto current = Snapshot();
            if (!current.open) {
                if (wasOpen) {
                    wasOpen = false;
                    last.clear();
                    Queue("{\"open\":false}", false);
                }
                return;
            }
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(chain->GetDesc(&desc)) && desc.BufferDesc.Width && desc.BufferDesc.Height)
                SetViewport(static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height));
            wasOpen = true;
            auto json = BuildOpenJson(current);
            if (json == last) return;
            last = json;
            Queue(std::move(json), true);
        }

        HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* chain, UINT interval, UINT flags) {
            if (chain == targetSwapChain && !(flags & DXGI_PRESENT_TEST)) {
                try {
                    if (domReady.load()) Pump(chain);
                } catch (const std::exception& e) {
                    static bool reported = false;
                    if (!reported) {
                        reported = true;
                        SKSE::log::error("PrismaUI pump failed: {}", e.what());
                    }
                }
            }
            return previousPresent(chain, interval, flags);
        }

        void OnDomReady(PrismaView) {
            domReady = true;
            SKSE::log::info("PrismaUI view DOM ready");
        }
    }

    bool RendererReady() { return ViewUsable(); }

    bool InstallRenderer() {
        prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
        if (!prisma) {
            SKSE::log::error("PrismaUI API unavailable. Install and enable PrismaUI; the vanilla favorites menu is kept.");
            return false;
        }
        prismaView = prisma->CreateView("FavoriteWheel/index.html", &OnDomReady);
        if (!prisma->IsValid(prismaView)) {
            SKSE::log::error("PrismaUI view creation failed. Check Data/PrismaUI/views/FavoriteWheel/index.html.");
            return false;
        }
        prisma->Hide(prismaView);
        SKSE::log::info("PrismaUI view created: FavoriteWheel/index.html");

        auto renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (!renderer) return false;
        targetSwapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
        if (!targetSwapChain) {
            SKSE::log::error("No game swap chain at DataLoaded");
            return false;
        }
        auto vtable = *reinterpret_cast<std::uintptr_t**>(targetSwapChain);
        previousPresent = reinterpret_cast<Present>(vtable[8]);
        REL::safe_write(reinterpret_cast<std::uintptr_t>(&vtable[8]), reinterpret_cast<std::uintptr_t>(&PresentHook));
        SKSE::log::info("Swap-chain Present pump installed (PrismaUI display)");
        return true;
    }
}
