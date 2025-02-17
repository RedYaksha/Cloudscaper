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
#include "pipeline_builder.h"
#include "memory/static_memory_allocator.h"

#ifndef APP_D3D_MININMUM_FEATURE_LEVEL
#define APP_D3D_MINIMUM_FEATURE_LEVEL D3D_FEATURE_LEVEL_12_0
#endif

template<typename T>
concept IsID3D12Device = IsAnyOf<T, ID3D12Device, ID3D12Device1, ID3D12Device2, ID3D12Device3, ID3D12Device4, ID3D12Device5,
ID3D12Device6, ID3D12Device7, ID3D12Device8, ID3D12Device9, ID3D12Device10>;

template<typename T>
concept IsIDXGISwapChain1 = IsAnyOf<T, IDXGISwapChain1, IDXGISwapChain2, IDXGISwapChain3, IDXGISwapChain4>;

const ResourceID Renderer::SwapChainRenderTargetID = "DefaultSwapChainRenderTarget";
const ResourceID Renderer::DefaultDepthStencilTargetID = "DefaultDepthStencilTarget";

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

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UseDefaultRenderTarget(uint16_t slotIndex) {
	return RenderTarget(Renderer::SwapChainRenderTargetID, slotIndex);
}

GraphicsPipelineBuilder& GraphicsPipelineBuilder::UseDefaultDepthBuffer() {
	return DepthBuffer(Renderer::DefaultDepthStencilTargetID);
}

std::shared_ptr<Renderer> Renderer::CreateRenderer(HWND hwnd, RendererConfig params, HRESULT& hr) {
	// https://stackoverflow.com/a/20961251
	struct MakeSharedEnabler : Renderer {
		MakeSharedEnabler(HWND hwnd, RendererConfig params, HRESULT& hr)
		: Renderer(hwnd, params, hr)
		{}
	};
	
	std::shared_ptr<Renderer> renderer = std::make_shared<MakeSharedEnabler>(hwnd, params, hr);
	
	if(FAILED(hr)) {
		return nullptr;
	}
	return renderer;
}

void Renderer::ExecutePipeline(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, std::shared_ptr<PipelineState> pso) {
	if(!pso->IsStateReady()) {
		std::cout << "pso not assembled" << std::endl;
		return;
	}
	
	if(!pso->IsReadyAndOk()) {
		// std::cout << "pso not assembled correctly..." << std::endl;
		return;
	}
	
	// check all dependent resources (vertex buffers, textures, etc.), abort if not ready
	// check that all render target resource states are OK, otherwise barriers are needed
	if(!pso->AreAllResourcesReady()) {
		std::cout << "pso resources not ready..." << std::endl;
		return;
	}


	// set render targets (via descriptor)
	// - the descriptors linked to the current backbuffer index
	// - the descriptors should've been made when creating the pipeline
	// barrier if needed
	const std::vector<ResourceID>& ids = pso->rtGroupId_.GetIDs();

	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	
	// for each render target id, find the RenderTarget for the current back buffer
	// and change its state (if different)
	for(const auto& id : ids) {
		WINRT_ASSERT(renderTargetMap_.contains(id));
		std::shared_ptr<RenderTargetHandle> rtHandle = renderTargetMap_[id];
		std::shared_ptr<RenderTarget> rt = rtHandle->resources[curBackBufferIndex_];
		rt->ChangeState(D3D12_RESOURCE_STATE_RENDER_TARGET, barriers);
	}

	if(barriers.size() > 0) {
		cmdList->ResourceBarrier(barriers.size(), barriers.data());
	}

	// Output Merger, set render target & depth buffer (if any)
	if(pso->rtGroupId_.GetIDs().size() > 0) {
		D3D12_CPU_DESCRIPTOR_HANDLE rtDescriptor;
		std::shared_ptr<DescriptorHeapAllocation> rtAlloc = renderTargetAllocMap_[pso->rtGroupId_][curBackBufferIndex_].lock();
		const uint32_t numRenderTargets = rtAlloc->GetAllocationSize();
		rtAlloc->GetCPUDescriptorHandle(rtDescriptor);

		D3D12_CPU_DESCRIPTOR_HANDLE depthDescriptor;
		D3D12_CPU_DESCRIPTOR_HANDLE* ptrDepthDescriptor = nullptr;
		if(depthStencilTargetMap_.contains(pso->depthId_)) {
			depthBufferAllocMap_[pso->depthId_][curBackBufferIndex_].lock()->GetCPUDescriptorHandle(depthDescriptor);
			ptrDepthDescriptor = &depthDescriptor;
		}
		
		cmdList->OMSetRenderTargets(numRenderTargets, &rtDescriptor, true, ptrDepthDescriptor);
	}
	
	// execute pipeline...
	// 1. Set root signature
	// 2. Graphics => bind vertex buffer(s), index buffer, and draw
	//    Compute => dispatch
	pso->Execute(cmdList);
}

