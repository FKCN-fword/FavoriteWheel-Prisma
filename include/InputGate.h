#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_set>
namespace Wheel {
    inline bool IsMovementEvent(std::string_view event) {
        return event=="Forward" || event=="Back" || event=="Strafe Left" || event=="Strafe Right";
    }
    // Tracks physical buttons across open/close. Wheel clicks must not become game
    // attacks on the next held/release event. Movement has a separate held-key policy.
    class InputGate {
    public:
        enum class Result { Pass, Release, Suppress, Resume };
        bool Swallowed(std::uint64_t id) const { return swallowed.contains(id); }
        Result Filter(std::uint64_t id, bool pressed, bool up, bool capture, bool impulse = false, bool movement = false) {
            if(movement && !impulse) {
                if(capture) {
                    if(pressed)capturedMovement.insert(id);
                    // Keep only a direction the game already received. A new UI
                    // key never starts movement until the wheel actually closes.
                    if(forwarded.contains(id) && !swallowed.contains(id)) {
                        if(up){forwarded.erase(id);capturedMovement.erase(id);}
                        return Result::Pass;
                    }
                } else if(capturedMovement.erase(id) || swallowed.contains(id)) {
                    swallowed.erase(id);
                    if(pressed){forwarded.insert(id);return Result::Resume;}
                    // The engine never saw this UI key, so don't emit its release.
                    const bool known=forwarded.erase(id)!=0;
                    return known?Result::Pass:Result::Suppress;
                }
            }
            if (capture || swallowed.contains(id)) {
                const bool release = forwarded.erase(id) != 0;
                if (pressed && !impulse) swallowed.insert(id);
                if (up) swallowed.erase(id);
                if(up)capturedMovement.erase(id);
                return release ? Result::Release : Result::Suppress;
            }
            if (pressed && !impulse) forwarded.insert(id);
            if (up) forwarded.erase(id);
            return Result::Pass;
        }
        void Reset() { forwarded.clear(); swallowed.clear(); capturedMovement.clear(); }
    private:
        std::unordered_set<std::uint64_t> swallowed;
        std::unordered_set<std::uint64_t> forwarded;
        std::unordered_set<std::uint64_t> capturedMovement;
    };

    // The left stick aims the wheel, but an existing movement vector stays fixed
    // until centering. Re-aiming cannot start or redirect movement while captured.
    class MovementStickGate {
        float lastX=0,lastY=0,heldX=0,heldY=0;
        bool captured=false;
    public:
        void Capture(bool value) {
            if(value && !captured){heldX=lastX;heldY=lastY;}
            if(!value)heldX=heldY=0;
            captured=value;
        }
        void Filter(float& x,float& y) {
            if(!captured){lastX=x;lastY=y;return;}
            if(x*x+y*y<=.04f)heldX=heldY=0;
            x=heldX;y=heldY;
            lastX=x;lastY=y;
        }
        void Reset(){lastX=lastY=heldX=heldY=0;captured=false;}
    };
}
