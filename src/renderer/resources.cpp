#include "directx/d3dx12.h"
#include "resources.h"

#include <iostream>

#include "windows.h"
#include "wincodec.h"

winrt::com_ptr<ID3D12Resource> Resource::GetNativeResource() {
    // UpdateNativeResource();
    // resource should not be null when updated
    winrt::check_pointer(res_.get());
    return res_;
}

bool Resource::CreateDescriptorByResourceType(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                              ResourceDescriptorType descriptorType,
                                              std::shared_ptr<DescriptorConfiguration> configData) const {
    switch(descriptorType) {
    case ResourceDescriptorType::SRV:
        return CreateShaderResourceView(cpuHandle, configData);
    case ResourceDescriptorType::UAV:
        return CreateUnorderedAccessView(cpuHandle, configData);
    case ResourceDescriptorType::CBV:
        return CreateConstantBufferView(cpuHandle, configData);
    case ResourceDescriptorType::Sampler:
        return CreateSamplerView(cpuHandle, configData);
    case ResourceDescriptorType::RenderTarget:
        return CreateRenderTargetView(cpuHandle, configData);
    case ResourceDescriptorType::DepthStencil:
        return CreateDepthStencilView(cpuHandle, configData);
    default:
        return false;
    }
}

void Resource::ChangeState(D3D12_RESOURCE_STATES newState, std::vector<D3D12_RESOURCE_BARRIER>& barriers) {
    if(newState == state_) {
        return;
    }

    const D3D12_RESOURCE_STATES oldState = state_;
    
    CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(res_.get(),
        oldState,
        newState);
    
    barriers.push_back(barrier);
    
    state_ = newState;
}

void Resource::ChangeStateDirect(D3D12_RESOURCE_STATES newState, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    ChangeState(newState, barriers);

    if(barriers.size() > 0) {
        cmdList->ResourceBarrier(barriers.size(), barriers.data());
    }
}

Texture2D::Texture2D(DXGI_FORMAT format, uint32_t width, uint32_t height, bool useAsUAV, D3D12_RESOURCE_STATES initialState)
    : format_(format), width_(width), height_(height), useAsUAV_(useAsUAV), useAsRenderTarget_(false) {
    
    isReady_ = false;
    state_ = initialState;
}

D3D12_RESOURCE_DESC Texture2D::CreateResourceDesc() const {
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
                                    format_, width_, height_, 1, 1);

    if(useAsUAV_) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    if(useAsRenderTarget_) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    
    return desc;
}

bool Texture2D::CreateShaderResourceViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                       winrt::com_ptr<ID3D12Device> device,
                                                       std::shared_ptr<DescriptorConfiguration> configData) const {
    
    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    desc.Format = format_;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    // TODO
    desc.Texture2D.MipLevels = 1;
    desc.Texture2D.PlaneSlice = 0;
    desc.Texture2D.MostDetailedMip = 0;
    desc.Texture2D.ResourceMinLODClamp = 0.f;

    device->CreateShaderResourceView(res_.get(), &desc, cpuHandle);
    return true;
}

bool Texture2D::CreateUnorderedAccessViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                        winrt::com_ptr<ID3D12Device> device,
                                                        std::shared_ptr<DescriptorConfiguration> configData) const {

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
    desc.Format = format_;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Texture2D.PlaneSlice = 0;

    device->CreateUnorderedAccessView(res_.get(), nullptr, &desc, cpuHandle);
    return true;
}

ImageTexture2D::ImageTexture2D(std::string filePath)
    : filePath_(filePath) {

    // check if file exists
    const DWORD fileAttrs = GetFileAttributes(filePath_.c_str());
    assert(fileAttrs != INVALID_FILE_ATTRIBUTES && !(fileAttrs & FILE_ATTRIBUTE_DIRECTORY));

    winrt::com_ptr<IWICBitmapFrameDecode> frame = GetWICFrame();

    UINT width, height;
    HRESULT hr = frame->GetSize(&width, &height);
    winrt::check_hresult(hr);

    // assume 32 bpp and DXGI_FORMAT_R8G8B8A8
    // StaticMemoryAllocator will allocate width*height amount of bytes
    width_ = (uint32_t) width;
    height_ = (uint32_t) height;

    // TODO: more complex formats according to metadata?
    // See WICTextureLoader::CreateTextureFromWIC
    format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
    state_ = D3D12_RESOURCE_STATE_COMMON;
    useAsUAV_ = false;
    useAsRenderTarget_ = false;
}

