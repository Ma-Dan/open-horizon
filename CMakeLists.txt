cmake_minimum_required(VERSION 2.8)

project(open_horizon)

add_subdirectory(deps/nya-engine)
include_directories(deps/nya-engine)
include_directories(deps/pugixml-1.4/src)
include_directories(deps/miso/include)
include_directories(.)

if(APPLE)
    # platform_dialogs.cpp contains Objective-C syntax (Cocoa dialogs)
    set_source_files_properties(util/platform_dialogs.cpp PROPERTIES
        COMPILE_FLAGS "-x objective-c++")
endif()

define_source_files(demo)
define_source_files(containers)
define_source_files(game)
define_source_files(gui)
define_source_files(phys)
define_source_files(renderer)
define_source_files(sound)
define_source_files(util)
define_source_files(deps/pugixml-1.4/src)
define_source_files(deps/miso/src)
list(APPEND src_files deps/nya-engine/extensions/zip_resources_provider.cpp)

set(CMAKE_CXX_FLAGS "-std=c++0x -Wno-multichar")

add_executable(open_horizon ${src_files})

target_link_libraries(open_horizon nya_engine)

find_package(OpenGL REQUIRED)
if (NOT OPENGL_FOUND)
    message(ERROR " OpenGL not found!")
endif()
include_directories(${OpenGL_INCLUDE_DIRS})
link_directories(${OpenGL_LIBRARY_DIRS})
add_definitions(${OpenGL_DEFINITIONS})
target_link_libraries(open_horizon ${OPENGL_LIBRARIES})

if (WIN32)
    include_directories(deps/zlib-1.2.8)
    target_link_libraries(open_horizon ${CMAKE_CURRENT_SOURCE_DIR}/deps/zlib-1.2.8/zlib.lib)
else ()
    find_package(ZLIB)
    if (NOT ZLIB_FOUND)
        message(ERROR " zlib not found!")
    endif()
    include_directories(${ZLIB_INCLUDE_DIRS})
    target_link_libraries(open_horizon ${ZLIB_LIBRARIES})
endif()

if (WIN32)
    include_directories(deps/glfw-bin/include)
    target_link_libraries(open_horizon ${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw-bin/lib-msvc110/glfw3.lib)
elseif (APPLE)
    include_directories(deps/glfw-bin/include)
    target_link_libraries(open_horizon ${CMAKE_CURRENT_SOURCE_DIR}/deps/glfw-bin/lib/libglfw3.a)

    # macOS: use the bundled lua 5.2 static library (script.cpp uses luaL_newstate, 5.2 API)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/deps/lua-5.2.4/include)
    target_link_libraries(open_horizon ${CMAKE_CURRENT_SOURCE_DIR}/deps/lua-5.2.4/liblua52.a)

    # macOS: OpenAL is provided as a system framework (headers: AL/al.h).
    # Source includes "AL/al.h", framework headers are flat, so provide an AL/ shim dir.
    if(NOT CMAKE_OSX_SYSROOT)
        execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE CMAKE_OSX_SYSROOT OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    set(AL_SHIM_DIR ${CMAKE_BINARY_DIR}/openal_include/AL)
    file(MAKE_DIRECTORY ${AL_SHIM_DIR})
    file(COPY ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/OpenAL.framework/Headers/al.h DESTINATION ${AL_SHIM_DIR})
    file(COPY ${CMAKE_OSX_SYSROOT}/System/Library/Frameworks/OpenAL.framework/Headers/alc.h DESTINATION ${AL_SHIM_DIR})
    include_directories(${CMAKE_BINARY_DIR}/openal_include)
    target_link_libraries(open_horizon "-framework OpenAL")

    find_library(COCOA_LIBRARY Cocoa)
    link_directories(${COCOA_LIBRARY})
    target_link_libraries(open_horizon ${COCOA_LIBRARY})

    find_library(IOKit_LIBRARY IOKit)
    link_directories(${IOKit_LIBRARY})
    target_link_libraries(open_horizon ${IOKit_LIBRARY})
else ()
    find_package(Lua51 REQUIRED)
    include_directories(${LUA_INCLUDE_DIR})
    target_link_libraries(open_horizon lua5.1)

    target_link_libraries(open_horizon openal)

    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/deps/cmake")
    find_package(GLFW REQUIRED)
    include_directories(${GLFW_INCLUDE_DIR})
    target_link_libraries(open_horizon ${GLFW_LIBRARIES})
endif ()

add_custom_command(TARGET open_horizon POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_SOURCE_DIR}/resources/ $<TARGET_FILE_DIR:open_horizon>/)
