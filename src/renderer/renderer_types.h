#ifndef RENDERER_RENDERER_TYPES_H_
#define RENDERER_RENDERER_TYPES_H_

#include <d3d12.h>
#include <dxgi1_5.h>
#include <winrt/windows.foundation.h>

template <typename T, typename... U>
concept IsAnyOf = (std::same_as<T, U> || ...);

#define CHECK_HR_RET(hr, ret) if(FAILED(hr)) return ret;
#define CHECK_HR(hr) if(FAILED(hr)) return;
#define CHECK_HR_NULL(hr) CHECK_HR_RET(hr, NULL)

#endif // RENDERER_RENDERER_TYPES_H_