void ImageTexture2D::HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    winrt::check_pointer(uploadRes_.get());
    
    HRESULT hr;

    // load image to memory
    winrt::com_ptr<IWICBitmapFrameDecode> frame = GetWICFrame();

    WICPixelFormatGUID pixelFormat;
    hr = frame->GetPixelFormat(&pixelFormat);
    
    winrt::check_hresult(hr);

    // force 32 bpp = R8B8G8A8
    // 128 bpp = R32B32G32A32
    const uint32_t bpp = 32; // 32;
    WICPixelFormatGUID finalPixelFmt;
    
    // Aside: memcpy_s is the more secure version, and WICPixelFormatGUID == GUID, constants are GUID hence the type difference
    // memcpy_s(&finalPixelFmt, sizeof(WICPixelFormatGUID), &pixelFormat, sizeof(GUID));
    memcpy_s(&finalPixelFmt, sizeof(WICPixelFormatGUID), &GUID_WICPixelFormat32bppRGBA, sizeof(GUID));
    // memcpy_s(&finalPixelFmt, sizeof(WICPixelFormatGUID), &GUID_WICPixelFormat128bppRGBAFloat, sizeof(GUID));

    // resolve size
    // adding 7 dividing 8 => rounds up for a valid byte count
    uint32_t rowPitch = (width_ * bpp + 7u) / 8u;
    uint32_t cbStride = ((bpp * width_ + 31) >> 3) & 0xFFFFFFFC;
    uint32_t b = GetBitsPerPixel(pixelFormat);

    std::cout << cbStride << " -- " << b << std::endl;
    
    uint32_t totalBytes = rowPitch * height_;

    // prepare srcData container, receiving the data
    srcData_.resize(totalBytes);

    D3D12_RANGE readRange = {0, 0};
    hr = uploadRes_->Map(0, &readRange, &uploadSrc_);
    winrt::check_hresult(hr);
    
    D3D12_RESOURCE_DESC uploadDesc = uploadRes_->GetDesc();
    assert(totalBytes <= uploadDesc.Height * uploadDesc.Width);

    // (For now) only support RGBA32
    // if(memcmp(&pixelFormat, &finalPixelFmt, sizeof(GUID)) != 0) {
    if(!IsEqualGUID(pixelFormat, finalPixelFmt)) {
        // convert
        winrt::com_ptr<IWICImagingFactory> imgFactory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
        winrt::com_ptr<IWICFormatConverter> converter;
        hr = imgFactory->CreateFormatConverter(converter.put());
        CHECK_HR(hr);

        BOOL canConvert;
        hr = converter->CanConvert(pixelFormat, finalPixelFmt, &canConvert);
        CHECK_HR(hr);

        assert(canConvert);

        hr = converter->Initialize(frame.get(), finalPixelFmt, WICBitmapDitherTypeErrorDiffusion, NULL, 0, WICBitmapPaletteTypeMedianCut);
        CHECK_HR(hr);

        hr = converter->CopyPixels(NULL, rowPitch, totalBytes, static_cast<unsigned char*>(uploadSrc_));
        CHECK_HR(hr);
    }
    else {
        hr = frame->CopyPixels(NULL, rowPitch, totalBytes, static_cast<unsigned char*>(uploadSrc_));
        CHECK_HR(hr);
    }

    // issue copy command
    winrt::com_ptr<ID3D12Device> device;
    hr = res_->GetDevice(__uuidof(ID3D12Device), device.put_void());
    CHECK_HR(hr);

    // use the destination's layout, ie interpret our buffer of raw bytes as a resource with
    // destination's Width, Height, etc. - for an accurate copy
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT dstPlacedFootprint;
    D3D12_RESOURCE_DESC resDesc = res_->GetDesc();
    device->GetCopyableFootprints(&resDesc, 0, 1, 0, &dstPlacedFootprint, NULL, NULL, NULL);
    
    CD3DX12_TEXTURE_COPY_LOCATION dstLoc = CD3DX12_TEXTURE_COPY_LOCATION(res_.get(), 0);
    CD3DX12_TEXTURE_COPY_LOCATION srcLoc = CD3DX12_TEXTURE_COPY_LOCATION(uploadRes_.get(), dstPlacedFootprint);
    cmdList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, NULL);
} 

void ImageTexture2D::SetUploadResource(winrt::com_ptr<ID3D12Resource> res) {
    uploadRes_ = res;
}

