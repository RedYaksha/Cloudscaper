#ifndef RENDERER_RESOURCES_H_
#define RENDERER_RESOURCES_H_

#include <functional>

#include "directx/d3dx12_core.h"
#include "renderer_types.h"
#include "wincodec.h"
#include "shader_types.h"

class Resource;
struct DescriptorConfiguration;

template<typename T>
concept IsResource = requires {
    std::derived_from<T, Resource>;
};

template<typename T>
concept IsDescriptorConfiguration = requires {
    std::derived_from<T, DescriptorConfiguration>;
};

template<typename T>
concept IsValidIndexBufferType = IsAnyOf<T, uint8_t, uint16_t, uint32_t, unsigned int>;


enum class DescriptorConfigType {
    Texture2D_UAV,
    Texture3D_UAV,

    NumDescriptorConfigTypes,
};

struct DescriptorConfiguration {
    DescriptorConfiguration(DescriptorConfigType type)
        : configType(type) {}

    DescriptorConfigType configType; 
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

    // TODO: need function name to be more descriptive on what type of descriptor it is
    // SRV_CBV_UAV, RTV, Sampler, etc.
    // virtual bool CreateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const { return false; };

// public facing descriptor creation, which internally calls the implementation version and will execute properly if
// the resource type supports the descriptor type
#define DEFINE_CREATE_DESCRIPTOR_FUNC(name) bool Create ## name ##(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, std::shared_ptr<DescriptorConfiguration> configData = nullptr) const { winrt::com_ptr<ID3D12Device> device; res_->GetDevice(__uuidof(ID3D12Device), device.put_void()); return Create ## name ## Implementation(cpuHandle, device, configData); }
    DEFINE_CREATE_DESCRIPTOR_FUNC(ShaderResourceView)
    DEFINE_CREATE_DESCRIPTOR_FUNC(UnorderedAccessView)
    DEFINE_CREATE_DESCRIPTOR_FUNC(ConstantBufferView)
    DEFINE_CREATE_DESCRIPTOR_FUNC(SamplerView)
    DEFINE_CREATE_DESCRIPTOR_FUNC(RenderTargetView)
    DEFINE_CREATE_DESCRIPTOR_FUNC(DepthStencilView)
#undef DEFINE_CREATE_DESCRIPTOR_FUNC
    
    bool CreateDescriptorByResourceType(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                        ResourceDescriptorType descriptorType,
                                        std::shared_ptr<DescriptorConfiguration> configData) const;

    virtual bool IsUploadNeeded() const { return false; };
    virtual bool IsDynamic() const { return false; }
    virtual void HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) { assert(false); };
    virtual void HandleDynamicUpload() { assert(false); };
    virtual bool GetOptimizedClearValue(D3D12_CLEAR_VALUE& clearVal) const { return false; };
    void ChangeState(D3D12_RESOURCE_STATES newState, std::vector<D3D12_RESOURCE_BARRIER>& barriers);
    void ChangeStateDirect(D3D12_RESOURCE_STATES newState, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);

protected:
// all derived children (specific type of resource) is responsible for implementing one (or more) of these so users can
// create descriptors from their resources.
#define DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(name) virtual bool Create ## name ## Implementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, winrt::com_ptr<ID3D12Device> device, std::shared_ptr<DescriptorConfiguration> configData) const { \
    WINRT_ASSERT("Not implemented!" && false); return false; }
    
    DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(ShaderResourceView)
    DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(UnorderedAccessView)
    DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(ConstantBufferView)
    DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(SamplerView)
    DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(RenderTargetView)
    DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC(DepthStencilView)
#undef DEFINE_CREATE_DESCRIPTOR_IMPL_FUNC
    
    friend class MemoryAllocator;
    friend class StaticMemoryAllocator;
    void SetNativeResource(winrt::com_ptr<ID3D12Resource> res) { res_ = res; }
    virtual void SetUploadResource(winrt::com_ptr<ID3D12Resource> res) { assert(false); }
    void SetIsReady(bool val) { isReady_ = val; }
    
    winrt::com_ptr<ID3D12Resource> res_;
    D3D12_RESOURCE_STATES state_;
    std::atomic_bool isReady_;
    
    // should be set by the memory allocator
    std::function<void()> initializeDynamicResourceFunc_;
    void* dynamicResMappedPtr_;
};

class Texture2D : public Resource {
public:
    
    Texture2D(DXGI_FORMAT format, uint32_t width, uint32_t height, bool useAsUAV, D3D12_RESOURCE_STATES initialState);
    
    D3D12_RESOURCE_DESC CreateResourceDesc() const override;
    bool CreateShaderResourceViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                winrt::com_ptr<ID3D12Device> device,
                                                std::shared_ptr<DescriptorConfiguration> configData) const override;
    
    bool CreateUnorderedAccessViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                 winrt::com_ptr<ID3D12Device> device,
                                                 std::shared_ptr<DescriptorConfiguration> configData) const override;

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
protected:
    Texture2D() = default;

    DXGI_FORMAT format_;
    uint32_t width_;
    uint32_t height_;
    bool useAsUAV_;
};

