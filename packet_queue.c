//
//  packet_queue.c
//  ffmpeg_proj
//
//  Created by HorsonChan on 2021/2/1.
//

#include "packet_queue.h"
#include "video_info.h"

void packetQueue_init(PacketQueue *queue) {
    memset(queue, 0, sizeof(PacketQueue));
    queue->mutex = SDL_CreateMutex();
    queue->cond = SDL_CreateCond();
}

int packetQueue_enqueue(PacketQueue *q, AVPacket *pkt) {
    int rc = 0;
    AVPacketList *node = malloc(sizeof(AVPacketList));
    if (!node) {
        rc = AVERROR_UNKNOWN;
        av_log(NULL, AV_LOG_ERROR, "failed to allocate AVPacket memory\n");
        goto __exit;
    }
    node->next = NULL;

    rc = av_packet_ref(&node->pkt, pkt);
    if (rc != 0) {
        av_log(NULL,
               AV_LOG_ERROR,
               "failed to copy avpacket ref for pkt enqueue\n");
        goto __exit;
    }

    SDL_LockMutex(q->mutex);

    if (!q->last_pkt) {
        q->first_pkt = node;
    } else {
        q->last_pkt->next = node;
    }

    q->last_pkt = node;
    q->nb_packets++;
    q->size += node->pkt.size;
    SDL_CondSignal(q->cond);

//    printf("[demo log] enqueue pkt size: %d, pkt pts: %lld\n", node->pkt.size, node->pkt.pts);

    SDL_UnlockMutex(q->mutex);

__exit:
    return rc;
}

int packetQueue_dequeue(PacketQueue *q, AVPacket *pkt, int block, void *userdata) {
    int rc = 0;
    AVPacketList *node = NULL;
    SDL_LockMutex(q->mutex);
    
    VideoInfo *info = (VideoInfo *)userdata;

    while (1) {
        
        if (info->quit) {
            break;
        }
        
        node = q->first_pkt;
        if (node) {
            q->first_pkt = node->next;
            if (!q->first_pkt) {
                q->last_pkt = NULL;
            }
            q->nb_packets--;
            q->size -= node->pkt.size;

            rc = av_packet_ref(pkt, &node->pkt);
            if (rc != 0) {
                av_log(NULL,
                       AV_LOG_ERROR,
                       "failed to copy avpacket ref for pkt dequeue\n");
            }

//            printf("[demo log] dequeue pkt size: %d, pkt pts: %lld\n", pkt->size, pkt->pts);
            av_packet_unref(&node->pkt);
            av_free(node);
            break;
        } else if (block) {
            SDL_CondWait(q->cond, q->mutex);
        } else if (!block) {
            break;
        }
    }
    
    SDL_UnlockMutex(q->mutex);
    return rc;
}

int packetQueue_destory(PacketQueue *q) {
    return 0;
}
