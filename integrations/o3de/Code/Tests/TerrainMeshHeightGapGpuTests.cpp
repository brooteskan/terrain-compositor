#include <AzTest/AzTest.h>
#include <TerrainCompositor/TerrainMeshCutoutRenderRegistry.h>
#include <TerrainCompositor/TerrainMeshHeightGapGpu.h>
#include <TerrainCompositor/TerrainMeshHeightStampSampling.h>
#include <cmath>
#include <cstdio>
#include <limits>

#if defined(AZ_PLATFORM_WINDOWS)
#include <AzCore/PlatformIncl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>
#include <wrl/client.h>
#endif

namespace TerrainCompositor
{
    namespace
    {
        // Full, mixed, and covered tiles, word/tile-word boundaries, and a partial final tile/word.
        TerrainMeshHeightDataPtr MakeGpuGapGrid(float spacing = 1.0f)
        {
            constexpr AZ::u32 width = 132, height = 18;
            AZStd::vector<AZ::Vector3> vertices;
            AZStd::vector<AZ::u32> indices;
            for (AZ::u32 y = 0; y < height; ++y)
                for (AZ::u32 x = 0; x < width; ++x)
                    vertices.emplace_back(-4.0f + float(x) * spacing, 3.0f + float(y) * spacing, 2.0f);
            for (AZ::u32 y = 0; y + 1 < height; ++y)
                for (AZ::u32 x = 0; x + 1 < width; ++x)
                {
                    if ((x < 8 && y < 8) || x == 31 || x == 32 || x == 130 || (x == 128 && y == 16))
                        continue;
                    const AZ::u32 a = y * width + x;
                    indices.insert(indices.end(), { a, a + 1, a + width + 1, a, a + width + 1, a + width });
                }
            auto data = AZStd::make_shared<TerrainMeshHeightData>();
            EXPECT_EQ(BuildTerrainMeshHeightData(vertices, indices, *data), TerrainMeshHeightValidation::Valid);
            data->m_revision = 0x123456789abcdef0ull;
            return data;
        }

        PreparedTerrainMeshHeightGap MakeGpuGap(
            TerrainMeshHeightDataPtr data, float yaw = 0, float scale = 1, float originX = 0, float originY = 0)
        {
            PreparedTerrainMeshHeightStamp height;
            height.m_data = AZStd::move(data);
            height.m_stampEntityId = AZ::EntityId(100);
            height.m_uncoveredAreaPolicy = TerrainMeshHeightUncoveredAreaPolicy::CutOutTerrain;
            height.m_cosYaw = std::cos(double(yaw));
            height.m_sinYaw = std::sin(double(yaw));
            height.m_scale = scale;
            height.m_inverseScale = 1.0 / scale;
            height.m_originX = originX;
            height.m_originY = originY;
            PreparedTerrainMeshHeightGap gap;
            EXPECT_TRUE(PrepareTerrainMeshHeightGap(height, 0.0f, gap));
            return gap;
        }

        AZ::Vector3 GapWorldPoint(const PreparedTerrainMeshHeightGap& gap, float x, float y)
        {
            return AZ::Vector3(
                float(gap.m_originX + (gap.m_cosYaw * x - gap.m_sinYaw * y) / gap.m_inverseScale),
                float(gap.m_originY + (gap.m_sinYaw * x + gap.m_cosYaw * y) / gap.m_inverseScale),
                37.0f);
        }

