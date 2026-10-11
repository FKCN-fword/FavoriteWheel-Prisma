#include "Wheel.h"
#include "RuntimeSupport.h"
#include "Outfits.h"
#include "Settings.h"
#include "InputGate.h"
#include "InputDispatchChain.h"
#include "OpenPolicy.h"
#include "InputBindings.h"
#include "QuickSlots.h"
#include "InventoryPages.h"
#include "TimeControl.h"
#include "TimePolicy.h"
#include "QueuedTask.h"
#include "ActorRuntime.h"
#include "ControllerSticks.h"
#include "ActionPolicy.h"
#include "UIResources.h"
#include "UILayout.h"
#include "WheelFonts.h"
#include "TextBridgeClient.h"
#include "NameHitMap.h"
#include <chrono>
#include <optional>
#include <Windows.h>

namespace Wheel {
    namespace {
        std::mutex viewMutex;
        // Native menu callbacks, input and player-update callbacks can overlap.
        // Serialize opening/closing with time synchronization. Recursive entry is
        // required because a time conflict cancels from inside SyncWheelTime.
        std::recursive_mutex lifecycleMutex;
        View view;
        InventoryPages inventoryPages;
        std::vector<Item> functionItems;
        bool inventoryLoaded=false,inventoryScope=false;
        std::atomic<std::uint64_t> directoryRevision{0};
        std::atomic<bool> directoryTaskQueued{false},detailTaskQueued{false};
        std::atomic<std::uint64_t> inventoryChanges{0};
        std::uint64_t inventoryStamp=0; // viewMutex held.
        std::chrono::steady_clock::time_point nextInventoryRefresh{};
        std::optional<ItemKey> hoverKey;
        std::chrono::steady_clock::time_point hoverStarted;
        std::uint64_t hoverSerial=0; // Input pump only; a reopened wheel starts a new dwell.
        int favoritePage=0,functionPage=0;
        FaceLight::Section functionCategory=FaceLight::Section::Outfits; // Remember the top-level type, not a child list.
        bool shiftHeld[2]{};
        bool keyboardHeld[256]{}, padHeld[16]{};
        float padX=0, padY=0;
        float nameRepeatAt[256]{};
        std::atomic<bool> gameActive{false};
        std::atomic<std::uint64_t> epoch{0};
        std::atomic<bool> closing{false};
        std::atomic<bool> openQueued{false};
        std::atomic<std::uint64_t> openSerial{0};
        struct PendingOpen {
            bool functions;
            RE::INPUT_DEVICE device;
            std::uint32_t code;
            std::uint64_t generation, serial;
            std::chrono::steady_clock::time_point deadline;
        };
        std::mutex openMutex;
        std::optional<PendingOpen> pendingOpen;
        REL::Relocation<void (*)(RE::PlayerCharacter*, float)> previousPlayerUpdate;
        bool wheelInstalled=false, playerUpdateInstalled=false;
        std::atomic<bool> updateLogged{false};
        std::atomic<float> viewportWidth{1280}, viewportHeight{900};
        InputGate inputGate;
        ControllerPageButtons controllerPages;
        ControllerPageButtons controllerTriggers{280};
        MovementStickGate movementStick;
        LookStickGate lookStick;
        std::mutex pauseMutex;
        bool pauseRequested=false;
        using Dispatch = void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent**);
        REL::Relocation<Dispatch> previousDispatch;
        std::atomic<std::uint64_t> dispatchCount{0};
        struct PendingAction {
            Item item;
            bool left;
            std::uint64_t generation;
            std::chrono::steady_clock::time_point deadline;
            bool retained=false;
            std::uint64_t serial=0;
        };
        std::mutex actionMutex;
        std::optional<PendingAction> pendingAction;
        std::atomic<bool> actionTaskQueued{false};
        std::uint64_t outfitGeneration=0; // SKSE task thread only.
        std::atomic<bool> retainedAction{false};
        std::atomic<std::uint64_t> playerUpdates{0};
        bool retainedSubmitted=false; // lifecycleMutex held.
        std::chrono::steady_clock::time_point retainedAt;
        std::uint64_t retainedUpdate=0;

        void CompleteRetainedAction(const PendingAction& action) {
            std::lock_guard lifecycle(lifecycleMutex);
            if(action.retained && retainedAction && action.serial==openSerial.load() && IsOpen()) {
                retainedSubmitted=true;retainedAt=std::chrono::steady_clock::now();retainedUpdate=playerUpdates.load();
            }
        }

        bool HasPendingAction() { std::lock_guard lock(actionMutex); return pendingAction.has_value(); }

