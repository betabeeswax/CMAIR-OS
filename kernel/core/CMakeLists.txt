add_library(cmair_core STATIC
    src/kernel.cpp
)
target_include_directories(cmair_core PUBLIC include)
target_compile_features(cmair_core PUBLIC cxx_std_17)
