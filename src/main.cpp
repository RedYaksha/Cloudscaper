#include <d3d12.h>
#include <dxgi1_6.h>

#include <windows.h>
#include <iostream>
#include <ShObjIdl.h>
#include <winrt/windows.foundation.h>


#include <stdio.h>

#include "cloudscaper.h"
#include "application/application.h"
#include "application/window.h"
#include "renderer/renderer.h"

#ifndef APP_D3D_MININMUM_FEATURE_LEVEL
#define APP_D3D_MINIMUM_FEATURE_LEVEL D3D_FEATURE_LEVEL_12_0
#endif

FILE *g_ic_file_cout_stream; FILE *g_ic_file_cin_stream;
// Success: true , Failure: false
bool InitConsole() 
{
    if (!AllocConsole()) { return false; }
    if (freopen_s(&g_ic_file_cout_stream, "CONOUT$", "w", stdout) != 0) { return false; } // For std::cout 
    if (freopen_s(&g_ic_file_cin_stream, "CONIN$", "w+", stdin) != 0) { return false; } // For std::cin
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
	
	std::shared_ptr<Application> app = std::make_shared<Cloudscaper>(hInstance);
	app->StartMainLoop();
	
	return 0;
}