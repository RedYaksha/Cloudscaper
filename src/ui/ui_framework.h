#ifndef RENDERER_UI_UI_FRAMEWORK_H_
#define RENDERER_UI_UI_FRAMEWORK_H_

#include "renderer/renderer_types.h"
#include <unordered_map>

#include "font_manager.h"
#include "resources.h"
#include "ui/primitive_renderers/ui_primitives.h"
#include "widgets/button.h"
#include "application/window.h"

class Renderer;
class MemoryAllocator;
class PipelineState;
class Widget;

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
    std::optional<KeyEvent> keyDownEvent;
    std::optional<KeyEvent> keyUpEvent;
    std::optional<MouseButtonEvent> mouseButtonDownEvent;
    std::optional<MouseButtonEvent> mouseButtonUpEvent;
};

class UIFrameworkBatcher {
public:
    UIFrameworkBatcher(std::shared_ptr<FontManager> fontManager): fontManager_(fontManager) {};
    
    void AddQuad(ninmath::Vector2f screenPos, ninmath::Vector2f screenSize, ninmath::Vector4f color) {
        quads_.push_back(Quad { color,
            ninmath::Vector4f {
                        screenPos.x,
                        screenPos.y,
                        screenSize.x,
                        screenSize.y
            }});
    }
    
    void AddRoundedRect(ninmath::Vector2f screenPos, ninmath::Vector2f screenSize,
                        ninmath::Vector4f radii, ninmath::Vector4f color) {
        roundedRects_.push_back(RoundedRect { color,
            ninmath::Vector4f {
                        screenPos.x,
                        screenPos.y,
                        screenSize.x,
                        screenSize.y
                    },
                    radii
        });
    }

    void AddText(ninmath::Vector2f baseScreenPos, ninmath::Vector4f color, float fontSize, std::string text, ninmath::Vector2f clipPos, ninmath::Vector2f clipSize) {
        const FontEntry& entry = fontManager_->GetFontEntry("Montserrat_Regular");
        const ninmath::Vector2f imageSize = { (float) entry.font.images[0].width, (float) entry.font.images[0].height };

        float curX = baseScreenPos.x;
        float curY = baseScreenPos.y;

        float scale = fontSize;

        float totalWidth, totalHeight;
        fontManager_->ComputeTextScreenSize("Montserrat_Regular", fontSize, text, totalWidth, totalHeight);

        for(int i = 0; i < text.size(); i++) {
            const char& curChar = text[i];

            // NOTE: imageBounds origin is bottom-left of image
            // NOTE: planeBounds is normalized and is relative to the baseline position
            
            const artery_font::Glyph<float>& glyph = entry.glyphMap.at(curChar);
            ninmath::Vector2f imagePxStart = {glyph.imageBounds.l, imageSize.y - glyph.imageBounds.t};
            ninmath::Vector2f imagePxEnd = {glyph.imageBounds.r, imageSize.y - glyph.imageBounds.b};

            // TODO: compute screen space start/end pos => startPos and size
            ninmath::Vector4f planeBounds = { glyph.planeBounds.l, glyph.planeBounds.r, glyph.planeBounds.t, glyph.planeBounds.b };
            planeBounds = scale * planeBounds;
            
            ninmath::Vector2f rectSize = {
                planeBounds.r - planeBounds.l,
                planeBounds.t - planeBounds.b,
            };

            ninmath::Vector2f rectPos = {
                curX + planeBounds.l,
                curY - planeBounds.t // subtract
            };
            
            TextRect rect;
            rect.color = color;
            rect.transform = {rectPos.x, rectPos.y, rectSize.x, rectSize.y };
            rect.clipTransform = {clipPos.x, clipPos.y, clipSize.x, totalHeight};
            rect.uvStart = imagePxStart / imageSize;
            rect.uvEnd = imagePxEnd / imageSize;

            textRects_.push_back(rect);

            curX += scale * glyph.advance.h;
            curY -= scale * glyph.advance.v;
        }
    }

    const std::vector<Quad>& GetQuads() const { return quads_; }
    const std::vector<TextRect>& GetTextRects() const { return textRects_; }
    const std::vector<RoundedRect>& GetRoundedRects() const { return roundedRects_; }
    
private:
    std::vector<Quad> quads_;
    std::vector<TextRect> textRects_;
    std::vector<RoundedRect> roundedRects_;

    std::shared_ptr<FontManager> fontManager_;
};

class QuadPrimitiveRenderer;
class TextRectPrimitiveRenderer;
class RoundedRectPrimitiveRenderer;

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
    
    template<IsWidget T, class ... _Types>
    requires std::is_constructible_v<T, _Types...>
    std::shared_ptr<T> CreateWidget(Widget* parent, WidgetID id, _Types&&... args);

    bool RegisterFont(FontID id, std::string arfontPath, std::string atlasImagePath);
    void Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList);
    void Tick(double deltaTime);

    void SetRootWidget(std::shared_ptr<Widget> widget) {
        WINRT_ASSERT(widgetMap_.contains(widget->GetID()));
        WINRT_ASSERT(!rootWidget_ && "Overriding root widget!");
        rootWidget_ = widget;
    }
    
private:
    void OnMouseMoved(MouseEvent e);
    void OnKeyDown(KeyEvent e);
    void OnKeyUp(KeyEvent e);
    void OnMouseButtonDown(MouseButtonEvent e);
    void OnMouseButtonUp(MouseButtonEvent e);
    
    std::shared_ptr<QuadPrimitiveRenderer> quadRenderer_;
    std::shared_ptr<TextRectPrimitiveRenderer> textRenderer_;
    std::shared_ptr<RoundedRectPrimitiveRenderer> roundedRectPrimitiveRenderer_;
    
    std::shared_ptr<Widget> rootWidget_;
    
    std::unordered_map<WidgetID, std::shared_ptr<Widget>> widgetMap_;
    std::mutex curFrameEventsMutex_;
    TickEvents curFrameEvents_;

    std::shared_ptr<FontManager> fontManager_;
    std::unordered_map<std::string, std::weak_ptr<Resource>> fontAtlasImageResourceMap_;
    
    std::shared_ptr<Renderer> renderer_;
    std::shared_ptr<MemoryAllocator> memAllocator_;
    std::shared_ptr<Window> window_;

    ninmath::Vector2i mostRecentMousePos_;
};

template <IsWidget T, class ... _Types> requires std::is_constructible_v<T, _Types...>
std::shared_ptr<T> UIFramework::CreateWidget(WidgetID id, _Types&&... args) {
    if(widgetMap_.contains(id)) {
        return nullptr;
    }

    std::shared_ptr<T> newWidget = std::make_shared<T>(std::forward<_Types>(args)...);
    widgetMap_.insert({id, newWidget});
    newWidget->SetID(id);
    newWidget->SetFontManager(fontManager_);
    newWidget->SetFramework(this);
    newWidget->Construct();
    newWidget->OnInitialized();

    // TODO: categorize widgets based on what event listeners they have
    // so we can just iterate 
    
    return newWidget;
}

template <IsWidget T, class ... _Types> requires std::is_constructible_v<T, _Types...>
std::shared_ptr<T> UIFramework::CreateWidget(Widget* parent, WidgetID id, _Types&&... args) {
    WidgetID childID = parent->GetID() + "__" + id;
    return CreateWidget<T>(childID, std::forward<_Types>(args)...);
    
}

#endif // RENDERER_UI_UI_FRAMEWORK_H_
