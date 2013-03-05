#include <glib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <stdint.h>

int main (int argc, char *argv[])
{
    const char *src_filename = argv[1];

    g_message ("Input filename: %s", src_filename);

    av_register_all();

    AVFormatContext *format_context = NULL;
    if (avformat_open_input (&format_context, src_filename, NULL, NULL) < 0)
    {
        g_error ("Could not open input");
    }

    if (avformat_find_stream_info (format_context, NULL) < 0)
    {
        g_critical ("Could not find stream information");
        goto end;
    }

    int video_stream_idx = -1;
    if ((video_stream_idx = av_find_best_stream (format_context,
                                    AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
    {
        g_critical ("Could not find best stream");
        goto end;
    }

    g_debug ("video stream index: %d", video_stream_idx);

    av_dump_format (format_context, 0, src_filename, 0);

    AVCodecContext *codec_context =
            format_context->streams[video_stream_idx]->codec;
    AVCodec *codec = avcodec_find_decoder (codec_context->codec_id);
    if (!codec)
    {
        g_critical ("Could not find codec");
        goto end;
    }

    if (avcodec_open2 (codec_context, codec, NULL) < 0)
    {
        g_critical ("Could not open codec");
        goto end;
    }

    AVFrame *frame = avcodec_alloc_frame ();
    if (!frame)
    {
        g_critical ("Could not allocate frame");
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
            max_size = MAX(max_size, pkt.size);
        }

        av_free_packet (&pkt);
    }

    g_debug ("packet max size: %d", max_size);

end:
    av_free (frame);
    avcodec_close (codec_context);
    avformat_close_input (&format_context);

    return 0;
}
