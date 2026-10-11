#pragma once
#include "OutfitModel.h"
namespace Wheel::Outfits {
    struct PlannedOperation {Piece piece;bool equip;};
    // Candidate owns only an identity in the plan; runtime extras are reacquired per call.
    template<class Candidate> std::vector<PlannedOperation> ReplacementPlan(
        const std::vector<Candidate>& inventory,const std::vector<int>& matches,bool removeOnly) {
        std::vector<PlannedOperation> result;
        if(!removeOnly) {
            auto targets=matches;
            // Skyrim biped slot 32 (Body): cover the torso before accessories.
            constexpr std::uint32_t body=1u<<(32-30);
            std::stable_sort(targets.begin(),targets.end(),[&](int a,int b){
                return (inventory[a].mask&body)!=0 && (inventory[b].mask&body)==0;
            });
            for(int i:targets)if(!inventory[i].worn)result.push_back({inventory[i].piece,true});
        }
        for(std::size_t i=0;i<inventory.size();++i)if(inventory[i].worn &&
            (removeOnly || std::find(matches.begin(),matches.end(),static_cast<int>(i))==matches.end()))
            result.push_back({inventory[i].piece,false});
        return result;
    }
    // Limits submissions as well as time; one slow engine call cannot be preempted.
    inline bool OutfitBudgetAvailable(unsigned calls,double elapsedMilliseconds) {
        return calls<2 && elapsedMilliseconds<3.;
    }
}
