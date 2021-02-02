//
//  player.c
//  ffmpeg_proj
//
//  Created by HorsonChan on 2021/2/1.
//

#include "player.h"
#include "packet_queue.h"
#include "common.h"
#include "video_info.h"
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>

//SDL event
#define USER_EVENT_VCODEC_READY SDL_USEREVENT + 1
#define USER_REFRESH_FRAME SDL_USEREVENT + 2

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

static int __decode_audio(VideoInfo *info, uint8_t *audio_buf, int buf_size) {
    int rc = 0;
    int len = 0;

    PacketQueue *audio_q = info->audio_q;
    SwrContext *swr_ctx = info->swr_ctx;
    AVCodecContext *c = info->a_c;
    
    AVPacket pkt;
    static AVFrame frame;
    int data_size = 0;

    while (1) {
        if (info->quit) break;
        /* read data from codec */
        rc = avcodec_receive_frame(c, &frame);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
            rc = 0;

        } else if (rc < 0 || rc == AVERROR(EINVAL)) {
            rc = -1;
            av_log(NULL, AV_LOG_ERROR, "codec not opened\n");
            goto __exit;

        } else {
            len = swr_convert(swr_ctx,
                              &audio_buf,
                              buf_size/2/2,
                              (const uint8_t **)frame.data,
                              frame.nb_samples);
            data_size = len * 2 * 2;
            
            /* ensure the audio clock is correct even without pkt.pts */
            info->audio_clock += ((double)data_size / (double)info->audio_data_size_ps);
            return data_size;
        }

        /* get pkt from the audio queue */
        rc = packetQueue_dequeue(audio_q, &pkt, 1, info);
        if (rc < 0) {
            av_log(NULL, AV_LOG_ERROR, "failed to dequeue packet\n");
            goto __exit;
        }

        /* send pkt to decoder */
        rc = avcodec_send_packet(c, &pkt);
        /* record current play pts */
        if (pkt.pts != AV_NOPTS_VALUE) {
            info->audio_clock = av_q2d(info->a_c->time_base) * pkt.pts;
        }
        av_packet_unref(&pkt);
        
    }

__exit:
    return rc;
}

void audio_callback(void *userdata, Uint8 * stream, int len) {
    VideoInfo *info = (VideoInfo *)userdata;
    int read_len = 0;

    //must do, otherwise SDL_MixAudio will boom your head, and this function will always call back, so the audio clock is not accurate
    memset(stream, 0, len);
    
    while (len > 0) {
        
        if (info->quit) break;
        
        if (info->audio_pos >= info->audio_end) {
            read_len = __decode_audio(info,
                                      info->audio_buf,
                                      MAX_AUDIO_FRAME_SIZE * 3);

            if (read_len < 0) {
                av_log(NULL, AV_LOG_ERROR, "error occured when audio codec decode pkt\n");
                return;
            }

            info->audio_pos = info->audio_buf;
            info->audio_end = info->audio_buf + read_len;
        }

        int cur_size = (int)(info->audio_end - info->audio_pos);

        int copy_len = cur_size < len ? cur_size : len;
        SDL_MixAudio(stream, info->audio_pos, copy_len, 50);

        len -= copy_len;
        stream += copy_len;
        info->audio_pos += copy_len;
    }
}

static int open_codec(AVCodec **codec,
                      AVCodecContext **c,
                      AVStream *src_st) {
    int rc = 0;
    CHECK_ERROR(!(*codec =
                  avcodec_find_decoder(src_st->codec->codec_id)),
                "failed to find compatible decoder",
                AVERROR_UNKNOWN,
                __exit);
    
    CHECK_ERROR(!(*c = avcodec_alloc_context3(*codec)),
                "failed to allocate avcodec context",
                AVERROR_UNKNOWN,
                __exit)
    
    CHECK_ERROR((rc = avcodec_copy_context(*c, src_st->codec)),
                "failed to copy codec context to dst codec_ctx",
                0, __exit)
    
    CHECK_ERROR(((rc = avcodec_open2(*c, *codec, NULL)) < 0),
                "failed to open decoder",
                0, __exit)
    
__exit:
    return rc;
}

