/*
 * Copyright (c) 2001 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/**
 * @file
 * libavcodec API use example.
 *
 * Note that libavcodec only handles codecs (mpeg, mpeg4, etc...),
 * not file formats (avi, vob, mp4, mov, mkv, mxf, flv, mpegts, mpegps, etc...). See library 'libavformat' for the
 * format handling
 * @example doc/examples/decoding_encoding.c
 */
#define _GNU_SOURCE
#include <math.h>
#include <string.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>

#define INBUF_SIZE 65536


/*
 * Video decoding example
 */
static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}
static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
                              AVFrame *frame, int *frame_count, AVPacket *pkt, int last)
{
    int len, got_frame;
    char buf[1024];
    len = avcodec_decode_video2(avctx, frame, &got_frame, pkt);
    if (len < 0) {
        fprintf(stderr, "Error while decoding frame %d\n", *frame_count);
        return len;
    }
    if (got_frame) {
        printf("Saving %sframe %3d\n", last ? "last " : "", *frame_count);
        fflush(stdout);
        /* the picture is allocated by the decoder, no need to free it */
        snprintf(buf, sizeof(buf), outfilename, *frame_count);
        pgm_save(frame->data[0], frame->linesize[0],
                 avctx->width, avctx->height, buf);
        (*frame_count)++;
    }
    if (pkt->data && len > 0) {
        pkt->size -= len;
        pkt->data += len;
    }
    return 0;
}
static void video_decode_example(const char *outfilename, const char *filename)
{
    AVCodec *codec;
    AVCodecContext *c= NULL;
    AVCodecParserContext *p = NULL;
    int frame_count;
    FILE *f;
    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + FF_INPUT_BUFFER_PADDING_SIZE];
    AVPacket avpkt;
    av_init_packet(&avpkt);
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    memset(inbuf + INBUF_SIZE, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    printf("Decode video file %s to %s\n", filename, outfilename);
    /* find the mpeg4 video decoder */
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }
    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    c->width=640;
    c->coded_width=640;
    c->height=480;
    c->coded_height=480;
    c->pix_fmt=AV_PIX_FMT_YUV420P;

    p = av_parser_init(AV_CODEC_ID_H264);

    if(codec->capabilities & CODEC_CAP_TRUNCATED)
        c->flags |= CODEC_FLAG_TRUNCATED; /* We may send incomplete frames */
    if(codec->capabilities & CODEC_FLAG2_CHUNKS)
        c->flags |= CODEC_FLAG2_CHUNKS;

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
       MUST be initialized there because this information is not
       available in the bitstream. */
    /* open it */
    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }
    frame_count = 0;
    size_t in_len = 0;
    uint8_t NAL_START[] = {0, 0, 0, 1};
    for (;;) {
        size_t nRead = fread(inbuf, 1, INBUF_SIZE - in_len, f);
        in_len += nRead;

        if (in_len == 0)
            break;



        uint8_t *in_data = inbuf;
        while(in_len){
            uint8_t *data;
            int size;
            int len = av_parser_parse2(p, c, &data, &size,
                                   in_data, in_len,
                                   0, 0, 0);
            in_data += len;
            in_len  -= len;
            if(size) {
                avpkt.data = data;
                avpkt.size = size;
                avpkt.flags |= AV_PKT_FLAG_KEY;
                printf("%u\n", size);
                break;
            }
        }
        printf("Hej %u\n", avpkt.data - inbuf);
        /*
        //printf("%u\n", in_len);



        uint8_t *nextFrame = memmem(inbuf + 4, in_len - 4, NAL_START, 4);

        size_t frameLen;

        if (nextFrame != NULL) {
            frameLen = nextFrame - inbuf;
        } else {
            frameLen = in_len;
        }

        uint8_t framebuf[frameLen + FF_INPUT_BUFFER_PADDING_SIZE];
        memcpy(framebuf, inbuf, frameLen);
        memset(framebuf + frameLen, 0, FF_INPUT_BUFFER_PADDING_SIZE);

        printf("%u\n", frameLen);

        avpkt.size = frameLen;
        avpkt.data = framebuf;

        memmove(inbuf, nextFrame, in_len - frameLen);
        in_len -= frameLen;
        */
        /* NOTE1: some codecs are stream based (mpegvideo, mpegaudio)
           and this is the only method to use them because you cannot
           know the compressed data size before analysing it.
           BUT some other codecs (msmpeg4, mpeg4) are inherently frame
           based, so you must call them with all the data for one
           frame exactly. You must also initialize 'width' and
           'height' before initializing them. */
        /* NOTE2: some codecs allow the raw parameters (frame size,
           sample rate) to be changed at any frame. We handle this, so
           you should also take care of it */
        /* here, we use a stream based decoder (mpeg1video), so we
           feed decoder and see if it could decode a frame */
        if (decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 0) < 0)
            ;
            //exit(1);

        printf("Size left: %u\n", avpkt.size);
        //memmove(inbuf, in_data, in_len);
        memmove(inbuf, avpkt.data, in_len + avpkt.size);

    }
    /* some codecs, such as MPEG, transmit the I and P frame with a
       latency of one frame. You must do the following to have a
       chance to get the last frame of the video */
    avpkt.data = NULL;
    avpkt.size = 0;
    decode_write_frame(outfilename, c, frame, &frame_count, &avpkt, 1);
    fclose(f);
    avcodec_close(c);
    av_free(c);
    av_frame_free(&frame);
    printf("\n");
}
int main(int argc, char **argv)
{
    /* register all the codecs */
    avcodec_register_all();

    video_decode_example("test%u.pgm", "stream.h264");
    return 0;
}
