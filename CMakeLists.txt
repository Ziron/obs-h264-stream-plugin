project(h264-stream)

set(h264-stream_SOURCES
        h264-stream-source.c)

add_library(h264-stream MODULE
        ${h264-stream_SOURCES})
target_link_libraries(h264-stream
        libobs
        ${h264-stream_PLATFORM_DEPS})

install_obs_plugin(h264-stream)
