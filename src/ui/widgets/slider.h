#ifndef UI_WIDGETS_SLIDER_H_
#define UI_WIDGETS_SLIDER_H_
#include <cassert>
#include <iostream>

#include "widget.h"
#include "ui/ui_framework.h"

template <typename T>
class Slider : public Widget {
public:
    Slider(T& val)
        : val_(val), length_(100), width_(5), handleHeight_(10), handleDown_(false) {
        minVal_ = val;
        maxVal_ = val;
    }

    void Render(double deltaTime, UIFrameworkBatcher& batcher) const override;

    void OnMouseMoved(const MouseEvent& e) override;
    void OnPressed(const MouseButtonEvent& e) override;
    void OnReleased(const MouseButtonEvent& e) override;
    
    void SetRange(T minVal, T maxVal);

    void SetLength(float val) { length_ = val; }
    void SetWidth(float val) { width_ = val; }
    void SetHandleHeight(float val) { handleHeight_ = val; }
    
    ninmath::Vector2f ComputeDesiredSize() const override;

private:

    T minVal_;
    T maxVal_;
    
    float length_;
    float width_;
    float handleHeight_;

    bool handleDown_;
    
    T& val_;
};

template <typename T>
void Slider<T>::Render(double deltaTime, UIFrameworkBatcher& batcher) const {
    const float px = pos_.x + margin_.l + padding_.l;
    float py = pos_.y + margin_.t + padding_.t;

    float alpha = (val_ - minVal_) / (maxVal_ - minVal_);
    if(minVal_ == maxVal_) {
        alpha = 0.5;
    }

    const float barYOffset = (handleHeight_ - width_) / 2.f;

    // background
    batcher.AddQuad(ninmath::Vector2f {px, py + barYOffset},
                    ninmath::Vector2f {length_, width_},
                    backgroundColor_
                    );

    // progress
    batcher.AddQuad(ninmath::Vector2f {px, py + barYOffset},
                    ninmath::Vector2f {alpha * length_, width_},
                    foregroundColor_
                    );

    // handle (circle)

    float handleX = (std::min)(px + alpha * length_, px + length_ - handleHeight_);
    batcher.AddQuad(ninmath::Vector2f {handleX, py},
                    ninmath::Vector2f {handleHeight_, handleHeight_},
                    foregroundColor_
                    );
}

template <typename T>
void Slider<T>::OnMouseMoved(const MouseEvent& e) {
    if(!handleDown_) {
        return;
    }
    
    const float startPx = pos_.x + margin_.l + padding_.l;
    const float endPx = startPx + length_;

    float newAlpha = (e.posX - startPx) / (endPx - startPx);
    newAlpha = (std::min)(newAlpha, 1.0f);
    newAlpha = (std::max)(newAlpha, 0.f);

    val_ = newAlpha * (maxVal_ - minVal_);
}

template <typename T>
void Slider<T>::OnPressed(const MouseButtonEvent& e) {
    // if hit handle,
    float alpha = (val_ - minVal_) / (maxVal_ - minVal_);
    const float px = pos_.x + margin_.l + padding_.l;
    float py = pos_.y + margin_.t + padding_.t;
    
    float handleX = (std::min)(px + alpha * length_, px + length_ - handleHeight_);
    const ninmath::Vector2f hitboxPos = ninmath::Vector2f {handleX, py};
    const ninmath::Vector2f hitboxSize = ninmath::Vector2f {handleHeight_, handleHeight_};
    const ninmath::Vector2f mousePos = ninmath::Vector2f {(float) e.posX, (float) e.posY};

    const bool mouseIsInHitbox = ninmath::IsPointInAxisAlignedRect(mousePos, hitboxPos, hitboxSize);

    if(mouseIsInHitbox) {
        handleDown_ = true;
        std::cout << "hit handle!" << std::endl;
        return;
    }
    
}

template <typename T>
void Slider<T>::OnReleased(const MouseButtonEvent& e) {
    if(!handleDown_) {
        return;
    }
    
    std::cout << "handle release!" << std::endl;
    handleDown_ = false;

}

template <typename T>
void Slider<T>::SetRange(T minVal, T maxVal) {
    assert(minVal < maxVal);
    minVal_ = minVal;
    maxVal_ = maxVal;

    val_ = (std::min)(val_, maxVal_);
    val_ = (std::max)(val_, minVal_);
}

template <typename T>
ninmath::Vector2f Slider<T>::ComputeDesiredSize() const {
    return ninmath::Vector2f {
        margin_.l + margin_.r + padding_.l + padding_.r + length_,
        margin_.t + margin_.b + padding_.t + padding_.b + (std::max)(width_, handleHeight_),
    };
}

#endif // UI_WIDGETS_SLIDER_H_
