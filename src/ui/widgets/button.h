#ifndef RENDERER_UI_BUTTON_H_
#define RENDERER_UI_BUTTON_H_

#include "widget.h"

class Button : public Widget {
public:
    Button()
    :
    hoverColor_({0,0,0,0}),
    pressedColor_({0,0,0,0}),
    contentSize_({100,100})
    {}
    
    void Render(double deltaTime, UIFrameworkBatcher& batcher) const override;
    ninmath::Vector2f ComputeDesiredSize() const override;
    void SetHoverColor(ninmath::Vector4f val) { hoverColor_ = val; }
    void SetPressedColor(ninmath::Vector4f val) { pressedColor_ = val; }
    
private:
    ninmath::Vector4f hoverColor_;
    ninmath::Vector4f pressedColor_;
    ninmath::Vector2f contentSize_;
};

#endif // RENDERER_UI_BUTTON_H_
