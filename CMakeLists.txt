cmake_minimum_required(VERSION 3.10)

project(EasyVK VERSION 1.0.0 LANGUAGES CXX C)

# Check if this is the main project or a subproject
set(EASYVK_MAIN_PROJECT OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(EASYVK_MAIN_PROJECT ON)
endif()

# Set C++ standard only if not already set by parent
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
if(NOT CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Find Vulkan package with better error handling
find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(STATUS "Vulkan not found via find_package, attempting manual detection")
    find_path(VULKAN_INCLUDE_DIR vulkan/vulkan.h
        HINTS $ENV{VULKAN_SDK}/include
        PATHS /usr/include /usr/local/include)
    find_library(VULKAN_LIBRARY vulkan
        HINTS $ENV{VULKAN_SDK}/lib
        PATHS /usr/lib /usr/local/lib)
    
    if(VULKAN_INCLUDE_DIR AND VULKAN_LIBRARY)
        set(Vulkan_FOUND TRUE)
        add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            IMPORTED_LOCATION ${VULKAN_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${VULKAN_INCLUDE_DIR})
    else()
        message(FATAL_ERROR "Vulkan not found. Please install Vulkan SDK or set VULKAN_SDK environment variable.")
    endif()
endif()

# Set platform-specific defines for Volk
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_MACOS_MVK)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_ANDROID_KHR)
endif()

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
    volk
)

target_link_libraries(easyvk PUBLIC
    volk
    Vulkan::Vulkan
)

# Android-specific settings
if(ANDROID)
    target_link_libraries(easyvk PUBLIC log)
endif()

# Set target compile features
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.1")
    target_compile_features(easyvk PUBLIC cxx_std_20)
endif()

# Options - only show in main project to avoid cluttering parent project cache
if(EASYVK_MAIN_PROJECT)
    option(EASYVK_INSTALL "Enable installation" OFF)
    option(EASYVK_BUILD_EXAMPLES "Build examples" OFF)
else()
    option(EASYVK_INSTALL "Enable installation" OFF)
    option(EASYVK_BUILD_EXAMPLES "Build examples" OFF)
    # Hide options from cache in subproject mode
    mark_as_advanced(EASYVK_INSTALL EASYVK_BUILD_EXAMPLES)
endif()

# Install targets
if(EASYVK_INSTALL)
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
endif()

# Add example subdirectory
if(EASYVK_BUILD_EXAMPLES)
    add_subdirectory(example)
endif()