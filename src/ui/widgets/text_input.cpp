#include "text_input.h"

#include <iostream>

#include "ui/ui_framework.h"


TextInput::TextInput()
    :
    text_(""),
    fontSize_(32),
    textHeight_(0),
    textWidth_(0),
    width_(50),
    textXPosOffset_(0),
    tickCount_(0),
    blinkTotalTime_(0.),
    blinkTime_(1.2) {
    
    isFocusable_ = true;
}

void TextInput::Tick(double deltaTime) {
    tickCount_++;
    
    blinkTotalTime_ += deltaTime;
    if(blinkTotalTime_ > blinkTime_) {
        blinkTotalTime_ = 0.;
    }
}

void TextInput::Render(double deltaTime, UIFrameworkBatcher& batcher) const {
    const float px = pos_.x + margin_.l + padding_.l;
    float py = pos_.y + margin_.t + padding_.t;

    float pxOffset = 0;
    if(isFocused_ && textWidth_ > width_) {
        pxOffset = -(textWidth_ - width_);
    }

    {
        const ninmath::Vector2f bgPos {
            pos_.x + margin_.l,
            pos_.y + margin_.t,
        };
        
        const ninmath::Vector2f bgSize{
            padding_.l + width_ + padding_.r,
            padding_.t + textHeight_ + padding_.b,
        };
        
        batcher.AddQuad(bgPos, bgSize, backgroundColor_);
    }
    
    if(text_.size() > 0) {
        ninmath::Vector2f baseScreenPos = {pxOffset + px, py + textHeight_};
        ninmath::Vector2f clipPos = {px, py};
        ninmath::Vector2f clipSize = {width_, textHeight_};
        
        batcher.AddText(baseScreenPos, foregroundColor_, fontSize_, text_, clipPos, clipSize);

        // draw text region
        // batcher.AddQuad({px, py}, {width_, textHeight_}, {1,0,0,1});
    }

    if(isFocused_ && blinkTotalTime_ > blinkTime_ / 2.) {
        batcher.AddQuad({px + pxOffset + textWidth_, py}, {2, textHeight_}, {1,1,1,1});
    }
}

ninmath::Vector2f TextInput::ComputeDesiredSize() const {
    return ninmath::Vector2f {
        margin_.l + margin_.r + padding_.l + padding_.r + width_,
        margin_.t + margin_.b + padding_.t + padding_.b + textHeight_,
    };
}

void TextInput::OnKeyPressed(KeyEvent e) {
    if(!isFocused_) {
        return;
    }

    // delete or backspace
    if(e.key == VK_DELETE || e.key == VK_BACK) {
        if(text_.size() <= 0) {
            return;
        }
        
        text_.pop_back();
        ComputeTextSize();
        return;
    }

    // enter key
    if(e.key == VK_RETURN) {
        SetIsFocused(false);
        OnUnfocused();
        return;
    }

    char c = VirtualKeyToChar(e.key);
    
    if(c < 32 || c > 122) {
        return;
    }
    
    std::cout << "tick count: " << tickCount_ << std::endl;
    std::cout << "key pressed: " << c << std::endl;
    text_ += c;
    ComputeTextSize();
}

void TextInput::OnInitialized() {
    ComputeTextSize();
}

void TextInput::SetText(std::string text) {
    text_ = text;
    ComputeTextSize();
}

void TextInput::SetFontSize(float fontSize) {
    fontSize_ = fontSize;
    ComputeTextSize();
}

void TextInput::ComputeTextSize() {
    float textWidth, textHeight;
    fontManager_->ComputeTextScreenSize("Montserrat_Regular", fontSize_, text_, textWidth, textHeight);
    textWidth_ = textWidth;
    textHeight_ = textHeight;
}

char TextInput::VirtualKeyToChar(UINT vkCode) {
    BYTE keyboardState[256];
    WCHAR unicodeChar[2] = {0};  // Buffer for UTF-16 character

    if (!GetKeyboardState(keyboardState)) {
        return 0;
    }

    // Convert VK to Unicode
    if (ToUnicode(vkCode, MapVirtualKey(vkCode, MAPVK_VK_TO_VSC), keyboardState, unicodeChar, 2, 0) == 1) {
        return static_cast<char>(unicodeChar[0]); // Only use first byte for ASCII
    }

    return 0;
}
