#ifndef RENDERER_UI_WIDGETS_VERTICAL_LAYOUT_H_
#define RENDERER_UI_WIDGETS_VERTICAL_LAYOUT_H_
#include <unordered_map>

#include "layout.h"

enum class HorizontalAlignment {
    Left,
    Center,
    Right,
    Fill,
    NumAlignments
};

class VerticalLayout : public Widget {
public:
    VerticalLayout()
        : gap_(0) {}

    void AddChild(std::shared_ptr<Widget> widget, HorizontalAlignment alignment);
    void ResolveChildrenPositions() override;
    void ResolveChildrenSize() override;
    ninmath::Vector2f ComputeDesiredSize() const override;
    void Render(double deltaTime, UIFrameworkBatcher& batcher) const override;
    void SetGap(float val) { gap_ = val; }

private:
    std::unordered_map<WidgetID, HorizontalAlignment> widgetAlignmentMap_;
    float gap_;
    
};

#endif // RENDERER_UI_WIDGETS_VERTICAL_LAYOUT_H_
