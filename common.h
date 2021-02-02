//
//  common.c
//  ffmpeg_proj
//
//  Created by Horson on 2021/1/24.
//

#include <stdio.h>
#include <libavutil/log.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

#define CHECK_ERROR(cond, inf, code, tip) \
if (cond) { \
    if (code) rc = code; \
    av_log(NULL, AV_LOG_ERROR, "[demo log] %s\n", inf); \
    goto tip; \
}

#define RETURN_ERROR_CHECK(rc)\
if (rc < 0) {\
    av_log(NULL, AV_LOG_ERROR,\
           "[demo log] error occured: %s, code: %d",\
           av_err2str(rc),\
           rc);\
    return rc;\
}

int encode_frame(AVFrame *frame,
                 AVPacket *pkt,
                 AVCodecContext *c,
                 FILE *f);

void log_packet(AVFormatContext *fmt_ctx,
                const AVPacket *pkt,
                const char *tag);

int recive_frame(AVFrame *frame,
                 AVCodecContext *c);
