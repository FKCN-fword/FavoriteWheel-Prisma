#pragma once
#include <Windows.h>
#include <cstdint>

namespace Wheel::TextBridge {
    struct State {bool available=false,enabled=false,active=false,composing=false;unsigned hotkey=0;};
    inline State Query() {
        using QueryFn=std::uint32_t (*)(std::uint32_t,std::uint32_t*);
        const auto module=GetModuleHandleW(L"SkyrimTextBridge.dll");
        auto query=module?reinterpret_cast<QueryFn>(GetProcAddress(module,"SkyrimTextBridge_QueryInputV1")):nullptr;
        if(!query)return {};
        std::uint32_t hotkey=0;const auto flags=query(1,&hotkey);
        return {bool(flags&1),bool(flags&2),bool(flags&4),bool(flags&8),hotkey};
    }
    inline void Reset() {
        using ResetFn=void (*)();
        const auto module=GetModuleHandleW(L"SkyrimTextBridge.dll");
        auto reset=module?reinterpret_cast<ResetFn>(GetProcAddress(module,"SkyrimTextBridge_ResetInputV1")):nullptr;
        if(reset)reset();
    }
}
