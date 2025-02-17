#ifndef RENDERER_SHADER_TYPES_H_
#define RENDERER_SHADER_TYPES_H_

#include <string>
#include "d3d12.h"
#include <map>
#include <set>
#include <winrt/windows.foundation.h>

class Resource;

enum class ShaderType {
    Vertex,
    Hull,
    Domain,
    Pixel,
    Geometry,
    
    Compute,
    
    NumShaderTypes
};

struct VertexInputLayoutElem {
    std::string semanticName;
    uint16_t semanticIndex;
    DXGI_FORMAT format;
};

inline bool operator<(const VertexInputLayoutElem& a, const VertexInputLayoutElem& b) {
    return memcmp(&a, &b, sizeof(VertexInputLayoutElem)) < 0;
    /*
    return (a.semanticName < b.semanticName) &&
           (a.semanticIndex < b.semanticIndex) &&
           (a.format < b.format);
   */
}

enum class ResourceDescriptorType {
    SRV,
    CBV,
    UAV,
    Sampler,
    RenderTarget,
    DepthStencil,
    
    Unknown, // not really a resource - useful for finding 32 bit constants entries

    NumResourceDescriptorTypes
};

struct ShaderRegister {
    ResourceDescriptorType type;
    uint16_t regSpace;
    uint16_t regNumber;

    ShaderRegister(ResourceDescriptorType type, uint16_t reg_space, uint16_t reg_number)
        : type(type),
          regSpace(reg_space),
          regNumber(reg_number) {}

    bool operator<(const ShaderRegister& other) const {
        return memcmp(this, &other, sizeof(ShaderRegister)) < 0;
    }
};

/*
inline bool operator<(const ShaderRegister& a, const ShaderRegister& b) {
    return memcmp(&a, &b, sizeof(ShaderRegister)) < 0;
}
*/

enum class ResourceBindMethod {
    Automatic,
    DescriptorTable,
    RootDescriptor,

    NumBindResourceMethods
};

struct ResourceInfo {
    std::weak_ptr<Resource> res;
    ResourceBindMethod bindMethod;
};

struct RootConstantInfo {
    const void* data;
    uint32_t num32BitValues;
};


typedef std::tuple<ResourceDescriptorType, uint16_t> RootParamUsageKey;

// map from <root parameter type (SRV,CBV,UAV), register space> to num registers used
typedef std::map<RootParamUsageKey, std::set<uint16_t>> RootParameterUsageMap;

namespace shader_utils {

    inline RootParamUsageKey CreateRootParamKey(ResourceDescriptorType type, uint16_t registerSpace) {
        return std::make_tuple(type, registerSpace);
    }
}

class RenderTask {
    virtual void Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const = 0;
};

class RootParameter {
public:
    virtual ~RootParameter() = default;
    
    RootParameter(uint32_t rootParamIndex, bool isCompute)
        : rootParamIndex_(rootParamIndex), isCompute_(isCompute)
    {}
    
    void Execute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const {

        if(isCompute_) {
            ExecuteCompute(cmdList);
        }
        else {
            ExecuteGraphics(cmdList);
        }
        
    }
    
protected:
    virtual void ExecuteGraphics(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const = 0;
    virtual void ExecuteCompute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const = 0;
    
    uint32_t rootParamIndex_;
    bool isCompute_;
};

class DescriptorTableParameter : public RootParameter {
public:
    DescriptorTableParameter(uint32_t rootParamIndex, bool isCompute, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
        : RootParameter(rootParamIndex, isCompute), gpuHandle_(gpuHandle) {}
    
    void ExecuteGraphics(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        cmdList->SetGraphicsRootDescriptorTable(rootParamIndex_, gpuHandle_);
    }
    
    void ExecuteCompute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        cmdList->SetComputeRootDescriptorTable(rootParamIndex_, gpuHandle_);
    }

private:
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle_;
};

class RootDescriptorParameter : public RootParameter {
public:
    RootDescriptorParameter(uint32_t rootParamIndex, bool isCompute, ID3D12Resource* res, ResourceDescriptorType descriptorType)
        : RootParameter(rootParamIndex, isCompute), res_(res), descriptorType_(descriptorType) {}
    
