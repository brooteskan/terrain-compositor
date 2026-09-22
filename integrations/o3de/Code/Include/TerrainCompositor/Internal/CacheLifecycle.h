#pragma once

#include <AzCore/EBus/Event.h>

namespace TerrainCompositor::Internal
{
    //! Process-local lifecycle notification for one cache interface type.
    //! Constructors signal after interface registration; destructors signal after
    //! interface removal, so observers can safely probe AZ::Interface from the callback.
    template<class Cache>
    class CacheLifecycle
    {
    public:
        using Event = AZ::Event<bool>;

        static void Connect(typename Event::Handler& handler) { handler.Connect(s_changed); }
        static void Signal(bool available) { s_changed.Signal(available); }

    private:
        inline static Event s_changed;
    };
}
