# EasyVK Example

find_program(CLSPV_EXECUTABLE clspv)
if (NOT CLSPV_EXECUTABLE)
    message(FATAL_ERROR "clspv not found. Please install clspv to compile OpenCL kernels.")
endif ()


# Check if we want embedded SPIR-V
option(USE_EMBEDDED_SPIRV "Embed SPIR-V kernels in executable" OFF)

# Setup kernel compilation
set(KERNEL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/vect-add.cl)
if (NOT EXISTS ${KERNEL_SOURCE})
    message(FATAL_ERROR "Kernel source ${KERNEL_SOURCE} not found")
endif()

# Vector addition example executable
add_executable(vect-add-example
        vect-add.cpp
)

# Configure compile definitions and kernel compilation
if (USE_EMBEDDED_SPIRV)
    message(STATUS "Using embedded SPIR-V loading (.cinit file)")
    target_compile_definitions(vect-add-example PRIVATE USE_EMBEDDED_SPIRV)
    add_custom_command(TARGET vect-add-example PRE_BUILD
            COMMAND ${CLSPV_EXECUTABLE}
            --cl-std=CL2.0
            --spv-version=1.3
            --inline-entry-points
            --output-format=c
            -o ${CMAKE_CURRENT_BINARY_DIR}/vect-add.cinit
            ${KERNEL_SOURCE}
            COMMENT "Generating embedded SPIR-V for vect-add.cl"
            VERBATIM
    )
else()
    message(STATUS "Using file-based SPIR-V loading (.spv file)")
    add_custom_command(TARGET vect-add-example PRE_BUILD
            COMMAND ${CLSPV_EXECUTABLE}
            --cl-std=CL2.0
            --spv-version=1.3
            --inline-entry-points
            -o ${CMAKE_CURRENT_BINARY_DIR}/vect-add.spv
            ${KERNEL_SOURCE}
            COMMENT "Compiling vect-add.cl to SPIR-V"
            VERBATIM
    )
endif()

target_link_libraries(vect-add-example PRIVATE easyvk)

target_include_directories(vect-add-example PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # For generated .cinit files
)

if (ANDROID)
    set_target_properties(vect-add-example PROPERTIES
            ANDROID_ARM_NEON TRUE
    )
endif()