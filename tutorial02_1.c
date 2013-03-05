#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <SDL.h>
#include <SDL_thread.h>
#include <stdint.h>
#include <stdlib.h>

int main (int argc, char *argv[])
{
    const char *src_filename = argv[1];

    av_log (NULL, AV_LOG_INFO, "Input filename: %s", src_filename);

    av_register_all();

    AVFormatContext *format_context = NULL;
    if (avformat_open_input (&format_context, src_filename, NULL, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open input");
        exit (1);
    }

    if (avformat_find_stream_info (format_context, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find stream information");
        goto end;
    }

    int video_stream_idx = -1;
    if ((video_stream_idx = av_find_best_stream (format_context,
                                    AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find best stream");
        goto end;
    }

    av_dlog (NULL, "video stream index: %d", video_stream_idx);

    av_dump_format (format_context, 0, src_filename, 0);

    AVCodecContext *codec_context =
            format_context->streams[video_stream_idx]->codec;
    AVCodec *codec = avcodec_find_decoder (codec_context->codec_id);
    if (!codec)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not find codec");
        goto end;
    }

    if (avcodec_open2 (codec_context, codec, NULL) < 0)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not open codec");
        goto end;
    }

    AVFrame *frame = avcodec_alloc_frame ();
    if (!frame)
    {
        av_log (NULL, AV_LOG_ERROR, "Could not allocate frame");
        goto end;
    }

    int num_bytes = avpicture_get_size (PIX_FMT_RGB24, codec_context->width,
                                        codec_context->height);
    uint8_t *buffer = av_malloc (num_bytes * sizeof (*buffer));

    AVPacket pkt;
    int max_size = -1;
    while (av_read_frame(format_context, &pkt) >= 0)
    {
        if (pkt.stream_index == video_stream_idx)
        {
            //            sapi_debug ("packet: pts=%lld, dts=%lld, duration=%d, size=%d",
            //                        pkt.pts, pkt.dts, pkt.duration, pkt.size);
//            max_size = MAX(max_size, pkt.size);
        }

        av_free_packet (&pkt);
    }

    av_dlog (NULL, "packet max size: %d", max_size);

end:
    av_free (frame);
    avcodec_close (codec_context);
    avformat_close_input (&format_context);

    return 0;
}