void Renderer::ExecuteGraphicsPipeline(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList,
	std::shared_ptr<PipelineState> pso, uint32_t numInstances) {
	
	std::static_pointer_cast<GraphicsPipelineState>(pso)->SetNumInstances(numInstances);
	ExecutePipeline(cmdList, pso);
}

Renderer::Renderer(HWND hwnd, RendererConfig config, HRESULT& hr)
: cmdListActive_(false), fenceValue_(0), config_(config) {
	numBuffers_ = config_.numBuffers;
	
	RECT rect;
	BOOL succeeded = GetWindowRect(hwnd, &rect);

	if(!succeeded) {
		hr = E_FAIL;
		return;
	}

	clientWidth_ = rect.right - rect.left;
	clientHeight_ = rect.bottom - rect.top;
	screenSizeRCV_.SetValue(ninmath::Vector2f{(float) clientWidth_, (float) clientHeight_});

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

	swapChain_ = dx12_init::CreateSwapChain<IDXGISwapChain4>(hwnd, cmdQueue_, 2, config.swapChainFormat, hr);
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

	scissorRect_ = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
	viewport_ = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(clientWidth_), static_cast<float>(clientHeight_));

	shaderCompiler_ = std::make_shared<ShaderCompiler>();

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
	
	renderTargetDescriptorAllocator_ = std::make_shared<StaticDescriptorAllocator>(device_,
																		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
																		10,
																		false	
																		);
	
	depthStencilDescriptorAllocator_ = std::make_shared<StaticDescriptorAllocator>(device_,
																		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
																		10,
																		false	
																		);
	
	pipelineAssembler_ = std::make_shared<PipelineAssembler>(device_, resourceDescriptorAllocator_, samplerDescriptorAllocator_);

	// create render target handles for swap chain buffers
	std::shared_ptr<RenderTargetHandle> swapChainRtHandle = std::make_shared<RenderTargetHandle>();
	swapChainRtHandle->format = config_.swapChainFormat;
	swapChainRtHandle->id = Renderer::SwapChainRenderTargetID;
	swapChainRtHandle->sampleDesc.Count = 1;
	swapChainRtHandle->sampleDesc.Quality = 0;
	
	for(int i = 0; i < numBuffers_; i++) {
		winrt::com_ptr<ID3D12Resource> res;
		swapChain_->GetBuffer(i, __uuidof(ID3D12Resource), res.put_void());
		WINRT_ASSERT(res);

		std::shared_ptr<RenderTarget> rt = std::make_shared<RenderTarget>(res, D3D12_RESOURCE_STATE_COMMON);
		swapChainRtHandle->resources.push_back(std::move(rt));
	}

	renderTargetMap_.insert({Renderer::SwapChainRenderTargetID, std::move(swapChainRtHandle)});

	RenderTargetGroupID swapChainGroupID({Renderer::SwapChainRenderTargetID});
	bool success = CreateRenderTargetDescriptorAllocation(swapChainGroupID);
	WINRT_ASSERT(success);

	//depthBufferMemoryAllocator_ = std::make_shared<StaticMemoryAllocator>(GetDevice());
	//InitializeDefaultDepthBuffers();
}

