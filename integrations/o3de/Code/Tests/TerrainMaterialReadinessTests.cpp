#include <TerrainCompositor/TerrainMaterialBindings.h>
#include <AzCore/Name/NameDictionary.h>
#include <AzCore/std/containers/array.h>
#include <AzCore/std/containers/span.h>
#include <AzTest/AzTest.h>

namespace TerrainCompositor
{
    namespace
    {
        struct Publication
        {
            AZStd::array<const void*, 6> buffers{};
            AZStd::array<AZ::u32, 6> values{};
            const void* GetBufferView(AZ::RHI::ShaderInputBufferIndex index, AZ::u32) const
            { return buffers[index.GetIndex()]; }
            AZStd::span<const AZ::u8> GetConstantRaw(AZ::RHI::ShaderInputConstantIndex index) const
            { return { reinterpret_cast<const AZ::u8*>(&values[index.GetIndex()]), sizeof(AZ::u32) }; }
        };

        class TerrainMaterialReadinessTests : public ::testing::Test
        {
        public:
            void SetUp() override
            {
                m_ownsNames = !AZ::NameDictionary::IsReady();
                if (m_ownsNames) AZ::NameDictionary::Create();
            }
            void TearDown() override { if (m_ownsNames) AZ::NameDictionary::Destroy(); }
            static AZ::RHI::Ptr<AZ::RHI::ShaderResourceGroupLayout> Layout(bool missingSurface = false)
            {
                using namespace AZ::RHI;
                auto layout = ShaderResourceGroupLayout::Create();
                layout->SetName(AZ::Name("TerrainMaterialSrg"));
                layout->SetBindingSlot(2);
                for (const char* name : { "m_meshCutouts", "m_meshCutoutVertices", "m_meshCutoutIndices", "m_meshHeightGaps", "m_meshHeightGapWords", "m_surfaceWords" })
                {
                    if (missingSurface && AZStd::string_view(name) == "m_surfaceWords") continue;
                    ShaderInputBufferDescriptor buffer;
                    buffer.m_name = AZ::Name(name);
                    buffer.m_type = ShaderInputBufferType::Structured;
                    buffer.m_access = ShaderInputBufferAccess::Read;
                    buffer.m_count = 1;
                    buffer.m_strideSize = 4;
                    layout->AddShaderInput(buffer);
                }
                unsigned offset = 0;
                for (const char* name : { "m_meshCutoutCount", "m_meshCutoutRevision", "m_meshHeightGapCount", "m_meshHeightGapRevision", "m_surfaceRegionCount", "m_surfaceRevision" })
                {
                    ShaderInputConstantDescriptor value;
                    value.m_name = AZ::Name(name);
                    value.m_constantByteOffset = offset;
                    value.m_constantByteCount = sizeof(AZ::u32);
                    layout->AddShaderInput(value);
                    offset += sizeof(AZ::u32);
                }
                EXPECT_TRUE(layout->Finalize());
                return layout;
            }
        private:
            bool m_ownsNames = false;
        };
    }

    TEST_F(TerrainMaterialReadinessTests, NewFallbackWaitsForEveryBufferAndItsSubmissionEvenWithZeroCounts)
    {
        const auto layout = Layout();
        AZStd::array<int, 6> emptyResources{};
        Publication requested, submitted;
        EXPECT_FALSE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
        for (size_t i = 0; i < emptyResources.size(); ++i)
        {
            requested.buffers[i] = &emptyResources[i];
            EXPECT_FALSE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
            submitted.buffers[i] = requested.buffers[i];
            EXPECT_EQ(IsTerrainPublicationCurrent(layout.get(), requested, submitted), i == emptyResources.size() - 1);
        }
        EXPECT_FALSE(IsTerrainPublicationCurrent(nullptr, requested, submitted));
        EXPECT_FALSE(IsTerrainPublicationCurrent(Layout(true).get(), requested, submitted));
    }

    TEST_F(TerrainMaterialReadinessTests, ReplacementBuffersAndEveryCountOrRevisionWaitForMatchingSubmittedData)
    {
        const auto layout = Layout();
        AZStd::array<int, 6> oldResources{}, newResources{};
        Publication requested, submitted;
        for (size_t i = 0; i < oldResources.size(); ++i) requested.buffers[i] = &oldResources[i];
        submitted = requested;
        ASSERT_TRUE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
        for (size_t i = 0; i < newResources.size(); ++i)
        {
            requested.buffers[i] = &newResources[i];
            EXPECT_FALSE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
            submitted = requested;
            EXPECT_TRUE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
            ++requested.values[i];
            EXPECT_FALSE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
            submitted = requested;
            EXPECT_TRUE(IsTerrainPublicationCurrent(layout.get(), requested, submitted));
        }
    }
}
