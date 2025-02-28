#include "ui_framework.h"

#include <iostream>
#include <queue>

#include "pipeline_builder.h"
#include "pipeline_state.h"
#include "primitive_renderers/quad_primitive_renderer.h"
#include "renderer.h"
#include "primitive_renderers/text_primitive_renderer.h"
#include "memory/memory_allocator.h"
#include "widgets/button.h"
#include "widgets/vertical_layout.h"
#include "application/window.h"
#include "primitive_renderers/rounded_rect_primitive_renderer.h"

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

    fontManager_ = std::make_shared<FontManager>();
    
    // default font
    const std::string fontAtlasSrc = "assets/fonts/Montserrat/sdf_atlas_montserrat_regular.png";
    RegisterFont("Montserrat_Regular",
                "assets/fonts/Montserrat/montserrat_regular.arfont",
                fontAtlasSrc);
    
    quadRenderer_ = std::make_shared<QuadPrimitiveRenderer>(renderer_, memAllocator_);
    textRenderer_ = std::make_shared<TextRectPrimitiveRenderer>(renderer_, memAllocator_, fontAtlasImageResourceMap_.at(fontAtlasSrc));
    roundedRectPrimitiveRenderer_ = std::make_shared<RoundedRectPrimitiveRenderer>(renderer_, memAllocator_);
}

bool UIFramework::RegisterFont(FontID id, std::string arfontPath, std::string atlasImagePath) {
    bool success = fontManager_->RegisterFont(id, arfontPath, atlasImagePath);
    
    if(!success) {
        return false;
    }

    const ResourceID resId = atlasImagePath; // just use the image path as id
    std::weak_ptr<Resource> res = memAllocator_->CreateResource<ImageTexture2D>(resId, atlasImagePath);
    
    if(res.expired()) {
        return false;
    }

    fontAtlasImageResourceMap_.insert({atlasImagePath, res});

    return true;
}

void UIFramework::Render(double deltaTime, winrt::com_ptr<ID3D12GraphicsCommandList> cmdList) {
    // batch all primitives using Widget::Renderer and traversing down the tree from root - do it twice?
    UIFrameworkBatcher batcher(fontManager_);
    rootWidget_->Render(deltaTime, batcher);

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
    // if layout (assume its children have already been processed),
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
    textRenderer_->SetTextRects(batcher.GetTextRects());
    roundedRectPrimitiveRenderer_->SetRoundedRects(batcher.GetRoundedRects());
    
    //
    quadRenderer_->Render(deltaTime, cmdList);
    roundedRectPrimitiveRenderer_->Render(deltaTime, cmdList);
    
    textRenderer_->Render(deltaTime, cmdList);
}

void UIFramework::Tick(double deltaTime) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    
    std::queue<Widget*> queue = std::queue<Widget*>();
    queue.push(rootWidget_.get());
    while(!queue.empty()) {
        Widget* curNode = queue.front();
        queue.pop();

        curNode->Tick(deltaTime);

        auto children = curNode->GetChildren();
        for(auto& child : children) {
            queue.push(child.get());
        }
    }   

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

            mostRecentMousePos_.x = e.posX;
            mostRecentMousePos_.y = e.posY;
        }

        if(curFrameEvents_.mouseButtonDownEvent.has_value()) {
            const MouseButtonEvent& e = curFrameEvents_.mouseButtonDownEvent.value();

            if(e.btn == MouseButton::Left) {
                if(widget->IsHovered()) {
                    widget->OnPressed();
                    widget->SetIsPressed(true);

                    if(widget->IsFocusable() && !widget->IsFocused()) {
                        widget->SetIsFocused(true);
                        widget->OnFocused();
                    }
                }
                else {
                    if(widget->IsFocusable() && widget->IsFocused()) {
                        widget->OnUnfocused();
                        widget->SetIsFocused(false);
                    }
                    
                }
            }
        }
        if(curFrameEvents_.mouseButtonUpEvent.has_value()) {
            const MouseButtonEvent& e = curFrameEvents_.mouseButtonUpEvent.value();

            if(e.btn == MouseButton::Left) {
                if(widget->IsPressed()) {
                    widget->SetIsPressed(false);
                    
                    // TODO: if mouse is still in the widget's hitbox, click()
                    // later... clicking focuses the top-most widget
                    // depends how we want to handle clicks
                    /*
                    const ninmath::Vector2f hitboxPos = widget->GetHitboxPosition();
                    const ninmath::Vector2f hitboxSize = widget->GetHitboxSize();
                    const ninmath::Vector2f mousePos = ninmath::Vector2f {(float) mostRecentMousePos_.x, (float) mostRecentMousePos_.y};
                    const bool mouseIsInHitbox = ninmath::IsPointInAxisAlignedRect(mousePos, hitboxPos, hitboxSize);
                    */
                }
            }
        }
        
        if(curFrameEvents_.keyDownEvent.has_value()) {
            const KeyEvent& e = curFrameEvents_.keyDownEvent.value();
            widget->OnKeyPressed(e);
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
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.keyDownEvent = e;
}

void UIFramework::OnKeyUp(KeyEvent e) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.keyUpEvent = e;
}

void UIFramework::OnMouseButtonDown(MouseButtonEvent e) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.mouseButtonDownEvent = e;
}

void UIFramework::OnMouseButtonUp(MouseButtonEvent e) {
    std::lock_guard<std::mutex> lockGuard(curFrameEventsMutex_);
    curFrameEvents_.mouseButtonUpEvent = e;
}
