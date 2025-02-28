#include "button.h"

#include "ui/ui_framework.h"

void Button::Render(double deltaTime, UIFrameworkBatcher& batcher) const {
    const float px = pos_.x + margin_.l;
    float py = pos_.y + margin_.t;
    const float sx = padding_.l + padding_.r + contentSize_.x;
    const float sy = padding_.t + padding_.b + contentSize_.y;

    ninmath::Vector4f color = backgroundColor_;
    ninmath::Vector4f textColor = {0,0,0,1};
    
    if(IsHovered()) {
        color = hoverColor_;
        textColor = {0,1,0,1};
    }
    if(IsPressed()) {
        color = pressedColor_;
        textColor = {0,0,1,1};
    }
    
    // batcher.AddQuad({px, py}, {sx, sy}, color);
    float uniformRadius = 20;
    batcher.AddRoundedRect({px, py}, {sx, sy}, {uniformRadius, uniformRadius, uniformRadius, uniformRadius}, color);

    if(text_.has_value()) {
        py += textHeight_;
        
        ninmath::Vector2f baseScreenPos = {px + padding_.l, py + padding_.t};
        ninmath::Vector2f clipPos = baseScreenPos;
        ninmath::Vector2f clipSize = contentSize_;
        batcher.AddText(baseScreenPos, textColor, fontSize_, text_.value(), clipPos, clipSize);
    }
}

ninmath::Vector2f Button::ComputeDesiredSize() const {
    return ninmath::Vector2f {
        margin_.l + margin_.r + padding_.l + padding_.r + contentSize_.x,
        margin_.t + margin_.b + padding_.t + padding_.b + contentSize_.y,
    };
}

void Button::SetText(std::string text, float fontSize) {
    float textWidth, textHeight;
    fontManager_->ComputeTextScreenSize("Montserrat_Regular", fontSize, text, textWidth, textHeight);

    contentSize_.x = textWidth;
    contentSize_.y = textHeight;

    textHeight_ = textHeight;
    text_ = text;
    fontSize_ = fontSize;
}

void Button::OnPressed() {
    Widget::OnPressed();

    if(onPressedCallback_.has_value()) {
        onPressedCallback_.value()();
    }
}
