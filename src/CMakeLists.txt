cmake_minimum_required(VERSION 3.24.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (CMAKE_CXX_STANDARD 20)

set( SOURCE_FILES 
# renderer
    renderer/renderer.cpp
    renderer/pipeline_state.cpp
    renderer/resources.cpp
    renderer/shader.cpp
    renderer/shader_compiler.cpp
    renderer/pipeline_assembler.cpp
    
    renderer/memory/descriptor_allocator.cpp
    renderer/memory/static_descriptor_allocator.cpp
    renderer/memory/memory_allocator.cpp
    renderer/memory/static_memory_allocator.cpp
    
    renderer/multithreading/thread_pool.cpp
    
# application
    application/application.cpp
    application/window.cpp
    
# cloudscaper
    cloudscaper.cpp

    main.cpp
)

set( HEADER_FILES 
# renderer
    renderer/renderer.h
    renderer/pipeline_state.h
    renderer/resources.h
    renderer/shader.h
    renderer/shader_compiler.h
    renderer/pipeline_assembler.h
    
    renderer/memory/descriptor_allocator.h
    renderer/memory/static_descriptor_allocator.h
    renderer/memory/memory_allocator.h
    renderer/memory/static_memory_allocator.h
    
    renderer/multithreading/thread_pool.h
    
    renderer/renderer_types.h
    renderer/shader_types.h
    
    renderer/root_constant_value.h
    
# application
    application/application.h
    application/window.h
    
# cloudscaper
    cloudscaper.h
)

set( SHADER_FILES 
    shaders/vertex_shader.hlsl
    shaders/pixel_shader.hlsl
)

set_source_files_properties(${SHADER_FILES} PROPERTIES LANGUAGE HLSL)
source_group("Sources" FILES ${SOURCE_FILES})
source_group("Headers" FILES ${HEADER_FILES})
source_group("Shaders" FILES ${SHADER_FILES})

add_executable(${PROJECT_NAME} WIN32 ${SOURCE_FILES} ${HEADER_FILES} ${SHADER_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE 
                             ${CMAKE_CURRENT_SOURCE_DIR}
                             ${CMAKE_CURRENT_SOURCE_DIR}/renderer
                             ${CMAKE_CURRENT_SOURCE_DIR}/application
                             ${THIRD_PARTY_SOURCE_DIR}/DirectX-Headers/include
                           )

target_link_libraries(${PROJECT_NAME} PRIVATE winmm.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Setupapi.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Version.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Imm32.lib)

# C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64
target_link_libraries(${PROJECT_NAME} PRIVATE DXGI.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE d3d12.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE d3dcompiler.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE dxguid.lib)
target_link_libraries(${PROJECT_NAME} PRIVATE dxcompiler.lib)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/assets
		VERBATIM
        )
        
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/shaders ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/shaders
		VERBATIM
        )
        
set( DX_DLL_HINT C:\\Program Files (x86)\\Windows Kits\\10\\bin\\${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}\\x64 )
        
find_file(DXC_DLL dxcompiler.dll HINTS ${DX_DLL_HINT} NO_CACHE)
find_file(DXIL_DLL dxil.dll HINTS ${DX_DLL_HINT} NO_CACHE)
    
message(DXC_DLL=${DXC_DLL})      
        
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${DXC_DLL} ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/
		VERBATIM
        )
        
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${DXIL_DLL} ${CMAKE_BINARY_DIR}/bin/$<CONFIG>/
		VERBATIM
        )

set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
