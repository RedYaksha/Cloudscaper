#ifndef RENDERER_UI_UI_FRAMEWORK_H_
#define RENDERER_UI_UI_FRAMEWORK_H_

#include "renderer/renderer_types.h"
#include "widgets/widget.h"
#include <unordered_map>

#include "resources.h"
#include "ui_primitives.h"
#include "widgets/button.h"
#include "widgets/vertical_layout.h"
#include "application/window.h"

class Renderer;
class MemoryAllocator;
class PipelineState;

struct TickEvents {
    TickEvents() = default;
    
    void Reset() {
        mouseEvent.reset();
        keyDownEvent.reset();
        keyUpEvent.reset();
        mouseButtonDownEvent.reset();
        mouseButtonUpEvent.reset();
    }
    
    std::optional<MouseEvent> mouseEvent;
    std::optional<KeyEventType> keyDownEvent;
    std::optional<KeyEventType> keyUpEvent;
    std::optional<MouseButtonEvent> mouseButtonDownEvent;
    std::optional<MouseButtonEvent> mouseButtonUpEvent;
    
};

class UIFrameworkBatcher {
public:
    void AddQuad(ninmath::Vector2f screenPos, ninmath::Vector2f screenSize, ninmath::Vector4f color) {
        quads_.push_back(Quad { color,
            ninmath::Vector4f {
                        screenPos.x,
                        screenPos.y,
                        screenSize.x,
                        screenSize.y
            }});
    }

    const std::vector<Quad>& GetQuads() const { return quads_; }
    
private:
    std::vector<Quad> quads_;
    
};

class QuadPrimitiveRenderer;

template<typename T>
concept IsWidget = requires {
    std::derived_from<T, Widget>;
};

class UIFramework {
public:
    UIFramework(std::shared_ptr<Renderer> renderer,
                std::shared_ptr<MemoryAllocator> memAllocator,
                std::shared_ptr<Window> window);
    
    template<IsWidget T, class ... _Types>
    requires std::is_constructible_v<T, _Types...>
    std::shared_ptr<T> CreateWidget(WidgetID id, _Types&&... args);

    void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    void Tick(double deltaTime);

private:
    void OnMouseMoved(MouseEvent e);
    void OnKeyDown(KeyEvent e);
    void OnKeyUp(KeyEvent e);
    void OnMouseButtonDown(MouseButtonEvent e);
    void OnMouseButtonUp(MouseButtonEvent e);
    
    
    std::shared_ptr<QuadPrimitiveRenderer> quadRenderer_;
    
    std::shared_ptr<VerticalLayout> rootWidget_;
    std::shared_ptr<Button> button0_;
    std::shared_ptr<Button> button1_;
    std::shared_ptr<Button> button2_;

    std::unordered_map<WidgetID, std::shared_ptr<Widget>> widgetMap_;
    std::mutex curFrameEventsMutex_;
    TickEvents curFrameEvents_;
    
    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
    std::shared_ptr<Window> window_;
};

template <IsWidget T, class ... _Types> requires std::is_constructible_v<T, _Types...>
std::shared_ptr<T> UIFramework::CreateWidget(WidgetID id, _Types&&... args) {
    if(widgetMap_.contains(id)) {
        return nullptr;
    }

    std::shared_ptr<T> newWidget = std::make_shared<T>(std::forward<_Types>(args)...);
    widgetMap_.insert({id, newWidget});
    newWidget->SetID(id);

    // TODO: categorize widgets based on what event listeners they have
    // so we can just iterate 
    
    return newWidget;
}

#endif // RENDERER_UI_UI_FRAMEWORK_H_
