//
//  video_info.c
//  ffmpeg_proj
//
//  Created by haocheng on 2021/2/2.
//

#include "video_info.h"

void videoInfo_init(VideoInfo *info) {
//AVFrame             audio_frame;
//AVPacket            audio_pkt;
    memset(info->in_filename, 0, sizeof(info->in_filename));
    memset(info->audio_buf, 0, sizeof(info->audio_buf));
    memset(info->video_buf, 0, sizeof(info->video_buf));
    
    info->fmt_ctx = NULL;
    info->has_audio = 0;
    info->has_video = 0;
    info->video_stream_idx = -1;
    info->audio_stream_idx = -1;
    
    //audio
    info->a_st = NULL;
    info->a_c = NULL;
    info->audio_pos = NULL;
    info->audio_end = NULL;
    info->swr_ctx = NULL;
    info->audio_clock = 0.0;
    info->audio_data_size_ps = 0.0;
    
    //video
    info->v_st = NULL;
    info->v_c = NULL;
    info->sws_ctx = NULL;
    info->v_frame = NULL;
    info->video_buf_size = info->video_buf_ridx = info->video_buf_widx = 0;
    info->refresh_time = 0.0;
    info->last_frame_pts = 0.0;
    info->last_frame_delay = 0.0;
    info->video_clock = 0.0;
    
    info->p_mutex = SDL_CreateMutex();
    info->p_cond = SDL_CreateCond();
    info->demux_t = NULL;
    info->decode_t = NULL;
    info->quit = 0;
    info->err_code = 0;
    
    info->w_mutex = SDL_CreateMutex();
    info->w_cond = SDL_CreateCond();
    
    info->video_q = malloc(sizeof(PacketQueue));
    packetQueue_init(info->video_q);
    
    info->audio_q = malloc(sizeof(PacketQueue));
    packetQueue_init(info->audio_q);
    
    //SDL
    info->window = NULL;
    info->renderer = NULL;
    info->texture = NULL;
}

void videoInfo_destory(VideoInfo *info) {
    free(info->audio_q);
    free(info->video_q);
    
//    char                in_filename[1024];
    if (info->fmt_ctx) {
        avformat_close_input(&info->fmt_ctx);
        info->fmt_ctx = NULL;
    }
    
    //audio
    if (info->a_c) {
        avcodec_close(info->a_c);
        avcodec_free_context(&info->a_c);
    }
    if (info->a_st) {
        info->a_st = NULL;
    }
    if (info->audio_q) {
        packetQueue_destory(info->audio_q);
//        free(info->audio_q);//question: free 函数的意义与实现？
        info->audio_q = NULL;
    }
    if (info->audio_pos) {
        info->audio_pos = NULL;
    }
    if (info->audio_end) {
        info->audio_end = NULL;
    }
    if (info->swr_ctx) {
        swr_free(&info->swr_ctx);
    }
    
    //video
    if (info->v_c) {
        avcodec_close(info->v_c);
        avcodec_free_context(&info->v_c);
    }
    if (info->v_st) {
        info->v_st = NULL;
    }
    if (info->video_q) {
        packetQueue_destory(info->video_q);
//        free(info->video_q);//question: free 函数的意义与实现？
        info->video_q = NULL;
    }
    if (info->sws_ctx) {
        sws_freeContext(info->sws_ctx);
        info->sws_ctx = NULL;
    }
    if (info->v_frame) {
        av_frame_free(&info->v_frame);
    }
    if (info->p_mutex) {
        SDL_DestroyMutex(info->p_mutex);
        info->p_mutex = NULL;
    }
    if (info->p_cond) {
        SDL_DestroyCond(info->p_cond);
        info->p_cond = NULL;
    }
    
    //thread
    if (info->demux_t) {
        SDL_WaitThread(info->demux_t, NULL);
        info->demux_t = NULL;
    }
    if (info->decode_t) {
        SDL_WaitThread(info->decode_t, NULL);
        info->decode_t = NULL;
    }
    
    if (info->w_mutex) {
        SDL_DestroyMutex(info->w_mutex);
        info->w_mutex = NULL;
    }
    if (info->w_cond) {
        SDL_DestroyCond(info->w_cond);
        info->w_cond = NULL;
    }
    
    //SDL
    if (info->window) {
        SDL_DestroyWindow(info->window);
        info->window = NULL;
    }
    
    if (info->renderer) {
        SDL_DestroyRenderer(info->renderer);
        info->renderer = NULL;
    }
    
    if (info->texture) {
        SDL_DestroyTexture(info->texture);
        info->texture = NULL;
    }
}
