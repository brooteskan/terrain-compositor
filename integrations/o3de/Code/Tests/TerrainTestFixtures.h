#pragma once

#include <AzCore/Name/NameDictionary.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <GradientSignal/Ebuses/GradientRequestBus.h>
#include <TerrainCompositor/TerrainExistenceSampling.h>

namespace TerrainCompositor::TestSupport
{
    class ScopedNameDictionary
    {
    public:
        ScopedNameDictionary()
        {
            if (m_owned)
                AZ::NameDictionary::Create();
        }
        ~ScopedNameDictionary()
        {
            if (m_owned)
                AZ::NameDictionary::Destroy();
        }
        ScopedNameDictionary(const ScopedNameDictionary&) = delete;
        ScopedNameDictionary& operator=(const ScopedNameDictionary&) = delete;
    private:
        bool m_owned = !AZ::NameDictionary::IsReady();
    };

    inline HeightmapData MakeImage(AZ::u32 width, AZ::u32 height, AZStd::vector<float> samples)
    {
        HeightmapData image;
        image.m_width = width;
        image.m_height = height;
        image.m_samples = AZStd::move(samples);
        return image;
    }

    inline HeightmapDataPtr MakeMask(float value)
    {
        return AZStd::make_shared<HeightmapData>(MakeImage(1, 1, { value }));
    }

    inline PreparedTerrainExistenceStamp MakePreparedMask(
        float value, TerrainExistenceOperation operation, float halfExtent = 1.0f)
    {
        PreparedTerrainExistenceStamp stamp;
        stamp.m_mask = MakeMask(value);
        stamp.m_placement.m_worldBounds = AZ::Aabb::CreateFromMinMax(
            AZ::Vector3(-halfExtent, -halfExtent, 0.0f), AZ::Vector3(halfExtent, halfExtent, 0.0f));
        stamp.m_placement.m_cosYaw = stamp.m_placement.m_inverseScale = 1.0;
        stamp.m_placement.m_halfWidth = stamp.m_placement.m_halfDepth = halfExtent;
        stamp.m_threshold = 0.5f;
        stamp.m_operation = operation;
        return stamp;
    }

    class ConstantGradient final : public GradientSignal::GradientRequestBus::Handler
    {
    public:
        ConstantGradient(AZ::EntityId id, float value) : m_value(value) { BusConnect(id); }
        ~ConstantGradient() override { BusDisconnect(); }
        float GetValue(const GradientSignal::GradientSampleParams&) const override { return m_value; }
    private:
        float m_value;
    };
}
