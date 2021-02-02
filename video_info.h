//
//  video_info.h
//  ffmpeg_proj
//
//  Created by haocheng on 2021/2/2.
//

#ifndef video_info_h
#define video_info_h

#include <stdio.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "packet_queue.h"
#include <SDL.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000
#define VIDEO_PICTURE_QUEUE_SIZE 3

typedef struct FrameInfo {
    AVFrame *frame;
    double pts;
} FrameInfo;

typedef struct VideoInfo {
    char                in_filename[1024];
    AVFormatContext     *fmt_ctx;
    
    int                 has_audio, has_video;
    int                 video_stream_idx, audio_stream_idx;
    
    //audio
    AVStream            *a_st;
    AVCodecContext      *a_c;
    PacketQueue         *audio_q;
    uint8_t             audio_buf[(MAX_AUDIO_FRAME_SIZE * 3)];
    uint8_t             *audio_pos;
    uint8_t             *audio_end;
    AVFrame             audio_frame;
    AVPacket            audio_pkt;
    SwrContext          *swr_ctx;
    double              audio_clock;
    int                 audio_data_size_ps;
    
    //video
    AVStream            *v_st;
    AVCodecContext      *v_c;
    PacketQueue         *video_q;
    struct SwsContext   *sws_ctx;
    AVFrame             *v_frame;
    FrameInfo           *video_buf[VIDEO_PICTURE_QUEUE_SIZE];
    int                 video_buf_widx, video_buf_ridx;
    int                 video_buf_size;
    SDL_mutex           *p_mutex;
    SDL_cond            *p_cond;
    double              refresh_time;
    double              last_frame_delay;
    double              last_frame_pts;
    double              video_clock;
    
    //thread
    SDL_Thread          *demux_t;
    SDL_Thread          *decode_t;
    
    int                 quit;
    int                 err_code;
    
    //write mutex
    SDL_mutex           *w_mutex;
    SDL_cond            *w_cond;
    
    //SDL
    SDL_Window          *window;
    SDL_Renderer        *renderer;
    SDL_Texture         *texture;
} VideoInfo;

void videoInfo_init(VideoInfo *info);

void videoInfo_destory(VideoInfo *info);

#endif /* video_info_h */