static int init_audio_component(VideoInfo *info) {
    if (!info->has_audio) return 0;
    
    int rc = 0;
    AVCodec *a_codec = NULL;
    SDL_AudioSpec spec;
    
    //ffmpeg
    info->a_st = info->fmt_ctx->streams[info->audio_stream_idx];
    CHECK_ERROR(((rc = open_codec(&a_codec, &info->a_c, info->a_st)) < 0),
                "open audio codec failed",
                0,
                __exit)
    
    /* after resample sample_fmt == S16, so why 2 below */
    info->audio_data_size_ps = 2 * info->a_c->channels * info->a_c->sample_rate;
    CHECK_ERROR(info->audio_data_size_ps <= 0,
                "caculate audio data size ps error, result <= 0",
                AVERROR_UNKNOWN,
                __exit)
    
    CHECK_ERROR(!(info->swr_ctx = swr_alloc()),
                "failed to allocate swrContext",
                AVERROR_UNKNOWN,
                __exit)
    
    int64_t in_channel_layout = av_get_default_channel_layout(info->a_c->channels);
    int64_t out_channel_layout = in_channel_layout; //AV_CH_LAYOUT_STEREO;
    swr_alloc_set_opts(info->swr_ctx,
                       out_channel_layout,
                       AV_SAMPLE_FMT_S16,
                       info->a_c->sample_rate,
                       in_channel_layout,
                       info->a_c->sample_fmt,
                       info->a_c->sample_rate,
                       0, NULL);
    
    CHECK_ERROR(((rc = swr_init(info->swr_ctx)) < 0),
                "failed to init swrContext",
                0, __exit)
    
    //SDL initialization
    spec.freq = info->a_c->sample_rate;
    spec.format = AUDIO_S16SYS;
    spec.channels = info->a_c->channels;
    spec.silence = 0;
    spec.callback = audio_callback;
    spec.userdata = info;
    spec.samples = SDL_AUDIO_BUFFER_SIZE;
    CHECK_ERROR(((rc = SDL_OpenAudio(&spec, NULL)) < 0),
                "failed to open audio device",
                0, __exit);
    
    SDL_PauseAudio(0);
    
    /* record time of begin audio render, so as video */
    info->refresh_time = av_gettime() / 1000000.0;
    info->last_frame_delay = 40e-3;
__exit:
    return rc;
}


AVFrame *sws_frame_alloc(VideoInfo *info) {
    
    AVFrame *r_vframe = av_frame_alloc();
    r_vframe->width = info->v_c->width;
    r_vframe->height = info->v_c->height;
    r_vframe->format = AV_PIX_FMT_YUV420P;
    av_frame_get_buffer(r_vframe, 0);
    return r_vframe;
}

static double sync_video_clock(VideoInfo *info,
                               AVFrame *frame,
                               double pts) {
    double frame_delay = 0;
    double time_base = 0;
    
    if (pts != 0) {
        info->video_clock = pts;
    } else {
        pts = info->video_clock;
    }
    
    time_base = av_q2d(info->v_st->time_base);
    frame_delay += time_base;
    frame_delay += frame->repeat_pict * time_base * 0.5;
    info->video_clock += pts;
    
    return pts;
}

