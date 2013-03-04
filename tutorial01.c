#include <glib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

int main (int argc, char *argv[])
{
    const char *src_filename = argv[1];

    g_message ("Input filename: %s", src_filename);

    av_register_all();

    AVFormatContext *fmt_ctx = NULL;

    if (avformat_open_input (&fmt_ctx, src_filename, NULL, NULL) < 0)
    {
        g_error ("Could not open input");
        exit (1);
    }

    if (avformat_find_stream_info (fmt_ctx, NULL) < 0)
    {
        g_error ("Could not find stream information");
        goto end;
    }

    int video_stream_idx = -1;
    if ((video_stream_idx = av_find_best_stream (fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0)
    {
        g_error ("Could not find best stream");
        goto end;
    }

    g_debug ("video stream index: %d", video_stream_idx);

    av_dump_format (fmt_ctx, 0, src_filename, 0);

    AVFrame *frame = avcodec_alloc_frame ();
    if (!frame)
    {
        g_error ("Could not allocate frame");
        goto end;
    }

    AVPacket pkt;
    int max_size = -1;
    while (av_read_frame(fmt_ctx, &pkt) >= 0)
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
    avformat_close_input (&fmt_ctx);

    return 0;
}