        AZStd::vector<AZ::Vector4> MakeBoundarySamples(AZStd::span<const PreparedTerrainMeshHeightGap> gaps)
        {
            AZStd::vector<AZ::Vector4> points;
            for (size_t instance = 0; instance < gaps.size(); ++instance)
            {
                const auto& gap = gaps[instance];
                const auto& data = *gap.m_data;
                for (int y = 0; y < int(data.m_height); ++y)
                    for (int x = 0; x < int(data.m_width); ++x)
                    {
                        for (float fraction : { 0.0f, 0.5f })
                        {
                            const auto world = GapWorldPoint(
                                gap,
                                data.m_localOrigin.GetX() + (x + fraction) * data.m_gridSpacing.GetX(),
                                data.m_localOrigin.GetY() + (y + fraction) * data.m_gridSpacing.GetY());
                            for (int axis = 0; axis < 3; ++axis)
                                for (float direction : { -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() })
                                {
                                    auto perturbed = world;
                                    if (axis < 2)
                                        perturbed.SetElement(axis, std::nextafter(world.GetElement(axis), direction));
                                    points.emplace_back(perturbed.GetX(), perturbed.GetY(), perturbed.GetZ(), float(instance));
                                }
                        }
                    }
            }
            return points;
        }
    } // namespace

