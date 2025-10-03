#ifndef RENDERER_RENDERER_COMMON_H_
#define RENDERER_RENDERER_COMMON_H_

#include "renderer_types.h"

namespace renderer_common {
    inline static D3D12_SAMPLER_DESC samplerLinearClamp = D3D12_SAMPLER_DESC {
    .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    .MipLODBias = 0,
    .MaxAnisotropy = 0,
    .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS,
    .BorderColor = {0, 0, 0, 0},
    .MinLOD = 0,
    .MaxLOD = 0
    };
    
    inline static D3D12_SAMPLER_DESC samplerLinearWrap = D3D12_SAMPLER_DESC {
    .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
    .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .MipLODBias = 0,
    .MaxAnisotropy = 0,
    .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS,
    .BorderColor = {0, 0, 0, 0},
    .MinLOD = 0,
    .MaxLOD = 0
    };
    
    inline static D3D12_SAMPLER_DESC samplerPointWrap= D3D12_SAMPLER_DESC {
    .Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
    .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
    .MipLODBias = 0,
    .MaxAnisotropy = 0,
    .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS,
    .BorderColor = {0, 0, 0, 0},
    .MinLOD = 0,
    .MaxLOD = 0
    };
    
    inline static D3D12_SAMPLER_DESC samplerPointClamp = D3D12_SAMPLER_DESC {
    .Filter = D3D12_FILTER_MIN_MAG_MIP_POINT,
    .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
    .MipLODBias = 0,
    .MaxAnisotropy = 0,
    .ComparisonFunc = D3D12_COMPARISON_FUNC_LESS,
    .BorderColor = {0, 0, 0, 0},
    .MinLOD = 0,
    .MaxLOD = 0
    };
    
} // renderer_common
#endif // RENDERER_RENDERER_COMMON_H_
