#include <TerrainCompositor/Components/ProceduralGroundGradientComponent.h>
#include "../ComponentConfiguration.h"

#include <Atom/RPI.Public/RPISystemInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/containers/array.h>
#include <AzFramework/Entity/EntityContextBus.h>
#include <AzFramework/Scene/SceneSystemInterface.h>
#include <LmbrCentral/Dependency/DependencyNotificationBus.h>
#include <TerrainRenderer/TerrainFeatureProcessor.h>

#include <cmath>
#include <AzCore/Console/Console.h>

#include <TerrainCompositor/TerrainCompositorTypeIds.h>

namespace TerrainCompositor
{
    AZ_CVAR(bool, r_terrainRetainedKernel, true, nullptr, AZ::ConsoleFunctorFlags::Null,
        "Skip provably zero hill contributions in retained procedural snapshots. Disable for kernel comparisons.");
    AZ_CVAR(bool, r_terrainCacheHillCells, true, nullptr, AZ::ConsoleFunctorFlags::Null,
        "Cache hill cells and reject distant hills. Disable to compare against the previous zero-profile-pruned kernel.");
    namespace
    {
        bool HoleSamplerEqual(const GradientSignal::GradientSampler& a, const GradientSignal::GradientSampler& b)
        {
            return a.m_gradientId == b.m_gradientId && a.m_opacity == b.m_opacity &&
                a.m_invertInput == b.m_invertInput && a.m_enableTransform == b.m_enableTransform &&
                a.m_translate == b.m_translate && a.m_scale == b.m_scale && a.m_rotate == b.m_rotate &&
                a.m_enableLevels == b.m_enableLevels && a.m_inputMid == b.m_inputMid &&
                a.m_inputMin == b.m_inputMin && a.m_inputMax == b.m_inputMax &&
                a.m_outputMin == b.m_outputMin && a.m_outputMax == b.m_outputMax;
        }
    } // namespace

    AZ_COMPONENT_IMPL(
        ProceduralGroundGradientComponent,
        "ProceduralGroundGradientComponent",
        ProceduralGroundGradientComponentTypeId,
        AzFramework::EditorEntityEvents);

