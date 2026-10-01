#include "scope_zoom.h"
#include "ui_capture.h"
#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

namespace edf6vr {
namespace {
using Microsoft::WRL::ComPtr;
SRWLOCK poseLock=SRWLOCK_INIT;
ScopeZoomPose pose{};
std::atomic<unsigned long long> published{0},composited{0};
ComPtr<ID3D11Device> device;
ComPtr<ID3D11VertexShader> vertex;
ComPtr<ID3D11PixelShader> pixel;
ComPtr<ID3D11Buffer> constants;
ComPtr<ID3D11SamplerState> sampler;
ComPtr<ID3D11BlendState> blend;
ComPtr<ID3D11DepthStencilState> depth;
ComPtr<ID3D11RasterizerState> raster;
constexpr char shader[]=R"(
Texture2D<float4> Scene:register(t0);
SamplerState Smooth:register(s0);
cbuffer Params:register(b0) {
 float4 Center;
 float4 BasisX;
 float4 BasisY;
 float4 Settings;
};
struct V {float4 position:SV_Position;float2 uv:TEXCOORD0;};
V vs(uint id:SV_VertexID) {
 V o;o.uv=float2((id<<1)&2,id&2);
 o.position=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;
}
float4 ps(V v):SV_Target {
 float2 d=v.uv-Center.xy;
 float det=BasisX.x*BasisY.y-BasisX.y*BasisY.x;
 float2 local=float2((d.x*BasisY.y-d.y*BasisY.x)/det,
                     (BasisX.x*d.y-BasisX.y*d.x)/det);
 if(dot(local,local)>1) discard;
 // One source sample per covered pixel. The world was already rendered once
 // for this eye; its inner half-width is a 2x view of the same scene.
 float2 sampleUv=Center.xy+d*.5;
 float4 c=Scene.SampleLevel(Smooth,sampleUv,0);
 if(Settings.z>.5) c.rgb=lerp(pow((c.rgb+.055)/1.055,2.4),c.rgb/12.92,step(c.rgb,.04045));
 return c;
})";
void Point(const Matrix& root,const float local[3],float out[3]) noexcept {
    for(unsigned j=0;j<3;++j) {
        out[j]=root.m[3][j];
        for(unsigned k=0;k<3;++k) out[j]+=local[k]*root.m[k][j];
    }
}
bool Project(const float p[3],const Matrix& view,const Matrix& projection,float out[2]) noexcept {
    float camera[4]{},clip[4]{};
    for(unsigned j=0;j<4;++j) {
        camera[j]=view.m[3][j];
        for(unsigned k=0;k<3;++k) camera[j]+=p[k]*view.m[k][j];
    }
    camera[0]=-camera[0];camera[2]=-camera[2]; // EDF VIEW_CHANGED Ry(pi)
    for(unsigned j=0;j<4;++j) for(unsigned k=0;k<4;++k)
        clip[j]+=camera[k]*projection.m[k][j];
    if(!std::isfinite(clip[3]) || clip[3]<=.03f) return false;
    out[0]=.5f+.5f*clip[0]/clip[3];
    out[1]=.5f-.5f*clip[1]/clip[3];
    return std::isfinite(out[0]) && std::isfinite(out[1]);
}
bool Build(ID3D11Device* d) noexcept {
    if(device.Get()!=d) {ReleaseScopeZoom();device=d;}
    if(vertex && pixel && constants && sampler && blend && depth && raster) return true;
    static HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");
    if(!compiler) return false;
    const auto compile=reinterpret_cast<decltype(&D3DCompile)>(
        reinterpret_cast<void*>(GetProcAddress(compiler,"D3DCompile")));
    if(!compile) return false;
    ComPtr<ID3DBlob> v,p;
    if(FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&v,nullptr)) ||
       FAILED(compile(shader,sizeof(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&p,nullptr))) return false;
    if(FAILED(d->CreateVertexShader(v->GetBufferPointer(),v->GetBufferSize(),nullptr,&vertex)) ||
       FAILED(d->CreatePixelShader(p->GetBufferPointer(),p->GetBufferSize(),nullptr,&pixel))) return false;
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=64;cb.Usage=D3D11_USAGE_DEFAULT;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC s{};s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;s.MaxLOD=D3D11_FLOAT32_MAX;
    D3D11_BLEND_DESC b{};b.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC z{};z.DepthFunc=D3D11_COMPARISON_ALWAYS;
    D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;
    r.DepthClipEnable=TRUE;r.ScissorEnable=TRUE;
    return SUCCEEDED(d->CreateBuffer(&cb,nullptr,&constants)) &&
        SUCCEEDED(d->CreateSamplerState(&s,&sampler)) &&
        SUCCEEDED(d->CreateBlendState(&b,&blend)) &&
        SUCCEEDED(d->CreateDepthStencilState(&z,&depth)) &&
        SUCCEEDED(d->CreateRasterizerState(&r,&raster));
}
}
void PublishScopeZoom(const Matrix& weaponRoot,std::uint64_t frame) noexcept {
    if(!frame) return;
    // SVU01's rear glass was centred at (0,.14795,-.06155) in the MDB.
    // Keep a small margin inside the physical scope ring.
    constexpr float rear[3]={0,.14795f,-.06155f};
    constexpr float edgeX[3]={.014f,.14795f,-.06155f};
    constexpr float edgeY[3]={0,.16195f,-.06155f};
    ScopeZoomPose next{};next.frame=frame;
    Point(weaponRoot,rear,next.center);Point(weaponRoot,edgeX,next.right);Point(weaponRoot,edgeY,next.up);
    for(float value:next.center) if(!std::isfinite(value)) return;
    AcquireSRWLockExclusive(&poseLock);pose=next;ReleaseSRWLockExclusive(&poseLock);
    published.fetch_add(1,std::memory_order_relaxed);
}
void ClearScopeZoom() noexcept {
    AcquireSRWLockExclusive(&poseLock);pose={};ReleaseSRWLockExclusive(&poseLock);
}
bool ReadScopeZoom(std::uint64_t frame,ScopeZoomPose& out) noexcept {
    AcquireSRWLockShared(&poseLock);out=pose;ReleaseSRWLockShared(&poseLock);
    return frame && out.frame==frame;
}
void ReleaseScopeZoom() noexcept {
    vertex.Reset();pixel.Reset();constants.Reset();sampler.Reset();blend.Reset();depth.Reset();raster.Reset();device.Reset();
}
const char* ScopeZoomStatus() noexcept {
    thread_local char line[100]{};
    std::snprintf(line,sizeof(line),"SCOPEZOOM published=%llu composited=%llu",
        published.load(std::memory_order_relaxed),composited.load(std::memory_order_relaxed));
    return line;
}
bool CompositeScopeZoom(ID3D11DeviceContext* ctx,ID3D11Texture2D* world,
                        ID3D11Texture2D* target,const Matrix& view,
                        const Matrix& projection,const ScopeZoomPose& scope) noexcept {
    if(!ctx || !world || !target || !scope.frame || world==target) return false;
    D3D11_TEXTURE2D_DESC src{},dst{};world->GetDesc(&src);target->GetDesc(&dst);
    if(!src.Width || src.Width!=dst.Width || src.Height!=dst.Height ||
       src.SampleDesc.Count!=1 || dst.SampleDesc.Count!=1 || src.ArraySize!=1 || dst.ArraySize!=1) return false;
    float center[2]{},right[2]{},up[2]{};
    if(!Project(scope.center,view,projection,center) ||
       !Project(scope.right,view,projection,right) ||
       !Project(scope.up,view,projection,up)) return false;
    const float bx[2]={right[0]-center[0],right[1]-center[1]};
    const float by[2]={up[0]-center[0],up[1]-center[1]};
    const float determinant=bx[0]*by[1]-bx[1]*by[0];
    const float rx=std::hypot(bx[0],by[0]),ry=std::hypot(bx[1],by[1]);
    if(std::fabs(determinant)<1e-9f || rx*dst.Width<3 || ry*dst.Height<3 ||
       rx>.25f || ry>.25f || center[0]<-rx || center[0]>1+rx || center[1]<-ry || center[1]>1+ry) return false;
    const LONG l=std::max(0L,static_cast<LONG>(std::floor((center[0]-rx)*dst.Width)));
    const LONG t=std::max(0L,static_cast<LONG>(std::floor((center[1]-ry)*dst.Height)));
    const LONG r=std::min(static_cast<LONG>(dst.Width),static_cast<LONG>(std::ceil((center[0]+rx)*dst.Width)));
    const LONG b=std::min(static_cast<LONG>(dst.Height),static_cast<LONG>(std::ceil((center[1]+ry)*dst.Height)));
    if(l>=r || t>=b) return false;
    ComPtr<ID3D11Device> d;ctx->GetDevice(&d);if(!Build(d.Get())) return false;
    DXGI_FORMAT sourceFormat=src.Format,targetFormat=dst.Format;
    if(sourceFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS) sourceFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(sourceFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS) sourceFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
    if(targetFormat==DXGI_FORMAT_R8G8B8A8_TYPELESS) targetFormat=DXGI_FORMAT_R8G8B8A8_UNORM;
    if(targetFormat==DXGI_FORMAT_B8G8R8A8_TYPELESS) targetFormat=DXGI_FORMAT_B8G8R8A8_UNORM;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};sv.Format=sourceFormat;sv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;sv.Texture2D.MipLevels=1;
    D3D11_RENDER_TARGET_VIEW_DESC rv{};rv.Format=targetFormat;rv.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11ShaderResourceView> input;ComPtr<ID3D11RenderTargetView> output;
    if(FAILED(d->CreateShaderResourceView(world,&sv,&input)) || FAILED(d->CreateRenderTargetView(target,&rv,&output))) return false;
    const bool srgb=targetFormat==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || targetFormat==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    const float data[16]={center[0],center[1],0,0,bx[0],bx[1],0,0,by[0],by[1],0,0,0,0,srgb?1.f:0.f,0};
    ctx->UpdateSubresource(constants.Get(),0,nullptr,data,0,0);
    PauseUiCapture pause;
    ID3D11RenderTargetView* oldRt[8]{};ComPtr<ID3D11DepthStencilView> oldDsv;ctx->OMGetRenderTargets(8,oldRt,&oldDsv);
    ComPtr<ID3D11VertexShader> oldVs;ComPtr<ID3D11PixelShader> oldPs;ComPtr<ID3D11GeometryShader> oldGs;
    ComPtr<ID3D11HullShader> oldHs;ComPtr<ID3D11DomainShader> oldDs;
    ID3D11ClassInstance* vi[256]{},*pi[256]{},*gi[256]{},*hi[256]{},*di[256]{};
    UINT vn=256,pn=256,gn=256,hn=256,dn=256;
    ctx->VSGetShader(&oldVs,vi,&vn);ctx->PSGetShader(&oldPs,pi,&pn);ctx->GSGetShader(&oldGs,gi,&gn);
    ctx->HSGetShader(&oldHs,hi,&hn);ctx->DSGetShader(&oldDs,di,&dn);
    ComPtr<ID3D11InputLayout> oldLayout;D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
    ctx->IAGetInputLayout(&oldLayout);ctx->IAGetPrimitiveTopology(&oldTopology);
    D3D11_VIEWPORT oldViewports[16]{};UINT viewportCount=16;ctx->RSGetViewports(&viewportCount,oldViewports);
    D3D11_RECT oldScissors[16]{};UINT scissorCount=16;ctx->RSGetScissorRects(&scissorCount,oldScissors);
    ComPtr<ID3D11RasterizerState> oldRaster;ctx->RSGetState(&oldRaster);
    ComPtr<ID3D11DepthStencilState> oldDepth;UINT oldRef=0;ctx->OMGetDepthStencilState(&oldDepth,&oldRef);
    ComPtr<ID3D11BlendState> oldBlend;float oldFactor[4]{};UINT oldMask=0;ctx->OMGetBlendState(&oldBlend,oldFactor,&oldMask);
    ComPtr<ID3D11ShaderResourceView> oldInput;ctx->PSGetShaderResources(0,1,&oldInput);
    ComPtr<ID3D11SamplerState> oldSampler;ctx->PSGetSamplers(0,1,&oldSampler);
    ComPtr<ID3D11Buffer> oldConstants;ctx->PSGetConstantBuffers(0,1,&oldConstants);
    auto* rt=output.Get();ctx->OMSetRenderTargets(1,&rt,nullptr);
    D3D11_VIEWPORT full{0,0,float(dst.Width),float(dst.Height),0,1};ctx->RSSetViewports(1,&full);
    const D3D11_RECT scissor{l,t,r,b};ctx->RSSetScissorRects(1,&scissor);
    ctx->RSSetState(raster.Get());ctx->OMSetDepthStencilState(depth.Get(),0);ctx->OMSetBlendState(blend.Get(),nullptr,~0u);
    ctx->IASetInputLayout(nullptr);ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->PSSetShader(pixel.Get(),nullptr,0);
    ctx->GSSetShader(nullptr,nullptr,0);ctx->HSSetShader(nullptr,nullptr,0);ctx->DSSetShader(nullptr,nullptr,0);
    auto* srv=input.Get();auto* smp=sampler.Get();auto* cb=constants.Get();
    ctx->PSSetShaderResources(0,1,&srv);ctx->PSSetSamplers(0,1,&smp);ctx->PSSetConstantBuffers(0,1,&cb);
    ctx->Draw(3,0);
    ID3D11ShaderResourceView* none=nullptr;ctx->PSSetShaderResources(0,1,&none);
    ctx->OMSetRenderTargets(8,oldRt,oldDsv.Get());srv=oldInput.Get();ctx->PSSetShaderResources(0,1,&srv);
    smp=oldSampler.Get();cb=oldConstants.Get();ctx->PSSetSamplers(0,1,&smp);ctx->PSSetConstantBuffers(0,1,&cb);
    ctx->RSSetViewports(viewportCount,oldViewports);ctx->RSSetScissorRects(scissorCount,oldScissors);ctx->RSSetState(oldRaster.Get());
    ctx->OMSetDepthStencilState(oldDepth.Get(),oldRef);ctx->OMSetBlendState(oldBlend.Get(),oldFactor,oldMask);
    ctx->IASetInputLayout(oldLayout.Get());ctx->IASetPrimitiveTopology(oldTopology);
    ctx->VSSetShader(oldVs.Get(),vi,vn);ctx->PSSetShader(oldPs.Get(),pi,pn);ctx->GSSetShader(oldGs.Get(),gi,gn);
    ctx->HSSetShader(oldHs.Get(),hi,hn);ctx->DSSetShader(oldDs.Get(),di,dn);
    for(auto* v:oldRt) if(v) v->Release();
    for(UINT i=0;i<vn;++i) if(vi[i]) vi[i]->Release();for(UINT i=0;i<pn;++i) if(pi[i]) pi[i]->Release();
    for(UINT i=0;i<gn;++i) if(gi[i]) gi[i]->Release();for(UINT i=0;i<hn;++i) if(hi[i]) hi[i]->Release();
    for(UINT i=0;i<dn;++i) if(di[i]) di[i]->Release();
    composited.fetch_add(1,std::memory_order_relaxed);
    return true;
}
}