        bool Focused() {
            DWORD process = 0;
            GetWindowThreadProcessId(GetForegroundWindow(), &process);
            return process == GetCurrentProcessId();
        }
        const char* BlockingMenu() {
            auto ui = RE::UI::GetSingleton();
            if (!ui) return "UI unavailable";
            return BlockingWheelMenu([&](const char* name){return ui->IsMenuOpen(name);});
        }
        bool BlockedMenu() { return BlockingMenu() != nullptr; }
        bool ValidPlayer() {
            auto player = RE::PlayerCharacter::GetSingleton();
            auto menus = RE::MenuControls::GetSingleton();
            return menus && PlayerEligible(gameActive, player != nullptr, player && player->Get3D(),
                ActorRuntime::IsDead(player), menus->InBeastForm());
        }
        bool EntryControlsEnabled(RE::INPUT_DEVICE device,std::uint32_t code) {
            auto controls=RE::ControlMap::GetSingleton();
            if(!controls)return false;
            if(device!=RE::INPUT_DEVICE::kKeyboard && device!=RE::INPUT_DEVICE::kMouse && device!=RE::INPUT_DEVICE::kGamepad)return false;
            const auto context=controls->controlMap[RE::UserEvents::INPUT_CONTEXT_ID::kGameplay];
            if(!context)return true; // A custom wheel key need not have a native mapping.
            const auto enabled=controls->GetRuntimeData().enabledControls.underlying();
            for(const auto& mapping:context->deviceMappings[static_cast<unsigned>(device)])
                if(mapping.inputKey==code && !EntryControlGroupEnabled(mapping.userEventGroupFlag.underlying(),enabled))return false;
            return true;
        }
        const char* OpenBlockReason() {
            auto ui = RE::UI::GetSingleton();
            auto controls = RE::ControlMap::GetSingleton();
            if (!Config().enabled) return "disabled";
            if (!RendererReady()) return "renderer not ready";
            if (closing) return "wheel still closing";
            if(Outfits::Busy())return "outfit changing";
            if (HasPendingAction()) return "item action pending";
            if (!gameActive) return "game not active";
            auto player = RE::PlayerCharacter::GetSingleton();
            if (!player) return "player unavailable";
            if (!player->Get3D()) return "player 3D unavailable";
            if (ActorRuntime::IsDead(player)) return "player dead";
            auto menus = RE::MenuControls::GetSingleton();
            if (!menus) return "menu controls unavailable";
            if (menus->InBeastForm()) return "beast form: yielding to vanilla";
            if (!Focused()) return "game not foreground";
            if (!ui) return "UI unavailable";
            if (const auto menu = BlockingMenu()) return menu;
            if (ui->GameIsPaused()) return "another menu pauses game";
            if (!controls) return "control map unavailable";
            if (!controls->IsMenuControlsEnabled()) return "menu controls disabled";
            if (controls->GetRuntimeData().textEntryCount > 0) return "text input active";
            return nullptr;
        }
        void RequestPause(bool pause) {
            std::lock_guard lock(pauseMutex);
            if(pauseRequested==pause)return;
            if(auto queue=RE::UIMessageQueue::GetSingleton()) {
                pauseRequested=pause;
                queue->AddMessage(pauseMenuName,pause?RE::UI_MESSAGE_TYPE::kShow:RE::UI_MESSAGE_TYPE::kHide,nullptr);
            }
        }
        void SyncWheelTime() {
            std::lock_guard lifecycle(lifecycleMutex);
            bool open,settings;int dialog;
            {std::lock_guard lock(viewMutex);open=view.open;settings=view.settingsOpen;dialog=view.outfitDialog;}
            if(!open){TimeControl::EndSession();RequestPause(false);return;}
            if(!RendererReady() || !Focused() || !ValidPlayer() || BlockedMenu()){Cancel();return;}
            const auto config=Config();
            const bool pause=PauseForWheel(config.timeMode,settings,dialog) && !retainedAction;
            if(!TimeControl::Update(!pause && config.timeMode==1,config.slowPercent)) {
                SKSE::log::warn("Wheel canceled: time state could not be safely maintained");Cancel();return;
            }
            RequestPause(pause);
        }
        void FailedTask(std::uint64_t generation,std::uint64_t serial,const char* task,const char* error,bool outfit=false) {
            SKSE::log::error("Wheel {} task failed: {}",task,error);
            std::lock_guard lifecycle(lifecycleMutex);
            if(generation!=epoch.load())return;
            if(outfit)Outfits::CancelJob();
            // A failure belonging to an earlier wheel cannot close a newer one.
            if(serial==openSerial.load()) {
                if(outfit){std::lock_guard lock(actionMutex);pendingAction.reset();}
                Cancel();
            }
        }
        void PublishPage(ItemPage page) { // viewMutex held
            view.items=std::move(page.items);view.itemOffset=page.offset;view.totalItems=page.total;
        }
        void FilterItems() { // viewMutex held; O(10), independent of category size.
            if(view.functions)PublishPage(SlicePage(functionItems,0,functionItems.size(),view.page));
            else {
                view.visibleCategories=CategoryVisibility(inventoryPages.visible,Config().hideEmptyCategories);
                const auto category=view.visibleCategories.Select(view.category);
                if(category!=view.category){view.category=category;view.page=favoritePage=0;}
                PublishPage(inventoryPages.Page(view.category,view.page));
            }
        }
        void PublishDirectory(std::vector<Item> items,bool scope,bool preserveSelection=false) { // viewMutex held
            const int oldSlot=WheelSlot(view.x,view.y),oldIndex=PageItemIndex(view,oldSlot);
            const auto oldKey=oldSlot>=0 && oldIndex>=0 && oldIndex<int(view.items.size())?
                std::optional<ItemKey>{view.items[oldIndex].key}:std::optional<ItemKey>{};
            ++directoryRevision; // Invalidates details for the replaced catalog.
            inventoryPages.Set(std::move(items));inventoryLoaded=true;inventoryScope=scope;view.inventoryLoading=false;
            view.inventoryWide=scope;
            if(!view.functions) {
                FilterItems();
                const int newIndex=PageItemIndex(view,oldSlot);
                const auto newKey=oldSlot>=0 && newIndex>=0 && newIndex<int(view.items.size())?
                    std::optional<ItemKey>{view.items[newIndex].key}:std::optional<ItemKey>{};
                if(preserveSelection && oldSlot>=0 && oldKey!=newKey)
                    view.x=view.y=0; // A changing live inventory must never silently replace the selected item.
            }
        }
        void RequestDirectory(bool refresh=false) {
            // Clear a previous wheel/page even when an older request is still queued.
            // The input pump retries after that stale task releases the queue flag.
            if(!refresh){std::lock_guard lock(viewMutex);view.inventoryLoading=true;if(!view.functions){view.visibleCategories={0};PublishPage({});}}
            const bool scope=Config().allInventory;
            if(directoryTaskQueued.exchange(true))return;
            const auto generation=epoch.load(),serial=openSerial.load(),revision=directoryRevision.load();
            const auto stamp=inventoryChanges.load();
            auto failed=[generation,serial](const char* error){FailedTask(generation,serial,"directory",error);};
            ScheduleQueuedTask(directoryTaskQueued,[&] {
                if(auto tasks=SKSE::GetTaskInterface())tasks->AddTask([generation,serial,revision,scope,stamp,refresh,failed] {
                    RunQueuedTask(directoryTaskQueued,[&] {
                        if(generation==epoch.load() && serial==openSerial.load() && revision==directoryRevision.load() &&
                            IsOpen() && Focused() && ValidPlayer() && !BlockedMenu()) {
                            auto items=CollectInventory(scope);
                            std::lock_guard lock(viewMutex);
                            if(generation==epoch.load() && serial==openSerial.load() && revision==directoryRevision.load() && view.open) {
                                PublishDirectory(std::move(items),scope,refresh);inventoryStamp=stamp;
                            }
                        }
                    },failed);
                });else directoryTaskQueued=false;
            },failed);
        }
        void PumpLiveInventory() {
            const auto now=std::chrono::steady_clock::now();
            bool refresh=false;
            {std::lock_guard lock(viewMutex);
                if(view.open && !view.functions && !view.settingsOpen && !view.outfitDialog && inventoryLoaded &&
                    (Config().timeMode!=0 || Config().keepOpen) && !retainedAction &&
                    inventoryStamp!=inventoryChanges.load() && now>=nextInventoryRefresh && !directoryTaskQueued) {
                    nextInventoryRefresh=now+std::chrono::milliseconds(250);refresh=true;
                }
            }
            if(refresh)RequestDirectory(true);
        }
        void RefreshFunctions() {
            const auto section=Snapshot().functionSection;
            auto entries=section==FaceLight::Section::Outfits?Outfits::Entries():FaceLight::Entries(section);
            std::lock_guard lock(viewMutex);
            functionItems=std::move(entries);FilterItems();
        }
        void SwitchWheel() {
            bool functions,needsDirectory=false;
            {std::lock_guard lock(viewMutex);
                if(view.functions){if(view.functionSection==FaceLight::Section::Outfits)functionPage=view.page;}else favoritePage=view.page;
                functionCategory=FaceLight::VisibleSection(functionCategory,view.faceLightAvailable);
                functions=view.functions=!view.functions;view.functionSection=functionCategory;
                view.page=functions?(functionCategory==FaceLight::Section::Outfits?functionPage:0):favoritePage;view.x=view.y=0;
                if(!functions) {
                    needsDirectory=!inventoryLoaded || inventoryScope!=Config().allInventory;
                    if(!needsDirectory)FilterItems();
                }
            }
            if(functions)RefreshFunctions();
            else if(needsDirectory)RequestDirectory();
        }
        void Open(bool functions) {
            std::lock_guard lifecycle(lifecycleMutex);
            FaceLight::Capture(); // After the previous player-update hook, before pause.
            const bool scope=Config().allInventory;
            const auto stamp=inventoryChanges.load();
            auto items=functions?std::vector<Item>{}:CollectInventory(scope);
            {
                std::lock_guard lock(viewMutex);
                view.faceLightAvailable=FaceLight::Available();
                functionCategory=FaceLight::VisibleSection(functionCategory,view.faceLightAvailable);
                ++directoryRevision;inventoryLoaded=false;inventoryPages.Set({});functionItems.clear();
                view.visibleCategories={0};
                inventoryStamp=stamp;nextInventoryRefresh=std::chrono::steady_clock::now();
                PublishPage({});
                view.inventoryGlyphs.clear();view.inventoryWide=scope;view.inventoryLoading=false;
                view.open = true; view.animateClose=false; view.functions=functions;view.functionSection=functionCategory;view.outfitDialog=0; view.settingsOpen = view.capturingKey = view.saveError = false;
                view.page = functions?(functionCategory==FaceLight::Section::Outfits?functionPage:0):favoritePage; view.x = view.y = 0;
                padX=padY=0;
                if(!functions)PublishDirectory(std::move(items),scope);
            }
            if(functions)RefreshFunctions();
            TimeControl::BeginSession();SyncWheelTime();
            if(!IsOpen())return;
            RE::UIMessageQueue::GetSingleton()->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kShow, nullptr);
            if(Config().sounds) RE::PlaySound("UIMenuOK");
            SKSE::log::info("Wheel opened: mode={} scope={} entries={} timeMode={} slowPercent={} padMove={}",functions?"functions":"inventory",scope?"all":"favorites",inventoryPages.items.size(),Config().timeMode,Config().slowPercent,Config().gamepadMoveWhileOpen);
        }
        void RequestOpen(bool functions,RE::INPUT_DEVICE device,std::uint32_t code) {
            std::lock_guard lifecycle(lifecycleMutex);
            std::lock_guard lock(openMutex);
            if(openQueued.exchange(true))return;
            pendingOpen=PendingOpen{functions,device,code,epoch.load(),++openSerial,
                std::chrono::steady_clock::now()+std::chrono::seconds(2)};
        }
        void PumpOpen() {
            std::lock_guard lifecycle(lifecycleMutex);
            if(!openQueued)return;
            std::optional<PendingOpen> request;
            {std::lock_guard lock(openMutex);request=std::move(pendingOpen);pendingOpen.reset();}
            if(!request || request->serial!=openSerial.load())return;
            RunQueuedTask(openQueued,[&] {
                const auto reason=!EntryControlsEnabled(request->device,request->code)?"entry control group disabled":OpenBlockReason();
                if(request->generation==epoch.load() && !IsOpen() && !reason &&
                    std::chrono::steady_clock::now()<request->deadline)Open(request->functions);
                else SKSE::log::info("Wheel opening canceled on player update: {}",reason?reason:"state changed or expired");
            },[&](const char* error){FailedTask(request->generation,request->serial,"open",error);});
        }
        void FunctionNavigate(FaceLight::Section section,bool preservePointer=false) {
            {std::lock_guard lock(viewMutex);
                section=FaceLight::VisibleSection(section,view.faceLightAvailable);
                if(view.functionSection==FaceLight::Section::Outfits)functionPage=view.page;
                functionCategory=section==FaceLight::Section::Outfits?section:FaceLight::Section::Lighting;
                view.functionSection=section;view.page=section==FaceLight::Section::Outfits?functionPage:0;
                if(!preservePointer)view.x=view.y=0;
            }
            RefreshFunctions();
        }
        void FunctionBack() {
            const auto section=Snapshot().functionSection;
            FunctionNavigate(section==FaceLight::Section::Followers?FaceLight::Section::Lighting:FaceLight::Section::Outfits);
        }
        void ChangeCategory(int direction) {
            const auto current=Snapshot();
            if(current.functions) {
                const auto section=FaceLight::ChangeType(current.functionSection,direction,current.faceLightAvailable);
                if(section!=current.functionSection)FunctionNavigate(section,true);
                return;
            }
            std::lock_guard lock(viewMutex);
            if(!inventoryLoaded || view.inventoryLoading)return;
            const auto category=CategoryVisibility(inventoryPages.visible,Config().hideEmptyCategories).Move(view.category,direction);
            if(category==view.category)return; // One type (or no items) preserves its page.
            view.category = category;
            view.page = 0; // Keep the pointer on the same sector when changing type.
            FilterItems();
        }
        void ChangePage(int direction,const char* source=nullptr) {
            std::lock_guard lock(viewMutex);
            const auto previous=view.page;
            const auto pages=PageCount(ItemCount(view));
            const auto next=Wrap(previous+direction,pages);
            if(source)SKSE::log::info("Controller page: key={} wheel={} category={} section={} items={} pages={} previous={} next={}",
                source,view.functions?"functions":"favorites",static_cast<int>(view.category),static_cast<int>(view.functionSection),
                ItemCount(view),pages,previous+1,next+1);
            if(next==previous)return;
            view.page = next;
            view.x = view.y = 0;
            FilterItems();
        }
        void OutfitDialog(int mode,std::uint32_t id,const std::string& name) {
            if(retainedAction)return;
            {std::lock_guard lock(viewMutex);view.outfitDialog=mode;view.outfitId=id;view.outfitName.Set(name);view.x=view.y=0;}
            SyncWheelTime();
        }
        void EndOutfitDialog() {
            if(Snapshot().outfitDialog==1 || Snapshot().outfitDialog==2)TextBridge::Reset();
            {std::lock_guard lock(viewMutex);view.outfitDialog=0;view.x=view.y=0;}
            SyncWheelTime();RefreshFunctions();
        }
        void Activate(bool left) {
            std::lock_guard lifecycle(lifecycleMutex);
            if(HasPendingAction() || retainedAction || Outfits::Busy())return;
            const auto current = Snapshot();
            const int slot = WheelSlot(current.x, current.y);
            const int index = PageItemIndex(current,slot);
            if(current.inventoryLoading && !current.functions)return;
            if (slot < 0 || index < 0 || index >= static_cast<int>(current.items.size())) {
                if(current.functions)SKSE::log::info("Function click ignored: neutral/empty slot={} x={:.3f} y={:.3f}",slot,current.x,current.y);
                return;
            }
            const auto selected = current.items[index];
            if(current.functions)SKSE::log::info("Function click: action={} preset={} usable={} equipped={} right={}",static_cast<int>(selected.action),selected.actionId,selected.usable,selected.equipped,left);
            if(selected.action==ActionKind::FunctionBack){FunctionBack();return;}
            if(selected.action==ActionKind::FaceLightMenu || selected.action==ActionKind::FaceLightFollowers) {
                if(selected.usable)FunctionNavigate(selected.action==ActionKind::FaceLightMenu?FaceLight::Section::Lighting:FaceLight::Section::Followers);
                else RE::SendHUDMessage::ShowHUDMessage(selected.detail.c_str());
                return;
            }
            if(selected.action==ActionKind::SaveOutfit){OutfitDialog(1,0,Tr(Config(),"outfitDefault"));return;}
            if(selected.action==ActionKind::ImportOutfits){Outfits::Import();RefreshFunctions();return;}
            if(selected.action==ActionKind::Outfit && left){OutfitDialog(3,selected.actionId,selected.name);return;}
            if (!selected.usable) {
                RE::SendHUDMessage::ShowHUDMessage((selected.action==ActionKind::FaceLightCommand?selected.detail:Tr(Config(),selected.action==ActionKind::Outfit?"outfitMissing":"unsupported")).c_str());
                return;
            }
            // Outfit changes close the overlay so the player can observe the full transition.
            const bool keep=selected.action!=ActionKind::Outfit && KeepWheelAfterUse(current.config.keepOpen,selected.action==ActionKind::Favorite &&
                (selected.category==Category::Potions || selected.category==Category::Food));
            {
                std::lock_guard lock(actionMutex);
                pendingAction = PendingAction{selected, left, epoch.load(), std::chrono::steady_clock::now() + std::chrono::seconds(2),keep,openSerial.load()};
            }
            if(keep) {
                retainedAction=true;retainedSubmitted=false;SyncWheelTime();
            }else Cancel(true);
        }
        void BindQuickSlot(int slot) {
            const auto current=Snapshot();
            const int radial=WheelSlot(current.x,current.y),index=PageItemIndex(current,radial);
            if(current.functions || current.settingsOpen || current.outfitDialog || radial<0 || index<0 ||
                index>=static_cast<int>(current.items.size()) || current.inventoryLoading || !ValidQuickSlot(slot))return;
            const auto selected=current.items[index];
            if(!selected.favorited){RE::SendHUDMessage::ShowHUDMessage(Tr(Config(),"quickSlotNeedsFavorite").c_str());return;}
            const auto generation=epoch.load(),serial=openSerial.load();
            if(auto tasks=SKSE::GetTaskInterface())tasks->AddTask([selected,slot,generation,serial] {
                const auto now=Snapshot();
                if(generation!=epoch.load() || serial!=openSerial.load() || !now.open || now.functions ||
                    now.settingsOpen || now.outfitDialog || !Focused() || !ValidPlayer() || BlockedMenu())return;
                int next=-1;
                if(!BindFavoriteQuickSlot(selected,slot,&next))return;
                std::lock_guard lock(viewMutex);
                if(generation!=epoch.load() || serial!=openSerial.load() || !view.open || view.functions)return;
                // Preserve expensive item-info snapshots and cursor/category state.
                for(auto& item:inventoryPages.items) {
                    item.quickSlot=ReassignedQuickSlot(item.quickSlot,item.key==selected.key,slot,next);
                }
                FilterItems();
            });
        }
        void PumpDetails() {
            const auto serial=openSerial.load();
            if(hoverSerial!=serial){hoverSerial=serial;hoverKey.reset();}
            const auto current=Snapshot();
            const int radial=WheelSlot(current.x,current.y),index=PageItemIndex(current,radial);
            if(!current.open || current.functions || current.settingsOpen || current.outfitDialog || current.inventoryLoading ||
                radial<0 || index<0 || index>=static_cast<int>(current.items.size())){hoverKey.reset();return;}
            const auto& item=current.items[index];
            const auto now=std::chrono::steady_clock::now();
            const auto tick=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
            if(!hoverKey || *hoverKey!=item.key){hoverKey=item.key;hoverStarted=now;return;}
            if(item.infoReady && (current.config.timeMode==0 || tick-item.infoReadAt<500))return;
            if(now-hoverStarted<std::chrono::milliseconds(80) || detailTaskQueued.exchange(true))return;
            const auto generation=epoch.load(),revision=directoryRevision.load();
            std::size_t catalogIndex;
            {std::lock_guard lock(viewMutex);catalogIndex=inventoryPages.Index(current.category,current.page,radial);}
            auto failed=[generation,serial](const char* error){FailedTask(generation,serial,"detail",error);};
            ScheduleQueuedTask(detailTaskQueued,[&] {
                if(auto tasks=SKSE::GetTaskInterface())tasks->AddTask([selected=item,catalogIndex,generation,serial,revision,failed] {
                    RunQueuedTask(detailTaskQueued,[&] {
                        auto stillSelected=[&] {
                            const auto v=Snapshot();const int slot=WheelSlot(v.x,v.y),i=PageItemIndex(v,slot);
                            return generation==epoch.load() && serial==openSerial.load() && revision==directoryRevision.load() &&
                                v.open && !v.functions && !v.settingsOpen && !v.outfitDialog && slot>=0 &&
                                i>=0 && i<static_cast<int>(v.items.size()) && v.items[i].key==selected.key;
                        };
                        if(stillSelected() && Focused() && ValidPlayer() && !BlockedMenu()) {
                            const auto started=std::chrono::steady_clock::now();
                            ItemInfo info;const bool valid=ReadItemInfo(selected,info);
                            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                            if(elapsed>8)SKSE::log::info("Inventory detail: form={:08X} total_ms={:.2f}",selected.key.form,elapsed);
                            std::lock_guard lock(viewMutex);
                            if(generation==epoch.load() && serial==openSerial.load() && revision==directoryRevision.load() &&
                                view.open && catalogIndex<inventoryPages.items.size() && inventoryPages.items[catalogIndex].key==selected.key) {
                                auto& cached=inventoryPages.items[catalogIndex];cached.info=std::move(info);cached.infoReady=true;
                                cached.infoReadAt=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count());
                                cached.detail=valid?std::string{}:Tr(Config(),"changed");
                                if(!view.functions)for(auto& visible:view.items)if(visible.key==cached.key)visible=cached;
                            }
                        }
                    },failed);
                });else detailTaskQueued=false;
            },failed);
        }
        std::optional<PendingAction> TakeAction(ActionExecutor executor) {
            std::lock_guard lifecycle(lifecycleMutex);
            std::lock_guard lock(actionMutex);
            if(!pendingAction || ExecutorFor(pendingAction->item.action==ActionKind::FaceLightCommand)!=executor)return {};
            auto ui=RE::UI::GetSingleton();
            const auto decision=DecideRetainedActionOn(executor,
                ExecutorFor(pendingAction->item.action==ActionKind::FaceLightCommand),
                pendingAction->generation,epoch.load(),
                ui && ValidPlayer() && Focused() && !BlockedMenu(),closing || (!pendingAction->retained && TimeControl::Active()),
                ui && (ui->IsMenuOpen(menuName) || ui->IsMenuOpen(pauseMenuName)),ui && ui->GameIsPaused(),
                std::chrono::steady_clock::now()>=pendingAction->deadline,
                pendingAction->retained,pendingAction->serial==openSerial.load(),IsOpen());
            if(decision==ActionDecision::Wait)return {};
            auto submit=decision==ActionDecision::Submit?std::move(pendingAction):std::optional<PendingAction>{};
            if(decision==ActionDecision::Discard) {
                SKSE::log::info("Pending action canceled: state changed or close timed out");
                retainedAction=false;retainedSubmitted=false;
            }
            pendingAction.reset();return submit;
        }
        void PumpAction() {
            bool taskAction;
            {std::lock_guard lock(actionMutex);taskAction=pendingAction &&
                ExecutorFor(pendingAction->item.action==ActionKind::FaceLightCommand)==ActionExecutor::TaskQueue;}
            if ((!taskAction && !Outfits::Busy()) || actionTaskQueued.exchange(true)) return;
            const auto generation=epoch.load(),serial=openSerial.load();
            auto failed=[generation,serial](const char* error){FailedTask(generation,serial,"action",error,true);};
            // Favorites and outfit jobs retain their existing SKSE task path.
            ScheduleQueuedTask(actionTaskQueued,[&] {
                if(auto tasks=SKSE::GetTaskInterface())tasks->AddTask([generation,failed] {
                    RunQueuedTask(actionTaskQueued,[&] {
                        std::lock_guard lifecycle(lifecycleMutex);
                        if(generation!=epoch.load())return;
                        auto submit=TakeAction(ActionExecutor::TaskQueue);
                        if (submit) {
                            if(submit->item.action==ActionKind::Outfit){outfitGeneration=submit->generation;Outfits::Apply(submit->item.actionId);}
                            else UseFavorite(submit->item, submit->left);
                            CompleteRetainedAction(*submit);
                        }
                        if(!submit && Outfits::Busy()) {
                            auto ui=RE::UI::GetSingleton();
                            Outfits::Tick(outfitGeneration==epoch.load() && ValidPlayer() && Focused() && !BlockedMenu() && ui && !ui->GameIsPaused());
                        }
                    },failed);
                });else actionTaskQueued=false;
            },failed);
        }
        void PumpRetainedAction() { // Only player-update may refresh the Face Lighting API.
            std::lock_guard lifecycle(lifecycleMutex);
            if(!retainedAction || !retainedSubmitted || !IsOpen())return;
            const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-retainedAt).count();
            if(!RetainedActionSettled(HasPendingAction(),Outfits::Busy(),elapsed,playerUpdates.load()-retainedUpdate))return;
            retainedAction=false;retainedSubmitted=false;
            FaceLight::Capture();
            const auto current=Snapshot();
            if(current.functions)RefreshFunctions();
            // Coalesce one post-action inventory refresh, also in pause mode.
            // PublishDirectory retains aim unless the hovered instance vanished.
            bool loaded;{std::lock_guard lock(viewMutex);loaded=inventoryLoaded;}
            if(loaded)RequestDirectory(true);
        }
        void PlayerUpdate(RE::PlayerCharacter* player,float delta) {
            // Face Lighting establishes its accepted thread in the previous hook.
            // Do not call its API from input, Present or an SKSE task callback.
            previousPlayerUpdate(player,delta);
            std::lock_guard lifecycle(lifecycleMutex);
            ++playerUpdates;
            if(!updateLogged.exchange(true))SKSE::log::info("Wheel player-update callback active: thread={}",GetCurrentThreadId());
            if(auto submit=TakeAction(ActionExecutor::PlayerUpdate)) {
                FaceLight::Execute(submit->item);CompleteRetainedAction(*submit);
            }
            PumpRetainedAction();
            if(IsOpen())SyncWheelTime();
            PumpOpen();
        }
        void InstallPlayerUpdate() {
            if(playerUpdateInstalled || !wheelInstalled)return;
            // NewGame/PostLoadGame is after every plugin's DataLoaded callback;
            // chain outside Face Lighting regardless of DLL load order.
            REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_PlayerCharacter[0]};
            previousPlayerUpdate=vtable.write_vfunc(RuntimeSupport::playerUpdateSlot,PlayerUpdate);
            playerUpdateInstalled=true;
            SKSE::log::info("Wheel player-update hook installed after DataLoaded");
        }
        std::uint32_t FavoritesScanCode(RE::INPUT_DEVICE device=RE::INPUT_DEVICE::kKeyboard) {
            auto controls = RE::ControlMap::GetSingleton();
            return controls ? controls->GetMappedKey("Favorites", device, RE::UserEvents::INPUT_CONTEXT_ID::kGameplay) : 0xFFFFFFFF;
        }
        int HeldModifiers() {
            return ((keyboardHeld[42]||keyboardHeld[54])?1:0) |
                ((keyboardHeld[29]||keyboardHeld[157])?2:0) | ((keyboardHeld[56]||keyboardHeld[184])?4:0);
        }
        int EffectiveKeyboardKey(const Settings& config) {
            return ResolveFavoritesKey(config.hotkey,FavoritesScanCode());
        }
        int EffectiveGamepadKey(const Settings& config) {
            if(config.gamepadHotkey>=0)return config.gamepadHotkey;
            const auto mapped=FavoritesScanCode(RE::INPUT_DEVICE::kGamepad);
            if(mapped==0xFF || mapped==0xFFFFFFFF)return -1;
            const auto key=SKSE::InputMap::GamepadMaskToKeycode(mapped);
            return key>=266 && key<=281?static_cast<int>(key):-1;
        }
        bool ReplacedEntrance(const RE::ButtonEvent* button) {
            const auto config=Config();
            if(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard)
                return button->GetIDCode()==FavoritesScanCode() && static_cast<int>(button->GetIDCode())==EffectiveKeyboardKey(config);
            if(button->GetDevice()==RE::INPUT_DEVICE::kGamepad)
                return button->GetIDCode()==FavoritesScanCode(RE::INPUT_DEVICE::kGamepad) &&
                    static_cast<int>(SKSE::InputMap::GamepadMaskToKeycode(button->GetIDCode()))==EffectiveGamepadKey(config);
            return false;
        }
        Opening ToggleKey(const RE::ButtonEvent* button) {
            const auto config=Config();
            if(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard) {
                const int favorite=EffectiveKeyboardKey(config);
                return KeyboardOpening(button->GetIDCode(),HeldModifiers(),favorite,config.hotkeyModifier,
                    config.actionHotkey<0?favorite:config.actionHotkey,config.actionModifier);
            }
            if(button->GetDevice()==RE::INPUT_DEVICE::kGamepad) {
                const int favorite=EffectiveGamepadKey(config);
                auto held=[&](int key){return key<0 || (key!=favorite && key>=266 && key<=281 && padHeld[key-266]);};
                return GamepadOpening(SKSE::InputMap::GamepadMaskToKeycode(button->GetIDCode()),favorite,
                    held(config.gamepadModifier),held(config.gamepadActionModifier));
            }
            return Opening::None;
        }

        void EnterSettings() {
            if(retainedAction)return;
            BeginSettings();
            {std::lock_guard lock(viewMutex);
                view.settingsOpen=true; view.capturingKey=false; view.settingsTab=0; view.saveError=false;
                view.x=view.y=0;padX=padY=0;}
            SyncWheelTime();
        }
        void LeaveSettings(bool save) {
            if (save && !SaveSettings()) {
                SKSE::log::error("Settings save failed: {}",SettingsDiagnostic());
                std::lock_guard lock(viewMutex);view.saveError=true;return;
            }
            if (save) {
                const auto config=Config();
                SKSE::log::info("Settings saved: {}; language={} theme={} scale={} hotkey={} keepOpen={} padCategory={} padMove={}",
                    SettingsDiagnostic(),config.language,config.theme,config.scale,config.hotkey,config.keepOpen,config.gamepadCategoryButtons,config.gamepadMoveWhileOpen);
            }
            if (!save) RevertSettings();
            bool reload=false;
            {std::lock_guard lock(viewMutex);
                view.settingsOpen=view.capturingKey=view.saveError=false;view.x=view.y=0;
                padX=padY=0;
                if(save && Config().allInventory!=inventoryScope) {
                    inventoryLoaded=false;++directoryRevision;reload=!view.functions;
                }
                if(!reload && inventoryLoaded && !view.functions)FilterItems();
            }
            if(reload)RequestDirectory();
            SyncWheelTime();
        }
        void AdjustSetting(int row,int direction) {
            auto config=Config();
            switch(row) {
            case 0: config.wheelScale+=direction*.05f; break;
            case 1: config.sensitivity+=direction*.1f; break;
            case 2: config.showHints=!config.showHints; break;
            case 3: config.language=CycleLanguage(config.language,direction); break;
            case 4: config.theme=CycleTheme(config.theme,direction); break;
            case 5: config.hotkey=-1; break;
            case 6: config.positionX+=direction*2; break;
            case 7: config.positionY+=direction*2; break;
            case 8: config.overlayOpacity+=direction*5; break;
            case 9: config.sounds=!config.sounds; break;
            case 10: config.animations=!config.animations; break;
            case 11: config.switchKey=19; break;
            case 12: config.hotkeyModifier=Wrap(config.hotkeyModifier+direction,8); break;
            case 13: config.actionHotkey=-1;config.actionModifier=1; break;
            case 14: config.actionModifier=Wrap(config.actionModifier+direction,8); break;
            case 15: config.gamepadHotkey=-1; break;
            case 16: config.gamepadModifier=Wrap((config.gamepadModifier<0?0:config.gamepadModifier-265)+direction,17);config.gamepadModifier=config.gamepadModifier?config.gamepadModifier+265:-1; break;
            case 17: config.gamepadActionModifier=Wrap((config.gamepadActionModifier<0?0:config.gamepadActionModifier-265)+direction,17);config.gamepadActionModifier=config.gamepadActionModifier?config.gamepadActionModifier+265:-1; break;
            case 18: config.allInventory=!config.allInventory; break;
            case 19: config.timeMode=Wrap(config.timeMode+direction,3); break;
            case 20: config.slowPercent+=direction*5; break;
            case 21: config.gamepadCategoryButtons=1-config.gamepadCategoryButtons; break;
            case 22: config.keepOpen=!config.keepOpen; break;
            case 23: config.gamepadMoveWhileOpen=!config.gamepadMoveWhileOpen; break;
            case 24: config.hideEmptyCategories=!config.hideEmptyCategories; break;
            }
            EditSettings(config);
            std::lock_guard lock(viewMutex); view.saveError=false;
        }
        void SettingsClick(bool right) {
            const auto current=Snapshot();
            const float x=current.x*224, y=current.y*224;
            if (current.capturingKey) return;
            if(generalTab.Contains(x,y) || controlsTab.Contains(x,y) || gameplayTab.Contains(x,y) || gamepadTab.Contains(x,y)) {
                std::lock_guard lock(viewMutex);view.settingsTab=gameplayTab.Contains(x,y)?2:gamepadTab.Contains(x,y)?3:controlsTab.Contains(x,y)?1:0;return;
            }
            if (applyButton.Contains(x,y) && !right) { LeaveSettings(true); return; }
            if (cancelButton.Contains(x,y) && !right) { LeaveSettings(false); return; }
            if (defaultsButton.Contains(x,y) && !right) { DefaultSettings(); return; }
            for (int slot=0;slot<SettingCount(current.settingsTab);++slot) {
                const int row=SettingRow(current.settingsTab,slot);
                if(!BindingRow(row) && MinusButton(slot).Contains(x,y)) { AdjustSetting(row,-1); return; }
                if(!BindingRow(row) && PlusButton(slot).Contains(x,y)) { AdjustSetting(row,1); return; }
                if(ValueButton(slot).Contains(x,y)) {
                    if(BindingRow(row) && !right) { std::lock_guard lock(viewMutex); view.capturingKey=true;view.captureBinding=row; }
                    else AdjustSetting(row,right?-1:1);
                    return;
                }
            }
        }

        void OutfitAccept() {
            const auto current=Snapshot();bool ok=false;
            switch(current.outfitDialog) {
            case 1:ok=Outfits::Capture(current.outfitName.text);break;
            case 2:ok=Outfits::Rename(current.outfitId,current.outfitName.text);break;
            case 4:ok=Outfits::Capture(current.outfitName.text,current.outfitId);break;
            case 5:ok=Outfits::Remove(current.outfitId);break;
            default:EndOutfitDialog();return;
            }
            if(ok)EndOutfitDialog();
        }
        void OutfitClick() {
            const auto current=Snapshot();const float x=current.x*224,y=current.y*224;
            if(current.outfitDialog!=3 && cancelButton.Contains(x,y)){EndOutfitDialog();return;}
            if(applyButton.Contains(x,y)){OutfitAccept();return;}
            if((current.outfitDialog==1 || current.outfitDialog==2) && outfitNameButton.Contains(x,y)) {
                const auto at=HitName(current.outfitName.text,x);
                std::lock_guard lock(viewMutex);view.outfitName.Place(at,(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);return;
            }
            if(current.outfitDialog!=3)return;
            if(outfitNameButton.Contains(x,y))OutfitDialog(2,current.outfitId,current.outfitName.text);
            else if(outfitOverwriteButton.Contains(x,y))OutfitDialog(4,current.outfitId,current.outfitName.text);
            else if(outfitDeleteButton.Contains(x,y))OutfitDialog(5,current.outfitId,current.outfitName.text);
            else if(outfitExportButton.Contains(x,y))Outfits::Export(current.outfitId);
        }
        void NameKey(std::uint32_t code,bool fallback) {
            std::lock_guard lock(viewMutex);
            auto& editor=view.outfitName;
            const bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
            const bool shift=shiftHeld[0]||shiftHeld[1] || (GetAsyncKeyState(VK_SHIFT)&0x8000);
            if(code==14){editor.Backspace();return;}
            if(code==211){editor.Delete();return;}
            if(code==203 || code==205){editor.Move(code==203?-1:1,shift);return;}
            if(code==199 || code==207){editor.Place(code==199?0:editor.text.size(),shift);return;}
            if(ctrl && code==30){editor.SelectAll();return;}
            if(ctrl && (code==46 || code==45)) {
                const auto selection=editor.Selection();if(selection.empty())return;
                const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,selection.data(),static_cast<int>(selection.size()),nullptr,0);
                if(count<=0)return;
                auto memory=GlobalAlloc(GMEM_MOVEABLE,(count+1)*sizeof(wchar_t));if(!memory)return;
                auto chars=static_cast<wchar_t*>(GlobalLock(memory));if(!chars){GlobalFree(memory);return;}
                MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,selection.data(),static_cast<int>(selection.size()),chars,count);chars[count]=0;GlobalUnlock(memory);
                bool copied=false;
                if(OpenClipboard(GetForegroundWindow())){if(EmptyClipboard())copied=SetClipboardData(CF_UNICODETEXT,memory)!=nullptr;CloseClipboard();}
                if(!copied)GlobalFree(memory);
                else if(code==45)editor.EraseSelection();
                return;
            }
            if(ctrl && code==47) {
                if(OpenClipboard(nullptr)) {
                    if(auto handle=GetClipboardData(CF_UNICODETEXT))if(auto chars=static_cast<const wchar_t*>(GlobalLock(handle))) {
                        const auto capacity=GlobalSize(handle)/sizeof(wchar_t);
                        std::size_t length=0;while(length<std::min<std::size_t>(capacity,128) && chars[length])++length;
                        editor.InsertUtf16({reinterpret_cast<const char16_t*>(chars),length});
                        GlobalUnlock(handle);
                    }
                    CloseClipboard();
                }
            } else if(fallback && !ctrl && !(GetAsyncKeyState(VK_MENU)&0x8000)) {
                BYTE keys[256]{};GetKeyboardState(keys);
                keys[VK_SHIFT]=(shiftHeld[0]||shiftHeld[1])?0x80:0;
                wchar_t chars[8]{};auto layout=GetKeyboardLayout(0);
                const auto vk=MapVirtualKeyExW(code,MAPVK_VSC_TO_VK_EX,layout);
                const int n=ToUnicodeEx(vk,code,keys,chars,8,4,layout);
                if(n>0)editor.InsertUtf16({reinterpret_cast<const char16_t*>(chars),static_cast<std::size_t>(n)});
            }
        }
        void InputHook(RE::BSTEventSource<RE::InputEvent*>* source, RE::InputEvent** events) {
            if (dispatchCount.fetch_add(1) == 0) SKSE::log::info("Input dispatch active (first callback)");
            auto ui=RE::UI::GetSingleton();
            bool captureBatch = IsOpen() || closing || openQueued ||
                (ui && (ui->IsMenuOpen(menuName) || ui->IsMenuOpen(pauseMenuName)));
            movementStick.Capture(captureBatch);
            if (IsOpen() && (!Focused() || !ValidPlayer() || BlockedMenu())) Cancel();
            const auto bridge=TextBridge::Query();
            bool characterBatch=false;
            // Resolve chords from the whole physical batch before mutating events.
            // The main button may precede its modifier in the same input poll.
            if(auto manager=RE::BSInputDeviceManager::GetSingleton()) {
                if(auto keyboard=manager->GetKeyboard())
                    for(unsigned i=0;i<256;++i)keyboardHeld[i]=(keyboard->GetRuntimeData().curState[i]&0x80)!=0;
                std::fill_n(padHeld,16,false);
                if(auto pad=manager->GetGamepad();pad && pad->IsEnabled()) {
                    const auto& buttons=static_cast<RE::BSInputDevice*>(pad)->GetRuntimeData().deviceButtons;
                    for(int i=0;i<16;++i) {
                        const auto at=buttons.find(SKSE::InputMap::GamepadKeycodeToMask(i+266));
                        padHeld[i]=at!=buttons.end() && at->second && at->second->heldDownSecs>0;
                    }
                } else {movementStick.Reset();lookStick.Reset(AppliedGamepadMoveWhileOpen());controllerPages.Reset();controllerTriggers.Reset();std::lock_guard lock(viewMutex);padX=padY=0;}
            }
            for(auto event=events?*events:nullptr;event;event=event->next) {
                if(auto c=event->AsCharEvent();c && c->keyCode>=32)characterBatch=true;
                if(auto b=event->AsButtonEvent()) {
                    if(b->GetDevice()==RE::INPUT_DEVICE::kKeyboard && b->GetIDCode()<256)keyboardHeld[b->GetIDCode()]=b->IsPressed();
                    if(b->GetDevice()==RE::INPUT_DEVICE::kGamepad) {
                        const auto key=SKSE::InputMap::GamepadMaskToKeycode(b->GetIDCode());
                        if(key>=266 && key<=281)padHeld[key-266]=b->IsPressed();
                    }
                }
                if(auto stick=event->AsThumbstickEvent();stick && IsOpen() &&
                    ControllerSelectionStick(AppliedGamepadMoveWhileOpen(),stick->IsLeft(),stick->IsRight())) {
                    std::lock_guard lock(viewMutex);padX=stick->xValue;padY=stick->yValue;
                    if(std::hypot(padX,padY)>.2f) {
                        view.gamepad=true;
                        if(!view.settingsOpen && !view.outfitDialog)AimStick(view.x,view.y,padX,padY);
                    }
                }
            }
            shiftHeld[0]=keyboardHeld[42];shiftHeld[1]=keyboardHeld[54];
            InputDispatchChain<RE::InputEvent> dispatch(events?*events:nullptr);
            for (auto event = events ? *events : nullptr; event; event = event->next) {
                bool suppress=false;
                if (auto button = event->AsButtonEvent()) {
                    const auto code = button->GetIDCode();
                    const auto identity = (static_cast<std::uint64_t>(button->GetDevice()) << 32) | code;
                    const bool pressed = button->IsPressed();
                    const bool gamepad=button->GetDevice()==RE::INPUT_DEVICE::kGamepad;
                    const auto pad=gamepad?SKSE::InputMap::GamepadMaskToKeycode(code):0u;
                    const bool pageDown=gamepad && controllerPages.Observe(pad,pressed,button->IsDown());
                    const bool triggerDown=gamepad && controllerTriggers.Observe(pad,pressed,button->IsDown());
                    // Some controller event sources omit the native zero-duration
                    // down or emit release with zero duration. Do not retain the
                    // UI's swallowed state after an observed physical release.
                    const bool up = gamepad?!pressed:button->IsUp();
                    const bool down = gamepad && IsOpen() && (pad==266 || pad==267 || pad==280 || pad==281)?
                        (pad>=280?triggerDown:pageDown):button->IsDown();
                    bool nameRepeat=false;
                    if(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && code<256) {
                        if(up)nameRepeatAt[code]=0;
                        else if(down)nameRepeatAt[code]=.4f;
                        else if(pressed && nameRepeatAt[code]>0 && button->GetRuntimeData().heldDownSecs>=nameRepeatAt[code]) {
                            nameRepeat=true;nameRepeatAt[code]=button->GetRuntimeData().heldDownSecs+.05f;
                        }
                    }
                    const auto opening=ToggleKey(button);
                    const bool toggle = opening!=Opening::None;
                    // Native contextual actions can disable a physical entrance
                    // through custom control groups without disabling all menus.
                    // Yield its whole cycle unchanged while idle; open/closing
                    // wheels still own captured buttons until physical release.
                    const bool yieldEntry=!captureBatch && !inputGate.Swallowed(identity) &&
                        (toggle || ReplacedEntrance(button)) && !EntryControlsEnabled(button->GetDevice(),code);
                    if(down && (button->GetDevice()==RE::INPUT_DEVICE::kGamepad || button->GetDevice()==RE::INPUT_DEVICE::kKeyboard || button->GetDevice()==RE::INPUT_DEVICE::kMouse)) {
                        std::lock_guard lock(viewMutex);view.gamepad=pad!=0;
                    }
                    if (down && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard && (code == 16 || toggle)) {
                        const auto reason = yieldEntry?"entry control group disabled":OpenBlockReason();
                        const auto player = RE::PlayerCharacter::GetSingleton();
                        const auto race = player ? player->GetRace() : nullptr;
                        SKSE::log::info("Favorites key: scan={} mapped={} event='{}' match={} open={} blocked='{}' race={:08X} racePlayable={}",
                            code, FavoritesScanCode(), button->GetUserEvent().c_str(), toggle, IsOpen(), reason ? reason : "none",
                            race ? race->GetFormID() : 0, race && race->GetPlayable());
                    }
                    if(down && pad && (toggle || pad==266 || static_cast<int>(pad)==EffectiveGamepadKey(Config()))) {
                        const auto reason=yieldEntry?"entry control group disabled":OpenBlockReason();
                        SKSE::log::info("Controller entrance: raw={} key={} mapped={} effective={} mode={} open={} blocked='{}'",
                            code,pad,FavoritesScanCode(RE::INPUT_DEVICE::kGamepad),EffectiveGamepadKey(Config()),
                            opening==Opening::Actions?"actions":opening==Opening::Favorites?"favorites":"none",IsOpen(),reason?reason:"none");
                    }
                    bool consume = captureBatch || inputGate.Swallowed(identity);
                    const auto current=Snapshot();
                    if(pageDown && current.open)SKSE::log::info("Controller page input: raw={} key={} nativeDown={} swallowed={} settings={} dialog={}",
                        code,pad,button->IsDown(),inputGate.Swallowed(identity),current.settingsOpen,current.outfitDialog);
                    const bool naming=current.open && (current.outfitDialog==1 || current.outfitDialog==2);
                    if(naming && nameRepeat && (code==14 || code==211 || code==203 || code==205) && !bridge.composing)NameKey(code,false);
                    const bool bridgeKey=naming && bridge.available && bridge.enabled && button->GetDevice()==RE::INPUT_DEVICE::kKeyboard &&
                        (code==bridge.hotkey || code==29 || code==157 || code==42 || code==54 || (bridge.active && code==57 && (GetAsyncKeyState(VK_CONTROL)&0x8000)));
                    if(yieldEntry) {
                        // Leave the event for the contextual action's handler.
                    } else if(down && openQueued && button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && (code==1 || code==15)) {
                        Cancel();
                    } else if(bridgeKey) {
                        // Keep toggle/modifier values intact for Text Bridge regardless of hook order.
                        consume=false;
                    } else if(down && current.open && current.outfitDialog && !inputGate.Swallowed(identity)) {
                        if(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard) {
                            if(naming && bridge.composing) { /* IME owns candidate editing/confirmation. */ }
                            else if(code==1 || code==15)EndOutfitDialog();
                            else if(code==28)OutfitAccept();
                            else if(naming)NameKey(code,!bridge.active && !characterBatch);
                        } else if(button->GetDevice()==RE::INPUT_DEVICE::kMouse && code==0)OutfitClick();
                        else if(pad==277)EndOutfitDialog();
                        else if(pad==276)OutfitClick();
                        else if(pad==279 && !naming)OutfitAccept();
                    } else if (down && current.open && current.settingsOpen && !inputGate.Swallowed(identity)) {
                        if(current.capturingKey) {
                            const bool cancel=(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && code==1) || pad==277;
                            const bool capture=current.captureBinding==15?(pad>=266 && pad<=281):
                                (button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && code<255 && !ModifierBit(code));
                            if(cancel || capture) {
                                if(!cancel) {
                                    auto config=Config();
                                    if(current.captureBinding==15)config.gamepadHotkey=static_cast<int>(pad);
                                    else if(current.captureBinding==11)config.switchKey=static_cast<int>(code);
                                    else if(current.captureBinding==13){config.actionHotkey=static_cast<int>(code);config.actionModifier=HeldModifiers();}
                                    else {config.hotkey=static_cast<int>(code);config.hotkeyModifier=HeldModifiers();}
                                    EditSettings(config);
                                }
                                std::lock_guard lock(viewMutex);view.capturingKey=false;
                            }
                        } else if(button->GetDevice()==RE::INPUT_DEVICE::kKeyboard) {
                            if(code==1 || code==15) LeaveSettings(false);
                        } else if(button->GetDevice()==RE::INPUT_DEVICE::kMouse && code<2) SettingsClick(code==1);
                        else if(pad==276 || pad==278)SettingsClick(pad==278);
                        else if(pad==277)LeaveSettings(false);
                        else if(pad==274 || pad==275){std::lock_guard lock(viewMutex);view.settingsTab=NextSettingsTab(view.settingsTab,pad==275?1:-1);}
                    } else if (down && current.open && !inputGate.Swallowed(identity) &&
                        ((button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && code==60) || pad==270)) {
                        EnterSettings(); // Remains reachable even if Favorites itself is bound to F2.
                    } else if(down && current.open && !inputGate.Swallowed(identity) &&
                        ((button->GetDevice()==RE::INPUT_DEVICE::kKeyboard && code==static_cast<unsigned>(Config().switchKey)) || pad==279)) {
                        SwitchWheel();
                    } else if (down && !inputGate.Swallowed(identity) && toggle && (!pad || !current.open)) {
                        if (IsOpen() || openQueued) { Cancel(true); consume = captureBatch = true; }
                        else if (closing || HasPendingAction() || Outfits::Busy()) { consume = true;if(Outfits::Busy())RE::SendHUDMessage::ShowHUDMessage(Tr(Config(),"outfitWorking").c_str()); }
                        else if (!OpenBlockReason()) { RequestOpen(opening==Opening::Actions,button->GetDevice(),code); consume = captureBatch = true; }
                    } else if (down && IsOpen() && !inputGate.Swallowed(identity)) {
                        if (button->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                            const int quickSlot=QuickSlotFromKey(code,button->GetUserEvent().c_str());
                            if(!current.functions && HeldModifiers()==0 && quickSlot>=0)BindQuickSlot(quickSlot);
                            else switch (code) {
                            case 1: case 15: if(current.functions && current.functionSection==FaceLight::Section::Followers)FunctionBack();else Cancel(true); break; // Esc / Tab
                            case 30: case 203: ChangeCategory(-1); break; // A / left
                            case 32: case 205: ChangeCategory(1); break; // D / right
                            case 17: case 200: ChangePage(-1); break; // W / up
                            case 31: case 208: ChangePage(1); break; // S / down
                            case 28: Activate(false); break;
                            default: break;
                            }
                        } else if (button->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                            if (code == 0 || code == 1) Activate(code == 1);
                            else if (code == 8) ChangePage(-1);
                            else if (code == 9) ChangePage(1);
                        } else if(pad) {
                            switch(ControllerCommand(pad,current.config.gamepadCategoryButtons)) {
                            case PadWheelCommand::UseRight:Activate(false);break;
                            case PadWheelCommand::UseLeft:Activate(true);break;
                            case PadWheelCommand::Back:if(current.functions && current.functionSection==FaceLight::Section::Followers)FunctionBack();else Cancel(true);break;
                            case PadWheelCommand::PreviousCategory:ChangeCategory(-1);break;
                            case PadWheelCommand::NextCategory:ChangeCategory(1);break;
                            case PadWheelCommand::PreviousPage:ChangePage(-1,"up");break;
                            case PadWheelCommand::NextPage:ChangePage(1,"down");break;
                            default:break;
                            }
                        }
                    } else if(down && !toggle && ReplacedEntrance(button) && !OpenBlockReason()) {
                        // A configured modifier is required for the replaced vanilla entrance.
                        // This only owns the mapped entrance, never number / Numpad quick slots.
                        consume=true;
                    }
                    const bool wheelImpulse = button->GetDevice() == RE::INPUT_DEVICE::kMouse && code >= 8;
                    // Identify the physical gameplay binding, including remaps.
                    // A wheel entrance takes priority even if bound to movement.
                    auto controls=RE::ControlMap::GetSingleton();
                    const auto device=button->GetDevice();
                    const bool movement=!toggle && !ReplacedEntrance(button) && controls &&
                        (device==RE::INPUT_DEVICE::kKeyboard || device==RE::INPUT_DEVICE::kMouse || device==RE::INPUT_DEVICE::kGamepad) &&
                        IsMovementEvent(controls->GetUserEventName(code,button->GetDevice()));
                    const auto decision = inputGate.Filter(identity, pressed, up,
                        movement?captureBatch:consume, wheelImpulse, movement);
                    if(down && button->GetDevice()==RE::INPUT_DEVICE::kKeyboard &&
                        ((code>=2 && code<=11) || code==82 || (code>=79 && code<=81)))
                        SKSE::log::info("Numeric shortcut input: scan={} event='{}' wheel={} queued={} closing={} filter={}",
                            code,button->GetUserEvent().c_str(),current.open,openQueued.load(),closing.load(),
                            decision==InputGate::Result::Pass?"pass":decision==InputGate::Result::Release?"release":
                            decision==InputGate::Result::Resume?"resume":"suppress");
                    if(down && (pad==274 || pad==275 || pad==280 || pad==281) && consume)
                        SKSE::log::info("Controller wheel input: raw={} key={} event='{}' filter={}",code,pad,
                            button->GetUserEvent().c_str(),decision==InputGate::Result::Suppress?"removed":
                            decision==InputGate::Result::Release?"release":"pass");
                    if(decision==InputGate::Result::Resume) {
                        // A held UI movement key needs a fresh down when gameplay
                        // resumes, rather than waiting for a physical re-press.
                        button->GetRuntimeData().heldDownSecs=0;
                    } else if (decision != InputGate::Result::Pass) {
                        // Release captured gameplay actions once; keep wheel
                        // clicks/entrances suppressed through physical release.
                        auto& data = button->GetRuntimeData();
                        data.value = 0;
                        data.heldDownSecs = decision == InputGate::Result::Release ? std::max(.001f, data.heldDownSecs) : 0;
                        suppress=decision==InputGate::Result::Suppress;
                    }
                } else if (auto mouse = event->AsMouseMoveEvent(); mouse && captureBatch) {
                    if (IsOpen()) {
                        std::lock_guard lock(viewMutex);
                        if(mouse->mouseInputX || mouse->mouseInputY){view.gamepad=false;padX=padY=0;}
                        const auto config=Config();
                        const float width=viewportWidth.load(),height=viewportHeight.load();
                        if(view.settingsOpen || view.outfitDialog) {
                            const float scale=LayoutScale(width,height,config.scale);
                            const float sensitivity=config.sensitivity/(224*scale);
                            view.x=std::clamp(view.x+mouse->mouseInputX*sensitivity,-width/(448*scale),width/(448*scale));
                            view.y=std::clamp(view.y+mouse->mouseInputY*sensitivity,-height/(448*scale),height/(448*scale));
                        } else {
                            MovePointer(view.x,view.y,mouse->mouseInputX*config.sensitivity/250.f,
                                mouse->mouseInputY*config.sensitivity/250.f);
                        }
                    }
                    mouse->mouseInputX = mouse->mouseInputY = 0;
                } else if(auto character=event->AsCharEvent();character && captureBatch) {
                    const auto current=Snapshot();
                    if(current.outfitDialog!=1 && current.outfitDialog!=2)character->keyCode=0;
                } else if (auto stick = event->AsThumbstickEvent(); stick) {
                    const auto config=Config();
                    const bool split=AppliedGamepadMoveWhileOpen();
                    if(stick->IsLeft()) {
                        bool open,modal;
                        {std::lock_guard lock(viewMutex);open=view.open;modal=view.settingsOpen || view.outfitDialog;}
                        if(split && captureBatch && config.timeMode!=0)
                            modal=modal || !Focused() || !ValidPlayer() || BlockedMenu();
                        const auto mode=ControllerLeftStick(split,captureBatch,
                            open,modal,config.timeMode,closing || openQueued);
                        movementStick.Capture(mode==LeftStickMode::HeldDirection);
                        if(mode==LeftStickMode::Block)stick->xValue=stick->yValue=0;
                        movementStick.Filter(stick->xValue,stick->yValue);
                    } else if(stick->IsRight()) {
                        lookStick.Filter(stick->xValue,stick->yValue,captureBatch,split);
                    } else if(captureBatch)stick->xValue=stick->yValue=0;
                }
                dispatch.Append(event,!suppress);
            }
            // Keep genuine gameplay releases and allowed movement/IME events.
            // Restore original queue links after every chained hook has returned;
            // injected character events are consumed during that same dispatch.
            dispatch.Finish();
            previousDispatch(source, dispatch.Events());
            dispatch.Restore();
            if (!Focused()) {
                inputGate.Reset();movementStick.Reset();lookStick.Reset(AppliedGamepadMoveWhileOpen());controllerPages.Reset();controllerTriggers.Reset();shiftHeld[0]=shiftHeld[1]=false;
                std::fill_n(keyboardHeld,256,false);std::fill_n(padHeld,16,false);
                std::lock_guard lock(viewMutex);padX=padY=0;
            }
            if(IsOpen())SyncWheelTime();
            PumpAction();
            {const auto current=Snapshot();if(current.open && !current.functions && !current.settingsOpen && !current.outfitDialog) {
                bool needed;{std::lock_guard lock(viewMutex);needed=!inventoryLoaded;}
                if(needed)RequestDirectory();
            }}
            PumpLiveInventory();
            PumpDetails();
        }

        class WheelMenu final : public RE::IMenu {
        public:
            WheelMenu() {
                depthPriority = 3;
                // This is an input-gated overlay. Native menu mode resets held
                // gameplay movement; input ownership is handled in InputHook.
                menuFlags.set(Flag::kDisablePauseMenu,Flag::kRequiresUpdate);
            }
            static RE::IMenu* Create() { return new WheelMenu; }
            RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override {
                std::lock_guard lifecycle(lifecycleMutex);
                if (message.type == RE::UI_MESSAGE_TYPE::kHide || message.type == RE::UI_MESSAGE_TYPE::kForceHide) {
                    const auto old=Snapshot();
                    if(old.outfitDialog==1 || old.outfitDialog==2)TextBridge::Reset();
                    TimeControl::EndSession();RequestPause(false);
                    retainedAction=false;retainedSubmitted=false;
                    RevertSettings();
                    std::lock_guard lock(viewMutex);
                    if(view.open) view.animateClose=false;
                    view.open = false;view.outfitDialog=0; view.settingsOpen=view.capturingKey=false; closing = false;
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }
                if (message.type == RE::UI_MESSAGE_TYPE::kShow) {
                    if(IsOpen())SyncWheelTime();
                    else if(auto q=RE::UIMessageQueue::GetSingleton())q->AddMessage(menuName,RE::UI_MESSAGE_TYPE::kHide,nullptr);
                    return RE::UI_MESSAGE_RESULTS::kHandled;
                }
                return RE::UI_MESSAGE_RESULTS::kPassOn;
            }
            void AdvanceMovie(float, std::uint32_t) override {SyncWheelTime();}
            void PostDisplay() override {}
        };

        class MenuObserver final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,RE::BSTEventSource<RE::MenuOpenCloseEvent>*)override {
                if(event && event->opening && event->menuName!=menuName && event->menuName!=pauseMenuName && IsOpen()) {
                    auto ui=RE::UI::GetSingleton();auto menu=ui?ui->GetMenu(event->menuName.c_str()):nullptr;
                    if(BlockedMenu() || (menu && (menu->PausesGame() || menu->Modal() || menu->ApplicationMenu())))Cancel();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // A separate fixed-flag menu lets the engine balance its pause counters.
        // Never toggle kPausesGame on an existing menu or modify UI pause counts.
        class PauseGuardMenu final : public RE::IMenu {
        public:
            PauseGuardMenu(){depthPriority=2;menuFlags.set(Flag::kPausesGame,Flag::kDisablePauseMenu);}
            static RE::IMenu* Create(){return new PauseGuardMenu;}
            RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& message) override {
                std::lock_guard lifecycle(lifecycleMutex);
                if(message.type==RE::UI_MESSAGE_TYPE::kShow) {
                    bool wanted;{std::lock_guard lock(pauseMutex);wanted=pauseRequested;}
                    if(!wanted)if(auto q=RE::UIMessageQueue::GetSingleton())q->AddMessage(pauseMenuName,RE::UI_MESSAGE_TYPE::kHide,nullptr);
                }else if(message.type==RE::UI_MESSAGE_TYPE::kHide || message.type==RE::UI_MESSAGE_TYPE::kForceHide) {
                    bool wanted;{std::lock_guard lock(pauseMutex);wanted=pauseRequested;}
                    if(wanted && IsOpen()) {
                        if(message.type==RE::UI_MESSAGE_TYPE::kForceHide)Cancel();
                        else if(auto q=RE::UIMessageQueue::GetSingleton())q->AddMessage(pauseMenuName,RE::UI_MESSAGE_TYPE::kShow,nullptr);
                    }
                }
                return RE::UI_MESSAGE_RESULTS::kHandled;
            }
            void AdvanceMovie(float,std::uint32_t)override{}
            void PostDisplay()override{}
        };

        // Event callbacks only invalidate a stamp; scans are coalesced by the input pump.
        class InventoryObserver final : public RE::BSTEventSink<RE::TESContainerChangedEvent>,public RE::BSTEventSink<RE::TESEquipEvent> {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* event,RE::BSTEventSource<RE::TESContainerChangedEvent>*)override {
                if(event && (event->oldContainer==0x14 || event->newContainer==0x14))++inventoryChanges;
                return RE::BSEventNotifyControl::kContinue;
            }
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event,RE::BSTEventSource<RE::TESEquipEvent>*)override {
                if(event && event->actor.get()==RE::PlayerCharacter::GetSingleton())++inventoryChanges;
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        // Receives the final event chain, including characters injected by inner hooks.
        class InputObserver final : public RE::BSTEventSink<RE::InputEvent*> {
            RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override {
                static bool warned = false;
                if (dispatchCount == 0 && !warned && events && *events) {
                    warned = true;
                    SKSE::log::warn("Input events reach the engine but FavoriteWheel dispatch hook is bypassed by another hook");
                }
                std::lock_guard lock(viewMutex);
                if(view.open)for(auto event=events?*events:nullptr;event;event=event->next)if(auto c=event->AsCharEvent()) {
                    if(view.outfitDialog==1 || view.outfitDialog==2)view.outfitName.Insert(c->keyCode);
                    c->keyCode=0;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    View Snapshot() { std::lock_guard lock(viewMutex); auto copy=view; copy.config=Config();copy.appliedGamepadMove=AppliedGamepadMoveWhileOpen(); return copy; }
    void AdvanceGamepadPointer(float elapsed) {
        std::lock_guard lock(viewMutex);
        if(!view.open || !view.gamepad || (!view.settingsOpen && !view.outfitDialog))return;
        const auto config=Config();
        const float scale=LayoutScale(viewportWidth,viewportHeight,config.scale);
        const float speed=2.4f*config.sensitivity*std::clamp(elapsed,0.f,.05f);
        view.x=std::clamp(view.x+StickAxis(padX)*speed,-viewportWidth.load()/(448*scale),viewportWidth.load()/(448*scale));
        view.y=std::clamp(view.y-StickAxis(padY)*speed,-viewportHeight.load()/(448*scale),viewportHeight.load()/(448*scale));
    }
    void SetViewport(float width,float height) { if(width>0 && height>0) {viewportWidth=width; viewportHeight=height;} }
    bool IsOpen() { std::lock_guard lock(viewMutex); return view.open; }
    void Cancel(bool effects) {
        std::lock_guard lifecycle(lifecycleMutex);
        retainedAction=false;retainedSubmitted=false;
        {std::lock_guard lock(openMutex);++openSerial;openQueued=false;pendingOpen.reset();}
        const auto current=Snapshot();
        if(current.outfitDialog==1 || current.outfitDialog==2)TextBridge::Reset();
        RevertSettings();
        TimeControl::EndSession();RequestPause(false);
        bool wasOpen;
        { std::lock_guard lock(viewMutex); wasOpen = view.open;if(view.functions){if(view.functionSection==FaceLight::Section::Outfits)functionPage=view.page;}else favoritePage=view.page; view.open = false;view.outfitDialog=0; view.animateClose=effects; view.settingsOpen=view.capturingKey=false; }
        if (wasOpen) {
            if(effects && Config().sounds) RE::PlaySound("UIMenuCancel");
            closing = true;
            if (auto queue = RE::UIMessageQueue::GetSingleton()) queue->AddMessage(menuName, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }
    void SetGameActive(bool active) {
        std::lock_guard lifecycle(lifecycleMutex);
        ++epoch;
        Outfits::CancelJob();
        FaceLight::Clear();
        { std::lock_guard lock(actionMutex); pendingAction.reset(); }
        if(active)InstallPlayerUpdate();
        gameActive = active;
        Cancel();
        favoritePage=functionPage=0;shiftHeld[0]=shiftHeld[1]=false;
        inputGate.Reset();movementStick.Reset();lookStick.Reset(AppliedGamepadMoveWhileOpen());controllerPages.Reset();controllerTriggers.Reset();std::fill_n(keyboardHeld,256,false);std::fill_n(padHeld,16,false);
        { std::lock_guard lock(viewMutex);++directoryRevision;inventoryLoaded=false;inventoryPages.Set({});functionItems.clear();
            view.items.clear();view.visibleCategories={0};view.totalItems=0;view.inventoryLoading=false;padX=padY=0; }
        SKSE::log::info("Game active={}", active);
        if (active) {
            const auto config=Config();const auto favorite=EffectiveKeyboardKey(config);
            SKSE::log::info("Input state after load: callbacks={} favoritesScan={} override={} effectiveFavorite={} favoriteModifiers={} effectiveAction={} actionModifiers={}",
                dispatchCount.load(),FavoritesScanCode(),config.hotkey,favorite,config.hotkeyModifier,
                config.actionHotkey<0?favorite:config.actionHotkey,config.actionModifier);
            if(config.hotkey<0 && favorite!=16)SKSE::log::info("Wheel follows the game's remapped Favorites key (scan={}); set Controls/Hotkey=16 to use physical Q",favorite);
        }
    }
    bool InstallWheel() {
        auto ui = RE::UI::GetSingleton();
        if (!ui) return false;
        // Same CommonLib input-dispatch call site used by the user's IME project.
        const auto address = REL::RelocationID(RuntimeSupport::inputDispatchSE, RuntimeSupport::inputDispatchAE).address() + RuntimeSupport::inputDispatchCall;
        if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8) {
            SKSE::log::error("Input dispatch call signature is unsupported; vanilla favorites retained");
            return false;
        }
        ui->Register(menuName, WheelMenu::Create);
        ui->Register(pauseMenuName,PauseGuardMenu::Create);
        static MenuObserver menuObserver;
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuObserver);
        if(auto events=RE::ScriptEventSourceHolder::GetSingleton()) {
            static InventoryObserver observer;
            events->AddEventSink<RE::TESContainerChangedEvent>(&observer);
            events->AddEventSink<RE::TESEquipEvent>(&observer);
        }
        SKSE::AllocTrampoline(32);
        previousDispatch = SKSE::GetTrampoline().write_call<5>(address, InputHook);
        if (auto input = RE::BSInputDeviceManager::GetSingleton()) {
            static InputObserver observer;
            input->AddEventSink(&observer);
        }
        wheelInstalled=true;
        SKSE::log::info("Input dispatch hook installed: runtime={} id={} callOffset={:X}",
            REL::Module::get().version().string(),REL::Module::IsAE()?RuntimeSupport::inputDispatchAE:RuntimeSupport::inputDispatchSE,
            RuntimeSupport::inputDispatchCall);
        return true;
    }
}
