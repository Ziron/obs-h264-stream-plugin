#include <obs-module.h>
#include <stdlib.h>
#include <unistd.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include <util/threading.h>
#include <util/platform.h>
#include <obs.h>

struct h264_stream_tex {
    obs_source_t *source;
    os_event_t *stop_signal;
    pthread_t thread;
    bool initialized;
    const char *ipaddr;
    uint32_t width;
    uint32_t height;
};

static const char *h264_stream_getname(void *unused) {
    UNUSED_PARAMETER(unused);
    return "H264 Stream Source";
}

static void h264_stream_destroy(void *data) {
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

static inline void fill_texture(uint8_t *pixels) {
    int i;
    for (i = 0; i < (640 * 480 * 3) / 2; i++) {
        pixels[i] = rand();
    }
    return;

    size_t x, y;

    for (y = 0; y < 20; y++) {
        for (x = 0; x < 20; x++) {
            uint32_t pixel = 0;
            pixel |= (rand() % 256);
            pixel |= (rand() % 256) << 8;
            pixel |= (rand() % 256) << 16;
            //pixel |= (rand()%256) << 24;
            //pixel |= 0xFFFFFFFF;
            pixels[y * 20 + x] = pixel;
        }
    }
}

static void *tcp_thread(void *data) {

}

static void *video_thread(void *data) {
#define WIDTH 1296
#define HEIGHT 972

    struct h264_stream_tex *rt = data;
    uint8_t pixels[(WIDTH * HEIGHT * 3) / 2];
    uint64_t cur_time = os_gettime_ns();

    struct obs_source_frame frame = {
            .data     = {[0] = pixels, [1] = pixels + WIDTH * HEIGHT, [2] = pixels + WIDTH * HEIGHT +
                                                                            WIDTH * HEIGHT / 4},
            .linesize = {[0] = WIDTH, [1] = WIDTH / 2, [2] = WIDTH / 2},
            .width    = WIDTH,
            .height   = HEIGHT,
            .format   = VIDEO_FORMAT_I420
    };
    video_format_get_parameters(VIDEO_CS_DEFAULT, VIDEO_RANGE_PARTIAL,
                                frame.color_matrix, frame.color_range_min,
                                frame.color_range_max);


    int in_pipe[2];
    int out_pipe[2];
    int pid1, pid2;

    pipe(in_pipe); //create a pipe
    pipe(out_pipe); //create a pipe
    pid1 = fork(); //span a child process
    if (pid1 == 0) {
        signal(SIGINT, SIG_DFL);
        // Child. Let's redirect its standard in-/output to our pipe and replace process with ffmpeg
        dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        //dup2(pipefd[1], STDERR_FILENO);
        //execl("/usr/bin/ffmpeg", "ffmpeg", "-re", "-framerate", "30", "-i", "/home/sebbe/Documents/piStream/stream.h264", "-f", "image2pipe", "-vcodec", "rawvideo", "-pix_fmt", "yuv420p", "-", (char*) NULL);
        execlp("ffmpeg", "ffmpeg", "-probesize", "32", "-analyzeduration", "20K", "-framerate", "100", "-i",
               "-", "-f", "image2pipe", "-fflags", "nobuffer", "-vcodec", "rawvideo", "-pix_fmt", "yuv420p", "-",
               (char *) NULL);
        exit(0);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    /*
    pthread_t tcp_thread_child;

    if (pthread_create(&tcp_thread_child, NULL, tcp_thread, rt) != 0) {
        h264_stream_destroy(rt);
        return NULL;
    }
    */

    pid2 = fork(); //span a child process
    if (pid2 == 0) {
        signal(SIGINT, SIG_DFL);
        int len = 0;
        uint8_t buf[1024];
        close(out_pipe[0]);

        while (1) {
            if (rt->ipaddr == NULL) {
                sleep(1);
                continue;
            }

            struct sockaddr_in serv_addr;
            int sock = 0;
            if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                fprintf(stderr, "Socket creation error \n");
                break;
            }

            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(50007);
            inet_pton(AF_INET, rt->ipaddr, &serv_addr.sin_addr);
            if (connect(sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
                fprintf(stderr, "Connection Failed \n");
                sleep(1);
                continue;
            }

            while ((len = read(sock, buf, sizeof(buf))) > 0) {
                write(in_pipe[1], buf, len);
            }
            shutdown(sock, SHUT_WR);
            close(sock);
        }
        close(in_pipe[1]);
        exit(0);
    }
    close(in_pipe[1]);

    //Only parent gets here. Listen to what the tail says
    //close(pipefd[1]);
    FILE *pipein = fdopen(out_pipe[0], "r");

    //FILE *pipein = popen("ffmpeg -re -framerate 30 -i /home/sebbe/Documents/piStream/stream.h264 -f image2pipe -vcodec rawvideo -pix_fmt yuv420p -", "r");

    // Process video frames
    while (os_event_try(rt->stop_signal) == EAGAIN) {
        // Read a frame from the input pipe into the buffer
        int count = fread(pixels, 1, sizeof(pixels), pipein);

        //printf("Got frame of size %u!\n", count);

        //if (!count) break;
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

    int ret1 = kill(pid1, SIGINT);
    int ret2 = kill(pid2, SIGINT);

    return NULL;
}

static void h264_stream_update(void *data, obs_data_t *settings) {
    struct h264_stream_tex *context = data;
    const char *ipaddr = obs_data_get_string(settings, "ipaddr");
    uint32_t width = (uint32_t) obs_data_get_int(settings, "width");
    uint32_t height = (uint32_t) obs_data_get_int(settings, "height");

    context->ipaddr = ipaddr;
    context->width = width;
    context->height = height;
}

static void *h264_stream_create(obs_data_t *settings, obs_source_t *source) {
    struct h264_stream_tex *rt = bzalloc(sizeof(struct h264_stream_tex));
    rt->source = source;

    h264_stream_update(rt, settings);

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


static obs_properties_t *h264_stream_properties(void *unused) {
    UNUSED_PARAMETER(unused);

    obs_properties_t *props = obs_properties_create();

    obs_properties_add_text(props, "ipaddr", "Ip address", OBS_TEXT_DEFAULT);

    obs_properties_add_int(props, "width", "Width ", 0, 3280, 1);

    obs_properties_add_int(props, "height", "Height", 0, 2464, 1);

    return props;
}

static void h264_stream_defaults(obs_data_t *settings) {
    obs_data_set_default_string(settings, "ipaddr", "192.168.0.1");
    obs_data_set_default_int(settings, "width", 1280);
    obs_data_set_default_int(settings, "height", 720);
}

struct obs_source_info h264_stream_source_info = {
        .id           = "h264-stream",
        .type         = OBS_SOURCE_TYPE_INPUT,
        .output_flags = OBS_SOURCE_ASYNC_VIDEO,
        .get_name     = h264_stream_getname,
        .create       = h264_stream_create,
        .destroy      = h264_stream_destroy,
        .get_properties = h264_stream_properties,
        .get_defaults   = h264_stream_defaults,
        .update         = h264_stream_update,
};

OBS_DECLARE_MODULE()

bool obs_module_load(void) {
    obs_register_source(&h264_stream_source_info);
    return true;
}