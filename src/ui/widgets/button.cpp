#include "button.h"

#include "ui/ui_framework.h"

void Button::Render(double deltaTime, UIFrameworkBatcher& batcher) const {
    const float px = pos_.x + margin_.l;
    const float py = pos_.y + margin_.t;
    const float sx = padding_.l + padding_.r + contentSize_.x;
    const float sy = padding_.t + padding_.b + contentSize_.y;

    ninmath::Vector4f color = backgroundColor_;
    if(IsHovered()) {
        color = hoverColor_;
    }
    if(IsPressed()) {
        color = pressedColor_;
    }
    
    batcher.AddQuad({px, py}, {sx, sy}, color);
}

ninmath::Vector2f Button::ComputeDesiredSize() const {
    return ninmath::Vector2f {
        margin_.l + margin_.r + padding_.l + padding_.r + contentSize_.x,
        margin_.t + margin_.b + padding_.t + padding_.b + contentSize_.y,
    };
}
