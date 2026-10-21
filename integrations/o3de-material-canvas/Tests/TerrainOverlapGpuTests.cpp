// Executes the production overlap shader, including the packed-buffer decoder, on D3D11.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <thread>
#include <chrono>
using Microsoft::WRL::ComPtr;
using V = std::array<float, 4>;
static void Check(HRESULT h) { if (FAILED(h)) throw std::runtime_error("D3D11 failure"); }
static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static std::string Read(const char* path)
{
    std::ifstream file(path); Require(bool(file), path);
    std::string line, text;
    while (std::getline(file, line)) if (line.find("#include") == std::string::npos && line.find("#pragma") == std::string::npos) text += line + '\n';
    return text;
}
static float Ramp(float d, float w) { float t = std::clamp(d / std::max(w, 1e-6f), 0.0f, 1.0f); return t*t*(3-2*t); }
static V Reference(V h, V c, unsigned count, float width, bool retained)
{
    V w{};
    if (!(width > 0) || !std::isfinite(width) || count > 4) return w;
    for (unsigned i=0;i<count;++i) c[i] = std::isfinite(c[i]) && std::isfinite(h[i]) ? std::clamp(c[i],0.0f,1.0f) : 0;
    if (retained)
    {
        for(unsigned i=0;i<count;++i) { w[i]=c[i]; for(unsigned j=0;j<count;++j)
            if(i!=j && c[i]>0 && c[j]>0) w[i]*=1-c[j]*Ramp(h[j]-h[i],width); }
    }
    else
    {
        float previous=0,mass=0;
        for(unsigned i=0;i<count;++i) if(c[i]>0)
        {
            const float alpha=c[i]*(1-0.5f*(mass>0?1-Ramp(std::abs(h[i]-previous),width):0));
            for(auto& v:w) v*=1-alpha;
            w[i]=alpha;
            const float next=mass*(1-alpha)+alpha;
            previous=(previous*mass*(1-alpha)+h[i]*alpha)/std::max(next,1e-20f); mass=next;
        }
    }
    float sum=0; for(auto v:w) sum+=v;
    if(sum>1e-20f) for(auto& v:w) v/=sum;
    return w;
}
int main(int argc, char**) try
{
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    std::vector<V> inputs,expected;
    auto add=[&](V h,V c,float width,bool retained,unsigned count=4) {
        inputs.push_back(h);inputs.push_back(c);inputs.push_back({width,float(retained),float(count),0});
        expected.push_back(Reference(h,c,count,width,retained)); };
    for(bool retained:{false,true})
    {
        add({0,0,0,0},{1,1,1,0},1,retained,3);
        add({-120,40,100,99},{1,1,1,1},0,retained);
        add({5,4,3,2},{1,1,1,1},1,retained,5);
        add({0,1,2,3},{0,0,0,0},1,retained);
        add({0,1,2,3},{1,1,1,1},std::numeric_limits<float>::infinity(),retained);
        add({0,std::numeric_limits<float>::quiet_NaN(),2,3},{1,1,1,1},1,retained);
        for(int i=0;i<=2000;++i)
        {
            const float t=float(i)/1000-1;
            add({20+t,20-t,20+0.1f,21},{1,1,0.7f,Ramp(t+0.5f,1)},0.75f,retained);
        }
    }
    auto source=Read(TERRAIN_SHADERS "/TerrainSurface.azsli")+Read(OVERLAP_SHADERS "/TerrainOverlapWeights.azsli");
    auto resolver=Read(OVERLAP_SHADERS "/TerrainOverlap.azsli");
    resolver.resize(resolver.find("float4 TC_SampleOverlapTexture"));
    source += "#define TC_OVERLAP_WORDS overlapWords\nStructuredBuffer<uint> overlapWords : register(t1);\n" + resolver;
    source += R"(
StructuredBuffer<float4> cases : register(t0);
RWStructuredBuffer<float4> output : register(u0);
[numthreads(64,1,1)] void Main(uint3 id : SV_DispatchThreadID) {
    uint n, stride; cases.GetDimensions(n,stride); if(id.x >= n/3) return;
    float4 config=cases[id.x*3+2];
    output[id.x]=TC_OverlapWeights(cases[id.x*3],cases[id.x*3+1],(uint)config.z,config.x,config.y!=0);
}
[numthreads(1,1,1)] void Decode(uint3 id : SV_DispatchThreadID) {
    TerrainOverlapSet s=TC_ResolveOverlap(float3(3,4,987));
    output[0]=s.weights;
    output[1]=float4(s.sources[1].localPosition,s.sources[1].worldPosition.z,s.count);
    output[2]=float4(s.sources[1].normal,s.sources[1].coverage);
    TerrainSurfaceChannels m[4];
    for(uint i=0;i<4;++i) { m[i]=(TerrainSurfaceChannels)0; m[i].baseColor=float3(i,2*i,3*i); m[i].roughness=0.2*i; m[i].ambientOcclusion=1; }
    TerrainSurfaceChannels fallback=(TerrainSurfaceChannels)0; fallback.baseColor=float3(0.7,0.8,0.9);
    TerrainSurfaceContext c; c.worldPosition=float3(3,4,987); c.geometricNormal=float3(0,0,1);
    TerrainSurfaceChannels b=TC_BlendOverlap(s,m,fallback,c);
    output[3]=float4(b.baseColor,b.roughness);
    output[4]=float4(TC_OverlapNormal(s.sources[1],float3(1,0.5,0),1,true),1);
    output[5]=float4(TC_OverlapNormal(s.sources[1],float3(1,0.5,0),1,false),1);
}
[numthreads(64,1,1)] void Benchmark(uint3 id : SV_DispatchThreadID) {
    if(id.x>=1920*1080) return;
    float2 xy=float2(id.x%1920,id.x/1920)*0.01;
    TerrainOverlapSet s=TC_ResolveOverlap(float3(xy,0));
    output[id.x]=s.weights;
}
)";
    auto compile=[&](const char* entry) {
        ComPtr<ID3DBlob> code,error;
        const auto hr=D3DCompile(source.data(),source.size(),"TerrainOverlap",nullptr,nullptr,entry,"cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"compile");
        ComPtr<ID3D11ComputeShader> shader;Check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));return shader; };
    auto makeBuffer=[&](unsigned bytes,unsigned stride,unsigned flags,const void* data) {
        D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.StructureByteStride=stride;d.BindFlags=flags;d.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        D3D11_SUBRESOURCE_DATA init{};init.pSysMem=data;ComPtr<ID3D11Buffer> b;Check(device->CreateBuffer(&d,data?&init:nullptr,&b));return b; };
    auto in=makeBuffer(unsigned(inputs.size()*sizeof(V)),sizeof(V),D3D11_BIND_SHADER_RESOURCE,inputs.data());
    auto out=makeBuffer(unsigned(expected.size()*sizeof(V)),sizeof(V),D3D11_BIND_UNORDERED_ACCESS,nullptr);
    ComPtr<ID3D11ShaderResourceView> inView;Check(device->CreateShaderResourceView(in.Get(),nullptr,&inView));
    ComPtr<ID3D11UnorderedAccessView> outView;Check(device->CreateUnorderedAccessView(out.Get(),nullptr,&outView));
    D3D11_BUFFER_DESC rd{};out->GetDesc(&rd);rd.BindFlags=rd.MiscFlags=rd.StructureByteStride=0;rd.Usage=D3D11_USAGE_STAGING;rd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> read;Check(device->CreateBuffer(&rd,nullptr,&read));
    auto shader=compile("Main");context->CSSetShader(shader.Get(),nullptr,0);
    auto* iv=inView.Get();auto* ov=outView.Get();context->CSSetShaderResources(0,1,&iv);context->CSSetUnorderedAccessViews(0,1,&ov,nullptr);
    context->Dispatch(unsigned((expected.size()+63)/64),1,1);context->CopyResource(read.Get(),out.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));
    auto* actual=static_cast<const V*>(mapped.pData);
    for(size_t i=0;i<expected.size();++i) for(unsigned j=0;j<4;++j)
        Require(std::isfinite(actual[i][j]) && std::abs(actual[i][j]-expected[i][j])<2e-5f,"GPU weight/reference mismatch");
    // Interior smooth sweeps must not jump as a third/fourth source enters the set.
    for(size_t start:{size_t(6),size_t(2013)}) for(size_t i=start+1;i<start+2001;++i)
        for(unsigned j=0;j<4;++j) Require(std::abs(actual[i][j]-actual[i-1][j])<0.01f,"discontinuous contributor transition");
    context->Unmap(read.Get(),0);
    std::vector<unsigned> words(1+8+48+4+2);words[0]=1;words[5]=9;words[6]=2;
    auto put=[&](unsigned p,float f){std::memcpy(&words[p],&f,4);};
    put(1,-100);put(2,-100);put(3,100);put(4,100);put(7,2);words[8]=1;
    auto base=[&](unsigned p,unsigned slot){words[p+1]=slot;put(p+6,1);put(p+8,1);put(p+9,-100);put(p+10,-100);put(p+11,100);put(p+12,100);put(p+13,7);put(p+15,1);put(p+17,1);};
    base(9,0);base(33,1);words[33]=1;words[35]=words[36]=2;put(37,1);put(38,2);put(39,0);put(40,1);put(41,0.5f);
    put(42,-10);put(43,-10);put(44,10);put(45,10);words[52]=57;put(47,1);
    for(unsigned i=57;i<61;++i)put(i,0);words[61]=0;words[62]=0x54434f31;
    auto packed=makeBuffer(unsigned(words.size()*4),4,D3D11_BIND_SHADER_RESOURCE,words.data());
    ComPtr<ID3D11ShaderResourceView> packedView;Check(device->CreateShaderResourceView(packed.Get(),nullptr,&packedView));
    auto* pv=packedView.Get();context->CSSetShaderResources(1,1,&pv);
    auto decode=compile("Decode");context->CSSetShader(decode.Get(),nullptr,0);context->Dispatch(1,1,1);context->CopyResource(read.Get(),out.Get());
    Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));actual=static_cast<const V*>(mapped.pData);
    Require(std::abs(actual[0][0]-.5f)<1e-6 && std::abs(actual[0][1]-.5f)<1e-6,"equal-height normalized pair");
    Require(actual[1][0]==1 && actual[1][1]==-1 && actual[1][2]==7 && actual[1][3]==2,"source translation/rotation/scale decode");
    Require(actual[2][2]==1 && actual[2][3]==1,"source normal/coverage decode");
    Require(std::abs(actual[3][0]-.5f)<1e-6 && std::abs(actual[3][3]-.1f)<1e-6,"pre-lighting material channel blend");
    Require(actual[4][0]==0 && actual[4][1]==1 && actual[4][2]==0,"tangent normal rotates into world space");
    Require(actual[5][0]==0 && actual[5][1]==0 && actual[5][2]==1,"missing normal image falls back to source normal");
    context->Unmap(read.Get(),0);
    words.back()=0;context->UpdateSubresource(packed.Get(),0,nullptr,words.data(),0,0);
    context->Dispatch(1,1,1);context->CopyResource(read.Get(),out.Get());
    Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));actual=static_cast<const V*>(mapped.pData);
    Require(actual[0]==V{} && actual[3][0]==0.7f && actual[3][1]==0.8f && actual[3][2]==0.9f,"old/disabled publication preserves graph fallback");
    context->Unmap(read.Get(),0);
    // Nonplanar mesh cell: the two diagonal choices must reproduce different
    // triangles, and a gap must remove that material contributor altogether.
    words.resize(65);words[33]=2;words[53]=61;words[54]=62;
    put(55,0.05f);put(56,0.05f);put(60,4);words[61]=0;words[62]=0;
    words[63]=0;words[64]=0x54434f31;
    auto mesh=makeBuffer(unsigned(words.size()*4),4,D3D11_BIND_SHADER_RESOURCE,words.data());
    ComPtr<ID3D11ShaderResourceView> meshView;Check(device->CreateShaderResourceView(mesh.Get(),nullptr,&meshView));
    auto* mv=meshView.Get();context->CSSetShaderResources(1,1,&mv);
    for(unsigned variant=0;variant<3;++variant)
    {
        words[61]=variant==2?1:0;words[62]=variant==1?1:0;
        context->UpdateSubresource(mesh.Get(),0,nullptr,words.data(),0,0);
        context->Dispatch(1,1,1);context->CopyResource(read.Get(),out.Get());
        Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));actual=static_cast<const V*>(mapped.pData);
        if(variant<2) Require(std::abs(actual[1][2]-(variant==0?8.8f:7.0f))<1e-5f,"mesh diagonal triangle height");
        else Require(actual[2][3]==0 && actual[0][0]==1 && actual[0][1]==0,"mesh gap excludes material source");
        context->Unmap(read.Get(),0);
    }
    std::cout << "PASS " << expected.size() << " GPU/reference cases; continuous 3/4-way sweeps; packed source mapping, mesh gaps/diagonals and channel blend\n";
    if (argc > 1)
    {
        auto benchmark=compile("Benchmark");
        auto large=makeBuffer(1920*1080*sizeof(V),sizeof(V),D3D11_BIND_UNORDERED_ACCESS,nullptr);
        ComPtr<ID3D11UnorderedAccessView> largeView;Check(device->CreateUnorderedAccessView(large.Get(),nullptr,&largeView));
        auto* lv=largeView.Get();context->CSSetUnorderedAccessViews(0,1,&lv,nullptr);
        context->CSSetShader(benchmark.Get(),nullptr,0);
        for(unsigned count=0;count<=4;++count)
        {
            words.assign(9+4*24+4+2,0);words[0]=1;words[5]=9;words[6]=count;
            put(1,-100);put(2,-100);put(3,100);put(4,100);put(7,2);words[8]=1;
            for(unsigned i=0;i<count;++i)
            {
                const unsigned p=9+i*24;base(p,i);words[p]=1;words[p+2]=words[p+3]=2;
                put(p+13,float(i)*.2f);put(p+14,1);words[p+19]=105;
            }
            put(105,0);put(106,3);put(107,1);put(108,4);words[109]=0;words[110]=0x54434f31;
            auto fixture=makeBuffer(unsigned(words.size()*4),4,D3D11_BIND_SHADER_RESOURCE,words.data());
            ComPtr<ID3D11ShaderResourceView> fixtureView;Check(device->CreateShaderResourceView(fixture.Get(),nullptr,&fixtureView));
            auto* fv=fixtureView.Get();context->CSSetShaderResources(1,1,&fv);
            std::vector<double> times;
            for(unsigned run=0;run<12;++run)
            {
                ComPtr<ID3D11Query> disjoint,start,end;
                D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT,0};Check(device->CreateQuery(&q,&disjoint));
                q.Query=D3D11_QUERY_TIMESTAMP;Check(device->CreateQuery(&q,&start));Check(device->CreateQuery(&q,&end));
                context->Begin(disjoint.Get());context->End(start.Get());context->Dispatch((1920*1080+63)/64,1,1);
                context->End(end.Get());context->End(disjoint.Get());context->Flush();
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing{};UINT64 a=0,b=0;
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
                while(context->GetData(disjoint.Get(),&timing,sizeof(timing),0)==S_FALSE)
                { Require(std::chrono::steady_clock::now()<deadline,"GPU benchmark timeout");std::this_thread::yield(); }
                while(context->GetData(start.Get(),&a,sizeof(a),0)==S_FALSE)
                { Require(std::chrono::steady_clock::now()<deadline,"GPU start timestamp timeout");std::this_thread::yield(); }
                while(context->GetData(end.Get(),&b,sizeof(b),0)==S_FALSE)
                { Require(std::chrono::steady_clock::now()<deadline,"GPU end timestamp timeout");std::this_thread::yield(); }
                Require(b>=a,"GPU timestamps out of order");
                if(run && !timing.Disjoint) times.push_back(double(b-a)*1000.0/double(timing.Frequency));
            }
            Require(!times.empty(),"No valid GPU timestamps");std::sort(times.begin(),times.end());
            std::cout<<"BENCHMARK resolver-only 1920x1080 contributors="<<count<<" median_ms="<<times[times.size()/2]
                     <<" max_ms="<<times.back()<<" source_bytes="<<words.size()*4<<" output_bytes="<<1920*1080*sizeof(V)<<'\n';
        }
    }
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
