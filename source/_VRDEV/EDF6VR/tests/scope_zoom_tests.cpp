#include "scope_zoom.h"
#include <wrl/client.h>
#include <vector>
#include <cstdio>

using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context))) return 2;
    constexpr unsigned side=256;
    std::vector<unsigned char> pixels(side*side*4,0);
    for(unsigned y=0;y<side;++y) for(unsigned x=0;x<side;++x) {
        const auto i=(y*side+x)*4;
        pixels[i]=static_cast<unsigned char>(x);
        pixels[i+1]=static_cast<unsigned char>(y);pixels[i+3]=255;
    }
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=side;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA init{};init.pSysMem=pixels.data();init.SysMemPitch=side*4;
    ComPtr<ID3D11Texture2D> world,target,readback;
    if(FAILED(device->CreateTexture2D(&desc,&init,&world))) return 3;
    // The compositor receives a normal world image before the lens overlay.
    // Keep that image here to check preservation of its existing outer pixels.
    if(FAILED(device->CreateTexture2D(&desc,&init,&target))) return 4;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&readback))) return 5;
    edf6vr::Matrix view{{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}}};
    edf6vr::Matrix projection{{{1,0,0,0},{0,1,0,0},{0,0,1,-1},{0,0,0,0}}};
    edf6vr::ScopeZoomPose scope{};scope.frame=1;
    scope.center[2]=scope.right[2]=scope.up[2]=.2f;
    scope.right[0]=.014f;scope.up[1]=.014f;
    if(!edf6vr::CompositeScopeZoom(context.Get(),world.Get(),target.Get(),view,projection,scope)) return 6;
    context->CopyResource(readback.Get(),target.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    if(FAILED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&map))) return 7;
    struct Sample {unsigned x,y,red,green;};
    // Sampling at half the displacement from the center gives these values
    // (within one byte for interpolation). In particular, 1x gives red=120
    // at (120,128), which cannot pass the expected 124 +/- 1 check.
    constexpr Sample samples[]={
        {128,128,128,128},
        {120,128,124,128},{135,128,131,128},
        {128,120,128,124},{128,135,128,131},
        // Outside the aperture, the original red/green gradient must remain.
        {160,128,160,128},{128,160,128,160},{120,120,120,120},
        {0,0,0,0},{255,255,255,255}
    };
    const auto withinByte=[](unsigned actual,unsigned expected) {
        return actual+1>=expected && actual<=expected+1;
    };
    unsigned failures=0;
    for(const auto& sample:samples) {
        const auto* pixel=static_cast<const unsigned char*>(map.pData)
            +sample.y*map.RowPitch+sample.x*4;
        if(!withinByte(pixel[0],sample.red) || !withinByte(pixel[1],sample.green)
           || pixel[2]!=0 || pixel[3]!=255) {
            std::printf("scope (%u,%u): rgba=%u,%u,%u,%u expected=%u,%u,0,255\n",
                sample.x,sample.y,unsigned(pixel[0]),unsigned(pixel[1]),
                unsigned(pixel[2]),unsigned(pixel[3]),sample.red,sample.green);
            ++failures;
        }
    }
    context->Unmap(readback.Get(),0);
    edf6vr::ReleaseScopeZoom();
    std::printf("scope 2x sampling (both axes), unchanged outside: %u failures\n",failures);
    return failures?8:0;
}
