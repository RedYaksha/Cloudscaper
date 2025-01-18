#define NOMINMAX
#include "renderer.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <iostream>
#include <winrt/windows.foundation.h>
#include <thread>

#include "pipeline_assembler.h"
#include "pipeline_state.h"
#include "shader.h"
#include "shader_compiler.h"
#include "memory/memory_allocator.h"
#include "memory/static_descriptor_allocator.h"

#ifndef APP_D3D_MININMUM_FEATURE_LEVEL
#define APP_D3D_MINIMUM_FEATURE_LEVEL D3D_FEATURE_LEVEL_12_0
#endif

template<typename T>
concept IsID3D12Device = IsAnyOf<T, ID3D12Device, ID3D12Device1, ID3D12Device2, ID3D12Device3, ID3D12Device4, ID3D12Device5,
ID3D12Device6, ID3D12Device7, ID3D12Device8, ID3D12Device9, ID3D12Device10>;

template<typename T>
concept IsIDXGISwapChain1 = IsAnyOf<T, IDXGISwapChain1, IDXGISwapChain2, IDXGISwapChain3, IDXGISwapChain4>;

namespace dx12_init {
	void EnableDebugLayer(HRESULT& hr);
	
    winrt::com_ptr<IDXGIAdapter4> GetDXGIAdapter(HRESULT& hr);
	
	template<IsID3D12Device T>
	winrt::com_ptr<T> CreateDevice(winrt::com_ptr<IDXGIAdapter4> adapter, HRESULT& hr);

	winrt::com_ptr<ID3D12CommandQueue> CreateCommandQueue(winrt::com_ptr<ID3D12Device> device,
														  D3D12_COMMAND_LIST_TYPE type,
														  HRESULT& hr);
	
	template<IsIDXGISwapChain1 T>
	winrt::com_ptr<T> CreateSwapChain(HWND hwnd, winrt::com_ptr<ID3D12CommandQueue> cmdQueue,
	                                  uint32_t numBuffers, DXGI_FORMAT format, HRESULT& hr);
} // dx12_init

std::shared_ptr<Renderer> Renderer::CreateRenderer(RendererInitParams params, HRESULT& hr) {
	// https://stackoverflow.com/a/20961251
	struct MakeSharedEnabler : Renderer {
		MakeSharedEnabler(RendererInitParams params, HRESULT& hr)
		: Renderer(params, hr)
		{}
	};
	
	std::shared_ptr<Renderer> renderer = std::make_shared<MakeSharedEnabler>(params, hr);
	
	if(FAILED(hr)) {
		return nullptr;
	}
	return renderer;
}

void Renderer::RegisterComputePipeline(std::string id, const ComputePipelineParams& params) {
	
}

Renderer::Renderer(RendererInitParams params, HRESULT& hr)
: cmdListActive_(false), fenceValue_(0) {
	numBuffers_ = 2;
	
	RECT rect;
	BOOL succeeded = GetWindowRect(params.hwnd, &rect);

	if(!succeeded) {
		hr = E_FAIL;
		return;
	}

	clientWidth_ = rect.right - rect.left;
	clientHeight_ = rect.bottom - rect.top;

	dx12_init::EnableDebugLayer(hr);
	CHECK_HR(hr);
	
    winrt::com_ptr<IDXGIAdapter4> dxgiAdapter = dx12_init::GetDXGIAdapter(hr);
	CHECK_HR(hr); // returns if failed

	device_ = dx12_init::CreateDevice<ID3D12Device2>(dxgiAdapter, hr);
	CHECK_HR(hr);

	cmdQueue_ = dx12_init::CreateCommandQueue(device_, D3D12_COMMAND_LIST_TYPE_DIRECT, hr);
	CHECK_HR(hr);
	
	cmdCopyQueue_ = dx12_init::CreateCommandQueue(device_, D3D12_COMMAND_LIST_TYPE_COPY, hr);
	CHECK_HR(hr);

	swapChain_ = dx12_init::CreateSwapChain<IDXGISwapChain4>(params.hwnd, cmdQueue_, 2, DXGI_FORMAT_R8G8B8A8_UNORM, hr);
	CHECK_HR(hr);

	curBackBufferIndex_ = swapChain_->GetCurrentBackBufferIndex();

	// 1 cmd allocator per frame buffer
	cmdAllocators_.resize(numBuffers_);
	cmdCopyAllocators_.resize(numBuffers_);
	
	for(int i = 0; i < numBuffers_; i++) {
		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
											__uuidof(ID3D12CommandAllocator),
											cmdAllocators_[i].put_void());
		CHECK_HR(hr);
											
		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
											__uuidof(ID3D12CommandAllocator),
											cmdCopyAllocators_[i].put_void());
		CHECK_HR(hr);
	}

	// 1 cmd list per thread (can be reset once cmd queue is executed, unlike allocators)
	// pCommandAllocator can be any valid allocator, it's going to get reset anyway on StartCommandList()
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAllocators_[0].get(), nullptr, __uuidof(ID3D12GraphicsCommandList), cmdList_.put_void());
	CHECK_HR(hr);

	cmdList_->Close();
	CHECK_HR(hr);
	
	hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY, cmdCopyAllocators_[0].get(), nullptr, __uuidof(ID3D12GraphicsCommandList), cmdCopyList_.put_void());
	CHECK_HR(hr);
	
	cmdCopyList_->Close();
	CHECK_HR(hr);

	mainFenceValues_.resize(numBuffers_, 0);
	hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), mainFence_.put_void());
	CHECK_HR(hr);

	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent_);

	shaderCompiler_ = std::make_shared<ShaderCompiler>();

	// TBD: how to specify what type of descriptor allocator?
	resourceDescriptorAllocator_ = std::make_shared<StaticDescriptorAllocator>(device_,
																		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
																		500,
																		true
																		);
																		
	samplerDescriptorAllocator_ = std::make_shared<StaticDescriptorAllocator>(device_,
																		D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
																		10,
																		true
																		);
	
	pipelineAssembler_ = std::make_shared<PipelineAssembler>(device_, resourceDescriptorAllocator_, samplerDescriptorAllocator_);
}

