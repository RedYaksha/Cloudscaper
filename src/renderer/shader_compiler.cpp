#include "shader_compiler.h"

#include <set>

#include "shader.h"
#include "multithreading/thread_pool.h"
#include "renderer_types.h"
#include "dxcapi.h"
#include "d3d12shader.h"
#include "d3dcompiler.h"
#include "shader_types.h"

ShaderCompiler::ShaderCompiler() {
    threadPool_ = std::make_shared<ThreadPool<Shader::State>>();
    threadPool_->Start();
}

ShaderCompiler::~ShaderCompiler() {
    // stop all threads
    threadPool_->Stop();
}

bool ShaderCompiler::Enqueue(std::weak_ptr<Shader> shader) {
    // don't enqueue if already in the queue
    // return false;
    shaderQueue_.push(shader);
    return true;
}

void ShaderCompiler::Flush() {
    while(shaderQueue_.size() > 0) {
        std::weak_ptr<Shader> shader = std::move(shaderQueue_.front());
        shaderQueue_.pop();

        std::promise<Shader::State>& shaderPromise = shader.lock()->promise_;
        std::packaged_task<Shader::State()> task = std::packaged_task<Shader::State()>(
                                std::bind(
                                    &ShaderCompiler::CompileShader,
                                    this,
                                    shader, 
                                    std::ref(shaderPromise)));
                                    
        threadPool_->AddTask(std::move(task));
    }
}

