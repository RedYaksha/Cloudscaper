#ifndef RENDERER_SHADER_COMPILER_H_
#define RENDERER_SHADER_COMPILER_H_

#include <memory>
#include <queue>

#include "shader.h"
#include "multithreading/thread_pool.h"



class ShaderCompiler {
public:
    ShaderCompiler();
    ~ShaderCompiler();
    bool Enqueue(std::weak_ptr<Shader> shader);
    void Flush();

private:
    Shader::State CompileShader(std::weak_ptr<Shader> shader, std::promise<Shader::State>& shaderPromise);
    static DXGI_FORMAT ScalarAndMaskToFormat(D3D_REGISTER_COMPONENT_TYPE scalarType, BYTE mask);
    
    std::shared_ptr<ThreadPool<Shader::State>> threadPool_;
    std::queue<std::weak_ptr<Shader>> shaderQueue_;
};

#endif // RENDERER_SHADER_COMPILER_H_
