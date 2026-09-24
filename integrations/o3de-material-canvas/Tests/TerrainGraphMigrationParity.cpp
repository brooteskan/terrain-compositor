// Compare independently compiled, native-generated legacy and migrated graphs.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("D3D migration parity failure"); }
static std::string Read(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Missing native fixture: " + path.string());
    std::string result, line;
    while (std::getline(stream, line))
        if (line.find("#include") == std::string::npos && line.find("#pragma") == std::string::npos)
            result += line + "\n";
    return result;
}
static void Replace(std::string& text, const std::string& from, const std::string& to)
{
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
        text.replace(at, from.size(), to);
}
static ComPtr<ID3DBlob> Compile(const std::string& source, const char* entry, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    auto result = D3DCompile(source.data(), source.size(), "NativeMigrationParity", nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Compile failed");
    return code;
}
int main(int argc, char** argv)
try
{
    if (argc != 2) throw std::runtime_error("Pass the root containing fresh legacy/ and migrated/ native outputs");
    const std::filesystem::path root(argv[1]);
    std::string common = Read(TERRAIN_SHADERS "/TerrainSurface.azsli") +
        Read(CANVAS_ROOT "/Assets/ShaderLib/MaterialCanvas/Procedural/LatticeNoise.azsli") +
        Read(CANVAS_ROOT "/Assets/ShaderLib/MaterialCanvas/Procedural/NormalFromHeight.azsli");
    // Deterministic texture sample surrogate exercises UV/multiply migration;
    // real texture resource/include compilation is covered by Asset Processor.
    common += "float4 TC_TestTexture(float2 uv) { return float4(frac(uv), .25, 1); }\n";
    const std::string entry = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
float4 PS(float4 pixel : SV_Position) : SV_Target {
    TerrainSurfaceContext ctx;
    ctx.worldPosition = float3((pixel.xy - 48) * .37, pixel.y * .11 - 2);
    ctx.geometricNormal = normalize(float3(.2, .3, 1));
    TerrainSurfaceChannels incoming;
    incoming.baseColor = float3(.2,.4,.6); incoming.normal = normalize(float3(.3,.4,1));
    incoming.roughness = .7; incoming.metalness = .3;
    incoming.specularFactor = .45; incoming.ambientOcclusion = .8;
    TerrainSurfaceChannels value = TC_CanvasSurface(ctx, incoming);
    // Evaluate derivatives uniformly; each readback field occupies complete quads.
    uint field = uint(pixel.x) / 32;
    [flatten] if (field == 0) return float4(value.baseColor, value.roughness);
    [flatten] if (field == 1) return float4(value.normal, value.metalness);
    return float4(value.specularFactor, value.ambientOcclusion, 0, 1);
})";
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 96; desc.Height = 64; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output, readback;
    Check(device->CreateTexture2D(&desc, nullptr, &output));
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&desc, nullptr, &readback));
    ComPtr<ID3D11RenderTargetView> target;
    Check(device->CreateRenderTargetView(output.Get(), nullptr, &target));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = true;
    ComPtr<ID3D11RasterizerState> rasterState;
    Check(device->CreateRasterizerState(&raster, &rasterState));
    context->RSSetState(rasterState.Get());
    D3D11_VIEWPORT viewport{0, 0, 96, 64, 0, 1};
    context->RSSetViewports(1, &viewport);
    auto* view = target.Get(); context->OMSetRenderTargets(1, &view, nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto render = [&](const std::filesystem::path& path)
    {
        std::string source = common + Read(path) + entry;
        Replace(source, "TerrainMaterialSrg::m_canvasTintStrength", "0.37");
        Replace(source, "TerrainMaterialSrg::m_canvasTintColor", "float3(.7,.85,.55)");
        Replace(source, "TerrainMaterialSrg::m_canvasTintTexture.Sample(TerrainMaterialSrg::m_canvasTintSampler, ", "TC_TestTexture(");
        auto vertexCode = Compile(source, "VS", "vs_5_0");
        auto pixelCode = Compile(source, "PS", "ps_5_0");
        ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
        Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vs));
        Check(device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &ps));
        context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
        context->Draw(3, 0); context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE data{};
        Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &data));
        std::vector<float> pixels(96 * 64 * 4);
        for (unsigned y = 0; y < 64; ++y)
        {
            auto row = reinterpret_cast<const float*>(static_cast<const char*>(data.pData) + y * data.RowPitch);
            std::copy(row, row + 96 * 4, pixels.begin() + y * 96 * 4);
        }
        context->Unmap(readback.Get(), 0);
        return pixels;
    };
    float maximum = 0;
    for (const char* name : {"minimal_tint", "procedural_tint", "texture_tint", "world_bands", "surface_passthrough",
            "surface_channels", "wet_terrain", "procedural_ground", "procedural_normal", "slope_elevation"})
    {
        const auto filename = std::string(name) + "_Tint.azsli";
        auto legacy = render(root / "legacy" / filename);
        auto migrated = render(root / "migrated" / filename);
        float error = 0;
        for (size_t i = 0; i < legacy.size(); ++i)
        {
            if (!std::isfinite(legacy[i]) || !std::isfinite(migrated[i])) throw std::runtime_error("Non-finite output");
            error = std::max(error, std::abs(legacy[i] - migrated[i]));
        }
        maximum = std::max(maximum, error);
        std::cout << name << ": maximum error " << error << '\n';
    }
    const float n = 1.0f / std::sqrt(1.25f);
    for (const auto& probe : {
            std::pair<const char*, std::array<float, 12>>{"disconnected", {.2f,.4f,.6f,.7f, .3f*n,.4f*n,n,.3f, .45f,.8f,0,1}},
            std::pair<const char*, std::array<float, 12>>{"explicit_defaults", {1,1,1,1, 0,0,1,0, .5f,1,0,1}}})
    {
        auto pixels = render(root / "compiler" / (std::string(probe.first) + "_Tint.azsli"));
        float error = 0;
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 96; ++x)
                for (unsigned c = 0; c < 4; ++c)
                {
                    const float value = pixels[(y * 96 + x) * 4 + c];
                    if (!std::isfinite(value)) throw std::runtime_error("Non-finite compiler probe");
                    error = std::max(error, std::abs(value - probe.second[(x / 32) * 4 + c]));
                }
        maximum = std::max(maximum, error);
        std::cout << probe.first << ": maximum error " << error << '\n';
    }
    return maximum < .00001f ? 0 : 1;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
