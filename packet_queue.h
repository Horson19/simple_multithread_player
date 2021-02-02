//
//  packet_queue.h
//  ffmpeg_proj
//
//  Created by HorsonChan on 2021/2/1.
//

#ifndef packet_queue_h
#define packet_queue_h

#include <stdio.h>
#include <SDL.h>
#include <libavformat/avformat.h>

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

void packetQueue_init(PacketQueue *queue);
int packetQueue_enqueue(PacketQueue *q, AVPacket *pkt);
int packetQueue_dequeue(PacketQueue *q, AVPacket *pkt, int block, void *userdata);
int packetQueue_destory(PacketQueue *q);
#endif /* packet_queue_h */
