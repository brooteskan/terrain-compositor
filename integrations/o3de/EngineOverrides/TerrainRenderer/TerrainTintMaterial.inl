#include <TerrainCompositor/TerrainMaterialBindings.h>
// Included by the maintained TerrainFeatureProcessor.cpp override. This code is
// compiled into Terrain.Static, so the optional Canvas Gem adds no reverse link.
namespace Terrain
{
    void TerrainFeatureProcessor::ValidateActiveTintMaterial()
    {
        // Material shader reloads can replace an SRG inside the same instance.
        // Reject that layout before the compositor binds any resources to it.
        if (m_materialInstance && m_materialInstance != m_defaultMaterial &&
            m_defaultMaterial && m_terrainSrg && !ValidateTintMaterial(m_materialInstance))
        {
            m_materialInstance = m_defaultMaterial;
            m_meshManager.SetMaterial(m_materialInstance);
            m_tintStageSource = {};
            m_tintStatus = "Incompatible shader reload; restored default graph material";
            AZ_Warning("TerrainTint", false, "%s", m_tintStatus);
        }
    }

    bool TerrainFeatureProcessor::SetTintMaterial(
        AZ::EntityId owner, const AZ::Data::Asset<AZ::RPI::MaterialAsset>& asset)
    {
        if (!owner.IsValid() || (m_tintOwner.IsValid() && m_tintOwner != owner))
            return false;
        if (!asset.GetId().IsValid())
        {
            ClearTintMaterial(owner);
            return true;
        }
        if (m_tintOwner == owner && m_tintAsset.GetId() == asset.GetId())
            return true;
        if (m_tintAsset.GetId().IsValid())
            AZ::Data::AssetBus::MultiHandler::BusDisconnect(m_tintAsset.GetId());
        m_tintOwner = owner;
        m_pendingTintMaterial = {};
        m_tintAsset = asset;
        m_tintStatus = "Loading terrain material";
        AZ::Data::AssetBus::MultiHandler::BusConnect(asset.GetId());
        if (m_tintAsset.IsReady())
            OnAssetReady(m_tintAsset);
        else
            m_tintAsset.QueueLoad();
        return true;
    }

    void TerrainFeatureProcessor::ClearTintMaterial(AZ::EntityId owner)
    {
        if (m_tintOwner != owner) return;
        if (m_tintAsset.GetId().IsValid())
            AZ::Data::AssetBus::MultiHandler::BusDisconnect(m_tintAsset.GetId());
        m_tintAsset = {};
        m_tintOwner = AZ::EntityId{};
        m_pendingTintMaterial = m_defaultMaterial;
        m_tintStageSource = {};
        m_tintStatus = "Restoring default graph material";
    }

    void TerrainFeatureProcessor::OnAssetError(AZ::Data::Asset<AZ::Data::AssetData> asset)
    {
        if (asset.GetId() == m_tintAsset.GetId())
        {
            m_tintStatus = "Terrain material asset failed; retaining the current valid terrain material";
            AZ_Warning("TerrainTint", false, "%s", m_tintStatus);
        }
    }

    void TerrainFeatureProcessor::OnAssetReloadError(AZ::Data::Asset<AZ::Data::AssetData> asset)
    {
        OnAssetError(asset);
    }

    bool TerrainFeatureProcessor::ValidateTintMaterial(const MaterialInstance& material) const
    {
        if (!material || !material->CanCompile() || !m_defaultMaterial || !m_terrainSrg)
            return false;
        const auto srg = material->GetShaderResourceGroup();
        const auto reference = m_defaultMaterial->GetShaderResourceGroup();
        if (!srg || !reference)
            return false;
        if (!TerrainCompositor::ValidateTerrainRendererBindings(srg->GetLayout(), reference->GetLayout()))
            return false;
        const auto objectLayout = material->GetAsset()->GetObjectSrgLayout();
        const auto referenceObject = m_defaultMaterial->GetAsset()->GetObjectSrgLayout();
        if (!objectLayout || !referenceObject || objectLayout->GetHash() != referenceObject->GetHash())
            return false;
        bool forward = false, depth = false, shadow = false, compatible = true;
        material->ForAllShaderItems([&](const AZ::Name&, const AZ::RPI::ShaderCollection::Item& item)
        {
            const auto& shader = item.GetShaderAsset();
            if (!shader.IsReady()) { compatible = false; return false; }
            const auto tag = shader->GetDrawListName();
            forward |= tag == AZ::Name("forward");
            depth |= tag == AZ::Name("depth");
            shadow |= tag == AZ::Name("shadow");
            const auto materialLayout = shader->FindShaderResourceGroupLayout(AZ::Name("TerrainMaterialSrg"));
            if (!materialLayout || materialLayout->GetHash() != srg->GetLayout()->GetHash()) compatible = false;
            const auto shaderObject = shader->FindShaderResourceGroupLayout(AZ::Name("ObjectSrg"));
            if (!shaderObject || shaderObject->GetHash() != referenceObject->GetHash()) compatible = false;
            const auto layout = shader->FindShaderResourceGroupLayout(AZ::Name("TerrainSrg"));
            if (tag == AZ::Name("forward") && !layout) compatible = false;
            if (layout && layout->GetHash() != m_terrainSrg->GetLayout()->GetHash()) compatible = false;
            return true;
        });
        struct PublicationProperty { const char* property; const char* binding; };
        for (const auto& field : {
            PublicationProperty{ "settings.meshCutoutCount", "m_meshCutoutCount" },
            PublicationProperty{ "settings.meshCutoutRevision", "m_meshCutoutRevision" },
            PublicationProperty{ "settings.meshHeightGapCount", "m_meshHeightGapCount" },
            PublicationProperty{ "settings.meshHeightGapRevision", "m_meshHeightGapRevision" } })
        {
            const auto index = material->FindPropertyIndex(AZ::Name(field.property));
            if (!index.IsValid() || !material->GetPropertyValue(index).Is<AZ::u32>()) return false;
            const auto& connections = material->GetMaterialPropertiesLayout()->GetPropertyDescriptor(index)->GetOutputConnections();
            if (connections.size() != 1 || connections[0].m_type != AZ::RPI::MaterialPropertyOutputType::ShaderInput ||
                connections[0].m_shaderInputName != AZ::Name(field.binding)) return false;
        }
        const auto version = material->FindPropertyIndex(AZ::Name("terrain.contractVersion"));
        if (!version.IsValid() || !material->GetPropertyValue(version).Is<AZ::u32>() ||
            material->GetPropertyValue<AZ::u32>(version) != 3) return false;
        return compatible && forward && depth && shadow;
    }

