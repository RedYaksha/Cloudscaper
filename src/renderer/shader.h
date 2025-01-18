#ifndef RENDERER_SHADER_H_
#define RENDERER_SHADER_H_

#include <future>
#include <string>
#include "renderer_types.h"
#include "shader_types.h"
#include <set>
#include <dxcapi.h>

class ShaderCompiler;

enum class ShaderType {
    Vertex,
    Hull,
    Domain,
    Pixel,
    
    Compute,
    
    NumShaderTypes
};

class Shader {
public:
    // TBD: Should shader <compile> state be defined/handled in ShaderCompiler,
    //      if then, what is the purpose of Shader, should we assume Shader is
    //      an 'always valid' resource?
    
    enum StateType {
        Ok,
        CompileError,
        NumStateTypes,
    };

    struct CompilationData {
        winrt::com_ptr<IDxcBlob> rootSigBlob;
        RootParameterUsageMap rootParamUsage;
        std::set<VertexInputLayoutElem> inputLayoutElems;
    };

    struct State {
        StateType type;
        std::string msg;
        std::shared_ptr<CompilationData> compileData;

        static State Error(std::string inMsg) {
            return State { StateType::CompileError, inMsg };
        }

        static State Ok() {
            return State { StateType::Ok, "" };
        }
    };
    
    Shader(std::string sourceFile, ShaderType type)
    :
    sourceFile_(std::move(sourceFile)),
    type_(type),
    future_(promise_.get_future().share())
    {}

    const std::string& GetSourceFile() const { return sourceFile_; }

    bool IsReadyAndOk() const {
        if(IsStateReady()) {
            Shader::State state = future_.get();
            return state.type == Shader::StateType::Ok;
        }
        return false;
    }

    bool IsStateReady() const {
        return future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    const Shader::State& GetState_Block() const {
        return future_.get();
    }
private:
    std::string sourceFile_;
    ShaderType type_;
    
    friend ShaderCompiler;
    std::promise<Shader::State> promise_;
    std::shared_future<Shader::State> future_;
};

class VertexShader : public Shader {
public:
    VertexShader(std::string sourceFile)
        : Shader(sourceFile, ShaderType::Vertex)
    {}
    
private:
};

class PixelShader : public Shader {
public:
    PixelShader(std::string sourceFile)
        : Shader(sourceFile, ShaderType::Pixel)
    {}
    
private:
};

class HullShader : public Shader {
public:
    HullShader(std::string sourceFile)
        : Shader(sourceFile, ShaderType::Hull)
    {}
    
private:
};

class DomainShader : public Shader {
public:
    DomainShader(std::string sourceFile)
        : Shader(sourceFile, ShaderType::Domain)
    {}
    
private:
};

#endif // RENDERER_SHADER_H_
