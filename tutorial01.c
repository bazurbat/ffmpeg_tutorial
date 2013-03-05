#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
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

    FILE *file;
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
            av_image_copy(ctx->data, ctx->linesize,
                          (const uint8_t **)frame->data, frame->linesize,
                          codec->pix_fmt, codec->width, codec->height);

            fwrite(ctx->data[0], 1, ctx->bufsize, ctx->file);
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf ("Usage: %s <src_filename> <dst_filename>\n", argv[0]);
        return -1;
    }

    const char *src_filename = argv[1];
    const char *dst_filename = argv[2];

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

    ctx.bufsize = av_image_alloc (ctx.data, ctx.linesize,
                                  ctx.codec->width, ctx.codec->height,
                                  ctx.codec->pix_fmt, 1);
    if (ctx.bufsize < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not allocate video buffer\n");
        goto end;
    }

    ctx.file = fopen (dst_filename, "wb");
    if (!ctx.file)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open output file\n");
        goto end;
    }

    AVPacket pkt =
    {
        .data = NULL,
        .size = 0
    };

    while (av_read_frame (format_ctx, &pkt) >= 0)
    {
        process_packet (&pkt, &ctx);

        av_free_packet (&pkt);
    }

    // flush cached frames
    pkt.data = NULL;
    pkt.size = 0;
    do
    {
        process_packet(&pkt, &ctx);
    }
    while (ctx.got_frame);

    av_log (NULL, AV_LOG_INFO, "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n",
            av_get_pix_fmt_name (ctx.codec->pix_fmt),
            ctx.codec->width, ctx.codec->height, dst_filename);

end:
    av_free_packet (&pkt);
    av_free (ctx.frame);
    av_free (ctx.data[0]);
    if (ctx.file)
        fclose (ctx.file);
    if (ctx.codec)
        avcodec_close (ctx.codec);
    if (format_ctx)
        avformat_close_input (&format_ctx);

    return 0;
}
