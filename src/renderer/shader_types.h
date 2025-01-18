#ifndef RENDERER_SHADER_TYPES_H_
#define RENDERER_SHADER_TYPES_H_

#include <string>
#include "d3d12.h"
#include <map>
#include <set>
#include <winrt/windows.foundation.h>

class Resource;

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
    void* data;
    uint32_t num32BitValues;
};


typedef std::tuple<ResourceDescriptorType, uint16_t> RootParamUsageKey;
typedef std::map<RootParamUsageKey, std::set<uint16_t>> RootParameterUsageMap;

namespace shader_utils {

    // map from <root parameter type (SRV,CBV,UAV), register space> to num registers used
    inline RootParamUsageKey CreateRootParamKey(ResourceDescriptorType type, uint16_t registerSpace) {
        return std::make_tuple(type, registerSpace);
    }
}

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
    RootConstantsParameter(uint32_t rootParamIndex, bool isCompute, void* data, uint32_t num32BitValues)
        : RootParameter(rootParamIndex, isCompute), data_(data), num32BitValues_(num32BitValues) {}

    void ExecuteGraphics(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        // limitation, keeping destOffset to 0
        cmdList->SetGraphicsRoot32BitConstants(rootParamIndex_, num32BitValues_, data_, 0);
    }
    
    void ExecuteCompute(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) const override {
        cmdList->SetComputeRoot32BitConstants(rootParamIndex_, num32BitValues_, data_, 0);
    }

private:
    void* data_;
    uint32_t num32BitValues_;
};

#endif // RENDERER_SHADER_TYPES_H_
