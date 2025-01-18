#ifndef RENDERER_ROOT_CONSTANT_VALUE_H_
#define RENDERER_ROOT_CONSTANT_VALUE_H_

#include <functional>
#include <vector>

typedef std::weak_ptr<bool> RootConstantValueHandle;
typedef std::function<void()> RootConstantListenerFunction;
typedef std::function<void(RootConstantListenerFunction)> RootConstantAddListenerFunction;

// Ended up not needing the callback functionality,
// since SetGraphicsRoot32BitConstants(...) needs to be called
// per frame anyway, but leaving in here, just in case.
template <typename T>
class RootConstantValue {
public:
    RootConstantValue() = default;
    RootConstantValue(T value)
        : value_(value), handle_(std::make_shared<bool>(true))
    {}

    void* GetData() { return static_cast<void*>(&value_); }
    uint32_t GetSizeIn32BitValues() const { return sizeof(T) / 4; }
    
    const T& GetValue() const { return value_; }

    void SetValue(const T& newVal) {
        value_ = newVal;
        for(const auto& func : setValueCallbacks_) {
            func();
        }
    }


    void AddListener(RootConstantListenerFunction newFunc) {
        setValueCallbacks_.push_back(newFunc);
    }

    RootConstantAddListenerFunction GetAddListenerFunc() const {
        return std::bind(&RootConstantValue<T>::AddListener, this);
    }

    RootConstantValueHandle GetHandle() const { return handle_; }

private:
    std::vector<std::function<void()>> setValueCallbacks_;
    
    // for listeners to check if this object is still alive - without actually storing this object
    RootConstantValueHandle handle_;
    
    T value_;
};

#endif // RENDERER_ROOT_CONSTANT_VALUE_H_
