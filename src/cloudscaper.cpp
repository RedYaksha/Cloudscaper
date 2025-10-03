#include "cloudscaper.h"

#include <iostream>
#include <renderer_common.h>


#include "renderer.h"
#include "resources.h"
#include "window.h"
#include "memory/static_memory_allocator.h"
#include "pipeline_state.h"
#include "memory/static_descriptor_allocator.h"
#include "pipeline_builder.h"
#include "ui/ui_framework.h"


Cloudscaper::Cloudscaper(HINSTANCE hinst)
    : Application(hinst, ApplicationParams("Cloudscaper")), curFrame_(0), elapsedTime_(0) {

    mainWindow_ = CreateAppWindow("First window");
    mainWindow_->Show();

    HRESULT hr;
    
    RendererConfig rendererConfig;
    rendererConfig.swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    rendererConfig.numBuffers = 2;

    renderer_ = Renderer::CreateRenderer(mainWindow_->GetHWND(), rendererConfig, hr);
    winrt::check_hresult(hr);

    memAllocator_ = renderer_->InitializeMemoryAllocator<StaticMemoryAllocator>(renderer_->GetDevice());
    winrt::check_pointer(memAllocator_.get());
    
    uiFramework_ = std::make_shared<UIFramework>(renderer_, memAllocator_, mainWindow_);


    imageTex_ = memAllocator_->CreateResource<ImageTexture2D>("Image", "assets/fonts/Montserrat/sdf_atlas_montserrat_regular.png");
    computeTex_ = memAllocator_->CreateResource<Texture2D>("Compute", DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256, true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    
    transmittanceLUT_ = memAllocator_->CreateResource<Texture2D>("Transmittance LUT", DXGI_FORMAT_R32G32B32A32_FLOAT, 256, 64, true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    multiScatteringLUT_ = memAllocator_->CreateResource<Texture2D>("MultiScattering LUT", DXGI_FORMAT_R32G32B32A32_FLOAT, 32, 32, true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    skyViewLUT_ = memAllocator_->CreateResource<Texture2D>("SkyView LUT", DXGI_FORMAT_R32G32B32A32_FLOAT, 256, 128, true, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    VertexBufferLayout layout({
        {"POSITION", 0, ShaderDataType::Float4},
        {"UV", 0, ShaderDataType::Float2},
    });

    vertices_ = std::vector<BasicVertexData> ({
        {.pos=ninmath::Vector4f{-0.5f, -0.5f, 0.f, 1.f}, .uv= ninmath::Vector2f{0.f, 1.f}},
        {.pos=ninmath::Vector4f{-0.5f, 0.5f, 0.f, 1.f}, .uv=ninmath::Vector2f{0.f, 0.f}},
        {.pos=ninmath::Vector4f{0.5f, 0.5f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 0.f}},
        {.pos=ninmath::Vector4f{0.5f, -0.5f, 0.f, 1.f}, .uv=ninmath::Vector2f{1.f, 1.f}},
    });
    indices_ = std::vector<uint32_t>({
        0, 2, 1,
        0, 3, 2
    });
    
    vertexBuffer_ = memAllocator_->CreateResource<StaticVertexBuffer<BasicVertexData>>("VB",
                                                                                 vertices_,
                                                                                 layout,
                                                                                 VertexBufferUsage::PerVertex,
                                                                                 D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
                                                                                );

    indexBuffer_ = memAllocator_->CreateResource<IndexBuffer<uint32_t>>("IB", indices_);
    
    atmosphereContext_ = { 6360.f, 6460.f };
    atmosphereContextBuffer_ = memAllocator_->CreateResource<DynamicBuffer<AtmosphereContext>>("Atmosphere Context", atmosphereContext_);

    

    ninmath::Vector3f lightDir = ninmath::Vector3f(0, 1, 0.9).Normal();
    lightDirAngle_ = 0;

    skyContext_ = {
        .cameraPos = {0,0,0.1}, // in km
        .pad0 = 0,
        .lightDir = lightDir,
        .pad1 = 0,
        .viewDir = {0,0,0}, // not used
        .pad2 = 0,
        .sunIlluminance = {1.f, 1.f, 1.f},
        .pad3 = 0,
        .groundAlbedo = {0,0,0},
        .pad4 = 0
    };

    skyContextBuffer_ = memAllocator_->CreateResource<DynamicBuffer<SkyContext>>("Sky Context", skyContext_);

    
    renderContextBuffer_ = memAllocator_->CreateResource<DynamicBuffer<RenderContext>>("Render Context", renderContext_);

    cloudParameters_.lightColor = ninmath::Vector3f(1,1,1);
    cloudParameters_.phaseG = 0.5f;
    cloudParameters_.modelNoiseScale = 0.55f;
    cloudParameters_.cloudCoverage = 0.88f;
    cloudParameters_.highFreqScale = 0.15f;
    cloudParameters_.highFreqModScale = 0.3f;
    cloudParameters_.highFreqHFScale = 10.0f;
    cloudParameters_.largeDtScale = 2.5f;
    cloudParameters_.extinction = 10.0f;
    cloudParameters_.beersScale = {0.5,0.2,0.2,0.08};
	
    cloudParameters_.numSamples = 128;

    cloudParameters_.weatherRadius = {700, 700};
	
    cloudParameters_.minWeatherCoverage = 0.6;
    cloudParameters_.useBlueNoise = 1;
    cloudParameters_.fixedDt = 1;
    cloudParameters_.lodThresholds = {0.5, 1.1, 1.1, .5};
    cloudParameters_.useAlpha = 1;
    cloudParameters_.windDir = {-1, 0, -0.3};
    cloudParameters_.windSpeed = 0.;
	
    cloudParameters_.innerShellRadius = 1.5;
    cloudParameters_.outerShellRadius= 7.0;

    cloudParameters_.lightDir = lightDir;

    cloudParametersBuffer_ = memAllocator_->CreateResource<DynamicBuffer<CloudParameters>>("Cloud Parameters", cloudParameters_);
    
    rootWidget_ = uiFramework_->CreateWidget<VerticalLayout>("Root widget");
    rootWidget_->SetGap(15);
    rootWidget_->SetMargin({5, 2.5});
    
    uiFramework_->SetRootWidget(rootWidget_);
    
    text_ = uiFramework_->CreateWidget<Text>("Text widget");

    auto addFloatInput  = [this](std::string label, float& val) {
        auto widget = uiFramework_->CreateWidget<LabeledNumericInput<float>>("input_" + label, val);
        widget->SetLabelText(label);
        paramNumericInputs_.push_back(widget);
        rootWidget_->AddChild(widget, HorizontalAlignment::Left);
    };

#define AddFloatInput(name) addFloatInput(#name, cloudParameters_. ## name);
    AddFloatInput(modelNoiseScale);
    AddFloatInput(highFreqScale);
    AddFloatInput(highFreqModScale);
    AddFloatInput(highFreqHFScale);
    AddFloatInput(extinction);

    blurRad_ = 0;
    blurRadRootConstant_.SetValue(blurRad_);
    
    addFloatInput("blur radius", blurRad_);

    AddFloatInput(largeDtScale);
    AddFloatInput(lodThresholds.x);
    AddFloatInput(beersScale.y);
    AddFloatInput(beersScale.z);
    AddFloatInput(windSpeed);

#undef AddFloatInput

    camPos_ = ninmath::Vector3f {0,0,0.02};

    testSliderVal_ = 1;
    testSlider_ = uiFramework_->CreateWidget<Slider<float>>("slider", camPos_.z);
    testSlider_->SetForegroundColor(ninmath::Vector4f{1,1,0,1});
    testSlider_->SetRange(0.1, 150);

    lightDirSlider_ = uiFramework_->CreateWidget<Slider<float>>("light dir slider", lightDirAngle_);
    lightDirSlider_->SetForegroundColor(ninmath::Vector4f{1,1,0,1});
    lightDirSlider_->SetRange(0, std::numbers::pi * 2);
    
    camSpinSlider_ = uiFramework_->CreateWidget<Slider<float>>("cam spin slider", camSpinAngle_);
    camSpinSlider_->SetForegroundColor(ninmath::Vector4f{1,1,0,1});
    camSpinSlider_->SetRange(0, std::numbers::pi * 2);
    
    rootWidget_->AddChild(testSlider_, HorizontalAlignment::Left);
    rootWidget_->AddChild(lightDirSlider_, HorizontalAlignment::Left);
    rootWidget_->AddChild(camSpinSlider_, HorizontalAlignment::Left);
    
    rootWidget_->AddChild(text_, HorizontalAlignment::Left);

    mainRT_ = renderer_->CreateRenderTarget("main_rt", DXGI_FORMAT_R8G8B8A8_UNORM, true, D3D12_RESOURCE_STATE_COMMON);

    // testConstVal_ = RootConstantValue<int>(0);
    testConstVal_.SetValue(0.5);
    
    transmittanceCPSO_ = 
        renderer_->BuildComputePipeline("Transmittance LUT Calculation")
        .ComputeShader("shaders/atmosphere/transmittance_lut_cs.hlsl")
        .UAV(transmittanceLUT_, 0)
        .CBV(atmosphereContextBuffer_, 0)
        .SyncThreadCountsWithTexture2DSize(transmittanceLUT_)
        .Build();

    multiScatteringCPSO_ =
        renderer_->BuildComputePipeline("MultiScattering LUT Calculation")
        .ComputeShader("shaders/atmosphere/multiscattering_lut_cs.hlsl")
        .UAV(multiScatteringLUT_, 0)
        .SRV(transmittanceLUT_, 0)
        .CBV(atmosphereContextBuffer_, 0, ResourceBindMethod::RootDescriptor)
        .CBV(skyContextBuffer_, 1, ResourceBindMethod::RootDescriptor)
        .SyncThreadCountsWithTexture2DSize(multiScatteringLUT_)
        .Build();
    
    skyviewCPSO_ =
        renderer_->BuildComputePipeline("SkyView LUT Calculation")
        .ComputeShader("shaders/atmosphere/skyview_lut_cs.hlsl")
        .UAV(skyViewLUT_, 0)
        .SRV(transmittanceLUT_, 0)
        .SRV(multiScatteringLUT_, 1)
        .CBV(atmosphereContextBuffer_, 0, ResourceBindMethod::RootDescriptor)
        .CBV(skyContextBuffer_, 1, ResourceBindMethod::RootDescriptor)
        .SyncThreadCountsWithTexture2DSize(skyViewLUT_)
        .Build();
    
    renderSkyGPSO_ =
        renderer_->BuildGraphicsPipeline("Sky Render")
        .VertexShader("shaders/vertex_shader.hlsl")
        .PixelShader("shaders/atmosphere/sky_raymarch_quad_ps.hlsl")
        .VertexBuffer(vertexBuffer_, 0)
        .IndexBuffer(indexBuffer_)
        .SRV(skyViewLUT_, 0)
        .StaticSampler(renderer_common::samplerLinearClamp, 0)
        .CBV(atmosphereContextBuffer_, 0, ResourceBindMethod::RootDescriptor)
        .CBV(skyContextBuffer_, 1, ResourceBindMethod::RootDescriptor)
        .CBV(renderContextBuffer_, 2, ResourceBindMethod::RootDescriptor)
        .RenderTargetConfiguration(0,
        RenderTargetConfiguration()
            .RenderTarget("main_rt", 0)
        )
        .Build();

    // clouds
    noiseGenDone_ = false;

    const uint32_t modelResolution = 256;
    const uint32_t detailResolution = 32;
    blueNoise_ = memAllocator_->CreateResource<ImageTexture2D>("Blue Noise", "assets/blue_noise_128x128.png");
    weatherTexture_ = memAllocator_->CreateResource<ImageTexture2D>("Weather Texture", "assets/weather_texture_sparse.png");
    modelNoise_ = memAllocator_->CreateResource<Texture3D>("Cloud Model Noise", DXGI_FORMAT_R32G32B32A32_FLOAT, modelResolution, modelResolution, modelResolution, true, 6, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    detailNoise_ = memAllocator_->CreateResource<Texture3D>("Cloud Detail Noise", DXGI_FORMAT_R32G32B32A32_FLOAT, detailResolution, detailResolution, detailResolution, true, 6, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    
    computeModelNoiseCPSO_ =
        renderer_->BuildComputePipeline("Compute Model Noise")
        .ComputeShader("shaders/cloudscapes/compute_model_noise_cs.hlsl")
        .UAV(modelNoise_, 0)
        .SyncThreadCountsWithTexture3DSize(modelNoise_)
        .Build();
    
    computeDetailNoiseCPSO_ =
        renderer_->BuildComputePipeline("Compute Detail Noise")
        .ComputeShader("shaders/cloudscapes/compute_detail_noise_cs.hlsl")
        .UAV(detailNoise_, 0)
        .SyncThreadCountsWithTexture3DSize(detailNoise_)
        .Build();

    gen3DMipMapsCPSO_ =
        renderer_->BuildComputePipeline("Cloud Noise 3D Mip Maps")
        .ComputeShader("shaders/cloudscapes/texture_3d_mip_maps_cs.hlsl")
        .ResourceConfiguration(0,
            ResourceConfiguration()
            .UAV<Texture3D::UAVConfig>(modelNoise_, Texture3D::UAVConfig(0, 0, 256), 0)
            .UAV<Texture3D::UAVConfig>(modelNoise_, Texture3D::UAVConfig(1, 0, 128), 1)
        )
        .ResourceConfiguration(1,
            ResourceConfiguration()
            .UAV<Texture3D::UAVConfig>(modelNoise_, Texture3D::UAVConfig(1, 0, 128), 0)
            .UAV<Texture3D::UAVConfig>(modelNoise_, Texture3D::UAVConfig(2, 0, 64), 1)
        )
        .ResourceConfiguration(2,
            ResourceConfiguration()
            .UAV<Texture3D::UAVConfig>(modelNoise_, Texture3D::UAVConfig(2, 0, 64), 0)
            .UAV<Texture3D::UAVConfig>(modelNoise_, Texture3D::UAVConfig(3, 0, 32), 1)
        )
        .SyncThreadCountsWithTexture3DSize(modelNoise_)
        .Build();

    D3D12_BLEND_DESC cloudsBlendDesc = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    cloudsBlendDesc.RenderTarget[0].BlendEnable = TRUE;
    cloudsBlendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    
    cloudsBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_ALPHA;
    cloudsBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    cloudsBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    
    cloudsBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    cloudsBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    cloudsBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    cloudRT0_ = renderer_->CreateRenderTarget("RT0", DXGI_FORMAT_R8G8B8A8_UNORM, true, D3D12_RESOURCE_STATE_COMMON);
    cloudRT1_ = renderer_->CreateRenderTarget("RT1", DXGI_FORMAT_R8G8B8A8_UNORM, true, D3D12_RESOURCE_STATE_COMMON);
    blurOutRT_ = renderer_->CreateRenderTarget("Blur Output", DXGI_FORMAT_R8G8B8A8_UNORM, true, D3D12_RESOURCE_STATE_COMMON);
    

    renderCloudsGPSO_ =
        renderer_->BuildGraphicsPipeline("Clouds Render")
        .VertexShader("shaders/vertex_shader.hlsl")
        .PixelShader("shaders/cloudscapes/raymarch_quad_ps.hlsl")
        .VertexBuffer(vertexBuffer_, 0)
        .IndexBuffer(indexBuffer_)
        .SRV(modelNoise_, 0)
        .SRV(detailNoise_, 1)
        .SRV(blueNoise_, 2)
        .SRV(weatherTexture_, 3)
        .SRV(skyViewLUT_, 4)
    
    
        .CBV(renderContextBuffer_, 0, ResourceBindMethod::RootDescriptor)
        .CBV(cloudParametersBuffer_, 1, ResourceBindMethod::RootDescriptor)
    
        .SRV(cloudRT1_, 5) // render to 0 => prevFrame is 1
        .ResourceConfiguration(1,
            ResourceConfiguration()
            .SRV(cloudRT0_, 5) // render to 1 => prevFrame is 0
        )
    
        .StaticSampler(renderer_common::samplerLinearWrap, 0)
        .StaticSampler(renderer_common::samplerPointClamp, 1)
        .BlendState(cloudsBlendDesc)
        .RenderTargetConfiguration(0,
            RenderTargetConfiguration()
            .RenderTarget("RT0", 0)
        )
        .RenderTargetConfiguration(1,
            RenderTargetConfiguration()
            .RenderTarget("RT1", 0)
        )
        .Build();

    copyCloudsToMainCPSO_ =
        renderer_->BuildComputePipeline("copy clouds to main")
        .ComputeShader("shaders/cloudscapes/blend_with_main_render_target_cs.hlsl")
        .ResourceConfiguration(0,
            ResourceConfiguration()
            .UAV(cloudRT0_, 0)
            .UAV(mainRT_, 1)
        )
        .ResourceConfiguration(1,
            ResourceConfiguration()
            .UAV(cloudRT1_, 0)
            .UAV(mainRT_, 1)
        )
        .SyncThreadCountsWithTexture2DSize(mainRT_)
        .Build();
    
    gaussianBlurCPSO_ =
        renderer_->BuildComputePipeline("blur cloud rt")
        .ComputeShader("shaders/compute_effects/gaussian_blur_cs.hlsl")
        .ResourceConfiguration(0,
            ResourceConfiguration()
            .SRV(cloudRT0_, 0)
            .UAV(blurOutRT_, 0)
            .RootConstant(blurRadRootConstant_, 0)
        )
        .ResourceConfiguration(1,
            ResourceConfiguration()
            .SRV(cloudRT1_, 0)
            .UAV(blurOutRT_, 0)
            .RootConstant(blurRadRootConstant_, 0)
        )
        .SyncThreadCountsWithTexture2DSize(mainRT_)
        .Build();
    
    cloudsTAACPSO_ =
        renderer_->BuildComputePipeline("taa cloud")
        .ComputeShader("shaders/compute_effects/taa_cs.hlsl")
        .UAV(cloudRT0_, 0)
        .UAV(cloudRT1_, 1)
        .RootConstant(taaCurInd_, 0)
        .SyncThreadCountsWithTexture2DSize(mainRT_)
        .Build();
    
    // Renderer Thoughts
    /*


        .ResourceConfig(0,
            ResourceConfigurationBuilder()
            // creates UAVConfig shared_ptr derived from ResourceDescriptorConfig, also stored in the resMap_ with the resource
            .UAV<Texture3D::UAVConfig>(tex3D, {.MipSlice=0, .FirstWSlice=0, .WSize=X}, 0)
            .UAV<Texture3D::UAVConfig>(tex3D, tex3D->CreateUAVConfigForMip(1), 1)
            .Build()
        )
        .ResourceConfig(1,
            ResourceConfigurationBuilder()
            .UAV<Texture3D::UAVConfig>(tex3D, {.MipSlice=1, .FirstWSlice=0, .WSize=X}, 0)
            .UAV<Texture3D::UAVConfig>(tex3D, {.MipSlice=2, .FirstWSlice=0, .WSize=X}, 1)
            .Build()
        )


        for(int i = 0; i < numMips; i++) {
            pso->SetResourceConfiguration(i);
            renderer_->ExecutePipeline(cmdList, pso.lock());
        }

        
        ...
        std::weak_ptr<RenderTargetHandle> rtHandle = renderer_->InitializeRenderTarget("", DXGI_FORMAT);
        .RenderTarget(rtHandle, 0)
        
     
        ... 
        How to do bindless?
        RootParameterType::Bindless?
        .SRV(res.getIndex())

        CreateBindlessGraphicsPipeline()
        ...
        .SRV(res, 0)
        .SRV(res, 0, RootParameterType::DescriptorTable)
        .SRV(res, 0, RootParameterType::DescriptorTable, 2)
        .SRV(res, 0, RootParameterType::RootDescriptor, 2)
        .RootConstant(RootConstantValue<T>(), 0, 0) // call RootConstantValue<T>::SetValue() => notify all pso listeners
        .RootConstant(RootConstantValue<T>(), 0)
        
        .Sampler( D3D12_SAMPLER_DESC , 0, 0) // uses a descriptor table
        .Sampler( D3D12_SAMPLER_DESC , 1) 
        .Sampler( D3D12_SAMPLER_DESC , 2)
        .Sampler( D3D12_SAMPLER_DESC , 4)

        .StaticSampler( D3D12_STATIC_SAMPLER_DESC, 0, 0)
        
        
        ...
        .ShaderMacros({{.Name="", .Value=""}})
        ...

        // limitations:
        //      - need everything aligned
        //      - manual process
        //      - no runtime checks with the actual bound vertex buffers

        // We can test the constructed one against the compiled layout for
        // minimal checks
    
        .VertexLayout(  VertexInputLayout::New()
                        .PerVertex({
                            {SemanticName, SemanticIndex, Format},
                            {SemanticName, SemanticIndex, Format},
                        })
                        .PerInstance({
                            {SemanticName, SemanticIndex, Format},
                            {SemanticName, SemanticIndex, Format},
                            {SemanticName, SemanticIndex, Format},
                        })
        )

        // ideally...
        ...
        .VertexBuffer(0, vertexBufferRes_) // will do runtime checking against the compiled vertex shader
        .VertexBuffer(1, instanceBufferRes_)
        ...

        later... pipeline assembly...

        .VertexBuffer(buffer, 0, VertexBufferType::PerVertex)
        .VertexBuffer(buffer, 1, VertexBufferType::PerInstance)
        .VertexBuffer(buffer, 2, VertexBufferType::PerInstance)
        .NumInstances(...)
        .RenderTarget(res, 0)
        .DepthBuffer(res)


        // bindless...
        
        .BindlessBuffer(0, 0) // reg num and space
        // 32 bit constants -> maps to cbv reg num,space -> used as array of indices into descriptor heap
        // entries in array set per frame, and the value of the index is determined by calls to
        // .BindlessRes(res, 0), etc.
        .BindlessRes(res, 0) // 0 is the index in the 32-bit constants

        // dynamic buffer
        .CBV(buffer, 0)
        ...
        buffer->SetValue(...)

        
        

        VertexBuffer("", {
        .Layout = VertexBufferLayout({
            {"Position", FLOAT4, 0},
            {"UV", FLOAT2, 0},
        })})

        
        

    ------
    
        cloudRayMarchPSO_ = renderer_->CreateGraphicsPipeline();

        DescriptorAllocationInfo d = descriptorAllocator_->PipelineAllocationInfo()
        .SRV(T0, res0)
        .SRV(T1, res1)
        .CBV(B0, res2)
        .UAV(U0, res3);

        // con: pipeline creation is now dependent on created resources
        // - dependent on HLSL root signatures

        c = renderer_->BuildGraphicsPipeline()
        .VertexShader("")
        .PixelShader("")
        .StaticSampler(S0, LINEAR_CLAMP)
        .DescriptorBlock( DescriptorBlock::Create()
                                            .SRV(T0, res0, DESCRIPTOR_TABLE)
                                            .SRV(T1, res1, DESCRIPTOR_TABLE)
                                            .CBV(B0, res2)
                                            .UAV(U0, res3))
        // .useHLSLRootSignature()
        .VertexLayout<SomeStruct>(0) // will do a run-time check on structure, comparing input layouts declared by app vs vertex shader
        .VertexLayout<OtherStruct>(1)
        
       // .RenderTarget( SWAPCHAIN )
        .RenderTarget( rtUI_ )
        TODO .Depth( )
        .BlendDesc( blendDesc ) // overrides default
        .Build();

        renderer_->InitializeDescriptorAllocator<StaticDescriptorAllocator>();

        renderer_->AllocateDescriptors(pso)
        .SRV(T0, res0)
        .SRV(T1, res1)
        .CBV(B0, res2)
        .UAV(U0, res3)
        .Sampler(S0, res4)
        .Allocate();
       
        ----

        c = std::make_shared<GraphicsPipeline>();
        c = 
     
     */


    // UI Thoughts
    /*
     uiFramework = std::make_shared<UIFramework>();

     btn = uiFramework->CreateWidget<Button>("")
           .OnPressed();
     text = uiFramework->CreateWidget<Text>("");
     
     uiFramework->CreateWidget<VerticalLayout>()
     .Add(btn, AlignLeft)
     .Add(text, AlignCenter)

     // when rendering UIFramework...
     //     - a widget tree will be formed, with the root being a default layout
     //     - position/sizing will be determined passing from root -> leaf
     //     - for layouts that depend on local sizing, 2 passes are done, or maybe added to a deferred stack/queue
     //

     UIPrimitiveBatch batch;
     uiFramework->Render(deltaTime, cmdList, batch);

     // batch the ui primitives together to maximize use of instancing
     // Primitives (all have transforms):
     //     - Quad
     //         - Color,
     //     - Rounded Quad
     //         - Color, Radii
     //     - Image
     //         - image index (bindless? or just bind different descriptor in render time), color, uvs,
     //     - SDF Image 
     //         - image index (bindless?), color, uvs
     

     // event handler:
     // win32 native callback => aggregate/queue up event (use mutex)
     // tick start => consume the event, clear up the queue, (use mutex)
     // tick() => dispatch event functions
     //
     // win32 events may be added after tick start, in which case will be consumed on the next tick

     // custom ui/shader => create custom widget, primitive with overriden pso binding?
    
     
     
     */
    
    memAllocator_->Commit();
}

Cloudscaper::~Cloudscaper() {
    memAllocator_.reset();
    renderer_.reset();
}

void Cloudscaper::Tick(double deltaTime) {
    Application::Tick(deltaTime);

    curFrame_++;
    elapsedTime_ += deltaTime;
    
    HRESULT hr;
    
    // tick logic

    // start command queue
    winrt::com_ptr<ID3D12GraphicsCommandList> cmdList = renderer_->StartCommandList(hr);
    HandleHRESULT(hr);

    blurRadRootConstant_.SetValue(blurRad_);
    
    ninmath::Vector2f screenSize = renderer_->GetScreenSize();
    float aspectRatio = screenSize.x / screenSize.y;

    ninmath::Vector3f camFwd = ninmath::Vector3f {
        std::sin(camSpinAngle_),
        std::cos(camSpinAngle_),
        0
    };

    auto perspMat = ninmath::PerspectiveProjectionMatrix4x4_RH_ZUp_ForwardY_HFOV(aspectRatio, 90, 0.1, 1000, 0, 1);
    auto viewMat = ninmath::LookAtViewMatrix_RH_ZUp(camPos_, camFwd);
    
    renderContext_.screenSize = { (uint32_t) screenSize.x, (uint32_t) screenSize.y };
    renderContext_.invProjectionMat = perspMat.Inverse();
    renderContext_.invViewMat = viewMat.Inverse();
    renderContext_.cameraPos = camPos_;
    renderContext_.frame = curFrame_;
    renderContext_.time = elapsedTime_;

    ninmath::Vector3f lightDir = ninmath::Vector3f {
        0,
        std::sin(lightDirAngle_),
        std::cos(lightDirAngle_),
    };

    cloudParameters_.lightDir = lightDir;
    skyContext_.lightDir = lightDir;

    renderContextBuffer_.lock()->UpdateGPUData();
    cloudParametersBuffer_.lock()->UpdateGPUData();
    skyContextBuffer_.lock()->UpdateGPUData();

    text_->SetText(std::to_string(curFrame_));

    renderer_->Tick(deltaTime);
    uiFramework_->Tick(deltaTime);

    renderer_->ExecutePipeline(cmdList, transmittanceCPSO_.lock());
    renderer_->ExecutePipeline(cmdList, multiScatteringCPSO_.lock());
    renderer_->ExecutePipeline(cmdList, skyviewCPSO_.lock());
    renderer_->ExecutePipeline(cmdList, renderSkyGPSO_.lock());
    
    std::shared_ptr<RenderTarget> swapChainRes_ = renderer_->GetCurrentSwapChainBufferResource();
    const bool usingFrame0 = (curFrame_ % 2) == 0;

    if(computeModelNoiseCPSO_.lock()->IsReadyAndOk() && computeDetailNoiseCPSO_.lock()->IsReadyAndOk()) {
        if(!noiseGenDone_) {
            renderer_->ExecutePipeline(cmdList, computeModelNoiseCPSO_.lock());
            renderer_->ExecutePipeline(cmdList, computeDetailNoiseCPSO_.lock());
            
            for(int i = 0; i < gen3DMipMapsCPSO_.lock()->GetNumResourceConfigurations(); i++) {
                gen3DMipMapsCPSO_.lock()->SetResourceConfigurationIndex(i);
                renderer_->ExecutePipeline(cmdList, gen3DMipMapsCPSO_.lock());
            }

            noiseGenDone_ = true;
        }
        else {
            std::shared_ptr<GraphicsPipelineState> cloudsGPSO = std::static_pointer_cast<GraphicsPipelineState>(renderCloudsGPSO_.lock());
            cloudsGPSO->SetResourceConfigurationIndex(usingFrame0? 0 : 1);
            cloudsGPSO->SetRenderTargetConfigurationIndex(usingFrame0? 0 : 1);

            D3D12_RESOURCE_STATES shaderResState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            if(usingFrame0) {
                cloudRT1_.lock()->ChangeStateDirect(shaderResState, cmdList);
                cloudRT0_.lock()->ChangeStateDirect(D3D12_RESOURCE_STATE_RENDER_TARGET, cmdList);
            }
            else {
                cloudRT0_.lock()->ChangeStateDirect(shaderResState, cmdList);
                cloudRT1_.lock()->ChangeStateDirect(D3D12_RESOURCE_STATE_RENDER_TARGET, cmdList);
            }

            renderer_->ExecutePipeline(cmdList, renderCloudsGPSO_.lock());

            if(usingFrame0) {
                cloudRT0_.lock()->ChangeStateDirect(shaderResState, cmdList);
            }
            else {
                cloudRT1_.lock()->ChangeStateDirect(shaderResState, cmdList);
            }

            taaCurInd_.SetValue(usingFrame0? 0 : 1);
            //renderer_->ExecutePipeline(cmdList, cloudsTAACPSO_.lock());
            

            blurOutRT_.lock()->ChangeStateDirect(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, cmdList);

            gaussianBlurCPSO_.lock()->SetResourceConfigurationIndex(usingFrame0? 0 : 1);
            renderer_->ExecutePipeline(cmdList, gaussianBlurCPSO_.lock());

            mainRT_.lock()->ChangeStateDirect(D3D12_RESOURCE_STATE_UNORDERED_ACCESS, cmdList);

            copyCloudsToMainCPSO_.lock()->SetResourceConfigurationIndex(usingFrame0? 0 : 1);
            renderer_->ExecutePipeline(cmdList, copyCloudsToMainCPSO_.lock());
        }
    }

    swapChainRes_->ChangeStateDirect(D3D12_RESOURCE_STATE_COPY_DEST, cmdList);
    mainRT_.lock()->ChangeStateDirect(D3D12_RESOURCE_STATE_COPY_SOURCE, cmdList);

    // TODO: 
    // copy cloud render target to final frame
    cmdList->CopyResource(swapChainRes_->GetNativeResource().get(),
                          mainRT_.lock()->GetNativeResource().get());

    // already done in Renderer::FinishCommandList
    swapChainRes_->ChangeStateDirect(D3D12_RESOURCE_STATE_PRESENT, cmdList);
    mainRT_.lock()->ChangeStateDirect(D3D12_RESOURCE_STATE_RENDER_TARGET, cmdList);

    uiFramework_->Render(deltaTime, cmdList);
    
    // tick render logic

    renderer_->FinishCommandList(cmdList, hr);
}