static int frame_enqueue(AVFrame *frame, VideoInfo *info) {
    int rc = 0;
    double pts = 0;
    
    /* get pts */
    pts = frame->best_effort_timestamp;
    pts *= av_q2d(info->v_st->time_base);
    pts = sync_video_clock(info, frame, pts);
    
    /* waiting if queue is fulling */
    SDL_LockMutex(info->p_mutex);
    while (info->video_buf_size >= VIDEO_PICTURE_QUEUE_SIZE && !info->quit)
        SDL_CondWait(info->p_cond, info->p_mutex);
    SDL_UnlockMutex(info->p_mutex);
    
    /* get resue buffer */
    FrameInfo *frame_info = info->video_buf[info->video_buf_widx];
    if (!frame_info) {
        frame_info = malloc(sizeof(FrameInfo));
        AVFrame *r_vframe = sws_frame_alloc(info);
        frame_info->frame = r_vframe;
    }
    /* record pts */
    frame_info->pts = pts;
    
    /* rescale frame */
    sws_scale(info->sws_ctx,
              (const uint8_t *const *)frame->data,
              frame->linesize,
              0,
              info->v_c->height,
              frame_info->frame->data,
              frame_info->frame->linesize);
    
    /* frame enqueue */
    info->video_buf[info->video_buf_widx++] = frame_info;
    if (info->video_buf_widx >= VIDEO_PICTURE_QUEUE_SIZE)
        info->video_buf_widx = 0;
    
    SDL_LockMutex(info->p_mutex);
    info->video_buf_size++;
    SDL_UnlockMutex(info->p_mutex);
    
    return rc;
}

static int decode_thread(void *data) {
    int rc = 0;
    
    VideoInfo *info = NULL;
    AVPacket pkt;
    
    info = (VideoInfo *)data;
    
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    
    int got_pic = 0;
    while (1) {
        if (info->quit) break;
        
        rc = packetQueue_dequeue(info->video_q, &pkt, 1, info);
        if (rc < 0) break;
        
        avcodec_decode_video2(info->v_c, info->v_frame, &got_pic, &pkt);
        if (got_pic) {
            rc = frame_enqueue(info->v_frame, info);
        }
        
        av_packet_unref(&pkt);
        
        if (rc < 0) break;
    }
    return rc;
}

static int init_video_component(VideoInfo *info) {
    if (!info->has_video) return 0;
    
    int rc = 0;
    AVCodec *v_codec = NULL;
    AVFrame *v_frame = NULL;
    //ffmpeg
    info->v_st = info->fmt_ctx->streams[info->video_stream_idx];
    CHECK_ERROR(((rc = open_codec(&v_codec,
                                  &info->v_c,
                                  info->v_st)) < 0),
                "open video codec failed",
                0,
                __exit)
    
    AVCodecContext *v_c = info->v_c;
    CHECK_ERROR(!(info->sws_ctx = sws_getContext(v_c->width,
                                                 v_c->height,
                                                 v_c->pix_fmt,
                                                 v_c->width,
                                                 v_c->height,
                                                 AV_PIX_FMT_YUV420P,
                                                 SWS_BILINEAR,
                                                 NULL, NULL, NULL)),
                "failed to create swscontext",
                AVERROR_UNKNOWN, __exit)
    
    //!!
    CHECK_ERROR(!(v_frame = av_frame_alloc()),
                "failed to create avframe",
                AVERROR_UNKNOWN,
                __exit)
    info->v_frame = v_frame;
    
    info->decode_t = SDL_CreateThread(decode_thread, "decode_thread", info);
    
    //notified to init SDL video component
    SDL_Event event;
    event.type = USER_EVENT_VCODEC_READY;
    SDL_PushEvent(&event);
    
__exit:
    return rc;
}

