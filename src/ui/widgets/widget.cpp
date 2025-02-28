#include "widget.h"
#include "assert.h"

void Widget::AddChild(std::shared_ptr<Widget> widget) {
    assert(widget);

    children_.push_back(widget);
    widget->SetParent(this);
}