Shader::State ShaderCompiler::CompileShader(std::weak_ptr<Shader> inShader, std::promise<Shader::State>& shaderPromise) {
    std::shared_ptr<Shader> shader = inShader.lock();
    std::cout << "Compiling " << shader->sourceFile_ << std::endl;;

    const std::string& sourceFile = shader->sourceFile_;

    // find file
    const DWORD fileAttrs = GetFileAttributes(sourceFile.c_str());
    if(fileAttrs == INVALID_FILE_ATTRIBUTES || (fileAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
        auto out = Shader::State::Error(std::format("File not found: {}", sourceFile));
        shaderPromise.set_value(out);
        return out;
    }

    // compile
    HRESULT hr;

    winrt::com_ptr<IDxcUtils> utils;
    hr = DxcCreateInstance(CLSID_DxcUtils, __uuidof(IDxcUtils), utils.put_void());
    winrt::check_hresult(hr);

    winrt::com_ptr<IDxcCompiler3> compiler;
    hr = DxcCreateInstance(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), compiler.put_void());
    winrt::check_hresult(hr);

    std::wstring sourceFileW;
    {
        const int numBytesNeeded = MultiByteToWideChar(CP_UTF8, 0, sourceFile.c_str(), -1, NULL, 0);
        assert(numBytesNeeded >= 0);
        
        sourceFileW = std::wstring(numBytesNeeded, 0);
        MultiByteToWideChar(CP_UTF8, 0, sourceFile.c_str(), -1, sourceFileW.data(), numBytesNeeded);
    }
    
    winrt::com_ptr<IDxcBlobEncoding> source;
    hr = utils->LoadFile(sourceFileW.c_str(), NULL, source.put());
    winrt::check_hresult(hr);

    std::wstring profileArg;
    switch(shader->type_) {
    case ShaderType::Vertex:
        profileArg = L"vs";
        break;
    case ShaderType::Pixel:
        profileArg = L"ps";
        break;
    case ShaderType::Compute:
        profileArg = L"cs";
        break;
    default:
        break;
    }

    profileArg += L"_6_0";

    std::vector<LPCWSTR> compileArgs {
        L"-E", // entry point
        L"main",
        
        L"-T", // profile
        profileArg.c_str(),
        
        DXC_ARG_PACK_MATRIX_ROW_MAJOR,
        DXC_ARG_DEBUG,
    };

    winrt::com_ptr<IDxcIncludeHandler> includeHandler;
    utils->CreateDefaultIncludeHandler(includeHandler.put());

    DxcBuffer sourceBuffer;
    sourceBuffer.Ptr = source->GetBufferPointer();
    sourceBuffer.Size = source->GetBufferSize();
    sourceBuffer.Encoding = 0u;

    winrt::com_ptr<IDxcResult> result;
    hr = compiler->Compile(
        &sourceBuffer,
        compileArgs.data(),
        compileArgs.size(),
        includeHandler.get(),
        __uuidof(IDxcResult),
        result.put_void()
    );
    winrt::check_hresult(hr);

    winrt::com_ptr<IDxcBlobUtf8> errors;
    result->GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), errors.put_void(), NULL);

    if(errors && errors->GetStringLength() > 0) {
        const LPCSTR errorMessage = errors->GetStringPointer();
        Shader::State out = Shader::State::Error(errorMessage);
        shaderPromise.set_value(out);
        
        std::cout << errorMessage << std::endl;
        return out;
    }

    //
    winrt::com_ptr<IDxcBlob> reflectionBlob;
    result->GetOutput(DXC_OUT_REFLECTION, __uuidof(IDxcBlob), reflectionBlob.put_void(), NULL);

    
    winrt::com_ptr<IDxcBlob> rootSigBlob;
    result->GetOutput(DXC_OUT_ROOT_SIGNATURE, __uuidof(IDxcBlob), rootSigBlob.put_void(), NULL);

    // winrt::com_ptr<ID3D12Device> device;
    // winrt::com_ptr<ID3D12RootSignature> rootSig;
    // device->CreateRootSignature(0, rootSigBlob->GetBufferPointer(), rootSigBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), rootSig.put_void());

    //
    DxcBuffer reflectionBuffer;
    reflectionBuffer.Ptr = reflectionBlob->GetBufferPointer();
    reflectionBuffer.Size = reflectionBlob->GetBufferSize();
    reflectionBuffer.Encoding = 0;

    winrt::com_ptr<ID3D12ShaderReflection> shaderReflection;
    utils->CreateReflection(&reflectionBuffer, __uuidof(ID3D12ShaderReflection), shaderReflection.put_void());

    D3D12_SHADER_DESC shaderDesc;
    shaderReflection->GetDesc(&shaderDesc);

    std::cout << shaderDesc.InputParameters << std::endl;

    std::set<VertexInputLayoutElem> inputLayoutElems;
    
    if(shader->type_ == ShaderType::Vertex) {
        for(uint32_t i = 0; i < shaderDesc.InputParameters; i++) {
            D3D12_SIGNATURE_PARAMETER_DESC sigParamDesc;
            shaderReflection->GetInputParameterDesc(i, &sigParamDesc);
            
            std::cout << sigParamDesc.SemanticName << std::endl;

            // ComponentType = Scalar Type (uint, float, etc.)
            // Mask = num components (float2, uint3, etc.) where # of 1's in a binary value is # of components

            VertexInputLayoutElem elem;
            elem.semanticName = sigParamDesc.SemanticName;
            elem.semanticIndex = sigParamDesc.SemanticIndex;
            elem.format = ScalarAndMaskToFormat(sigParamDesc.ComponentType, sigParamDesc.Mask);

            WINRT_ASSERT(!inputLayoutElems.contains(elem) && "Defining same index semantics is a compilation error.");
            inputLayoutElems.insert(elem);
        }

        std::cout << "Input layout computed..." << std::endl;
    }

    // index = register
    //

    
    static std::map<D3D_SHADER_INPUT_TYPE, ResourceDescriptorType> shaderToRootType = {
        {D3D_SIT_TEXTURE, ResourceDescriptorType::SRV},
        {D3D_SIT_STRUCTURED, ResourceDescriptorType::SRV},
        {D3D_SIT_SAMPLER, ResourceDescriptorType::Sampler},
        
        {D3D_SIT_CBUFFER, ResourceDescriptorType::CBV},
        
        {D3D_SIT_UAV_RWTYPED, ResourceDescriptorType::UAV},
        {D3D_SIT_UAV_RWSTRUCTURED, ResourceDescriptorType::UAV},
    };
    
    RootParameterUsageMap rootParamUsage;
    
    for(uint32_t i = 0; i < shaderDesc.BoundResources; i++) {
        D3D12_SHADER_INPUT_BIND_DESC desc;
        shaderReflection->GetResourceBindingDesc(i, &desc);

        std::cout << desc.Name << std::endl;
        
        // Could be used for bindless resources...
        // desc.Name = variable name
        
        // TODO: use for type checking
        // desc.ReturnType
        // desc.Dimension

        // BindPoint = register number
        // Space = register space
        WINRT_ASSERT(shaderToRootType.contains(desc.Type));

        const ResourceDescriptorType rootType = shaderToRootType[desc.Type];

        RootParamUsageKey key = shader_utils::CreateRootParamKey(rootType, desc.Space);
        rootParamUsage[key].insert(desc.BindPoint);
    }

    // Output:
    // - Resource map (SRV,CBV,UAV,Sampler)
    //      What's bound at each register
    // - Root signature Blob (if one exists) from this compilation
    // - Vertex Buffer Layout
    //      - can be used for runtime type checking when binding vertex buffers

    std::shared_ptr<Shader::CompilationData> compilationData = std::make_shared<Shader::CompilationData>();
    compilationData->inputLayoutElems = inputLayoutElems;
    compilationData->rootParamUsage = rootParamUsage;
    compilationData->rootSigBlob = rootSigBlob;

    Shader::State out = Shader::State::Ok();
    out.compileData = compilationData;
    shaderPromise.set_value(out);
    return out;
}

