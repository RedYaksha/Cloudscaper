#include "ui_framework.h"

#include <iostream>
#include <queue>

#include "pipeline_builder.h"
#include "pipeline_state.h"
#include "quad_primitive_renderer.h"
#include "renderer.h"
#include "memory/memory_allocator.h"
#include "widgets/button.h"
#include "widgets/vertical_layout.h"
#include "application/window.h"

UIFramework::UIFramework(std::shared_ptr<Renderer> renderer,
                         std::shared_ptr<MemoryAllocator> memAllocator,
                         std::shared_ptr<Window> window)
    : renderer_(renderer),
      memAllocator_(memAllocator),
      window_(window) {

    window_->AddMouseMovedCallback(std::bind(&UIFramework::OnMouseMoved, this, std::placeholders::_1));
    window_->AddKeyDownCallback(std::bind(&UIFramework::OnKeyDown, this, std::placeholders::_1));
    window_->AddKeyUpCallback(std::bind(&UIFramework::OnKeyUp, this, std::placeholders::_1));
    window_->AddMouseButtonDownCallback(std::bind(&UIFramework::OnMouseButtonDown, this, std::placeholders::_1));
    window_->AddMouseButtonUpCallback(std::bind(&UIFramework::OnMouseButtonUp, this, std::placeholders::_1));
    
    quadRenderer_ = std::make_shared<QuadPrimitiveRenderer>(renderer_, memAllocator_);
    
    button0_ = CreateWidget<Button>("button0");
    button0_->SetBackgroundColor({1,0,0,1});
    button0_->SetHoverColor({0.7,0,0,1});
    button0_->SetPressedColor({0.3,0,0,1});
    
    button1_ = CreateWidget<Button>("button1");
    button1_->SetPadding({50, 5});
    button1_->SetMargin({10, 10});
    button1_->SetBackgroundColor({0,1,0,1});
    button1_->SetHoverColor({0,0.7,0,1});
    button1_->SetPressedColor({0,0.3,0,1});
    
    button2_ = CreateWidget<Button>("button2");
    button2_->SetBackgroundColor({0,0,1,1});
    button2_->SetHoverColor({0,0,0.7,1});
    button2_->SetPressedColor({0,0,0.3,1});

    rootWidget_ = CreateWidget<VerticalLayout>("Root Widget");
    rootWidget_->SetGap(20);

    rootWidget_->AddChild(button0_, HorizontalAlignment::Right);
    rootWidget_->AddChild(button1_, HorizontalAlignment::Center);
    rootWidget_->AddChild(button2_, HorizontalAlignment::Center);
}

void UIFramework::Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    // batch all primitives using Widget::Renderer and traversing down the tree from root - do it twice?
    UIFrameworkBatcher batcher;
    rootWidget_->Render(deltaTime, batcher);

    /*

        uiFramework->CreateWidget<Button>("")
        .MarginPc()
        .Padding()
        .Text("Click me")
        .OnHover()
        .OnClick()
        .Create();

        btn->SetMargin()
        btn->SetPadding()
        btn->Set


        uiFramework->CreateWidget<VerticalLayout>("")
        .Margin()
        .Gap(50)
        .

        
        
        from root -> leaf

        CalculateSize(curNode):
             - goal 1: figure out my size
                 - if directly computed,
                    - return size with the computation (e.g. adding padding, margin, etc)
                 - if depends on children,
                    - for each child: CalculateSize(child)
                    - compute my size, given all childrens' size

        ResolvePosition(curNode) (only done by layouts or widgets that have children):
            - goal: figure out my position 
                - if directly computed (e.g. root is at (0,0))
                    - return computation
            - goal: set children's start position


        Iterate tree from bottom.
            if leaf, ComputeDesiredSize()
            if layout,
                -> ComputeDesiredSize() => ignore align fill
                -> ResolveChildrenSize() => AlignFill children will be size_.x

        Iterate tree from top.
            if layout, ResolveChildrenPosition()
    */
    
    std::set<WidgetID> seen;
    std::queue<Widget*> queue;
    std::queue<Widget*> leafQueue;
    std::unordered_map<WidgetID, uint32_t> parentCountMap;

    // extract all leaves
    queue.push(rootWidget_.get());
    while(!queue.empty()) {
        Widget* curNode = queue.front();
        queue.pop();

        if(curNode->GetParent() != nullptr) {
            parentCountMap[curNode->GetParent()->GetID()] += 1;
        }

        if(!curNode->HasChildren()) {
            leafQueue.push(curNode);
        }
        else {
            const std::vector<std::shared_ptr<Widget>>& children = curNode->GetChildren();
            for(int i = 0; i < children.size(); i++) {
                queue.push(children[i].get());
            }
        }
    }

    //
    // Iterate tree from bottom.
    // if leaf, ComputeDesiredSize()
    // if layout,
    //     -> ComputeDesiredSize() => ignore align fill
    //     -> ResolveChildrenSize() => AlignFill children will be size_.x
    //
    while(!leafQueue.empty()) {
        Widget* curNode = leafQueue.front();
        leafQueue.pop();

        // if leaf
        if(!curNode->HasChildren()) {
            curNode->ComputeAndCacheDesiredSize();
        }
        else {
            curNode->ComputeAndCacheDesiredSize();
            curNode->ResolveChildrenSize();
        }

        if(curNode->GetParent() == nullptr) {
            continue;
        }

        const WidgetID parentId = curNode->GetParent()->GetID();

        assert(parentCountMap.at(parentId) > 0);
        parentCountMap[parentId] -= 1;
        
        if(parentCountMap.at(parentId) == 0) {
            leafQueue.push(curNode->GetParent());
        }
    }
    
    // Iterate tree from top.
    //    if layout, ResolveChildrenPosition()
    queue = std::queue<Widget*>();
    queue.push(rootWidget_.get());
    while(!queue.empty()) {
        Widget* curNode = queue.front();
        queue.pop();

        if(curNode->HasChildren()) {
            curNode->ResolveChildrenPositions();
        }

        auto children = curNode->GetChildren();
        for(auto& child : children) {
            queue.push(child.get());
        }
    }
    
    queue = std::queue<Widget*>();
    queue.push(rootWidget_.get());
    while(!queue.empty()) {
        Widget* curNode = queue.front();
        queue.pop();

        curNode->Render(deltaTime, batcher);

        auto children = curNode->GetChildren();
        for(auto& child : children) {
            queue.push(child.get());
        }
    }   
    
    // from batched primitives, render QuadPrimitiveRenderer...
    // depending on size changes... the native buffer resources may change... nonetheless, the data will be synced to the GPU buffer
    quadRenderer_->SetQuads(batcher.GetQuads());
    
    //
    quadRenderer_->Render(deltaTime, cmdList);
}

