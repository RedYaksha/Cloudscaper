#ifndef RENDERER_RESOURCES_H_
#define RENDERER_RESOURCES_H_

#include "renderer_types.h"
#include "wincodec.h"

class Resource;

template<typename T>
concept IsResource = requires {
    std::derived_from<T, Resource>;
    typename T::Config;
};

class Resource {
public:
    virtual ~Resource() {};
    
    // native resource is only set if a memory allocator has actually
    // created it
    bool HasNativeResource() const { return res_ != nullptr; }
    bool IsReady() const { return isReady_.load(); };
    
    winrt::com_ptr<ID3D12Resource> GetNativeResource();
    D3D12_RESOURCE_STATES GetResourceState() const { return state_; }

    // needed for a memory allocator to create the native resource
    virtual D3D12_RESOURCE_DESC CreateResourceDesc() const = 0;
    virtual bool CreateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const = 0;
    
    virtual D3D12_RESOURCE_DESC CreateUploadResourceDesc() const { return D3D12_RESOURCE_DESC {};};
    virtual bool IsUploadNeeded() const { return false; };
    virtual void HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) { assert(false); };
    virtual bool GetOptimizedClearValue(D3D12_CLEAR_VALUE& clearVal) const { return false; };

protected:
    friend class MemoryAllocator;
    friend class StaticMemoryAllocator;
    void SetNativeResource(winrt::com_ptr<ID3D12Resource> res) { res_ = res; }
    virtual void SetUploadResource(winrt::com_ptr<ID3D12Resource> res) { assert(false); }
    void SetIsReady(bool val) { isReady_ = val; }
    
    winrt::com_ptr<ID3D12Resource> res_;
    D3D12_RESOURCE_STATES state_;
    std::atomic_bool isReady_;
};

class Texture2D : public Resource {
public:
    struct Config {
        DXGI_FORMAT format;
        uint32_t width;
        uint32_t height;
    };
    
    Texture2D(const Config& config);
    
    D3D12_RESOURCE_DESC CreateResourceDesc() const override;
    bool CreateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const override;
    
protected:
    Texture2D() = default;
    
    Config config_;
};

class ImageTexture2D : public Texture2D {
public:
    struct Config {
        std::string filePath;
    };
    
    ImageTexture2D(const Config& config);
    bool IsUploadNeeded() const override { return true; }
    D3D12_RESOURCE_DESC CreateUploadResourceDesc() const override;
    virtual void HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    virtual void SetUploadResource(winrt::com_ptr<ID3D12Resource> res);
    winrt::com_ptr<IWICBitmapFrameDecode> GetWICFrame() const;

    void FreeSourceData();
private:
    
    std::string filePath_;
    std::vector<uint8_t> srcData;
    void* uploadSrc_;
    winrt::com_ptr<ID3D12Resource> uploadRes_;
};

class RenderTarget : public Texture2D {
public:
    
};

class StaticBuffer : public Resource {
public:
    
};

class DynamicBuffer : public Resource {
public:
    
};

#endif // RENDERER_RESOURCES_H_
