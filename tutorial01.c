#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <stdio.h>

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf ("Please provide a movie file\n");
        return -1;
    }

    const char *filename = argv[1];

    av_register_all();

    AVFormatContext *format_context = NULL;
    if (avformat_open_input (&format_context, filename, NULL, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open input file\n");
        return -1;
    }

    if (avformat_find_stream_info (format_context, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find stream information\n");
        goto end;
    }

    av_dump_format (format_context, 0, argv[1], 0);

    int video_stream_idx = -1;
    if ((video_stream_idx = av_find_best_stream (format_context,
                                    AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find best stream");
        goto end;
    }

    AVCodecContext *codec_ctx =
            format_context->streams[video_stream_idx]->codec;

    AVCodec *codec = avcodec_find_decoder (codec_ctx->codec_id);
    if (!codec)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find codec\n");
        goto end;
    }

    if (avcodec_open2 (codec_ctx, codec, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open codec\n");
        goto end;
    }

    AVFrame *frame = avcodec_alloc_frame ();

    uint8_t *video_dst_data[4] = {0};
    int video_dst_linesize[4] = {0};
    int ret = av_image_alloc (video_dst_data, video_dst_linesize,
                              codec_ctx->width, codec_ctx->height,
                              codec_ctx->pix_fmt, 1);
    if (ret < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not allocate image\n");
        goto end;
    }

    int video_dst_bufsize = ret;
    AVPacket packet;
    int got_frame;
    int i=0;
    while (av_read_frame (format_context, &packet) >= 0)
    {
        if(packet.stream_index == video_stream_idx)
        {
            avcodec_decode_video2 (codec_ctx, frame, &got_frame, &packet);

            if(got_frame)
            {
                av_image_copy (video_dst_data, video_dst_linesize,
                              (const uint8_t **)(frame->data), frame->linesize,
                              codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height);

                if(++i<=5)
                {
//                    SaveFrame(pFrameRGB, codec_ctx->width, codec_ctx->height, i);
                }
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet (&packet);
    }

end:

    // Free the RGB image
//    av_free(buffer);
//    av_free(pFrameRGB);

    if (frame)
        av_free(frame);
    if (codec_ctx)
        avcodec_close(codec_ctx);
    if (format_context)
        avformat_close_input (&format_context);

    return 0;
}