Renderer::~Renderer() {
	std::cout << "Destroying renderer." << std::endl;
	// flush all graphics commands
	const uint64_t lastFenceVal = fenceValue_;
	HRESULT hr = cmdQueue_->Signal(mainFence_.get(), lastFenceVal);
	winrt::check_hresult(hr);

	const uint64_t complValue = mainFence_->GetCompletedValue();
	if(complValue < lastFenceVal) {
		mainFence_->SetEventOnCompletion(lastFenceVal, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE); // block
	}

	shaderCompiler_.reset();
	pipelineAssembler_.reset();
	
	renderTargetMap_.clear();
	depthStencilTargetMap_.clear();

	resourceDescriptorAllocator_.reset();
    samplerDescriptorAllocator_.reset();
    renderTargetDescriptorAllocator_.reset();
    depthStencilDescriptorAllocator_.reset();
	
	memoryAllocator_.reset();
}

std::weak_ptr<PipelineState> Renderer::FinalizeGraphicsPipelineBuild(const GraphicsPipelineBuilder& builder) {
	WINRT_ASSERT(shaderCompiler_);
	
	std::cout << "Finalizing " << builder.id_ << std::endl;
	if(psoMap_.contains(builder.id_)) {
		return std::weak_ptr<GraphicsPipelineState>();
	}

	// check
	WINRT_ASSERT(builder.vertexShaderPath_.has_value() && "Vertex shader not set.");
	WINRT_ASSERT(builder.pixelShaderPath_.has_value() && "Pixel shader not set.");
	// WINRT_ASSERT(builder.renderTarget_ != nullptr || builder.useDefaultRenderTarget_ && "Render target not specified");
	// WINRT_ASSERT(builder.depthBuffer_ != nullptr || builder.useDefaultDepthBuffer_ && "Render target not specified");

	std::shared_ptr<GraphicsPipelineState> pso = std::make_shared<GraphicsPipelineState>(builder.id_);
	pso->resMap_ = std::move(builder.resMap_);
	pso->constantMap_ = std::move(builder.constantMap_);
	pso->samplerMap_ = std::move(builder.samplerMap_);
	pso->staticSamplerMap_ = std::move(builder.staticSamplerMap_);
	pso->rootSignaturePriorityShader_ = builder.rootSignaturePriorityShader_;

	pso->vertexBufferMap_ = std::move(builder.vertexBufferMap_);
	pso->indexBuffer_ = std::move(builder.indexBuffer_);

	for(const auto& [slotIndex, id]: builder.renderTargetMap_) {
		WINRT_ASSERT(renderTargetMap_.contains(id));
		pso->renderTargetMap_.insert({slotIndex, renderTargetMap_[id]});
	}

	if(builder.depthBufferId_.has_value()) {
		WINRT_ASSERT(depthStencilTargetMap_.contains(builder.depthBufferId_.value()));
		pso->depthBuffer_ = depthStencilTargetMap_.at(builder.depthBufferId_.value());
		pso->depthId_ = builder.depthBufferId_.value();
	}

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


	// allocate descriptors for render target(s) & depth buffers
	// if the tuple of render targets (order matters) is already allocated
	// in the descriptor heap, then it is reused, otherwise allocate a new one

	// NOTE: here we depend on the sorted order std::map provides (expecting slotInd to be increasing and continuous integers+)
	std::vector<ResourceID> rtIds;
	for(const auto& [slotInd, rt] : pso->renderTargetMap_) {
		rtIds.push_back(rt.lock()->id);
	}

	// create a tuple from the (in order) render target IDs
	const RenderTargetGroupID rtGroupId(rtIds);

	// if the group id exists, use the mapped set of rt descriptors (1 group of render targets per frame),
	// otherwise, create a new one
	if(!renderTargetAllocMap_.contains(rtGroupId)) {
		CreateRenderTargetDescriptorAllocation(rtGroupId);
	}

	WINRT_ASSERT(renderTargetAllocMap_.contains(rtGroupId));

	// will be used for binding the correct set of render targets in render time
	pso->rtGroupId_ = rtGroupId;

	// enqueue for assembly, and insert into map
	pipelineAssembler_->Enqueue(pso);
	psoMap_.insert({builder.id_, std::move(pso)});
	
	return psoMap_[builder.id_];
}

