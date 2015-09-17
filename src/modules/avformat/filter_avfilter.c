/*
 * filter_avfilter.c -- image scaling filter
 * Copyright (C) 2008-2014 Meltytech, LLC
 * Copyright (C) 2015 anba8005
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <framework/mlt_filter.h>
#include <framework/mlt_frame.h>
#include <framework/mlt_factory.h>
#include <framework/mlt_log.h>

// ffmpeg Header files
#include <libavformat/avformat.h>
#include "libavfilter/avfilter.h"
#include <libavfilter/avfiltergraph.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int MEM_ALIGN = 32;

static int convert_mlt_to_av_cs(mlt_image_format format) {
	int value = 0;

	switch (format) {
	case mlt_image_rgb24:
		value = AV_PIX_FMT_RGB24;
		break;
	case mlt_image_rgb24a:
	case mlt_image_opengl:
		value = AV_PIX_FMT_RGBA;
		break;
	case mlt_image_yuv422:
		value = AV_PIX_FMT_YUYV422;
		break;
	case mlt_image_yuv420p:
		value = AV_PIX_FMT_YUV420P;
		break;
	default:
		mlt_log_error(NULL, "[filter avfilter] Invalid format %s\n", mlt_image_format_name(format));
		break;
	}

	return value;
}

void close_filtergraph(AVFilterGraph* graph) {
	if (graph)
		avfilter_graph_free(&graph);
}

static void create_filtergraph(mlt_filter self, mlt_properties properties, int mlt_target_format) {
	//
	AVFilterGraph* graph = avfilter_graph_alloc();
	if (graph == NULL)
		return;
	graph->scale_sws_opts = av_strdup("flags=bicubic:interl=1");

	//
	int width = mlt_properties_get_int(properties, "width");
	int height = mlt_properties_get_int(properties, "height");
	int format = convert_mlt_to_av_cs(mlt_properties_get_int(properties, "format"));
	int frame_rate_num = mlt_properties_get_int(properties, "meta.media.frame_rate_num");
	int frame_rate_den = mlt_properties_get_int(properties, "meta.media.frame_rate_den");
	int frame_sar_num = mlt_properties_get_int(properties, "meta.media.sample_aspect_num");
	int frame_sar_den = mlt_properties_get_int(properties, "meta.media.sample_aspect_den");
	//
	int target_format = convert_mlt_to_av_cs(mlt_target_format);

	// create input
	AVFilterContext* input;
	char args[512];
	snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:frame_rate=%d/%d:pixel_aspect=%d/%d",
			width, height, format, frame_rate_den, frame_rate_num, frame_rate_num, frame_rate_den, frame_sar_num,
			frame_sar_den);
	mlt_log_info(self, "input is -> %s\n",args);
	int result = avfilter_graph_create_filter(&input, avfilter_get_by_name("buffer"), "in", args, NULL, graph);
	if (result < 0) {
		mlt_log_error(self, "error creating video filter input\n");
		return;
	}

	// create output
	AVFilterContext* output;
	result = avfilter_graph_create_filter(&output, avfilter_get_by_name("buffersink"), "out", NULL, NULL, graph);
	if (result < 0) {
		mlt_log_error(self, "error creating video filter output\n");
		return;
	}

	// configure output
	enum AVPixelFormat fmts[] = { target_format, AV_PIX_FMT_NONE };
	if (av_opt_set_int_list(output, "pix_fmts", fmts, AV_PIX_FMT_NONE, 1)) {
		mlt_log_error(self, "error configuring video filter output\n");
		return;
	}

	// create endpoints
	AVFilterInOut *outputs = avfilter_inout_alloc();
	outputs->name = av_strdup("in");
	outputs->filter_ctx = input;
	outputs->pad_idx = 0;
	outputs->next = NULL;
	AVFilterInOut *inputs = avfilter_inout_alloc();
	inputs->name = av_strdup("out");
	inputs->filter_ctx = output;
	inputs->pad_idx = 0;
	inputs->next = NULL;

	// create graph
	char *filter = mlt_properties_get(MLT_FILTER_PROPERTIES(self), "filter");
	if (filter == NULL)
		filter = "copy";
	mlt_log_info(self, "filter is -> %s\n",filter);
	result = avfilter_graph_parse_ptr(graph, filter, &inputs, &outputs, NULL);
	if (result < 0) {
		mlt_log_error(self, "error creating video filter graph\n");
		return;
	}

	// configure graph
	result = avfilter_graph_config(graph, NULL);
	if (result < 0) {
		mlt_log_error(self, "error configuring video filter graph\n");
		return;
	}

	// save
	mlt_properties filter_properties = MLT_FILTER_PROPERTIES(self);
	mlt_properties_set_data(filter_properties, "filtergraph", graph, 0, (mlt_destructor) close_filtergraph, NULL);
	mlt_properties_set_data(filter_properties, "filtergraph_input", input, 0, NULL, NULL);
	mlt_properties_set_data(filter_properties, "filtergraph_output", output, 0, NULL, NULL);
	mlt_properties_set_int(filter_properties, "last_frame_pts", -1);
}

static AVFrame* create_avframe(int format, int width, int height, long pts, int interlaced, int tff) {
	AVFrame* frame = av_frame_alloc();
	frame->format = format;
	frame->width = width;
	frame->height = height;
	frame->pts = pts;
	frame->interlaced_frame = interlaced;
	frame->top_field_first = tff;
	av_frame_get_buffer(frame, MEM_ALIGN);
	return frame;
}

static int avfilter_get_image(mlt_frame frame, uint8_t **image, mlt_image_format *format, int *width, int *height,
		int writable) {
	// Get the properties
	mlt_properties properties = MLT_FRAME_PROPERTIES(frame);
	mlt_filter filter = mlt_frame_pop_service(frame);
	mlt_properties filter_properties = MLT_FILTER_PROPERTIES(filter);
	if (*format == mlt_image_none)
		*format = mlt_image_yuv422;

	// get graph
	AVFilterGraph* graph = mlt_properties_get_data(filter_properties, "filtergraph", NULL);
	if (graph == NULL) {
		create_filtergraph(filter, properties, *format);
		graph = mlt_properties_get_data(filter_properties, "filtergraph", NULL);
		if (graph == NULL) {
			return 1;
		}
	}

	//
	int frame_width = mlt_properties_get_int(properties, "width");
	int frame_height = mlt_properties_get_int(properties, "height");
	int frame_format = mlt_properties_get_int(properties, "format");
	int frame_avformat = convert_mlt_to_av_cs(frame_format);
	int frame_interlaced = !mlt_properties_get_int(properties, "progressive");
	int frame_tff = mlt_properties_get_int(properties, "top_field_first");

	//
	int error = mlt_frame_get_image(frame, image, &frame_format, &frame_width, &frame_height, 1);
	if (error)
		return 1;

start_filtering: ;

	//
	int frame_pts = mlt_properties_get_int(filter_properties, "last_frame_pts") + 1;
	mlt_properties_set_int(filter_properties, "last_frame_pts", frame_pts);

	//
	AVFrame* input_avframe = create_avframe(frame_avformat, frame_width, frame_height, frame_pts, frame_interlaced,
			frame_tff);
	if (!input_avframe)
		return 1;

	//
	AVPicture picture;
	memset(&picture, 0, sizeof(AVPicture));
	av_image_fill_arrays(picture.data, picture.linesize, (uint8_t*) *image, frame_avformat, frame_width, frame_height,
			MEM_ALIGN);
	av_image_copy(input_avframe->data, input_avframe->linesize, (const uint8_t**) picture.data, picture.linesize,
			frame_avformat, frame_width, frame_height);

	//
	AVFilterContext* input = mlt_properties_get_data(filter_properties, "filtergraph_input", NULL);
	av_buffersrc_add_frame_flags(input, input_avframe, AV_BUFFERSRC_FLAG_KEEP_REF);
	av_frame_free(&input_avframe);

	//
	AVFilterContext* output = mlt_properties_get_data(filter_properties, "filtergraph_output", NULL);
	AVFrame* output_avframe = av_frame_alloc();
	int result = av_buffersink_get_frame(output, output_avframe);
	if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
		if (frame_pts == 0) {
			// initial delay caused by yadif
			goto start_filtering;
		} else {
			mlt_log_error(filter, "buffersink returned %s\n",av_err2str(result));
			return 1;
		}
	} else if (result < 0) {
		mlt_log_error(filter, "buffersink returned %s\n",av_err2str(result));
		return 1;
	}

	// allocate output
	*width = output_avframe->width;
	*height = output_avframe->height;
	int size = mlt_image_format_size(*format, *width, *height, 0);
	uint8_t *output_image = mlt_pool_alloc(size);
	if (!output_image)
		return 1;

	// fill output
	int avsize = avpicture_get_size(output_avframe->format,*width, *height);
	memcpy(output_image,output_avframe->data[0],output_avframe->linesize[0] * output_avframe->height);
	av_frame_free(&output_avframe);
	mlt_frame_set_image(frame, output_image, size, mlt_pool_release);
	*image = output_image;

	return 0;
}

static mlt_frame avfilter_process(mlt_filter filter, mlt_frame frame) {
	mlt_frame_push_service(frame, filter);
	mlt_frame_push_service(frame, avfilter_get_image);
	return frame;
}

mlt_filter filter_avfilter_init(mlt_profile profile, mlt_service_type type, const char *id, char *arg) {
	mlt_filter filter = mlt_filter_new();
	if (filter != NULL) {
		filter->process = avfilter_process;
		mlt_properties filter_properties = MLT_FILTER_PROPERTIES(filter);
		mlt_properties_set_int(filter_properties, "sar_num", profile->sample_aspect_num);
		mlt_properties_set_int(filter_properties, "sar_den", profile->sample_aspect_den);

	}
	return filter;
}

