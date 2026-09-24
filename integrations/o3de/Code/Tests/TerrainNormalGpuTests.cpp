#include <AzTest/AzTest.h>
#include <cmath>

#if defined(AZ_PLATFORM_WINDOWS)
#include <AzCore/PlatformIncl.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <fstream>
#include <sstream>
#include <vector>

namespace TerrainCompositor
{
    TEST(TerrainNormalGpuTests, SharedShaderDecodesPackedNormalsAndClodBlendsOnD3D11)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        auto status = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(status))
            status = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &device, nullptr, &context);
        ASSERT_TRUE(SUCCEEDED(status));

        std::ifstream file(TERRAIN_COMPOSITOR_SHADER_DIR "/TerrainNormal.azsli");
        ASSERT_TRUE(file.is_open());
        std::ostringstream helper;
        helper << file.rdbuf();
        // Compile the exact helper used by terrain's forward/depth/shadow vertex shader.
        const std::string source = helper.str() + R"(
StructuredBuffer<float4> packedNormals : register(t0);
RWStructuredBuffer<float4> normals : register(u0);
[numthreads(64, 1, 1)] void Main(uint3 id : SV_DispatchThreadID)
{
    uint count, stride; packedNormals.GetDimensions(count, stride);
    if (id.x < count * 5)
    {
        float4 packed = packedNormals[id.x / 5];
        float blend = (id.x % 5) * 0.25;
        normals[id.x] = float4(DecodeTerrainNormal(lerp(packed.xy, packed.zw, blend)), 0);
    }
})";
        ComPtr<ID3DBlob> bytecode, errors;
        status = D3DCompile(source.data(), source.size(), "TerrainNormal.azsli", nullptr, nullptr, "Main", "cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
        ASSERT_TRUE(SUCCEEDED(status)) << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Compile failed");
        ComPtr<ID3D11ComputeShader> shader;
        ASSERT_TRUE(SUCCEEDED(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader)));

        using Float4 = std::array<float, 4>;
        std::vector<Float4> inputs;
        for (int x = -127; x <= 127; ++x)
            for (int y = -127; y <= 127; ++y)
                inputs.push_back({ x / 127.0f, y / 127.0f, -y / 127.0f, x / 127.0f });
        const size_t outputCount = inputs.size() * 5;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = UINT(inputs.size() * sizeof(Float4));
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(Float4);
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = inputs.data();
        ComPtr<ID3D11Buffer> inputBuffer, outputBuffer, readback;
        ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&desc, &initial, &inputBuffer)));
        desc.ByteWidth = UINT(outputCount * sizeof(Float4));
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &outputBuffer)));
        ComPtr<ID3D11ShaderResourceView> inputView;
        ComPtr<ID3D11UnorderedAccessView> outputView;
        ASSERT_TRUE(SUCCEEDED(device->CreateShaderResourceView(inputBuffer.Get(), nullptr, &inputView)));
        ASSERT_TRUE(SUCCEEDED(device->CreateUnorderedAccessView(outputBuffer.Get(), nullptr, &outputView)));
        auto* inputBinding = inputView.Get();
        auto* outputBinding = outputView.Get();
        context->CSSetShader(shader.Get(), nullptr, 0);
        context->CSSetShaderResources(0, 1, &inputBinding);
        context->CSSetUnorderedAccessViews(0, 1, &outputBinding, nullptr);
        context->Dispatch(UINT((outputCount + 63) / 64), 1, 1);

        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = desc.StructureByteStride = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&desc, nullptr, &readback)));
        context->CopyResource(readback.Get(), outputBuffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        ASSERT_TRUE(SUCCEEDED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
        const auto* output = static_cast<const Float4*>(mapped.pData);
        size_t mismatches = 0;
        for (size_t i = 0; i < outputCount; ++i)
        {
            const auto& n = output[i];
            const auto& packed = inputs[i / 5];
            const double blend = (i % 5) * 0.25;
            const double x = packed[0] * (1 - blend) + packed[2] * blend;
            const double y = packed[1] * (1 - blend) + packed[3] * blend;
            const double radiusSquared = x * x + y * y;
            // Unit hemisphere inside the disk; radial projection onto its rim outside.
            const double length = radiusSquared > 1 ? std::sqrt(radiusSquared) : 1;
            const double z = radiusSquared < 1 ? std::sqrt(1 - radiusSquared) : 0;
            const double actualLength = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (!std::isfinite(actualLength) || std::abs(actualLength - 1) > 2e-6 ||
                std::abs(n[0] - x / length) > 0.0007 || std::abs(n[1] - y / length) > 0.0007 ||
                std::abs(n[2] - z) > 0.0007)
                ++mismatches;
        }
        context->Unmap(readback.Get(), 0);
        EXPECT_EQ(mismatches, 0) << "of " << outputCount << " packed normal/CLOD cases";
    }
    TEST(TerrainDepthGpuTests, DepthPrepassPreservesMsaaSamplesOnSlopes)
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        auto status = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context);
        if (FAILED(status))
            status = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &device, nullptr, &context);
        ASSERT_TRUE(SUCCEEDED(status));

        std::ifstream file(TERRAIN_COMPOSITOR_SHADER_DIR "/TerrainDepth.azsli");
        ASSERT_TRUE(file.is_open());
        std::ostringstream text;
        text << file.rdbuf();
        const std::string production = text.str();
        const auto inputBegin = production.find("struct VSDepthOutput");
        const auto inputEnd = production.find("VSDepthOutput MainVS");
        const auto outputBegin = production.find("struct PSDepthOutput");
        ASSERT_NE(inputBegin, std::string::npos);
        ASSERT_NE(inputEnd, std::string::npos);
        ASSERT_NE(outputBegin, std::string::npos);
        // Compile the production interface and fragment function. A simple cutout
        // keeps the test independent of the scene's mesh-cutout buffers.
        const std::string source =
            "bool TGMeshCutoutDiscardsTerrain(float3 p) { return p.x > 0.25 && p.y > 0.25; }\n" +
            production.substr(inputBegin, inputEnd - inputBegin) + production.substr(outputBegin) + R"(
