#pragma once
// Copyright (C) 2026 BlackMesa79. SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>
#include <type_traits>

// Windows x64 ABI. No engine/STL objects or allocation ownership cross DLLs.
namespace FaceLightingAPI {
    inline constexpr std::uint32_t version = 1;
    inline constexpr const char* exportName = "FaceLighting_GetAPI";
    enum class Result : std::uint32_t {
        Ok, NoChange, InvalidArgument, WrongThread, NotReady, StaleSession,
        StaleRevision, InvalidTarget, NotLoaded, SourceDisabled, ListFull,
        SaveFailed, BusyPreview, Blocked, InternalError
    };
    enum class Command : std::uint32_t { SetPlayer, SetActor, SetFollowerGroup };
    enum Flag : std::uint32_t {
        // Enabled combines personal preferences. GroupEnabled is the group used
        // by SetActor (follower for teammates, selected otherwise).
        Enabled = 1u << 0, GroupEnabled = 1u << 1, Loaded = 1u << 2,
        Registered = 1u << 3, Follower = 1u << 4, Selected = 1u << 5,
        HiddenSneak = 1u << 6, HiddenDialogue = 1u << 7, HiddenView = 1u << 8,
        HiddenAmbient = 1u << 9, NotAllocated = 1u << 10, MissingModel = 1u << 11,
        Unsafe = 1u << 12, Dialogue = 1u << 13, Fading = 1u << 14,
        FollowerEnabled = 1u << 15, SelectedEnabled = 1u << 16,
        FollowerGroupEnabled = 1u << 17, SelectedGroupEnabled = 1u << 18
    };
    enum Capability : std::uint32_t { PlayerControl = 1, ActorControl = 2, FollowerList = 4, StatusQuery = 8 };
    struct Token { std::uint64_t session = 0; std::uint32_t handle = 0, formID = 0; };
    struct Context {
        std::uint32_t structSize = sizeof(Context), ready = 0;
        std::uint64_t session = 0, revision = 0;
        std::uint32_t playerEnabled = 0, followerGroupEnabled = 0;
    };
    struct ActorState {
        std::uint32_t structSize = sizeof(ActorState), flags = 0;
        Token target;
        std::uint64_t revision = 0;
        char name[256]{}; // Null-terminated UTF-8, truncated at a codepoint boundary.
    };
    struct FollowerPage {
        std::uint32_t structSize = sizeof(FollowerPage), offset = 0, capacity = 0, count = 0;
        std::uint32_t total = 0, reserved = 0;
        std::uint64_t session = 0, revision = 0; // revision=0 starts a fresh snapshot.
        ActorState* rows = nullptr; // Caller-owned; initialize every row's structSize.
    };
    struct Request {
        std::uint32_t structSize = sizeof(Request);
        Command command = Command::SetPlayer;
        Token target; // Non-actor commands require only target.session.
        std::uint64_t revision = 0; // Required opaque preference stamp from a query.
        std::uint32_t enabled = 0, reserved = 0;
    };
    struct Interface {
        std::uint32_t structSize, apiVersion, capabilities, reserved;
        Result (*GetContext)(Context*) noexcept;
        Result (*CaptureTarget)(ActorState*) noexcept;
        Result (*QueryPlayer)(ActorState*) noexcept;
        Result (*QueryActor)(const Token*, ActorState*) noexcept;
        Result (*EnumerateFollowers)(FollowerPage*) noexcept;
        Result (*Execute)(const Request*) noexcept;
    };
    using GetAPI = const Interface* (*)(std::uint32_t requestedVersion) noexcept;
    // GetAPI may run on any thread; all table callbacks require the game thread.
    // Discover with GetModuleHandle/GetProcAddress; never LoadLibrary in a client.
    static_assert(sizeof(Token) == 16 && sizeof(Context) == 32 && sizeof(ActorState) == 288);
    static_assert(sizeof(FollowerPage) == 48 && sizeof(Request) == 40 && sizeof(Interface) == 64);
    static_assert(std::is_standard_layout_v<Interface> && std::is_trivially_copyable_v<ActorState>);
}
