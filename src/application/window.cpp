#include "window.h"

#include <iostream>
#include <winrt/windows.foundation.h>
#include <windowsx.h>

LRESULT CALLBACK WindowProc2(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

Window::Window(HINSTANCE hinst, std::string windowName)
: name_(windowName) {
	const wchar_t* className = L"Default Window Class";
	
	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc2;
	wc.hInstance = hinst;
	wc.lpszClassName = LPSTR(className);
	wc.hbrBackground = 0;

	if(!GetClassInfoA(hinst, LPCSTR(className), &wc)) {
		RegisterClassA(&wc);
	}
	
	const auto winName = std::make_shared<std::string>(_strdup(windowName.c_str()));
	
	hwnd_ = CreateWindowEx(
		0,
		(LPCSTR) className,
		winName->c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		NULL,
		NULL,
		hinst,
		this
	);
	
	if(hwnd_ == NULL) {
		DWORD errorCode = GetLastError();
		
		LPSTR messageBuffer = nullptr;
		FormatMessageA(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			errorCode,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPSTR)&messageBuffer,
			0,
			NULL
		);	
		
		std::cout << "hwnd null!" << std::endl;
		std::cout << errorCode << std::endl;
		std::cout << messageBuffer << std::endl;

		LocalFree(messageBuffer);
		return;
	}

	SetIsAlive(true);
}

void Window::Tick(double deltaTime) {
	
}

void Window::Show() {
	winrt::check_pointer(hwnd_);
	ShowWindow(hwnd_, SW_SHOW);
}


LRESULT Window::HandleMsg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch(uMsg) {
	case WM_SIZE:
		{
			if(wParam == SIZE_RESTORED) {
				const int width = LOWORD(lParam);
				const int height = HIWORD(lParam);
				std::cout << "Window resized: (" << width << ", " << height << ")" << std::endl;
			}
			else {
				std::cout << "Max/min" << std::endl;
			}
		}
		break;
	case WM_KEYDOWN:
		{
			KeyEvent e;
			e.type = KeyEventType::Down;
			e.key = wParam;

			std::cout << wParam << std::endl;
			
			for(const auto& func : keyDownCallbacks_) {
				func(e);
			}
		}
		break;
	case WM_KEYUP:
		{
			KeyEvent e;
			e.type = KeyEventType::Up;
			e.key = wParam;
			
			for(const auto& func : keyUpCallbacks_) {
				func(e);
			}
		}
		break;
	case WM_MOUSEMOVE:
		{
			MouseEvent e;
			e.posX = GET_X_LPARAM(lParam);
			e.posY = GET_Y_LPARAM(lParam);
			// TODO
			e.deltaX = 0;
			e.deltaY = 0;

			for(const auto& func : mouseMovedCallbacks_) {
				func(e);
			}
			
		}
		break;
	case WM_LBUTTONDOWN:
		{
			MouseButtonEvent e;
			e.btn = MouseButton::Left;
			
			for(const auto& func : mouseButtonDownCallbacks_) {
				func(e);
			}
		}
		break;
	case WM_LBUTTONUP:
		{
			MouseButtonEvent e;
			e.btn = MouseButton::Left;
			
			for(const auto& func : mouseButtonUpCallbacks_) {
				func(e);
			}
		}
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		std::cout << "qutting window! " << GetName() << std::endl;
		SetIsAlive(false);
		return 0;
	default:
		break;
	}
	
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK WindowProc2(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch(uMsg) {
	case WM_CREATE:
	case WM_NCCREATE:
		{
			CREATESTRUCT* cstruct = (CREATESTRUCT*) lParam;
			Window* window = (Window*) cstruct->lpCreateParams;
			std::cout << "creating window! " << window->GetName() << std::endl;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)window);
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}
	default:
		break;
	}

	LONG_PTR ptr = GetWindowLongPtr(hwnd, GWLP_USERDATA);
	Window *window= reinterpret_cast<Window*>(ptr);
	
	if(window) {
		return window->HandleMsg(hwnd, uMsg, wParam, lParam);
	}

	// default message handling
	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
