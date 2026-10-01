cmake_minimum_required(VERSION 3.20)
project(IDPhotoBooth VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ══════════════════════════════════════════════
#  MUST BE 32-BIT if your EDSDK.dll is 32-bit
#  Configure with:  cmake -B build -A Win32
#  (or -A x64 if you have the 64-bit EDSDK)
# ══════════════════════════════════════════════

find_package(Qt6 REQUIRED COMPONENTS Widgets Core Gui)
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)

# ── EDSDK path (edit if yours differs) ──
# Expected layout:
#   EDSDK/Header/EDSDK.h, EDSDKTypes.h, EDSDKErrors.h
#   EDSDK/Library/EDSDK.lib
#   EDSDK/Dll/EDSDK.dll, EdsImage.dll
set(EDSDK_ROOT "${CMAKE_SOURCE_DIR}/EDSDK" CACHE PATH "Canon EDSDK root folder")

if(EXISTS "${EDSDK_ROOT}/Header/EDSDK.h")
    set(EDSDK_FOUND TRUE)
    message(STATUS "EDSDK found at ${EDSDK_ROOT}")
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
    Qt6::Widgets Qt6::Core Qt6::Gui
    ${OpenCV_LIBS}
)

if(EDSDK_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE "${EDSDK_ROOT}/Header")
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_EDSDK=1)
    # Link the .lib if present, otherwise we load the DLL dynamically
    if(EXISTS "${EDSDK_ROOT}/Library/EDSDK.lib")
        target_link_libraries(${PROJECT_NAME} PRIVATE "${EDSDK_ROOT}/Library/EDSDK.lib")
        target_compile_definitions(${PROJECT_NAME} PRIVATE EDSDK_STATIC_LINK=1)
    endif()
    # Copy EDSDK DLLs next to the exe
    file(GLOB EDSDK_DLLS "${EDSDK_ROOT}/Dll/*.dll")
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
