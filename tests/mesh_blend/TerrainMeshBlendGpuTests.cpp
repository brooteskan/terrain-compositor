// Executes the production UV metric and (when present) contact-cache decoder.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
using V=std::array<float,4>;
static void Check(HRESULT h) { if(FAILED(h)) throw std::runtime_error("D3D11 failure"); }
static void Require(bool b,const char* why) { if(!b) throw std::runtime_error(why); }
static std::string Read(const std::string& p) { std::ifstream f(p); Require(bool(f),p.c_str()); std::ostringstream s;s<<f.rdbuf();return s.str(); }
int main(int argc,char** argv) try {
    Require(argc==2,"Pass compositor repository root");
    std::string root=argv[1], assets=root+"/integrations/o3de/Assets/";
    std::string source=Read(assets+"ShaderLib/TerrainCompositor/TerrainMeshUvMetric.azsli");
    std::ifstream cacheFile(assets+"Shaders/Terrain/TerrainGraphClipmapSampling.azsli");
    const bool cache=bool(cacheFile);
    if(cache) {
        auto text=Read(assets+"Shaders/Terrain/TerrainGraphClipmapSampling.azsli");
        auto begin=text.find("float TC_MeshContactCacheWeight(");
        auto end=text.find("TerrainSurfaceChannels TC_ResolveGraphClipmap",begin);
        Require(begin!=std::string::npos && end!=std::string::npos,"Missing contact decoder");
        auto decoder=text.substr(begin,end-begin);
        for(auto p=decoder.find("TerrainMaterialSrg::");p!=std::string::npos;p=decoder.find("TerrainMaterialSrg::")) decoder.erase(p,20);
        source+="\nStructuredBuffer<uint> m_surfaceWords:register(t1);\n"+decoder;
    } else source+="\nfloat TC_MeshContactCacheWeight(float2 xy) { return 1; }\n";
    source+=R"(
StructuredBuffer<float4> cases:register(t0);
RWStructuredBuffer<float4> output:register(u0);
[numthreads(1,1,1)] void Main(uint3 id:SV_DispatchThreadID) {
    uint i=id.x*6; float3 ray;
    bool valid=TC_AtlasRay(cases[i].xyz,cases[i+1].xyz,cases[i+2].xyz,cases[i+3].xy,cases[i+4].xy,cases[i+5].xyz,ray);
    output[id.x]=float4(ray,valid);
}
[numthreads(1,1,1)] void Contact(uint3 id:SV_DispatchThreadID) { output[id.x]=TC_MeshContactCacheWeight(cases[id.x].xy); }
[numthreads(1,1,1)] void Bound(uint3 id:SV_DispatchThreadID) {
    float4 config=cases[id.x*2+1];
    float3 ray=TC_BoundAtlasRay(cases[id.x*2].xyz,config.x,config.y,config.zw);
    float travel=max(abs(config.y),abs(config.x+config.y));
    float2 extent=abs(ray.xy)*travel/max(ray.z,1e-12);
    output[id.x]=float4(ray,max(0,max(extent.x-config.z,extent.y-config.w)));
}
)";
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    auto compile=[&](const char* entry) {
        ComPtr<ID3DBlob> code,error;
        auto hr=D3DCompile(source.data(),source.size(),"MeshBlend",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
        if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"compile");
        ComPtr<ID3D11ComputeShader> shader;Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));return shader;
    };
    auto buffer=[&](unsigned bytes,unsigned stride,unsigned flags,const void* data) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.StructureByteStride=stride;d.BindFlags=flags;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        D3D11_SUBRESOURCE_DATA init{};init.pSysMem=data;ComPtr<ID3D11Buffer>b;Check(device->CreateBuffer(&d,data?&init:nullptr,&b));return b;
    };
    std::vector<V> input,expected;
    // Same physical plane at different pixel footprints, atlas densities and
    // mirrored/sheared packings. Expected values come from analytic gradients.
    for(float footprint:{.001f,.1f,1.f,100.f}) for(float density:{.001f,.02f,.25f,1.f}) for(float mirror:{-1.f,1.f}) {
        for(V row:std::vector<V>{{0,0,1,0},{footprint,0,0,0},{0,footprint,0,0},
            {density*footprint*mirror,.3f*density*footprint,0,0},{.2f*density*footprint,density*footprint,0,0},{2,3,4,0}}) input.push_back(row);
        expected.push_back({density*(mirror*2+.2f*3),density*(.3f*2+3),4,1});
    }
    // A vertical plane uses world X/Z, independent of the imported tangent basis.
    for(V row:std::vector<V>{{0,-1,0,0},{2,0,0,0},{0,0,3,0},{.1f,0,0,0},{0,.3f,0,0},{2,-4,3,0}}) input.push_back(row);
    expected.push_back({.1f,.3f,4,1});
    for(V row:std::vector<V>{{0,0,1,0},{0,0,0,0},{0,1,0,0},{1,0,0,0},{0,1,0,0},{2,3,4,0}}) input.push_back(row);
    expected.push_back({0,0,1,0});
    auto in=buffer(unsigned(input.size()*sizeof(V)),sizeof(V),D3D11_BIND_SHADER_RESOURCE,input.data());
    auto out=buffer(unsigned(expected.size()*sizeof(V)),sizeof(V),D3D11_BIND_UNORDERED_ACCESS,nullptr);
    ComPtr<ID3D11ShaderResourceView> iv;Check(device->CreateShaderResourceView(in.Get(),nullptr,&iv));
    ComPtr<ID3D11UnorderedAccessView> ov;Check(device->CreateUnorderedAccessView(out.Get(),nullptr,&ov));
    D3D11_BUFFER_DESC rd{};out->GetDesc(&rd);rd.BindFlags=rd.MiscFlags=rd.StructureByteStride=0;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> read;Check(device->CreateBuffer(&rd,nullptr,&read));
    auto* ip=iv.Get();auto* op=ov.Get();context->CSSetShaderResources(0,1,&ip);context->CSSetUnorderedAccessViews(0,1,&op,nullptr);
    auto shader=compile("Main");context->CSSetShader(shader.Get(),nullptr,0);context->Dispatch(unsigned(expected.size()),1,1);
    auto verify=[&](const std::vector<V>& wanted) {
        context->CopyResource(read.Get(),out.Get());D3D11_MAPPED_SUBRESOURCE m{};Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&m));
        auto* actual=static_cast<const V*>(m.pData);
        for(size_t i=0;i<wanted.size();++i) for(unsigned j=0;j<4;++j) Require(std::isfinite(actual[i][j]) && std::abs(actual[i][j]-wanted[i][j])<1e-5f,"GPU/reference mismatch");
        context->Unmap(read.Get(),0);
    };
    verify(expected);
    std::vector<V> boundInput(input.size()),boundExpected;
    for(unsigned i=0;i<32;++i) {
        const float x=(i%2?-1.f:1.f)*(1+i),y=(i%3?-1.f:1.f)*(.5f+i),z=i%2?1e-5f:2.f;
        const float scale=.16f*(1+i%3),offset=i%3==0?-.08f:(i%3==1?0.f:.1f),limit=.005f;
        boundInput[i*2]={x,y,z,0};boundInput[i*2+1]={scale,offset,limit,limit};
        const float travel=std::max(std::abs(offset),std::abs(scale+offset));
        const float factor=std::min(1.f,limit*z/(std::max(std::abs(x),std::abs(y))*travel));
        boundExpected.push_back({x*factor,y*factor,z,0});
    }
    context->UpdateSubresource(in.Get(),0,nullptr,boundInput.data(),0,0);
    auto bounded=compile("Bound");context->CSSetShader(bounded.Get(),nullptr,0);context->Dispatch(32,1,1);verify(boundExpected);
    if(cache) {
        auto contact=compile("Contact");context->CSSetShader(contact.Get(),nullptr,0);
        std::vector<unsigned> words(10);words[1]=1;
        auto put=[&](unsigned at,float value){std::memcpy(&words[at],&value,4);};
        put(2,10);put(3,20);put(4,30);put(5,40);words[6]=1;words[7]=0x54434d42;words[8]=0;words[9]=0x54434f31;
        auto packed=buffer(unsigned(words.size()*4),4,D3D11_BIND_SHADER_RESOURCE,words.data());
        ComPtr<ID3D11ShaderResourceView> pv;Check(device->CreateShaderResourceView(packed.Get(),nullptr,&pv));auto* pp=pv.Get();context->CSSetShaderResources(1,1,&pp);
        std::vector<V> positions(input.size());positions[0]={20,30,0,0};positions[1]={30,40,0,0};positions[2]={32,40,0,0};positions[3]={34,40,0,0};positions[4]={0,0,0,0};
        context->UpdateSubresource(in.Get(),0,nullptr,positions.data(),0,0);context->Dispatch(5,1,1);
        verify({V{0,0,0,0},V{0,0,0,0},V{.5f,.5f,.5f,.5f},V{1,1,1,1},V{1,1,1,1}});
        // Legacy/malformed footer retains the prior cache path.
        words[7]=0;context->UpdateSubresource(packed.Get(),0,nullptr,words.data(),0,0);context->Dispatch(1,1,1);verify({V{1,1,1,1}});
    }
    std::cout<<expected.size()<<" UV-metric cases and 32 bounded-search cases passed; contact decoder "<<(cache?"passed":"not present in this revision")<<".\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