    TEST(TerrainMeshHeightGapGpuTests, PacksWordsTilesAndSharesAnImmutableMaskAcrossInstancesAndTransforms)
    {
        const auto data = MakeGpuGapGrid();
        auto first = MakeGpuGap(data);
        auto moved = MakeGpuGap(data, 0.731f, 1.75f, 300.0f, -100.0f);
        moved.m_entityId = AZ::EntityId(101);
        const auto initial = PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ first, moved });
        ASSERT_EQ(initial.m_descriptors.size(), 2);
        ASSERT_EQ(initial.m_masks.size(), 1);
        ASSERT_TRUE(initial.m_words);
        EXPECT_EQ(initial.m_descriptors[0].m_maskOffset, initial.m_descriptors[1].m_maskOffset);
        EXPECT_EQ(initial.m_descriptors[0].m_tileOffset, initial.m_descriptors[1].m_tileOffset);
        EXPECT_EQ(initial.m_maskBytes, data->m_uncoveredCellBits.size() * sizeof(AZ::u32));
        EXPECT_EQ(initial.m_tileBytes, ((data->m_gapTileOccupancy.size() + 15) / 16) * sizeof(AZ::u32));
        EXPECT_EQ(initial.m_descriptors[0].m_revisionLow, 0x9abcdef0u);
        EXPECT_EQ(initial.m_descriptors[0].m_revisionHigh, 0x12345678u);
        const size_t tail = GetTerrainMeshHeightCellCount(*data) % 32;
        EXPECT_EQ((*initial.m_words)[data->m_uncoveredCellBits.size() - 1] >> tail, 0u);
        for (size_t tile = 0; tile < data->m_gapTileOccupancy.size(); ++tile)
            EXPECT_EQ(
                ((*initial.m_words)[initial.m_descriptors[0].m_tileOffset + tile / 16] >> ((tile % 16) * 2)) & 3,
                AZ::u32(data->m_gapTileOccupancy[tile]));

        const auto next = PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ moved, first, moved }, &initial);
        EXPECT_FALSE(next.m_masksChanged);
        EXPECT_EQ(next.m_words, initial.m_words);
        EXPECT_EQ(next.m_masks.size(), 1);
        EXPECT_EQ(next.m_descriptors.size(), 3);
        const auto unchanged = PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ moved, first, moved }, &next);
        EXPECT_FALSE(unchanged.m_masksChanged);
        EXPECT_EQ(unchanged.m_words, next.m_words);
        // A new immutable asset revision must never reuse the previous words, even with the same shape.
        auto reloaded = MakeGpuGap(MakeGpuGapGrid());
        const auto reload = PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ reloaded }, &next);
        EXPECT_TRUE(reload.m_masksChanged);
        EXPECT_NE(reload.m_words, next.m_words);
    }

    TEST(TerrainMeshHeightGapGpuTests, LimitsAndInvalidDataDeterministicallyRejectOnlyCoupledContributions)
    {
        const auto first = MakeGpuGap(MakeGpuGapGrid());
        const auto second = MakeGpuGap(MakeGpuGapGrid(0.3f));
        auto collisionOnly = first;
        collisionOnly.m_affectTerrainRendering = false;
        auto invalid = first;
        invalid.m_inverseScale = std::numeric_limits<double>::infinity();
        const auto result =
            PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ first, collisionOnly, invalid, second }, nullptr, { 1, 1, 4096 });
        ASSERT_EQ(result.m_admitted.size(), 1);
        EXPECT_TRUE(SameTerrainMeshHeightGap(result.m_admitted[0], first));
        EXPECT_EQ(result.m_rejected, 2);
        EXPECT_TRUE(IsTerrainMeshHeightGapAdmitted(collisionOnly, result.m_admitted));
        EXPECT_FALSE(IsTerrainMeshHeightGapAdmitted(second, result.m_admitted));
        const auto noBytes = PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ first }, nullptr, { 256, 256, 0 });
        EXPECT_TRUE(noBytes.m_admitted.empty());
        EXPECT_EQ(noBytes.m_rejected, 1);
        const auto noUnique = PrepareTerrainMeshHeightGapGpuData(AZStd::vector{ first, second }, nullptr, { 256, 1, 4096 });
        EXPECT_EQ(noUnique.m_admitted.size(), 1);
        EXPECT_EQ(noUnique.m_rejected, 1);
        const auto empty = PrepareTerrainMeshHeightGapGpuData({}, &result);
        EXPECT_TRUE(empty.m_descriptors.empty());
        EXPECT_EQ(empty.m_maskBytes + empty.m_tileBytes, 0);
        const auto stillEmpty = PrepareTerrainMeshHeightGapGpuData({}, &empty);
        EXPECT_FALSE(stillEmpty.m_masksChanged);
        EXPECT_EQ(stillEmpty.m_words, empty.m_words);
    }

    TEST(TerrainMeshHeightGapGpuTests, CpuAndPackedReferenceAgreeAtTransformedCellWordTileAndDomainBoundaries)
    {
        const auto data = MakeGpuGapGrid();
        const AZStd::vector gaps{ MakeGpuGap(data),
                                  MakeGpuGap(data, 0.731f, 1.75f, 300.0f, -100.0f),
                                  MakeGpuGap(MakeGpuGapGrid(0.3f), -1.27f, 0.37f, -90.0f, 1234.0f),
                                  MakeGpuGap(data, 0, 1, 4, -3),
                                  MakeGpuGap(MakeGpuGapGrid(0.3f)) };
        const auto gpu = PrepareTerrainMeshHeightGapGpuData(gaps);
        ASSERT_EQ(gpu.m_descriptors.size(), gaps.size());
        for (const auto& point : MakeBoundarySamples(gaps))
        {
            const size_t instance = size_t(point.GetW());
            ASSERT_EQ(
                SampleTerrainMeshHeightGap(point.GetAsVector3(), gaps[instance], TerrainMeshHeightGapConsumer::Rendering),
                SampleTerrainMeshHeightGapGpuData(point.GetAsVector3(), gpu.m_descriptors[instance], *gpu.m_words))
                << "instance " << instance << " at " << point.GetX() << ", " << point.GetY();
        }
        // Inclusive authored maximum must survive the rounded reciprocal; next float outside must not.
        const auto& nonBinary = gaps.back();
        const auto maximum = nonBinary.m_data->m_localBounds.GetMax();
        EXPECT_TRUE(SampleTerrainMeshHeightGap(maximum, nonBinary, TerrainMeshHeightGapConsumer::Rendering));
        EXPECT_FALSE(SampleTerrainMeshHeightGap(
            AZ::Vector3(std::nextafter(maximum.GetX(), INFINITY), maximum.GetY(), 0), nonBinary, TerrainMeshHeightGapConsumer::Rendering));
    }

    TEST(TerrainMeshHeightGapGpuTests, CpuEligibilityUsesOneRetainedActivationAndHonorsIndependentFlags)
    {
        auto gap = MakeGpuGap(MakeGpuGapGrid());
        const auto point = GapWorldPoint(gap, -3.5f, 3.5f);
        PreparedTerrainExistenceContributor contributor;
        contributor.m_type = PreparedTerrainExistenceContributor::Type::MeshHeightGap;
        contributor.m_meshHeightGap = gap;
        AZStd::vector contributors{ contributor };
        const AZStd::span<const PreparedTerrainMeshHeightGap> pending;
        EXPECT_TRUE(ComposeTerrainExists(point, true, contributors, &pending));
        const AZStd::vector admitted{ gap };
        const AZStd::span<const PreparedTerrainMeshHeightGap> ready(admitted);
        EXPECT_FALSE(ComposeTerrainExists(point, true, contributors, &ready));
        contributors[0].m_meshHeightGap.m_affectTerrainCollisionQueries = false;
        EXPECT_TRUE(ComposeTerrainExists(point, true, contributors, &ready));
        contributors[0].m_meshHeightGap.m_affectTerrainCollisionQueries = true;
        contributors[0].m_meshHeightGap.m_affectTerrainRendering = false;
        EXPECT_FALSE(ComposeTerrainExists(point, true, contributors, &pending));
        EXPECT_TRUE(ComposeTerrainRenderGeometryExists(point, true, contributors));
        gap.m_compositionSession = AZ::Uuid::CreateRandom();
        EXPECT_FALSE(IsTerrainMeshHeightGapAdmitted(gap, ready));
        gap = admitted[0];
        gap.m_originX += 1;
        EXPECT_FALSE(IsTerrainMeshHeightGapAdmitted(gap, ready));
    }

    TEST(TerrainMeshHeightGapGpuTests, ActivationRejectsStaleWorkAcrossPublishRemovalAndScenesWithoutMutatingReaders)
    {
        ASSERT_EQ(AZ::Interface<TerrainMeshCutoutRenderRegistry>::Get(), nullptr);
        TerrainMeshCutoutRenderRegistry registry;
        int sceneA = 0, sceneB = 0;
        const auto a = registry.AcquireSceneChannel(&sceneA);
        const auto b = registry.AcquireSceneChannel(&sceneB);
        const auto session = AZ::Uuid::CreateRandom();
        auto gap = MakeGpuGap(MakeGpuGapGrid());
        gap.m_compositionSession = session;
        ASSERT_TRUE(registry.Publish(&sceneA, session, {}, {}, 1, { gap }));
        const auto first = a->m_snapshot.load();
        ASSERT_TRUE(registry.ActivateGaps(&sceneA, first, { gap }));
        const auto batch = a->m_activation.load();
        ASSERT_TRUE(batch);
        ASSERT_EQ(batch->m_gaps.size(), 1);
        ASSERT_TRUE(registry.Publish(&sceneA, session, {}, {}, 2, { gap }));
        EXPECT_FALSE(registry.ActivateGaps(&sceneA, first, { gap }));
        EXPECT_FALSE(registry.ActivateGaps(&sceneB, a->m_snapshot.load(), { gap }));
        EXPECT_TRUE(b->m_activation.load()->m_gaps.empty());
        registry.ClearGapActivation(&sceneA);
        EXPECT_FALSE(a->m_activation.load());
        EXPECT_TRUE(IsTerrainMeshHeightGapAdmitted(gap, batch->m_gaps));
        const auto beforeRemoval = a->m_snapshot.load();
        registry.Remove(session);
        EXPECT_FALSE(registry.ActivateGaps(&sceneA, beforeRemoval, { gap }));
        EXPECT_TRUE(a->m_snapshot.load()->m_meshHeightGaps.empty());
        EXPECT_EQ(batch->m_gaps.size(), 1);
    }

    TEST(TerrainMeshHeightGapGpuTests, RetainedRenderHeightAndExistenceOverrideCollisionGroundPlaneIndependently)
    {
        auto height = std::make_shared<float>(42.0f);
        TerrainRenderGeometryQuery query;
        query.m_regionBounds = AZ::Aabb::CreateFromMinMax(AZ::Vector3(-10, -10, 0), AZ::Vector3(10, 10, 100));
        query.m_getHeight = [retained = *height](const auto&)
        {
            return retained;
        };
        query.m_getTerrainExists = [](const auto& position)
        {
            return position.GetX() >= 0;
        };
        TerrainMeshCutoutRenderSnapshot snapshot;
        snapshot.m_renderGeometryQueries.push_back(query);
        *height = 99.0f;
        float groundPlaneHeight = -1000.0f;
        bool renderExists = false;
        ASSERT_TRUE(TryGetTerrainRenderGeometryHeight(snapshot, AZ::Vector3(1, 1, -1000), groundPlaneHeight));
        ASSERT_TRUE(TryGetTerrainRenderGeometryExists(snapshot, AZ::Vector3(1, 1, -1000), renderExists));
        EXPECT_FLOAT_EQ(groundPlaneHeight, 42.0f);
        EXPECT_TRUE(renderExists);
        ASSERT_TRUE(TryGetTerrainRenderGeometryExists(snapshot, AZ::Vector3(-1, 1, -1000), renderExists));
        EXPECT_FALSE(renderExists); // Authored image holes are still topology holes.
        EXPECT_FALSE(TryGetTerrainRenderGeometryHeight(snapshot, AZ::Vector3(20, 1, -1000), groundPlaneHeight));
    }

