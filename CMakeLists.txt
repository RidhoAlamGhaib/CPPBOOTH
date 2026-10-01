cmake_minimum_required(VERSION 3.20)
project(IDPhotoBooth VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ══════════════════════════════════════════════
#  Architecture must match the Canon EDSDK you link.
#  This repo ships BOTH:
#    x64 : EDSDK/EDSDK.lib        + EDSDK/EDSDK.dll,  EDSDK/EdsImage.dll
#    x86 : EDSDK/Library/EDSDK.lib + EDSDK/Dll/EDSDK.dll, EDSDK/Dll/EdsImage.dll
#  Build 64-bit (default):  cmake -B build -A x64
#  Build 32-bit:            cmake -B build -A Win32
#  CMake auto-picks the matching EDSDK below from the compiler's pointer size.
# ══════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui PrintSupport)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

# ── EDSDK path (edit if yours differs) ──
# Expected layout:
#   EDSDK/Header/EDSDK.h, EDSDKTypes.h, EDSDKErrors.h
set(EDSDK_ROOT "${CMAKE_SOURCE_DIR}/EDSDK" CACHE PATH "Canon EDSDK root folder")

# Pick the lib/DLL set that matches the build architecture.
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(EDSDK_LIB  "${EDSDK_ROOT}/EDSDK.lib")                       # x64 import lib
    set(EDSDK_DLL_DIR "${EDSDK_ROOT}")                             # x64 DLLs live in root
    message(STATUS "EDSDK: 64-bit build")
else()
    set(EDSDK_LIB  "${EDSDK_ROOT}/Library/EDSDK.lib")              # x86 import lib
    set(EDSDK_DLL_DIR "${EDSDK_ROOT}/Dll")                         # x86 DLLs
    message(STATUS "EDSDK: 32-bit build")
endif()

if(EXISTS "${EDSDK_ROOT}/Header/EDSDK.h")
    set(EDSDK_FOUND TRUE)
    message(STATUS "EDSDK found at ${EDSDK_ROOT} (lib ${EDSDK_LIB})")
else()
    set(EDSDK_FOUND FALSE)
    message(WARNING "EDSDK not found at ${EDSDK_ROOT} — building without Canon support")
endif()

add_executable(${PROJECT_NAME} WIN32
    src/main.cpp
    src/Config.h
    src/Config.cpp
    src/EdsdkWrapper.h
    src/EdsdkWrapper.cpp
    src/CameraThread.h
    src/CameraThread.cpp
    src/ImageService.h
    src/ImageService.cpp
    src/SizeCard.h
    src/CameraPreview.h
    src/CameraPreview.cpp
    src/PhotoBoothApp.h
    src/PhotoBoothApp.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${OpenCV_INCLUDE_DIRS}
    src
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Widgets Qt6::Core Qt6::Gui Qt6::PrintSupport
    ${OpenCV_LIBS}
)

if(EDSDK_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${EDSDK_ROOT}/Header")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EDSDK=1)
    # Link the arch-matched .lib if present, otherwise we load the DLL dynamically
    if(EXISTS "${EDSDK_LIB}")
        target_link_libraries(${PROJECT_NAME} PRIVATE "${EDSDK_LIB}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE EDSDK_STATIC_LINK=1)
    endif()
    # Copy the arch-matched EDSDK DLLs next to the exe
    file(GLOB EDSDK_DLLS "${EDSDK_DLL_DIR}/*.dll")
    foreach(dll ${EDSDK_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach()
endif()

if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${PROJECT_NAME}> $<TARGET_FILE_DIR:${PROJECT_NAME}>
        COMMAND_EXPAND_LISTS)
endif()
