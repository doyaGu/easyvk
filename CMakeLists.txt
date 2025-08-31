cmake_minimum_required(VERSION 3.10)

project(EasyVK VERSION 2.0.0 LANGUAGES CXX C)

# Check if this is the main project or a subproject
set(EASYVK_MAIN_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(EASYVK_MAIN_PROJECT ON)
endif ()

# Set C++ standard only if not already set by parent
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif ()
if (NOT CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif ()

# Set platform-specific defines for Volk
if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_MACOS_MVK)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
endif ()

# Add Volk submodule
add_subdirectory(volk)

# EasyVK library
add_library(easyvk STATIC
        src/easyvk.cpp
        src/easyvk.h
)

# Create alias for consistent usage in parent projects
add_library(EasyVK::easyvk ALIAS easyvk)

target_include_directories(easyvk PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(easyvk PUBLIC volk)

# Android-specific settings
if (ANDROID)
    target_link_libraries(easyvk PUBLIC log)
endif ()

# Set target compile features
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.1")
    target_compile_features(easyvk PUBLIC cxx_std_11)
endif ()

# Options - only show in main project to avoid cluttering parent project cache
if (EASYVK_MAIN_PROJECT)
    option(EASYVK_INSTALL "Enable installation" ON)
    option(EASYVK_BUILD_EXAMPLES "Build examples" ON)
    option(EASYVK_ENABLE_PORTABILITY_ENUMERATION "Opt into VK_KHR_portability_enumeration" ON)
    option(EASYVK_USE_SPIRV_TOOLS "Validate SPIR-V modules at runtime" OFF)
    option(EASYVK_DEFAULT_ENABLE_DEBUG_UTILS "Enable debug utils by default" OFF)
    option(EASYVK_DEFAULT_ENABLE_ROBUSTNESS2 "Enable robustness2 by default" OFF)
    option(EASYVK_USE_VMA "Use Vulkan Memory Allocator for buffers" OFF)
else ()
    option(EASYVK_INSTALL "Enable installation" OFF)
    option(EASYVK_BUILD_EXAMPLES "Build examples" OFF)
    option(EASYVK_ENABLE_PORTABILITY_ENUMERATION "Opt into VK_KHR_portability_enumeration" ON)
    option(EASYVK_USE_SPIRV_TOOLS "Validate SPIR-V modules at runtime" OFF)
    option(EASYVK_DEFAULT_ENABLE_DEBUG_UTILS "Enable debug utils by default" OFF)
    option(EASYVK_DEFAULT_ENABLE_ROBUSTNESS2 "Enable robustness2 by default" OFF)
    option(EASYVK_USE_VMA "Use Vulkan Memory Allocator for buffers" OFF)
    # Hide options from cache in subproject mode
    mark_as_advanced(EASYVK_INSTALL EASYVK_BUILD_EXAMPLES EASYVK_ENABLE_PORTABILITY_ENUMERATION
            EASYVK_USE_SPIRV_TOOLS EASYVK_DEFAULT_ENABLE_DEBUG_UTILS
            EASYVK_DEFAULT_ENABLE_ROBUSTNESS2 EASYVK_USE_VMA)
endif ()

# Configure compile definitions based on options
if (EASYVK_ENABLE_PORTABILITY_ENUMERATION)
    target_compile_definitions(easyvk PRIVATE EASYVK_ENABLE_PORTABILITY_ENUMERATION=1)
endif ()

if (EASYVK_DEFAULT_ENABLE_DEBUG_UTILS)
    target_compile_definitions(easyvk PRIVATE EASYVK_DEFAULT_ENABLE_DEBUG_UTILS=1)
endif ()

if (EASYVK_DEFAULT_ENABLE_ROBUSTNESS2)
    target_compile_definitions(easyvk PRIVATE EASYVK_DEFAULT_ENABLE_ROBUSTNESS2=1)
endif ()

# SPIR-V Tools integration
if (EASYVK_USE_SPIRV_TOOLS)
    find_package(SPIRV-Tools CONFIG QUIET)
    if (SPIRV-Tools_FOUND)
        target_link_libraries(easyvk PRIVATE SPIRV-Tools)
        target_compile_definitions(easyvk PRIVATE EASYVK_USE_SPIRV_TOOLS=1)
        message(STATUS "EasyVK: SPIR-V validation enabled")
    else ()
        message(WARNING "EasyVK: SPIRV-Tools not found, disabling SPIR-V validation")
    endif ()
endif ()

# Vulkan Memory Allocator integration
if (EASYVK_USE_VMA)
    find_path(VMA_INCLUDE_DIR
            NAMES vk_mem_alloc.h
            PATHS ${CMAKE_CURRENT_SOURCE_DIR}/extern/VMA/include
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/VMA/include
    )
    if (VMA_INCLUDE_DIR)
        target_include_directories(easyvk PRIVATE ${VMA_INCLUDE_DIR})
        target_compile_definitions(easyvk PRIVATE EASYVK_USE_VMA=1)
        message(STATUS "EasyVK: VMA integration enabled")
    else ()
        message(WARNING "EasyVK: VMA headers not found, disabling VMA integration")
    endif ()
endif ()

# Install targets
if (EASYVK_INSTALL)
    include(GNUInstallDirs)

    install(TARGETS easyvk volk
            EXPORT EasyVKTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(FILES src/easyvk.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Configure package config files
    include(CMakePackageConfigHelpers)

    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EasyVKConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/EasyVKConfig.cmake"
            INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EasyVK
    )

    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/EasyVKConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
    )

    install(EXPORT EasyVKTargets
            FILE EasyVKTargets.cmake
            NAMESPACE EasyVK::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EasyVK
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/EasyVKConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/EasyVKConfigVersion.cmake"
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EasyVK
    )
endif ()

# Add example subdirectory
if (EASYVK_BUILD_EXAMPLES)
    add_subdirectory(example)
endif ()