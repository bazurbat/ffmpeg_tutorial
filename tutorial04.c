#include "packet_queue.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define SDL_AUDIO_BUFFER_SIZE 1024

int quit = 0;

void signal_handler (int signum)
{
    quit = 1;
}

typedef struct PlayerContext
{
    AVFormatContext *format_ctx;

    AVCodecContext *video_codec;
    int video_stream_index;

    AVCodecContext *audio_codec;
    int audio_stream_index;

    PacketQueue audioq;

    AVFrame *frame;
    int got_frame;

    AVPacket *pkt;

    uint8_t *data[4];
    int linesize[4];
    int bufsize;

    SDL_Rect rect;
    SDL_Overlay *overlay;
} PlayerContext;

static int process_packet(AVPacket *pkt, PlayerContext *ctx)
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
        packet_queue_put (&ctx->audioq, pkt);
    }
    else
    {
        av_free_packet (pkt);
    }

    return 0;
}

int decode_audio_frame (PlayerContext *ctx, uint8_t **buf)
{
    static AVPacket pkt, cur_pkt;
    static AVFrame *frame;
    int got_frame, decoded_bytes;

    if (!frame)
    {
        frame = avcodec_alloc_frame ();
        if (!frame)
            return AVERROR (ENOMEM);
    }

    for (;;)
    {
        while (pkt.size > 0)
        {
            avcodec_get_frame_defaults (frame);

            decoded_bytes = avcodec_decode_audio4 (ctx->audio_codec,
                                                   frame, &got_frame,
                                                   &pkt);
            if (decoded_bytes < 0)
            {
                // error, skip the frame
                pkt.size = 0;
                break;
            }

            pkt.data += decoded_bytes;
            pkt.size -= decoded_bytes;

            *buf = frame->data[0];

            return av_samples_get_buffer_size(NULL,
                                              frame->channels,
                                              frame->nb_samples,
                                              frame->format, 1);
        }

        // free the current packet
        if (cur_pkt.data)
            av_free_packet (&cur_pkt);
        memset (&pkt, 0, sizeof (pkt));

        if (quit)
            return -1;

        // read next packet
        if (packet_queue_get (&ctx->audioq, &cur_pkt, 1) < 0)
            return -1;

        pkt = cur_pkt;
    }
}

void audio_callback (void *userdata, Uint8 *stream, int len)
{
    static uint8_t silence_buf[SDL_AUDIO_BUFFER_SIZE] = {0};
    static uint8_t *buf = NULL;
    static size_t buf_size = 0;
    static size_t buf_index = 0;

    PlayerContext *ctx = userdata;
    int send_len, decoded_size;

    while (len > 0 && !quit)
    {
        if (buf_index >= buf_size)
        {
            decoded_size = decode_audio_frame (ctx, &buf);
            if (decoded_size < 0)
            {
                av_log (NULL, AV_LOG_WARNING, "Could not decode audio frame");
                buf = silence_buf;
                buf_size = sizeof silence_buf;
            }
            else
            {
                buf_size = decoded_size;
            }

            buf_index = 0;
        }

        send_len = buf_size - buf_index;

        if (send_len > len)
        {
            send_len = len;
        }

        memcpy (stream, buf + buf_index, send_len);
        len -= send_len;
        stream += send_len;
        buf_index += send_len;
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf ("Usage: %s <filename>\n", argv[0]);
        return -1;
    }

    signal (SIGINT, signal_handler);
    signal (SIGTERM, signal_handler);

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

    PlayerContext ctx = {0};

    AVCodec *video_decoder = NULL;
    ctx.video_stream_index = av_find_best_stream (format_ctx,
                                AVMEDIA_TYPE_VIDEO, -1, -1, &video_decoder, 0);
    if (ctx.video_stream_index < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find the best video stream\n");
        goto end;
    }

    av_log (NULL, AV_LOG_INFO, "Using video stream: %d\n",
            ctx.video_stream_index);

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

    av_log (NULL, AV_LOG_INFO, "Using audio stream: %d\n",
            ctx.audio_stream_index);

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

    if (SDL_OpenAudio (&desired_spec, &audio_spec) < 0)
    {
        av_log (NULL, AV_LOG_INFO, "Could not open audio device: %s\n",
                SDL_GetError ());
        goto end;
    }

    packet_queue_init (&ctx.audioq);
    SDL_PauseAudio (0);

    AVPacket pkt =
    {
        .data = NULL,
        .size = 0
    };

    SDL_Event event;

    while ((av_read_frame (format_ctx, &pkt) >= 0) && !quit)
    {
        process_packet (&pkt, &ctx);

        SDL_PollEvent (&event);
        switch (event.type) {
        case SDL_QUIT:
            packet_queue_stop (&ctx.audioq);
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
    while (ctx.got_frame && !quit);

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

