#pragma once
#include "Favorites.h"
#include "CategoryNavigation.h"
#include <array>
namespace Wheel {
    struct ItemPage {
        std::vector<Item> items;
        int offset=0,total=0;
    };
    inline ItemPage SlicePage(const std::vector<Item>& items,std::size_t begin,std::size_t count,int& page) {
        page=std::clamp(page,0,PageCount(count)-1);
        const auto offset=static_cast<std::size_t>(page)*slots;
        const auto length=std::min<std::size_t>(slots,count-offset);
        ItemPage result;result.offset=static_cast<int>(offset);result.total=static_cast<int>(count);
        result.items.assign(items.begin()+begin+offset,items.begin()+begin+offset+length);
        return result;
    }
    struct InventoryPages {
        std::vector<Item> items;
        std::array<std::size_t,categoryCount+1> boundaries{};
        VisibleCategories visible{0};
        void Set(std::vector<Item> values) {
            items=std::move(values);
            std::size_t at=0;
            for(int category=0;category<categoryCount;++category) {
                boundaries[category]=at;
                while(at<items.size() && static_cast<int>(items[at].category)==category)++at;
            }
            boundaries[categoryCount]=at;
            visible.mask=0;
            for(int category=0;category<categoryCount;++category)
                if(boundaries[category+1]>boundaries[category])visible.mask|=1u<<category;
        }
        ItemPage Page(Category category,int& page) const {
            const auto type=static_cast<int>(category);
            return SlicePage(items,boundaries[type],boundaries[type+1]-boundaries[type],page);
        }
        std::size_t Index(Category category,int page,int slot)const {
            return boundaries[static_cast<int>(category)]+static_cast<std::size_t>(page)*slots+slot;
        }
    };
}
