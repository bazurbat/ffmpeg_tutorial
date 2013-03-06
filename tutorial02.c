#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stdint.h>

typedef struct
{
    AVCodecContext *codec;
    int stream_index;

    AVFrame *frame;
    int got_frame;

    uint8_t *data[4];
    int linesize[4];
    int bufsize;

    SDL_Rect rect;
    SDL_Overlay *overlay;
} DecodingContext;

static int process_packet(AVPacket *pkt, DecodingContext *ctx)
{
    AVCodecContext *codec = ctx->codec;
    AVFrame *frame = ctx->frame;

    if (pkt->stream_index == ctx->stream_index)
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
        }
    }

    return 0;
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

    AVCodec *decoder = NULL;
    ctx.stream_index = av_find_best_stream (format_ctx, AVMEDIA_TYPE_VIDEO,
                                            -1, -1, &decoder, 0);
    if (ctx.stream_index < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find the best stream\n");
        goto end;
    }

    ctx.codec = format_ctx->streams[ctx.stream_index]->codec;
    if (avcodec_open2 (ctx.codec, decoder, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open codec\n");
        goto end;
    }

    ctx.frame = avcodec_alloc_frame ();
    if (!ctx.frame)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not allocate video frame\n");
        goto end;
    }

    SDL_Surface *screen = SDL_SetVideoMode (ctx.codec->width, ctx.codec->height,
                                            0, 0);
    if (!screen)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not create screen: %s\n",
                SDL_GetError ());
        goto end;
    }

    ctx.overlay = SDL_CreateYUVOverlay (
                ctx.codec->width, ctx.codec->height, SDL_YV12_OVERLAY, screen);

    av_log (NULL, AV_LOG_INFO, "YUV overlay: format=%u, w=%d, h=%d, planes=%d\n",
             ctx.overlay->format, ctx.overlay->w, ctx.overlay->h,
             ctx.overlay->planes);

    ctx.rect.x = 0;
    ctx.rect.y = 0;
    ctx.rect.w = ctx.codec->width;
    ctx.rect.h = ctx.codec->height;

    AVPacket pkt =
    {
        .data = NULL,
        .size = 0
    };

    SDL_Event event;

    while (av_read_frame (format_ctx, &pkt) >= 0)
    {
        process_packet (&pkt, &ctx);

        av_free_packet (&pkt);

        SDL_PollEvent (&event);
        switch (event.type) {
        case SDL_QUIT:
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
    if (ctx.codec)
        avcodec_close (ctx.codec);
    if (format_ctx)
        avformat_close_input (&format_ctx);
    SDL_Quit ();

    return 0;
}
