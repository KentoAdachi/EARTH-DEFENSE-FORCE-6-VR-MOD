#pragma once
#include "camera_math.h"
#include <d3d11.h>
#include <cstdint>

namespace edf6vr {
// The SVU01 glass is removed by the companion RAB override. These points are
// on its rear aperture, in the exact weapon matrix used for this native pair.
struct ScopeZoomPose {
    float center[3]{},right[3]{},up[3]{};
    std::uint64_t frame=0;
};
void PublishScopeZoom(const Matrix& weaponRoot,std::uint64_t frame) noexcept;
void ClearScopeZoom() noexcept;
bool ReadScopeZoom(std::uint64_t frame,ScopeZoomPose& out) noexcept;
bool CompositeScopeZoom(ID3D11DeviceContext*,ID3D11Texture2D* world,
                        ID3D11Texture2D* target,const Matrix& view,
                        const Matrix& projection,const ScopeZoomPose&) noexcept;
const char* ScopeZoomStatus() noexcept;
void ReleaseScopeZoom() noexcept;
}
