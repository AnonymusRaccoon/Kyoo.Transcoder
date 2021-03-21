//
// Created by Anonymus Raccoon on 20/12/2019.
//

#include "transcoder.h"
#include "helper.h"
#include "compatibility.h"
#include "path_helper.h"
#include <malloc.h>

static bool should_copy_to_transmuxed(enum AVMediaType codec_type)
{
	if (codec_type == AVMEDIA_TYPE_VIDEO)
		return true;
	if (codec_type == AVMEDIA_TYPE_AUDIO)
		return true;
	return false;
}

static int *prepare_streammap(AVFormatContext *in_ctx, AVFormatContext *out_ctx)
{
	int *stream_map = malloc(sizeof(int) * in_ctx->nb_streams);
	int stream_count = 0;
	AVStream *stream;

	if (!stream_map)
		return NULL;
	for (unsigned i = 0; i < in_ctx->nb_streams; i++) {
		stream = in_ctx->streams[i];
		if (should_copy_to_transmuxed(stream->codecpar->codec_type)) {
			stream_map[i] = stream_count;
			stream_count++;
			if (!copy_stream_to_output(out_ctx, stream)) {
				free(stream_map);
				return NULL;
			}
		} else
			stream_map[i] = -1;
	}
	return stream_map;
}

static AVDictionary *create_options_context(const char *out_path)
{
	AVDictionary *options = NULL;
	char seg_path[strlen(out_path) + 22];
	int folder_index = strrchr(out_path, '/') - out_path;

	sprintf(seg_path, "%.*s/segments/", folder_index, out_path);
	if (path_mkdir(seg_path, 0755) < 0) {
		fprintf(stderr, "Error: Couldn't create segment output folder. "
			"Part of the output path does not exist or you don't have write rights.\n");
		return NULL;
	}
	strcat(seg_path, "%v-%03d.ts");
	av_dict_set(&options, "hls_segment_filename", seg_path, 0);
	av_dict_set(&options, "hls_base_url", "segments/", 0);
	av_dict_set(&options, "hls_list_size", "0", 0);
	av_dict_set(&options, "streaming", "1", 0);
	return options;
}

static void write_to_output(AVFormatContext *in_ctx, AVFormatContext *out_ctx, int *stream_map, float *playable_duration)
{
	AVPacket pkt;
	AVStream *istream;
	AVStream *ostream;
	int index;

	while (av_read_frame(in_ctx, &pkt) == 0) {
		index = pkt.stream_index;
		if (index >= in_ctx->nb_streams || stream_map[index] < 0) {
			av_packet_unref(&pkt);
			continue;
		}
		istream = in_ctx->streams[index];
		ostream = out_ctx->streams[stream_map[index]];
		pkt.stream_index = stream_map[index];
		process_packet(&pkt, istream, ostream);
		if (pkt.stream_index == 0)
			*playable_duration += pkt.duration * (float)ostream->time_base.num / ostream->time_base.den;
		if (av_interleaved_write_frame(out_ctx, &pkt) < 0)
			fprintf(stderr, "Error while writing a packet to the output file.\n");
		av_packet_unref(&pkt);
	}
}

static void destroy_context(AVFormatContext *in_ctx, AVFormatContext *out_ctx, AVDictionary *options, int *stream_map)
{
	if (options)
		av_dict_free(&options);
	av_write_trailer(out_ctx);
	avformat_close_input(&in_ctx);
	if (out_ctx && !(out_ctx->oformat->flags & AVFMT_NOFILE))
		avio_close(out_ctx->pb);
	avformat_free_context(out_ctx);
	if (stream_map)
		free(stream_map);
}

int transmux(const char *path, const char *out_path, float *playable_duration)
{
	AVFormatContext *in_ctx = NULL;
	AVFormatContext *out_ctx = NULL;
	AVDictionary *options = NULL;
	int *stream_map;

	*playable_duration = 0;
	av_log_set_level(AV_LOG_LEVEL);
	if (open_input_context(&in_ctx, path) != 0) {
		fprintf(stderr, "Error: Coudln't open the input file.\n");
		return -1;
	}
	if (avformat_alloc_output_context2(&out_ctx, NULL, NULL, out_path) < 0) {
		fprintf(stderr, "Error: Couldn't create an output file.\n");
		avformat_close_input(&in_ctx);
		return -1;
	}
	stream_map = prepare_streammap(in_ctx, out_ctx);
	options = create_options_context(out_path);
	if (!stream_map || !options || open_output_file_for_write(out_ctx, out_path, &options) != 0) {
		destroy_context(in_ctx, out_ctx, options, stream_map);
		return -1;
	}
	write_to_output(in_ctx, out_ctx, stream_map, playable_duration);
	destroy_context(in_ctx, out_ctx, options, stream_map);
	return 0;
}