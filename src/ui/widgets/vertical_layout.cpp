#include "vertical_layout.h"

void VerticalLayout::AddChild(std::shared_ptr<Widget> widget, HorizontalAlignment alignment) {
    assert(widget);
    
    children_.push_back(widget);
    widgetAlignmentMap_.insert({widget->GetID(), alignment});
    
    widget->SetParent(this);
}

void VerticalLayout::ResolveChildrenPositions() {
    ninmath::Vector2f curPos = pos_;

    float contentSizeX = size_.x - margin_.l - margin_.r;
    
    for(int i = 0 ; i < children_.size(); i++) {
        std::shared_ptr<Widget>& child = children_[i];

        ninmath::Vector2f childSize = child->ComputeDesiredSize();
        ninmath::Vector2f childPos = curPos;
        
        switch(widgetAlignmentMap_.at(child->GetID())) {
        case HorizontalAlignment::Right:
            childPos.x += contentSizeX - childSize.x;
            break;
        case HorizontalAlignment::Center:
            childPos.x += (contentSizeX / 2.f) - (childSize.x / 2.f);
            break;
        case HorizontalAlignment::Left:
        default:
            break;
        }

        
        child->SetPosition(childPos);

        curPos.y += childSize.y + gap_;
    }
}

void VerticalLayout::ResolveChildrenSize() {
    for(int i = 0 ; i < children_.size(); i++) {
        std::shared_ptr<Widget>& child = children_[i];
        if(widgetAlignmentMap_.at(child->GetID()) == HorizontalAlignment::Fill) {
            ninmath::Vector2f childSize = child->ComputeDesiredSize();
            child->SetSize({size_.x, childSize.y});
        }
    }
}

ninmath::Vector2f VerticalLayout::ComputeDesiredSize() const {
    float sizeY = 0;
    float sizeX = std::numeric_limits<float>::min();
    
    for(int i = 0 ; i < children_.size(); i++) {
        const std::shared_ptr<Widget>& child = children_[i];
        ninmath::Vector2f childSize = child->ComputeDesiredSize();
        
        sizeY += childSize.y;
        if(widgetAlignmentMap_.at(child->GetID()) != HorizontalAlignment::Fill) {
            sizeX = std::max(sizeX, childSize.x);
        }
    }

    const float gapSpace = static_cast<float>(GetNumChildren() - 1) * gap_;

    return ninmath::Vector2f { sizeX, sizeY + gapSpace};
}

void VerticalLayout::Render(double deltaTime, UIFrameworkBatcher& batcher) const {
    
}
