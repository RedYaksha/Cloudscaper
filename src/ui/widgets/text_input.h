#ifndef UI_WIDGETS_TEXT_INPUT_H_
#define UI_WIDGETS_TEXT_INPUT_H_

#include "widget.h"

class TextInput : public Widget {
public:
    TextInput();

    void Tick(double deltaTime) override;
    void Render(double deltaTime, UIFrameworkBatcher& batcher) const override;
    ninmath::Vector2f ComputeDesiredSize() const override;
    void OnKeyPressed(KeyEvent e) override;
    void OnInitialized() override;
    
    void SetText(std::string text);
    void SetFontSize(float fontSize);
    void SetTextColor(ninmath::Vector4f color) { SetForegroundColor(color); }
    void SetWidth(float width) { width_ = width; }
    
    
protected:
    void ComputeTextSize();
	char VirtualKeyToChar(UINT vkCode); 
    
    std::string text_;
    float fontSize_;
    float textHeight_;
    float textWidth_;
    float width_;

    float textXPosOffset_;

    uint32_t tickCount_;
    double blinkTotalTime_;
    double blinkTime_;
};


#endif // UI_WIDGETS_TEXT_INPUT_H_