static int demux_thread(void *data) {
    int rc = 0;
    VideoInfo *info = (VideoInfo *)data;
    AVPacket pkt;
    
    SDL_Event event;
    
    char *in_filename = info->in_filename;
    
    CHECK_ERROR((rc = avformat_open_input(&info->fmt_ctx,
                                          in_filename,
                                          NULL, NULL)),
                "failed to open input format",
                0, __exit)
    
    CHECK_ERROR(((rc = avformat_find_stream_info(info->fmt_ctx, NULL)) < 0),
                "failed to find stream info for input filed",
                0, __exit)
    
    av_dump_format(info->fmt_ctx, 0, NULL, 0);
        
    for (int i = 0; i < info->fmt_ctx->nb_streams; i++) {
        if (info->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO
            && info->video_stream_idx < 0) {
            info->video_stream_idx = i;
            info->has_video = 1;
        }
        if (info->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO
            && info->audio_stream_idx < 0) {
            info->audio_stream_idx = i;
            info->has_audio = 1;
        }
    }
    CHECK_ERROR(info->video_stream_idx == -1 && info->audio_stream_idx == -1,
                "failed to find video and audio stream",
                AVERROR_UNKNOWN,
                __exit);
    
    //if has video stream
    CHECK_ERROR(((rc = init_video_component(info)) < 0),
                "failed to init video component",
                0,
                __exit)
    
    //if has audio stream
    CHECK_ERROR(((rc = init_audio_component(info)) < 0),
                "failed to init video component",
                0,
                __exit)
    
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    
    while ((rc = av_read_frame(info->fmt_ctx, &pkt)) == 0) {
        if (info->quit) {
            SDL_CondSignal(info->audio_q->cond);
            SDL_CondSignal(info->video_q->cond);
            break;
        };
        
//        if (info->audio_q->size > MAX_AUDIOQ_SIZE || info->video_q->size > MAX_VIDEOQ_SIZE) {
////            SDL_Delay(10);
//            continue;
//        }
        
        if (pkt.stream_index == info->audio_stream_idx) {
            packetQueue_enqueue(info->audio_q, &pkt);
        } else if (pkt.stream_index == info->video_stream_idx) {
            packetQueue_enqueue(info->video_q, &pkt);
        }
        av_packet_unref(&pkt);
    }
    
    rc = 0;
    while (!info->quit) {
        SDL_Delay(200);
    }
    
__exit:
    
    SDL_LockMutex(info->w_mutex);
    
    info->err_code = rc;
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    
    SDL_UnlockMutex(info->w_mutex);
    
    return rc;
}

static int init_video_SDL_component(VideoInfo *info, int width, int height) {
    int rc = 0;
    CHECK_ERROR(!(info->window = SDL_CreateWindow("video player",
                                                  SDL_WINDOWPOS_UNDEFINED,
                                                  SDL_WINDOWPOS_UNDEFINED,
                                                  width, height,
                                                  SDL_WINDOW_OPENGL |
                                                  SDL_WINDOW_RESIZABLE)),
                "failed to create SDL window",
                AVERROR_UNKNOWN,
                __exit)
    
    CHECK_ERROR(!(info->renderer = SDL_CreateRenderer(info->window, -1, 0)),
                "failed to create SDL renderer",
                AVERROR_UNKNOWN,
                __exit)

    CHECK_ERROR(!(info->texture = SDL_CreateTexture(info->renderer,
                                                    SDL_PIXELFORMAT_IYUV,
                                                    SDL_TEXTUREACCESS_STREAMING,
                                                    width, height)),
                "failed to create SDL texture",
                AVERROR_UNKNOWN,
                __exit)
    
__exit:
    return rc;
}

static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
  SDL_Event event;
  event.type = USER_REFRESH_FRAME;
  event.user.data1 = opaque;
  SDL_PushEvent(&event);
  return 0; /* 0 means stop timer */
}

static void schedule_refresh(VideoInfo *info, int delay) {
    SDL_AddTimer(delay, sdl_refresh_timer_cb, info);
}

static void render_frame(VideoInfo *info, AVFrame *frame) {
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = info->v_c->width;
    rect.h = info->v_c->height;
    
    //renderer frame
    SDL_UpdateYUVTexture(info->texture, &rect,
                         frame->data[0],
                         frame->linesize[0],
                         frame->data[1],
                         frame->linesize[1],
                         frame->data[2],
                         frame->linesize[2]);
    
    SDL_RenderClear(info->renderer);
    SDL_RenderCopy(info->renderer, info->texture, NULL, NULL);
    SDL_RenderPresent(info->renderer);
    
}

static double get_audio_cur_time(VideoInfo *info) {
    double cur_audio_time = info->audio_clock;
    size_t remain_buf_size = info->audio_end - info->audio_pos;
    if (remain_buf_size) {
        cur_audio_time -= (double)remain_buf_size / (double)info->audio_data_size_ps;
    }
    return cur_audio_time;
}