    void ExecuteGraphics(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        switch(descriptorType_) {
        case ResourceDescriptorType::SRV:
            cmdList->SetGraphicsRootShaderResourceView(rootParamIndex_, res_->GetGPUVirtualAddress());
            break;
        case ResourceDescriptorType::CBV:
            cmdList->SetGraphicsRootConstantBufferView(rootParamIndex_, res_->GetGPUVirtualAddress());
            break;
        case ResourceDescriptorType::UAV:
            cmdList->SetGraphicsRootUnorderedAccessView(rootParamIndex_, res_->GetGPUVirtualAddress());
            break;
        default:
            return;
        }
    }
    
    void ExecuteCompute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        switch(descriptorType_) {
        case ResourceDescriptorType::SRV:
            cmdList->SetComputeRootShaderResourceView(rootParamIndex_, res_->GetGPUVirtualAddress());
            break;
        case ResourceDescriptorType::CBV:
            cmdList->SetComputeRootConstantBufferView(rootParamIndex_, res_->GetGPUVirtualAddress());
            break;
        case ResourceDescriptorType::UAV:
            cmdList->SetComputeRootUnorderedAccessView(rootParamIndex_, res_->GetGPUVirtualAddress());
            break;
        default:
            return;
        }
    }
    
private:
    ID3D12Resource* res_;
    ResourceDescriptorType descriptorType_;
};

class RootConstantsParameter : public RootParameter {
public:
    RootConstantsParameter(uint32_t rootParamIndex, bool isCompute, const void* data, uint32_t num32BitValues)
        : RootParameter(rootParamIndex, isCompute), data_(data), num32BitValues_(num32BitValues) {}

    void ExecuteGraphics(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        // limitation, keeping destOffset to 0
        cmdList->SetGraphicsRoot32BitConstants(rootParamIndex_, num32BitValues_, data_, 0);
    }
    
    void ExecuteCompute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        cmdList->SetComputeRoot32BitConstants(rootParamIndex_, num32BitValues_, data_, 0);
    }

private:
    const void* data_;
    uint32_t num32BitValues_;
};

typedef std::string ResourceID;

struct RenderTargetHandle {
    std::vector<std::shared_ptr<class RenderTarget>> resources;
    ResourceID id;
    DXGI_FORMAT format;
    DXGI_SAMPLE_DESC sampleDesc;
};

struct DepthStencilTargetHandle {
    std::vector<std::shared_ptr<class DepthBuffer>> resources;
    ResourceID id;
    DXGI_FORMAT format;
    DXGI_SAMPLE_DESC sampleDesc;
};

// represents a "group" of render target handles, where
// the id is a tuple of the identifiers. Used to identify
// groups of render targets that will be targeted in a graphics
// pipeline - which makes it easier to reuse descriptor allocations.
struct RenderTargetGroupID {

    RenderTargetGroupID() = default;

    RenderTargetGroupID(std::vector<ResourceID> inIds) {
        concatIds = "";
        ids = inIds;

        for(const auto& s : ids) {
            concatIds += s;
        }
    }

    auto operator <=>(const RenderTargetGroupID& rhs) const {
        return concatIds <=> rhs.concatIds;
    }
    bool operator==(const RenderTargetGroupID& rhs) const {
        return concatIds == rhs.concatIds;
    }

    

    const std::vector<ResourceID>& GetIDs() const { return ids; }
    const std::string& GetConcatenatedID() const { return concatIds; }
    
private:
    std::vector<ResourceID> ids;
    std::string concatIds;
};

struct RenderTargetGroupIDHasher {
    std::size_t operator()(const RenderTargetGroupID& k) const {
        return std::hash<std::string>()(k.GetConcatenatedID());
    }
};

enum class ShaderDataType {
    Float,
    Float2,
    Float3,
    Float4,

    Int,
    Int2,
    Int3,
    Int4,

    UInt,
    UInt2,
    UInt3,
    UInt4,
};

enum class VertexBufferUsage {
    PerVertex,
    PerInstance
};

struct VertexBufferLayout {
    struct Element {
        std::string semanticName;
        uint16_t semanticIndex;
        ShaderDataType dataType;
    };

    VertexBufferLayout(const std::vector<Element>& elems)
        : elements(std::move(elems)) {}

    VertexBufferLayout() = default;

    std::vector<Element> elements;
};




#endif // RENDERER_SHADER_TYPES_H_
