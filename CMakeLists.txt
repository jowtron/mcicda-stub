cmake_minimum_required(VERSION 3.10)
project(mcicda_stub)

# Build as 32-bit DLL
add_library(mcicda SHARED mcicda_stub.c mcicda.def)

# Link with Windows multimedia library
target_link_libraries(mcicda winmm)

# Set output name
set_target_properties(mcicda PROPERTIES
    OUTPUT_NAME "mcicda"
    PREFIX ""
    SUFFIX ".dll"
)
