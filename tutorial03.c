#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SDL_AUDIO_BUFFER_SIZE 1024

int quit = 0;

typedef struct DecodingContext
{
    AVCodecContext *video_codec;
    int video_stream_index;

    AVCodecContext *audio_codec;
    int audio_stream_index;

    AVFrame *frame;
    int got_frame;

    AVPacket *pkt;

    uint8_t *data[4];
    int linesize[4];
    int bufsize;

    SDL_Rect rect;
    SDL_Overlay *overlay;
} DecodingContext;

typedef struct PacketQueue
{
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

PacketQueue audioq;

void packet_queue_init (PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;
    if(av_dup_packet(pkt) < 0) {
        return -1;
    }
    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;


    SDL_LockMutex(q->mutex);

    if (!q->last_pkt)
        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;
    SDL_CondSignal(q->cond);

    SDL_UnlockMutex(q->mutex);
    return 0;
}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    SDL_LockMutex(q->mutex);

    for(;;)
    {
        if(quit)
        {
            ret = -1;
            break;
        }

        pkt1 = q->first_pkt;
        if (pkt1)
        {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            av_free(pkt1);
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
            SDL_CondWait(q->cond, q->mutex);
        }
    }

    SDL_UnlockMutex(q->mutex);

    return ret;
}

static int process_packet(AVPacket *pkt, DecodingContext *ctx)
{
    AVCodecContext *codec = ctx->video_codec;
    AVFrame *frame = ctx->frame;

    if (pkt->stream_index == ctx->video_stream_index)
    {
        if (avcodec_decode_video2(codec, frame, &ctx->got_frame, pkt) < 0)
        {
            av_log (NULL, AV_LOG_ERROR, "Error decoding video frame\n");
            return -1;
        }

        if (ctx->got_frame)
        {
            SDL_LockYUVOverlay (ctx->overlay);

            AVPicture pict;
            pict.data[0] = ctx->overlay->pixels[0];
            pict.data[1] = ctx->overlay->pixels[2];
            pict.data[2] = ctx->overlay->pixels[1];

            pict.linesize[0] = ctx->overlay->pitches[0];
            pict.linesize[1] = ctx->overlay->pitches[2];
            pict.linesize[2] = ctx->overlay->pitches[1];

            av_image_copy(pict.data, pict.linesize,
                          (const uint8_t **)frame->data, frame->linesize,
                          codec->pix_fmt, codec->width, codec->height);

            SDL_UnlockYUVOverlay (ctx->overlay);
            SDL_DisplayYUVOverlay (ctx->overlay, &ctx->rect);

            av_free_packet (pkt);
        }
    }
    else if (pkt->stream_index == ctx->audio_stream_index)
    {
        packet_queue_put (&audioq, pkt);
    }
    else
    {
        av_free_packet (pkt);
    }

    return 0;
}

