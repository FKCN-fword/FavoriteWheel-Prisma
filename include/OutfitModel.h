#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
namespace Wheel::Outfits {
    struct FormRef {
        std::string plugin;
        std::uint32_t id=0;
        bool operator==(const FormRef&) const = default;
    };
    struct Piece {
        FormRef base, enchantment, owner;
        std::uint16_t unique=0;
        float health=1.f;
        std::string customName, label;
        bool operator==(const Piece&) const = default;
    };
    struct Preset {
        std::uint32_t id=0;
        std::string name;
        std::vector<Piece> pieces;
        bool operator==(const Preset&) const = default;
    };
    inline bool Same(const Piece& a,const Piece& b) {
        return a.base==b.base && a.enchantment==b.enchantment && std::abs(a.health-b.health)<.0001f &&
            a.customName==b.customName && a.unique==b.unique && (!a.unique || a.owner==b.owner);
    }
    inline bool Portable(const Preset& p) {
        return std::all_of(p.pieces.begin(),p.pieces.end(),[](const Piece& x){
            return !x.base.plugin.empty() && (!x.enchantment.id || !x.enchantment.plugin.empty());
        });
    }
    inline bool ValidName(const std::string& name) {
        return !name.empty() && name.size()<=128 &&
            std::none_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c==127;}) &&
            std::any_of(name.begin(),name.end(),[](unsigned char c){return c>32;});
    }
    inline std::string Encode(const std::vector<Preset>& presets) {
        std::ostringstream out;out.imbue(std::locale::classic());out<<"FWO 1 "<<presets.size()<<'\n'<<std::setprecision(std::numeric_limits<float>::max_digits10);
        auto ref=[&](const FormRef& r){out<<std::quoted(r.plugin)<<' '<<r.id<<' ';};
        for(const auto& p:presets) {
            out<<p.id<<' '<<std::quoted(p.name)<<' '<<p.pieces.size()<<'\n';
            for(const auto& x:p.pieces) {
                ref(x.base);ref(x.enchantment);ref(x.owner);
                out<<x.unique<<' '<<x.health<<' '<<std::quoted(x.customName)<<' '<<std::quoted(x.label)<<'\n';
            }
        }
        return out.str();
    }
    inline bool Decode(const std::string& text,std::vector<Preset>& result) {
        if(text.size()>1024*1024) return false;
        std::istringstream in(text);in.imbue(std::locale::classic());std::string magic;unsigned version=0,count=0;
        if(!(in>>magic>>version>>count) || magic!="FWO" || version!=1 || count>100) return false;
        std::vector<Preset> next;
        auto ref=[&](FormRef& r){return bool(in>>std::quoted(r.plugin)>>r.id) && r.plugin.size()<260;};
        for(unsigned i=0;i<count;++i) {
            Preset p;unsigned n=0;
            if(!(in>>p.id>>std::quoted(p.name)>>n) || !p.id || p.id>1000000 || !ValidName(p.name) || n==0 || n>64 ||
                std::any_of(next.begin(),next.end(),[&](const auto& other){return other.id==p.id;})) return false;
            for(unsigned j=0;j<n;++j) {
                Piece x;unsigned uid=0;
                if(!ref(x.base)||!ref(x.enchantment)||!ref(x.owner)||!(in>>uid>>x.health>>std::quoted(x.customName)>>std::quoted(x.label)) ||
                    !x.base.id || uid>65535 || !std::isfinite(x.health) || x.health<=0 || x.customName.size()>512 || x.label.size()>512) return false;
                x.unique=static_cast<std::uint16_t>(uid);p.pieces.push_back(std::move(x));
            }
            next.push_back(std::move(p));
        }
        in>>std::ws;if(!in.eof())return false;
        result=std::move(next);return true;
    }
    // A unique id requires an exact match. Without one, copies with the same
    // complete signature are interchangeable, including temper/name/enchantment.
    template<class Candidate> int Match(const Piece& p,const std::vector<Candidate>& candidates) {
        int found=-1;
        for(int i=0;i<static_cast<int>(candidates.size());++i) if(Same(p,candidates[i].piece)) {
            if(found>=0 && p.unique)return -2;
            if(found<0 || candidates[i].worn) found=i;
        }
        return found;
    }
    enum class ResolveReason { None, Missing, Changed, Ambiguous, Duplicate };
    struct ResolveProblem {
        ResolveReason reason=ResolveReason::None;
        std::size_t piece=0;
    };
    template<class Candidate> bool Resolve(const Preset& p,const std::vector<Candidate>& inventory,std::vector<int>& matches,
        ResolveProblem* problem=nullptr) {
            matches.clear();std::vector<int> used;
            if(problem)*problem={};
            for(std::size_t at=0;at<p.pieces.size();++at) {
                const auto& piece=p.pieces[at];
                const int i=Match(piece,inventory);
                if(i<0 || std::find(used.begin(),used.end(),i)!=used.end()) {
                    if(problem) {
                        const bool basePresent=std::any_of(inventory.begin(),inventory.end(),[&](const auto& c){return c.piece.base==piece.base;});
                        *problem={i==-2?ResolveReason::Ambiguous:i<0?(basePresent?ResolveReason::Changed:ResolveReason::Missing):ResolveReason::Duplicate,at};
                    }
                    return false;
                }
                used.push_back(i);
                matches.push_back(i);
            }
            return true;
        }
    // The badge describes the preset's pieces, including items equipped manually.
    // Additional worn apparel does not make those pieces unequipped.
    template<class Candidate> bool WearingPieces(const std::vector<Candidate>& inventory,const std::vector<int>& matches) {
            return !matches.empty() && std::all_of(matches.begin(),matches.end(),[&](int i){
                return i>=0 && static_cast<std::size_t>(i)<inventory.size() && inventory[i].worn;
            });
        }
    // Outfit transactions still require the complete managed set to match.
    template<class Candidate> bool Wearing(const std::vector<Candidate>& inventory,const std::vector<int>& matches) {
            return WearingPieces(inventory,matches) &&
                std::count_if(inventory.begin(),inventory.end(),[](const auto& c){return c.worn;})==matches.size();
        }

}
