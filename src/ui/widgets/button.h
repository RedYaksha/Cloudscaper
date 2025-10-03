#ifndef RENDERER_UI_BUTTON_H_
#define RENDERER_UI_BUTTON_H_

#include <functional>
#include <optional>

#include "widget.h"


typedef std::function<void()> OnPressedCallback;

class Button : public Widget {
public:
    Button()
    :
    hoverColor_({0,0,0,0}),
    pressedColor_({0,0,0,0}),
    contentSize_({100,50})
    {}
    
    void Render(double deltaTime, UIFrameworkBatcher& batcher) const override;
    ninmath::Vector2f ComputeDesiredSize() const override;
    void SetHoverColor(ninmath::Vector4f val) { hoverColor_ = val; }
    void SetPressedColor(ninmath::Vector4f val) { pressedColor_ = val; }
    void SetText(std::string text, float fontSize);
    void SetOnPressed(OnPressedCallback callback) { onPressedCallback_ = callback; }
    
    void OnPressed(const MouseButtonEvent& e) override;
    
private:
    ninmath::Vector4f hoverColor_;
    ninmath::Vector4f pressedColor_;
    ninmath::Vector2f contentSize_;

    std::optional<std::string> text_;
    float fontSize_;
    float textHeight_;
    std::optional<OnPressedCallback> onPressedCallback_;
};

#endif // RENDERER_UI_BUTTON_H_
