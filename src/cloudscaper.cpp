#include "cloudscaper.h"

#include <iostream>


#include "renderer.h"
#include "resources.h"
#include "window.h"
#include "memory/static_memory_allocator.h"
#include "pipeline_state.h"
#include "memory/static_descriptor_allocator.h"
#include "pipeline_builder.h"
#include "ui/ui_framework.h"


Cloudscaper::Cloudscaper(HINSTANCE hinst)
    : Application(hinst, ApplicationParams("Cloudscaper")) {

    mainWindow_ = CreateAppWindow("First window");
    mainWindow_->Show();

    //
    // event = mainWindow_->GetEventManager()
    // event.AddKeyPressCallback(VK_ESCAPE, std::bind());
    // event.
    //

    HRESULT hr;
    
    RendererConfig rendererConfig;
    rendererConfig.swapChainFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    rendererConfig.numBuffers = 2;

    renderer_ = Renderer::CreateRenderer(mainWindow_->GetHWND(), rendererConfig, hr);
    winrt::check_hresult(hr);

    memAllocator_ = renderer_->InitializeMemoryAllocator<StaticMemoryAllocator>(renderer_->GetDevice());
    winrt::check_pointer(memAllocator_.get());
    
    uiFramework_ = std::make_shared<UIFramework>(renderer_, memAllocator_, mainWindow_);

    //memAllocator_->CreateResource<Texture2D>("T1", DXGI_FORMAT_R8G8B8A8_UNORM, 100, 100);
    //memAllocator_->CreateResource<Texture2D>("T2", DXGI_FORMAT_R8G8B8A8_UNORM, 100, 5000);
    //memAllocator_->CreateResource<Texture2D>("T3", DXGI_FORMAT_R8G8B8A8_UNORM, 100, 200);

    imageTex_ = memAllocator_->CreateResource<ImageTexture2D>("Image", "assets/blue_noise_16x16.png");
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
    
    memAllocator_->Commit();

    // testConstVal_ = RootConstantValue<int>(0);

    D3D12_SAMPLER_DESC desc;
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.BorderColor[0] = 0;
    desc.BorderColor[1] = 0;
    desc.BorderColor[2] = 0;
    desc.BorderColor[3] = 0;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS;
    desc.MaxAnisotropy = 0;
    desc.MaxLOD = 0;
    desc.MinLOD = 0;
    desc.MipLODBias = 0;

    testConstVal_.SetValue(0.5);
    

    testPso_ =
        renderer_->BuildGraphicsPipeline("FirstPipeline1")
        .VertexShader("shaders/vertex_shader.hlsl")
        .PixelShader("shaders/pixel_shader.hlsl")
        // No reflection => manual vertex layout matching...with weak tests
        //.SRV(imageTex_, 1)
        .SRV(computeTex_, 0)
        //.SRV(imageTex_, 2)
        .RootConstant(testConstVal_, 0)
        .StaticSampler(desc, 0)
        //.StaticSampler(desc, 1)
        .VertexBuffer(vertexBuffer_, 0)
        .IndexBuffer(indexBuffer_)
        .UseDefaultRenderTarget()
        .UseDefaultDepthBuffer()
        .Build();

    testComputePso_ =
        renderer_->BuildComputePipeline("First Compute")
        .ComputeShader("shaders/test_cs.hlsl")
        .SRV(imageTex_, 0)
        .UAV(computeTex_, 0)
        .ThreadCount(32, 32, 1) 
        .ThreadGroupCount(8, 8, 1)
        .Build();

    /*
    transmittanceCPSO_ = 
        renderer_->BuildComputePipeline("Transmittance LUT Calculation")
        .ComputeShader("shaders/atmosphere/transmittance_lut_cs.hlsl")
        .UAV(transmittanceLUT_, 0)
        .CBV(computeTex_, 0, ResourceBindMethod::RootDescriptor)
        .ThreadCount(32, 32, 1) 
        .ThreadGroupCount(8, 8, 1)
        .Build();
    */

    // Renderer Thoughts
    /*
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
}

Cloudscaper::~Cloudscaper() {
    memAllocator_.reset();
    renderer_.reset();
}

void Cloudscaper::Tick(double deltaTime) {
    Application::Tick(deltaTime);
    
    HRESULT hr;
    
    // tick logic

    // start command queue
    winrt::com_ptr<ID3D12GraphicsCommandList> cmdList = renderer_->StartCommandList(hr);
    HandleHRESULT(hr);

    renderer_->Tick(deltaTime);
    uiFramework_->Tick(deltaTime);

    renderer_->ExecutePipeline(cmdList, testComputePso_.lock());
    renderer_->ExecutePipeline(cmdList, testPso_.lock());

    uiFramework_->Render(deltaTime, cmdList);
    
    // tick render logic

    renderer_->FinishCommandList(cmdList, hr);
}