class ImageTexture2D : public Texture2D {
public:
    ImageTexture2D(std::string filePath);
    bool IsUploadNeeded() const override { return true; }
    virtual void HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    virtual void SetUploadResource(winrt::com_ptr<ID3D12Resource> res);
    winrt::com_ptr<IWICBitmapFrameDecode> GetWICFrame() const;
    uint32_t GetBitsPerPixel(REFGUID guid) const;

    void FreeSourceData();
private:
    
    std::string filePath_;
    std::vector<uint8_t> srcData_;
    void* uploadSrc_;
    winrt::com_ptr<ID3D12Resource> uploadRes_;
};

class RenderTarget : public Texture2D {
public:
    // constructor used for creating a RenderTarget from an already made resource (e.g. swap chain back buffers)
    RenderTarget(winrt::com_ptr<ID3D12Resource> res, D3D12_RESOURCE_STATES initState);


protected:
    bool CreateRenderTargetViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                              winrt::com_ptr<ID3D12Device> device,
                                              std::shared_ptr<DescriptorConfiguration> configData) const override;
};

enum class DepthBufferFormat {
    D16_UNORM = DXGI_FORMAT_D16_UNORM,
    D32_FLOAT = DXGI_FORMAT_D32_FLOAT,
    D24_UNORM_S8_UINT = DXGI_FORMAT_D24_UNORM_S8_UINT,
    D32_FLOAT_S8X24_UINT = DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
};

class DepthBuffer : public Texture2D {
public:
    DepthBuffer(DepthBufferFormat format, uint32_t width, uint32_t height);

    D3D12_RESOURCE_DESC CreateResourceDesc() const override;
    bool GetOptimizedClearValue(D3D12_CLEAR_VALUE& clearVal) const override;
protected:
    
    bool CreateDepthStencilViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                              winrt::com_ptr<ID3D12Device> device,
                                              std::shared_ptr<DescriptorConfiguration> configData) const override;
};

class Buffer : public Resource {
public:
    D3D12_RESOURCE_DESC CreateResourceDesc() const override {
        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(GetSizeInBytes(), D3D12_RESOURCE_FLAG_NONE);
        return desc;
    }

    bool CreateConstantBufferViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                winrt::com_ptr<ID3D12Device> device,
                                                std::shared_ptr<DescriptorConfiguration> configData) const override;

protected:
    
    virtual const void* GetSourceData() const = 0;
    virtual uint64_t GetSizeInBytes() const = 0;
    virtual uint64_t GetStrideInBytes() const = 0;
};

class StaticBuffer : public Buffer {
public:

    bool IsUploadNeeded() const override { return true; }
    virtual void HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    virtual void SetUploadResource(winrt::com_ptr<ID3D12Resource> res) { uploadRes_ = res; }
    
protected:
    StaticBuffer() = default;
    
private:
    winrt::com_ptr<ID3D12Resource> uploadRes_;
};


class DynamicBufferBase : public Buffer {
public:
    DynamicBufferBase(uint32_t resourceSizeInBytes)
        : resourceSizeInBytes_(resourceSizeInBytes) {}

    D3D12_RESOURCE_DESC CreateResourceDesc() const override;
    bool IsUploadNeeded() const override { return false; }
    bool IsDynamic() const override { return true; }
    void HandleDynamicUpload() override;
    
    void UpdateGPUData();
    
protected:
    DynamicBufferBase() = default;
    
    uint32_t resourceSizeInBytes_;
};


class VertexBufferBase {
public:
    virtual ~VertexBufferBase() = default;

    virtual bool CreateVertexBufferDescriptor(D3D12_VERTEX_BUFFER_VIEW& outView) const = 0; 
    
    const VertexBufferLayout& GetLayout() const { return layout_; }
    const VertexBufferUsage GetUsage() const { return usage_; }
    virtual uint32_t GetNumVertices() const = 0;
    
protected:
    VertexBufferBase() = default;

    VertexBufferBase(const VertexBufferLayout& layout, VertexBufferUsage usage, D3D_PRIMITIVE_TOPOLOGY topology)
        : layout_(layout),
          usage_(usage),
          topology_(topology) {}

protected:
    VertexBufferLayout layout_;
    VertexBufferUsage usage_;
    D3D_PRIMITIVE_TOPOLOGY topology_;
};

template <typename T>
class StaticVertexBuffer : public VertexBufferBase, public StaticBuffer {
public:

    StaticVertexBuffer(const std::vector<T>& source, VertexBufferLayout layout, VertexBufferUsage usage, D3D_PRIMITIVE_TOPOLOGY topology)
        : VertexBufferBase(layout, usage, topology), source_(source) {
        
    }
    