#if defined(AZ_PLATFORM_WINDOWS)
    TEST(TerrainMeshHeightGapGpuTests, ActualShaderMatchesCpuClassificationOnD3D11HardwareOrWarp)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        auto status =
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
        const bool hardware = SUCCEEDED(status);
        if (!hardware)
            status =
                D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
        ASSERT_TRUE(SUCCEEDED(status)) << "D3D11 hardware and WARP unavailable: " << status;
        std::printf("Shader verification backend: %s\n", hardware ? "D3D11 hardware" : "D3D11 WARP");

        const auto read = [](const char* path)
        {
            std::ifstream file(path);
            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        };
        const auto srg = read(TERRAIN_COMPOSITOR_SHADER_DIR "/TerrainMaterialSrg.azsli");
        const auto begin = srg.find("struct MeshHeightGap");
        ASSERT_NE(begin, std::string::npos);
        const auto end = srg.find("};", begin);
        ASSERT_NE(end, std::string::npos);
        auto descriptor = srg.substr(begin, end + 2 - begin);
        descriptor.replace(descriptor.find("MeshHeightGap"), 13, "TG_MeshHeightGap");
        auto predicate = read(TERRAIN_COMPOSITOR_SHADER_DIR "/TerrainMeshHeightGapPredicate.azsli");
        ASSERT_FALSE(predicate.empty());
        // Only flatten AZSL's SRG namespace; compile the exact checked-in predicate and descriptor layout.
        const std::string qualifier = "TerrainMaterialSrg::";
        for (size_t at = predicate.find(qualifier); at != std::string::npos; at = predicate.find(qualifier))
            predicate.replace(at, qualifier.size(), "TG_");
        const std::string source = descriptor + R"(