Renderer::~Renderer() {
	// flush all graphics commands
	const uint64_t lastFenceVal = ++fenceValue_;
	HRESULT hr = cmdQueue_->Signal(mainFence_.get(), lastFenceVal);
	winrt::check_hresult(hr);

	const uint64_t complValue = mainFence_->GetCompletedValue();
	if(complValue < lastFenceVal) {
		mainFence_->SetEventOnCompletion(lastFenceVal, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE); // block
	}
}

std::weak_ptr<PipelineState> Renderer::FinalizeGraphicsPipelineBuild(const GraphicsPipelineBuilder& builder) {
	WINRT_ASSERT(shaderCompiler_);
	
	std::cout << "Finalizing " << builder.id << std::endl;
	if(psoMap_.contains(builder.id)) {
		return std::weak_ptr<GraphicsPipelineState>();
	}

	// check
	WINRT_ASSERT(builder.vertexShaderPath_.has_value() && "Vertex shader not set.");
	WINRT_ASSERT(builder.pixelShaderPath_.has_value() && "Pixel shader not set.");
	WINRT_ASSERT(builder.renderTarget_ != nullptr || builder.useDefaultRenderTarget_ && "Render target not specified");
	WINRT_ASSERT(builder.depthBuffer_ != nullptr || builder.useDefaultDepthBuffer_ && "Render target not specified");

	std::shared_ptr<GraphicsPipelineState> pso = std::make_shared<GraphicsPipelineState>(builder.id);
	pso->resMap_ = std::move(builder.resMap_);
	pso->constantMap_ = std::move(builder.constantMap_);
	pso->samplerMap_ = std::move(builder.samplerMap_);
	pso->staticSamplerMap_ = std::move(builder.staticSamplerMap_);

	// assign shaders
	
	// if shaders not created, send to compiler
	auto getOrCreateShader = [this](const std::string& sourceFile, ShaderType stype)->std::weak_ptr<Shader> {
		if(shaderMap_.contains(sourceFile)) {
			return std::weak_ptr(shaderMap_[sourceFile]);
		}
		
		std::shared_ptr<Shader> outShader;
		switch(stype) {
		case ShaderType::Vertex:
			outShader = std::make_shared<VertexShader>(sourceFile);
			break;
		case ShaderType::Pixel:
			outShader = std::make_shared<PixelShader>(sourceFile);
			break;
		// not supported
		case ShaderType::Hull:
			outShader = std::make_shared<HullShader>(sourceFile);
			break;
		case ShaderType::Domain:
			outShader = std::make_shared<DomainShader>(sourceFile);
			break;
		default:
			WINRT_ASSERT(false);
		}

		// send to compiler
		shaderCompiler_->Enqueue(outShader);
		shaderMap_.insert({sourceFile, outShader});
		return outShader;
	};

	std::weak_ptr<Shader> vs = getOrCreateShader(builder.vertexShaderPath_.value(), ShaderType::Vertex);
	std::weak_ptr<Shader> ps = getOrCreateShader(builder.pixelShaderPath_.value(), ShaderType::Pixel);

	pso->vertexShader_ = std::static_pointer_cast<VertexShader>(vs.lock());
	pso->pixelShader_ = std::static_pointer_cast<PixelShader>(ps.lock());

	pipelineAssembler_->Enqueue(pso);
	psoMap_.insert({builder.id, std::move(pso)});

	return psoMap_[builder.id];
}


