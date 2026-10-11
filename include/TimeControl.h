#pragma once
namespace Wheel::TimeControl {
    void BeginSession();
    bool Update(bool slow,int percent); // Native menu/input/task callbacks only; never Present.
    void EndSession(); // Idempotent; only restores a multiplier still owned by us.
    bool Active();
}