StructuredBuffer<TG_MeshHeightGap> descriptors : register(t0);
StructuredBuffer<uint> TG_m_meshHeightGapWords : register(t1);
StructuredBuffer<float4> points : register(t2);
RWStructuredBuffer<uint> outputMask : register(u0);
)" + predicate +
            R"(
[numthreads(64, 1, 1)] void Main(uint3 id : SV_DispatchThreadID)
{
    uint count, stride; points.GetDimensions(count, stride);
    if (id.x < count) { float4 samplePosition = points[id.x]; outputMask[id.x] = TGMeshHeightGapContains(samplePosition.xyz, descriptors[uint(samplePosition.w)]) ? 1u : 0u; }
})";
        ComPtr<ID3DBlob> bytecode, errors;
        status = D3DCompile(
            source.data(),
            source.size(),
            "TerrainMeshHeightGapPredicate.azsli",
            nullptr,
            nullptr,
            "Main",
            "cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &bytecode,
            &errors);
        ASSERT_TRUE(SUCCEEDED(status)) << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Compile failed");
        ComPtr<ID3D11ComputeShader> shader;
        ASSERT_TRUE(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)));

        const auto data = MakeGpuGapGrid();
        const AZStd::vector gaps{ MakeGpuGap(data),
                                  MakeGpuGap(data, 0.731f, 1.75f, 300.0f, -100.0f),
                                  MakeGpuGap(MakeGpuGapGrid(0.3f), -1.27f, 0.37f, -90.0f, 1234.0f),
                                  MakeGpuGap(data, 0, 1, 4, -3),
                                  MakeGpuGap(MakeGpuGapGrid(0.3f)) };
        const auto gpu = PrepareTerrainMeshHeightGapGpuData(gaps);
        const auto points = MakeBoundarySamples(gaps);
        ASSERT_EQ(gpu.m_descriptors.size(), gaps.size());
        const auto buffer = [&](const void* contents, UINT stride, size_t count, UINT flags)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = UINT(stride * count);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = flags;
            desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            desc.StructureByteStride = stride;
            D3D11_SUBRESOURCE_DATA initial{};
            initial.pSysMem = contents;
            ComPtr<ID3D11Buffer> result;
            EXPECT_TRUE(SUCCEEDED(device->CreateBuffer(&desc, contents ? &initial : nullptr, &result)));
            return result;
        };
        const auto descriptors = buffer(
            gpu.m_descriptors.data(), sizeof(TerrainMeshHeightGapGpuDescriptor), gpu.m_descriptors.size(), D3D11_BIND_SHADER_RESOURCE);
        const auto words = buffer(gpu.m_words->data(), sizeof(AZ::u32), gpu.m_words->size(), D3D11_BIND_SHADER_RESOURCE);
        const auto positions = buffer(points.data(), sizeof(AZ::Vector4), points.size(), D3D11_BIND_SHADER_RESOURCE);
        const auto output = buffer(nullptr, sizeof(AZ::u32), points.size(), D3D11_BIND_UNORDERED_ACCESS);
        ASSERT_TRUE(descriptors && words && positions && output);
        ComPtr<ID3D11ShaderResourceView> descriptorView, wordView, pointView;
        ASSERT_TRUE(SUCCEEDED(device->CreateShaderResourceView(descriptors.Get(), nullptr, &descriptorView)));
        ASSERT_TRUE(SUCCEEDED(device->CreateShaderResourceView(words.Get(), nullptr, &wordView)));
        ASSERT_TRUE(SUCCEEDED(device->CreateShaderResourceView(positions.Get(), nullptr, &pointView)));
        ComPtr<ID3D11UnorderedAccessView> outputView;
        ASSERT_TRUE(SUCCEEDED(device->CreateUnorderedAccessView(output.Get(), nullptr, &outputView)));
        ID3D11ShaderResourceView* inputs[]{ descriptorView.Get(), wordView.Get(), pointView.Get() };
        auto* outputBinding = outputView.Get();
        context->CSSetShader(shader.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 3, inputs);
        context->CSSetUnorderedAccessViews(0, 1, &outputBinding, nullptr);
        context->Dispatch(UINT((points.size() + 63) / 64), 1, 1);
        D3D11_BUFFER_DESC readbackDesc{};
        readbackDesc.ByteWidth = UINT(points.size() * sizeof(AZ::u32));
        readbackDesc.Usage = D3D11_USAGE_STAGING;
        readbackDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> readback;
        ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&readbackDesc, nullptr, &readback)));
        context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ASSERT_TRUE(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        const auto* results = static_cast<const AZ::u32*>(mapped.pData);
        size_t mismatches = 0;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto& point = points[i];
            const bool expected =
                SampleTerrainMeshHeightGap(point.GetAsVector3(), gaps[size_t(point.GetW())], TerrainMeshHeightGapConsumer::Rendering);
            if ((results[i] != 0) != expected)
            {
                if (mismatches < 8)
                    ADD_FAILURE() << "GPU/CPU mismatch at " << point.GetX() << ", " << point.GetY() << " instance " << point.GetW();
                ++mismatches;
            }
        }
        context->Unmap(readback.Get(), 0);
        context->ClearState();
        EXPECT_EQ(mismatches, 0);
        std::printf("Compared %zu boundary classifications; %zu mismatches.\n", points.size(), mismatches);
    }
#endif
} // namespace TerrainCompositor
