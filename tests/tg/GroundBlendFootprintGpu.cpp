// Headless hardware raster test for ground/contact texture-mip agreement.
#define NOMINMAX
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("D3D failure "+std::to_string(unsigned(hr)));}
static ComPtr<ID3DBlob> Compile(const std::string& text,const char* entry,const char* target)
{
    ComPtr<ID3DBlob> code,error;
    auto hr=D3DCompile(text.data(),text.size(),nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr)){if(error)std::cerr<<static_cast<const char*>(error->GetBufferPointer());Check(hr);}
    return code;
}
int main(int argc,char** argv)
{
    try
    {
        if(argc<2 || argc>3)throw std::runtime_error("shader path and optional --horizon required");
        const bool horizon=argc==3 && std::string(argv[2])=="--horizon";
        std::ifstream input(argv[1]);const std::string text((std::istreambuf_iterator<char>(input)),{});
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL feature;
        Check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&feature,&context));
        auto vertex=Compile(text,"VS","vs_5_0");ComPtr<ID3D11VertexShader> vs;
        Check(device->CreateVertexShader(vertex->GetBufferPointer(),vertex->GetBufferSize(),nullptr,&vs));
        ComPtr<ID3D11PixelShader> ps[3];int pi=0;
        for(const char* name:{"GroundPS","LegacyPS","ContactPS"})
        {auto code=Compile(text,name,"ps_5_0");Check(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps[pi++]));}
        D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=16;cbDesc.Usage=D3D11_USAGE_DEFAULT;cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants;Check(device->CreateBuffer(&cbDesc,nullptr,&constants));
        context->VSSetConstantBuffers(0,1,constants.GetAddressOf());context->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        context->VSSetShader(vs.Get(),nullptr,0);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;rd.MultisampleEnable=TRUE;
        ComPtr<ID3D11RasterizerState> rasterizer;Check(device->CreateRasterizerState(&rd,&rasterizer));context->RSSetState(rasterizer.Get());
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.StencilEnable=FALSE;depth.DepthFunc=D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> depthState;Check(device->CreateDepthStencilState(&depth,&depthState));context->OMSetDepthStencilState(depthState.Get(),0);
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP;sd.MaxLOD=D3D11_FLOAT32_MAX;sd.ComparisonFunc=D3D11_COMPARISON_NEVER;
        ComPtr<ID3D11SamplerState> sampler;Check(device->CreateSamplerState(&sd,&sampler));context->PSSetSamplers(0,1,sampler.GetAddressOf());
        std::array<std::vector<float>,11> pixels;std::array<D3D11_SUBRESOURCE_DATA,11> data{};
        for(int mip=0;mip<11;++mip)
        {
            const unsigned size=1024u>>mip;pixels[mip].resize(size*size*4);
            for(size_t i=0;i<pixels[mip].size();i+=4){pixels[mip][i]=mip/10.f;pixels[mip][i+3]=1;}
            data[mip].pSysMem=pixels[mip].data();data[mip].SysMemPitch=size*4*sizeof(float);
        }
        D3D11_TEXTURE2D_DESC tex{};tex.Width=tex.Height=1024;tex.MipLevels=11;tex.ArraySize=1;tex.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        tex.SampleDesc.Count=1;tex.Usage=D3D11_USAGE_IMMUTABLE;tex.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;Check(device->CreateTexture2D(&tex,data.data(),&texture));
        ComPtr<ID3D11ShaderResourceView> srv;Check(device->CreateShaderResourceView(texture.Get(),nullptr,&srv));context->PSSetShaderResources(0,1,srv.GetAddressOf());
        unsigned cases=0;float largestLegacyError=0,largestCorrectedError=0;
        for(auto size:{std::array<UINT,2>{1280,720},{1600,900},{1920,1080},{2560,1440}})
        for(UINT samples:{1u,2u,4u})
        {
            UINT quality=0;Check(device->CheckMultisampleQualityLevels(DXGI_FORMAT_R32G32B32A32_FLOAT,samples,&quality));
            if(!quality)throw std::runtime_error("Requested MSAA unsupported");
            D3D11_TEXTURE2D_DESC td{};td.Width=size[0];td.Height=size[1];td.MipLevels=td.ArraySize=1;td.Format=tex.Format;
            td.SampleDesc.Count=samples;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> target;Check(device->CreateTexture2D(&td,nullptr,&target));
            ComPtr<ID3D11RenderTargetView> rtv;Check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
            td.SampleDesc.Count=1;td.BindFlags=0;ComPtr<ID3D11Texture2D> resolved;Check(device->CreateTexture2D(&td,nullptr,&resolved));
            td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> read;Check(device->CreateTexture2D(&td,nullptr,&read));
            D3D11_VIEWPORT viewport{0,0,float(size[0]),float(size[1]),0,1};context->RSSetViewports(1,&viewport);
            auto render=[&](int shader,float slope)
            {
                const float values[4]={slope,0,0,0};context->UpdateSubresource(constants.Get(),0,nullptr,values,0,0);
                const float clear[4]={0,0,0,0};context->ClearRenderTargetView(rtv.Get(),clear);context->OMSetRenderTargets(1,rtv.GetAddressOf(),nullptr);
                context->PSSetShader(ps[shader].Get(),nullptr,0);context->Draw(4,0);context->OMSetRenderTargets(0,nullptr,nullptr);
                if(samples>1)context->ResolveSubresource(resolved.Get(),0,target.Get(),0,tex.Format);
                else context->CopyResource(resolved.Get(),target.Get());
                context->CopyResource(read.Get(),resolved.Get());D3D11_MAPPED_SUBRESOURCE mapped{};Check(context->Map(read.Get(),0,D3D11_MAP_READ,0,&mapped));
                std::vector<float> out(size[0]*size[1]*4);
                for(UINT y=0;y<size[1];++y)std::copy_n(reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch),size[0]*4,out.data()+y*size[0]*4);
                context->Unmap(read.Get(),0);return out;
            };
            if(horizon)
            {
                const auto legacy=render(1,-1),fixed=render(2,-1);
                auto rowMean=[&](const std::vector<float>& image,UINT y)
                {
                    float sum=0;UINT count=0;
                    for(UINT x=size[0]/4;x<3*size[0]/4;++x)
                    {
                        const size_t at=(size_t(y)*size[0]+x)*4;
                        if(image[at+3]<.99f)throw std::runtime_error("Incomplete horizon coverage");
                        if(!std::isfinite(image[at]) || !std::isfinite(image[at+1]))throw std::runtime_error("Non-finite horizon sample");
                        sum+=image[at];++count;
                    }
                    return sum/count;
                };
                auto excess=[&](const std::vector<float>& image)
                {
                    const UINT middle=size[1]/2,offset=size[1]/16;
                    const float center=.5f*(rowMean(image,middle-1)+rowMean(image,middle));
                    return std::abs(center-.5f*(rowMean(image,middle-offset)+rowMean(image,middle+offset)));
                };
                const float oldError=excess(legacy),newError=excess(fixed);
                largestLegacyError=std::max(largestLegacyError,oldError);
                largestCorrectedError=std::max(largestCorrectedError,newError);++cases;
                if(newError>.01f)throw std::runtime_error("Corrected horizon stripe remains: "+std::to_string(newError));
                continue;
            }
            const auto ground=render(0,0);
            for(float slope:{1.f,3.f})
            {
                auto legacy=render(1,slope),fixed=render(2,slope);float oldError=0,newError=0;
                size_t contacts=0;
                for(size_t i=0;i<ground.size();i+=4)
                    // Agreement is required at the physical contact. Extending
                    // full ray/plane projection up the slope causes a horizon pole.
                    if(ground[i+3]>.01f && fixed[i+3]>.01f && legacy[i+3]>.01f &&
                        fixed[i+1]/fixed[i+3]<.02f)
                    {
                        ++contacts;
                        oldError=std::max(oldError,std::abs(ground[i]/ground[i+3]-legacy[i]/legacy[i+3]));
                        newError=std::max(newError,std::abs(ground[i]/ground[i+3]-fixed[i]/fixed[i+3]));
                    }
                if(!contacts)throw std::runtime_error("Fixture did not cover the ground contact");
                largestLegacyError=std::max(largestLegacyError,oldError);largestCorrectedError=std::max(largestCorrectedError,newError);++cases;
                if(newError>.003f)throw std::runtime_error("Corrected ground/contact mips differ: "+std::to_string(newError));
            }
        }
        if(largestLegacyError<.005f)throw std::runtime_error("Fixture did not reproduce the original mip mismatch");
        std::cout<<"{\"cases\":"<<cases<<",\"legacy_max_error\":"<<largestLegacyError<<",\"corrected_max_error\":"<<largestCorrectedError<<",\"passed\":true}\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
