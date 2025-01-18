#ifndef APPLICATION_APPLICATION_H_
#define APPLICATION_APPLICATION_H_

#include <windows.h>
#include <winrt/windows.foundation.h>
#include <vector>

class Window;

struct ApplicationParams {
    ApplicationParams() = default;
    ApplicationParams(std::string appName)
        : appName(appName) {}
    
    std::string appName;

};

class Application {
public:
    Application(HINSTANCE hinst, const ApplicationParams& params);

    void StartMainLoop();
    bool AppTick();
    virtual void Tick(double deltaTime) {};

    std::shared_ptr<Window> CreateAppWindow(std::string windowName);

    virtual ~Application();

protected:
    // Default behavior:
    //      - return on success
    //      - print and assert on error
    void HandleHRESULT(HRESULT hr);
    
    std::string appName_;
    HINSTANCE hinst_;
    std::vector<std::shared_ptr<Window>> activeWindows_;
};

#endif // APPLICATION_APPLICATION_H_
