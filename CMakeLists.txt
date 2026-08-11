cmake_minimum_required(VERSION 3.31)

project(cuPIR LANGUAGES CUDA CXX)
set(CMAKE_CXX_STANDARD 23)
#set(CMAKE_CUDA_ARCHITECTURES 75)
#set(CMAKE_CUDA_FLAGS "${CMAKE_CUDA_FLAGS} --extended-lambda")


# Try to find nvcc in PATH if not explicitly set
#if(NOT DEFINED CMAKE_CUDA_COMPILER)
#    find_program(NVCC_PATH nvcc HINTS /usr/local/cuda/bin ENV PATH)
#    if(NVCC_PATH)
#        set(CMAKE_CUDA_COMPILER ${NVCC_PATH} CACHE FILEPATH "CUDA compiler")
#    else()
#        message(FATAL_ERROR "CUDA compiler (nvcc) not found. Please install CUDA or add nvcc to PATH.")
#    endif()
#endif()

#find_package(CUDAToolkit REQUIRED)
#
#add_executable(test_mm_mult
#        test/test_mm_mult.cu
#)
#target_include_directories(test_mm_mult PUBLIC src)
#
#add_executable(test_goldberg
#        test/test_goldberg.cu
#)
#target_include_directories(test_goldberg PUBLIC src)

