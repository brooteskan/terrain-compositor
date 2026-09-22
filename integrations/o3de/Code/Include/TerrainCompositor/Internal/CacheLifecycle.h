#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/std/function/function_template.h>

namespace TerrainCompositor::Internal
{
    template<class Cache>
    class CacheLifecycleNotifications
        : public AZ::EBusTraits
    {
    public:
        static constexpr AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Multiple;
        static constexpr AZ::EBusAddressPolicy AddressPolicy = AZ::EBusAddressPolicy::Single;

        virtual ~CacheLifecycleNotifications() = default;
        virtual void OnCacheAvailabilityChanged(bool available) = 0;
    };

    template<class Cache>
    using CacheLifecycleNotificationBus = AZ::EBus<CacheLifecycleNotifications<Cache>>;

    //! Process-local lifecycle notification for one cache interface type.
    //! Constructors signal after interface registration; destructors signal after
    //! interface removal, so observers can safely probe AZ::Interface from the callback.
    template<class Cache>
    class CacheLifecycle
    {
    public:
        class Handler final
            : private CacheLifecycleNotificationBus<Cache>::Handler
        {
        public:
            using Callback = AZStd::function<void(bool)>;

            Handler() = default;
            explicit Handler(Callback callback)
                : m_callback(AZStd::move(callback))
            {
            }
            Handler(const Handler&) = delete;
            Handler& operator=(const Handler&) = delete;
            ~Handler() { Disconnect(); }

            void Connect()
            {
                if (!IsConnected())
                {
                    CacheLifecycleNotificationBus<Cache>::Handler::BusConnect();
                }
            }
            void Disconnect() { CacheLifecycleNotificationBus<Cache>::Handler::BusDisconnect(); }
            bool IsConnected() const { return CacheLifecycleNotificationBus<Cache>::Handler::BusIsConnected(); }

        private:
            void OnCacheAvailabilityChanged(bool available) override
            {
                if (m_callback)
                {
                    m_callback(available);
                }
            }

            Callback m_callback;
        };

        static void Connect(Handler& handler) { handler.Connect(); }
        static void Signal(bool available)
        {
            CacheLifecycleNotificationBus<Cache>::Broadcast(
                &CacheLifecycleNotifications<Cache>::OnCacheAvailabilityChanged, available);
        }
    };
}
