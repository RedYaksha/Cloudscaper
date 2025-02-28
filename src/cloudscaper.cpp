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

    skyContext_ = {
        .cameraPos = {0,0,0.1}, // in km
        .pad0 = 0,
        .lightDir = {0,0,1.0},
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
    cloudParameters_.modelNoiseScale = 0.9f;
    cloudParameters_.cloudCoverage = 0.88f;
    cloudParameters_.highFreqScale = 0.85f;
    cloudParameters_.highFreqModScale = 0.8f;
    cloudParameters_.highFreqHFScale = 4.0f;
    cloudParameters_.largeDtScale = .04f;
    cloudParameters_.extinction = 10.0f;
    cloudParameters_.beersScale = {0.5,0.2,1.,0.08};
	
    cloudParameters_.numSamples = 256;

    cloudParameters_.weatherRadius = {150, 150};
	
    cloudParameters_.minWeatherCoverage = 0.18;
    cloudParameters_.useBlueNoise = 1;
    cloudParameters_.fixedDt = 1;
    cloudParameters_.lodThresholds = {0.2, 1.1, 0.1, .5};
    cloudParameters_.useAlpha = 1;
    cloudParameters_.windDir = {-1, 0, -0.3};
    cloudParameters_.windSpeed = 0.;
	
    cloudParameters_.innerShellRadius = 1.5;
    cloudParameters_.outerShellRadius= 5.0;

    cloudParameters_.lightDir = ninmath::Vector3f(0,0,1).Normal();

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
    AddFloatInput(cloudCoverage);
    AddFloatInput(highFreqScale);
    AddFloatInput(highFreqModScale);
    AddFloatInput(highFreqHFScale);
    AddFloatInput(largeDtScale);
    AddFloatInput(extinction);
    AddFloatInput(minWeatherCoverage);
    AddFloatInput(weatherRadius.x);
    AddFloatInput(weatherRadius.y);
#undef AddFloatInput
    
    rootWidget_->AddChild(text_, HorizontalAlignment::Left);
    

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
        .UseDefaultRenderTarget()
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

        /*
        .SRV(cloudRT0_, 5) // prevFrame
        .ResourceConfiguration(1,
            ResourceConfiguration()
            .SRV(cloudRT1_, 5) // prevFrame
        )
        */
    
        .CBV(renderContextBuffer_, 0, ResourceBindMethod::RootDescriptor)
        .CBV(cloudParametersBuffer_, 1, ResourceBindMethod::RootDescriptor)
        .StaticSampler(renderer_common::samplerLinearClamp, 0)
        .BlendState(cloudsBlendDesc)
        .UseDefaultRenderTarget()
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

    ninmath::Vector2f screenSize = renderer_->GetScreenSize();
    float aspectRatio = screenSize.x / screenSize.y;

    ninmath::Vector3f camPos = {0, 0, 1};
    
    auto perspMat = ninmath::PerspectiveProjectionMatrix4x4_RH_ZUp_ForwardY_HFOV(aspectRatio, 90, 0.1, 1000, 0, 1);
    auto viewMat = ninmath::LookAtViewMatrix_RH_ZUp(camPos, {0, 1, 0});
    
    renderContext_.screenSize = { (uint32_t) screenSize.x, (uint32_t) screenSize.y };
    renderContext_.invProjectionMat = perspMat.Inverse();
    renderContext_.invViewMat = viewMat.Inverse();
    renderContext_.cameraPos = camPos;
    renderContext_.frame = curFrame_;
    renderContext_.time = elapsedTime_;
    renderContextBuffer_.lock()->UpdateGPUData();
    cloudParametersBuffer_.lock()->UpdateGPUData();

    text_->SetText(std::to_string(elapsedTime_));

    renderer_->Tick(deltaTime);
    uiFramework_->Tick(deltaTime);

    renderer_->ExecutePipeline(cmdList, transmittanceCPSO_.lock());
    renderer_->ExecutePipeline(cmdList, multiScatteringCPSO_.lock());
    renderer_->ExecutePipeline(cmdList, skyviewCPSO_.lock());
    renderer_->ExecutePipeline(cmdList, renderSkyGPSO_.lock());

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
            renderer_->ExecutePipeline(cmdList, renderCloudsGPSO_.lock());
        }
    }

    // TODO: 
    // copy cloud render target to final frame
    

    uiFramework_->Render(deltaTime, cmdList);
    
    // tick render logic

    renderer_->FinishCommandList(cmdList, hr);
}