cbuffer Plane : register(b0) { float2 slope; float2 padding; };
VSDepthOutput TestVS(uint id : SV_VertexID)
{
    float2 xy = float2((id & 1) ? 1.0 : -1.0, (id & 2) ? 1.0 : -1.0);
    VSDepthOutput o;
    o.m_position = float4(xy, 0.5 + dot(xy, slope), 1.0);
    o.m_worldPosition = o.m_position.xyz;
    return o;
}
// Independent oracle: fixed-function depth retains each covered sample's depth.
void ReferencePS(VSDepthOutput input)
{
    if (TGMeshCutoutDiscardsTerrain(input.m_worldPosition)) discard;
}
struct ProbeInput { float4 position : SV_Position; float3 worldPosition : UV0; };
uint ProbePS(ProbeInput input) : SV_Target0
{
    if (TGMeshCutoutDiscardsTerrain(input.worldPosition)) discard;
    return 1;
}
Texture2DMS<uint> coverage : register(t0);
RWStructuredBuffer<uint> samples : register(u0);
[numthreads(8, 8, 1)] void ReadSamples(uint3 id : SV_DispatchThreadID)
{
    uint width, height, count;
    coverage.GetDimensions(width, height, count);
    if (id.x < width && id.y < height)
        for (uint i = 0; i < count; ++i)
            samples[(id.y * width + id.x) * count + i] = coverage.Load(id.xy, i);
})";
        auto compile = [&](const char* entry, const char* profile)
        {
            ComPtr<ID3DBlob> code, errors;
            const auto hr = D3DCompile(source.data(), source.size(), "TerrainDepth.azsli", nullptr, nullptr,
                entry, profile, D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            EXPECT_TRUE(SUCCEEDED(hr)) << (errors ? static_cast<const char*>(errors->GetBufferPointer()) : entry);
            return code;
        };
        auto vsCode = compile("TestVS", "vs_5_0");
        auto psCode = compile("MainPS", "ps_5_0");
        auto referenceCode = compile("ReferencePS", "ps_5_0");
        auto probeCode = compile("ProbePS", "ps_5_0");
        auto readCode = compile("ReadSamples", "cs_5_0");
        ASSERT_TRUE(vsCode && psCode && referenceCode && probeCode && readCode);
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps, reference, probe;
        ComPtr<ID3D11ComputeShader> read;
        ASSERT_TRUE(SUCCEEDED(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs)));
        ASSERT_TRUE(SUCCEEDED(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps)));
        ASSERT_TRUE(SUCCEEDED(device->CreatePixelShader(referenceCode->GetBufferPointer(), referenceCode->GetBufferSize(), nullptr, &reference)));
        ASSERT_TRUE(SUCCEEDED(device->CreatePixelShader(probeCode->GetBufferPointer(), probeCode->GetBufferSize(), nullptr, &probe)));
        ASSERT_TRUE(SUCCEEDED(device->CreateComputeShader(readCode->GetBufferPointer(), readCode->GetBufferSize(), nullptr, &read)));
        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = rasterDesc.MultisampleEnable = true;
        ComPtr<ID3D11RasterizerState> raster;
        ASSERT_TRUE(SUCCEEDED(device->CreateRasterizerState(&rasterDesc, &raster)));
        context->RSSetState(raster.Get());
        constexpr UINT Size = 32;
        const D3D11_VIEWPORT viewport{ 0, 0, float(Size), float(Size), 0, 1 };
        context->RSSetViewports(1, &viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        context->VSSetShader(vs.Get(), nullptr, 0);
        D3D11_DEPTH_STENCIL_DESC depthStateDesc{};
        depthStateDesc.DepthEnable = true;
        depthStateDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
        depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        ComPtr<ID3D11DepthStencilState> writeDepth, testDepth;
        ASSERT_TRUE(SUCCEEDED(device->CreateDepthStencilState(&depthStateDesc, &writeDepth)));
        depthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ASSERT_TRUE(SUCCEEDED(device->CreateDepthStencilState(&depthStateDesc, &testDepth)));
        D3D11_BUFFER_DESC constantsDesc{};
        constantsDesc.ByteWidth = 16;
        constantsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> constants;
        ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&constantsDesc, nullptr, &constants)));
        auto* constantsBinding = constants.Get();
        context->VSSetConstantBuffers(0, 1, &constantsBinding);

        for (UINT count : { 1u, 2u, 4u })
        {
            SCOPED_TRACE(::testing::Message() << "samples " << count);
            D3D11_TEXTURE2D_DESC imageDesc{};
            imageDesc.Width = imageDesc.Height = Size;
            imageDesc.MipLevels = imageDesc.ArraySize = 1;
            imageDesc.SampleDesc.Count = count;
            imageDesc.Format = DXGI_FORMAT_D32_FLOAT;
            imageDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            ComPtr<ID3D11Texture2D> depth, color;
            ComPtr<ID3D11DepthStencilView> depthView;
            ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&imageDesc, nullptr, &depth)));
            ASSERT_TRUE(SUCCEEDED(device->CreateDepthStencilView(depth.Get(), nullptr, &depthView)));
            imageDesc.Format = DXGI_FORMAT_R32_UINT;
            imageDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11RenderTargetView> colorView;
            ComPtr<ID3D11ShaderResourceView> colorRead;
            ASSERT_TRUE(SUCCEEDED(device->CreateTexture2D(&imageDesc, nullptr, &color)));
            ASSERT_TRUE(SUCCEEDED(device->CreateRenderTargetView(color.Get(), nullptr, &colorView)));
            ASSERT_TRUE(SUCCEEDED(device->CreateShaderResourceView(color.Get(), nullptr, &colorRead)));
            D3D11_BUFFER_DESC bufferDesc{};
            bufferDesc.ByteWidth = Size * Size * count * sizeof(uint32_t);
            bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bufferDesc.StructureByteStride = sizeof(uint32_t);
            ComPtr<ID3D11Buffer> buffer, staging;
            ComPtr<ID3D11UnorderedAccessView> bufferView;
            ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&bufferDesc, nullptr, &buffer)));
            ASSERT_TRUE(SUCCEEDED(device->CreateUnorderedAccessView(buffer.Get(), nullptr, &bufferView)));
            bufferDesc.BindFlags = bufferDesc.MiscFlags = bufferDesc.StructureByteStride = 0;
            bufferDesc.Usage = D3D11_USAGE_STAGING;
            bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ASSERT_TRUE(SUCCEEDED(device->CreateBuffer(&bufferDesc, nullptr, &staging)));
            for (auto slope : { std::array<float, 4>{0, 0, 0, 0}, {0.125f, 0.0625f, 0, 0}, {-0.125f, 0.125f, 0, 0} })
            {
                SCOPED_TRACE(::testing::Message() << "slope " << slope[0] << ", " << slope[1]);
                context->UpdateSubresource(constants.Get(), 0, nullptr, slope.data(), 0, 0);
                std::vector<uint32_t> expected;
                for (auto* prepass : { reference.Get(), ps.Get() })
                {
                    ID3D11ShaderResourceView* noRead = nullptr;
                    context->CSSetShaderResources(0, 1, &noRead);
                    const float zero[4]{};
                    context->ClearRenderTargetView(colorView.Get(), zero);
                    context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 0, 0);
                    context->OMSetRenderTargets(0, nullptr, depthView.Get());
                    context->OMSetDepthStencilState(writeDepth.Get(), 0);
                    context->PSSetShader(prepass, nullptr, 0);
                    context->Draw(4, 0);
                    auto* target = colorView.Get();
                    context->OMSetRenderTargets(1, &target, depthView.Get());
                    context->OMSetDepthStencilState(testDepth.Get(), 0);
                    context->PSSetShader(probe.Get(), nullptr, 0);
                    context->Draw(4, 0);
                    context->OMSetRenderTargets(0, nullptr, nullptr);
                    context->CSSetShader(read.Get(), nullptr, 0);
                    auto* input = colorRead.Get();
                    auto* output = bufferView.Get();
                    context->CSSetShaderResources(0, 1, &input);
                    context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
                    context->Dispatch(Size / 8, Size / 8, 1);
                    context->CopyResource(staging.Get(), buffer.Get());
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    ASSERT_TRUE(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
                    const auto* samples = static_cast<const uint32_t*>(mapped.pData);
                    std::vector<uint32_t> actual(samples, samples + Size * Size * count);
                    context->Unmap(staging.Get(), 0);
                    if (prepass == reference.Get())
                    {
                        expected = std::move(actual);
                        size_t invalidReferenceSamples = 0;
                        for (UINT y = 0; y < Size; ++y)
                            for (UINT x = 0; x < Size; ++x)
                                for (UINT sample = 0; sample < count; ++sample)
                                {
                                    // The full-screen plane has one pixel-aligned
                                    // rectangular cutout in its upper-right corner.
                                    const uint32_t covered = x >= 20 && y < 12 ? 0u : 1u;
                                    invalidReferenceSamples += expected[(y * Size + x) * count + sample] != covered;
                                }
                        ASSERT_EQ(invalidReferenceSamples, 0) << "invalid coverage oracle/readback";
                    }
                    else EXPECT_EQ(actual, expected) << "depth prepass lost forward-pass MSAA coverage";
                }
            }
        }
    }
}
#endif