DXGI_FORMAT ShaderCompiler::ScalarAndMaskToFormat(D3D_REGISTER_COMPONENT_TYPE scalarType, BYTE mask) {
    // ComponentType = Scalar Type (uint, float, etc.)
    // Mask = num components (float2, uint3, etc.) where # of 1's in a binary value is # of components

    static std::map<BYTE, uint16_t> numElemMap = {
        {0x1, 1u}, // b'0001'
        {0x3, 2u}, // b'0011'
        {0x7, 3u}, // b'0111'
        {0xF, 4u}, // b'1111'
    };

    if(!numElemMap.contains(mask)) {
        return DXGI_FORMAT_UNKNOWN;
    }

    const uint16_t numElements = numElemMap[mask];

    typedef std::tuple<D3D_REGISTER_COMPONENT_TYPE, uint16_t> FormatKey;
#define FORMAT_KEY(scalarType, numElem) std::make_tuple(D3D_REGISTER_COMPONENT_ ## scalarType ##, numElem)
    static std::map<FormatKey, DXGI_FORMAT> formatMap = {
        {FORMAT_KEY(UINT32, 1), DXGI_FORMAT_R32_UINT},
        {FORMAT_KEY(UINT32, 2), DXGI_FORMAT_R32G32_UINT},
        {FORMAT_KEY(UINT32, 3), DXGI_FORMAT_R32G32B32_UINT},
        {FORMAT_KEY(UINT32, 4), DXGI_FORMAT_R32G32B32A32_UINT},
        
        {FORMAT_KEY(SINT32, 1), DXGI_FORMAT_R32_SINT},
        {FORMAT_KEY(SINT32, 2), DXGI_FORMAT_R32G32_SINT},
        {FORMAT_KEY(SINT32, 3), DXGI_FORMAT_R32G32B32_SINT},
        {FORMAT_KEY(SINT32, 4), DXGI_FORMAT_R32G32B32A32_SINT},
        
        {FORMAT_KEY(FLOAT32, 1), DXGI_FORMAT_R32_FLOAT},
        {FORMAT_KEY(FLOAT32, 2), DXGI_FORMAT_R32G32_FLOAT},
        {FORMAT_KEY(FLOAT32, 3), DXGI_FORMAT_R32G32B32_FLOAT},
        {FORMAT_KEY(FLOAT32, 4), DXGI_FORMAT_R32G32B32A32_FLOAT},
    };
#undef FORMAT_KEY
    
    const FormatKey key = std::make_tuple(scalarType, numElements);

    WINRT_ASSERT(formatMap.contains(key) && "Using an invalid scalar/num pair.");

    return formatMap[key];
}
