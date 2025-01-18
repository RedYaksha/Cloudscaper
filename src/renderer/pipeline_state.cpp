#include "pipeline_state.h"
#include "shader.h"

#include <winrt/base.h>

#define ADD_IF_OK(var) if(! ## var ##.expired()) { outVec.push_back(var); }
void GraphicsPipelineState::GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) {
    WINRT_ASSERT(!vertexShader_.expired() && !pixelShader_.expired());

    ADD_IF_OK(vertexShader_);
    ADD_IF_OK(hullShader_);
    ADD_IF_OK(domainShader_);
    ADD_IF_OK(pixelShader_);
}

void ComputePipelineState::GetShaders(std::vector<std::weak_ptr<Shader>>& outVec) {
    ADD_IF_OK(computeShader_);
}
#undef ADD_IF_OK
