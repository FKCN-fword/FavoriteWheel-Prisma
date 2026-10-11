#pragma once
#include <utility>
#include <vector>

namespace Wheel {
    // A dispatch-only view of engine-owned events. Dropped buttons must be absent,
    // not zero-valued: downstream handlers can interpret zero as a release.
    template <class Event>
    class InputDispatchChain {
    public:
        explicit InputDispatchChain(Event* first) : head(first), tail(&head) {}
        InputDispatchChain(const InputDispatchChain&) = delete;
        InputDispatchChain& operator=(const InputDispatchChain&) = delete;
        ~InputDispatchChain() { Restore(); }
        void Restore() noexcept {
            for(auto it=changed.rbegin();it!=changed.rend();++it)*it->first=it->second;
            changed.clear();
        }
        // Visit original nodes in order, before Finish. Only preceding links are
        // changed, so the caller may continue iterating through event->next.
        void Append(Event* event, bool keep) {
            if(!keep)return;
            Link(event);
            tail=&event->next;
        }
        void Finish() { Link(nullptr); }
        Event** Events() { return &head; }
    private:
        void Link(Event* value) {
            if(*tail==value)return;
            // Save before mutation: allocation failure leaves this link intact.
            if(tail!=&head)changed.emplace_back(tail,*tail);
            *tail=value;
        }
        Event* head;
        Event** tail;
        std::vector<std::pair<Event**,Event*>> changed;
    };
}
