#ifndef UI_WIDGETS_TEXT_H_
#define UI_WIDGETS_TEXT_H_

#include <optional>

#include "widget.h"

class Text : public Widget {
public:
    Text();
    
    void Render(double deltaTime, UIFrameworkBatcher& batcher) const override;
    ninmath::Vector2f ComputeDesiredSize() const override;
    void SetText(std::string text);
    void SetFontSize(float fontSize);
    void SetTextColor(ninmath::Vector4f color) { SetForegroundColor(color); }

    
private:
    void ComputeTextSize();
    
    std::optional<std::string> text_;
    float fontSize_;
    float textHeight_;
    float textWidth_;
};

#endif // UI_WIDGETS_TEXT_H_
