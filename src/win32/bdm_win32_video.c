#include "bdm_win32_video.h"
#include "bdm_frontend.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS 1
#endif
#include <windows.h>
#if defined(BDM_WIN64_FRONTEND)
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(BDM_WIN64_FRONTEND)
typedef HRESULT (WINAPI *bdm_d3dcompile_fn)(LPCVOID src_data, SIZE_T src_size, LPCSTR source_name,
                                            const D3D_SHADER_MACRO *defines, ID3DInclude *include,
                                            LPCSTR entrypoint, LPCSTR target, UINT flags1, UINT flags2,
                                            ID3DBlob **code, ID3DBlob **error_msgs);

typedef struct bdm_d3d_vertex {
    float x, y;
    float u, v;
} bdm_d3d_vertex_t;
#endif

struct bdm_win32_video {
    HWND hwnd;
    unsigned window_w;
    unsigned window_h;
    unsigned scale;
    int integer_scaling;
    int requested_d3d11;
    int d3d11_available;
    char backend_name[32];
    BITMAPINFO bmi;
    HDC gdi_mem_dc;
    HBITMAP gdi_bitmap;
    HGDIOBJ gdi_old_bitmap;
    void *gdi_bits;
    unsigned gdi_w;
    unsigned gdi_h;
#if defined(BDM_WIN64_FRONTEND)
    HMODULE d3dcompiler;
    bdm_d3dcompile_fn d3d_compile;
    ID3D11Device *d3d_device;
    ID3D11DeviceContext *d3d_context;
    IDXGISwapChain *swap_chain;
    ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *input_layout;
    ID3D11Buffer *vertex_buffer;
    ID3D11Texture2D *lcd_texture;
    ID3D11ShaderResourceView *lcd_srv;
    ID3D11SamplerState *sampler;
#endif
};

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == *b;
}

static void calc_dest(const bdm_win32_video_t *v, RECT *out) {
    RECT r;
    r.left = 0;
    r.top = 0;
    r.right = (LONG)(v ? v->window_w : BDM_LCD_WIDTH);
    r.bottom = (LONG)(v ? v->window_h : BDM_LCD_HEIGHT);
    if (!v || !v->integer_scaling) {
        *out = r;
        return;
    }
    unsigned sx = v->window_w / BDM_LCD_WIDTH;
    unsigned sy = v->window_h / BDM_LCD_HEIGHT;
    unsigned s = sx < sy ? sx : sy;
    if (!s) s = 1u;
    unsigned dw = BDM_LCD_WIDTH * s;
    unsigned dh = BDM_LCD_HEIGHT * s;
    r.left = (LONG)((v->window_w - dw) / 2u);
    r.top = (LONG)((v->window_h - dh) / 2u);
    r.right = r.left + (LONG)dw;
    r.bottom = r.top + (LONG)dh;
    *out = r;
}

#if defined(BDM_WIN64_FRONTEND)
static const char *bdm_d3d_shader_source =
    "Texture2D lcdTex : register(t0);\n"
    "SamplerState lcdSampler : register(s0);\n"
    "struct VSIn { float2 pos : POSITION; float2 tex : TEXCOORD0; };\n"
    "struct VSOut { float4 pos : SV_POSITION; float2 tex : TEXCOORD0; };\n"
    "VSOut vs_main(VSIn input) {\n"
    "    VSOut output;\n"
    "    output.pos = float4(input.pos, 0.0f, 1.0f);\n"
    "    output.tex = input.tex;\n"
    "    return output;\n"
    "}\n"
    "float4 ps_main(VSOut input) : SV_Target {\n"
    "    return lcdTex.Sample(lcdSampler, input.tex);\n"
    "}\n";

