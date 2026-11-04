#include <TerrainCompositor/Components/TerrainMeshBlendComponent.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/RTTI/BehaviorContext.h>
#include <AzCore/Script/ScriptContextAttributes.h>
#include <AzCore/Math/MathUtils.h>
#include <Atom/RPI.Public/Material/Material.h>
#include <AzFramework/Visibility/BoundsBus.h>
#include <algorithm>
#include <cmath>

namespace TerrainCompositor
{
    namespace { const AZStd::string DistanceProperty="terrainBlend.distance"; }

    void TerrainMeshBlendComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize=azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<TerrainMeshBlendComponent,AZ::Component>()->Version(1)
                ->Field("TargetCompositionEntityId",&TerrainMeshBlendComponent::m_targetComposition)
                ->Field("BlendDistance",&TerrainMeshBlendComponent::m_blendDistance);
            if (auto* edit=serialize->GetEditContext())
            {
                edit->Class<TerrainMeshBlendComponent>("Terrain Mesh Blend","Register this prefab's retained mesh materials with a terrain compositor. Distance and skirt geometry control blending; no rock masks.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData,"")
                    ->Attribute(AZ::Edit::Attributes::Category,"Terrain")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu,AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand,true)
                    ->DataElement(AZ::Edit::UIHandlers::Default,&TerrainMeshBlendComponent::m_targetComposition,"Target Compositor","Terrain Composition Gradient in this level/context.")
                    ->Attribute(AZ::Edit::Attributes::RequiredService,AZ_CRC_CE("TerrainCompositionService"))
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify,&TerrainMeshBlendComponent::OnConfigurationChanged)
                    ->DataElement(AZ::Edit::UIHandlers::Default,&TerrainMeshBlendComponent::m_blendDistance,"Blend Distance","World metres above the terrain over which appearance blends. Zero disables blending.")
                    ->Attribute(AZ::Edit::Attributes::Min,0.f)
                    ->Attribute(AZ::Edit::Attributes::Suffix," m")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify,&TerrainMeshBlendComponent::OnConfigurationChanged);
            }
        }
        if (auto* behavior=azrtti_cast<AZ::BehaviorContext*>(context))
        {
            behavior->EBus<TerrainMeshBlendRequestBus>("TerrainMeshBlendRequestBus")
                ->Attribute(AZ::Script::Attributes::Module,"terrain")
                ->Attribute(AZ::Script::Attributes::Scope,AZ::Script::Attributes::ScopeFlags::Common)
                ->Event("GetBlendDistance",&TerrainMeshBlendRequestBus::Events::GetBlendDistance)
                ->Event("SetBlendDistance",&TerrainMeshBlendRequestBus::Events::SetBlendDistance)
                ->Event("IsRegistered",&TerrainMeshBlendRequestBus::Events::IsRegistered)
                ->Event("GetAppliedMaterialCount",&TerrainMeshBlendRequestBus::Events::GetAppliedMaterialCount)
                ->Event("GetAppliedBlendDistance",&TerrainMeshBlendRequestBus::Events::GetAppliedBlendDistance)
                ->Event("GetStatus",&TerrainMeshBlendRequestBus::Events::GetStatus);
        }
    }
    void TerrainMeshBlendComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& s) { s.push_back(AZ_CRC_CE("TerrainMeshBlendService")); }
    void TerrainMeshBlendComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& s) { s.push_back(AZ_CRC_CE("TerrainMeshBlendService")); }
    void TerrainMeshBlendComponent::GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& s) { s.push_back(AZ_CRC_CE("TransformService")); }
    void TerrainMeshBlendComponent::EditorActivate(AZ::EntityId id) { m_owner=id; Activate(); }
    void TerrainMeshBlendComponent::EditorDeactivate(AZ::EntityId) { Deactivate(); }
    void TerrainMeshBlendComponent::Activate()
    {
        if (m_active) return;
        if (!m_owner.IsValid()) m_owner=GetEntityId();
        m_active=true;
        TerrainMeshBlendRequestBus::Handler::BusConnect(m_owner);
        AZ::TransformNotificationBus::Handler::BusConnect(m_owner);
        ConnectTarget(); Refresh();
    }
    void TerrainMeshBlendComponent::Deactivate()
    {
        if (!m_active) return;
        m_active=false;
        TerrainMeshBlendRequestBus::Handler::BusDisconnect();
        AZ::TransformNotificationBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        AZ::Render::MaterialComponentNotificationBus::MultiHandler::BusDisconnect();
        m_subscriptions.clear();
        DisconnectTarget(); RestoreOverrides(); m_owner=AZ::EntityId{};
    }
    void TerrainMeshBlendComponent::ConnectTarget()
    {
        AzFramework::EntityContextId context;
        AzFramework::EntityIdContextQueryBus::EventResult(context,m_owner,&AzFramework::EntityIdContextQueries::GetOwningContextId);
        m_address={context,m_targetComposition};
        if (!context.IsNull() && m_targetComposition.IsValid()) TerrainCompositionNotificationBus::Handler::BusConnect(m_address);
    }
    void TerrainMeshBlendComponent::DisconnectTarget()
    {
        if (m_registered) TerrainCompositionRequestBus::Event(m_address,&TerrainCompositionRequests::UnregisterMeshBlend,m_owner,m_session);
        TerrainCompositionNotificationBus::Handler::BusDisconnect();
        m_registered=false; m_session={};
    }
    void TerrainMeshBlendComponent::SetBlendDistance(float distance)
    {
        if (!std::isfinite(distance) || distance<0) return;
        m_blendDistance=distance; Refresh();
    }
    void TerrainMeshBlendComponent::QueueRefresh()
    {
        if (m_active && !AZ::SystemTickBus::Handler::BusIsConnected()) AZ::SystemTickBus::Handler::BusConnect();
    }
    void TerrainMeshBlendComponent::OnSystemTick()
    {
        // One deferred update per edit/transform batch, never a periodic scene monitor.
        AZ::SystemTickBus::Handler::BusDisconnect(); Refresh();
    }
    AZ::u32 TerrainMeshBlendComponent::OnConfigurationChanged()
    {
        if (m_active) { DisconnectTarget(); ConnectTarget(); Refresh(); }
        return AZ::Edit::PropertyRefreshLevels::ValuesOnly;
    }
    void TerrainMeshBlendComponent::Refresh()
    {
        if (!m_active || m_refreshing) return;
        m_refreshing=true;
        AZStd::vector<AZ::EntityId> owners;
        AZ::TransformBus::EventResult(owners,m_owner,&AZ::TransformInterface::GetAllDescendants);
        owners.push_back(m_owner);
        AZ::Aabb bounds=AZ::Aabb::CreateNull();
        for (const auto id:owners)
        {
            AZ::Aabb child=AZ::Aabb::CreateNull();
            AzFramework::BoundsRequestBus::EventResult(child,id,&AzFramework::BoundsRequests::GetWorldBounds);
            if (child.IsValid()) bounds.AddAabb(child);
        }
        TerrainCompositionRequestBus::EventResult(m_session,m_address,&TerrainCompositionRequests::GetCompositionSession);
        m_registered=false;
        if (!m_session.IsNull() && std::isfinite(m_blendDistance) && m_blendDistance>=0)
        {
            TerrainMeshBlendRegistrationData registration{m_owner,m_address.first,m_session,m_blendDistance,bounds};
            TerrainCompositionRequestBus::EventResult(m_registered,m_address,&TerrainCompositionRequests::RegisterMeshBlend,registration);
        }
        m_appliedCount=0;
        for (const auto id:owners)
        {
            if (AZStd::find(m_subscriptions.begin(),m_subscriptions.end(),id)==m_subscriptions.end())
            {
                AZ::Render::MaterialComponentNotificationBus::MultiHandler::BusConnect(id); m_subscriptions.push_back(id);
            }
            AZ::Render::MaterialAssignmentMap materials;
            AZ::Render::MaterialComponentRequestBus::EventResult(materials,id,&AZ::Render::MaterialComponentRequests::GetMaterialMapCopy);
            bool uniqueRequired=false;
            for (auto& entry:materials)
            {
                auto& assignment=entry.second;
                if (assignment.m_materialInstance && assignment.m_materialInstance->FindPropertyIndex(AZ::Name(DistanceProperty)).IsValid() && !assignment.m_materialInstanceMustBeUnique)
                {
                    assignment.m_materialInstanceMustBeUnique=true; uniqueRequired=true;
                }
            }
            if (uniqueRequired)
            {
                AZ::Render::MaterialComponentRequestBus::Event(id,&AZ::Render::MaterialComponentRequests::SetMaterialMap,materials);
                QueueRefresh(); continue;
            }
            for (const auto& [slot,assignment]:materials)
            {
                if (!assignment.m_materialInstance || !assignment.m_materialInstance->FindPropertyIndex(AZ::Name(DistanceProperty)).IsValid()) continue;
                const auto index=assignment.m_materialInstance->FindPropertyIndex(AZ::Name(DistanceProperty));
                const float previous=assignment.m_materialInstance->GetPropertyValue<float>(index);
                if (AZStd::find_if(m_previous.begin(),m_previous.end(),[&](const auto& p){ return p.m_entity==id && p.m_slot==slot; })==m_previous.end())
                    m_previous.push_back({id,slot,previous});
                const float distance=m_registered?m_blendDistance:0.f;
                // Runtime shader constants are owned here, not serialized into
                // the child's Material overrides. The prefab root is the sole knob.
                if (previous!=distance)
                {
                    assignment.m_materialInstance->SetPropertyValue(index,distance);
                    assignment.m_materialInstance->Compile();
                }
                m_lastAppliedDistance=assignment.m_materialInstance->GetPropertyValue<float>(index);
                ++m_appliedCount;
            }
        }
        m_refreshing=false;
    }
    void TerrainMeshBlendComponent::RestoreOverrides()
    {
        for (const auto& previous:m_previous)
        {
            AZ::Render::MaterialAssignmentMap materials;
            AZ::Render::MaterialComponentRequestBus::EventResult(materials,previous.m_entity,&AZ::Render::MaterialComponentRequests::GetMaterialMapCopy);
            const auto found=materials.find(previous.m_slot);
            if (found==materials.end() || !found->second.m_materialInstance) continue;
            const auto material=found->second.m_materialInstance;
            const auto index=material->FindPropertyIndex(AZ::Name(DistanceProperty));
            if (index.IsValid()) { material->SetPropertyValue(index,previous.m_value); material->Compile(); }
        }
        m_previous.clear(); m_appliedCount=0; m_lastAppliedDistance=-1.f;
    }
    AZStd::string TerrainMeshBlendComponent::GetStatus() const
    {
        if (!m_targetComposition.IsValid()) return "Select a Target Compositor; blending is disabled.";
        if (!m_registered) return "Waiting for the target compositor in this entity context; blending is disabled.";
        if (!m_appliedCount) return "Registered; waiting for terrain-blend materials on this prefab or its descendants.";
        return AZStd::string::format("Registered with compositor; controlling %u material assignments.",m_appliedCount);
    }
}
