#include "cloudscaper.h"

#include <iostream>

#include "renderer.h"
#include "resources.h"
#include "window.h"
#include "memory/static_memory_allocator.h"
#include "pipeline_state.h"
#include "memory/static_descriptor_allocator.h"

Cloudscaper::Cloudscaper(HINSTANCE hinst)
    : Application(hinst, ApplicationParams("Cloudscaper")) {

    mainWindow_ = CreateAppWindow("First window");
    mainWindow_->Show();

    HRESULT hr;
    renderer_ = Renderer::CreateRenderer(RendererInitParams{ mainWindow_->GetHWND() }, hr);
    winrt::check_hresult(hr);

    memAllocator_ = renderer_->InitializeMemoryAllocator<StaticMemoryAllocator>(renderer_->GetDevice());
    winrt::check_pointer(memAllocator_.get());

    Texture2D::Config p;
    p.width = 100;
    p.height = 100;
    p.format = DXGI_FORMAT_R8G8B8A8_UNORM;

    memAllocator_->CreateResource<Texture2D>("T1", p);
    p.height = 1000;
    memAllocator_->CreateResource<Texture2D>("T2", p);
    p.width = 500;
    memAllocator_->CreateResource<Texture2D>("T3", p);

    imageTex_ = memAllocator_->CreateResource<ImageTexture2D>("Image", { .filePath="assets/blue_noise_16x16.png" });
    
    memAllocator_->Commit();

    testConstVal_ = RootConstantValue<int>(0);
    
    testPso_ =
        renderer_->BuildGraphicsPipeline("FirstPipeline1")
        .VertexShader("shaders/vertex_shader.hlsl")
        .PixelShader("shaders/pixel_shader.hlsl")
        // No reflection => manual vertex layout matching...with weak tests
        .SRV(imageTex_, 1)
        .SRV(imageTex_, 2)
        .RootConstant(testConstVal_, 0)
        .UseDefaultRenderTarget()
        .UseDefaultDepthBuffer()
        .Build();

    /*
     
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
}

void Cloudscaper::Tick(double deltaTime) {
    Application::Tick(deltaTime);
    
    HRESULT hr;
    
    // tick logic

    // start command queue
    winrt::com_ptr<ID3D12GraphicsCommandList> cmdList = renderer_->StartCommandList(hr);
    HandleHRESULT(hr);

    renderer_->Tick(deltaTime);
    
    // tick render logic
    if(imageTex_->IsReady()) {
        //std::cout << "Image is ready!" << std::endl;
    }
    else {
        //std::cout << "Image NOT ready!" << std::endl;
    }

    renderer_->FinishCommandList(cmdList, hr);
}
