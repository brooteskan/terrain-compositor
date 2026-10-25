// Hardware raster coverage regression for the production terrain ground culler.
// See VerifyTerrainGroundCull.py for the generated shader and invocation.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
void ok(HRESULT h)
{
    if (FAILED(h))
        throw std::runtime_error("D3D failure " + std::to_string(unsigned(h)));
}
ComPtr<ID3DBlob> compile(const std::string &source, const char *entry, const char *target)
{
    ComPtr<ID3DBlob> b, e;
    auto hr = D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, entry, target,
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e);
    if (FAILED(hr))
    {
        if (e)
            std::cerr << (char *)e->GetBufferPointer();
        ok(hr);
    }
    return b;
}
struct V
{
    float position[4], world[3];
};
int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("shader path required");
        std::ifstream f(argv[1]);
        std::string shader((std::istreambuf_iterator<char>(f)), {});
        ComPtr<ID3D11Device> d;
        ComPtr<ID3D11DeviceContext> c;
        D3D_FEATURE_LEVEL level;
        ok(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &d, &level,
                             &c));
        auto ref = compile(shader, "ReferenceVS", "vs_5_0"), opt = compile(shader, "OptimizedVS", "vs_5_0"),
             rp = compile(shader, "ReferencePS", "ps_5_0"), op = compile(shader, "OptimizedPS", "ps_5_0");
        ComPtr<ID3D11VertexShader> vs[2];
        ComPtr<ID3D11PixelShader> ps[2];
        ok(d->CreateVertexShader(ref->GetBufferPointer(), ref->GetBufferSize(), nullptr, &vs[0]));
        ok(d->CreateVertexShader(opt->GetBufferPointer(), opt->GetBufferSize(), nullptr, &vs[1]));
        ok(d->CreatePixelShader(rp->GetBufferPointer(), rp->GetBufferSize(), nullptr, &ps[0]));
        ok(d->CreatePixelShader(op->GetBufferPointer(), op->GetBufferSize(), nullptr, &ps[1]));
        D3D11_INPUT_ELEMENT_DESC els[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        ComPtr<ID3D11InputLayout> il;
        ok(d->CreateInputLayout(els, 2, ref->GetBufferPointer(), ref->GetBufferSize(), &il));
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 3 * sizeof(V);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        ComPtr<ID3D11Buffer> vb;
        ok(d->CreateBuffer(&bd, nullptr, &vb));
        bd.ByteWidth = 16;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> cb;
        ok(d->CreateBuffer(&bd, nullptr, &cb));
        D3D11_RASTERIZER_DESC rd = {};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = true;
        rd.MultisampleEnable = true;
        ComPtr<ID3D11RasterizerState> rs;
        ok(d->CreateRasterizerState(&rd, &rs));
        c->RSSetState(rs.Get());
        D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
        c->RSSetViewports(1, &vp);
        c->IASetInputLayout(il.Get());
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        UINT stride = sizeof(V), offset = 0;
        c->IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
        c->VSSetConstantBuffers(0, 1, cb.GetAddressOf());
        c->PSSetConstantBuffers(0, 1, cb.GetAddressOf());
        const std::array<std::array<float, 3>, 14> heights = {{{1, 1, 1},
                                                               {-1, -1, -1},
                                                               {0, 0, 0},
                                                               {0, 1, 1},
                                                               {0, -1, -1},
                                                               {0, 0, 1},
                                                               {0, 0, -1},
                                                               {-1, 1, 1},
                                                               {1, -1, -1},
                                                               {-1, 0, 1},
                                                               {.0001f, -.0001f, .0002f},
                                                               {1, 2, 3},
                                                               {-2, 1, .3f},
                                                               {.17f, -.81f, .29f}}};
        unsigned cases = 0, changedCases = 0, totalChanged = 0, maxChanged = 0;
        for (UINT samples : {1u, 2u})
        {
            UINT quality = 0;
            ok(d->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, samples, &quality));
            if (!quality)
                throw std::runtime_error("MSAA unsupported");
            D3D11_TEXTURE2D_DESC td = {};
            td.Width = 64;
            td.Height = 64;
            td.MipLevels = 1;
            td.ArraySize = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            td.SampleDesc.Count = samples;
            td.Usage = D3D11_USAGE_DEFAULT;
            td.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> rt;
            ok(d->CreateTexture2D(&td, nullptr, &rt));
            ComPtr<ID3D11RenderTargetView> view;
            ok(d->CreateRenderTargetView(rt.Get(), nullptr, &view));
            td.SampleDesc.Count = 1;
            td.BindFlags = 0;
            ComPtr<ID3D11Texture2D> resolved;
            ok(d->CreateTexture2D(&td, nullptr, &resolved));
            td.Usage = D3D11_USAGE_STAGING;
            td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> read;
            ok(d->CreateTexture2D(&td, nullptr, &read));
            for (float cutoff : {0.f, 1000.f})
                for (bool perspective : {false, true})
                    for (auto h : heights)
                    {
                        V verts[3] = {};
                        const float xy[3][2] = {{-.9f, -.9f}, {.9f, -.9f}, {0, .9f}};
                        for (int i = 0; i < 3; ++i)
                        {
                            float w = perspective ? float(1 << i) : 1;
                            verts[i].position[0] = xy[i][0] * w;
                            verts[i].position[1] = xy[i][1] * w;
                            verts[i].position[2] = .5f * w;
                            verts[i].position[3] = w;
                            verts[i].world[2] = cutoff + h[i];
                        }
                        c->UpdateSubresource(vb.Get(), 0, nullptr, verts, 0, 0);
                        float params[4] = {cutoff, 0, 0, 0};
                        c->UpdateSubresource(cb.Get(), 0, nullptr, params, 0, 0);
                        std::vector<unsigned char> images[2];
                        for (int mode = 0; mode < 2; ++mode)
                        {
                            float clear[4] = {0, 0, 0, 0};
                            c->ClearRenderTargetView(view.Get(), clear);
                            c->OMSetRenderTargets(1, view.GetAddressOf(), nullptr);
                            c->VSSetShader(vs[mode].Get(), nullptr, 0);
                            c->PSSetShader(ps[mode].Get(), nullptr, 0);
                            c->Draw(3, 0);
                            c->OMSetRenderTargets(0, nullptr, nullptr);
                            if (samples > 1)
                                c->ResolveSubresource(resolved.Get(), 0, rt.Get(), 0, td.Format);
                            else
                                c->CopyResource(resolved.Get(), rt.Get());
                            c->CopyResource(read.Get(), resolved.Get());
                            D3D11_MAPPED_SUBRESOURCE mapped;
                            ok(c->Map(read.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                            images[mode].resize(64 * 64);
                            for (int y = 0; y < 64; ++y)
                                for (int x = 0; x < 64; ++x)
                                    images[mode][y * 64 + x] =
                                        ((unsigned char *)mapped.pData)[y * mapped.RowPitch + x * 4];
                            c->Unmap(read.Get(), 0);
                        }
                        unsigned changed = 0;
                        for (int i = 0; i < 64 * 64; ++i)
                            changed += images[0][i] != images[1][i];
                        ++cases;
                        if (changed)
                        {
                            ++changedCases;
                            totalChanged += changed;
                            maxChanged = std::max(maxChanged, changed);
                            std::cerr << "Coverage difference: MSAA " << samples << " cutoff " << cutoff
                                      << " perspective " << perspective << " heights " << h[0] << "," << h[1] << ","
                                      << h[2] << " pixels " << changed << "\n";
                        }
                    }
        }
        std::cout << "{\"cases\":" << cases << ",\"changed_cases\":" << changedCases
                  << ",\"changed_pixels\":" << totalChanged << ",\"max_changed_pixels\":" << maxChanged << "}\n";
        return changedCases ? 2 : 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
