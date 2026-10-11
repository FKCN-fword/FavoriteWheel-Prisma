#include "FaceLightClient.h"
#include "FaceLightModel.h"
#include "Settings.h"
#include "UIResources.h"
#include <Windows.h>
#include <atomic>
#include <mutex>

namespace Wheel::FaceLight {
    namespace {
        namespace API=FaceLightingAPI;
        std::atomic<const API::Interface*> api{nullptr};
        std::mutex mutex;
        FaceLightModel::Snapshot snapshot;
        std::string TrKey(const char* key){return Tr(Config(),key);}
        const char* ResultKey(API::Result result) {
            switch(result) {
            case API::Result::Ok:return "lightSubmitted";
            case API::Result::NoChange:return "lightNoChange";
            case API::Result::WrongThread:return "lightWrongThread";
            case API::Result::NotReady:return "lightNotReady";
            case API::Result::StaleSession:case API::Result::StaleRevision:return "lightStale";
            case API::Result::InvalidTarget:case API::Result::NotLoaded:return "lightTargetInvalid";
            case API::Result::SourceDisabled:return "lightSourceDisabled";
            case API::Result::ListFull:return "lightListFull";
            case API::Result::SaveFailed:return "lightSaveFailed";
            case API::Result::BusyPreview:return "lightBusyPreview";
            case API::Result::Blocked:return "lightBlocked";
            default:return "lightError";
            }
        }
        Item Nav(ActionKind action,const char* key) {
            Item item;item.category=Category::Other;item.name=TrKey(key);item.magic=true;item.action=action;return item;
        }
        std::string ActorDetail(const API::ActorState& state,bool player=false) {
            std::string result;
            auto add=[&](const char* key){if(!result.empty())result+=" · ";result+=TrKey(key);};
            if(!player && !(state.flags&API::GroupEnabled))add("lightGroupOff");
            if(!(state.flags&API::Loaded))add("lightUnloaded");
            if(state.flags&API::Unsafe)add("lightUnsafe");
            if(state.flags&API::HiddenSneak)add("lightSneak");
            if(state.flags&API::HiddenView)add("lightView");
            if(state.flags&API::HiddenAmbient)add("lightAmbient");
            if(state.flags&API::HiddenDialogue)add("lightDialogueHidden");
            if(state.flags&API::Dialogue)add("lightDialogue");
            if(state.flags&API::MissingModel)add("lightModel");
            if(state.flags&API::NotAllocated)add("lightWaiting");
            if(result.empty())add("lightPreferenceHint");
            return result;
        }
        Item ActorItem(const API::ActorState& state,const std::string& name) {
            auto item=Nav(ActionKind::FaceLightCommand,"lightTarget");item.name=name;item.icon=IconKind::LightTarget;
            item.equipped=(state.flags&API::Enabled)!=0;item.detail=ActorDetail(state);
            auto request=FaceLightModel::ActorRequest(state);item.usable=request.has_value();
            if(request)item.lightRequest=*request;
            if(state.flags&API::GroupEnabled)item.detail+=" · "+TrKey("lightActorSave");
            return item;
        }
        FaceLightModel::Snapshot Cached(){std::lock_guard lock(mutex);return snapshot;}
    }
    void Discover() {
        const auto module=GetModuleHandleW(L"FaceLighting.dll");
        const auto getAPI=module?reinterpret_cast<API::GetAPI>(GetProcAddress(module,API::exportName)):nullptr;
        const auto candidate=getAPI?getAPI(API::version):nullptr;
        const auto found=FaceLightModel::Compatible(candidate)?candidate:nullptr;api=found;
        if(!module)SKSE::log::info("Face Lighting integration disabled: FaceLighting.dll not loaded");
        else if(!getAPI)SKSE::log::warn("Face Lighting integration disabled: public API export missing");
        else if(!candidate)SKSE::log::warn("Face Lighting integration disabled: provider does not offer API V{}",API::version);
        else if(!found)SKSE::log::warn("Face Lighting integration disabled: incompatible function table (version={}, size={})",candidate->apiVersion,candidate->structSize);
        SKSE::log::info("Face Lighting API V1: {} capabilities={:X}",found?"available":"unavailable",found?found->capabilities:0);
    }
    bool Available() { return api.load()!=nullptr; }
    void Capture() {
        auto value=FaceLightModel::Read(api.load());
        if(value.available)SKSE::log::info("Face Lighting snapshot: thread={} result={} session={} target={} followers={} listResult={}",
            GetCurrentThreadId(),static_cast<unsigned>(value.result),value.context.session,static_cast<unsigned>(value.targetResult),value.followers.size(),static_cast<unsigned>(value.followersResult));
        std::lock_guard lock(mutex);snapshot=std::move(value);
    }
    void Clear(){std::lock_guard lock(mutex);snapshot={};}
    std::vector<Item> Entries(Section section) {
        const auto s=Cached();std::vector<Item> rows;
        if(!s.available || s.result!=API::Result::Ok) {
            auto unavailable=Nav(ActionKind::FaceLightMenu,"lightMenu");unavailable.icon=IconKind::LightPlayer;
            unavailable.usable=false;unavailable.detail=TrKey(s.available?ResultKey(s.result):"lightUnavailable");
            rows.push_back(std::move(unavailable));return rows;
        }
        if(section==Section::Followers) {
            for(const auto& actor:s.followers) {
                auto item=ActorItem(actor,actor.name[0]?std::string(actor.name):TrKey("lightUnnamed"));
                if(!(s.capabilities&API::ActorControl)){item.usable=false;item.detail=TrKey("lightUnavailable");}
                if(!(actor.flags&API::Follower)){item.usable=false;item.detail=TrKey("lightNotFollower");}
                rows.push_back(std::move(item));
            }
            if(rows.empty()) {
                auto item=Nav(ActionKind::FaceLightCommand,"lightNoFollowers");item.usable=false;
                item.detail=TrKey(s.followersResult==API::Result::Ok?"lightNoFollowers":ResultKey(s.followersResult));rows.push_back(std::move(item));
            }
        } else {
            auto player=Nav(ActionKind::FaceLightCommand,"lightPlayer");player.icon=IconKind::LightPlayer;
            player.usable=s.result==API::Result::Ok && s.playerResult==API::Result::Ok;
            player.equipped=s.context.playerEnabled!=0;
            player.detail=player.usable?ActorDetail(s.player,true):TrKey(ResultKey(s.playerResult));
            player.lightRequest=FaceLightModel::ContextRequest(s.context,API::Command::SetPlayer);rows.push_back(std::move(player));
            if(s.targetResult==API::Result::Ok)rows.push_back(ActorItem(s.target,TrKey("lightTarget")+" · "+s.target.name));
            else {
                auto target=Nav(ActionKind::FaceLightCommand,"lightTarget");target.icon=IconKind::LightTarget;target.usable=false;
                target.detail=TrKey(s.targetResult==API::Result::InvalidTarget?"lightNoTarget":ResultKey(s.targetResult));rows.push_back(std::move(target));
            }
            auto group=Nav(ActionKind::FaceLightCommand,"lightFollowerGroup");group.icon=IconKind::LightGroup;
            group.equipped=s.context.followerGroupEnabled!=0;group.usable=s.result==API::Result::Ok && (s.capabilities&API::ActorControl);
            group.lightRequest=FaceLightModel::ContextRequest(s.context,API::Command::SetFollowerGroup);group.detail=TrKey("lightGroupHint");rows.push_back(std::move(group));
            auto followers=Nav(ActionKind::FaceLightFollowers,"lightFollowers");followers.icon=IconKind::LightGroup;
            followers.usable=s.followersResult==API::Result::Ok;followers.count=static_cast<int>(s.followers.size());
            followers.detail=followers.usable?TrKey("lightOpen"):TrKey(ResultKey(s.followersResult));rows.push_back(std::move(followers));
        }
        if(section==Section::Followers) {
            auto back=Nav(ActionKind::FunctionBack,"lightBack");back.icon=IconKind::Back;rows.push_back(std::move(back));
        }
        return rows;
    }
    std::string Names() {
        const auto s=Cached();std::string text=s.target.name;
        for(const auto& actor:s.followers)text+=actor.name;
        return text;
    }
    void Execute(const Item& item) {
        const auto functions=api.load();const auto result=FaceLightModel::Submit(functions,item.lightRequest);
        SKSE::log::info("Face Lighting command: thread={} kind={} form={:08X} session={} revision={:X} enabled={} result={}",
            GetCurrentThreadId(),static_cast<unsigned>(item.lightRequest.command),item.lightRequest.target.formID,item.lightRequest.target.session,
            item.lightRequest.revision,item.lightRequest.enabled,static_cast<unsigned>(result));
        RE::SendHUDMessage::ShowHUDMessage(TrKey(functions?ResultKey(result):"lightUnavailable").c_str());
        Clear(); // Explicit reopen obtains new revisions; stale commands are never replayed.
    }
}