void UIFramework::Tick(double deltaTime) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);

    //
    for(auto& [id, widget] : widgetMap_) {

        // MouseLeave, MouseEnter
        if(curFrameEvents_.mouseEvent.has_value()) {
            MouseEvent& e = curFrameEvents_.mouseEvent.value();

            const ninmath::Vector2f hitboxPos = widget->GetHitboxPosition();
            const ninmath::Vector2f hitboxSize = widget->GetHitboxSize();
            const ninmath::Vector2f mousePos = ninmath::Vector2f {(float) e.posX, (float) e.posY};
            const bool mouseIsInHitbox = ninmath::IsPointInAxisAlignedRect(mousePos, hitboxPos, hitboxSize);
            const bool widgetIsHovered = widget->IsHovered();
            
            if(mouseIsInHitbox && !widgetIsHovered) {
                widget->SetIsHovered(true);
                widget->OnMouseEnter();
            }
            else if(!mouseIsInHitbox && widgetIsHovered) {
                widget->SetIsHovered(false);
                widget->OnMouseLeave();
            }
        }

        if(curFrameEvents_.mouseButtonDownEvent.has_value()) {
            const MouseButtonEvent& e = curFrameEvents_.mouseButtonDownEvent.value();
            std::cout << "down" << std::endl;

            if(e.btn == MouseButton::Left) {
                if(widget->IsHovered()) {
                    widget->OnPressed();
                    widget->SetIsPressed(true);
                    std::cout << "pressed" << std::endl;
                }
            }
        }
        if(curFrameEvents_.mouseButtonUpEvent.has_value()) {
            const MouseButtonEvent& e = curFrameEvents_.mouseButtonUpEvent.value();
            std::cout << "up" << std::endl;

            if(e.btn == MouseButton::Left) {
                if(widget->IsPressed()) {
                    widget->SetIsPressed(false);
                    std::cout << "release" << std::endl;

                    // TODO: if mouse is still in the widget's hitbox, click()
                    // later... clicking focuses the top-most widget
                    
                }
            }
        }
    }
    

    curFrameEvents_.Reset();
}

void UIFramework::OnMouseMoved(MouseEvent e) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.mouseEvent = e;
    //std::cout << "Mouse pos: " << e.posX << ", " << e.posY << std::endl;
}

void UIFramework::OnKeyDown(KeyEvent e) {
    
}

void UIFramework::OnKeyUp(KeyEvent e) {
    
}

void UIFramework::OnMouseButtonDown(MouseButtonEvent e) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.mouseButtonDownEvent = e;
}

void UIFramework::OnMouseButtonUp(MouseButtonEvent e) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.mouseButtonUpEvent = e;
}