winrt::com_ptr<ID3D12GraphicsCommandList> Renderer::StartCommandList(HRESULT& hr) {
	// we're still writing to the command list
	assert(!cmdListActive_);

	// TODO: add sync functions here instead of ::FinishCommandList
	
	
	hr = cmdAllocators_[curBackBufferIndex_]->Reset();
	CHECK_HR_NULL(hr);
	
	cmdList_->Reset(cmdAllocators_[curBackBufferIndex_].get(), nullptr);
	CHECK_HR_NULL(hr);

	cmdListActive_ = true;
	
	return cmdList_;
}

void Renderer::FinishCommandList(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, HRESULT& hr) {
	assert(cmdList == cmdList_);

	hr = cmdList_->Close();
	CHECK_HR(hr);
	

	ID3D12CommandList* const cmdLists[] = {
		cmdList.get()
	};
	
	cmdQueue_->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	// present whatever's on the current buffer, which was rendered onto (completely) already in a previous frame
	swapChain_->Present(0, 0);

	// update fence value for previous buffer
	const uint64_t nextFenceVal = ++fenceValue_;
	mainFenceValues_[curBackBufferIndex_] = nextFenceVal;
	hr = cmdQueue_->Signal(mainFence_.get(), nextFenceVal);
	CHECK_HR(hr);

	const uint32_t nextBackBuffer = swapChain_->GetCurrentBackBufferIndex();
	const uint64_t waitFenceVal = mainFenceValues_[nextBackBuffer];
	
	if(mainFence_->GetCompletedValue() < waitFenceVal) {
		mainFence_->SetEventOnCompletion(waitFenceVal, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE); // block
	}

	curBackBufferIndex_ = nextBackBuffer;
	cmdListActive_ = false;

	// execute shader compilation
	shaderCompiler_->Flush();
	pipelineAssembler_->Flush();
}

void Renderer::Tick(double deltaTime) {
	HRESULT hr;
	
	// let memory allocator do work, if there is any
	if(memoryAllocator_->HasWork()) {
		hr = cmdCopyAllocators_[curBackBufferIndex_]->Reset();
		winrt::check_hresult(hr);
		
		hr = cmdCopyList_->Reset(cmdCopyAllocators_[curBackBufferIndex_].get(), NULL);
		winrt::check_hresult(hr);
		
		// TODO: memoryAllocator is fully responsible for the command list and queue...
		// should it just "own" them then?
		memoryAllocator_->Update(cmdCopyList_, cmdCopyQueue_);
	}
}

GraphicsPipelineBuilder Renderer::BuildGraphicsPipeline(std::string id) {
	GraphicsPipelineBuilder::BuildFunction buildFunc = std::bind(&Renderer::FinalizeGraphicsPipelineBuild, this, std::placeholders::_1);
	GraphicsPipelineBuilder builder = GraphicsPipelineBuilder(id, buildFunc);
	return builder;
}

//
// dx12_init IMPLEMENTATION
//

void dx12_init::EnableDebugLayer(HRESULT& hr) {
	winrt::com_ptr<ID3D12Debug> debug;
	hr = D3D12GetDebugInterface(__uuidof(ID3D12Debug), debug.put_void());
	CHECK_HR(hr);

	debug->EnableDebugLayer();
}

winrt::com_ptr<IDXGIAdapter4> dx12_init::GetDXGIAdapter(HRESULT& hr) {
	winrt::com_ptr<IDXGIFactory7> dxgiFactory;
	hr = CreateDXGIFactory2(0, __uuidof(dxgiFactory), dxgiFactory.put_void());
	CHECK_HR_NULL(hr);
	
	// find a compatible adapter
	winrt::com_ptr<IDXGIAdapter1> dxgiAdapter;
	winrt::com_ptr<IDXGIAdapter4> outDXGIAdapter;
	
	size_t maxDedicatedVideoMemory = 0;
	for(uint32_t i = 0; dxgiFactory->EnumAdapters1(i, dxgiAdapter.put()) == S_OK ; i++) {
		DXGI_ADAPTER_DESC1 adapterDesc;
		hr = dxgiAdapter->GetDesc1(&adapterDesc);
		CHECK_HR_NULL(hr);

		const bool isSoftwareAdapter = adapterDesc.Flags == DXGI_ADAPTER_FLAG_SOFTWARE;
		const bool enoughVideoMemory = adapterDesc.DedicatedVideoMemory > maxDedicatedVideoMemory;
		// "If ppDevice is NULL and the function succeeds, S_FALSE is returned, rather than S_OK."
		const bool createDeviceSuccessful = D3D12CreateDevice(dxgiAdapter.get(), APP_D3D_MINIMUM_FEATURE_LEVEL, __uuidof(ID3D12Device), NULL) == S_FALSE;

		if(!isSoftwareAdapter && enoughVideoMemory && createDeviceSuccessful) {
			maxDedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
			hr = dxgiAdapter.as(__uuidof(IDXGIAdapter4), outDXGIAdapter.put_void());
			CHECK_HR_NULL(hr);
		}
	}

	return outDXGIAdapter;
}

