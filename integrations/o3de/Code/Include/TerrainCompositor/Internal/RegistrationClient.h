#pragma once

#include <AzCore/Component/TickBus.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <TerrainCompositor/HeightmapControlThread.h>
#include <TerrainCompositor/Internal/AssetSubscription.h>
#include <TerrainCompositor/TerrainCompositionBus.h>

namespace TerrainCompositor::Internal
{
    // One control-thread owner for a typed claim's lease, session, context and replay protocol.
    template<class Registration, auto EntityMember, auto Register, auto Unregister>
    class RegistrationClient
        : private TerrainCompositionNotificationBus::Handler
        , private AzFramework::EntityContextEventBus::Handler
        , private AZ::SystemTickBus::Handler
    {
    protected:
        using Configuration = decltype(Registration::m_configuration);

        void ActivateClient(AZ::EntityId entityId, const Configuration& configuration,
            const AZ::Transform& worldTransform, bool transformAvailable, bool identityPending, bool nonUniformScale = false)
        {
            if (!m_active)
                m_controlThread.BindForActivation();
            if (!m_controlThread.Check())
                return;
            DeactivateClient();
            m_active = true;
            m_registration.*EntityMember = entityId;
            UpdateClient(configuration, worldTransform, transformAvailable, identityPending, nonUniformScale);
        }

        void UpdateClient(const Configuration& configuration, const AZ::Transform& worldTransform,
            bool transformAvailable, bool identityPending, bool nonUniformScale = false)
        {
            if (!CanUpdate())
                return;
            UpdateClientForContext(configuration, worldTransform, transformAvailable, identityPending,
                nonUniformScale, CurrentContext());
        }

        void UpdateClientForContext(const Configuration& configuration, const AZ::Transform& worldTransform,
            bool transformAvailable, bool identityPending, bool nonUniformScale,
            const AzFramework::EntityContextId& context)
        {
            if (!CanUpdate())
                return;
            const TerrainCompositionAddress address{ context, configuration.m_targetCompositionEntityId };
            if (address != m_address)
            {
                DisconnectTarget();
                m_registration.m_registrationId = AZ::Uuid::CreateRandom();
                m_registration.m_updateRevision = 0;
            }
            m_registration.m_contextId = context;
            m_registration.m_configuration = configuration;
            m_registration.m_worldTransform = worldTransform;
            m_registration.m_transformAvailable = transformAvailable;
            m_registration.m_identityPending = identityPending;
            if constexpr (requires { m_registration.m_hasNonUniformScale; })
                m_registration.m_hasNonUniformScale = nonUniformScale;
            m_address = address;
            ObserveContext(context);
            UpdateAssets();
            if (!context.IsNull() && address.second.IsValid() && (m_registration.*EntityMember).IsValid())
            {
                // Subscribe before probing the provider so either activation order replays the claim.
                if (!TerrainCompositionNotificationBus::Handler::BusIsConnected())
                    TerrainCompositionNotificationBus::Handler::BusConnect(address);
                OnCompositionAvailable({});
            }
            else
            {
                ValidateUnavailable();
            }
        }

        void DeactivateClient()
        {
            if (!m_active || !m_controlThread.Check())
                return;
            m_active = false;
            AZ::SystemTickBus::Handler::BusDisconnect();
            AzFramework::EntityContextEventBus::Handler::BusDisconnect();
            ResetAssets();
            DisconnectTarget();
            m_registration = {};
        }

        template<class Cache>
        bool RefreshAsset(AssetSubscription<Cache>& subscription, const AZ::Data::AssetId& assetId,
            typename AssetSubscription<Cache>::Snapshot& snapshot)
        {
            return subscription.Update(assetId, snapshot, [this] { return CanUpdate(); },
                [this]
                {
                    if (TerrainCompositionNotificationBus::Handler::BusIsConnected())
                        OnCompositionAvailable({});
                });
        }

        bool IsClientActive() const { return m_controlThread.Check() && m_active; }
        bool IsClientRegistered() const { return m_controlThread.Check() && m_registered; }

        const char* GetOrderingStatus(const char* missingTarget, const char* unavailableTarget,
            const char* pendingIdentity, const char* invalidIdentity, const char* duplicateIdentity) const
        {
            if (!m_address.second.IsValid()) return missingTarget;
            if (m_address.first.IsNull())
                return AZ::SystemTickBus::Handler::BusIsConnected()
                    ? "Waiting for entity context ownership."
                    : "Entity context ownership remains unresolved after 8 retry ticks; edit or reactivate to retry.";
            if (!m_registered) return unavailableTarget;
            if (m_registration.m_identityPending) return pendingIdentity;
            const auto key = m_registration.m_configuration.GetRuntimeOrderKey();
            if (key.empty()) return invalidIdentity;
            size_t claims = 0;
            TerrainCompositionRequestBus::EventResult(
                claims, m_address, &TerrainCompositionRequestBus::Events::GetOrderingClaimCount, key);
            return claims > 1 ? duplicateIdentity : nullptr;
        }

        Registration m_registration;
        HeightmapControlThread m_controlThread;
        TerrainCompositionAddress m_address;
        bool m_active = false;
        bool m_registered = false;

    private:
        virtual void UpdateAssets() = 0;
        virtual void ResetAssets() = 0;
        virtual void ValidateUnavailable() {}
        virtual void BeforeRegister() {}

        bool CanUpdate() const { return IsClientActive(); }

        AzFramework::EntityContextId CurrentContext() const
        {
            AzFramework::EntityContextId context{};
            AzFramework::EntityIdContextQueryBus::EventResult(context, m_registration.*EntityMember,
                &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
            return context;
        }

        void DisconnectTarget()
        {
            TerrainCompositionNotificationBus::Handler::BusDisconnect();
            if (m_address.second.IsValid() && !m_address.first.IsNull())
            {
                TerrainCompositionRequestBus::Event(m_address, Unregister, m_registration.*EntityMember,
                    m_registration.m_registrationId, m_registration.m_compositionSession);
            }
            m_registered = false;
            m_address = TerrainCompositionAddress{};
            m_registration.m_compositionSession = {};
        }

        void OnCompositionAvailable(const AZ::Uuid& expectedSession) override
        {
            if (!CanUpdate())
                return;
            AZ::Uuid session{};
            TerrainCompositionRequestBus::EventResult(session, m_address, &TerrainCompositionRequests::GetCompositionSession);
            if (session.IsNull())
            {
                m_registered = false;
                ValidateUnavailable();
                return;
            }
            if (!expectedSession.IsNull() && expectedSession != session)
                return;
            BeforeRegister();
            m_registration.m_compositionSession = session;
            ++m_registration.m_updateRevision;
            TerrainCompositionRequestBus::EventResult(m_registered, m_address, Register, m_registration);
        }

        void OnCompositionUnavailable(const AZ::Uuid& session) override
        {
            if (!m_controlThread.Check() || session != m_registration.m_compositionSession)
                return;
            m_registered = false;
            m_registration.m_compositionSession = {};
        }

        void OnSystemTick() override
        {
            if (!CanUpdate())
                return;
            const auto context = CurrentContext();
            if (context.IsNull())
            {
                if (m_contextRetriesRemaining > 0) --m_contextRetriesRemaining;
                if (m_contextRetriesRemaining == 0) AZ::SystemTickBus::Handler::BusDisconnect();
                return;
            }
            if (context == m_address.first)
                return;
            // Copy current configuration so retries cannot replay a captured old target or placement.
            const auto configuration = m_registration.m_configuration;
            bool nonUniformScale = false;
            if constexpr (requires { m_registration.m_hasNonUniformScale; })
                nonUniformScale = m_registration.m_hasNonUniformScale;
            UpdateClientForContext(configuration, m_registration.m_worldTransform, m_registration.m_transformAvailable,
                m_registration.m_identityPending, nonUniformScale, context);
        }

        void ObserveContext(const AzFramework::EntityContextId& context)
        {
            AZ::SystemTickBus::Handler::BusDisconnect();
            AzFramework::EntityContextEventBus::Handler::BusDisconnect();
            if (context.IsNull())
            {
                m_contextRetriesRemaining = ContextResolutionAttempts;
                AZ::SystemTickBus::Handler::BusConnect();
            }
            else
                AzFramework::EntityContextEventBus::Handler::BusConnect(context);
        }

        void OnEntityContextDestroyEntity(const AZ::EntityId& entityId) override
        {
            if (!CanUpdate() || entityId != m_registration.*EntityMember)
                return;
            const auto configuration = m_registration.m_configuration;
            bool nonUniformScale = false;
            if constexpr (requires { m_registration.m_hasNonUniformScale; })
                nonUniformScale = m_registration.m_hasNonUniformScale;
            UpdateClientForContext(configuration, m_registration.m_worldTransform, m_registration.m_transformAvailable,
                m_registration.m_identityPending, nonUniformScale, {});
        }

        void OnEntityContextReset() override
        {
            OnEntityContextDestroyEntity(m_registration.*EntityMember);
        }

        static constexpr unsigned ContextResolutionAttempts = 8;
        unsigned m_contextRetriesRemaining = 0;
    };
}
