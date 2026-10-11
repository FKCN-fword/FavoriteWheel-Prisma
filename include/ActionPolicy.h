#pragma once
#include <cstdint>
namespace Wheel {
    enum class ActionDecision { Wait, Submit, Discard };
    enum class ActionExecutor { TaskQueue, PlayerUpdate };
    constexpr bool KeepWheelAfterUse(bool enabled,bool consumable) {return enabled && !consumable;}
    constexpr ActionExecutor ExecutorFor(bool faceLightCommand) {
        return faceLightCommand ? ActionExecutor::PlayerUpdate : ActionExecutor::TaskQueue;
    }
    // A queue may only consume its own work, including cancellations/timeouts.
    // SKSE tasks are not necessarily on the thread recognized by Face Lighting.
    inline ActionDecision DecideActionOn(ActionExecutor caller, ActionExecutor owner,
        std::uint64_t expected, std::uint64_t current, bool valid, bool wheelClosing,
        bool wheelOpen, bool paused, bool expired) {
        if (caller != owner) return ActionDecision::Wait;
        if (expected != current || !valid || expired) return ActionDecision::Discard;
        if (wheelClosing || wheelOpen || paused) return ActionDecision::Wait;
        return ActionDecision::Submit;
    }
    // Retained actions may execute with our overlay present, but never through
    // another pause or after a newer wheel has replaced the accepting session.
    inline ActionDecision DecideRetainedActionOn(ActionExecutor caller,ActionExecutor owner,
        std::uint64_t expected,std::uint64_t current,bool valid,bool closing,bool menuOpen,
        bool paused,bool expired,bool retained,bool sameSession,bool viewOpen) {
        if(caller!=owner)return ActionDecision::Wait;
        if(retained && viewOpen && !sameSession)return ActionDecision::Discard;
        return DecideActionOn(caller,owner,expected,current,valid,closing,
            menuOpen && !(retained && sameSession && viewOpen),paused,expired);
    }
    inline bool RetainedActionSettled(bool pending,bool outfitBusy,double elapsed,std::uint64_t updates) {
        // Give native equip queues a subsequent player update before pausing
        // again; wall time alone is insufficient when another menu freezes us.
        return !pending && !outfitBusy && elapsed>=.15 && updates>=2;
    }
    inline ActionDecision DecideAction(std::uint64_t expected, std::uint64_t current,
        bool valid, bool wheelClosing, bool wheelOpen, bool paused, bool expired) {
        return DecideActionOn(ActionExecutor::TaskQueue, ActionExecutor::TaskQueue,
            expected, current, valid, wheelClosing, wheelOpen, paused, expired);
    }
}
