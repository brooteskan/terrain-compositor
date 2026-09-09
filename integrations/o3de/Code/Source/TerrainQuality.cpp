#include <TerrainCompositor/TerrainQuality.h>

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/containers/vector.h>
#include <AzFramework/Terrain/TerrainDataRequestBus.h>
#include <Atom/RPI.Public/Scene.h>
#include <Components/TerrainWorldRendererComponent.h>
#include <TerrainRenderer/TerrainFeatureProcessor.h>

#include <cmath>

namespace TerrainCompositor
{
    namespace
    {
        bool SameConfiguration(const TerrainQualityConfig& left, const TerrainQualityConfig& right)
        {
            return left.m_overrideTerrainQuality == right.m_overrideTerrainQuality &&
                left.m_heightQueryResolution == right.m_heightQueryResolution &&
                left.m_renderer.m_overrideMeshSettings == right.m_renderer.m_overrideMeshSettings &&
                left.m_renderer.m_renderDistance == right.m_renderer.m_renderDistance &&
                left.m_renderer.m_firstLodDistance == right.m_renderer.m_firstLodDistance &&
                left.m_renderer.m_clodEnabled == right.m_renderer.m_clodEnabled &&
                left.m_renderer.m_clodDistance == right.m_renderer.m_clodDistance;
        }

        Terrain::TerrainFeatureProcessor* GetTerrainFeatureProcessor(AzFramework::EntityContextId contextId)
        {
            if (AZ::RPI::Scene* scene = AZ::RPI::Scene::GetSceneForEntityContextId(contextId))
            {
                return scene->GetFeatureProcessor<Terrain::TerrainFeatureProcessor>();
            }
            return nullptr;
        }
    } // namespace

    class TerrainQualityRegistry final
    {
    public:
        static TerrainQualityRegistry& Get()
        {
            static TerrainQualityRegistry registry;
            return registry;
        }

        void Register(TerrainQualityController& controller)
        {
            if (AZStd::find(m_controllers.begin(), m_controllers.end(), &controller) == m_controllers.end())
            {
                m_controllers.push_back(&controller);
            }
            Reevaluate();
        }

        void Unregister(TerrainQualityController& controller)
        {
            if (m_appliedOwner == &controller)
            {
                RestoreAppliedOwner();
            }
            AZStd::erase(m_controllers, &controller);
            controller.SetStatus(TerrainQualityStatus::Disabled);
            controller.m_applyIssued = false;
            Reevaluate();
        }

        void ConfigurationChanged(TerrainQualityController& controller)
        {
            controller.m_applyIssued = false;
            Reevaluate();
        }

