#pragma once

#include <AzTest/AzTest.h>
#include <Atom/Feature/RayTracing/RayTracingFeatureProcessorInterface.h>

namespace AZ::Render
{
    // Exercise the production registration/selection path without a GPU scene.
    class SectorRayTracingMock : public RayTracingFeatureProcessorInterface
    {
    public:
        using BindlessIndices = AZStd::unordered_map<int, uint32_t>;
        MOCK_METHOD(ProceduralGeometryTypeHandle, RegisterProceduralGeometryType,
            (const AZStd::string&, const Data::Instance<RPI::Shader>&, const AZStd::string&, const BindlessIndices&), (override));
        MOCK_METHOD(void, SetProceduralGeometryTypeBindlessBufferIndex, (ProceduralGeometryTypeWeakHandle, const BindlessIndices&), (override));
        MOCK_METHOD(void, AddProceduralGeometry,
            (ProceduralGeometryTypeWeakHandle, const Uuid&, const Aabb&, const MeshInfoHandle&,
                RHI::RayTracingAccelerationStructureInstanceInclusionMask, uint32_t), (override));
        MOCK_METHOD(void, SetProceduralGeometryTransform, (const Uuid&, const Transform&, const Vector3&), (override));
        MOCK_METHOD(void, SetProceduralGeometryLocalInstanceIndex, (const Uuid&, uint32_t), (override));
        MOCK_METHOD(void, RemoveProceduralGeometry, (const Uuid&), (override));
        MOCK_METHOD(int, GetProceduralGeometryCount, (ProceduralGeometryTypeWeakHandle), (const, override));
        MOCK_METHOD(void, AddMesh, (const Uuid&, const Mesh&, const SubMeshVector&), (override));
        MOCK_METHOD(void, RemoveMesh, (const Uuid&), (override));
        MOCK_METHOD(void, SetMeshTransform, (const Uuid&, const Transform, const Vector3), (override));
        MOCK_METHOD(const SubMeshVector&, GetSubMeshes, (), (const, override));
        MOCK_METHOD(SubMeshVector&, GetSubMeshes, (), (override));
        MOCK_METHOD(const MeshMap&, GetMeshMap, (), (override));
        MOCK_METHOD(Data::Instance<RPI::ShaderResourceGroup>, GetRayTracingSceneSrg, (), (const, override));
        MOCK_METHOD(const RHI::Ptr<RHI::RayTracingTlas>&, GetTlas, (), (const, override));
        MOCK_METHOD(RHI::Ptr<RHI::RayTracingTlas>&, GetTlas, (), (override));
        MOCK_METHOD(uint32_t, GetRevision, (), (const, override));
        MOCK_METHOD(uint32_t, GetBuiltRevision, (int), (const, override));
        MOCK_METHOD(void, SetBuiltRevision, (int, uint32_t), (override));
        MOCK_METHOD(uint32_t, GetProceduralGeometryTypeRevision, (), (const, override));
        MOCK_METHOD(AZStd::mutex&, GetBlasBuiltMutex, (), (override));
        MOCK_METHOD(uint32_t, GetSkinnedMeshCount, (), (const, override));
        MOCK_METHOD(RHI::RayTracingBufferPools&, GetBufferPools, (), (override));
        MOCK_METHOD(uint32_t, GetSubMeshCount, (), (const, override));
        MOCK_METHOD(bool, HasMeshGeometry, (), (const, override));
        MOCK_METHOD(bool, HasProceduralGeometry, (), (const, override));
        MOCK_METHOD(bool, HasGeometry, (), (const, override));
        MOCK_METHOD(RHI::AttachmentId, GetTlasAttachmentId, (), (const, override));
        MOCK_METHOD(void, BeginFrame, (int), (override));
        MOCK_METHOD(void, UpdateRayTracingSrgs, (), (override));
        MOCK_METHOD(BlasInstanceMap&, GetBlasInstances, (), (override));
        MOCK_METHOD(BlasBuildList&, GetBlasBuildList, (int), (override));
        MOCK_METHOD(const BlasBuildList&, GetSkinnedMeshBlasList, (), (override));
        MOCK_METHOD(BlasBuildList&, GetBlasCompactionList, (int), (override));
        const void MarkBlasInstanceForCompaction(int, Data::AssetId) override {}
        const void MarkBlasInstanceAsCompactionEnqueued(int, Data::AssetId) override {}
        MOCK_METHOD(const ProceduralGeometryTypeList&, GetProceduralGeometryTypes, (), (const, override));
        MOCK_METHOD(const ProceduralGeometryList&, GetProceduralGeometries, (), (const, override));
        MOCK_METHOD(RHI::MultiDevice::DeviceMask, GetDeviceMask, (), (const, override));
    };
}
