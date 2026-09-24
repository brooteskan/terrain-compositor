// Hardware pixel tests for native graph defaults and derivative-based normals.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;
static void Check(HRESULT code) { if (FAILED(code)) throw std::runtime_error("D3D surface test failure"); }
static std::string Read(const std::string& path)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Missing native shader: " + path);
    std::string line, result;
    while (std::getline(file, line))
        if (line.find("#include") == std::string::npos && line.find("#pragma") == std::string::npos)
            result += line + "\n";
    return result;
}
static ComPtr<ID3DBlob> Compile(const std::string& text, const char* entry, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    auto result = D3DCompile(text.data(), text.size(), "TerrainSurfaceTests", nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Shader compile failed");
    return code;
}
int main()
try
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 96; desc.Height = 32; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
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
    D3D11_VIEWPORT viewport{0, 0, 96, 32, 0, 1};
    context->RSSetViewports(1, &viewport);
    auto* view = target.Get();
    context->OMSetRenderTargets(1, &view, nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const auto common = Read(TERRAIN_SHADERS "/TerrainSurface.azsli") +
        Read(CANVAS_ROOT "/Assets/ShaderLib/MaterialCanvas/Procedural/LatticeNoise.azsli") +
        Read(CANVAS_ROOT "/Assets/ShaderLib/MaterialCanvas/Procedural/NormalFromHeight.azsli");
    const std::string vertex = R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
)";
    auto vertexCode = Compile(vertex, "VS", "vs_5_0");
    ComPtr<ID3D11VertexShader> vs;
    Check(device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &vs));
    context->VSSetShader(vs.Get(), nullptr, 0);

    struct Case { const char* name; std::string function; std::array<float, 12> expected; };
    const std::array<float, 12> incoming{.2f,.4f,.6f,.7f, 0,0,1,.3f, .45f,.8f,0,1};
    const float n = 1.0f / std::sqrt(1.25f);
    const float tiltedX = -.5f * n - .24f;
    const float tiltedZ = n - .12f;
    const float tiltedLength = std::sqrt(tiltedX * tiltedX + .16f + tiltedZ * tiltedZ);
    const float shadingLength = std::sqrt(.09f + .16f + 1.025f * 1.025f);
    const Case cases[]{
        {"native unconnected surface", Read(CANVAS_ROOT "/Assets/MaterialCanvas/Terrain/Examples/surface_passthrough_Tint.azsli"), incoming},
        {"native six connected channels", Read(CANVAS_ROOT "/Assets/MaterialCanvas/Terrain/Examples/surface_channels_Tint.azsli"),
            {.6f,.25f,.1f,.22f, .6f,0,.8f,.65f, .8f,.35f,0,1}},
        {"world-space derivative normal", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    s.normal = TC_NormalFromHeight(ctx.worldPosition, s.normal, 0.3 * ctx.worldPosition.x + 0.4 * ctx.worldPosition.y, 1);
    return s;
})", {.2f,.4f,.6f,.7f, -.3f*n,-.4f*n,n,.3f, .45f,.8f,0,1}},
        {"sloped position derivatives", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    float3 p = float3(ctx.worldPosition.xy, .5 * ctx.worldPosition.x);
    s.normal = TC_NormalFromHeight(p, normalize(float3(-.5,0,1)), .3*p.x + .4*p.y, 1);
    return s;
})", {.2f,.4f,.6f,.7f, tiltedX/tiltedLength,-.4f/tiltedLength,tiltedZ/tiltedLength,.3f, .45f,.8f,0,1}},
        {"ordinary vertical mesh", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    float3 p = float3(ctx.worldPosition.x, 0, ctx.worldPosition.y);
    s.normal = TC_NormalFromHeight(p, float3(0,1,0), .3*p.x + .4*p.z, 1);
    return s;
})", {.2f,.4f,.6f,.7f, -.3f*n,n,-.4f*n,.3f, .45f,.8f,0,1}},
        {"mirrored derivative orientation", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    float3 p = float3(-ctx.worldPosition.x, ctx.worldPosition.y, 0);
    s.normal = TC_NormalFromHeight(p, float3(0,0,1), .3*p.x + .4*p.y, 1);
    return s;
})", {.2f,.4f,.6f,.7f, -.3f*n,-.4f*n,n,.3f, .45f,.8f,0,1}},
        {"explicit composed shading normal", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    s.normal = TC_NormalFromHeight(ctx.worldPosition, float3(.6,0,.8), .3*ctx.worldPosition.x + .4*ctx.worldPosition.y, 1);
    return s;
})", {.2f,.4f,.6f,.7f, .3f/shadingLength,-.4f/shadingLength,1.025f/shadingLength,.3f, .45f,.8f,0,1}},
        {"constant height preserves base normal", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    s.normal = TC_NormalFromHeight(ctx.worldPosition, s.normal, 14.0, 1); return s;
})", incoming},
        {"invalid derivative height falls back", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    s.normal = TC_NormalFromHeight(ctx.worldPosition, s.normal, asfloat(0x7fc00000), 1); return s;
})", incoming},
        {"zero-strength normal", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    s.normal = TC_NormalFromHeight(ctx.worldPosition, s.normal, ctx.worldPosition.x, 0); return s;
})", incoming},
        {"degenerate derivatives", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    s.normal = TC_NormalFromHeight(float3(0,0,0), s.normal, ctx.worldPosition.x, 1); return s;
})", incoming},
        {"finite fallback and channel clamping", R"(
TerrainSurfaceChannels TC_CanvasSurface(TerrainSurfaceContext ctx, TerrainSurfaceChannels s) {
    TerrainSurfaceChannels v = s;
    v.baseColor = float3(1.7, 0.4, 0.6);
    v.normal = float3(0,0,0); v.roughness = -1; v.metalness = 2;
    v.specularFactor = asfloat(0x7fc00000); v.ambientOcclusion = -0.1;
    return TC_ValidateSurface(v, s);
})", {1.7f,.4f,.6f,0, 0,0,1,1, .45f,0,0,1}}
    };
    float maximum = 0;
    for (const auto& test : cases)
    {
        auto source = common + test.function + vertex + R"(
float4 PS(float4 pixel : SV_Position) : SV_Target {
    TerrainSurfaceContext ctx;
    ctx.worldPosition = float3(pixel.xy - 48, 0); ctx.geometricNormal = float3(0,0,1);
    TerrainSurfaceChannels s;
    s.baseColor = float3(.2,.4,.6); s.normal = float3(0,0,1); s.roughness = .7;
    s.metalness = .3; s.specularFactor = .45; s.ambientOcclusion = .8;
    s = TC_CanvasSurface(ctx, s);
    // Whole 2x2 quads select the same channel so shader derivatives remain defined.
    uint field = uint(pixel.x) / 32;
    [flatten] if (field == 0) return float4(s.baseColor, s.roughness);
    [flatten] if (field == 1) return float4(s.normal, s.metalness);
    return float4(s.specularFactor, s.ambientOcclusion, 0, 1);
})";
        auto code = Compile(source, "PS", "ps_5_0");
        ComPtr<ID3D11PixelShader> ps;
        Check(device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps));
        context->PSSetShader(ps.Get(), nullptr, 0);
        const float sentinel[]{100,100,100,100}; context->ClearRenderTargetView(view, sentinel);
        context->Draw(3, 0); context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE data{};
        Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &data));
        for (unsigned y = 0; y < 32; ++y)
        {
            auto row = reinterpret_cast<const float*>(static_cast<const char*>(data.pData) + y * data.RowPitch);
            for (unsigned x = 0; x < 96; ++x)
                for (unsigned c = 0; c < 4; ++c)
                {
                    const float value = row[x * 4 + c];
                    if (!std::isfinite(value)) throw std::runtime_error(test.name);
                    if (std::abs(value - test.expected[(x / 32) * 4 + c]) > 0.00001f && y == 0 && x == 32)
                        std::cerr << test.name << " pixel " << x << " component " << c << ": " << value
                            << " expected " << test.expected[(x / 32) * 4 + c] << '\n';
                    maximum = std::max(maximum, std::abs(value - test.expected[(x / 32) * 4 + c]));
                }
        }
        context->Unmap(readback.Get(), 0);
        std::cout << test.name << ": cumulative maximum error " << maximum << '\n';
    }
    return maximum < 0.00001f ? 0 : 1;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
