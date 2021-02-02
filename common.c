//
//  common.c
//  ffmpeg_proj
//
//  Created by Horson on 2021/1/24.
//

#include <stdio.h>
#include "common.h"

int encode_frame(AVFrame *frame,
                 AVPacket *pkt,
                 AVCodecContext *c,
                 FILE *f) {
    int rc = 0;
    size_t len = 0;
    rc = avcodec_send_frame(c, frame);
    
    while (rc == 0) {
        rc = avcodec_receive_packet(c, pkt);
        
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            av_packet_unref(pkt);
            return 0;
        }
        if (rc == AVERROR(EINVAL)) {
            av_log(NULL, AV_LOG_ERROR, "codec opened error\n");
            av_packet_unref(pkt);
            return AVERROR(EINVAL);
        }
        if (rc < 0) {
            av_log(NULL, AV_LOG_ERROR, "codec working error\n");
            av_packet_unref(pkt);
            return rc;
        }
        
        len = fwrite(pkt->data, 1, pkt->size, f);
        if (len != pkt->size) {
            av_log(NULL, AV_LOG_WARNING,
                   "Warnning: write data size is not equal to input size\n");
        }
        av_log(NULL, AV_LOG_INFO, "pkt size: %d, write size: %ld\n", pkt->size, len);
        fflush(f);
        av_packet_unref(pkt);
    }
    return rc;
}

void log_packet(AVFormatContext *fmt_ctx,
                const AVPacket *pkt,
                const char *tag) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    
    av_log(NULL, AV_LOG_DEBUG,
           "[demo log] tag: %s pts: %s, pts_time: %s, dts: %s, dts_time: %s, duration: %s, duration_time: %s, stream_idx: %d\n",
           tag,
           av_ts2str(pkt->pts),
           av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts),
           av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration),
           av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);
}

int recive_frame(AVFrame *frame,
                 AVCodecContext *c) {
    int rc = 0;
    rc = avcodec_receive_frame(c, frame);
    if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
        rc = 0;
    }
    
    if (rc == AVERROR(EINVAL)) {
        rc = AVERROR(EINVAL);
        av_log(NULL, AV_LOG_ERROR, "codec not opened\n");
    }
    
    if (rc < 0) {
        av_log(NULL, AV_LOG_ERROR, "decode error occured, or codec not opened\n");
    }
    return rc;
}
