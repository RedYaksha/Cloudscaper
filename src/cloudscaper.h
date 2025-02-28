#ifndef CLOUDSCAPER_CLOUDSCAPER_H_
#define CLOUDSCAPER_CLOUDSCAPER_H_

#include "application.h"
#include "resources.h"
#include "root_constant_value.h"
#include "ninmath/ninmath.h"
#include "ui/widgets/labeled_numeric_input.h"
#include "ui/widgets/text.h"
#include "ui/widgets/vertical_layout.h"

class Resource;
class MemoryAllocator;
class Renderer;
class UIFramework;
class PipelineState;
class GraphicsPipelineState;
class DepthBuffer;
class RenderTarget;

class VertexBufferBase;
class IndexBufferBase;

struct BasicVertexData {
    ninmath::Vector4f pos;
    ninmath::Vector2f uv;
};

class Cloudscaper : public Application {
public:
    Cloudscaper(HINSTANCE hinst);
    ~Cloudscaper();
    virtual void Tick(double deltaTime) override;

private:
    std::weak_ptr<Resource> imageTex_;
    std::weak_ptr<Resource> computeTex_;
    std::weak_ptr<VertexBufferBase> vertexBuffer_;
    std::weak_ptr<IndexBufferBase> indexBuffer_;

    struct AtmosphereContext {
        float Rb;
        float Rt;
    };

    struct SkyContext {
		ninmath::Vector3f cameraPos;
		float pad0;
		ninmath::Vector3f lightDir;
		float pad1;
		ninmath::Vector3f viewDir;
		float pad2;
		ninmath::Vector3f sunIlluminance;
		float pad3;
		ninmath::Vector3f groundAlbedo;
		float pad4;
    };
    
    struct RenderContext
    {
        ninmath::Matrix4x4f invProjectionMat;
        ninmath::Matrix4x4f invViewMat;
        ninmath::Vector2u screenSize;
        uint32_t frame;
        float pad0;
        ninmath::Vector3f cameraPos;
        float time; // total elapsed time
    };
    
	struct CloudParameters {
		ninmath::Vector3f lightColor;
		float phaseG;
		
		float modelNoiseScale; // 
		float cloudCoverage; //
		float highFreqScale;
		float highFreqModScale;
		
		float highFreqHFScale;
		float largeDtScale;
		float extinction;
		int numSamples;

		ninmath::Vector4f beersScale;

		ninmath::Vector2f weatherRadius;
		float minWeatherCoverage;
		int useBlueNoise;
		
		int fixedDt;
		ninmath::Vector3f pad0;

		int useAlpha;
		ninmath::Vector3f windDir;

		float windSpeed;
		ninmath::Vector3f pad1;
		
		ninmath::Vector4f lodThresholds;
		
		float innerShellRadius;
		float outerShellRadius;
		ninmath::Vector2f pad2;

		ninmath::Vector3f lightDir; // posToLight
		float pad3;
	};
	
    std::weak_ptr<DynamicBuffer<AtmosphereContext>> atmosphereContextBuffer_;
    AtmosphereContext atmosphereContext_;
    
    std::weak_ptr<DynamicBuffer<SkyContext>> skyContextBuffer_;
    SkyContext skyContext_;
	
    std::weak_ptr<DynamicBuffer<RenderContext>> renderContextBuffer_;
    RenderContext renderContext_;
	
    std::weak_ptr<DynamicBuffer<CloudParameters>> cloudParametersBuffer_;
    CloudParameters cloudParameters_;
    
    // std::weak_ptr<DepthBuffer> renderTarget_;

    std::vector<BasicVertexData> vertices_;
    std::vector<uint32_t> indices_;

    std::shared_ptr<Window> mainWindow_;
    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<UIFramework> uiFramework_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
    RootConstantValue<float> testConstVal_;
    
    RootConstantValue<ninmath::Vector2f> screenSizeRCV_;

	// atmosphere resources
    std::weak_ptr<Texture2D> transmittanceLUT_;
    std::weak_ptr<Texture2D> multiScatteringLUT_;
    std::weak_ptr<Texture2D> skyViewLUT_;

	// atmosphere pipelines
    std::weak_ptr<PipelineState> transmittanceCPSO_;
    std::weak_ptr<PipelineState> multiScatteringCPSO_;
    std::weak_ptr<PipelineState> skyviewCPSO_;
    std::weak_ptr<PipelineState> renderSkyGPSO_;

	// cloud resources
    std::weak_ptr<Texture2D> blueNoise_;
    std::weak_ptr<Texture2D> weatherTexture_;
    std::weak_ptr<Texture3D> modelNoise_;
    std::weak_ptr<Texture3D> detailNoise_;
    std::weak_ptr<RenderTarget> cloudRT0_;
    std::weak_ptr<RenderTarget> cloudRT1_;
	
	// clouds
    std::weak_ptr<PipelineState> computeModelNoiseCPSO_;
    std::weak_ptr<PipelineState> computeDetailNoiseCPSO_;
    std::weak_ptr<PipelineState> gen3DMipMapsCPSO_;
    std::weak_ptr<PipelineState> renderCloudsGPSO_;
	

	bool noiseGenDone_;

	uint32_t curFrame_;
	float elapsedTime_;

	std::shared_ptr<VerticalLayout> rootWidget_;
	std::shared_ptr<Text> text_;
	std::shared_ptr<LabeledNumericInput<float>> testFloatInput_;
	std::vector<std::shared_ptr<Widget>> paramNumericInputs_;
};


#endif // CLOUDSCAPER_CLOUDSCAPER_H_
