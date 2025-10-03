#include "text.h"

#include "ui/ui_framework.h"

Text::Text()
    : fontSize_(32), textHeight_(0), textWidth_(0) {}

void Text::Render(double deltaTime, UIFrameworkBatcher& batcher) const {
    if(!text_.has_value()) {
        return;
    }
    
    const float px = pos_.x + margin_.l + padding_.l;
    float py = pos_.y + margin_.t + padding_.t + textHeight_;

    ninmath::Vector2f baseScreenPos = {px + padding_.l, py + padding_.t};
    ninmath::Vector2f clipPos = baseScreenPos;
    ninmath::Vector2f clipSize = {textWidth_, textHeight_};

    // ninmath::Vector4f color = isHovered_? ninmath::Vector4f {0,0,0,1} : foregroundColor_;
    batcher.AddText(baseScreenPos, foregroundColor_, fontSize_, text_.value(), clipPos, clipSize);

    /*
    ninmath::Vector2f hitBoxPos = GetHitboxPosition();
    ninmath::Vector2f hitBoxSize = GetHitboxSize();
    batcher.AddQuad(hitBoxPos, hitBoxSize, ninmath::Vector4f {0,1,0,1});
    */
}

ninmath::Vector2f Text::ComputeDesiredSize() const {
    return ninmath::Vector2f {
        margin_.l + margin_.r + padding_.l + padding_.r + textWidth_,
        margin_.t + margin_.b + padding_.t + padding_.b + textHeight_,
    };
}

void Text::SetText(std::string text) {
    text_ = text;
    ComputeTextSize();
}

void Text::SetFontSize(float fontSize) {
    fontSize_ = fontSize;
    ComputeTextSize();
}

void Text::ComputeTextSize() {
    if(!text_.has_value()) {
        return;
    }
    float textWidth, textHeight;
    fontManager_->ComputeTextScreenSize("Montserrat_Regular", fontSize_, text_.value(), textWidth, textHeight);
    textWidth_ = textWidth;
    textHeight_ = textHeight;
}