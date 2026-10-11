#pragma once
#include "Favorites.h"
#include <string>
namespace Wheel::Outfits {
    bool Install();
    std::string Names();
    std::vector<Item> Entries();
    bool Capture(const std::string& name,std::uint32_t replace=0);
    bool Rename(std::uint32_t id,const std::string& name);
    bool Remove(std::uint32_t id);
    bool Export(std::uint32_t id);
    bool Import();
    bool Busy();
    void CancelJob();
    void Tick(bool valid);
    void Apply(std::uint32_t id);
}
