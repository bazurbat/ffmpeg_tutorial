#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <stdbool.h>

typedef struct AVPacket AVPacket;
typedef struct AVPacketList AVPacketList;
typedef struct SDL_mutex SDL_mutex;
typedef struct SDL_cond SDL_cond;

typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    bool stop_request;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

void packet_queue_init (PacketQueue *q);

int packet_queue_put (PacketQueue *q, AVPacket *pkt);

int packet_queue_get (PacketQueue *q, AVPacket *pkt, bool block);

void packet_queue_stop (PacketQueue *q);

#endif // PACKET_QUEUE_H
