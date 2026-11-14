add_executable(cmair-desktop main.cpp)
target_link_libraries(cmair-desktop PRIVATE cmair_core)
target_compile_features(cmair-desktop PRIVATE cxx_std_17)
