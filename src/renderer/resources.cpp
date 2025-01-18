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

Texture2D::Texture2D(const Config& config)
    : config_(config) {
    
    isReady_ = false;
    state_ = D3D12_RESOURCE_STATE_COPY_DEST;
}

D3D12_RESOURCE_DESC Texture2D::CreateResourceDesc() const {
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
                                    config_.format, config_.width, config_.height);

    
    
    return desc;
}

bool Texture2D::CreateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) const {
    WINRT_ASSERT(res_);
    
    HRESULT hr;

    winrt::com_ptr<ID3D12Device> device;
    res_->GetDevice(__uuidof(ID3D12Device), device.put_void());

    D3D12_SHADER_RESOURCE_VIEW_DESC desc;
    desc.Format = config_.format;
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


ImageTexture2D::ImageTexture2D(const Config& config)
    : filePath_(config.filePath) {

    // check if file exists
    const DWORD fileAttrs = GetFileAttributes(filePath_.c_str());
    assert(fileAttrs != INVALID_FILE_ATTRIBUTES && !(fileAttrs & FILE_ATTRIBUTE_DIRECTORY));

    winrt::com_ptr<IWICBitmapFrameDecode> frame = GetWICFrame();

    UINT width, height;
    HRESULT hr = frame->GetSize(&width, &height);
    winrt::check_hresult(hr);

    // assume 32 bpp and DXGI_FORMAT_R8G8B8A8
    // StaticMemoryAllocator will allocate width*height amount of bytes
    config_.width = (uint32_t) width;
    config_.height = (uint32_t) height;

    // TODO: more complex formats according to metadata?
    // See WICTextureLoader::CreateTextureFromWIC
    config_.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    state_ = D3D12_RESOURCE_STATE_COMMON;
}

D3D12_RESOURCE_DESC ImageTexture2D::CreateUploadResourceDesc() const {
    // CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer()
    return D3D12_RESOURCE_DESC{};
    
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
    uint32_t rowPitch = (config_.width * bpp + 7u) / 8u;
    uint32_t totalBytes = rowPitch * config_.height;

    // prepare srcData container, receiving the data
    srcData.resize(totalBytes);

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

