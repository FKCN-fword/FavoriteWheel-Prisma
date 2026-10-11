#include "Outfits.h"
#include "OutfitModel.h"
#include "OutfitStep.h"
#include "OutfitPlan.h"
#include "UIResources.h"
#include "Settings.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <optional>
#include <unordered_map>
namespace Wheel::Outfits {
    namespace {
        std::mutex mutex;
        std::vector<Preset> presets;
        std::unordered_map<std::uint32_t,ResolveProblem> reportedProblems; // mutex held; log only state changes.
        constexpr std::uint32_t record=0x46574F50;
        const std::filesystem::path folder="Data/SKSE/Plugins/FavoriteWheel/Outfits";
        struct Candidate {Piece piece; RE::TESObjectARMO* armor; RE::ExtraDataList* extra; bool worn,quest;std::uint32_t mask;};
        void Tell(const char* key) {RE::SendHUDMessage::ShowHUDMessage(Tr(Config(),key).c_str());}
        FormRef Ref(RE::TESForm* form) {
            if(!form)return {};
            if(form->GetFormID()<0xFF000000)if(auto file=form->GetFile(0))return {std::string(file->GetFilename()),form->GetLocalFormID()};
            return {"",form->GetFormID()};
        }
        Piece Describe(RE::TESObjectARMO* armor,RE::ExtraDataList* extra,bool labels) {
            Piece x;x.base=Ref(armor);const auto name=armor->GetName();x.label=name&&*name?name:"?";
            if(extra) {
                // Display-name generation may update text display extras. Capture
                // the instance signature after that work, never before it.
                if(labels)if(auto label=extra->GetDisplayName(armor);label && *label)x.label=label;
                if(auto e=extra->GetByType<RE::ExtraEnchantment>())x.enchantment=Ref(e->enchantment);
                if(auto h=extra->GetByType<RE::ExtraHealth>())x.health=h->health;
                if(auto u=extra->GetByType<RE::ExtraUniqueID>()) {x.unique=u->uniqueID;x.owner=Ref(RE::TESForm::LookupByID(u->baseID));}
                if(auto t=extra->GetByType<RE::ExtraTextDisplayData>();t && t->IsPlayerSet()) {
                    x.customName=t->displayName.c_str();
                    if(t->customNameLength>0 && t->customNameLength<x.customName.size())x.customName.resize(t->customNameLength);
                }
            }
            return x;
        }
        std::vector<Candidate> Inventory(RE::FormID only=0,bool labels=false) {
            const auto start=std::chrono::steady_clock::now();
            std::vector<Candidate> result;
            const auto player=RE::PlayerCharacter::GetSingleton();if(!player)return result;
            for(auto& [object,data]:player->GetInventory([&](RE::TESBoundObject& object) {
                if(only)return object.GetFormID()==only;
                auto armor=object.As<RE::TESObjectARMO>();return armor && !armor->IsShield();
            })) {
                auto armor=object?object->As<RE::TESObjectARMO>():nullptr;
                if(!armor || armor->IsShield() || data.first<=0 || !data.second)continue;
                int represented=0;const bool quest=data.second->IsQuestObject();
                if(data.second->extraLists)for(auto extra:*data.second->extraLists) if(extra) {
                    represented+=std::max(1,extra->GetCount());
                    result.push_back({Describe(armor,extra,labels),armor,extra,
                        extra->HasType<RE::ExtraWorn>()||extra->HasType<RE::ExtraWornLeft>(),quest,armor->GetSlotMask().underlying()});
                }
                if(data.first>represented)result.push_back({Describe(armor,nullptr,labels),armor,nullptr,false,quest,armor->GetSlotMask().underlying()});
            }
            const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(ms>8)SKSE::log::info("Outfit inventory scan: form={:08X}, candidates={}, ms={:.2f}",only,result.size(),ms);
            return result;
        }
        RE::FormID RuntimeID(const FormRef& ref) {
            if(ref.plugin.empty())return ref.id;
            auto data=RE::TESDataHandler::GetSingleton();return data?data->LookupFormID(ref.id,ref.plugin):0;
        }
        const char* ReasonKey(ResolveReason reason) {
            switch(reason) {
            case ResolveReason::Missing:return "outfitItemMissing";
            case ResolveReason::Changed:return "outfitItemChanged";
            case ResolveReason::Ambiguous:return "outfitItemAmbiguous";
            case ResolveReason::Duplicate:return "outfitItemDuplicate";
            default:return "outfitMissing";
            }
        }
        void LogProblem(const Preset& preset,const std::vector<Candidate>& inventory,ResolveProblem problem,const char* stage) {
            if(problem.piece>=preset.pieces.size())return;
            const auto& expected=preset.pieces[problem.piece];
            SKSE::log::warn("Outfit validation: stage={} preset={} name='{}' pieces={} failed_piece={} reason={} label='{}' base={}:{:X} runtime={:08X} uid={} owner={}:{:X} enchant={}:{:X} health={} custom='{}'",
                stage,preset.id,preset.name,preset.pieces.size(),problem.piece+1,ReasonKey(problem.reason),expected.label,
                expected.base.plugin,expected.base.id,RuntimeID(expected.base),expected.unique,expected.owner.plugin,expected.owner.id,
                expected.enchantment.plugin,expected.enchantment.id,expected.health,expected.customName);
            unsigned count=0;
            for(const auto& candidate:inventory)if(candidate.piece.base==expected.base) {
                const auto& actual=candidate.piece;
                if(count<4)SKSE::log::warn("Outfit candidate: worn={} uid={} owner={}:{:X} enchant={}:{:X} health={} custom='{}'",
                    candidate.worn,actual.unique,actual.owner.plugin,actual.owner.id,actual.enchantment.plugin,actual.enchantment.id,actual.health,actual.customName);
                ++count;
            }
            SKSE::log::warn("Outfit validation: same_base_candidates={} (at most 4 detailed above)",count);
        }
        struct Job {
            Preset preset;std::vector<PlannedOperation> operations;std::size_t index=0;
            bool issued=false,removeOnly=false,targetsConfirmed=false;
            std::chrono::steady_clock::time_point started,issuedAt;
        };
        std::optional<Job> job; // Only accessed from SKSE task callbacks.
        std::atomic<bool> busy=false;
        std::atomic<bool> cancelRequested=false;
        void Finish(const char* message) {
            if(job)SKSE::log::info("Outfit {} finished: {} step={}/{} elapsed_ms={:.1f}",job->preset.id,message,
                job->index,job->operations.size(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-job->started).count());
            job.reset();busy=false;
            if(message)Tell(message);
        }
        std::uint32_t NextID() {std::uint32_t id=1;for(const auto& p:presets)id=std::max(id,p.id+1);return id;}
        void Save(SKSE::SerializationInterface* api) {
            std::lock_guard lock(mutex);const auto text=Encode(presets);
            if(!api->WriteRecord(record,1,text.data(),static_cast<std::uint32_t>(text.size())))SKSE::log::error("Unable to save outfit presets");
        }
        void Revert(SKSE::SerializationInterface*) {std::lock_guard lock(mutex);presets.clear();reportedProblems.clear();}
        void Load(SKSE::SerializationInterface* api) {
            std::lock_guard lock(mutex);presets.clear();reportedProblems.clear();std::uint32_t type,version,length;
            while(api->GetNextRecordInfo(type,version,length)) {
                if(type!=record || version!=1 || length>1024*1024)continue;
                std::string text(length,'\0');std::vector<Preset> loaded;
                if(api->ReadRecordData(text.data(),length)!=length || !Decode(text,loaded)) {SKSE::log::error("Invalid outfit record");continue;}
                for(auto& p:loaded)for(auto& x:p.pieces)for(auto r:{&x.base,&x.enchantment,&x.owner}) {
                    if(r->plugin.empty() && r->id) {
                        RE::FormID resolved=0;
                        if(api->ResolveFormID(r->id,resolved))r->id=resolved;
                        else r->plugin="<unresolved>";
                    }
                }
                presets=std::move(loaded);
            }
            SKSE::log::info("Loaded {} outfit presets from character co-save",presets.size());
        }
    }
    bool Install() {
        auto api=SKSE::GetSerializationInterface();if(!api)return false;
        api->SetUniqueID(record);api->SetSaveCallback(Save);api->SetLoadCallback(Load);api->SetRevertCallback(Revert);
        return true;
    }
    std::string Names() {std::lock_guard lock(mutex);std::string names;for(const auto& p:presets)names+=p.name;return names;}
    std::vector<Item> Entries() {
        const auto inventory=Inventory();std::lock_guard lock(mutex);std::vector<Item> result;
        result.push_back({{},Category::Armor,Tr(Config(),"outfitSave"),0,false,true,true,ActionKind::SaveOutfit});
        result.push_back({{},Category::Armor,Tr(Config(),"outfitImport"),0,false,true,true,ActionKind::ImportOutfits});
        for(const auto& p:presets) {
            std::vector<int> matches;ResolveProblem problem;const bool valid=Resolve(p,inventory,matches,&problem);
            result.push_back({{},Category::Armor,p.name,static_cast<int>(p.pieces.size()),valid&&WearingPieces(inventory,matches),false,valid,ActionKind::Outfit,p.id});
            if(valid)reportedProblems.erase(p.id);
            else {
                result.back().detail=Tr(Config(),ReasonKey(problem.reason))+": "+p.pieces[problem.piece].label;
                const auto previous=reportedProblems.find(p.id);
                if(previous==reportedProblems.end() || previous->second.reason!=problem.reason || previous->second.piece!=problem.piece) {
                    LogProblem(p,inventory,problem,"entries");reportedProblems[p.id]=problem;
                }
            }
        }
        return result;
    }
    bool Capture(const std::string& name,std::uint32_t replace) {
        Preset next;next.name=name;if(!ValidName(name))return false;
        const auto inventory=Inventory(0,true);
        for(const auto& c:inventory)if(c.worn)next.pieces.push_back(c.piece);
        if(next.pieces.empty()||next.pieces.size()>64){Tell("outfitEmpty");return false;}
        std::vector<int> matches;ResolveProblem problem;
        if(!Resolve(next,inventory,matches,&problem)) {
            LogProblem(next,inventory,problem,"capture");Tell("outfitCaptureMismatch");return false;
        }
        std::lock_guard lock(mutex);
        auto found=std::find_if(presets.begin(),presets.end(),[&](const auto& p){return p.id==replace;});
        if(replace && found==presets.end())return false;
        if(!replace && presets.size()>=100){Tell("outfitLimit");return false;}
        next.id=replace?replace:NextID();
        auto updated=presets;
        if(replace)updated[std::distance(presets.begin(),found)]=next;else updated.push_back(next);
        std::vector<Preset> validated;
        if(!Decode(Encode(updated),validated)){Tell("outfitLimit");return false;}
        presets=std::move(updated);
        reportedProblems.erase(next.id);
        SKSE::log::info("Outfit saved: preset={} name='{}' pieces={}",next.id,next.name,next.pieces.size());
        Tell("outfitSaved");return true;
    }
    bool Rename(std::uint32_t id,const std::string& name) {
        if(!ValidName(name))return false;std::lock_guard lock(mutex);
        for(auto& p:presets)if(p.id==id) {
            const auto old=p.name;p.name=name;
            if(Encode(presets).size()>1024*1024){p.name=old;return false;}
            return true;
        }return false;
    }
    bool Remove(std::uint32_t id) {std::lock_guard lock(mutex);reportedProblems.erase(id);return std::erase_if(presets,[&](const auto& p){return p.id==id;})>0;}
    bool Export(std::uint32_t id) {
        Preset copy;
        {std::lock_guard lock(mutex);for(const auto& p:presets)if(p.id==id)copy=p;}
        if(!copy.id)return false;
        // Transferable recipes deliberately omit character-local inventory unique ids.
        if(!Portable(copy)){Tell("outfitNotPortable");return false;}
        for(auto& x:copy.pieces){x.unique=0;x.owner={};}
        try {
            std::filesystem::create_directories(folder/"Exports");
            std::filesystem::create_directories(folder/"Imports");
            const auto target=folder/"Exports"/(std::to_string(id)+"-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+".fwo");auto temporary=target;temporary+=".tmp";
            {std::ofstream out(temporary,std::ios::binary);out<<Encode({copy});out.flush();if(!out)throw std::runtime_error("write failed");}
            if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("replace failed");
            Tell("outfitExported");return true;
        }catch(const std::exception& e){SKSE::log::error("Outfit export: {}",e.what());Tell("outfitFileError");return false;}
    }
    bool Import() {
        std::vector<Preset> added;
        try {
            std::filesystem::create_directories(folder/"Imports");
            auto inventory=Inventory();
            for(const auto& file:std::filesystem::directory_iterator(folder/"Imports")) {
                if(!file.is_regular_file()||file.path().extension()!=".fwo")continue;
                if(file.file_size()>1024*1024)throw std::runtime_error("oversized file");
                std::ifstream in(file.path(),std::ios::binary);std::string text((std::istreambuf_iterator<char>(in)),{});
                std::vector<Preset> incoming;if(!Decode(text,incoming))throw std::runtime_error("invalid import");
                for(auto& p:incoming) {
                    if(!Portable(p))throw std::runtime_error("nonportable import");
                    for(auto& piece:p.pieces) {
                        piece.unique=0;piece.owner={};int match=-1;
                        for(int i=0;i<static_cast<int>(inventory.size());++i) {
                            auto neutral=inventory[i].piece;neutral.unique=0;neutral.owner={};
                            if(Same(piece,neutral)) {
                                if(match>=0)throw std::runtime_error("ambiguous import instance");match=i;
                            }
                        }
                        if(match<0)throw std::runtime_error("missing import piece");
                        piece=inventory[match].piece;
                    }
                    std::vector<int> matches;if(!Resolve(p,inventory,matches))throw std::runtime_error("duplicate outfit pieces");
                    added.push_back(std::move(p));if(added.size()>100)throw std::runtime_error("too many imports");
                }
            }
            if(added.empty()){Tell("outfitNoImport");return false;}
            std::lock_guard lock(mutex);
            auto next=presets;
            std::uint32_t nextID=NextID();
            for(auto& p:added) {
                if(std::any_of(next.begin(),next.end(),[&](const auto& existing){return existing.name==p.name && existing.pieces==p.pieces;}))continue;
                p.id=nextID++;next.push_back(std::move(p));
            }
            std::vector<Preset> validated;
            if(!Decode(Encode(next),validated))throw std::runtime_error("preset count or record size limit");
            presets=std::move(next);
            Tell("outfitImported");return true;
        }catch(const std::exception& e){SKSE::log::warn("Outfit import canceled: {}",e.what());Tell("outfitImportFailed");return false;}
    }
    void Apply(std::uint32_t id) {
        if(busy)return;
        Preset p;{std::lock_guard lock(mutex);for(const auto& entry:presets)if(entry.id==id)p=entry;}
        if(!p.id)return;
        auto inventory=Inventory();std::vector<int> matches;ResolveProblem problem;
        if(!Resolve(p,inventory,matches,&problem)) {
            LogProblem(p,inventory,problem,"apply");
            Tell("outfitMissing");return;
        }
        auto player=RE::PlayerCharacter::GetSingleton();auto manager=RE::ActorEquipManager::GetSingleton();if(!player||!manager)return;
        // Preflight before taking anything off. Refuse protected worn items and shield conflicts.
        std::uint32_t shieldMask=0;
        for(auto& [object,data]:player->GetInventory([](RE::TESBoundObject& object){auto armor=object.As<RE::TESObjectARMO>();return armor && armor->IsShield();})) {
            auto armor=object?object->As<RE::TESObjectARMO>():nullptr;
            if(armor && armor->IsShield() && data.second && data.second->IsWorn())shieldMask|=armor->GetSlotMask().underlying();
        }
        const bool removeOnly=Wearing(inventory,matches);
        for(int i:matches)if(!removeOnly && (inventory[i].armor->GetSlotMask().underlying()&shieldMask)){Tell("outfitBlocked");return;}
        for(const auto& c:inventory)if(c.worn) {
            if(c.quest){Tell("outfitBlocked");return;}
        }
        Job next;next.preset=p;next.removeOnly=removeOnly;
        next.operations=ReplacementPlan(inventory,matches,removeOnly);
        next.started=std::chrono::steady_clock::now();
        cancelRequested=false;job=std::move(next);busy=true;
        SKSE::log::info("Outfit {} started: mode=replacement removeOnly={} operations={}",id,removeOnly,job->operations.size());
        Tell("outfitWorking");
    }
    bool Busy() {return busy.load();}
    void CancelJob() {cancelRequested=true;}
    void Tick(bool valid) {
        if(!job)return;
        if(!valid || cancelRequested.exchange(false)){Finish("outfitInterrupted");return;}
        const auto now=std::chrono::steady_clock::now();
        if(now-job->started>std::chrono::seconds(15)){Finish("outfitPartial");return;}
        auto player=RE::PlayerCharacter::GetSingleton();auto manager=RE::ActorEquipManager::GetSingleton();
        if(!player||!manager){Finish("outfitInterrupted");return;}
        unsigned calls=0,checked=0;
        while(checked++<16 && OutfitBudgetAvailable(calls,
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-now).count())) {
            if(job->index==job->operations.size()) {
                auto inventory=Inventory();std::vector<int> matches;
                const bool success=job->removeOnly?
                    std::none_of(inventory.begin(),inventory.end(),[](const auto& c){return c.worn;}):
                    Resolve(job->preset,inventory,matches)&&Wearing(inventory,matches);
                Finish(success?(job->removeOnly?"outfitRemoved":"outfitApplied"):"outfitPartial");return;
            }
            const auto& op=job->operations[job->index];
            // Do not clean up old apparel until every target is observed worn.
            if(!op.equip && !job->removeOnly && !job->targetsConfirmed) {
                const auto current=Inventory();
                std::vector<int> matches;
                if(!Resolve(job->preset,current,matches) || !WearingPieces(current,matches)){Finish("outfitPartial");return;}
                job->targetsConfirmed=true;
                // Validation scans also count toward the per-tick time budget.
                if(!OutfitBudgetAvailable(calls,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-now).count()))return;
            }
            const auto form=RuntimeID(op.piece.base);
            if(!form){Finish("outfitChanged");return;}
            auto inventory=Inventory(form);const int match=Match(op.piece,inventory);
            if(match<0){Finish("outfitChanged");return;}
            const auto& candidate=inventory[match];
            switch(DecideStep(candidate.worn,op.equip,job->issued,std::chrono::duration<double>(std::chrono::steady_clock::now()-job->issuedAt).count())) {
            case StepDecision::Advance: ++job->index;job->issued=false;continue;
            case StepDecision::Wait:return;
            case StepDecision::Timeout:Finish("outfitPartial");return;
            case StepDecision::Issue:break;
            }
            if(!op.equip && candidate.quest){Finish("outfitBlocked");return;}
            // A costly identity scan must not starve the first operation forever.
            if(calls && !OutfitBudgetAvailable(calls,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-now).count()))return;
            const auto start=std::chrono::steady_clock::now();
            if(op.equip)manager->EquipObject(player,candidate.armor,candidate.extra,1,nullptr,false,false,false,true);
            else manager->UnequipObject(player,candidate.armor,candidate.extra,1,nullptr,false,false,false,true);
            job->issued=true;job->issuedAt=std::chrono::steady_clock::now();
            ++calls;
            const double ms=std::chrono::duration<double,std::milli>(job->issuedAt-start).count();
            SKSE::log::info("Outfit {} step {}/{} {} {:08X}: call_ms={:.2f}",job->preset.id,
                job->index+1,job->operations.size(),op.equip?"equip":"unequip",form,ms);
        }
    }
}