static void refresh_frame(void *userdata) {
    VideoInfo *info = (VideoInfo *)userdata;
    double delay, audio_clock, rel_diff, threshold_delay, actual_delay = 0.0;
    
    if (info->quit) {
        SDL_CondSignal(info->p_cond);
        return;
    }
    
    if (info->has_video) {
        if (!info->video_buf_size) {
            schedule_refresh(info, 1);
        } else {
            
            FrameInfo *frame_info = info->video_buf[info->video_buf_ridx++];
            AVFrame *frame = frame_info->frame;
            
            double pts = frame_info->pts;
            
            delay = pts - info->last_frame_pts;
            if (delay <= 0 || delay > 1.0) {
                /* incorrect delay */
                delay = info->last_frame_delay;
            }
            
            info->last_frame_pts = pts;
            info->last_frame_delay = delay;
            
            audio_clock = get_audio_cur_time(info);
            /* get real diff, see how good effect last predict is */
            rel_diff = pts - audio_clock;
            
            threshold_delay = delay > AV_SYNC_THRESHOLD ? delay : AV_SYNC_THRESHOLD;
            
            if (rel_diff <= -threshold_delay) {
                delay = 0;
            } else if (rel_diff >= threshold_delay) {
                delay = 2 * delay;
            }
            
            if (delay < 0.01) {
                delay = 0.01;
            }
            
            schedule_refresh(info, (int)(delay * 1000));
            
            render_frame(info, frame);
            if (info->video_buf_ridx >= VIDEO_PICTURE_QUEUE_SIZE)
                info->video_buf_ridx = 0;
            
            SDL_LockMutex(info->p_mutex);
            info->video_buf_size--;
            SDL_CondSignal(info->p_cond);
            SDL_UnlockMutex(info->p_mutex);
        }
    } else {
        schedule_refresh(info, 200);
    }
}

static int start_playing(char *in_filename) {
    int rc = 0;
    int thread_error_code = 0;
    VideoInfo *video_info = NULL;
    
    //init core struct
    video_info = malloc(sizeof(VideoInfo));
    videoInfo_init(video_info);
    
    strcpy(video_info->in_filename, in_filename);
    
    //init SDL
    CHECK_ERROR((SDL_Init(SDL_INIT_VIDEO)),
                "failed to init SDL",
                AVERROR_UNKNOWN,
                __exit);
    
    video_info->demux_t = SDL_CreateThread(demux_thread, "demux_thread", video_info);
    
    //init video SDL component (must be initialized in main thread)
    if (video_info->v_c) {
        init_video_SDL_component(video_info, video_info->v_c->width, video_info->v_c->height);
    }
    
    for(;;) {
        SDL_Event event;
        SDL_WaitEvent(&event);
        switch(event.type) {
                
            case SDL_QUIT:
                fprintf(stderr, "receive a QUIT event: %d\n", event.type);
                video_info->quit = 1;
                goto __exit;
                
            case USER_EVENT_VCODEC_READY:
                init_video_SDL_component(video_info, video_info->v_c->width, video_info->v_c->height);
                schedule_refresh(video_info, 40);
                break;
                
            case USER_REFRESH_FRAME:
                refresh_frame(event.user.data1);
                break;
            default:
                break;
      }
    }
    
__exit:
    
    thread_error_code = video_info->err_code;
    
    if (video_info) {
        videoInfo_destory(video_info);
        free(video_info);
        video_info = NULL;
    }
    
    SDL_Quit();
    
    RETURN_ERROR_CHECK(thread_error_code)
    RETURN_ERROR_CHECK(rc)
    
    return rc;
}

void start_play_real_video(void) {
    start_playing("");
}

#if 0
int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage command: <in_filename>");
        return -1;
    }
    char *in_filename = argv[1];
    start_playing(in_filename);
    return 0;
}
#endif