winrt::com_ptr<IWICBitmapFrameDecode> ImageTexture2D::GetWICFrame() const {
    
    // load props to memory
    winrt::com_ptr<IWICImagingFactory> imgFactory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);

    std::wstring filePath_w;
    {
        const int numBytesNeeded = MultiByteToWideChar(CP_UTF8, 0, filePath_.c_str(), -1, NULL, 0);
        assert(numBytesNeeded >= 0);
        
        filePath_w = std::wstring(numBytesNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, filePath_.c_str(), -1, filePath_w.data(), numBytesNeeded);
    }

    HRESULT hr;
    
    winrt::com_ptr<IWICBitmapDecoder> decoder;

    hr = imgFactory->CreateDecoderFromFilename(
        filePath_w.c_str(),
        NULL,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        decoder.put()
    );
    winrt::check_hresult(hr);

    winrt::com_ptr<IWICMetadataQueryReader> metadataQueryReader;
    winrt::com_ptr<IWICBitmapFrameDecode> frame;
    
    hr = decoder->GetFrame(0, frame.put());
    winrt::check_hresult(hr);

    return frame;
}

uint32_t ImageTexture2D::GetBitsPerPixel(const GUID& guid) const {
    winrt::com_ptr<IWICImagingFactory> imgFactory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICComponentInfo> componentInfo;
    HRESULT hr;
    hr = imgFactory->CreateComponentInfo(guid, componentInfo.put());
    WINRT_ASSERT(SUCCEEDED(hr));

    WICComponentType type;
    hr = componentInfo->GetComponentType(&type);
    WINRT_ASSERT(SUCCEEDED(hr));

    WINRT_ASSERT(type == WICPixelFormat);

    winrt::com_ptr<IWICPixelFormatInfo> pixelFormatInfo;
    componentInfo.as(pixelFormatInfo);

    UINT bpp;
    hr = pixelFormatInfo->GetBitsPerPixel(&bpp);
    WINRT_ASSERT(SUCCEEDED(hr));

    UINT numChannels;
    pixelFormatInfo->GetChannelCount(&numChannels);
    // std::cout << "bpp: " << bpp << " channels: " << numChannels << std::endl;
    return bpp;
}

void ImageTexture2D::FreeSourceData() {

}

RenderTarget::RenderTarget(winrt::com_ptr<ID3D12Resource> res, D3D12_RESOURCE_STATES initState) {
    res_ = res;
    state_ = initState;
    isReady_ = true;
}

RenderTarget::RenderTarget(DXGI_FORMAT format, uint32_t width, uint32_t height, bool useAsUAV,
    D3D12_RESOURCE_STATES initialState)
        : Texture2D(format, width, height, useAsUAV, initialState) {

    useAsRenderTarget_ = true;
}

RenderTarget::RenderTarget(D3D12_RESOURCE_STATES initState) {
    state_ = initState;
    isReady_ = false;
}

bool RenderTarget::GetOptimizedClearValue(D3D12_CLEAR_VALUE& clearVal) const {
    clearVal.Format = (DXGI_FORMAT) format_;
    clearVal.Color[0] = 0;
    clearVal.Color[1] = 0;
    clearVal.Color[2] = 0;
    clearVal.Color[3] = 1;
    return true;
}

bool RenderTarget::CreateRenderTargetViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                        winrt::com_ptr<ID3D12Device> device,
                                                        std::shared_ptr<DescriptorConfiguration> configData) const {
    D3D12_RESOURCE_DESC resDesc = res_->GetDesc();
    
    D3D12_RENDER_TARGET_VIEW_DESC desc;
    desc.Format = resDesc.Format;

    // TODO, perhaps another type, should we really extend from Texture2D
    // but how about re-using this resource as a Texture2D
    desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    
    // TODO configure
    desc.Texture2D.MipSlice = 0;
    desc.Texture2D.PlaneSlice = 0;

    device->CreateRenderTargetView(res_.get(), &desc, cpuHandle);

    return true;
}

DepthBuffer::DepthBuffer(DepthBufferFormat format, uint32_t width, uint32_t height) {
    format_ = (DXGI_FORMAT) format;
    width_ = width;
    height_ = height;
    state_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    useAsUAV_ = false;
    useAsRenderTarget_ = false;
}

D3D12_RESOURCE_DESC DepthBuffer::CreateResourceDesc() const {
    D3D12_RESOURCE_DESC desc = Texture2D::CreateResourceDesc();
    desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    return desc;
}

bool DepthBuffer::GetOptimizedClearValue(D3D12_CLEAR_VALUE& clearVal) const {
    clearVal.Format = (DXGI_FORMAT) format_;
    clearVal.DepthStencil = {1.0f, 0};
    return true;
}

bool DepthBuffer::CreateDepthStencilViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                       winrt::com_ptr<ID3D12Device> device,
                                                       std::shared_ptr<DescriptorConfiguration> configData) const {

    D3D12_DEPTH_STENCIL_VIEW_DESC desc;
    desc.Format = format_;
    desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Flags = D3D12_DSV_FLAG_NONE;
    
    device->CreateDepthStencilView(res_.get(), &desc, cpuHandle);
    return true;
}

