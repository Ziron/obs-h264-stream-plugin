#include <obs-module.h>
#include <stdlib.h>
#include <util/threading.h>
#include <util/platform.h>
#include <obs.h>

struct h264_stream_tex {
    obs_source_t *source;
    os_event_t   *stop_signal;
    pthread_t    thread;
    bool         initialized;
};

static const char *h264_stream_getname(void *unused)
{
    UNUSED_PARAMETER(unused);
    return "H264 Stream Source";
}

static void h264_stream_destroy(void *data)
{
    struct h264_stream_tex *rt = data;

    if (rt) {
        if (rt->initialized) {
            os_event_signal(rt->stop_signal);
            pthread_join(rt->thread, NULL);
        }

        os_event_destroy(rt->stop_signal);
        bfree(rt);
    }
}

static inline void fill_texture(uint8_t *pixels)
{
    int i;
    for (i = 0; i < (640 * 480 * 3) / 2; i++) {
        pixels[i] = rand();
    }
    return;

    size_t x, y;

    for (y = 0; y < 20; y++) {
        for (x = 0; x < 20; x++) {
            uint32_t pixel = 0;
            pixel |= (rand()%256);
            pixel |= (rand()%256) << 8;
            pixel |= (rand()%256) << 16;
            //pixel |= (rand()%256) << 24;
            //pixel |= 0xFFFFFFFF;
            pixels[y*20 + x] = pixel;
        }
    }
}

static void *video_thread(void *data)
{
    struct h264_stream_tex   *rt = data;
    uint8_t            pixels[(640 * 480 * 3) / 2];
    uint64_t           cur_time = os_gettime_ns();

    struct obs_source_frame frame = {
            .data     = {[0] = pixels, [1] = pixels + 640*480, [2] = pixels + 640*480 + 640*480 / 4},
            .linesize = {[0] = 640, [1] = 640 / 2, [2] = 640 / 2},
            .width    = 640,
            .height   = 480,
            .format   = VIDEO_FORMAT_I420
    };
    video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_PARTIAL,
                                frame.color_matrix, frame.color_range_min,
                                frame.color_range_max);


    FILE *pipein = popen("ffmpeg -re -framerate 30 -i /home/sebbe/Documents/piStream/stream.h264 -f image2pipe -vcodec rawvideo -pix_fmt yuv420p -", "r");

    // Process video frames
    while(os_event_try(rt->stop_signal) == EAGAIN) {
        // Read a frame from the input pipe into the buffer
        int count = fread(pixels, 1, sizeof(pixels), pipein);

        //printf("Got frame of size %u!\n", count);

        if (count < sizeof(pixels)) break;

        cur_time = os_gettime_ns();
        frame.timestamp = cur_time;

        obs_source_output_video(rt->source, &frame);

        //os_sleepto_ns(cur_time + 100000000);
    }
    while (os_event_try(rt->stop_signal) == EAGAIN) {
        fill_texture(pixels);

        cur_time = os_gettime_ns();
        frame.timestamp = cur_time;

        obs_source_output_video(rt->source, &frame);

        os_sleepto_ns(cur_time + 250000000);
    }
    return NULL;
}

static void *h264_stream_create(obs_data_t *settings, obs_source_t *source)
{
    struct h264_stream_tex *rt = bzalloc(sizeof(struct h264_stream_tex));
    rt->source = source;

    if (os_event_init(&rt->stop_signal, OS_EVENT_TYPE_MANUAL) != 0) {
        h264_stream_destroy(rt);
        return NULL;
    }

    if (pthread_create(&rt->thread, NULL, video_thread, rt) != 0) {
        h264_stream_destroy(rt);
        return NULL;
    }

    rt->initialized = true;

    UNUSED_PARAMETER(settings);
    UNUSED_PARAMETER(source);
    return rt;
}

struct obs_source_info h264_stream_source_info = {
        .id           = "h264-stream",
        .type         = OBS_SOURCE_TYPE_INPUT,
        .output_flags = OBS_SOURCE_ASYNC_VIDEO,
        .get_name     = h264_stream_getname,
        .create       = h264_stream_create,
        .destroy      = h264_stream_destroy,
};

OBS_DECLARE_MODULE()

bool obs_module_load(void)
{
    obs_register_source(&h264_stream_source_info);
    return true;
}