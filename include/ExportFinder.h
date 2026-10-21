#pragma once
// Reads the export table of a loaded PE image (x64) without any Windows headers, so it can be unit tested.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Wheel::exports {
    struct Export { std::string name; std::uint32_t rva = 0; };

    template <class T>
    inline bool ReadAt(const std::uint8_t* base, std::size_t offset, T& out) {
        std::memcpy(&out, base + offset, sizeof(T));
        return true;
    }

    // `image` is a mapped module (as returned by GetModuleHandle). Only named exports are listed.
    inline std::vector<Export> List(const std::uint8_t* image, std::size_t limit = 20000) {
        std::vector<Export> result;
        std::uint16_t dosMagic = 0;
        ReadAt(image, 0, dosMagic);
        if (dosMagic != 0x5A4D) return result;
        std::uint32_t ntOffset = 0;
        ReadAt(image, 0x3C, ntOffset);
        std::uint32_t signature = 0;
        ReadAt(image, ntOffset, signature);
        if (signature != 0x00004550) return result;
        const std::size_t optional = ntOffset + 24;
        std::uint16_t magic = 0;
        ReadAt(image, optional, magic);
        if (magic != 0x20B) return result; // PE32+ only
        std::uint32_t exportRva = 0, exportSize = 0;
        ReadAt(image, optional + 112, exportRva);
        ReadAt(image, optional + 116, exportSize);
        if (!exportRva || !exportSize) return result;
        std::uint32_t count = 0, functions = 0, names = 0, ordinals = 0;
        ReadAt(image, exportRva + 24, count);
        ReadAt(image, exportRva + 28, functions);
        ReadAt(image, exportRva + 32, names);
        ReadAt(image, exportRva + 36, ordinals);
        for (std::uint32_t i = 0; i < count && result.size() < limit; ++i) {
            std::uint32_t nameRva = 0;
            std::uint16_t ordinal = 0;
            std::uint32_t functionRva = 0;
            ReadAt(image, names + i * 4, nameRva);
            ReadAt(image, ordinals + i * 2, ordinal);
            ReadAt(image, functions + static_cast<std::size_t>(ordinal) * 4, functionRva);
            if (!nameRva || !functionRva) continue;
            const char* text = reinterpret_cast<const char*>(image + nameRva);
            result.push_back({std::string(text, strnlen(text, 512)), functionRva});
        }
        return result;
    }

    inline bool EndsWith(std::string_view value, std::string_view suffix) {
        return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
    }

    // Find `bool ImGui::<needle>()`: MSVC-mangled "?<needle>@ImGui@@YA_NXZ" or a plain/cimgui-style name.
    inline const Export* FindBoolVoid(const std::vector<Export>& list, std::string_view needle) {
        const Export* best = nullptr;
        for (const auto& item : list) {
            const std::string_view name = item.name;
            if (name.find(needle) == std::string_view::npos) continue;
            bool ok = false;
            if (!name.empty() && name.front() == '?')
                ok = name.find("@ImGui@@") != std::string_view::npos && EndsWith(name, "YA_NXZ");
            else
                ok = name == needle || name == std::string("ig").append(needle) || name == std::string("ImGui_").append(needle);
            if (ok && (!best || item.name.size() < best->name.size())) best = &item;
        }
        return best;
    }
}