bool Buffer::CreateConstantBufferViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                    winrt::com_ptr<ID3D12Device> device,
                                                    std::shared_ptr<DescriptorConfiguration> configData) const {
    D3D12_CONSTANT_BUFFER_VIEW_DESC desc;
    desc.BufferLocation = res_->GetGPUVirtualAddress();
    desc.SizeInBytes = GetSizeInBytes();
    device->CreateConstantBufferView(&desc, cpuHandle);
    return true;
}


void StaticBuffer::HandleUpload(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    winrt::check_pointer(uploadRes_.get());

    const void* srcData = GetSourceData();
    const uint64_t srcSizeInBytes = GetSizeInBytes();

    D3D12_RANGE readRange = {0, 0};
    void* mappedData;
    uploadRes_->Map(0, &readRange, &mappedData);
    memcpy(mappedData, GetSourceData(), srcSizeInBytes);
    uploadRes_->Unmap(0, nullptr);

    cmdList->CopyBufferRegion(res_.get(), 0, uploadRes_.get(), 0, srcSizeInBytes);
}


D3D12_RESOURCE_DESC DynamicBufferBase::CreateResourceDesc() const {
    const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(resourceSizeInBytes_, D3D12_RESOURCE_FLAG_NONE);
    return desc;
}

void DynamicBufferBase::HandleDynamicUpload() {
    memcpy(dynamicResMappedPtr_, GetSourceData(), GetSizeInBytes());
}

void DynamicBufferBase::UpdateGPUData() {
    // call the callback (should have been set when this resource was made)
    //
    // This makes sure we have an up to date native resource, and a valid mapped pointer
    // - if the source data is larger than the destination, then the gpu resource will expand
    //
    if(GetSizeInBytes() > resourceSizeInBytes_) {
        resourceSizeInBytes_ *= 2;
        initializeDynamicResourceFunc_();
    }
    
    // memcpy the source to mapped dest
    HandleDynamicUpload();
}

bool IndexBufferBase::CreateIndexBufferDescriptor(D3D12_INDEX_BUFFER_VIEW& outView) {
    outView.BufferLocation = res_->GetGPUVirtualAddress();
    outView.SizeInBytes = GetSizeInBytes();

    DXGI_FORMAT format;
    switch(GetStrideInBytes()) {
    case sizeof(uint8_t):
        format = DXGI_FORMAT_R8_UINT;
        break;
    case sizeof(uint16_t):
        format = DXGI_FORMAT_R16_UINT;
        break;
    case sizeof(uint32_t):
        format = DXGI_FORMAT_R32_UINT;
        break;
    default:
        return false;
    }

    outView.Format = format;
    return true;
}

Texture3D::Texture3D(DXGI_FORMAT format, uint32_t width, uint32_t height, uint32_t depth, bool useAsUAV,
    uint32_t numMips, D3D12_RESOURCE_STATES initialState)
: format_(format), width_(width), height_(height), depth_(depth), numMips_(numMips), useAsUAV_(useAsUAV) {
    
    isReady_ = false;
    state_ = initialState;
}

D3D12_RESOURCE_DESC Texture3D::CreateResourceDesc() const {
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex3D(format_, width_, height_, depth_, numMips_);
    
    if(useAsUAV_) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    return desc;
}

bool Texture3D::CreateShaderResourceViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                       winrt::com_ptr<ID3D12Device> device,
                                                       std::shared_ptr<DescriptorConfiguration> configData) const {
    
    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    desc.Format = format_;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    
    desc.Texture3D.MipLevels = numMips_;
    desc.Texture3D.MostDetailedMip = 0;
    desc.Texture3D.ResourceMinLODClamp = 0.f;

    device->CreateShaderResourceView(res_.get(), &desc, cpuHandle);
    return true;
}

bool Texture3D::CreateUnorderedAccessViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                        winrt::com_ptr<ID3D12Device> device,
                                                        std::shared_ptr<DescriptorConfiguration> configData) const {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc;
    desc.Format = format_;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    
    if(configData) {
        WINRT_ASSERT(configData->configType == DescriptorConfigType::Texture3D_UAV);
        std::shared_ptr<Texture3D::UAVConfig> config = std::static_pointer_cast<Texture3D::UAVConfig>(configData);
        
        desc.Texture3D.MipSlice = config->mipSlice;
        desc.Texture3D.WSize = config->depthSize;
        desc.Texture3D.FirstWSlice = config->firstDepthSlice;
    }
    else {
        desc.Texture3D.MipSlice = 0;
        desc.Texture3D.WSize = depth_;
        desc.Texture3D.FirstWSlice = 0;
    }

    device->CreateUnorderedAccessView(res_.get(), nullptr, &desc, cpuHandle);

    return true;
}
