// Executes the native Material Canvas output and the retained legacy shader on
// the same pixel quads. This tests real ddx/ddy, negative cells and uint hashing.
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
static void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("D3D call failed"); }
static std::string Load(const char* path)
{
    std::ifstream file(path);
    if (!file) throw std::runtime_error(std::string("Missing native generated shader: ") + path);
    std::string line, result;
    while (std::getline(file, line))
        if (line.find("#include") == std::string::npos && line.find("#pragma") == std::string::npos)
            result += line + "\n";
    return result;
}
static void Replace(std::string& text, const std::string& from, const std::string& to)
{
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) text.replace(at, from.size(), to);
}
static ComPtr<ID3DBlob> Compile(const std::string& source, const char* entry, const char* profile)
{
    ComPtr<ID3DBlob> code, errors;
    auto result = D3DCompile(source.data(), source.size(), "TerrainCanvasParity", nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if (FAILED(result)) throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Compile failed");
    return code;
}
int main()
try
{
    std::string shader = "cbuffer Inputs : register(b0) { float strength; float stepSize; float2 origin; };\n";
    shader += Load(TERRAIN_SHADERS "/TerrainLegacyTint.azsli");
    shader += Load(CANVAS_ROOT "/Assets/ShaderLib/TerrainCanvas/LatticeNoise.azsli");
    shader += Load(CANVAS_ROOT "/Assets/MaterialCanvas/Terrain/Examples/procedural_tint_Tint.azsli");
    Replace(shader, "TerrainMaterialSrg::m_noiseTintStrength", "strength");
    Replace(shader, "TerrainMaterialSrg::m_canvasTintStrength", "strength");
    Replace(shader, "TerrainMaterialSrg::m_canvasTintColor", "float3(0.7, 0.85, 0.55)");
    shader += R"(
float4 VS(uint id : SV_VertexID) : SV_Position {
    return float4(id == 2 ? 3 : -1, id == 1 ? 3 : -1, 0, 1);
}
float4 PS(float4 pixel : SV_Position) : SV_Target {
    float3 world = float3(origin + pixel.xy * stepSize, pixel.y * 0.04 - 5.0);
    return float4(EVALUATE_TINT(world), 1);
})";
    auto legacySource = shader;
    Replace(legacySource, "EVALUATE_TINT", "TGTerrainNoiseTint");
    Replace(shader, "EVALUATE_TINT", "TC_CanvasTint");
    auto vsCode = Compile(shader, "VS", "vs_5_0");
    // Separate shader compilations prevent common-subexpression elimination from
    // simplifying a same-shader comparison to zero without evaluating the tint.
    auto legacyCode = Compile(legacySource, "PS", "ps_5_0");
    auto canvasCode = Compile(shader, "PS", "ps_5_0");
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context));
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> legacyPs, canvasPs;
    Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs));
    Check(device->CreatePixelShader(legacyCode->GetBufferPointer(), legacyCode->GetBufferSize(), nullptr, &legacyPs));
    Check(device->CreatePixelShader(canvasCode->GetBufferPointer(), canvasCode->GetBufferSize(), nullptr, &canvasPs));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 256; desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> output, readback;
    Check(device->CreateTexture2D(&desc, nullptr, &output));
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Check(device->CreateTexture2D(&desc, nullptr, &readback));
    ComPtr<ID3D11RenderTargetView> target;
    Check(device->CreateRenderTargetView(output.Get(), nullptr, &target));
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = 16; buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> constants;
    Check(device->CreateBuffer(&buffer, nullptr, &constants));
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID; raster.CullMode = D3D11_CULL_NONE; raster.DepthClipEnable = true;
    ComPtr<ID3D11RasterizerState> rasterState;
    Check(device->CreateRasterizerState(&raster, &rasterState));
    context->RSSetState(rasterState.Get());
    D3D11_VIEWPORT viewport{ 0, 0, 256, 256, 0, 1 };
    context->RSSetViewports(1, &viewport);
    auto* view = target.Get(); auto* cb = constants.Get();
    context->OMSetRenderTargets(1, &view, nullptr);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &cb);
    float maximum = 0;
    size_t samples = 0;
    std::vector<float> reference(256 * 256 * 4);
    for (float strength : { 0.0f, 0.75f, 1.0f })
        for (float step : { 0.01f, 1.0f, 32.0f, 48.0f, 64.0f, 128.0f })
        {
            const float values[]{ strength, step, -8192.25f, -79.75f };
            context->UpdateSubresource(constants.Get(), 0, nullptr, values, 0, 0);
            for (unsigned pass = 0; pass < 2; ++pass)
            {
                context->PSSetShader(pass ? canvasPs.Get() : legacyPs.Get(), nullptr, 0);
                const float sentinel[]{ 100, 100, 100, 100 };
                context->ClearRenderTargetView(view, sentinel);
                context->Draw(3, 0);
                context->CopyResource(readback.Get(), output.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                for (unsigned y = 0; y < 256; ++y)
                {
                    const auto* row = reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData) + y * mapped.RowPitch);
                    for (unsigned x = 0; x < 256 * 4; ++x)
                    {
                        if (!std::isfinite(row[x]) || row[x] < 0 || row[x] > 1)
                            throw std::runtime_error("Invalid or unwritten tint output");
                        auto& expected = reference[y * 256 * 4 + x];
                        if (!pass) expected = row[x];
                        else maximum = std::max(maximum, std::abs(expected - row[x]));
                        if (strength == 0) maximum = std::max(maximum, std::abs(1.0f - row[x]));
                    }
                }
                context->Unmap(readback.Get(), 0);
            }
            samples += 256 * 256;
        }
    std::cout << "GPU pixel pairs: " << samples << "; maximum RGB/neutrality error: " << maximum << '\n';
    return maximum <= 0.000001f ? 0 : 1;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