static int d3d11_load_compiler(bdm_win32_video_t *v) {
    static const char *dlls[] = {
        "d3dcompiler_47.dll",
        "d3dcompiler_46.dll",
        "d3dcompiler_43.dll",
        "d3dcompiler_42.dll",
        "d3dcompiler_41.dll"
    };
    if (!v) return 0;
    for (size_t i = 0; i < sizeof(dlls) / sizeof(dlls[0]); ++i) {
        HMODULE mod = LoadLibraryA(dlls[i]);
        if (!mod) continue;
        v->d3d_compile = (bdm_d3dcompile_fn)GetProcAddress(mod, "D3DCompile");
        if (v->d3d_compile) {
            v->d3dcompiler = mod;
            return 1;
        }
        FreeLibrary(mod);
    }
    return 0;
}

static void d3d11_release_pipeline(bdm_win32_video_t *v) {
    if (!v) return;
    if (v->d3d_context) {
        ID3D11ShaderResourceView *null_srv = NULL;
        ID3D11DeviceContext_PSSetShaderResources(v->d3d_context, 0, 1, &null_srv);
        ID3D11DeviceContext_ClearState(v->d3d_context);
    }
    if (v->sampler) ID3D11SamplerState_Release(v->sampler);
    if (v->lcd_srv) ID3D11ShaderResourceView_Release(v->lcd_srv);
    if (v->lcd_texture) ID3D11Texture2D_Release(v->lcd_texture);
    if (v->vertex_buffer) ID3D11Buffer_Release(v->vertex_buffer);
    if (v->input_layout) ID3D11InputLayout_Release(v->input_layout);
    if (v->ps) ID3D11PixelShader_Release(v->ps);
    if (v->vs) ID3D11VertexShader_Release(v->vs);
    v->sampler = NULL;
    v->lcd_srv = NULL;
    v->lcd_texture = NULL;
    v->vertex_buffer = NULL;
    v->input_layout = NULL;
    v->ps = NULL;
    v->vs = NULL;
}

static int d3d11_compile_shader(bdm_win32_video_t *v, const char *entry, const char *target, ID3DBlob **out_blob) {
    ID3DBlob *blob = NULL;
    ID3DBlob *errors = NULL;
    HRESULT hr;
    if (!v || !v->d3d_compile || !entry || !target || !out_blob) return 0;
    *out_blob = NULL;
    hr = v->d3d_compile(bdm_d3d_shader_source, strlen(bdm_d3d_shader_source), "bdm_win32_video.hlsl",
                        NULL, NULL, entry, target, 0, 0, &blob, &errors);
    if (errors) {
        const char *msg = (const char *)ID3D10Blob_GetBufferPointer(errors);
        if (msg && *msg) OutputDebugStringA(msg);
        ID3D10Blob_Release(errors);
    }
    if (FAILED(hr) || !blob) {
        if (blob) ID3D10Blob_Release(blob);
        return 0;
    }
    *out_blob = blob;
    return 1;
}

static int d3d11_create_rtv(bdm_win32_video_t *v) {
    ID3D11Texture2D *back = NULL;
    HRESULT hr;
    if (!v || !v->swap_chain || !v->d3d_device) return 0;
    hr = IDXGISwapChain_GetBuffer(v->swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back);
    if (FAILED(hr) || !back) return 0;
    hr = ID3D11Device_CreateRenderTargetView(v->d3d_device, (ID3D11Resource *)back, NULL, &v->rtv);
    ID3D11Texture2D_Release(back);
    return SUCCEEDED(hr) && v->rtv;
}