bool Renderer::CreateRenderTargetDescriptorAllocation(const RenderTargetGroupID& groupId) {
	if(renderTargetAllocMap_.contains(groupId)) {
		return false;
	}
	
	std::vector<std::weak_ptr<DescriptorHeapAllocation>> allocs;
	const std::vector<ResourceID>& rtIds = groupId.GetIDs();

	// for each "frame" (i.e. swap chain buffer), create a set of descriptors according to the queried ids.
	for(int swapChainBufferInd = 0; swapChainBufferInd < numBuffers_; swapChainBufferInd++) {
		std::shared_ptr<DescriptorHeapAllocation> allocation = renderTargetDescriptorAllocator_->Allocate(rtIds.size()).lock();

		// realize each descriptor slot, in the newly created allocation
		for(int rtInd = 0; rtInd < rtIds.size(); rtInd++) {
			const ResourceID& id = rtIds[rtInd];
			
			WINRT_ASSERT(renderTargetMap_.contains(id));
			
			std::shared_ptr<RenderTargetHandle>& rtHandle = renderTargetMap_[id];
			WINRT_ASSERT(rtHandle->resources.size() == numBuffers_);
			
			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
			bool success = allocation->GetCPUDescriptorHandleOffsetted(rtInd, cpuHandle);
			WINRT_ASSERT(success);

			std::shared_ptr<RenderTarget> rt = rtHandle->resources[swapChainBufferInd];
			success = rt->CreateRenderTargetView(cpuHandle);
			WINRT_ASSERT(success);
		}

		allocs.push_back(allocation);
	}

	// insert to map
	renderTargetAllocMap_.insert({groupId, allocs});

	WINRT_ASSERT(renderTargetAllocMap_.contains(groupId));
	return true;
}

void Renderer::OnMemoryAllocatorSet() {
	WINRT_ASSERT(memoryAllocator_);

	auto allocateDescriptors = [&]() {
		WINRT_ASSERT(depthStencilDescriptorAllocator_);
		
		std::vector<std::weak_ptr<DescriptorHeapAllocation>> allocations;
		auto depthHandle = depthStencilTargetMap_.at(Renderer::DefaultDepthStencilTargetID);
		
		for(int i = 0; i < numBuffers_; i++) {
			std::shared_ptr<DescriptorHeapAllocation> allocation = depthStencilDescriptorAllocator_->Allocate(1).lock();

			D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
			bool success = allocation->GetCPUDescriptorHandle(cpuHandle);
			WINRT_ASSERT(success);

			success = depthHandle->resources[i]->CreateDepthStencilView(cpuHandle);
			WINRT_ASSERT(success);
			
			allocations.push_back(allocation);
		}
		
		depthBufferAllocMap_.insert({Renderer::DefaultDepthStencilTargetID, allocations});
	};
	
	memoryAllocator_->AddCommitCallback(allocateDescriptors);
	
	std::shared_ptr<DepthStencilTargetHandle> depthHandle = std::make_shared<DepthStencilTargetHandle>();
	depthHandle->format = DXGI_FORMAT_D32_FLOAT;
	depthHandle->id = Renderer::DefaultDepthStencilTargetID;
	depthHandle->sampleDesc.Count = 1;
	depthHandle->sampleDesc.Quality = 0;
	
	for(int i = 0; i < numBuffers_; i++) {
		const std::string id = Renderer::DefaultDepthStencilTargetID + "_" + std::to_string(i);

		// force depth to be a CommittedResource() due to:
		// "...cannot have D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL set with D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, nor D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS."
		// i.e. depth buffer and uav texture cannot be placed in the same heap
		//
		// Alternatively, we can just use a private heap for depth buffers and (custom) render targets

		std::weak_ptr<DepthBuffer> newDepthBuffer = memoryAllocator_->CreateResource<DepthBuffer>(id,
																								DepthBufferFormat::D32_FLOAT,
																								clientWidth_,
																								clientHeight_);

		depthHandle->resources.push_back(newDepthBuffer.lock());
	}

	depthStencilTargetMap_.insert({Renderer::DefaultDepthStencilTargetID, depthHandle});
}

