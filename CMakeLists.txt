cmake_minimum_required(VERSION 3.10)
project(PyCpp)

set(CMAKE_CXX_STANDARD 20)

# Enable Hot Reload for MSVC compilers if supported.
if(POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Set output directories to create out/ and debug_out/ folders
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/debug_out)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out)
endif()

# Make sure output directories exist
file(MAKE_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

# Add source to this project's executable.
add_executable(PyCpp  
    "PyCpp/src/lexer.cpp" 
    "PyCpp/src/pycpp_parser.cpp" 
    "PyCpp/src/semantic_analyzer.cpp" 
    "PyCpp/src/code_generator.cpp" 
    "PyCpp/src/main.cpp"
    "PyCpp/src/ast.cpp"
    "PyCpp/src/include_manager.cpp"
    "PyCpp/src/header_generator.cpp"
    "PyCpp/src/import_resolver.cpp"
    "PyCpp/src/compiler_backend.cpp"  # Only .cpp files here
)

# Include directories
target_include_directories(PyCpp PRIVATE "PyCpp/include")

# --- ADDED FOR STATIC LINKING (Fixes the missing libstdc++-6.dll error) ---
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(PyCpp PRIVATE -static-libgcc -static-libstdc++ -static)
endif()
# --------------------------------------------------------------------------

# Set output name and properties
set_target_properties(PyCpp PROPERTIES
    OUTPUT_NAME "pycpp"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/debug_out"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/out"
)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Output directory: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "Binary directory: ${CMAKE_BINARY_DIR}")

# Post-build message to show where executable is located
add_custom_command(TARGET PyCpp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "PyCpp compiler built successfully!"
    COMMAND ${CMAKE_COMMAND} -E echo "Executable location: $<TARGET_FILE:PyCpp>"
)