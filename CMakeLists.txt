cmake_minimum_required(VERSION 3.10)
project(bladeRF_wideband_sweeper)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wideband_sweeper src/wideband_spectrum_sweep.cpp)

target_include_directories(wideband_sweeper PRIVATE /usr/include)
target_link_directories(wideband_sweeper PRIVATE /usr/lib/x86_64-linux-gnu /usr/local/lib)

# FreeGLUT 추가
target_link_libraries(wideband_sweeper PRIVATE 
    bladeRF 
    fftw3 
    GL 
    glfw 
    glut 
    GLU
    m 
    pthread
)

target_compile_options(wideband_sweeper PRIVATE -O3 -march=native -Wall -Wextra)