void Renderer::InitializeDefaultDepthBuffers() {
	WINRT_ASSERT(depthBufferMemoryAllocator_);
	WINRT_ASSERT(depthStencilDescriptorAllocator_);
	

	//
	depthBufferMemoryAllocator_->Commit();
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

	// there's only 1 resource and sampler heap, bind them now
	ID3D12DescriptorHeap* heaps[] = {
		resourceDescriptorAllocator_->GetDescriptorHeap().get(),
		samplerDescriptorAllocator_->GetDescriptorHeap().get()
	};
	cmdList_->SetDescriptorHeaps(_countof(heaps), heaps);

	// scissor and viewport represent entire window, will need to
	// modify this in order to support split-screen
	cmdList_->RSSetScissorRects(1, &scissorRect_);
	cmdList_->RSSetViewports(1, &viewport_);

	// in order for us to clear render targets, they must be in RENDER_TARGET state
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	std::shared_ptr<RenderTarget> curSwapChainBuffer = renderTargetMap_[Renderer::SwapChainRenderTargetID]->resources[curBackBufferIndex_];
	curSwapChainBuffer->ChangeState(D3D12_RESOURCE_STATE_RENDER_TARGET, barriers);

	if(barriers.size() > 0) {
		cmdList_->ResourceBarrier(barriers.size(), barriers.data());	
	}

	// clear current swap chain buffer and default depth
	FLOAT clearColor[4] = {0,0,0,0};
	
	D3D12_CPU_DESCRIPTOR_HANDLE rtDescriptor;
	renderTargetAllocMap_[RenderTargetGroupID({Renderer::SwapChainRenderTargetID})][curBackBufferIndex_].lock()->GetCPUDescriptorHandle(rtDescriptor);

	D3D12_CPU_DESCRIPTOR_HANDLE depthDescriptor;
	depthBufferAllocMap_[Renderer::DefaultDepthStencilTargetID][curBackBufferIndex_].lock()->GetCPUDescriptorHandle(depthDescriptor);
	
	cmdList_->ClearRenderTargetView(rtDescriptor, clearColor, 0, nullptr);
	cmdList_->ClearDepthStencilView(depthDescriptor, D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 0, nullptr);
	
	return cmdList_;
}

