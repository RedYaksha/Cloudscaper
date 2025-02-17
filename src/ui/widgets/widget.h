#ifndef RENDERER_UI_WIDGET_H_
#define RENDERER_UI_WIDGET_H_

#include <assert.h>
#include <vector>
#include <memory>
#include <string>

#include "ninmath/ninmath.h"

class UIFrameworkBatcher;

typedef std::string WidgetID;

class Widget {
public:
    virtual ~Widget() = default;
    Widget()
    :
    parent_(nullptr),
    pos_({0,0}),
    size_({0,0}),
    padding_({0,0,0,0}),
    margin_({0,0,0,0}),
    isHovered_(false),
    isPressed_(false)
    {}

    virtual void Render(double deltaTime, UIFrameworkBatcher& batcher) const = 0;
    virtual ninmath::Vector2f ComputeDesiredSize() const = 0;
    virtual ninmath::Vector2f ComputeAndCacheDesiredSize() {
        size_ = ComputeDesiredSize();
        return size_;
    }
    
    virtual void ResolveChildrenPositions() {}
    virtual void ResolveChildrenSize() {}
    void SetPosition(ninmath::Vector2f pos) { pos_ = pos; }
    void SetSize(ninmath::Vector2f size) { size_ = size; }
    WidgetID GetID() const { return id_; }
    void SetParent(Widget* widget) { parent_ = widget; }
    Widget* GetParent() const { return parent_; }
    bool HasChildren() const { return children_.size() > 0; }
    uint32_t GetNumChildren() const { return (uint32_t) children_.size(); }
    const std::vector<std::shared_ptr<Widget>>& GetChildren() const { return children_; }

    virtual ninmath::Vector2f GetHitboxPosition() const {
        ninmath::Vector2f pos;
        pos.x = pos_.x + margin_.l;
        pos.y = pos_.y + margin_.t;
        return pos;
    }
    
    virtual ninmath::Vector2f GetHitboxSize() const {
        ninmath::Vector2f size;
        size.x = size_.x - margin_.l - margin_.r;
        size.y = size_.y - margin_.t - margin_.b;
        return size;
    }

    virtual void OnMouseEnter() {}
    virtual void OnMouseLeave() {}
    virtual void OnPressed() {}

    void SetMargin(ninmath::Vector4f val) { margin_ = val; }
    void SetMargin(ninmath::Vector2f val) { margin_ = {val.x, val.x, val.y, val.y}; }
    void SetPadding(ninmath::Vector4f val) { padding_ = val; }
    void SetPadding(ninmath::Vector2f val) { padding_ = {val.x, val.x, val.y, val.y}; }
    void SetBackgroundColor(ninmath::Vector4f val) { backgroundColor_ = val; }

    bool IsHovered() const { return isHovered_; }
    void SetIsHovered(bool val) { isHovered_ = val; }
    void SetID(WidgetID id) { id_ = id; }
    bool IsPressed() const { return isPressed_; }
    void SetIsPressed(bool val) { isPressed_ = val; }
    
protected:
    WidgetID id_;
    std::vector<std::shared_ptr<Widget>> children_;
    Widget* parent_;

    ninmath::Vector2f pos_;
    ninmath::Vector2f size_;
    
    ninmath::Vector4f padding_;
    ninmath::Vector4f margin_;

    ninmath::Vector4f backgroundColor_;
    ninmath::Vector4f foregroundColor_;

    bool isHovered_;
    bool isPressed_;
};

#endif // RENDERER_UI_WIDGET_H_
