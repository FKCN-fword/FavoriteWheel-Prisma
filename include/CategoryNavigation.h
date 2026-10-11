#pragma once
#include "WheelLogic.h"
#include <cstdint>
namespace Wheel {
    // A small value snapshot: navigation and rendering never scan the inventory.
    struct VisibleCategories {
        std::uint32_t mask=(1u<<categoryCount)-1;
        bool Contains(Category category) const {return (mask&(1u<<static_cast<int>(category)))!=0;}
        int Count() const {
            int count=0;for(int i=0;i<categoryCount;++i)if(Contains(static_cast<Category>(i)))++count;
            return count;
        }
        int Index(Category category) const {
            int index=0;
            for(int i=0;i<categoryCount;++i)if(Contains(static_cast<Category>(i))) {
                if(i==static_cast<int>(category))return index;
                ++index;
            }
            return 0;
        }
        Category At(int index) const {
            for(int i=0;i<categoryCount;++i)if(Contains(static_cast<Category>(i)) && index--==0)return static_cast<Category>(i);
            return Category::Weapons; // Only used for nonempty strips.
        }
        Category Select(Category current) const {
            if(!mask || Contains(current))return current;
            for(int step=1;step<=categoryCount;++step) {
                const auto next=static_cast<Category>(Wrap(static_cast<int>(current)+step,categoryCount));
                if(Contains(next))return next;
            }
            return current;
        }
        Category Move(Category current,int direction) const {
            const auto count=Count();
            return count?At(Wrap(Index(Select(current))+direction,count)):current;
        }
    };
    inline VisibleCategories CategoryVisibility(VisibleCategories populated,bool hideEmpty) {
        return hideEmpty?populated:VisibleCategories{};
    }
}