        void Reevaluate()
        {
            AZStd::vector<TerrainQualityController*> validClaims;
            for (TerrainQualityController* controller : m_controllers)
            {
                if (!controller->m_active || !controller->m_configuration.m_overrideTerrainQuality)
                {
                    controller->SetStatus(TerrainQualityStatus::Disabled);
                    continue;
                }
                if (!controller->Validate())
                {
                    controller->SetStatus(TerrainQualityStatus::Invalid,
                        "Height resolution and renderer distances must be finite and inside the inspector limits.");
                    continue;
                }
                validClaims.push_back(controller);
            }

            if (m_restorePending)
            {
                float activeResolution = 0.0f;
                if (!AzFramework::Terrain::TerrainDataRequestBus::HasHandlers())
                {
                    for (TerrainQualityController* controller : validClaims)
                    {
                        controller->SetStatus(TerrainQualityStatus::Pending, "Waiting for the terrain service to restore the stock profile.");
                    }
                    return;
                }
                AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                    activeResolution, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
                if (std::abs(activeResolution - m_baseline.m_heightQueryResolution) > 1.0e-5f)
                {
                    for (TerrainQualityController* controller : validClaims)
                    {
                        controller->SetStatus(TerrainQualityStatus::Pending, "Waiting for the stock terrain profile to become active.");
                    }
                    return;
                }
                m_restorePending = false;
                m_hasBaseline = false;
            }

            if (validClaims.size() != 1)
            {
                if (m_appliedOwner)
                {
                    RestoreAppliedOwner();
                }
                if (validClaims.size() > 1)
                {
                    AZStd::string claimants;
                    for (TerrainQualityController* controller : validClaims)
                    {
                        if (!claimants.empty())
                        {
                            claimants += ", ";
                        }
                        claimants += AZStd::string::format("%s (context %s)",
                            controller->m_ownerEntityId.ToString().c_str(),
                            controller->m_contextId.ToString<AZStd::string>().c_str());
                    }
                    bool newConflict = false;
                    for (TerrainQualityController* controller : validClaims)
                    {
                        newConflict = newConflict || controller->m_status != TerrainQualityStatus::Conflict;
                        controller->SetStatus(TerrainQualityStatus::Conflict,
                            "Multiple active compositors request the process-global terrain quality profile.");
                    }
                    AZ_Warning("TerrainQuality", !newConflict,
                        "Terrain quality override conflict; all overrides are suppressed. Claimants: %s",
                        claimants.c_str());
                }
                if (validClaims.empty())
                {
                    // Retain the baseline until a later claim observes that the asynchronous restoration completed.
                }
                return;
            }

            TerrainQualityController* candidate = validClaims.front();
            if (m_appliedOwner && m_appliedOwner != candidate)
            {
                RestoreAppliedOwner();
                candidate->SetStatus(TerrainQualityStatus::Pending, "Waiting for the previous terrain profile to be restored.");
                return;
            }
            if (m_hasBaseline && m_baselineContextId != candidate->m_contextId)
            {
                m_hasBaseline = false;
            }
            if (!m_hasBaseline)
            {
                TerrainQualityBaseline baseline;
                if (!candidate->CaptureBaseline(baseline))
                {
                    candidate->SetStatus(TerrainQualityStatus::Pending,
                        "Waiting for Terrain World and Terrain World Renderer activation.");
                    return;
                }
                m_baseline = baseline;
                m_baselineContextId = candidate->m_contextId;
                m_hasBaseline = true;
            }
            else if (candidate->m_configuration.m_renderer.m_overrideMeshSettings && !m_baseline.m_meshSettingsAvailable)
            {
                // A height-only owner can enable renderer overrides later. Extend the saved baseline with
                // the still-stock mesh settings while retaining the original height resolution.
                TerrainQualityBaseline augmentedBaseline = m_baseline;
                const float originalHeightResolution = m_baseline.m_heightQueryResolution;
                if (!candidate->CaptureBaseline(augmentedBaseline))
                {
                    candidate->SetStatus(TerrainQualityStatus::Pending,
                        "Waiting for Terrain World Renderer activation before capturing its stock mesh profile.");
                    return;
                }
                augmentedBaseline.m_heightQueryResolution = originalHeightResolution;
                m_baseline = augmentedBaseline;
            }
            m_appliedOwner = candidate;
            if (!candidate->Apply(m_baseline))
            {
                candidate->SetStatus(TerrainQualityStatus::Pending,
                    "Waiting for the terrain service, renderer feature processor, or height-setting readback.");
            }
        }

    private:
        void RestoreAppliedOwner()
        {
            TerrainQualityController* owner = m_appliedOwner;
            m_appliedOwner = nullptr;
            if (owner && m_hasBaseline)
            {
                const bool terrainServiceAvailable = AzFramework::Terrain::TerrainDataRequestBus::HasHandlers();
                owner->Restore(m_baseline);
                owner->m_applyIssued = false;
                m_restorePending = terrainServiceAvailable;
                if (!terrainServiceAvailable)
                {
                    // Terrain World has already gone away, so its next activation owns a fresh stock baseline.
                    m_hasBaseline = false;
                }
            }
        }

        AZStd::vector<TerrainQualityController*> m_controllers;
        TerrainQualityController* m_appliedOwner = nullptr;
        TerrainQualityBaseline m_baseline;
        AzFramework::EntityContextId m_baselineContextId{};
        bool m_hasBaseline = false;
        bool m_restorePending = false;
    };

    void TerrainRendererMeshQualityConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<TerrainRendererMeshQualityConfig>()))
            {
                return;
            }
            serialize->Class<TerrainRendererMeshQualityConfig>()
                ->Version(1)
                ->Field("OverrideMeshSettings", &TerrainRendererMeshQualityConfig::m_overrideMeshSettings)
                ->Field("RenderDistance", &TerrainRendererMeshQualityConfig::m_renderDistance)
                ->Field("FirstLodDistance", &TerrainRendererMeshQualityConfig::m_firstLodDistance)
                ->Field("ClodEnabled", &TerrainRendererMeshQualityConfig::m_clodEnabled)
                ->Field("ClodDistance", &TerrainRendererMeshQualityConfig::m_clodDistance);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainRendererMeshQualityConfig>("Renderer Mesh Settings", "Optional Terrain World Renderer mesh profile.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, false)
                    ->GroupElementToggle("Override Renderer Mesh Settings", &TerrainRendererMeshQualityConfig::m_overrideMeshSettings)
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::AttributesAndValues)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &TerrainRendererMeshQualityConfig::m_renderDistance,
                        "Mesh Render Distance", "Distance from the camera at which terrain meshes stop rendering.")
                    ->Attribute(AZ::Edit::Attributes::Min, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 100000.0f)
                    ->Attribute(AZ::Edit::Attributes::SoftMin, 100.0f)
                    ->Attribute(AZ::Edit::Attributes::SoftMax, 10000.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &TerrainRendererMeshQualityConfig::m_firstLodDistance,
                        "First LOD Distance", "Distance covered by the closest terrain LOD.")
                    ->Attribute(AZ::Edit::Attributes::Min, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 10000.0f)
                    ->Attribute(AZ::Edit::Attributes::SoftMin, 10.0f)
                    ->Attribute(AZ::Edit::Attributes::SoftMax, 1000.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::CheckBox, &TerrainRendererMeshQualityConfig::m_clodEnabled,
                        "CLOD Enabled", "Smoothly blend between terrain mesh levels of detail.")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &TerrainRendererMeshQualityConfig::m_clodDistance,
                        "CLOD Distance", "Distance over which the closest LOD blends into the next LOD.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1000.0f)
                    ->Attribute(AZ::Edit::Attributes::SoftMax, 100.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m");
            }
        }
    }

    void TerrainQualityConfig::Reflect(AZ::ReflectContext* context)
    {
        TerrainRendererMeshQualityConfig::Reflect(context);
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            if (!serialize->IsRemovingReflection() && serialize->FindClassData(azrtti_typeid<TerrainQualityConfig>()))
            {
                return;
            }
            serialize->Class<TerrainQualityConfig>()
                ->Version(1)
                ->Field("OverrideTerrainQuality", &TerrainQualityConfig::m_overrideTerrainQuality)
                ->Field("HeightQueryResolution", &TerrainQualityConfig::m_heightQueryResolution)
                ->Field("Renderer", &TerrainQualityConfig::m_renderer);

            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<TerrainQualityConfig>("Terrain Quality", "Optional owner of the O3DE terrain grid and renderer mesh profile.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, false)
                    ->GroupElementToggle("Override Terrain Quality", &TerrainQualityConfig::m_overrideTerrainQuality)
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, AZ::Edit::PropertyRefreshLevels::AttributesAndValues)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainQualityConfig::m_heightQueryResolution,
                        "Height Query Resolution", "World-space spacing of the O3DE terrain height grid.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.1f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &TerrainQualityConfig::m_renderer,
                        "Renderer", "Optional renderer mesh settings for the owning entity context.");
            }
        }
    }

    TerrainQualityController::~TerrainQualityController()
    {
        Deactivate();
    }

    void TerrainQualityController::Activate(AzFramework::EntityContextId contextId, AZ::EntityId ownerEntityId,
        const TerrainQualityBaseline* baseline)
    {
        Deactivate();
        m_contextId = contextId;
        m_ownerEntityId = ownerEntityId;
        m_active = true;
        if (baseline)
        {
            m_preferredBaseline = *baseline;
            m_hasPreferredBaseline = true;
        }
        TerrainQualityRegistry::Get().Register(*this);
    }

    void TerrainQualityController::Deactivate()
    {
        if (!m_active)
        {
            return;
        }
        m_active = false;
        TerrainQualityRegistry::Get().Unregister(*this);
        m_contextId = {};
        m_ownerEntityId.SetInvalid();
        m_hasPreferredBaseline = false;
        m_statusDetail.clear();
    }

    void TerrainQualityController::Update(const TerrainQualityConfig& configuration)
    {
        if (SameConfiguration(m_configuration, configuration))
        {
            return;
        }
        m_configuration = configuration;
        if (m_active)
        {
            TerrainQualityRegistry::Get().ConfigurationChanged(*this);
        }
    }

    void TerrainQualityController::Tick()
    {
        if (m_active && m_status == TerrainQualityStatus::Pending)
        {
            TerrainQualityRegistry::Get().Reevaluate();
        }
    }

    bool TerrainQualityController::Validate() const
    {
        const auto& mesh = m_configuration.m_renderer;
        return std::isfinite(m_configuration.m_heightQueryResolution) && m_configuration.m_heightQueryResolution >= 0.1f &&
            (!mesh.m_overrideMeshSettings ||
                (std::isfinite(mesh.m_renderDistance) && mesh.m_renderDistance >= 1.0f && mesh.m_renderDistance <= 100000.0f &&
                 std::isfinite(mesh.m_firstLodDistance) && mesh.m_firstLodDistance >= 1.0f && mesh.m_firstLodDistance <= 10000.0f &&
                 std::isfinite(mesh.m_clodDistance) && mesh.m_clodDistance >= 0.0f && mesh.m_clodDistance <= 1000.0f));
    }

    bool TerrainQualityController::CaptureBaseline(TerrainQualityBaseline& baseline) const
    {
        if (!AzFramework::Terrain::TerrainDataRequestBus::HasHandlers())
        {
            return false;
        }
        if (m_hasPreferredBaseline)
        {
            baseline = m_preferredBaseline;
        }
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            baseline.m_heightQueryResolution,
            &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        if (!std::isfinite(baseline.m_heightQueryResolution) || baseline.m_heightQueryResolution <= 0.0f)
        {
            return false;
        }
        if (!m_configuration.m_renderer.m_overrideMeshSettings)
        {
            return true;
        }
        if (!baseline.m_meshSettingsAvailable)
        {
            AZ::ComponentApplicationBus::Broadcast(
                &AZ::ComponentApplicationRequests::EnumerateEntities,
                [this, &baseline](AZ::Entity* entity)
                {
                    if (!entity || baseline.m_meshSettingsAvailable)
                    {
                        return;
                    }
                    AzFramework::EntityContextId contextId{};
                    AzFramework::EntityIdContextQueryBus::EventResult(
                        contextId, entity->GetId(), &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
                    if (contextId != m_contextId)
                    {
                        return;
                    }
                    for (AZ::Component* component : entity->GetComponents())
                    {
                        Terrain::TerrainWorldRendererConfig rendererConfig;
                        auto* renderer = azrtti_cast<Terrain::TerrainWorldRendererComponent*>(component);
                        if (renderer && renderer->WriteOutConfig(&rendererConfig))
                        {
                            baseline.m_renderDistance = rendererConfig.m_meshConfig.m_renderDistance;
                            baseline.m_firstLodDistance = rendererConfig.m_meshConfig.m_firstLodDistance;
                            baseline.m_clodEnabled = rendererConfig.m_meshConfig.m_clodEnabled;
                            baseline.m_clodDistance = rendererConfig.m_meshConfig.m_clodDistance;
                            baseline.m_meshSettingsAvailable = true;
                            return;
                        }
                    }
                });
        }
        return baseline.m_meshSettingsAvailable && GetTerrainFeatureProcessor(m_contextId);
    }

    bool TerrainQualityController::Apply(const TerrainQualityBaseline& baseline)
    {
        if (!AzFramework::Terrain::TerrainDataRequestBus::HasHandlers())
        {
            return false;
        }
        if ((m_configuration.m_renderer.m_overrideMeshSettings || m_meshOverrideWasApplied) &&
            !GetTerrainFeatureProcessor(m_contextId))
        {
            return false;
        }
        if (!m_applyIssued)
        {
            float currentResolution = 0.0f;
            AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                currentResolution, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
            AzFramework::Terrain::TerrainDataRequestBus::Broadcast(
                &AzFramework::Terrain::TerrainDataRequests::SetTerrainHeightQueryResolution,
                m_configuration.m_heightQueryResolution);
            m_heightSettingsChanged = m_heightSettingsChanged || currentResolution != m_configuration.m_heightQueryResolution;

            if (m_configuration.m_renderer.m_overrideMeshSettings)
            {
                Terrain::MeshConfiguration mesh;
                mesh.m_renderDistance = m_configuration.m_renderer.m_renderDistance;
                mesh.m_firstLodDistance = m_configuration.m_renderer.m_firstLodDistance;
                mesh.m_clodEnabled = m_configuration.m_renderer.m_clodEnabled;
                mesh.m_clodDistance = m_configuration.m_renderer.m_clodDistance;
                GetTerrainFeatureProcessor(m_contextId)->SetMeshConfiguration(mesh);
                m_meshOverrideWasApplied = true;
            }
            else if (m_meshOverrideWasApplied && baseline.m_meshSettingsAvailable)
            {
                Terrain::MeshConfiguration mesh;
                mesh.m_renderDistance = baseline.m_renderDistance;
                mesh.m_firstLodDistance = baseline.m_firstLodDistance;
                mesh.m_clodEnabled = baseline.m_clodEnabled;
                mesh.m_clodDistance = baseline.m_clodDistance;
                GetTerrainFeatureProcessor(m_contextId)->SetMeshConfiguration(mesh);
                m_meshOverrideWasApplied = false;
            }
            m_applyIssued = true;
        }

        float activeResolution = 0.0f;
        AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
            activeResolution, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
        if (std::abs(activeResolution - m_configuration.m_heightQueryResolution) > 1.0e-5f)
        {
            return false;
        }
        SetStatus(TerrainQualityStatus::Applied);
        return true;
    }

    void TerrainQualityController::Restore(const TerrainQualityBaseline& baseline)
    {
        if (AzFramework::Terrain::TerrainDataRequestBus::HasHandlers())
        {
            float currentResolution = 0.0f;
            AzFramework::Terrain::TerrainDataRequestBus::BroadcastResult(
                currentResolution, &AzFramework::Terrain::TerrainDataRequests::GetTerrainHeightQueryResolution);
            AzFramework::Terrain::TerrainDataRequestBus::Broadcast(
                &AzFramework::Terrain::TerrainDataRequests::SetTerrainHeightQueryResolution,
                baseline.m_heightQueryResolution);
            m_heightSettingsChanged = m_heightSettingsChanged || currentResolution != baseline.m_heightQueryResolution;
        }
        if (baseline.m_meshSettingsAvailable)
        {
            if (Terrain::TerrainFeatureProcessor* featureProcessor = GetTerrainFeatureProcessor(m_contextId))
            {
                Terrain::MeshConfiguration mesh;
                mesh.m_renderDistance = baseline.m_renderDistance;
                mesh.m_firstLodDistance = baseline.m_firstLodDistance;
                mesh.m_clodEnabled = baseline.m_clodEnabled;
                mesh.m_clodDistance = baseline.m_clodDistance;
                featureProcessor->SetMeshConfiguration(mesh);
            }
        }
        m_meshOverrideWasApplied = false;
    }

    void TerrainQualityController::SetStatus(TerrainQualityStatus status, AZStd::string message)
    {
        m_status = status;
        m_statusDetail = AZStd::move(message);
    }

    AZStd::string TerrainQualityController::GetStatusMessage() const
    {
        if (!m_statusDetail.empty())
        {
            return m_statusDetail;
        }
        switch (m_status)
        {
        case TerrainQualityStatus::Disabled: return "Terrain quality override disabled.";
        case TerrainQualityStatus::Pending: return "Terrain quality override pending activation.";
        case TerrainQualityStatus::Applied: return "Terrain quality override applied.";
        case TerrainQualityStatus::Invalid: return "Terrain quality override is invalid.";
        case TerrainQualityStatus::Conflict: return "Terrain quality override conflicts with another compositor.";
        }
        return "Terrain quality state unavailable.";
    }

    bool TerrainQualityController::ConsumeHeightSettingsChanged()
    {
        const bool changed = m_heightSettingsChanged;
        m_heightSettingsChanged = false;
        return changed;
    }
} // namespace TerrainCompositor
