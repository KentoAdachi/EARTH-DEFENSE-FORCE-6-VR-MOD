#include "scope_zoom.h"
#include <wrl/client.h>
#include <algorithm>
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
        pixels[i]=static_cast<unsigned char>(x);pixels[i+3]=255;
    }
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=side;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA init{};init.pSysMem=pixels.data();init.SysMemPitch=side*4;
    ComPtr<ID3D11Texture2D> world,target,readback;
    if(FAILED(device->CreateTexture2D(&desc,&init,&world))) return 3;
    std::fill(pixels.begin(),pixels.end(),static_cast<unsigned char>(0));
    init.pSysMem=pixels.data();
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
    const auto sample=[&](unsigned x) {return static_cast<unsigned char*>(map.pData)[128*map.RowPitch+x*4];};
    const unsigned middle=sample(128),inside=sample(133),outside=sample(160);
    context->Unmap(readback.Get(),0);
    edf6vr::ReleaseScopeZoom();
    if(middle<126 || middle>130 || inside<129 || inside>133 || outside!=0) {
        std::printf("scope sample middle=%u inside=%u outside=%u\n",middle,inside,outside);
        return 8;
    }
    std::puts("scope 2x inside, unchanged outside");
    return 0;
}
