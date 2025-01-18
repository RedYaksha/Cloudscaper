#include "application.h"
#include "window.h"
#include <winrt/windows.foundation.h>
#include <iostream>
#include "comdef.h"

Application::Application(HINSTANCE hinst, const ApplicationParams& params)
: hinst_(hinst) {
	appName_ = params.appName;
	
	winrt::init_apartment();
	
	// TODO: multiple window support
	// multi window and rendering to each of them may need careful planning
	// esp. thinking about a global "3D world" and maybe each window
	// has a different role, or maybe even its own separate world
}

Application::~Application() {
	winrt::uninit_apartment();
}

void Application::HandleHRESULT(HRESULT hr) {
	// default
	if(SUCCEEDED(hr)) {
		return;
	}

	_com_error err(hr);
	LPCTSTR errMsg = err.ErrorMessage();

	std::cerr << errMsg << std::endl;
	assert(false);
}

void Application::StartMainLoop() {
	
	// simple window management, single-threaded
	while(true) {
		if(!AppTick())
			break;

		for(auto& win : activeWindows_) {
			win->Tick(0.);
		}

		Tick(0.);
	}
}

bool Application::AppTick() {
	// remove dead windows
	std::erase_if(activeWindows_,
		[](std::shared_ptr<Window> w )->bool {
				return !w->IsAlive();
			});

	// break if no more windows to process
	if(activeWindows_.size() <= 0) {
		return false;
	}
	
	MSG msg;
	while(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return true;
}

std::shared_ptr<Window> Application::CreateAppWindow(std::string windowName) {
	assert(activeWindows_.size() == 0 && "Multi-window setups not supported.");
	
	std::shared_ptr<Window> window = std::make_shared<Window>(hinst_, windowName);
	activeWindows_.push_back(window);
	return window;
}