void Renderer::FinishCommandList(winrt::com_ptr<ID3D12GraphicsCommandList> cmdList, HRESULT& hr) {
	assert(cmdList == cmdList_);

	// swap chain render targets to state: PRESENT
	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	std::shared_ptr<RenderTarget> curSwapChainBuffer = renderTargetMap_[Renderer::SwapChainRenderTargetID]->resources[curBackBufferIndex_];
	curSwapChainBuffer->ChangeState(D3D12_RESOURCE_STATE_PRESENT, barriers);

	if(barriers.size() > 0) {
		cmdList->ResourceBarrier(barriers.size(), barriers.data());	
	}

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
	
	screenSizeRCV_.SetValue(ninmath::Vector2f{(float) clientWidth_, (float) clientHeight_});
	
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

ComputePipelineBuilder Renderer::BuildComputePipeline(std::string id) {
	ComputePipelineBuilder::BuildFunction buildFunc = std::bind(&Renderer::FinalizeComputePipelineBuild, this, std::placeholders::_1);
	ComputePipelineBuilder builder = ComputePipelineBuilder(id, buildFunc);
	return builder;
}

std::weak_ptr<PipelineState> Renderer::FinalizeComputePipelineBuild(const ComputePipelineBuilder& builder) {
	WINRT_ASSERT(shaderCompiler_);
	
	std::cout << "Finalizing " << builder.id_ << std::endl;
	if(psoMap_.contains(builder.id_)) {
		return std::weak_ptr<GraphicsPipelineState>();
	}

	// check
	WINRT_ASSERT(builder.computeShaderPath_.has_value() && "Compute shader not set.");
	
	std::shared_ptr<ComputePipelineState> pso = std::make_shared<ComputePipelineState>(builder.id_);
	pso->resMap_ = std::move(builder.resMap_);
	pso->constantMap_ = std::move(builder.constantMap_);
	pso->samplerMap_ = std::move(builder.samplerMap_);
	pso->staticSamplerMap_ = std::move(builder.staticSamplerMap_);
	pso->threadCountX_ = std::move(builder.threadCountX_);
	pso->threadCountY_ = std::move(builder.threadCountY_);
	pso->threadCountZ_ = std::move(builder.threadCountZ_);
	pso->threadGroupCountX_ = std::move(builder.threadGroupCountX_);
	pso->threadGroupCountY_ = std::move(builder.threadGroupCountY_);
	pso->threadGroupCountZ_ = std::move(builder.threadGroupCountZ_);
	
	const std::string& shaderPath = builder.computeShaderPath_.value();

	std::shared_ptr<Shader> cs;
	if(shaderMap_.contains(shaderPath)) {
		cs = shaderMap_[shaderPath];
	}
	else {
		cs = std::make_shared<ComputeShader>(shaderPath, pso->threadCountX_, pso->threadCountY_, pso->threadCountZ_);
		shaderCompiler_->Enqueue(cs);
		shaderMap_.insert({shaderPath, cs});
	}

	WINRT_ASSERT(cs);

	pso->computeShader_ = cs;

	// enqueue for assembly, and insert into map
	pipelineAssembler_->Enqueue(pso);
	psoMap_.insert({builder.id_, std::move(pso)});
	
	return psoMap_[builder.id_];
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

	D3D12_MESSAGE_SEVERITY severities[] = {
		D3D12_MESSAGE_SEVERITY_INFO
	};

	// Suppress individual messages by their ID
	D3D12_MESSAGE_ID deny_ids[] = {
		D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,   // I'm really not sure how to avoid this message.
		D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,                         // This warning occurs when using capture frame while graphics debugging.
		D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,                       // This warning occurs when using capture frame while graphics debugging.
	};
 
	D3D12_INFO_QUEUE_FILTER new_filter= {};
	//NewFilter.DenyList.NumCategories = _countof(Categories);
	//NewFilter.DenyList.pCategoryList = Categories;
	new_filter.DenyList.NumSeverities = _countof(severities);
	new_filter.DenyList.pSeverityList = severities;
	new_filter.DenyList.NumIDs = _countof(deny_ids);
	new_filter.DenyList.pIDList = deny_ids;
 
	hr = infoQueue->PushStorageFilter(&new_filter);
	CHECK_HR_NULL(hr);

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

	// "Swap chain back buffers automatically start out in the D3D12_RESOURCE_STATE_COMMON state."

	hr = swapChain1.as(__uuidof(T), swapChain.put_void());
	CHECK_HR_NULL(hr);

	hr = dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
	CHECK_HR_NULL(hr);

	return swapChain;
}


#undef CHECK_HR