    bool TerrainFeatureProcessor::UpdateTintMaterial()
    {
        // Called after the compositor has bound this frame's cutout/gap publication.
        // Stage a complete material, compile it, and publish only on a subsequent
        // frame whose source publication is unchanged. No terrain invalidation.
        if (!m_pendingTintMaterial || !m_materialInstance || !m_terrainSrg) return false;
        if (m_pendingTintMaterial == m_materialInstance)
        {
            m_pendingTintMaterial = {};
            m_tintStageSource = {};
            m_tintStatus = m_materialInstance == m_defaultMaterial ? "Default graph terrain material" : "Graph terrain material active";
            return false;
        }
        if (!ValidateTintMaterial(m_pendingTintMaterial))
        {
            m_tintStatus = "Incompatible terrain material; retaining the current terrain material";
            AZ_Warning("TerrainTint", false, "%s", m_tintStatus);
            m_pendingTintMaterial = {};
            return false;
        }
        const auto source = m_materialInstance->GetShaderResourceGroup();
        const auto target = m_pendingTintMaterial->GetShaderResourceGroup();
        if (!source || !target) return false;
        const auto sourceChange = m_materialInstance->GetCurrentChangeId();
        if (m_tintStageSource != source || m_tintStageTarget != target || m_tintStageChange != sourceChange ||
            m_tintStageTargetChange != m_pendingTintMaterial->GetCurrentChangeId() ||
            m_tintStageTargetLayout != target->GetLayout()->GetHash())
        {
            for (const char* name : { "settings.meshCutoutCount", "settings.meshCutoutRevision", "settings.meshHeightGapCount", "settings.meshHeightGapRevision" })
            {
                const auto from = m_materialInstance->FindPropertyIndex(AZ::Name(name));
                const auto to = m_pendingTintMaterial->FindPropertyIndex(AZ::Name(name));
                m_pendingTintMaterial->SetPropertyValue(to, m_materialInstance->GetPropertyValue(from));
            }
            for (const char* name : { "m_meshCutouts", "m_meshCutoutVertices", "m_meshCutoutIndices", "m_meshHeightGaps", "m_meshHeightGapWords" })
            {
                const auto from = source->FindShaderInputBufferIndex(AZ::Name(name));
                const auto to = target->FindShaderInputBufferIndex(AZ::Name(name));
                if (!from.IsValid() || !to.IsValid() || !source->GetBufferView(from) ||
                    !target->SetBufferView(to, source->GetBufferView(from).get())) return false;
            }
            // Mark the SRG dirty even when all copied counts are zero. Buffer-view
            // setters alone do not notify the MaterialInstanceHandler.
            const auto revision = m_pendingTintMaterial->FindPropertyIndex(AZ::Name("settings.meshCutoutRevision"));
            const auto revisionValue = m_pendingTintMaterial->GetPropertyValue<AZ::u32>(revision);
            m_pendingTintMaterial->SetPropertyValue(revision, revisionValue ^ 1u);
            m_pendingTintMaterial->SetPropertyValue(revision, revisionValue);
            m_pendingTintMaterial->Compile();
            m_tintStageSource = source;
            m_tintStageTarget = target;
            m_tintStageChange = sourceChange;
            m_tintStageTargetChange = m_pendingTintMaterial->GetCurrentChangeId();
            m_tintStageTargetLayout = target->GetLayout()->GetHash();
            m_tintStatus = "Preparing terrain material bindings";
            return false;
        }
        if (target->IsQueuedForCompile() || m_pendingTintMaterial->NeedsCompile()) return false;
        m_materialInstance = AZStd::move(m_pendingTintMaterial);
        m_meshManager.SetMaterial(m_materialInstance);
        m_tintStageSource = {};
        m_tintStageTarget = {};
        m_tintStatus = m_materialInstance == m_defaultMaterial ? "Default graph terrain material" : "Graph terrain material active";
        return true;
    }
} // namespace Terrain
