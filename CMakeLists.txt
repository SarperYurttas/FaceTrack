cmake_minimum_required(VERSION 3.10)
project(facetrack)

execute_process(
    COMMAND bash -c "gcc -dumpmachine | cut -f1 -d -"
    OUTPUT_VARIABLE TARGET_DEVICE
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CMAKE_THREAD_LIBS_INIT "-lpthread")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread")
set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)
set(THREADS_PREFER_PTHREAD_FLAG ON)

# Add the source files
set(SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

# Add the include directories
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/include/gstreamer-1.0
    /usr/include/glib-2.0
    /usr/lib/${TARGET_DEVICE}-linux-gnu/glib-2.0/include # glib-config.h
    /opt/nvidia/deepstream/deepstream/sources/includes
    /usr/local/cuda/include
    /usr/local/cuda/targets/${TARGET_DEVICE}-linux/include
)

# Add the libraries
set(GSTREAMER_LIBRARIES
    gstreamer-1.0
    gobject-2.0
    glib-2.0
)

# Add the libraries
set(NV_LIBRARIES
    cuda
    cudart
    nvdsgst_meta
    nvds_meta
    nvdsgst_helper
    nvv4l2
    EGL
    nvbufsurface
    nvbufsurftransform
    nvdsbufferpool
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -L/usr/local/cuda/lib64/ -L/usr/local/cuda/targets/${TARGET_DEVICE}-linux/lib -L/opt/nvidia/deepstream/deepstream/lib/")

# Include directories
include_directories(${INCLUDE_DIRS})

# Add the executable
add_executable(${PROJECT_NAME} ${SRCS})

if (DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG=1)
endif ()

# Link the libraries
target_link_libraries(${PROJECT_NAME} ${GSTREAMER_LIBRARIES} ${NV_LIBRARIES} ${OTHER_LIBRARIES})
