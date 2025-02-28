#ifndef APPLICATION_WINDOW_H_
#define APPLICATION_WINDOW_H_

#include <functional>
#define NOMINMAX
#include "windows.h"
#include <string>
#include <vector>

struct MouseEvent {
    int posX; // relative to window pos
    int posY; // relative to window pos
    int deltaX; // delta since last time the mouse has been moved
    int deltaY;
};

enum class KeyEventType {
    Down,
    Up,
};

struct KeyEvent {
    KeyEventType type;
    WPARAM key; 
};

enum MouseButton {
    Left,
    Right,
    Middle
};

struct MouseButtonEvent {
    MouseButton btn;
};

typedef std::function<void(MouseEvent e)> MouseMoveCallback;
typedef std::function<void(KeyEvent e)> KeyDownCallback;
typedef std::function<void(KeyEvent e)> KeyUpCallback;
typedef std::function<void(MouseButtonEvent e)> MouseButtonDownCallback;
typedef std::function<void(MouseButtonEvent e)> MouseButtonUpCallback;

class Window {
public:
    Window(HINSTANCE hinst, std::string windowName);

    void Tick(double deltaTime);
    void Show();
    HWND GetHWND() const { return hwnd_; }
    std::string GetName() const { return name_; }
    bool IsAlive() const { return isAlive_; }
    void SetIsAlive(bool val) { isAlive_ = val; }
    LRESULT HandleMsg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    void AddMouseMovedCallback(MouseMoveCallback callback) { mouseMovedCallbacks_.push_back(callback); }
    void AddKeyDownCallback(KeyDownCallback callback) { keyDownCallbacks_.push_back(callback); }
    void AddKeyUpCallback(KeyUpCallback callback) { keyUpCallbacks_.push_back(callback); }
    void AddMouseButtonDownCallback(MouseButtonDownCallback callback) { mouseButtonDownCallbacks_.push_back(callback); }
    void AddMouseButtonUpCallback(MouseButtonUpCallback callback) { mouseButtonUpCallbacks_.push_back(callback); }
    
private:
    std::vector<MouseMoveCallback> mouseMovedCallbacks_; 
    std::vector<KeyDownCallback> keyDownCallbacks_; 
    std::vector<KeyUpCallback> keyUpCallbacks_; 
    std::vector<MouseButtonDownCallback> mouseButtonDownCallbacks_; 
    std::vector<MouseButtonUpCallback> mouseButtonUpCallbacks_; 

    bool isAlive_;
    std::string name_;
    HWND hwnd_;
};

#endif // APPLICATION_WINDOW_H_
