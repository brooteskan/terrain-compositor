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
            m_detailMaterialManager.SetTerrainMaterial(m_materialInstance);
            m_meshManager.SetMaterial(m_materialInstance);
            m_tintStageSource = {};
            m_tintStatus = "Incompatible shader reload; restored legacy terrain material";
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
        m_tintStatus = "Loading terrain tint material";
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
        m_tintStatus = "Restoring legacy terrain tint";
    }

    void TerrainFeatureProcessor::OnAssetError(AZ::Data::Asset<AZ::Data::AssetData> asset)
    {
        if (asset.GetId() == m_tintAsset.GetId())
        {
            m_tintStatus = "Tint asset failed; retaining the current valid terrain material";
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
        if (!srg || !reference || srg->GetLayout()->GetHash() != reference->GetLayout()->GetHash())
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
            const auto layout = shader->FindShaderResourceGroupLayout(AZ::Name("TerrainSrg"));
            if (tag == AZ::Name("forward") && !layout) compatible = false;
            if (layout && layout->GetHash() != m_terrainSrg->GetLayout()->GetHash()) compatible = false;
            return true;
        });
        const auto baseColor = material->FindPropertyIndex(AZ::Name("baseColor.color"));
        if (!baseColor.IsValid() || !material->GetPropertyValue(baseColor).Is<AZ::Color>()) return false;
        for (const char* name : { "settings.detailTextureMultiplier", "settings.detailFadeDistance", "settings.detailFadeLength" })
        {
            const auto index = material->FindPropertyIndex(AZ::Name(name));
            if (!index.IsValid() || !material->GetPropertyValue(index).Is<float>()) return false;
        }
        for (const char* name : { "settings.meshCutoutCount", "settings.meshCutoutRevision",
            "settings.meshHeightGapCount", "settings.meshHeightGapRevision" })
        {
            const auto index = material->FindPropertyIndex(AZ::Name(name));
            if (!index.IsValid() || !material->GetPropertyValue(index).Is<AZ::u32>()) return false;
        }
        if (material != m_defaultMaterial)
        {
            const auto version = material->FindPropertyIndex(AZ::Name("tint.contractVersion"));
            const auto strength = material->FindPropertyIndex(AZ::Name("tint.strength"));
            const auto color = material->FindPropertyIndex(AZ::Name("tint.color"));
            const auto texture = material->FindPropertyIndex(AZ::Name("tint.texture"));
            if (!version.IsValid() || !material->GetPropertyValue(version).Is<AZ::u32>() ||
                material->GetPropertyValue<AZ::u32>(version) != 1 || !strength.IsValid() ||
                !material->GetPropertyValue(strength).Is<float>() || !color.IsValid() ||
                !material->GetPropertyValue(color).Is<AZ::Vector3>() || !texture.IsValid() ||
                !material->GetPropertyValue(texture).Is<AZ::Data::Instance<AZ::RPI::Image>>()) return false;
        }
        return compatible && forward && depth && shadow;
    }

    bool TerrainFeatureProcessor::UpdateTintMaterial()
    {
        // Called after the compositor has bound this frame's cutout/gap publication.
        // Stage a complete material, compile it, and publish only on a subsequent
        // frame whose source publication is unchanged. No terrain invalidation.
        if (!m_pendingTintMaterial || !m_materialInstance || !m_terrainSrg) return false;
        if (!ValidateTintMaterial(m_pendingTintMaterial))
        {
            m_tintStatus = "Incompatible tint material; retaining the current terrain material";
            AZ_Warning("TerrainTint", false, "%s", m_tintStatus);
            m_pendingTintMaterial = {};
            return false;
        }
        const auto source = m_materialInstance->GetShaderResourceGroup();
        const auto target = m_pendingTintMaterial->GetShaderResourceGroup();
        if (!source || !target) return false;
        const auto sourceChange = m_materialInstance->GetCurrentChangeId();
        if (m_tintStageSource != source || m_tintStageTarget != target || m_tintStageChange != sourceChange)
        {
            for (const char* name : { "baseColor.color", "settings.detailTextureMultiplier", "settings.detailFadeDistance", "settings.detailFadeLength",
                "settings.meshCutoutCount", "settings.meshCutoutRevision", "settings.meshHeightGapCount", "settings.meshHeightGapRevision" })
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
            // A missing optional texture is neutral white, never an unbound sample.
            const auto textureProperty = m_pendingTintMaterial->FindPropertyIndex(AZ::Name("tint.texture"));
            if (textureProperty.IsValid() &&
                !m_pendingTintMaterial->GetPropertyValue<AZ::Data::Instance<AZ::RPI::Image>>(textureProperty))
                m_pendingTintMaterial->SetPropertyValue(textureProperty,
                    AZ::RPI::ImageSystemInterface::Get()->GetSystemImage(AZ::RPI::SystemImage::White));
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
            m_tintStatus = "Preparing terrain tint bindings";
            return false;
        }
        if (target->IsQueuedForCompile() || m_pendingTintMaterial->NeedsCompile()) return false;
        m_materialInstance = AZStd::move(m_pendingTintMaterial);
        m_detailMaterialManager.SetTerrainMaterial(m_materialInstance);
        m_meshManager.SetMaterial(m_materialInstance);
        m_tintStageSource = {};
        m_tintStageTarget = {};
        m_tintStatus = m_materialInstance == m_defaultMaterial ? "Legacy terrain tint" : "Canvas terrain tint active";
        return true;
    }
} // namespace Terrain
