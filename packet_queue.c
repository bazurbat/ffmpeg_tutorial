#include "packet_queue.h"
#include <libavformat/avformat.h>
#include <SDL.h>
#include <SDL_thread.h>

void packet_queue_init (PacketQueue *q)
{
    memset (q, 0, sizeof *q);
    q->stop_request = false;
    q->mutex = SDL_CreateMutex ();
    q->cond = SDL_CreateCond ();
}

int packet_queue_put (PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *node;

    if (q->stop_request)
        return -1;

    if (av_dup_packet (pkt) < 0)
        return -1;

    SDL_LockMutex(q->mutex);

    node = av_malloc (sizeof *node);
    if (!node)
        return -1;

    node->pkt = *pkt;
    node->next = NULL;

    if (!q->last_pkt)
        q->first_pkt = node;
    else
        q->last_pkt->next = node;

    q->last_pkt = node;
    q->nb_packets++;
    q->size += node->pkt.size;

    SDL_CondSignal (q->cond);
    SDL_UnlockMutex (q->mutex);

    return 0;
}

int packet_queue_get (PacketQueue *q, AVPacket *pkt, bool block)
{
    AVPacketList *node;
    int ret;

    SDL_LockMutex (q->mutex);

    for (;;)
    {
        if (q->stop_request)
        {
            ret = -1;
            break;
        }

        node = q->first_pkt;
        if (node)
        {
            q->first_pkt = node->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= node->pkt.size;
            *pkt = node->pkt;
            av_free(node);
            ret = 1;
            break;
        }
        else if (!block)
        {
            ret = 0;
            break;
        }
        else
        {
            SDL_CondWait (q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex (q->mutex);

    return ret;
}

void packet_queue_flush (PacketQueue *q)
{
    AVPacketList *node, *next_node;

    SDL_LockMutex (q->mutex);

    for (node = q->first_pkt; node != NULL; node = next_node)
    {
        next_node = node->next;
        av_free_packet (&node->pkt);
        av_freep (&node);
    }

    q->first_pkt = NULL;
    q->last_pkt = NULL;
    q->nb_packets = 0;
    q->size = 0;

    SDL_UnlockMutex (q->mutex);
}

void packet_queue_stop (PacketQueue *q)
{
    SDL_LockMutex (q->mutex);

    q->stop_request = true;

    SDL_CondSignal (q->cond);
    SDL_UnlockMutex (q->mutex);
}
