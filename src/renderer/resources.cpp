#include "directx/d3dx12.h"
#include "resources.h"
#include "windows.h"
#include "wincodec.h"

winrt::com_ptr<ID3D12Resource> Resource::GetNativeResource() {
    // UpdateNativeResource();
    // resource should not be null when updated
    winrt::check_pointer(res_.get());
    return res_;
}

bool Resource::CreateDescriptorByResourceType(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                              ResourceDescriptorType descriptorType) const {
    switch(descriptorType) {
    case ResourceDescriptorType::SRV:
        return CreateShaderResourceView(cpuHandle);
    case ResourceDescriptorType::UAV:
        return CreateUnorderedAccessView(cpuHandle);
    case ResourceDescriptorType::CBV:
        return CreateConstantBufferView(cpuHandle);
    case ResourceDescriptorType::Sampler:
        return CreateSamplerView(cpuHandle);
    case ResourceDescriptorType::RenderTarget:
        return CreateRenderTargetView(cpuHandle);
    case ResourceDescriptorType::DepthStencil:
        return CreateDepthStencilView(cpuHandle);
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
    : format_(format), width_(width), height_(height), useAsUAV_(useAsUAV) {
    
    isReady_ = false;
    state_ = initialState;
}

D3D12_RESOURCE_DESC Texture2D::CreateResourceDesc() const {
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
                                    format_, width_, height_);

    if(useAsUAV_) {
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    return desc;
}

bool Texture2D::CreateShaderResourceViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    winrt::com_ptr<ID3D12Device> device) const {
    
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
    winrt::com_ptr<ID3D12Device> device) const {

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
    const uint32_t bpp = 32;
    WICPixelFormatGUID finalPixelFmt;
    
    // Aside: memcpy_s is the more secure version, and WICPixelFormatGUID == GUID, constants are GUID hence the type difference
    // memcpy_s(&finalPixelFmt, sizeof(WICPixelFormatGUID), &pixelFormat, sizeof(GUID));
    memcpy_s(&finalPixelFmt, sizeof(WICPixelFormatGUID), &GUID_WICPixelFormat32bppRGBA, sizeof(GUID));
    
    // resolve size
    // adding 7 dividing 8 => rounds up for a valid byte count
    uint32_t rowPitch = (width_ * bpp + 7u) / 8u;
    uint32_t totalBytes = rowPitch * height_;

    // prepare srcData container, receiving the data
    srcData_.resize(totalBytes);

    D3D12_RANGE readRange = {0, 0};
    hr = uploadRes_->Map(0, &readRange, &uploadSrc_);
    winrt::check_hresult(hr);
    
    D3D12_RESOURCE_DESC uploadDesc = uploadRes_->GetDesc();
    assert(totalBytes <= uploadDesc.Height * uploadDesc.Width);

    // (For now) only support RGBA32
    if(memcmp(&pixelFormat, &finalPixelFmt, sizeof(GUID)) != 0) {
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

        converter->CopyPixels(NULL, rowPitch, totalBytes, static_cast<unsigned char*>(uploadSrc_));
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
    CD3DX12_TEXTURE_COPY_LOCATION srcLoc = CD3DX12_TEXTURE_COPY_LOCATION(uploadRes_.get(), dstPlacedFootprint);
    
    CD3DX12_TEXTURE_COPY_LOCATION dstLoc = CD3DX12_TEXTURE_COPY_LOCATION(res_.get(), 0);
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

void ImageTexture2D::FreeSourceData() {

}

RenderTarget::RenderTarget(winrt::com_ptr<ID3D12Resource> res, D3D12_RESOURCE_STATES initState) {
    res_ = res;
    state_ = initState;
}

bool RenderTarget::CreateRenderTargetViewImplementation(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
    winrt::com_ptr<ID3D12Device> device) const {
    
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
                                                       winrt::com_ptr<ID3D12Device> device) const {

    D3D12_DEPTH_STENCIL_VIEW_DESC desc;
    desc.Format = format_;
    desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = 0;
    desc.Flags = D3D12_DSV_FLAG_NONE;
    
    device->CreateDepthStencilView(res_.get(), &desc, cpuHandle);
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


D3D12_RESOURCE_DESC DynamicBuffer::CreateResourceDesc() const {
    const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(resourceSizeInBytes_, D3D12_RESOURCE_FLAG_NONE);
    return desc;
}

void DynamicBuffer::HandleDynamicUpload() {
    memcpy(dynamicResMappedPtr_, GetSourceData(), GetSizeInBytes());
}

void DynamicBuffer::UpdateGPUData() {
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
