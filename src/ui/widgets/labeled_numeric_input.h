#ifndef UI_WIDGETS_LABELED_NUMERIC_INPUT_H_
#define UI_WIDGETS_LABELED_NUMERIC_INPUT_H_
#include "numeric_input.h"
#include "ui/ui_framework.h"
#include "widget.h"
#include "text.h"
#include "vertical_layout.h"


template <typename T>
class LabeledNumericInput : public Widget {
public:
    LabeledNumericInput(T& val)
        : val_(val) {}

    void Construct() override;
    
    ninmath::Vector2f ComputeDesiredSize() const override;
    void ResolveChildrenSize() override;
    void ResolveChildrenPositions() override;

    void SetNumericWidth(float val) {
        numericInput_->SetWidth(val);
    }
    void SetNumericFontSize(float val) {
        numericInput_->SetFontSize(val);
    }
    void SetLabelFontSize(float val) {
        label_->SetFontSize(val);
    }
    void SetLabelText(std::string val) {
        label_->SetText(val);
    }
    
protected:
    std::shared_ptr<VerticalLayout> container_;
    std::shared_ptr<Text> label_;
    std::shared_ptr<NumericInput<T>> numericInput_;
    
    T& val_;
};

template <typename T>
void LabeledNumericInput<T>::Construct() {
    container_ = framework_->CreateWidget<VerticalLayout>(this, "container");
    label_ = framework_->CreateWidget<Text>(this, "label");
    label_->SetText("Test Label");
    label_->SetFontSize(12);
    
    numericInput_ = framework_->CreateWidget<NumericInput<T>>(this, "numeric_input", val_);
    numericInput_->SetWidth(100);
    numericInput_->SetFontSize(18);

    container_->SetGap(4);
    container_->AddChild(label_, HorizontalAlignment::Left);
    container_->AddChild(numericInput_, HorizontalAlignment::Left);

    AddChild(container_);
}

template <typename T>
ninmath::Vector2f LabeledNumericInput<T>::ComputeDesiredSize() const {
    return label_->ComputeDesiredSize() + numericInput_->ComputeDesiredSize();
}

template <typename T>
void LabeledNumericInput<T>::ResolveChildrenSize() {
    
}

template <typename T>
void LabeledNumericInput<T>::ResolveChildrenPositions() {
    container_->SetPosition(pos_);
}

#endif // UI_WIDGETS_LABELED_NUMERIC_INPUT_H_
