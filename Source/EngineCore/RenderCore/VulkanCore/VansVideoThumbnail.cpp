#include "VansVideoThumbnail.h"

#include <algorithm>
#include <cmath>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

namespace VansGraphics
{
bool VansVideoThumbnailBuilder::Build(
	const std::filesystem::path& sourcePath,
	int maximumWidth,
	int maximumHeight,
	VansVideoThumbnail& thumbnail,
	std::string& error)
{
	thumbnail = {};
	error.clear();
	if (sourcePath.empty() || !std::filesystem::is_regular_file(sourcePath))
	{
		error = "Video thumbnail source does not exist";
		return false;
	}
	maximumWidth = std::clamp(maximumWidth, 16, 256);
	maximumHeight = std::clamp(maximumHeight, 9, 144);

	AVFormatContext* format = nullptr;
	AVCodecContext* decoder = nullptr;
	AVFrame* frame = nullptr;
	AVPacket* packet = nullptr;
	SwsContext* scaler = nullptr;
	auto cleanup = [&]()
	{
		if (scaler) sws_freeContext(scaler);
		if (packet) av_packet_free(&packet);
		if (frame) av_frame_free(&frame);
		if (decoder) avcodec_free_context(&decoder);
		if (format) avformat_close_input(&format);
	};

	if (avformat_open_input(&format, sourcePath.string().c_str(), nullptr, nullptr) < 0 ||
		avformat_find_stream_info(format, nullptr) < 0)
	{
		error = "Video thumbnail decoder could not open the source";
		cleanup();
		return false;
	}
	const int streamIndex = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (streamIndex < 0)
	{
		error = "Video thumbnail source contains no video stream";
		cleanup();
		return false;
	}
	AVStream* stream = format->streams[streamIndex];
	const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
	decoder = codec ? avcodec_alloc_context3(codec) : nullptr;
	if (!decoder || avcodec_parameters_to_context(decoder, stream->codecpar) < 0 ||
		avcodec_open2(decoder, codec, nullptr) < 0)
	{
		error = "Video thumbnail codec initialization failed";
		cleanup();
		return false;
	}
	if (stream->duration != AV_NOPTS_VALUE)
		thumbnail.durationSeconds = static_cast<double>(stream->duration) * av_q2d(stream->time_base);
	else if (format->duration != AV_NOPTS_VALUE)
		thumbnail.durationSeconds = static_cast<double>(format->duration) / AV_TIME_BASE;

	frame = av_frame_alloc();
	packet = av_packet_alloc();
	if (!frame || !packet)
	{
		error = "Video thumbnail frame allocation failed";
		cleanup();
		return false;
	}
	bool decoded = false;
	while (!decoded && av_read_frame(format, packet) >= 0)
	{
		if (packet->stream_index == streamIndex)
		{
			const int sendResult = avcodec_send_packet(decoder, packet);
			if (sendResult >= 0 || sendResult == AVERROR(EAGAIN))
				decoded = avcodec_receive_frame(decoder, frame) >= 0;
		}
		av_packet_unref(packet);
	}
	if (!decoded && avcodec_send_packet(decoder, nullptr) >= 0)
		decoded = avcodec_receive_frame(decoder, frame) >= 0;
	if (!decoded || frame->width <= 0 || frame->height <= 0)
	{
		error = "Video thumbnail source has no decodable frame";
		cleanup();
		return false;
	}
	const double scale = std::min(
		static_cast<double>(maximumWidth) / frame->width,
		static_cast<double>(maximumHeight) / frame->height);
	thumbnail.width = std::max(1, static_cast<int>(std::floor(frame->width * scale)));
	thumbnail.height = std::max(1, static_cast<int>(std::floor(frame->height * scale)));
	thumbnail.rgba.resize(static_cast<std::size_t>(thumbnail.width) * thumbnail.height * 4);
	scaler = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
		thumbnail.width, thumbnail.height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!scaler)
	{
		error = "Video thumbnail pixel conversion initialization failed";
		cleanup();
		return false;
	}
	std::uint8_t* destination[4] = { thumbnail.rgba.data(), nullptr, nullptr, nullptr };
	int destinationStride[4] = { thumbnail.width * 4, 0, 0, 0 };
	if (sws_scale(scaler, frame->data, frame->linesize, 0, frame->height,
		destination, destinationStride) <= 0)
	{
		error = "Video thumbnail pixel conversion failed";
		cleanup();
		return false;
	}
	cleanup();
	return true;
}
}
