#pragma once
#include <SKSE/SKSE.h>
#include <cstddef>
#include <cstdint>

namespace Wheel::RuntimeSupport
{
    // Exact releases remain opt-in; GOG 1.6.1179 and untested Steam runtimes
    // require their own matching SKSE/Address Library and gameplay reports.
    constexpr bool Supported(REL::Version version)
    {
        return version == REL::Version{1, 5, 97, 0} || version == REL::Version{1, 6, 640, 0} ||
               version == REL::Version{1, 6, 1170, 0} || version == SKSE::RUNTIME_SSE_1_6_1179 ||
               version == REL::Version{1, 7, 99, 0} || version == REL::Version{1, 7, 104, 0};
    }
    constexpr std::uint64_t inputDispatchSE = 67315, inputDispatchAE = 68617;
    constexpr std::ptrdiff_t inputDispatchCall = 0x7B;
    constexpr std::size_t playerUpdateSlot = 0xAD;
} // namespace Wheel::RuntimeSupport