    bool CreateVertexBufferDescriptor(D3D12_VERTEX_BUFFER_VIEW& outView) const override {
        outView.BufferLocation = res_->GetGPUVirtualAddress();
        outView.SizeInBytes = GetSizeInBytes();
        outView.StrideInBytes = GetStrideInBytes();
        return true;
    }

    uint32_t GetNumVertices() const override { return source_.size(); }

protected:
    const void* GetSourceData() const override {
        return static_cast<const void*>(source_.data());
    }

    uint64_t GetSizeInBytes() const override {
        return sizeof(T) * source_.size();
    }
    
    uint64_t GetStrideInBytes() const override {
        return sizeof(T);
    }

private:
    const std::vector<T>& source_;
};

template <typename T>
class DynamicBuffer : public DynamicBufferBase {
public:
    DynamicBuffer(const T& source)
        : source_(source) {
        resourceSizeInBytes_ = sizeof(T);
    }

protected:
    const void* GetSourceData() const override {
        return static_cast<const void*>(&source_);
    }

    uint64_t GetSizeInBytes() const override {
        return sizeof(T);
    }
    
    uint64_t GetStrideInBytes() const override {
        return sizeof(T);
    }
    
private:
    const T& source_;
};

// TODO: really, this is just a DynamicArrayBuffer, that has a vertex layout
template <typename T>
class DynamicVertexBuffer : public VertexBufferBase, public DynamicBufferBase {
public:

    DynamicVertexBuffer(const std::vector<T>& source, VertexBufferLayout layout, VertexBufferUsage usage, D3D_PRIMITIVE_TOPOLOGY topology)
    :
    source_(source),
    VertexBufferBase(layout, usage, topology)
    {
        resourceSizeInBytes_ = DynamicVertexBuffer<T>::GetSizeInBytes() * 2;
    }
    
    bool CreateVertexBufferDescriptor(D3D12_VERTEX_BUFFER_VIEW& outView) const override {
        outView.BufferLocation = res_->GetGPUVirtualAddress();
        outView.SizeInBytes = GetSizeInBytes();
        outView.StrideInBytes = GetStrideInBytes();
        return true;
    }

    uint32_t GetNumVertices() const override { return source_.size(); }

protected:
    const void* GetSourceData() const override {
        return static_cast<const void*>(source_.data());
    }

    uint64_t GetSizeInBytes() const override {
        return sizeof(T) * source_.size();
    }
    
    uint64_t GetStrideInBytes() const override {
        return sizeof(T);
    }

private:
    const std::vector<T>& source_;
};

class IndexBufferBase : public StaticBuffer {
public:

    bool CreateIndexBufferDescriptor(D3D12_INDEX_BUFFER_VIEW& outView);

    virtual uint32_t GetNumIndices() const = 0;
    
protected:
    IndexBufferBase() = default;
};


template <IsValidIndexBufferType T>
class IndexBuffer : public IndexBufferBase {
public:
    
    IndexBuffer(const std::vector<T>& source): IndexBufferBase(),
        source_(source) {}


    virtual uint32_t GetNumIndices() const override { return source_.size(); }

protected:
    const void* GetSourceData() const override {
        return static_cast<const void*>(source_.data());
    }

    uint64_t GetSizeInBytes() const override {
        return sizeof(T) * source_.size();
    }

    uint64_t GetStrideInBytes() const override {
        return sizeof(T);
    }

private:
    const std::vector<T>& source_;
};

class Texture3D : public Resource {
public:
    struct UAVConfig : public DescriptorConfiguration {
        UAVConfig(uint32_t mip_slice, uint32_t first_depth_slice, uint32_t depth_size)
            : DescriptorConfiguration(DescriptorConfigType::Texture3D_UAV),
              mipSlice(mip_slice),
              firstDepthSlice(first_depth_slice),
              depthSize(depth_size) {}

        uint32_t mipSlice;
        uint32_t firstDepthSlice;
        uint32_t depthSize;
    };
    
    Texture3D(DXGI_FORMAT format,
              uint32_t width,
              uint32_t height,
              uint32_t depth,
              bool useAsUAV,
              uint32_t numMips,
              D3D12_RESOURCE_STATES initialState);
    
    D3D12_RESOURCE_DESC CreateResourceDesc() const override;
    bool CreateShaderResourceViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                winrt::com_ptr<ID3D12Device> device,
                                                std::shared_ptr<DescriptorConfiguration> configData) const override;
    
    bool CreateUnorderedAccessViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                 winrt::com_ptr<ID3D12Device> device,
                                                 std::shared_ptr<DescriptorConfiguration> configData) const override;

    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }
    uint32_t GetDepth() const { return depth_; }

protected:
    DXGI_FORMAT format_;
    uint32_t width_;
    uint32_t height_;
    uint32_t depth_;
    uint32_t numMips_;
    bool useAsUAV_;
    
};
#endif // RENDERER_RESOURCES_H_
