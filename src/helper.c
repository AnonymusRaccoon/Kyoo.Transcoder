#include <stdio.h>
#include "helper.h"
#include "stream.h"

int open_input_context(AVFormatContext **in_ctx, const char *path)
{
	if (avformat_open_input(in_ctx, path, NULL, NULL)) {
		fprintf(stderr, "Error: Can't open the file at %s.\n", path);
		return 1;
	}
	if (avformat_find_stream_info(*in_ctx, NULL) < 0) {
		fprintf(stderr,"Error: Could't find streams informations for the file at %s.\n", path);
		return 1;
	}
	return 0;
}

AVStream *copy_stream_to_output(AVFormatContext *out_ctx, AVStream *in_stream)
{
	AVStream *out_stream = avformat_new_stream(out_ctx, NULL);

	if (out_stream == NULL) {
		fprintf(stderr,"Error: Couldn't create stream.\n");
		return NULL;
	}
	if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
		fprintf(stderr, "Error: Couldn't copy parameters to the output file.\n");
		return NULL;
	}
	out_stream->codecpar->codec_tag = 0;
	return out_stream;
}

int open_output_file_for_write(AVFormatContext *out_ctx, const char *out_path, AVDictionary **options)
{
	if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open(&out_ctx->pb, out_path, AVIO_FLAG_WRITE) < 0) {
			fprintf(stderr, "Error: Couldn't open file at %s.\n", out_path);
			return 1;
		}
	}
	else
		printf("Output flag set to AVFMT_NOFILE.\n");
	if (avformat_write_header(out_ctx, options) < 0) {
		fprintf(stderr, "Error: Couldn't write headers to file at %s.\n", out_path);
		return 1;
	}
	return 0;
}

void process_packet(AVPacket *pkt, AVStream *in_stream, AVStream *out_stream)
{
	pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
	pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
	pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
	pkt->pos = -1;
}

type type_fromffmpeg(AVStream *stream)
{
	switch (stream->codecpar->codec_type)
	{
	case AVMEDIA_TYPE_VIDEO:
		return video;
	case AVMEDIA_TYPE_AUDIO:
		return audio;
	case AVMEDIA_TYPE_SUBTITLE:
		return subtitle;
	case AVMEDIA_TYPE_ATTACHMENT:
		if (!strcmp(avcodec_get_name(stream->codecpar->codec_id), "ttf"))
			return font;
		return none;
	default:
		return none;
	}
}