    void ProceduralGroundGradientConfig::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ProceduralGroundGradientConfig, AZ::ComponentConfig>()
                ->Version(3)
                ->Field("HillDensity", &ProceduralGroundGradientConfig::m_hillDensity)
                ->Field("AmplitudeMeters", &ProceduralGroundGradientConfig::m_amplitudeMeters)
                ->Field("Frequency", &ProceduralGroundGradientConfig::m_frequency)
                ->Field("KernelPolicy", &ProceduralGroundGradientConfig::m_kernelPolicy)
                ->Field("NoiseTintStrength", &ProceduralGroundGradientConfig::m_noiseTintStrength)
                ->Field("HoleMask", &ProceduralGroundGradientConfig::m_holeMask)
                ->Field("HoleThreshold", &ProceduralGroundGradientConfig::m_holeThreshold);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ProceduralGroundGradientConfig>(
                    "Procedural Ground Gradient Configuration",
                    "Controls the generated hill field and terrain-wide procedural tint.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Visibility, AZ::Edit::PropertyVisibility::ShowChildrenOnly)
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Slider,
                        &ProceduralGroundGradientConfig::m_hillDensity,
                        "Hill Density",
                        "Number of hill centers per meter along each terrain axis. For example, 0.005 produces roughly one hill every 200 meters.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.001f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " hills/m")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &ProceduralGroundGradientConfig::m_amplitudeMeters,
                        "Amplitude",
                        "Maximum bump height or depression depth from Z=0, in meters.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1024.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Suffix, " m")
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &ProceduralGroundGradientConfig::m_frequency,
                        "Frequency",
                        "How quickly each feature rises or falls. Low values create broad gentle slopes; high values create tight bumps and depressions without changing their count.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.1f)
                    ->Attribute(AZ::Edit::Attributes::Max, 16.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.1f)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Slider,
                        &ProceduralGroundGradientConfig::m_noiseTintStrength,
                        "Noise Tint Strength",
                        "Strength of the legacy terrain tint. Inactive while a Terrain Tint Material is selected; does not change terrain height.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.01f)
                    ->DataElement(nullptr, &ProceduralGroundGradientConfig::m_holeMask,
                        "Terrain Hole Mask", "Optional gradient; values at or above the threshold remove terrain.")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &ProceduralGroundGradientConfig::m_holeThreshold,
                        "Hole Threshold", "Mask values greater than or equal to this value remove terrain.")
                    ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                    ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->Attribute(AZ::Edit::Attributes::Step, 0.01f);
            }
        }
    }

    void ProceduralGroundGradientComponent::Reflect(AZ::ReflectContext* context)
    {
        ProceduralGroundGradientConfig::Reflect(context);

        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<ProceduralGroundGradientComponent, AZ::Component>()
                ->Version(1)
                ->Field("Configuration", &ProceduralGroundGradientComponent::m_configuration);

            if (AZ::EditContext* editContext = serializeContext->GetEditContext())
            {
                editContext->Class<ProceduralGroundGradientComponent>(
                    "Procedural Ground Gradient",
                    "Provides the unbounded procedural base height used by TerrainCompositor terrain composition.")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Gradients")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(
                        AZ::Edit::UIHandlers::Default,
                        &ProceduralGroundGradientComponent::m_configuration,
                        "Configuration",
                        "Procedural hill field settings.")
                    ->Attribute(AZ::Edit::Attributes::ChangeNotify, &ProceduralGroundGradientComponent::OnConfigurationChanged);
            }
        }
    }

    ProceduralGroundGradientComponent::ProceduralGroundGradientComponent(
        const ProceduralGroundGradientConfig& configuration)
        : m_configuration(configuration)
        , m_queryConfiguration(configuration)
    {
    }

    void ProceduralGroundGradientComponent::GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
    {
        provided.push_back(AZ_CRC_CE("GradientService"));
    }

    ProceduralGroundGradientComponent::~ProceduralGroundGradientComponent()
    {
        // Also retire snapshots if an editor-owned provider is destroyed without
        // an explicit deactivation. Disconnect shared dispatch before members die.
        StopGradient();
    }

    void ProceduralGroundGradientComponent::GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
    {
        incompatible.push_back(AZ_CRC_CE("GradientService"));
        incompatible.push_back(AZ_CRC_CE("GradientTransformService"));
    }

    void ProceduralGroundGradientComponent::GetRequiredServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& required)
    {
    }

    void ProceduralGroundGradientComponent::GetDependentServices(
        [[maybe_unused]] AZ::ComponentDescriptor::DependencyArrayType& dependent)
    {
    }

    void ProceduralGroundGradientComponent::Activate()
    {
        StartGradient(GetEntityId());
    }

    void ProceduralGroundGradientComponent::Deactivate()
    {
        StopGradient();
    }

    void ProceduralGroundGradientComponent::EditorActivate(AZ::EntityId entityId)
    {
        // GenericComponentWrapper does not call the runtime Activate method in edit mode.
        StartGradient(entityId);
    }

    void ProceduralGroundGradientComponent::EditorDeactivate([[maybe_unused]] AZ::EntityId entityId)
    {
        StopGradient();
    }

    void ProceduralGroundGradientComponent::StartGradient(AZ::EntityId entityId)
    {
        // Publish before exposing the handler, including when a serialized component is reactivated.
        m_activeEntityId = entityId;
        {
            AZStd::unique_lock lock(m_queryMutex);
            if (m_snapshotDependency) m_snapshotDependency->Retire();
            m_snapshotDependency = std::make_shared<TerrainPreparationDependency>();
            m_snapshotSession = AZ::Uuid::CreateRandom();
            m_snapshotEntityId = entityId;
        }
        OnConfigurationChanged();
        GradientSignal::GradientRequestBus::Handler::BusConnect(entityId);
        TerrainExistenceSourceRequestBus::Handler::BusConnect(entityId);
        TerrainProceduralSnapshotRequestBus::Handler::BusConnect(entityId);
        m_materialUpdateState = std::make_shared<MaterialUpdateState>();
        m_materialUpdateState->m_owner.store(this, std::memory_order_release);
        BindTerrainMaterial();
        if (entityId.IsValid())
            LmbrCentral::DependencyNotificationBus::Event(
                entityId, &LmbrCentral::DependencyNotificationBus::Events::OnCompositionChanged);
    }

    void ProceduralGroundGradientComponent::StopGradient()
    {
        {
            AZStd::unique_lock lock(m_queryMutex);
            if (m_snapshotDependency) m_snapshotDependency->Retire();
            m_snapshotDependency.reset();
        }
        TerrainProceduralSnapshotRequestBus::Handler::BusDisconnect();
        // The shared-dispatch bus waits for in-flight height queries before disconnecting.
        TerrainExistenceSourceRequestBus::Handler::BusDisconnect();
        GradientSignal::GradientRequestBus::Handler::BusDisconnect();
        m_holeDependencyMonitor.Reset();
        StopNoiseTintUpdates();
        const AZ::EntityId entityId = m_activeEntityId;
        m_activeEntityId.SetInvalid();
        if (entityId.IsValid())
            LmbrCentral::DependencyNotificationBus::Event(
                entityId, &LmbrCentral::DependencyNotificationBus::Events::OnCompositionChanged);
    }

    AZ::u32 ProceduralGroundGradientComponent::OnConfigurationChanged()
    {
        bool heightsChanged;
        m_configuration.m_holeMask.m_ownerEntityId = m_activeEntityId;
        {
            AZStd::unique_lock lock(m_queryMutex);
            heightsChanged = m_queryConfiguration.m_hillDensity != m_configuration.m_hillDensity ||
                m_queryConfiguration.m_amplitudeMeters != m_configuration.m_amplitudeMeters ||
                m_queryConfiguration.m_frequency != m_configuration.m_frequency ||
                m_queryConfiguration.m_kernelPolicy != m_configuration.m_kernelPolicy ||
                m_queryConfiguration.m_holeThreshold != m_configuration.m_holeThreshold ||
                !HoleSamplerEqual(m_queryConfiguration.m_holeMask, m_configuration.m_holeMask);
            if (heightsChanged && m_snapshotDependency) m_snapshotDependency->Invalidate();
            m_queryConfiguration = m_configuration;
            if (heightsChanged || !m_queryKernel)
                m_queryKernel = std::make_shared<const ProceduralHillKernel>(m_configuration.m_hillDensity,
                    m_configuration.m_amplitudeMeters, m_configuration.m_frequency, m_configuration.m_kernelPolicy);
        }
        m_holeDependencyMonitor.Reset();
        if (m_activeEntityId.IsValid())
        {
            m_holeDependencyMonitor.ConnectOwner(m_activeEntityId);
            if (m_configuration.m_holeMask.m_gradientId.IsValid())
            {
                m_holeDependencyMonitor.ConnectDependency(m_configuration.m_holeMask.m_gradientId);
            }
        }
        // Notify outside the lock: dependents may immediately sample the newly published configuration.
        if (heightsChanged && m_activeEntityId.IsValid())
        {
            LmbrCentral::DependencyNotificationBus::Event(
                m_activeEntityId, &LmbrCentral::DependencyNotificationBus::Events::OnCompositionChanged);
        }
        if (m_terrainMaterial) ApplyNoiseTint(m_terrainMaterial);
        return AZ::Edit::PropertyRefreshLevels::None;
    }

    ProceduralGroundGradientConfig ProceduralGroundGradientComponent::GetQueryConfiguration() const
    {
        AZStd::shared_lock lock(m_queryMutex);
        return m_queryConfiguration;
    }

    std::shared_ptr<const ProceduralHillKernel> ProceduralGroundGradientComponent::GetQueryKernel() const
    {
        AZStd::shared_lock lock(m_queryMutex);
        return m_queryKernel;
    }

    TerrainProceduralSnapshotPtr ProceduralGroundGradientComponent::AcquireTerrainSnapshot() const
    {
        AZStd::shared_lock lock(m_queryMutex);
        if (!m_snapshotDependency) return {};
        const auto configuration = m_queryConfiguration;
        auto snapshot = std::make_shared<TerrainProceduralSnapshot>();
        snapshot->m_kernel = TerrainProceduralSnapshot::Kernel::ProceduralGround;
        snapshot->m_zeroProfilePruning = r_terrainRetainedKernel;
        snapshot->m_entityId = m_snapshotEntityId;
        snapshot->m_session = m_snapshotSession;
        snapshot->m_ticket = { m_snapshotDependency, m_snapshotDependency->Capture() };
        TerrainRenderChannelCapability supported;
        supported.m_source = TerrainRenderSource::RetainedAvailable;
        supported.m_sourceEntityId = m_snapshotEntityId;
        supported.m_inputZ = TerrainRenderInputZ::Independent;
        supported.m_requiresOrdinaryResult = false;
        supported.m_sampling.m_declared = true;
        supported.m_sampling.m_explicitPositions = supported.m_sampling.m_regularGrid = true;
        supported.m_sampling.m_exact = supported.m_sampling.m_clamp = supported.m_sampling.m_bilinear = true;
        // Keeps density-scaled cell coordinates and their +/-1 neighbors well
        // inside int64. Finite world XY only; Z is never read by either kernel.
        supported.m_sampling.m_maxAbsXY = 1.0e12f;
        snapshot->m_heightResult = TerrainSourceAcquisition::InvalidConfiguration;
        if (std::isfinite(configuration.m_hillDensity) && std::isfinite(configuration.m_amplitudeMeters) &&
            std::isfinite(configuration.m_frequency))
        {
            snapshot->m_height = supported;
            snapshot->m_heightResult = TerrainSourceAcquisition::Acquired;
            // The legacy diagnostic switch only selects exact-equivalent work.
            // Approximation policy always comes from the invalidated source state.
            const auto kernel = !snapshot->m_zeroProfilePruning && configuration.m_kernelPolicy != ProceduralHillPolicy::IntegerPowers
                ? std::make_shared<const ProceduralHillKernel>(configuration.m_hillDensity,
                    configuration.m_amplitudeMeters, configuration.m_frequency, ProceduralHillPolicy::Reference)
                : (!r_terrainCacheHillCells && configuration.m_kernelPolicy == ProceduralHillPolicy::CachedExact
                    ? std::make_shared<const ProceduralHillKernel>(configuration.m_hillDensity,
                        configuration.m_amplitudeMeters, configuration.m_frequency, ProceduralHillPolicy::PrunedReference)
                    : m_queryKernel);
            snapshot->m_hillKernel = kernel;
            snapshot->m_heightBatch = [kernel](auto positions, auto values, auto* counters)
            {
                kernel->SampleBatch(positions, values, counters);
            };
            snapshot->m_heightValue = [kernel](const AZ::Vector3& position)
            {
                ProceduralHillKernel::Scratch scratch;
                return kernel->Sample(position.GetX(), position.GetY(), scratch);
            };
        }
        snapshot->m_existenceResult = TerrainSourceAcquisition::ExternalMask;
        if (!configuration.m_holeMask.m_gradientId.IsValid())
        {
            snapshot->m_existence = supported;
            snapshot->m_existenceResult = TerrainSourceAcquisition::Acquired;
            snapshot->m_constantExistence = true;
            snapshot->m_existenceValue = [](const AZ::Vector3&) { return true; };
        }
        return snapshot;
    }

    void ProceduralGroundGradientComponent::StopNoiseTintUpdates()
    {
        if (m_materialUpdateState)
        {
            m_materialUpdateState->m_owner.store(nullptr, std::memory_order_release);
            ++m_materialUpdateState->m_generation;
        }
        m_materialChangedHandler.Disconnect();
        m_materialChannel.reset();
        AzFramework::EntityContextEventBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        m_materialUpdateState.reset();
        m_terrainMaterial.reset();
        m_reportedMissingTintProperty = false;
    }

    void ProceduralGroundGradientComponent::BindTerrainMaterial()
    {
        AzFramework::EntityContextId context{};
        AzFramework::EntityIdContextQueryBus::EventResult(
            context, m_activeEntityId, &AzFramework::EntityIdContextQueryBus::Events::GetOwningContextId);
        BindTerrainMaterialForContext(context);
    }

    void ProceduralGroundGradientComponent::BindTerrainMaterialForContext(const AzFramework::EntityContextId& context)
    {
        const AZ::u64 generation = ++m_materialUpdateState->m_generation;
        m_materialChangedHandler.Disconnect();
        m_materialChannel.reset();
        AzFramework::EntityContextEventBus::Handler::BusDisconnect();
        AZ::SystemTickBus::Handler::BusDisconnect();
        if (!context.IsNull())
            AzFramework::EntityContextEventBus::Handler::BusConnect(context);
        const auto* rpiSystem = AZ::RPI::RPISystemInterface::Get();
        auto* registry = AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get();
        const auto* sceneSystem = AzFramework::SceneSystemInterface::Get();
        if (!rpiSystem || !rpiSystem->IsInitialized() || !registry || !sceneSystem)
        {
            m_materialRetriesRemaining = MaterialResolutionAttempts;
            AZ::SystemTickBus::Handler::BusConnect();
            return;
        }
        const auto* scene = AZ::RPI::Scene::GetSceneForEntityContextId(context);
        if (!scene)
        {
            m_materialRetriesRemaining = MaterialResolutionAttempts;
            AZ::SystemTickBus::Handler::BusConnect();
            return;
        }
        const auto channel = registry->AcquireSceneChannel(scene);
        m_materialChannel = channel;
        const std::weak_ptr<MaterialUpdateState> weak = m_materialUpdateState;
        m_materialChangedHandler = TerrainMeshCutoutRenderChannel::MaterialChangedEvent::Handler(
            [weak, generation](AZ::Data::Instance<AZ::RPI::Material> material)
            {
                AZ::SystemTickBus::QueueFunction([weak, generation, material = AZStd::move(material)]() mutable
                {
                    if (const auto state = weak.lock())
                    {
                        if (state->m_generation.load(std::memory_order_acquire) != generation) return;
                        if (auto* owner = state->m_owner.load(std::memory_order_acquire))
                            owner->ApplyNoiseTint(material);
                    }
                });
            });
        m_materialChangedHandler.Connect(channel->m_materialChanged);
        const auto* terrain = scene ? scene->GetFeatureProcessor<Terrain::TerrainFeatureProcessor>() : nullptr;
        ApplyNoiseTint(terrain ? terrain->GetMaterial() : nullptr);
    }

    void ProceduralGroundGradientComponent::OnSystemTick()
    {
        if (!m_activeEntityId.IsValid()) return;
        if (m_materialRetriesRemaining > 0) --m_materialRetriesRemaining;
        const unsigned remaining = m_materialRetriesRemaining;
        BindTerrainMaterial();
        m_materialRetriesRemaining = remaining;
        if (AZ::SystemTickBus::Handler::BusIsConnected() && remaining == 0)
            AZ::SystemTickBus::Handler::BusDisconnect();
    }

    void ProceduralGroundGradientComponent::OnEntityContextDestroyEntity(const AZ::EntityId& entityId)
    {
        if (entityId == m_activeEntityId)
        {
            ApplyNoiseTint({});
            BindTerrainMaterialForContext({});
        }
    }

    void ProceduralGroundGradientComponent::OnEntityContextReset()
    {
        OnEntityContextDestroyEntity(m_activeEntityId);
    }

    void ProceduralGroundGradientComponent::ApplyNoiseTint(const AZ::Data::Instance<AZ::RPI::Material>& material)
    {
        if (material != m_terrainMaterial)
        {
            // Scene-local mask bindings require a unique material. Follow the terrain
            // renderer's actual instance, including editor/game transitions and reloads.
            m_terrainMaterial = material;
            m_reportedMissingTintProperty = false;
        }
        if (!m_terrainMaterial) return;

        // Canvas material parameters have one owner: the selected material asset.
        // Preserve the serialized legacy value and reapply it when the override clears.
        if (m_terrainMaterial->FindPropertyIndex(AZ::Name("tint.contractVersion")).IsValid()) return;

        const auto propertyIndex = m_terrainMaterial->FindPropertyIndex(AZ::Name("settings.noiseTintStrength"));
        if (!propertyIndex.IsValid())
        {
            if (!m_reportedMissingTintProperty)
            {
                AZ_Warning("ProceduralGroundGradient", false,
                    "Terrain material has no settings.noiseTintStrength property. Allow Asset Processor to process "
                    "the project-local terrain material and shaders.");
                m_reportedMissingTintProperty = true;
            }
            return;
        }

        const float strength = AZ::GetClamp(GetQueryConfiguration().m_noiseTintStrength, 0.0f, 1.0f);
        // Compare with the material, not a cached value, so shader hot reload also restores the inspector setting.
        if (m_terrainMaterial->GetPropertyValue<float>(propertyIndex) != strength)
        {
            m_terrainMaterial->SetPropertyValue(propertyIndex, strength);
        }
        // TerrainFeatureProcessor compiles its scene-local material after preparing surfaces.
    }

    bool ProceduralGroundGradientComponent::ReadInConfig(const AZ::ComponentConfig* baseConfig)
    {
        return Internal::ReadConfiguration(baseConfig, m_configuration, [this] { OnConfigurationChanged(); });
    }

    bool ProceduralGroundGradientComponent::WriteOutConfig(AZ::ComponentConfig* outBaseConfig) const
    {
        return Internal::WriteConfiguration(outBaseConfig, m_configuration);
    }

    float ProceduralGroundGradientComponent::EvaluatePosition(
        const AZ::Vector3& position, const ProceduralGroundGradientConfig& configuration, bool pruneZeroProfiles)
    {
        const float density = AZ::GetClamp(configuration.m_hillDensity, 0.0f, 1.0f);
        const float amplitudeMeters = AZ::GetClamp(configuration.m_amplitudeMeters, 0.0f, TerrainHeightRangeMeters * 0.5f);
        if ((density <= 0.0f) || (amplitudeMeters <= 0.0f))
        {
            return NormalizedGroundHeight;
        }

        const float riseFrequency = AZ::GetClamp(configuration.m_frequency, 0.1f, 16.0f);
        const float hillAmount = ProceduralHillKernel::EvaluateReferenceField(position.GetX(), position.GetY(), density, riseFrequency, pruneZeroProfiles);
        const float normalizedAmplitude = amplitudeMeters / TerrainHeightRangeMeters;
        return AZ::GetClamp(NormalizedGroundHeight + (hillAmount * normalizedAmplitude), 0.0f, 1.0f);
    }

    float ProceduralGroundGradientComponent::GetValue(const GradientSignal::GradientSampleParams& sampleParams) const
    {
        if (const auto kernel = GetQueryKernel())
        {
            ProceduralHillKernel::Scratch scratch;
            return kernel->Sample(sampleParams.m_position.GetX(), sampleParams.m_position.GetY(), scratch);
        }
        return EvaluatePosition(sampleParams.m_position, GetQueryConfiguration());
    }

    void ProceduralGroundGradientComponent::GetValues(
        AZStd::span<const AZ::Vector3> positions,
        AZStd::span<float> outValues) const
    {
        if (positions.size() != outValues.size())
        {
            AZ_Assert(false, "Input and output lists are different sizes (%zu vs %zu).", positions.size(), outValues.size());
            return;
        }

        if (const auto kernel = GetQueryKernel()) kernel->SampleBatch(positions, outValues);
        else
        {
            const auto configuration = GetQueryConfiguration();
            for (size_t index = 0; index < positions.size(); ++index)
                outValues[index] = EvaluatePosition(positions[index], configuration);
        }
    }

    bool ProceduralGroundGradientComponent::GetTerrainExists(const AZ::Vector3& position) const
    {
        const auto configuration = GetQueryConfiguration();
        const auto maskEntityId = configuration.m_holeMask.m_gradientId;
        if (!maskEntityId.IsValid() || !std::isfinite(configuration.m_holeThreshold) ||
            configuration.m_holeThreshold < 0.0f || configuration.m_holeThreshold > 1.0f ||
            maskEntityId == configuration.m_holeMask.m_ownerEntityId ||
            GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread() ||
            !GradientSignal::GradientRequestBus::HasHandlers(maskEntityId) ||
            configuration.m_holeMask.IsEntityInHierarchy(configuration.m_holeMask.m_ownerEntityId))
        {
            return true;
        }
        const GradientSignal::GradientSampleParams params(position);
        return configuration.m_holeMask.GetValue(params) < configuration.m_holeThreshold;
    }

    void ProceduralGroundGradientComponent::GetTerrainExistsFromList(
        AZStd::span<const AZ::Vector3> positions, AZStd::span<bool> terrainExists) const
    {
        if (positions.size() != terrainExists.size())
        {
            AZ_Assert(false, "Terrain existence input/output lists have different sizes.");
            return;
        }
        if (positions.empty())
            return;
        const auto configuration = GetQueryConfiguration();
        const auto maskEntityId = configuration.m_holeMask.m_gradientId;
        if (!maskEntityId.IsValid() || !std::isfinite(configuration.m_holeThreshold) || configuration.m_holeThreshold < 0.0f ||
            configuration.m_holeThreshold > 1.0f || maskEntityId == configuration.m_holeMask.m_ownerEntityId ||
            GradientSignal::GradientRequestBus::HasReentrantEBusUseThisThread() ||
            !GradientSignal::GradientRequestBus::HasHandlers(maskEntityId) ||
            configuration.m_holeMask.IsEntityInHierarchy(configuration.m_holeMask.m_ownerEntityId))
        {
            AZStd::fill(terrainExists.begin(), terrainExists.end(), true);
            return;
        }
        // Snapshot configuration and validate the dependency once per batch, not
        // once per vertex. Bound scratch storage even for large terrain regions.
        constexpr size_t BatchSize = 256;
        AZStd::array<float, BatchSize> maskValues{};
        for (size_t offset = 0; offset < positions.size(); offset += BatchSize)
        {
            const size_t count = AZStd::min(BatchSize, positions.size() - offset);
            AZStd::fill(maskValues.begin(), maskValues.end(), 0.0f);
            configuration.m_holeMask.GetValues(positions.subspan(offset, count), AZStd::span<float>(maskValues.data(), count));
            for (size_t index = 0; index < count; ++index)
            {
                terrainExists[offset + index] = maskValues[index] < configuration.m_holeThreshold;
            }
        }
    }
} // namespace TerrainCompositor
