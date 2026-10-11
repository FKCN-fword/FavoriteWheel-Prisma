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
        std::array<std::size_t,groupCount+1> boundaries{};
        VisibleCategories visible{0};
        void Set(std::vector<Item> values) {
            // Items arrive sorted by category; groups are runs of adjacent categories.
            std::erase_if(values,[](const Item& item){return GroupOf(item.category)<0;});
            items=std::move(values);
            std::size_t at=0;
            visible.mask=0;
            for(int group=0;group<groupCount;++group) {
                boundaries[group]=at;
                while(at<items.size() && GroupOf(items[at].category)==group)++at;
                if(at>boundaries[group])visible.mask|=1u<<static_cast<int>(groupCategory[group]);
            }
            boundaries[groupCount]=at;
        }
        ItemPage Page(Category category,int& page) const {
            const int group=std::max(0,GroupOf(category));
            return SlicePage(items,boundaries[group],boundaries[group+1]-boundaries[group],page);
        }
        std::size_t Index(Category category,int page,int slot)const {
            return boundaries[std::max(0,GroupOf(category))]+static_cast<std::size_t>(page)*slots+slot;
        }
    };
}
