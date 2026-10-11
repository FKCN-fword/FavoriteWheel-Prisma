#include "Favorites.h"
#include "Settings.h"
#include "ActorRuntime.h"
#include "UIResources.h"
#include "EquipPolicy.h"
#include "ItemInfoCapture.h"
#include "QuickSlots.h"
#include "InventoryPolicy.h"
#include "MagicCategories.h"
#include <chrono>
#include <unordered_map>
#include <optional>

namespace Wheel {
    namespace {
        bool ImportantProvider() {
            // Inventory collection starts in-game, after all SKSE plugins load.
            // Cache presence once; do not inspect modules per item or per frame.
            static const bool loaded=[] {
                const bool found=GetModuleHandleW(L"DoubleFavoriteAsImportant.dll")!=nullptr;
                if(found)SKSE::log::info("Double Favorite As Important detected: Important items excluded from favorites; protection markers preserved");
                return found;
            }();
            return loaded;
        }
        bool IsFavorite(const RE::ExtraHotkey* hotkey) {
            return InventoryFavorite(hotkey!=nullptr,hotkey?static_cast<int>(hotkey->hotkey.underlying()):-1,ImportantProvider());
        }
        ItemKey Key(RE::TESBoundObject* object, RE::ExtraDataList* extra) {
            ItemKey key{object->GetFormID(), reinterpret_cast<std::uintptr_t>(extra)};
            if (extra) {
                if (const auto uid = extra->GetByType<RE::ExtraUniqueID>()) key.unique = uid->uniqueID;
                if (const auto ench = extra->GetByType<RE::ExtraEnchantment>(); ench && ench->enchantment) key.enchantment = ench->enchantment->GetFormID();
                if (const auto health = extra->GetByType<RE::ExtraHealth>()) key.health = health->health;
            }
            return key;
        }
        Category Classify(RE::TESBoundObject* form) {
            if (form->As<RE::TESObjectWEAP>() || form->As<RE::TESAmmo>()) return Category::Weapons;
            if (form->As<RE::TESObjectARMO>()) return Category::Armor;
            if (const auto potion = form->As<RE::AlchemyItem>()) {
                if (potion->IsPoison()) return Category::Other;
                return potion->IsFood() ? Category::Food : Category::Potions;
            }
            return Category::Other;
        }
        IconKind ClassifyIcon(RE::TESBoundObject* form) {
            if(auto weapon=form->As<RE::TESObjectWEAP>()) {
                switch(weapon->GetWeaponType()) {
                case RE::WEAPON_TYPE::kOneHandDagger:return IconKind::Dagger;
                case RE::WEAPON_TYPE::kOneHandAxe:case RE::WEAPON_TYPE::kTwoHandAxe:return IconKind::Axe;
                case RE::WEAPON_TYPE::kOneHandMace:return IconKind::Mace;
                case RE::WEAPON_TYPE::kBow:return IconKind::Bow;
                case RE::WEAPON_TYPE::kCrossbow:return IconKind::Crossbow;
                case RE::WEAPON_TYPE::kStaff:return IconKind::Staff;
                default:return IconKind::Sword;
                }
            }
            if(form->As<RE::TESAmmo>())return IconKind::Arrow;
            if(auto armor=form->As<RE::TESObjectARMO>()) {
                using Slot=RE::BGSBipedObjectForm::BipedObjectSlot;
                if(armor->IsShield())return IconKind::Shield;
                if(armor->IsChestpiece())return armor->IsClothing()?IconKind::Robe:IconKind::Armor;
                if(armor->IsHelmet() || armor->IsCirclet())return IconKind::Helmet;
                if(armor->IsGauntlets())return IconKind::Gloves;
                if(armor->IsBoots())return IconKind::Boots;
                if(armor->HasPartOf(Slot::kRing))return IconKind::Ring;
                if(armor->HasPartOf(Slot::kAmulet))return IconKind::Amulet;
                return IconKind::Armor;
            }
            if(auto potion=form->As<RE::AlchemyItem>())return potion->IsFood()?IconKind::Food:IconKind::Potion;
            if(form->As<RE::ScrollItem>())return IconKind::Scroll;
            if(form->As<RE::TESObjectLIGH>())return IconKind::Torch;
            return IconKind::Other;
        }
        bool Supported(RE::TESBoundObject* form) {
            if (form->As<RE::TESObjectWEAP>() || form->As<RE::TESAmmo>() || form->As<RE::TESObjectARMO>() || form->As<RE::ScrollItem>() || form->As<RE::TESObjectLIGH>()) return true;
            if (auto potion = form->As<RE::AlchemyItem>()) return !potion->IsPoison();
            return false;
        }
        bool Plain(RE::ExtraDataList* extra) {
            return !extra->HasType<RE::ExtraEnchantment>() && !extra->HasType<RE::ExtraHealth>() &&
                !extra->HasType<RE::ExtraTextDisplayData>() && !extra->HasType<RE::ExtraUniqueID>();
        }
        void Notify(const char* key) { RE::SendHUDMessage::ShowHUDMessage(Tr(Config(),key).c_str()); }
        RE::BGSEquipSlot* Hand(bool left) {
            const auto defaults = RE::BGSDefaultObjectManager::GetSingleton();
            return defaults ? defaults->GetObject<RE::BGSEquipSlot>(left ? RE::DEFAULT_OBJECT::kLeftHandEquip : RE::DEFAULT_OBJECT::kRightHandEquip) : nullptr;
        }
        void AppendInventory(std::vector<Item>& result,RE::TESBoundObject* object,int count,
            RE::InventoryEntryData* entry,bool allInventory,const Item* exact=nullptr) {
            if(!object || count<=0)return;
            const auto category=Classify(object);const auto icon=ClassifyIcon(object);const bool usable=Supported(object);
            auto append=[&](RE::ExtraDataList* extra,int amount) {
                if(amount<=0)return;
                const auto hotkey=extra?extra->GetByType<RE::ExtraHotkey>():nullptr;
                const bool favorite=IsFavorite(hotkey);
                if(!InventoryVisible(allInventory,favorite))return;
                if(exact && (reinterpret_cast<std::uintptr_t>(extra)!=exact->key.extra ||
                    Key(object,extra)!=exact->key || (exact->favorited && !favorite)))return;
                const char* name=extra?extra->GetDisplayName(object):object->GetName();
                if(!name || !*name)name=object->GetName();
                result.push_back({Key(object,extra),category,name && *name?name:"?",amount,
                    extra && (extra->HasType<RE::ExtraWorn>() || extra->HasType<RE::ExtraWornLeft>()),
                    false,usable,ActionKind::Favorite,0,icon});
                auto& item=result.back();item.favorited=favorite;item.inventoryWide=allInventory;
                if(hotkey){const int assigned=static_cast<int>(hotkey->hotkey.underlying());item.quickSlot=ValidQuickSlot(assigned)?assigned:-1;}
            };
            InventoryBudget budget{count};
            RE::ExtraDataList* anchor=nullptr;bool anchorFavorite=false;
            if(entry && entry->extraLists)for(auto extra:*entry->extraLists)if(extra) {
                const int amount=budget.Take(extra->GetCount());
                const bool favorite=IsFavorite(extra->GetByType<RE::ExtraHotkey>());
                if(amount>0 && (!anchor || (!anchorFavorite && favorite)) && InventoryVisible(allInventory,favorite) && Plain(extra)) {
                    // Preserve native favorites behavior; all-inventory merging
                    // additionally protects ownership/charge/poison/soul differences.
                    const bool compatible=!allInventory || (!extra->HasType<RE::ExtraOwnership>() &&
                        !extra->HasType<RE::ExtraCharge>() && !extra->HasType<RE::ExtraPoison>() && !extra->HasType<RE::ExtraSoul>());
                    if(compatible){anchor=extra;anchorFavorite=favorite;}
                }
            }
            const int spare=budget.Spare();budget.remaining=count;
            if(entry && entry->extraLists)for(auto extra:*entry->extraLists)if(extra) {
                const int amount=budget.Take(extra->GetCount());
                append(extra,amount+(extra==anchor?spare:0));
            }
            if(allInventory && !anchor)append(nullptr,spare);
        }
        std::optional<Item> ResolveItem(const Item& requested) {
            auto player=RE::PlayerCharacter::GetSingleton();if(!player)return {};
            auto form=RE::TESForm::LookupByID(requested.key.form);if(!form)return {};
            if(requested.magic) {
                auto favorites=RE::MagicFavorites::GetSingleton();if(!favorites)return {};
                const auto spell=form->As<RE::SpellItem>();const auto shout=form->As<RE::TESShout>();
                if((!spell || !player->HasSpell(spell)) && (!shout || !player->HasShout(shout)))return {};
                if(!MagicCategory(shout!=nullptr,spell?spell->GetSpellType():RE::MagicSystem::SpellType::kVoicePower))return {};
                if(std::find(favorites->spells.begin(),favorites->spells.end(),form)==favorites->spells.end())return {};
                Item item=requested;item.quickSlot=-1;
                for(int i=0;i<nativeQuickSlots && i<static_cast<int>(favorites->hotkeys.size());++i)
                    if(favorites->hotkeys[i]==form){item.quickSlot=i;break;}
                return item;
            }
            const auto object=form->As<RE::TESBoundObject>();if(!object)return {};
            // Borrow only the matching form's entry, without allocating a whole
            // inventory map or cloning its instance-list nodes for each hover.
            RE::InventoryEntryData* entry=nullptr;std::int64_t count=0;
            if(auto changes=player->GetInventoryChanges();changes && changes->entryList)
                for(auto candidate:*changes->entryList)if(candidate && candidate->object==object){entry=candidate;count=candidate->countDelta;break;}
            if((!entry || !entry->IsLeveled()))if(auto container=player->GetContainer())
                container->ForEachContainerObject([&](RE::ContainerObject& base) {
                    if(base.obj==object)count+=base.count;
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            if(count<=0)return {};
            std::vector<Item> candidates;
            AppendInventory(candidates,object,static_cast<int>(std::min<std::int64_t>(count,INT_MAX)),entry,requested.inventoryWide,&requested);
            for(auto& candidate:candidates)if(candidate.key==requested.key && (!requested.favorited || candidate.favorited))return std::move(candidate);
            return {};
        }
    }

    static std::vector<Item> CollectDirectory(bool allInventory,bool includeInfo) {
        const auto started=std::chrono::steady_clock::now();
        std::vector<Item> result;
        const auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return result;
        // Borrow native entries only for this synchronous walk. Match CommonLib's
        // change/base-container count merge without cloning every extra-list node.
        struct Stock {std::int64_t count=0;RE::InventoryEntryData* entry=nullptr;};
        std::unordered_map<RE::TESBoundObject*,Stock> inventory;
        auto changes=player->GetInventoryChanges();
        if(changes && changes->entryList)for(auto entry:*changes->entryList)
            if(entry && entry->object)inventory.emplace(entry->object,Stock{entry->countDelta,entry});
        if(auto container=player->GetContainer())container->ForEachContainerObject([&](RE::ContainerObject& base) {
            if(base.obj) {
                auto& stock=inventory[base.obj];
                if(!stock.entry || !stock.entry->IsLeveled())stock.count+=base.count;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        result.reserve(inventory.size());
        for(const auto& [object,stock]:inventory) {
            if(stock.count<=0 || (!allInventory && (!stock.entry || !stock.entry->IsFavorited())))continue;
            const auto first=result.size();
            AppendInventory(result,object,static_cast<int>(std::min<std::int64_t>(stock.count,INT_MAX)),stock.entry,allInventory);
            if(includeInfo)for(auto i=first;i<result.size();++i) {
                result[i].info=CaptureItemInfo(object,reinterpret_cast<RE::ExtraDataList*>(result[i].key.extra),player);
                result[i].infoReady=true;
            }
        }
        if (auto favorites = RE::MagicFavorites::GetSingleton()) {
            for (auto form : favorites->spells) {
                if (!form) continue;
                const auto spell = form->As<RE::SpellItem>();
                const auto shout = form->As<RE::TESShout>();
                if ((!spell || !player->HasSpell(spell)) && (!shout || !player->HasShout(shout))) continue;
                const auto category=MagicCategory(shout!=nullptr,spell?spell->GetSpellType():RE::MagicSystem::SpellType::kVoicePower);
                if(!category)continue;
                const char* name = form->GetName();
                bool equipped = player->GetEquippedObject(false) == form || player->GetEquippedObject(true) == form;
                if (auto defaults = RE::BGSDefaultObjectManager::GetSingleton()) {
                    if (auto slot = defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECT::kVoiceEquip))
                        equipped |= player->GetEquippedObjectInSlot(slot) == form;
                }
                result.push_back({{form->GetFormID()}, *category, name && *name ? name : "?", 1, equipped, true, true});
                result.back().inventoryWide=allInventory;
                for(int slot=0;slot<nativeQuickSlots && slot<static_cast<int>(favorites->hotkeys.size());++slot)
                    if(favorites->hotkeys[slot]==form){result.back().quickSlot=slot;break;}
                if(includeInfo){if(spell)result.back().info=CaptureSpellInfo(spell,player);result.back().infoReady=true;}
            }
        }
        std::sort(result.begin(), result.end(), [allInventory](const Item& a, const Item& b) {
            if (a.category != b.category) return a.category < b.category;
            if(allInventory) {
                if(a.equipped!=b.equipped)return a.equipped>b.equipped;
                if(a.favorited!=b.favorited)return a.favorited>b.favorited;
            }
            if (a.name != b.name) return a.name < b.name;
            if (a.key.form != b.key.form) return a.key.form < b.key.form;
            if (a.key.unique != b.key.unique) return a.key.unique < b.key.unique;
            return a.key.extra < b.key.extra;
        });
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        SKSE::log::info("Inventory directory: scope={} items={} details={} total_ms={:.2f}",allInventory?"all":"favorites",result.size(),includeInfo,elapsed);
        return result;
    }

    std::vector<Item> CollectFavorites(bool includeInfo){return CollectDirectory(false,includeInfo);}
    std::vector<Item> CollectInventory(bool allInventory){return CollectDirectory(allInventory,false);}
    bool ReadItemInfo(const Item& requested,ItemInfo& info) {
        const auto found=ResolveItem(requested);if(!found)return false;
        auto player=RE::PlayerCharacter::GetSingleton();
        auto form=RE::TESForm::LookupByID(found->key.form);if(!player || !form)return false;
        if(found->magic) {if(auto spell=form->As<RE::SpellItem>())info=CaptureSpellInfo(spell,player);}
        else if(auto object=form->As<RE::TESBoundObject>())info=CaptureItemInfo(object,reinterpret_cast<RE::ExtraDataList*>(found->key.extra),player);
        return true;
    }

    bool BindFavoriteQuickSlot(const Item& requested,int slot,int* assignment) {
        if(!ValidQuickSlot(slot) || requested.action!=ActionKind::Favorite)return false;
        auto player=RE::PlayerCharacter::GetSingleton();
        auto magic=RE::MagicFavorites::GetSingleton();
        if(!player || !magic || ActorRuntime::IsDead(player))return false;
        // Revalidate the exact inventory instance before using its borrowed extra list.
        const auto found=ResolveItem(requested);
        if(!found){Notify("changed");return false;}
        if(!found->favorited){Notify("quickSlotNeedsFavorite");return false;}
        RE::ExtraHotkey* target=nullptr;
        RE::TESForm* spell=nullptr;
        if(found->magic) {
            spell=RE::TESForm::LookupByID<RE::TESForm>(found->key.form);
            if(!spell || (!spell->As<RE::SpellItem>() && !spell->As<RE::TESShout>()))return false;
        } else {
            auto extra=reinterpret_cast<RE::ExtraDataList*>(found->key.extra);
            target=extra?extra->GetByType<RE::ExtraHotkey>():nullptr;
            if(!target)return false;
        }
        const int next=AssignedQuickSlot(found->quickSlot,slot);
        // Allocate the magic-slot array before modifying anything else.
        // Do not truncate slots owned by extensions beyond the native eight.
        if(spell && next>=0 && magic->hotkeys.size()<nativeQuickSlots)magic->hotkeys.resize(nativeQuickSlots);
        bool inventoryChanged=false;
        auto changes=player->GetInventoryChanges();
        if(changes && changes->entryList)for(auto entry:*changes->entryList) {
            if(!entry || !entry->object || !entry->extraLists)continue;
            for(auto extra:*entry->extraLists)if(extra)if(auto hotkey=extra->GetByType<RE::ExtraHotkey>()) {
                const int old=static_cast<int>(hotkey->hotkey.underlying());
                const int replacement=ReassignedQuickSlot(old,hotkey==target,slot,next);
                if(replacement!=old) {
                    // kUnbound keeps the ExtraHotkey: removing it would also unfavorite the item.
                    hotkey->hotkey=static_cast<RE::ExtraHotkey::Hotkey>(replacement);
                    inventoryChanged=true;
                }
            }
        }
        if(inventoryChanged)player->AddChange(static_cast<std::uint32_t>(RE::TESObjectREFR::ChangeFlags::kInventory));
        for(std::size_t i=0;i<std::min<std::size_t>(magic->hotkeys.size(),nativeQuickSlots);++i)
            if(i==static_cast<std::size_t>(slot) || (spell && magic->hotkeys[i]==spell))magic->hotkeys[i]=nullptr;
        if(spell && next>=0)magic->hotkeys[next]=spell;
        if(assignment)*assignment=next;
        SKSE::log::info("Favorite quick slot: form={:08X} extra={:X} key={} assignment={}",
            found->key.form,found->key.extra,slot+1,next<0?"unbound":"bound");
        return true;
    }

    void UseFavorite(const Item& requested, bool leftHand) {
        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto manager = RE::ActorEquipManager::GetSingleton();
        if (!player || !manager || ActorRuntime::IsDead(player)) return;
        // Resolve again at execution time. A queued action must never use an expired extra-list pointer.
        const auto found = ResolveItem(requested);
        if (!found) {
            Notify("changed");
            SKSE::log::warn("Favorite no longer matches: {:08X}", requested.key.form);
            return;
        }
        if (!found->usable) { Notify("unsupported"); return; }
        const auto form = RE::TESForm::LookupByID(requested.key.form);
        if (!form) return;
        if (auto shout = form->As<RE::TESShout>()) { manager->EquipShout(player, shout); return; }
        if (auto spell = form->As<RE::SpellItem>()) {
            auto slot = spell->GetEquipSlot();
            const auto type = spell->GetSpellType();
            if (type == RE::MagicSystem::SpellType::kSpell && !spell->IsTwoHanded()) slot = Hand(leftHand);
            if (slot) manager->EquipSpell(player, spell, slot);
            return;
        }
        auto object = form->As<RE::TESBoundObject>();
        if (!object) return;
        // This address was matched against a fresh inventory walk above, on this same game-thread task.
        auto extra = reinterpret_cast<RE::ExtraDataList*>(found->key.extra);
        if (auto potion = object->As<RE::AlchemyItem>()) {
            if (potion->IsPoison()) return;
            // Use the engine equip entry point: UAPNG hooks this path, and EAS
            // listens for OnObjectEquipped. Never also call DrinkPotion or send
            // a synthetic equip event: animation mods may defer consumption.
            manager->EquipObject(player, object, extra, 1);
            SKSE::log::info("Submitted consumable {:08X} through EquipObject (food={}, count=1)", found->key.form, potion->IsFood());
            return;
        }
        RE::BGSEquipSlot* slot = nullptr;
        bool unequipWeapon=false;
        if (auto weapon = object->As<RE::TESObjectWEAP>()) {
            const bool twoHands = weapon->IsTwoHandedSword() || weapon->IsTwoHandedAxe() || weapon->IsBow() || weapon->IsCrossbow();
            slot = twoHands ? weapon->GetEquipSlot() : Hand(leftHand);
            unequipWeapon=UnequipWeapon(twoHands,leftHand,extra && extra->HasType<RE::ExtraWorn>(),
                extra && extra->HasType<RE::ExtraWornLeft>());
        } else if (auto scroll = object->As<RE::ScrollItem>()) {
            slot = scroll->IsTwoHanded() ? scroll->GetEquipSlot() : Hand(leftHand);
        }
        // Match the freshly resolved instance and requested hand, not just the base FormID.
        if (unequipWeapon || (object->As<RE::TESObjectARMO>() && found->equipped))
            manager->UnequipObject(player, object, extra, 1, unequipWeapon?slot:nullptr, false, false, true, true);
        else
            manager->EquipObject(player, object, extra, object->As<RE::TESAmmo>() ? found->count : 1, slot, false, false, true, true);
        SKSE::log::info("Used favorite {:08X}, left={}, extra={:X}", found->key.form, leftHand, found->key.extra);
    }
}
