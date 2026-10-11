#pragma once
#include "FaceLightingAPI.h"
#include <algorithm>
#include <optional>
#include <vector>

namespace Wheel::FaceLightModel {
    namespace API=FaceLightingAPI;
    struct Snapshot {
        bool available=false;
        std::uint32_t capabilities=0;
        API::Result result=API::Result::NotReady;
        API::Context context;
        API::ActorState player,target;
        API::Result playerResult=API::Result::NotReady,targetResult=API::Result::InvalidTarget,followersResult=API::Result::NotReady;
        std::vector<API::ActorState> followers;
    };
    inline bool Compatible(const API::Interface* api) {
        return api && api->apiVersion==API::version && api->structSize>=sizeof(API::Interface) &&
            api->GetContext && api->CaptureTarget && api->QueryPlayer && api->QueryActor && api->EnumerateFollowers && api->Execute;
    }
    // Face Lighting player-update thread only. Publish a complete list or no list, never mixed revisions.
    inline Snapshot Read(const API::Interface* api) {
        Snapshot s;if(!Compatible(api))return s;
        s.available=true;s.capabilities=api->capabilities;s.result=api->GetContext(&s.context);
        if(s.result!=API::Result::Ok || !s.context.ready) {
            s.context={};if(s.result==API::Result::Ok)s.result=API::Result::NotReady;
            s.playerResult=s.targetResult=s.followersResult=s.result;return s;
        }
        if(api->capabilities&API::PlayerControl)s.playerResult=api->QueryPlayer(&s.player);
        if(s.playerResult!=API::Result::Ok)s.player={};
        if(api->capabilities&API::ActorControl)s.targetResult=api->CaptureTarget(&s.target);
        if(s.targetResult!=API::Result::Ok)s.target={};
        if(!(api->capabilities&API::FollowerList))return s;
        API::FollowerPage count;count.session=s.context.session;
        s.followersResult=api->EnumerateFollowers(&count);
        if(s.followersResult!=API::Result::Ok)return s;
        if(count.total>4096 || !count.revision){s.followersResult=API::Result::InternalError;return s;}
        const auto total=count.total;const auto revision=count.revision;
        std::vector<API::ActorState> rows;
        for(std::uint32_t offset=0;offset<total;) {
            std::vector<API::ActorState> buffer(std::min<std::uint32_t>(64,total-offset));
            API::FollowerPage page;page.session=s.context.session;page.revision=revision;
            page.offset=offset;page.capacity=static_cast<std::uint32_t>(buffer.size());page.rows=buffer.data();
            s.followersResult=api->EnumerateFollowers(&page);
            if(s.followersResult!=API::Result::Ok)return s;
            if(page.total!=total || page.revision!=revision || !page.count || page.count>page.capacity) {
                s.followersResult=API::Result::StaleRevision;return s;
            }
            rows.insert(rows.end(),buffer.begin(),buffer.begin()+page.count);offset+=page.count;
        }
        s.followers=std::move(rows);return s;
    }
    inline API::Result Submit(const API::Interface* api,const API::Request& request) {
        return Compatible(api)?api->Execute(&request):API::Result::NotReady;
    }
    inline bool SafeActor(const API::ActorState& state) {
        return state.target.session && state.target.handle && state.target.formID && state.revision &&
            (state.flags&API::Loaded) && !(state.flags&API::Unsafe);
    }
    inline std::optional<API::Request> ActorRequest(const API::ActorState& state) {
        if(!SafeActor(state))return {};
        const bool enabled=(state.flags&API::Enabled)!=0;
        if(!enabled && !(state.flags&API::GroupEnabled))return {};
        API::Request r;r.command=API::Command::SetActor;r.target=state.target;r.revision=state.revision;r.enabled=!enabled;return r;
    }
    inline API::Request ContextRequest(const API::Context& context,API::Command command) {
        API::Request r;r.command=command;r.target.session=context.session;r.revision=context.revision;
        r.enabled=!(command==API::Command::SetPlayer?context.playerEnabled:context.followerGroupEnabled);return r;
    }
}
