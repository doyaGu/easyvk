# EasyVK Example

# Find required tools for shader compilation
find_program(CLSPV_EXECUTABLE clspv)
if(NOT CLSPV_EXECUTABLE)
    message(WARNING "clspv not found. Shader compilation will be skipped. Install clspv to compile OpenCL kernels.")
endif()

# Create build directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Function to compile OpenCL kernels to SPIR-V
function(compile_opencl_kernel KERNEL_NAME)
    set(KERNEL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/${KERNEL_NAME}.cl)
    set(SPIRV_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${KERNEL_NAME}.spv)
    set(CINIT_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${KERNEL_NAME}.cinit)
    
    if(CLSPV_EXECUTABLE AND EXISTS ${KERNEL_SOURCE})
        # Generate .spv file
        add_custom_command(
            OUTPUT ${SPIRV_OUTPUT}
            COMMAND ${CLSPV_EXECUTABLE}
                --cl-std=CL2.0
                --spv-version=1.3
                --inline-entry-points
                ${KERNEL_SOURCE}
                -o ${SPIRV_OUTPUT}
            DEPENDS ${KERNEL_SOURCE}
            COMMENT "Compiling ${KERNEL_NAME}.cl to SPIR-V"
        )
        
        # Generate .cinit file (C array initialization)
        add_custom_command(
            OUTPUT ${CINIT_OUTPUT}
            COMMAND ${CLSPV_EXECUTABLE}
                --cl-std=CL2.0
                --spv-version=1.3
                --inline-entry-points
                --output-format=c
                ${KERNEL_SOURCE}
                -o ${CINIT_OUTPUT}
            DEPENDS ${KERNEL_SOURCE}
            COMMENT "Generating C initialization for ${KERNEL_NAME}.cl"
        )
        
        set(${KERNEL_NAME}_SPIRV ${SPIRV_OUTPUT} PARENT_SCOPE)
        set(${KERNEL_NAME}_CINIT ${CINIT_OUTPUT} PARENT_SCOPE)
    else()
        message(WARNING "Cannot compile ${KERNEL_NAME}.cl: clspv not found or source file missing")
    endif()
endfunction()

# Compile OpenCL kernels (if clspv is available)
compile_opencl_kernel(vect-add)

# Vector addition example executable
add_executable(vect-add-example
    vect-add.cpp
)

# Configure shader loading method
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vect-add.cinit)
    # Use existing .cinit file (embedded SPIR-V approach)
    message(STATUS "Using existing vect-add.cinit file for embedded SPIR-V")
    target_compile_definitions(vect-add-example PRIVATE USE_EMBEDDED_SPIRV)
    # Copy .cinit file to build directory so it can be included
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/vect-add.cinit
        ${CMAKE_CURRENT_BINARY_DIR}/vect-add.cinit
        COPYONLY
    )
elseif(CLSPV_EXECUTABLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vect-add.cl)
    # Generate .cinit file using clspv if available
    message(STATUS "Using clspv to generate embedded SPIR-V kernel")
    target_compile_definitions(vect-add-example PRIVATE USE_EMBEDDED_SPIRV)
    if(vect-add_CINIT)
        add_dependencies(vect-add-example vect-add-cinit)
    endif()
else()
    # Fallback: use file-based loading
    message(STATUS "Using file-based SPIR-V loading (runtime)")
    message(STATUS "Note: The example will expect a vect-add.spv file at runtime.")
    message(STATUS "To compile manually: clspv --cl-std=CL2.0 --spv-version=1.3 --inline-entry-points vect-add.cl -o vect-add.spv")
endif()

# Link against EasyVK library
target_link_libraries(vect-add-example PRIVATE
    easyvk
)

target_include_directories(vect-add-example PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}  # For generated .cinit files
)

# Set output properties
set_target_properties(vect-add-example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    OUTPUT_NAME "vect-add"
)

# Android NDK build support (if building for Android)
if(ANDROID)
    # Android-specific properties
    set_target_properties(vect-add-example PROPERTIES
        ANDROID_ARM_NEON TRUE
    )
endif()