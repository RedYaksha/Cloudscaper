#ifndef APPLICATION_WINDOW_H_
#define APPLICATION_WINDOW_H_

#include <memory>
#include "windows.h"
#include <string>



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
    
private:

    bool isAlive_;
    std::string name_;
    HWND hwnd_;
};

#endif // APPLICATION_WINDOW_H_