template<IsID3D12Device T>
winrt::com_ptr<T> dx12_init::CreateDevice(winrt::com_ptr<IDXGIAdapter4> adapter, HRESULT& hr) {
	winrt::com_ptr<T> device;

	hr = D3D12CreateDevice(adapter.get(), APP_D3D_MINIMUM_FEATURE_LEVEL, __uuidof(T), device.put_void());
	CHECK_HR_NULL(hr);

	// "[ID3D12InfoQueue] interface is obtained by querying it from the ID3D12Device using IUnknown::QueryInterface.
	// The ID3D12Debug layer must be enabled through ID3D12Debug::EnableDebugLayer for that operation to succeed."
	winrt::com_ptr<ID3D12InfoQueue> infoQueue;
	if(SUCCEEDED(device.as(__uuidof(ID3D12InfoQueue), infoQueue.put_void()))) {
		// will only run if debug layers was ran
		// TODO: set this through config params, or macros
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
	}

	return device;
}


winrt::com_ptr<ID3D12CommandQueue> dx12_init::CreateCommandQueue(winrt::com_ptr<ID3D12Device> device, D3D12_COMMAND_LIST_TYPE type, HRESULT& hr) {
	winrt::com_ptr<ID3D12CommandQueue> commandQueue;
	
	D3D12_COMMAND_QUEUE_DESC desc;
	desc.Type = type;
	desc.Priority = 0;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;
	
	hr = device->CreateCommandQueue(&desc, __uuidof(ID3D12CommandQueue), commandQueue.put_void());
	CHECK_HR_NULL(hr);
	
	return commandQueue;
}

template<IsIDXGISwapChain1 T>
winrt::com_ptr<T> dx12_init::CreateSwapChain(HWND hwnd, winrt::com_ptr<ID3D12CommandQueue> cmdQueue,
	uint32_t numBuffers, DXGI_FORMAT format, HRESULT& hr) {
	
	winrt::com_ptr<IDXGIFactory2> dxgiFactory;
	hr = CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), dxgiFactory.put_void());
	CHECK_HR_NULL(hr);

	DXGI_SWAP_CHAIN_DESC1 scDesc;
	// "If you specify the height as zero when you call the IDXGIFactory2::CreateSwapChainForHwnd method to create a
	// swap chain, the runtime obtains the height from the output window and assigns this height value to the swap-chain description."
	scDesc.Width = 0;
	scDesc.Height = 0;
	scDesc.Format = format;
	scDesc.Stereo = FALSE;
	scDesc.SampleDesc = DXGI_SAMPLE_DESC {1, 0}; // can only be this value, MSAA not supported for flip model
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.BufferCount = numBuffers; // can be in [2,16]
	scDesc.Scaling = DXGI_SCALING_STRETCH;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
	scDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	scDesc.Flags = DXGI_MWA_NO_ALT_ENTER;

	/*
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC fsDesc;
	fsDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
	fsDesc.Windowed = ;
	fsDesc.RefreshRate = ;
	fsDesc.ScanlineOrdering = ;
	*/

	winrt::com_ptr<T> swapChain;
	winrt::com_ptr<IDXGISwapChain1> swapChain1;
	// "For Direct3D 12 this (pDevice) is a pointer to a direct command queue (refer to ID3D12CommandQueue).
	// This parameter cannot be NULL."
	hr = dxgiFactory->CreateSwapChainForHwnd(cmdQueue.get(), hwnd, &scDesc, nullptr, nullptr, swapChain1.put());
	CHECK_HR_NULL(hr);

	hr = swapChain1.as(__uuidof(T), swapChain.put_void());
	CHECK_HR_NULL(hr);

	hr = dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
	CHECK_HR_NULL(hr);

	return swapChain;
}


#undef CHECK_HR