static int d3d11_create_pipeline(bdm_win32_video_t *v) {
    ID3DBlob *vs_blob = NULL;
    ID3DBlob *ps_blob = NULL;
    HRESULT hr;
    D3D11_INPUT_ELEMENT_DESC il[2];
    D3D11_BUFFER_DESC vb_desc;
    D3D11_TEXTURE2D_DESC tex_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D11_SAMPLER_DESC samp_desc;

    if (!v || !v->d3d_device || !v->d3d_context) return 0;
    if (!d3d11_load_compiler(v)) return 0;
    if (!d3d11_compile_shader(v, "vs_main", "vs_4_0", &vs_blob)) return 0;
    if (!d3d11_compile_shader(v, "ps_main", "ps_4_0", &ps_blob)) {
        ID3D10Blob_Release(vs_blob);
        return 0;
    }

    hr = ID3D11Device_CreateVertexShader(v->d3d_device,
                                         ID3D10Blob_GetBufferPointer(vs_blob),
                                         ID3D10Blob_GetBufferSize(vs_blob),
                                         NULL, &v->vs);
    if (FAILED(hr)) goto fail;
    hr = ID3D11Device_CreatePixelShader(v->d3d_device,
                                        ID3D10Blob_GetBufferPointer(ps_blob),
                                        ID3D10Blob_GetBufferSize(ps_blob),
                                        NULL, &v->ps);
    if (FAILED(hr)) goto fail;

    memset(il, 0, sizeof(il));
    il[0].SemanticName = "POSITION";
    il[0].SemanticIndex = 0;
    il[0].Format = DXGI_FORMAT_R32G32_FLOAT;
    il[0].InputSlot = 0;
    il[0].AlignedByteOffset = 0;
    il[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    il[0].InstanceDataStepRate = 0;
    il[1].SemanticName = "TEXCOORD";
    il[1].SemanticIndex = 0;
    il[1].Format = DXGI_FORMAT_R32G32_FLOAT;
    il[1].InputSlot = 0;
    il[1].AlignedByteOffset = 8;
    il[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    il[1].InstanceDataStepRate = 0;
    hr = ID3D11Device_CreateInputLayout(v->d3d_device, il, 2,
                                        ID3D10Blob_GetBufferPointer(vs_blob),
                                        ID3D10Blob_GetBufferSize(vs_blob),
                                        &v->input_layout);
    if (FAILED(hr)) goto fail;

    memset(&vb_desc, 0, sizeof(vb_desc));
    vb_desc.ByteWidth = (UINT)(sizeof(bdm_d3d_vertex_t) * 4u);
    vb_desc.Usage = D3D11_USAGE_DYNAMIC;
    vb_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(v->d3d_device, &vb_desc, NULL, &v->vertex_buffer);
    if (FAILED(hr)) goto fail;

    memset(&tex_desc, 0, sizeof(tex_desc));
    tex_desc.Width = BDM_LCD_WIDTH;
    tex_desc.Height = BDM_LCD_HEIGHT;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DYNAMIC;
    tex_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateTexture2D(v->d3d_device, &tex_desc, NULL, &v->lcd_texture);
    if (FAILED(hr)) goto fail;

    memset(&srv_desc, 0, sizeof(srv_desc));
    srv_desc.Format = tex_desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;
    hr = ID3D11Device_CreateShaderResourceView(v->d3d_device, (ID3D11Resource *)v->lcd_texture, &srv_desc, &v->lcd_srv);
    if (FAILED(hr)) goto fail;

    memset(&samp_desc, 0, sizeof(samp_desc));
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.MinLOD = 0.0f;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = ID3D11Device_CreateSamplerState(v->d3d_device, &samp_desc, &v->sampler);
    if (FAILED(hr)) goto fail;

    ID3D10Blob_Release(vs_blob);
    ID3D10Blob_Release(ps_blob);
    return 1;

fail:
    if (vs_blob) ID3D10Blob_Release(vs_blob);
    if (ps_blob) ID3D10Blob_Release(ps_blob);
    d3d11_release_pipeline(v);
    return 0;
}

static int d3d11_init(bdm_win32_video_t *v) {
    RECT rc;
    DXGI_SWAP_CHAIN_DESC sd;
    UINT flags = 0;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_DRIVER_TYPE drivers[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = E_FAIL;

    if (!v || !v->hwnd) return 0;
    GetClientRect(v->hwnd, &rc);
    memset(&sd, 0, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = (UINT)((rc.right > rc.left) ? (rc.right - rc.left) : 1);
    sd.BufferDesc.Height = (UINT)((rc.bottom > rc.top) ? (rc.bottom - rc.top) : 1);
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = v->hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    for (size_t i = 0; i < sizeof(drivers) / sizeof(drivers[0]); ++i) {
        hr = D3D11CreateDeviceAndSwapChain(NULL, drivers[i], NULL, flags,
                                           levels, (UINT)(sizeof(levels) / sizeof(levels[0])),
                                           D3D11_SDK_VERSION, &sd, &v->swap_chain,
                                           &v->d3d_device, &got, &v->d3d_context);
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return 0;
    if (!d3d11_create_rtv(v) || !d3d11_create_pipeline(v)) {
        d3d11_release_pipeline(v);
        if (v->rtv) { ID3D11RenderTargetView_Release(v->rtv); v->rtv = NULL; }
        if (v->swap_chain) { IDXGISwapChain_Release(v->swap_chain); v->swap_chain = NULL; }
        if (v->d3d_context) { ID3D11DeviceContext_Release(v->d3d_context); v->d3d_context = NULL; }
        if (v->d3d_device) { ID3D11Device_Release(v->d3d_device); v->d3d_device = NULL; }
        if (v->d3dcompiler) { FreeLibrary(v->d3dcompiler); v->d3dcompiler = NULL; v->d3d_compile = NULL; }
        return 0;
    }
    v->d3d11_available = 1;
    return 1;
}

static void d3d11_destroy(bdm_win32_video_t *v) {
    if (!v) return;
    d3d11_release_pipeline(v);
    if (v->rtv) ID3D11RenderTargetView_Release(v->rtv);
    if (v->swap_chain) IDXGISwapChain_Release(v->swap_chain);
    if (v->d3d_context) ID3D11DeviceContext_Release(v->d3d_context);
    if (v->d3d_device) ID3D11Device_Release(v->d3d_device);
    if (v->d3dcompiler) FreeLibrary(v->d3dcompiler);
    v->rtv = NULL;
    v->swap_chain = NULL;
    v->d3d_context = NULL;
    v->d3d_device = NULL;
    v->d3dcompiler = NULL;
    v->d3d_compile = NULL;
    v->d3d11_available = 0;
}

static void d3d11_resize(bdm_win32_video_t *v, unsigned w, unsigned h) {
    HRESULT hr;
    if (!v || !v->swap_chain) return;
    if (v->rtv) {
        ID3D11RenderTargetView_Release(v->rtv);
        v->rtv = NULL;
    }
    hr = IDXGISwapChain_ResizeBuffers(v->swap_chain, 0, (UINT)(w ? w : 1u), (UINT)(h ? h : 1u), DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) (void)d3d11_create_rtv(v);
}

static int d3d11_update_texture(bdm_win32_video_t *v, const uint32_t *fb) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    if (!v || !v->d3d_context || !v->lcd_texture || !fb) return 0;
    hr = ID3D11DeviceContext_Map(v->d3d_context, (ID3D11Resource *)v->lcd_texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return 0;
    for (unsigned y = 0; y < BDM_LCD_HEIGHT; ++y) {
        uint8_t *dst = (uint8_t *)mapped.pData + (size_t)y * mapped.RowPitch;
        const uint8_t *src = (const uint8_t *)(fb + (size_t)y * BDM_LCD_WIDTH);
        memcpy(dst, src, BDM_LCD_WIDTH * sizeof(uint32_t));
    }
    ID3D11DeviceContext_Unmap(v->d3d_context, (ID3D11Resource *)v->lcd_texture, 0);
    return 1;
}

static int d3d11_update_vertices(bdm_win32_video_t *v) {
    RECT dst;
    float ww, wh, l, r, t, b;
    bdm_d3d_vertex_t verts[4];
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    if (!v || !v->d3d_context || !v->vertex_buffer || !v->window_w || !v->window_h) return 0;
    calc_dest(v, &dst);
    ww = (float)v->window_w;
    wh = (float)v->window_h;
    l = ((float)dst.left / ww) * 2.0f - 1.0f;
    r = ((float)dst.right / ww) * 2.0f - 1.0f;
    t = 1.0f - ((float)dst.top / wh) * 2.0f;
    b = 1.0f - ((float)dst.bottom / wh) * 2.0f;
    verts[0].x = l; verts[0].y = t; verts[0].u = 0.0f; verts[0].v = 0.0f;
    verts[1].x = r; verts[1].y = t; verts[1].u = 1.0f; verts[1].v = 0.0f;
    verts[2].x = l; verts[2].y = b; verts[2].u = 0.0f; verts[2].v = 1.0f;
    verts[3].x = r; verts[3].y = b; verts[3].u = 1.0f; verts[3].v = 1.0f;
    hr = ID3D11DeviceContext_Map(v->d3d_context, (ID3D11Resource *)v->vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return 0;
    memcpy(mapped.pData, verts, sizeof(verts));
    ID3D11DeviceContext_Unmap(v->d3d_context, (ID3D11Resource *)v->vertex_buffer, 0);
    return 1;
}

static int d3d11_present(bdm_win32_video_t *v, const uint32_t *fb) {
    FLOAT clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    D3D11_VIEWPORT vp;
    UINT stride = sizeof(bdm_d3d_vertex_t);
    UINT offset = 0;
    ID3D11ShaderResourceView *null_srv = NULL;

    if (!v || !v->d3d11_available || !v->d3d_context || !v->swap_chain || !v->rtv) return 0;
    if (!d3d11_update_texture(v, fb) || !d3d11_update_vertices(v)) return 0;

    memset(&vp, 0, sizeof(vp));
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = (float)(v->window_w ? v->window_w : 1u);
    vp.Height = (float)(v->window_h ? v->window_h : 1u);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    ID3D11DeviceContext_OMSetRenderTargets(v->d3d_context, 1, &v->rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(v->d3d_context, 1, &vp);
    ID3D11DeviceContext_ClearRenderTargetView(v->d3d_context, v->rtv, clear);
    ID3D11DeviceContext_IASetInputLayout(v->d3d_context, v->input_layout);
    ID3D11DeviceContext_IASetVertexBuffers(v->d3d_context, 0, 1, &v->vertex_buffer, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(v->d3d_context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_VSSetShader(v->d3d_context, v->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(v->d3d_context, v->ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(v->d3d_context, 0, 1, &v->lcd_srv);
    ID3D11DeviceContext_PSSetSamplers(v->d3d_context, 0, 1, &v->sampler);
    ID3D11DeviceContext_Draw(v->d3d_context, 4, 0);
    ID3D11DeviceContext_PSSetShaderResources(v->d3d_context, 0, 1, &null_srv);
    return SUCCEEDED(IDXGISwapChain_Present(v->swap_chain, 0, 0));
}
#else
static int d3d11_init(bdm_win32_video_t *v) { (void)v; return 0; }
static void d3d11_destroy(bdm_win32_video_t *v) { (void)v; }
static void d3d11_resize(bdm_win32_video_t *v, unsigned w, unsigned h) { (void)v; (void)w; (void)h; }
static int d3d11_present(bdm_win32_video_t *v, const uint32_t *fb) { (void)v; (void)fb; return 0; }
#endif

static void gdi_destroy_backbuffer(bdm_win32_video_t *v) {
    if (!v) return;
    if (v->gdi_mem_dc && v->gdi_old_bitmap) {
        SelectObject(v->gdi_mem_dc, v->gdi_old_bitmap);
    }
    if (v->gdi_bitmap) DeleteObject(v->gdi_bitmap);
    if (v->gdi_mem_dc) DeleteDC(v->gdi_mem_dc);
    v->gdi_mem_dc = NULL;
    v->gdi_bitmap = NULL;
    v->gdi_old_bitmap = NULL;
    v->gdi_bits = NULL;
    v->gdi_w = 0;
    v->gdi_h = 0;
}

static int gdi_ensure_backbuffer(bdm_win32_video_t *v, unsigned w, unsigned h) {
    HDC wnd_dc;
#if !defined(BDM_WIN32_WIN31)
    BITMAPINFO bi;
#endif
    void *bits = NULL;
    HBITMAP bitmap;
    HDC mem_dc;

    if (!v || !v->hwnd) return 0;
    if (!w) w = 1u;
    if (!h) h = 1u;
    if (v->gdi_mem_dc && v->gdi_bitmap && v->gdi_w == w && v->gdi_h == h) return 1;

    gdi_destroy_backbuffer(v);

    wnd_dc = GetDC(v->hwnd);
    if (!wnd_dc) return 0;
    mem_dc = CreateCompatibleDC(wnd_dc);
#if defined(BDM_WIN32_WIN31)
    /* Keep the strict 32-bit Windows target on the classic GDI bitmap path;
       CreateDIBSection is a later GDI convenience and is not needed here. */
    bitmap = CreateCompatibleBitmap(wnd_dc, (int)w, (int)h);
    bits = bitmap ? (void *)1 : NULL;
#else
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(wnd_dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
#endif
    ReleaseDC(v->hwnd, wnd_dc);

    if (!mem_dc || !bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        if (mem_dc) DeleteDC(mem_dc);
        return 0;
    }

    v->gdi_mem_dc = mem_dc;
    v->gdi_bitmap = bitmap;
    v->gdi_old_bitmap = SelectObject(mem_dc, bitmap);
    v->gdi_bits = bits;
    v->gdi_w = w;
    v->gdi_h = h;
    return 1;
}

static int gdi_present_direct(bdm_win32_video_t *v, const uint32_t *fb) {
    HDC dc;
    RECT client;
    RECT dst;
    if (!v || !fb) return -1;
    dc = GetDC(v->hwnd);
    if (!dc) return -1;
    GetClientRect(v->hwnd, &client);
    FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    calc_dest(v, &dst);
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                  0, 0, (int)BDM_LCD_WIDTH, (int)BDM_LCD_HEIGHT,
                  fb, &v->bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(v->hwnd, dc);
    return 0;
}

static int gdi_present(bdm_win32_video_t *v, const uint32_t *fb) {
    HDC dc;
    RECT client;
    RECT dst;
    RECT mem_rc;

    if (!v || !fb) return -1;
    GetClientRect(v->hwnd, &client);
    if (client.right <= client.left || client.bottom <= client.top) return 0;

    if (!gdi_ensure_backbuffer(v, (unsigned)(client.right - client.left), (unsigned)(client.bottom - client.top))) {
        return gdi_present_direct(v, fb);
    }

    mem_rc.left = 0;
    mem_rc.top = 0;
    mem_rc.right = (LONG)v->gdi_w;
    mem_rc.bottom = (LONG)v->gdi_h;
    FillRect(v->gdi_mem_dc, &mem_rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    calc_dest(v, &dst);
    SetStretchBltMode(v->gdi_mem_dc, COLORONCOLOR);
    StretchDIBits(v->gdi_mem_dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                  0, 0, (int)BDM_LCD_WIDTH, (int)BDM_LCD_HEIGHT,
                  fb, &v->bmi, DIB_RGB_COLORS, SRCCOPY);

    dc = GetDC(v->hwnd);
    if (!dc) return -1;
    BitBlt(dc, 0, 0, (int)v->gdi_w, (int)v->gdi_h, v->gdi_mem_dc, 0, 0, SRCCOPY);
    ReleaseDC(v->hwnd, dc);
    return 0;
}

bdm_win32_video_t *bdm_win32_video_create(HWND hwnd, unsigned scale, int integer_scaling, const char *backend) {
    bdm_win32_video_t *v = (bdm_win32_video_t *)calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->hwnd = hwnd;
    v->scale = scale ? scale : 1u;
    v->integer_scaling = integer_scaling != 0;
    v->requested_d3d11 = backend && str_eq(backend, "d3d11");
    memset(&v->bmi, 0, sizeof(v->bmi));
    v->bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    v->bmi.bmiHeader.biWidth = (LONG)BDM_LCD_WIDTH;
    v->bmi.bmiHeader.biHeight = -(LONG)BDM_LCD_HEIGHT;
    v->bmi.bmiHeader.biPlanes = 1;
    v->bmi.bmiHeader.biBitCount = 32;
    v->bmi.bmiHeader.biCompression = BI_RGB;
    strcpy(v->backend_name, "gdi");
    if (v->requested_d3d11 && d3d11_init(v)) {
        strcpy(v->backend_name, "d3d11-shader");
    }
    RECT rc;
    GetClientRect(hwnd, &rc);
    bdm_win32_video_resize(v, (unsigned)(rc.right - rc.left), (unsigned)(rc.bottom - rc.top));
    return v;
}

void bdm_win32_video_destroy(bdm_win32_video_t *v) {
    if (!v) return;
    d3d11_destroy(v);
    gdi_destroy_backbuffer(v);
    free(v);
}

void bdm_win32_video_resize(bdm_win32_video_t *v, unsigned width, unsigned height) {
    if (!v) return;
    v->window_w = width ? width : 1u;
    v->window_h = height ? height : 1u;
    if (v->d3d11_available) d3d11_resize(v, v->window_w, v->window_h);
    else gdi_destroy_backbuffer(v);
}

int bdm_win32_video_present(bdm_win32_video_t *v, const bdm_video_t *video) {
    size_t w = 0, h = 0;
    const uint32_t *fb;
    if (!v || !video) return -1;
    fb = bdm_video_framebuffer(video, &w, &h);
    if (!fb || w != BDM_LCD_WIDTH || h != BDM_LCD_HEIGHT) return -1;
    if (v->d3d11_available && d3d11_present(v, fb)) return 0;
    return gdi_present(v, fb);
}

void bdm_win32_video_window_to_pen_fp(bdm_win32_video_t *v, const bdm_video_t *video, int wx, int wy, int touch_offset_x, int touch_offset_y, int32_t *out_x_fp, int32_t *out_y_fp) {
    RECT dst;
    calc_dest(v, &dst);
    int dw = (int)(dst.right - dst.left);
    int dh = (int)(dst.bottom - dst.top);
    float lx = dw > 0 ? ((float)(wx - dst.left) * (float)BDM_LCD_WIDTH) / (float)dw : 0.0f;
    float ly = dh > 0 ? ((float)(wy - dst.top) * (float)BDM_LCD_HEIGHT) / (float)dh : 0.0f;
    bdm_fe_logical_to_pen_fp(video, lx, ly, touch_offset_x, touch_offset_y, out_x_fp, out_y_fp);
}

void bdm_win32_video_window_to_pen(bdm_win32_video_t *v, const bdm_video_t *video, int wx, int wy, int touch_offset_x, int touch_offset_y, int *out_x, int *out_y) {
    int32_t x_fp = 0, y_fp = 0;
    bdm_win32_video_window_to_pen_fp(v, video, wx, wy, touch_offset_x, touch_offset_y, &x_fp, &y_fp);
    if (out_x) *out_x = (int)(x_fp >> 16);
    if (out_y) *out_y = (int)(y_fp >> 16);
}

const char *bdm_win32_video_active_backend(const bdm_win32_video_t *v) { return v ? v->backend_name : "none"; }
void bdm_win32_video_set_integer_scaling(bdm_win32_video_t *v, int enabled) { if (v) v->integer_scaling = enabled != 0; }
int bdm_win32_video_integer_scaling(const bdm_win32_video_t *v) { return v ? v->integer_scaling : 0; }
