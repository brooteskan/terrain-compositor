#include <TerrainRenderer/GraphMaterialClipmaps.h>
#include <TerrainRenderer/GraphMaterialClipmapSchedule.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/Vector4.h>
#include <AzCore/std/containers/array.h>
#include <Atom/RPI.Public/Image/AttachmentImage.h>
#include <Atom/RPI.Public/Image/AttachmentImagePool.h>
#include <Atom/RPI.Public/Image/ImageSystemInterface.h>
#include <Atom/RPI.Public/Pass/RasterPass.h>
#include <Atom/RPI.Public/Pass/PassAttachment.h>
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Reflect/Pass/PassTemplate.h>
#include <Atom/RPI.Reflect/Pass/RasterPassData.h>
#include <cmath>
#include <Atom/RHI/RHISystemInterface.h>
#include <Atom/RHI/DrawListTagRegistry.h>

AZ_CVAR(bool, r_terrainGraphClipmaps, true, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Use graph terrain material clipmaps; false selects the unchanged direct evaluator.");
AZ_CVAR(bool, r_terrainGraphClipmapDebug, false, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Show material cache coverage: green cached, red direct.");

AZ_CVAR(bool, r_terrainGraphClipmapDiagnostics, false, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Publish cache readiness and update counters to the legacy editor console.");
// Diagnostic values are overwritten by the renderer every frame.
AZ_CVAR(AZ::u64, r_terrainGraphClipmapUpdates, 0, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Number of completed material cache layer updates in the current scene.");
AZ_CVAR(bool, r_terrainGraphClipmapReady, false, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Whether the current terrain material cache is ready for sampling.");

AZ_CVAR(bool, r_terrainGraphClipmapIncremental, true, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Use budgeted toroidal strip updates; false selects the full-layer reference path.");
AZ_CVAR(AZ::u32, r_terrainGraphClipmapBudgetTexels, 32768, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Maximum material texels scheduled per frame across all clipmap levels.");
AZ_CVAR(AZ::u32, r_terrainGraphClipmapBudgetPasses, 2, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Maximum cache raster passes scheduled per frame (one to five).");
AZ_CVAR(AZ::u64, r_terrainGraphClipmapTexels, 0, nullptr, AZ::ConsoleFunctorFlags::Null,"Scheduled cache texels this frame.");
AZ_CVAR(AZ::u64, r_terrainGraphClipmapPendingTexels, 0, nullptr, AZ::ConsoleFunctorFlags::Null,"Pending cache texels including submitted work.");
AZ_CVAR(AZ::u64, r_terrainGraphClipmapGpuNs, 0, nullptr, AZ::ConsoleFunctorFlags::Null,"Fresh delayed GPU cache query durations; zero means unavailable.");
AZ_CVAR(AZ::u64, r_terrainGraphClipmapGpuSamples, 0, nullptr, AZ::ConsoleFunctorFlags::Null,"Number of fresh GPU cache query results this frame.");

AZ_CVAR(AZ::u64, r_terrainGraphClipmapFrameUs, 0, nullptr, AZ::ConsoleFunctorFlags::Null,
    "RHI CPU frame-to-frame interval in microseconds.");
AZ_CVAR(AZ::u64, r_terrainGraphClipmapFrameId, 0, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Terrain cache render-update sequence for timing freshness.");

namespace TerrainCompositor
{
    namespace
    {
        constexpr AZ::u32 Levels=5, Size=512, Channels=4;
        using Images=AZStd::array<AZ::Data::Instance<AZ::RPI::AttachmentImage>,Channels>;
        const char* Slots[Channels]={"CacheColor","CacheNormal","CacheFactors","CacheGeometry"};
        const char* Bindings[Channels]={"m_graphCacheColor","m_graphCacheNormal","m_graphCacheFactors","m_graphCacheGeometry"};
        using namespace AZ;

        class GraphCacheRasterPass final : public RPI::RasterPass
        {
        public:
            AZ_RTTI(GraphCacheRasterPass,"{7ADE3D92-7D3C-4A78-8372-34FA18419161}",RPI::RasterPass);
            AZ_CLASS_ALLOCATOR(GraphCacheRasterPass,SystemAllocator);
            GraphCacheRasterPass(const RPI::PassDescriptor& desc,Images images)
                :RPI::RasterPass(desc),m_images(AZStd::move(images)) {}
            bool IsEnabled() const override
            {
                const auto state=GetPassState();
                return m_refresh && (state==RPI::PassState::Idle || state==RPI::PassState::Queued ||
                    state==RPI::PassState::Rendering) && RPI::RasterPass::IsEnabled();
            }
            void Disable() { m_refresh=false; }
            void Configure(const Vector4& projection,float tag,GraphClipmaps::Region rectangle,
                const Data::Instance<RPI::ShaderResourceGroup>& terrain,bool clear,u64 ticket)
            {
                m_projection=projection; m_tag=tag; m_rectangle=rectangle; m_terrain=terrain;
                m_clear=clear; m_ticket=ticket; m_refresh=true;
                SetTimestampQueryEnabled(r_terrainGraphClipmapDiagnostics);
            }
            u64 CompletedTicket() const { return m_completedTicket; }
            u64 Updates() const { return m_updates; }
            bool CanSchedule() { return GetShaderResourceGroup()!=nullptr && GetPassState()==RPI::PassState::Idle; }
        protected:
            void BuildInternal() override
            {
                RPI::RasterPass::BuildInternal();
                for(u32 c=0;c<Channels;++c) AttachImageToSlot(Name(Slots[c]),m_images[c]);
            }
            void FrameBeginInternal(FramePrepareParams params) override
            {
                RPI::RasterPass::FrameBeginInternal(params);
                m_scissorState=RHI::Scissor(int(m_rectangle.x),int(m_rectangle.y),
                    int(m_rectangle.x)+m_rectangle.width,int(m_rectangle.y)+m_rectangle.height);
                m_overrideScissorSate=true;
            }
            void SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph) override
            {
                for(u32 c=0;c<Channels;++c)
                    GetOutputBinding(c).m_unifiedScopeDesc.m_loadStoreAction.m_loadAction=
                        m_clear?RHI::AttachmentLoadAction::Clear:RHI::AttachmentLoadAction::Load;
                RPI::RasterPass::SetupFrameGraphDependencies(frameGraph);
            }
            void CompileResources(const RHI::FrameGraphCompileContext& context) override
            {
                if(auto* srg=GetShaderResourceGroup())
                {
                    RHI::ShaderInputNameIndex projection("m_graphCacheProjection"),tag("m_graphCacheTag");
                    srg->SetConstant(projection,m_projection);srg->SetConstant(tag,m_tag);
                }
                if(m_terrain) BindSrg(m_terrain->GetRHIShaderResourceGroup());
                RPI::RasterPass::CompileResources(context);
            }
            void FrameEndInternal() override
            {
                if(IsEnabled()) { m_completedTicket=m_ticket; ++m_updates; }
                RPI::RasterPass::FrameEndInternal();
            }
        private:
            Images m_images;
            Data::Instance<RPI::ShaderResourceGroup> m_terrain;
            Vector4 m_projection=Vector4::CreateZero();
            GraphClipmaps::Region m_rectangle{};
            float m_tag=0;
            bool m_refresh=false,m_clear=false;
            u64 m_ticket=0,m_completedTicket=0,m_updates=0;
        };

        RPI::PassSlot Slot(const Name& name,RPI::PassSlotType type,RHI::ScopeAttachmentUsage usage)
        {
            RPI::PassSlot slot;
            slot.m_name=name; slot.m_slotType=type; slot.m_scopeAttachmentUsage=usage;
            slot.m_shaderInputName=Name("NoBind");
            slot.m_scopeAttachmentStage=RHI::ScopeAttachmentStage::FragmentShader;
            return slot;
        }
    }

    struct GraphMaterialClipmaps::State
    {
        struct Layer
        {
            Images images;
            AZStd::array<AZ::RPI::Ptr<GraphCacheRasterPass>,4> passes;
            GraphClipmaps::Schedule schedule;
            GraphClipmaps::Region lease{};
            AZ::RPI::Ptr<GraphCacheRasterPass> leasedPass;
            AZ::u64 ticket=0;
            bool initialized=false,fullDirty=true;
        };
        AZStd::array<Layer,Levels> layers;
        AZStd::array<AZ::Vector4,Levels> mappings,tags;
        AZStd::array<AZ::u64,Levels*4> timestampTicks{};
        AZ::RPI::RenderPipelinePtr pipeline;
        AZ::Data::Instance<AZ::RPI::Material> material;
        AZ::Data::Asset<AZ::RPI::ShaderAsset> shader;
        AZ::Matrix4x4 camera=AZ::Matrix4x4::CreateZero();
        AZ::u64 revision=~AZ::u64(0),change=~AZ::u64(0),nextTicket=1;
        AZ::Vector3 lastPosition=AZ::Vector3::CreateZero();
        AZ::u32 nextTag=1,cursor=0,stationaryFrames=0;
        bool dirty=true,incremental=true,geometryPending=false;
        ~State() { for(auto& layer:layers) for(auto& pass:layer.passes) if(pass) pass->QueueForRemoval(); }
    };

    GraphMaterialClipmaps::GraphMaterialClipmaps()=default;
    GraphMaterialClipmaps::~GraphMaterialClipmaps()=default;
    void GraphMaterialClipmaps::Shutdown() { m_state.reset(); }
    void GraphMaterialClipmaps::Invalidate() { if(m_state) m_state->dirty=true; }

    void GraphMaterialClipmaps::Update(AZ::RPI::Scene* scene,
        const AZ::Data::Instance<AZ::RPI::Material>& material,
        const AZ::Data::Instance<AZ::RPI::ShaderResourceGroup>& terrainSrg,
        const AZ::RPI::ViewPtr& view,AZ::u64 geometryRevision)
    {
        using namespace AZ;
        struct Diagnostics
        {
            bool ready=false; u64 updates=0,texels=0,pending=0,gpuNs=0,gpuSamples=0,frameUs=0,frameId=0;
            ~Diagnostics()
            {
                r_terrainGraphClipmapReady=ready;r_terrainGraphClipmapUpdates=updates;
                if(!r_terrainGraphClipmapDiagnostics) return;
                if(auto* console=Interface<IConsole>::Get())
                {
                    console->PerformCommand("r_terrainGraphClipmapReady",{ready?"true":"false"},ConsoleSilentMode::Silent);
                    const char* names[]={"r_terrainGraphClipmapUpdates","r_terrainGraphClipmapTexels",
                        "r_terrainGraphClipmapPendingTexels","r_terrainGraphClipmapGpuNs","r_terrainGraphClipmapGpuSamples","r_terrainGraphClipmapFrameUs","r_terrainGraphClipmapFrameId"};
                    const u64 values[]={updates,texels,pending,gpuNs,gpuSamples,frameUs,frameId};
                    for(u32 i=0;i<7;++i)
                    { const auto text=AZStd::string::format("%llu",static_cast<unsigned long long>(values[i]));
                      console->PerformCommand(names[i],{text},ConsoleSilentMode::Silent); }
                }
            }
        } diagnostics;
        static u64 frameId=0;
        diagnostics.frameId=++frameId;
        if(auto* rhi=RHI::RHISystemInterface::Get())
            diagnostics.frameUs=u64(AZStd::max(0.0,rhi->GetCpuFrameTime())*1000.0);
        if(!terrainSrg || !terrainSrg->GetLayout()->FindShaderInputConstantIndex(Name("m_graphCacheEnabled")).IsValid()) return;
        RHI::ShaderInputNameIndex enabled("m_graphCacheEnabled"),debug("m_graphCacheDebug"),mappingIndex("m_graphCacheMappings"),
            toroidal("m_graphCacheToroidal"),tagsIndex("m_graphCacheTileTags");
        terrainSrg->SetConstant(enabled,u32(0));terrainSrg->SetConstant(debug,u32(r_terrainGraphClipmapDebug));
        terrainSrg->SetConstant(toroidal,u32(r_terrainGraphClipmapIncremental));
        if(m_state) for(auto& layer:m_state->layers) for(auto& pass:layer.passes) if(pass) pass->Disable();
        if(!scene || !view || !material || !r_terrainGraphClipmaps) return;
        bool temporalReuse=false;
        const auto reuse=material->FindPropertyIndex(Name("terrain.clipmapTemporalReuse"));
        if(reuse.IsValid() && material->GetPropertyValue(reuse).Is<bool>()) temporalReuse=material->GetPropertyValue<bool>(reuse);
        // A temporal graph cannot be spread over frames without changing its look.
        // Its unchanged direct evaluator supplies current-frame material values.
        if(r_terrainGraphClipmapIncremental && !temporalReuse) return;
        Data::Asset<RPI::ShaderAsset> cacheShader;
        material->ForAllShaderItems([&](const Name&,const RPI::ShaderCollection::Item& item)
        {
            if(item.GetShaderAsset()->GetDrawListName()==Name("terrain-graph-cache")) cacheShader=item.GetShaderAsset();
            return true;
        });
        if(!cacheShader.IsReady()) return;
        auto pipeline=scene->GetDefaultRenderPipeline();if(!pipeline) return;
        auto forward=pipeline->FindFirstPass(Name("Forward"));if(!forward) return;
        if(m_state && (m_state->pipeline!=pipeline || m_state->material!=material || m_state->shader.GetId()!=cacheShader.GetId() ||
            m_state->incremental!=bool(r_terrainGraphClipmapIncremental) || m_state->nextTag>0xffff00)) Shutdown();
        if(!m_state)
        {
            m_state=AZStd::make_unique<State>();auto& state=*m_state;
            state.pipeline=pipeline;state.material=material;state.shader=cacheShader;state.incremental=r_terrainGraphClipmapIncremental;
            auto pool=RPI::ImageSystemInterface::Get()->GetSystemAttachmentPool();
            const auto clear=RHI::ClearValue::CreateVector4Float(0,0,0,0);
            for(u32 level=0;level<Levels;++level)
            {
                auto& layer=state.layers[level];
                for(u32 c=0;c<Channels;++c)
                {
                    auto descriptor=RHI::ImageDescriptor::Create2D(RHI::ImageBindFlags::Color|RHI::ImageBindFlags::ShaderRead,
                        Size,Size,c==2?RHI::Format::R32G32B32A32_FLOAT:RHI::Format::R16G16B16A16_FLOAT);
                    layer.images[c]=RPI::AttachmentImage::Create(*pool,descriptor,
                        Name(AZStd::string::format("GraphClipmap%p_%u_%u",static_cast<void*>(scene),level,c)),&clear);
                    if(!layer.images[c]) { Shutdown();return; }
                }
                for(u32 tile=0;tile<4;++tile)
                {
                    auto passTemplate=AZStd::make_shared<RPI::PassTemplate>();
                    passTemplate->m_name=Name(AZStd::string::format("GraphMaterialClipmap%u_%u",level,tile));
                    passTemplate->m_passClass=Name("RasterPass");
                    auto data=AZStd::make_shared<RPI::RasterPassData>();
                    data->m_drawListTag=Name("terrain-graph-cache");data->m_pipelineViewTag=forward->GetPipelineViewTag();
                    data->m_bindViewSrg=true;data->m_viewportAndScissorTargetOutputIndex=0;
                    data->m_passSrgShaderReference.m_assetId=cacheShader.GetId();passTemplate->m_passData=data;
                    for(u32 c=0;c<Channels;++c)
                    {
                        auto slot=Slot(Name(Slots[c]),RPI::PassSlotType::Output,RHI::ScopeAttachmentUsage::RenderTarget);
                        slot.m_loadStoreAction.m_loadAction=RHI::AttachmentLoadAction::Load;
                        slot.m_loadStoreAction.m_clearValue=clear;passTemplate->AddSlot(slot);
                    }
                    auto& pass=layer.passes[tile];
                    pass=aznew GraphCacheRasterPass(RPI::PassDescriptor(passTemplate->m_name,passTemplate),layer.images);
                    if(!pipeline->AddPassBefore(pass,Name("Forward"))) { Shutdown();return; }
                    pass->QueueForBuildAndInitialization();
                }
            }
        }
        auto& state=*m_state;
        auto camera=view->GetViewToWorldMatrix();camera.SetTranslation(Vector3::CreateZero());
        const auto change=material->GetCurrentChangeId();
        const bool contentChanged=state.dirty || state.change!=change;
        const bool geometryChanged=state.revision!=geometryRevision;
        const bool orientationChanged=!state.camera.IsClose(camera,1e-6f);
        const bool fullRefresh=contentChanged || geometryChanged || orientationChanged || !temporalReuse;
        const auto position=view->GetViewToWorldMatrix().GetTranslation();
        state.geometryPending|=geometryChanged;
        state.stationaryFrames=state.lastPosition.IsClose(position,0.0001f)
            ? AZStd::min(state.stationaryFrames+1,u32(8)):0;
        state.lastPosition=position;
        // Mesh publications during travel change CLOD, not material content. The sampling
        // height/normal checks reject those pixels until a budgeted refill after settling.
        const bool coherenceRefill=state.geometryPending && state.stationaryFrames>=8;
        if(coherenceRefill) state.geometryPending=false;
        const bool coverageChanged=orientationChanged || coherenceRefill;
        AZStd::array<GraphClipmaps::Cell,Levels> origins;
        for(u32 level=0;level<Levels;++level)
        {
            auto& layer=state.layers[level];
            if(layer.leasedPass)
            {
                if(layer.leasedPass->CompletedTicket()==layer.ticket)
                {
                    if(state.incremental) layer.schedule.Complete(layer.lease);
                    layer.initialized=true;
                }
                layer.leasedPass=nullptr;
            }
            const float scale=0.125f*std::pow(4.0f,float(level));
            const GraphClipmaps::Cell origin{int64_t(std::floor(position.GetX()/scale/16))*16-Size/2,
                int64_t(std::floor(position.GetY()/scale/16))*16-Size/2};
            origins[level]=origin;
            const Vector4 mapping(float(origin.x)*scale,float(origin.y)*scale,1.0f/(scale*Size),scale);
            if(state.incremental)
            {
                layer.schedule.Plan(origin,contentChanged,coverageChanged,state.nextTag);
                const auto& tags=layer.schedule.Tags();state.tags[level]=Vector4(float(tags[0]),float(tags[1]),float(tags[2]),float(tags[3]));
            }
            else layer.fullDirty|=fullRefresh || !state.mappings[level].IsClose(mapping,1e-6f);
            state.mappings[level]=mapping;
            for(u32 c=0;c<Channels;++c)
            { RHI::ShaderInputNameIndex input(Bindings[c]);terrainSrg->SetImage(input,layer.images[c],level); }
        }
        u32 budget=AZStd::max(u32(1),u32(r_terrainGraphClipmapBudgetTexels));
        u32 passBudget=AZStd::clamp(u32(r_terrainGraphClipmapBudgetPasses),u32(1),Levels);
        if(!state.incremental) { budget=Levels*Size*Size;passBudget=Levels; }
        const u32 start=state.incremental?state.cursor:0;
        for(u32 offset=0;offset<Levels && budget && passBudget;++offset)
        {
            const u32 level=(start+offset)%Levels;auto& layer=state.layers[level];
            if(layer.leasedPass) continue;
            GraphClipmaps::Region job;
            u32 slot=0;float tag=1;const auto mapping=state.mappings[level];
            Vector4 projection=mapping;
            if(state.incremental)
            {
                job=layer.schedule.Next(budget);if(!job.Area()) continue;
                const GraphClipmaps::Cell tile{GraphClipmaps::FloorDivide(job.x,Size),GraphClipmaps::FloorDivide(job.y,Size)};
                slot=GraphClipmaps::TileIndex(tile);tag=float(layer.schedule.Tag(tile));
                projection.SetX(float(tile.x*Size)*mapping.GetW());projection.SetY(float(tile.y*Size)*mapping.GetW());
            }
            else { if(!layer.fullDirty)continue;job={origins[level].x,origins[level].y,Size,Size}; }
            auto& pass=layer.passes[slot];if(!pass->CanSchedule()) continue;
            const GraphClipmaps::Region physical{state.incremental?GraphClipmaps::Wrap(job.x):0,
                state.incremental?GraphClipmaps::Wrap(job.y):0,job.width,job.height};
            layer.lease=job;layer.leasedPass=pass;layer.ticket=state.nextTicket++;
            pass->Configure(projection,tag,physical,terrainSrg,!state.incremental || !layer.initialized,layer.ticket);
            layer.fullDirty=false;budget-=u32(job.Area());--passBudget;diagnostics.texels+=job.Area();
            if(state.incremental) state.cursor=(level+1)%Levels;
        }
        bool ready=true;
        for(u32 level=0;level<Levels;++level)
        {
            auto& layer=state.layers[level];ready&=layer.initialized;
            if(state.incremental)diagnostics.pending+=layer.schedule.PendingArea();
            for(u32 tile=0;tile<4;++tile)
            {
                const auto& pass=layer.passes[tile];diagnostics.updates+=pass->Updates();
                if(r_terrainGraphClipmapDiagnostics)
                {
                    const auto timestamp=pass->GetLatestTimestampResult();const u64 tick=timestamp.GetTimestampBeginInTicks();
                    auto& previous=state.timestampTicks[level*4+tile];
                    if(tick && tick!=previous) { diagnostics.gpuNs+=timestamp.GetDurationInNanoseconds();++diagnostics.gpuSamples;previous=tick; }
                }
            }
        }
        auto filter=RPI::PassFilter::CreateWithPassClass<RPI::RasterPass>();
        filter.SetOwnerScene(scene);
        RPI::PassSystemInterface::Get()->ForEachPass(filter,[&](RPI::Pass* base)
        {
            auto* raster=azrtti_cast<RPI::RasterPass*>(base);
            if(!raster || raster->GetDrawListTag()!=RHI::RHISystemInterface::Get()->GetDrawListTagRegistry()->FindTag(Name("forward")))
                return RPI::PassFilterExecutionFlow::ContinueVisitingPasses;
            for(u32 level=0;level<Levels;++level) for(u32 c=0;c<Channels;++c)
            {
                Name slotName(AZStd::string::format("GraphCacheRead%u_%u",level,c));
                if(!raster->FindAttachmentBinding(slotName))
                {
                    auto slot=Slot(slotName,RPI::PassSlotType::Input,RHI::ScopeAttachmentUsage::Shader);
                    raster->AddAttachmentBinding(RPI::PassAttachmentBinding(slot));
                }
                auto* binding=raster->FindAttachmentBinding(slotName);
                const auto& image=state.layers[level].images[c];
                if(!binding->GetAttachment() || binding->GetAttachment()->GetAttachmentId()!=image->GetAttachmentId())
                {
                    // A direct imported binding releases its previous image on replacement.
                    // AttachImageToSlot is only for an empty slot and retains owned attachments.
                    RPI::Ptr<RPI::PassAttachment> attachment=aznew RPI::PassAttachment();
                    attachment->m_name=slotName;
                    attachment->m_path=image->GetAttachmentId();
                    attachment->m_lifetime=RHI::AttachmentLifetimeType::Imported;
                    attachment->m_descriptor=RHI::UnifiedAttachmentDescriptor(image->GetDescriptor());
                    attachment->m_importedResource=image;
                    attachment->m_ownerPass=raster;
                    binding->SetOriginalAttachment(attachment);
                }
            }
            return RPI::PassFilterExecutionFlow::ContinueVisitingPasses;
        });

        terrainSrg->SetConstantArray(mappingIndex,state.mappings);
        terrainSrg->SetConstantArray(tagsIndex,state.tags);
        terrainSrg->SetConstant(enabled,u32(ready));diagnostics.ready=ready;
        state.dirty=false;state.revision=geometryRevision;state.change=change;state.camera=camera;
    }
}