int audio_decode_frame (AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size)
{
    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;

    int len1, data_size;

    for (;;)
    {
        while (audio_pkt_size > 0)
        {
            data_size = buf_size;
            len1 = avcodec_decode_audio2 (aCodecCtx, (int16_t *)audio_buf, &data_size,
                                          audio_pkt_data, audio_pkt_size);
            if (len1 < 0)
            {
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }

            audio_pkt_data += len1;
            audio_pkt_size -= len1;

            if (data_size <= 0)
            {
                /* No data yet, get more frames */
                continue;
            }

            /* We have data, return it and come back for more later */
            return data_size;
        }

        if (pkt.data)
        {
            av_free_packet(&pkt);
        }

        if (quit)
        {
            return -1;
        }

        if (packet_queue_get(&audioq, &pkt, 1) < 0)
        {
            return -1;
        }

        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}

void audio_callback (void *userdata, Uint8 *stream, int len)
{
    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while (len > 0)
    {
        if (audio_buf_index >= audio_buf_size)
        {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame (aCodecCtx, audio_buf, sizeof(audio_buf));
            if (audio_size < 0)
            {
                /* If error, output silence */
                audio_buf_size = 1024; // arbitrary?
                memset(audio_buf, 0, audio_buf_size);
            }
            else
            {
                audio_buf_size = audio_size;
            }

            audio_buf_index = 0;
        }

        len1 = audio_buf_size - audio_buf_index;

        if (len1 > len)
        {
            len1 = len;
        }

        memcpy (stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf ("Usage: %s <filename>\n", argv[0]);
        return -1;
    }

    const char *src_filename = argv[1];

    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        av_log (NULL, AV_LOG_ERROR, "Could not initialize SDL: %s\n",
                SDL_GetError ());
        return -1;
    }

    av_register_all();

    AVFormatContext *format_ctx = NULL;
    if (avformat_open_input (&format_ctx, src_filename, NULL, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open input file\n");
        return -1;
    }

    if (avformat_find_stream_info (format_ctx, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto end;
    }

    av_dump_format (format_ctx, 0, src_filename, 0);

    DecodingContext ctx = {0};

    AVCodec *video_decoder = NULL;
    ctx.video_stream_index = av_find_best_stream (format_ctx,
                                AVMEDIA_TYPE_VIDEO, -1, -1, &video_decoder, 0);
    if (ctx.video_stream_index < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find the best video stream\n");
        goto end;
    }

    ctx.video_codec = format_ctx->streams[ctx.video_stream_index]->codec;
    if (avcodec_open2 (ctx.video_codec, video_decoder, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open video codec\n");
        goto end;
    }

    AVCodec *audio_decoder = NULL;
    ctx.audio_stream_index = av_find_best_stream (format_ctx,
                                AVMEDIA_TYPE_AUDIO, -1, -1, &audio_decoder, 0);
    if (ctx.audio_stream_index < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find the best audio stream\n");
        goto end;
    }

    ctx.audio_codec = format_ctx->streams[ctx.audio_stream_index]->codec;
    if (avcodec_open2 (ctx.audio_codec, audio_decoder, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open audio codec\n");
        goto end;
    }

    ctx.frame = avcodec_alloc_frame ();
    if (!ctx.frame)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto end;
    }

    SDL_Surface *screen = SDL_SetVideoMode (ctx.video_codec->width, ctx.video_codec->height,
                                            0, 0);
    if (!screen)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not create screen: %s\n",
                SDL_GetError ());
        goto end;
    }

    ctx.overlay = SDL_CreateYUVOverlay (
                ctx.video_codec->width, ctx.video_codec->height, SDL_YV12_OVERLAY, screen);

    av_log (NULL, AV_LOG_INFO, "YUV overlay: format=%u, w=%d, h=%d, planes=%d\n",
             ctx.overlay->format, ctx.overlay->w, ctx.overlay->h,
             ctx.overlay->planes);

    ctx.rect.x = 0;
    ctx.rect.y = 0;
    ctx.rect.w = ctx.video_codec->width;
    ctx.rect.h = ctx.video_codec->height;

    SDL_AudioSpec desired_spec =
    {
        .freq = ctx.audio_codec->sample_rate,
        .format = AUDIO_S16SYS,
        .channels = ctx.audio_codec->channels,
        .samples = SDL_AUDIO_BUFFER_SIZE,
        .callback = audio_callback,
        .userdata = &ctx
    };

    SDL_AudioSpec audio_spec = {0};

    if (!SDL_OpenAudio (&desired_spec, &audio_spec))
    {
        av_log (NULL, AV_LOG_INFO, "Could not open audio device: %s\n",
                SDL_GetError ());
        goto end;
    }

    packet_queue_init (&audioq);
    SDL_PauseAudio (0);

    AVPacket pkt =
    {
        .data = NULL,
        .size = 0
    };

    SDL_Event event;

    while (av_read_frame (format_ctx, &pkt) >= 0)
    {
        process_packet (&pkt, &ctx);

        SDL_PollEvent (&event);
        switch (event.type) {
        case SDL_QUIT:
            quit = 1;
            break;
        default:
            break;
        }
    }

    // flush cached frames
    pkt.data = NULL;
    pkt.size = 0;
    do
    {
        process_packet(&pkt, &ctx);
    }
    while (ctx.got_frame);

end:
    av_free_packet (&pkt);
    av_free (ctx.frame);
    av_free (ctx.data[0]);
    if (ctx.video_codec)
        avcodec_close (ctx.video_codec);
    if (format_ctx)
        avformat_close_input (&format_ctx);
    SDL_CloseAudio ();
    SDL_Quit ();

    return 0;
}
