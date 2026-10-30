#include "extera/extera_round_video.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "core/file_utilities.h"
#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "mainwindow.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
} // extern "C"

namespace Extera::RoundVideo {
namespace {

constexpr auto kSide = 400;
constexpr auto kMaxDuration = 60 * crl::time(1000);
constexpr auto kAudioFrequency = 48'000;
constexpr auto kAudioBitRate = 64 * 1024;
constexpr auto kVideoBitRate = 1536 * 1024;
constexpr auto kMinSide = 0.3;

struct Result {
	QByteArray content;
	crl::time duration = 0;
};

class Transcoder final {
public:
	explicit Transcoder(const QString &path);

	[[nodiscard]] bool valid() const;
	[[nodiscard]] QImage firstFrame();
	[[nodiscard]] Result run(QRectF crop);

private:
	[[nodiscard]] bool readPacket(FFmpeg::Packet &packet);
	[[nodiscard]] QImage toImage(not_null<AVFrame*> frame);
	[[nodiscard]] bool initOutput();
	[[nodiscard]] bool initVideoEncoder();
	[[nodiscard]] bool initAudioEncoder();
	bool encode(
		AVCodecContext *codec,
		AVStream *stream,
		AVFrame *frame);
	void writeVideo(not_null<AVFrame*> frame, QRectF crop);
	void writeAudio(not_null<AVFrame*> frame);
	void drainAudio();

	FFmpeg::ReadFileWrap _input;
	FFmpeg::FormatPointer _format;
	FFmpeg::CodecPointer _videoDecoder;
	FFmpeg::CodecPointer _audioDecoder;
	FFmpeg::SwscalePointer _toImage;
	FFmpeg::SwscalePointer _toYuv;
	int _videoIndex = -1;
	int _audioIndex = -1;
	int _rotation = 0;

	FFmpeg::WriteBytesWrap _output;
	FFmpeg::FormatPointer _result;
	FFmpeg::CodecPointer _videoEncoder;
	FFmpeg::CodecPointer _audioEncoder;
	FFmpeg::SwresamplePointer _resampler;
	FFmpeg::FramePointer _videoFrame;
	AVStream *_videoStream = nullptr;
	AVStream *_audioStream = nullptr;
	AVAudioFifo *_fifo = nullptr;
	int64_t _audioPts = 0;
	int64_t _videoStart = AV_NOPTS_VALUE;
	crl::time _duration = 0;
	bool _finished = false;

};

Transcoder::Transcoder(const QString &path) {
	_input.file.setFileName(path);
	if (!_input.file.open(QIODevice::ReadOnly)) {
		return;
	}
	_format = FFmpeg::MakeFormatPointer(
		static_cast<void*>(&_input),
		&FFmpeg::ReadFileWrap::Read,
		nullptr,
		&FFmpeg::ReadFileWrap::Seek);
	if (!_format
		|| avformat_find_stream_info(_format.get(), nullptr) < 0) {
		_format = nullptr;
		return;
	}
	_videoIndex = av_find_best_stream(
		_format.get(),
		AVMEDIA_TYPE_VIDEO,
		-1,
		-1,
		nullptr,
		0);
	if (_videoIndex < 0) {
		_format = nullptr;
		return;
	}
	const auto video = _format->streams[_videoIndex];
	_rotation = FFmpeg::ReadRotationFromMetadata(video);
	_videoDecoder = FFmpeg::MakeCodecPointer({ .stream = video });
	_audioIndex = av_find_best_stream(
		_format.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		_videoIndex,
		nullptr,
		0);
	if (_audioIndex >= 0) {
		_audioDecoder = FFmpeg::MakeCodecPointer({
			.stream = _format->streams[_audioIndex],
		});
		if (!_audioDecoder) {
			_audioIndex = -1;
		}
	}
}

bool Transcoder::valid() const {
	return _format && _videoDecoder;
}

bool Transcoder::readPacket(FFmpeg::Packet &packet) {
	return av_read_frame(_format.get(), &packet.fields()) >= 0;
}

QImage Transcoder::toImage(not_null<AVFrame*> frame) {
	const auto size = QSize(frame->width, frame->height);
	_toImage = FFmpeg::MakeSwscalePointer(
		size,
		frame->format,
		size,
		AV_PIX_FMT_BGRA,
		&_toImage);
	if (!_toImage) {
		return QImage();
	}
	auto image = QImage(size, QImage::Format_ARGB32);
	uint8_t *data[AV_NUM_DATA_POINTERS] = { image.bits() };
	int linesize[AV_NUM_DATA_POINTERS] = { int(image.bytesPerLine()) };
	sws_scale(
		_toImage.get(),
		frame->data,
		frame->linesize,
		0,
		frame->height,
		data,
		linesize);
	if (_rotation) {
		image = image.transformed(QTransform().rotate(_rotation));
	}
	return image;
}

QImage Transcoder::firstFrame() {
	if (!valid()) {
		return QImage();
	}
	auto frame = FFmpeg::MakeFramePointer();
	auto packet = FFmpeg::Packet();
	while (readPacket(packet)) {
		const auto index = packet.fields().stream_index;
		if (index == _videoIndex
			&& avcodec_send_packet(_videoDecoder.get(), &packet.fields()) >= 0
			&& avcodec_receive_frame(_videoDecoder.get(), frame.get()) >= 0) {
			auto result = toImage(frame.get());
			av_seek_frame(_format.get(), -1, 0, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(_videoDecoder.get());
			return result;
		}
		av_packet_unref(&packet.fields());
	}
	return QImage();
}

bool Transcoder::initOutput() {
	_result = FFmpeg::MakeWriteFormatPointer(
		static_cast<void*>(&_output),
		nullptr,
		&FFmpeg::WriteBytesWrap::Write,
		&FFmpeg::WriteBytesWrap::Seek,
		"mp4"_q);
	if (!_result || !initVideoEncoder()) {
		return false;
	}
	if (_audioIndex >= 0 && !initAudioEncoder()) {
		_audioIndex = -1;
	}
	return avformat_write_header(_result.get(), nullptr) >= 0;
}

bool Transcoder::initVideoEncoder() {
	auto codec = avcodec_find_encoder_by_name("libopenh264");
	if (!codec) {
		codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!codec) {
		return false;
	}
	_videoStream = avformat_new_stream(_result.get(), codec);
	_videoEncoder = FFmpeg::CodecPointer(avcodec_alloc_context3(codec));
	if (!_videoStream || !_videoEncoder) {
		return false;
	}
	_videoEncoder->codec_id = codec->id;
	_videoEncoder->codec_type = AVMEDIA_TYPE_VIDEO;
	_videoEncoder->width = kSide;
	_videoEncoder->height = kSide;
	_videoEncoder->time_base = AVRational{ 1, 1'000'000 };
	_videoEncoder->framerate = AVRational{ 0, 1 };
	_videoEncoder->pix_fmt = AV_PIX_FMT_YUV420P;
	_videoEncoder->bit_rate = kVideoBitRate;
	if (_result->oformat->flags & AVFMT_GLOBALHEADER) {
		_videoEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	if (avcodec_open2(_videoEncoder.get(), codec, nullptr) < 0
		|| avcodec_parameters_from_context(
			_videoStream->codecpar,
			_videoEncoder.get()) < 0) {
		return false;
	}
	_videoFrame = FFmpeg::MakeFramePointer();
	_videoFrame->format = AV_PIX_FMT_YUV420P;
	_videoFrame->width = kSide;
	_videoFrame->height = kSide;
	return av_frame_get_buffer(_videoFrame.get(), 0) >= 0;
}

bool Transcoder::initAudioEncoder() {
	const auto codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (!codec) {
		return false;
	}
	_audioStream = avformat_new_stream(_result.get(), codec);
	_audioEncoder = FFmpeg::CodecPointer(avcodec_alloc_context3(codec));
	if (!_audioStream || !_audioEncoder) {
		return false;
	}
	_audioEncoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
	_audioEncoder->bit_rate = kAudioBitRate;
	_audioEncoder->sample_rate = kAudioFrequency;
	_audioEncoder->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	_audioEncoder->time_base = AVRational{ 1, kAudioFrequency };
	if (_result->oformat->flags & AVFMT_GLOBALHEADER) {
		_audioEncoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}
	if (avcodec_open2(_audioEncoder.get(), codec, nullptr) < 0
		|| avcodec_parameters_from_context(
			_audioStream->codecpar,
			_audioEncoder.get()) < 0) {
		return false;
	}
	_resampler = FFmpeg::MakeSwresamplePointer(
		&_audioDecoder->ch_layout,
		_audioDecoder->sample_fmt,
		_audioDecoder->sample_rate,
		&_audioEncoder->ch_layout,
		_audioEncoder->sample_fmt,
		_audioEncoder->sample_rate);
	_fifo = av_audio_fifo_alloc(
		_audioEncoder->sample_fmt,
		1,
		_audioEncoder->frame_size);
	return _resampler && _fifo;
}

bool Transcoder::encode(
		AVCodecContext *codec,
		AVStream *stream,
		AVFrame *frame) {
	if (avcodec_send_frame(codec, frame) < 0) {
		return false;
	}
	auto packet = FFmpeg::Packet();
	while (avcodec_receive_packet(codec, &packet.fields()) >= 0) {
		av_packet_rescale_ts(
			&packet.fields(),
			codec->time_base,
			stream->time_base);
		packet.fields().stream_index = stream->index;
		av_interleaved_write_frame(_result.get(), &packet.fields());
		av_packet_unref(&packet.fields());
	}
	return true;
}

void Transcoder::writeVideo(not_null<AVFrame*> frame, QRectF crop) {
	const auto stream = _format->streams[_videoIndex];
	auto pts = frame->best_effort_timestamp;
	if (pts == AV_NOPTS_VALUE) {
		pts = frame->pts;
	}
	if (pts == AV_NOPTS_VALUE) {
		return;
	}
	if (_videoStart == AV_NOPTS_VALUE) {
		_videoStart = pts;
	}
	const auto time = FFmpeg::PtsToTime(pts - _videoStart, stream->time_base);
	if (time >= kMaxDuration) {
		_finished = true;
		return;
	}
	const auto image = toImage(frame);
	if (image.isNull()) {
		return;
	}
	const auto side = crop.width() * std::min(image.width(), image.height());
	const auto rect = QRect(
		int(crop.x() * image.width()),
		int(crop.y() * image.height()),
		int(side),
		int(side)).intersected(image.rect());
	const auto square = image.copy(rect).scaled(
		kSide,
		kSide,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
	_toYuv = FFmpeg::MakeSwscalePointer(
		QSize(kSide, kSide),
		AV_PIX_FMT_BGRA,
		QSize(kSide, kSide),
		AV_PIX_FMT_YUV420P,
		&_toYuv);
	if (!_toYuv || av_frame_make_writable(_videoFrame.get()) < 0) {
		return;
	}
	const uint8_t *data[AV_NUM_DATA_POINTERS] = { square.constBits() };
	int linesize[AV_NUM_DATA_POINTERS] = { int(square.bytesPerLine()) };
	sws_scale(
		_toYuv.get(),
		data,
		linesize,
		0,
		kSide,
		_videoFrame->data,
		_videoFrame->linesize);
	_videoFrame->pts = time * 1000;
	_duration = std::max(_duration, time);
	encode(_videoEncoder.get(), _videoStream, _videoFrame.get());
}

void Transcoder::writeAudio(not_null<AVFrame*> frame) {
	if (_audioPts >= kMaxDuration * kAudioFrequency / 1000) {
		return;
	}
	const auto count = swr_get_out_samples(_resampler.get(), frame->nb_samples);
	if (count <= 0) {
		return;
	}
	auto buffer = std::vector<float>(count);
	uint8_t *out[] = { reinterpret_cast<uint8_t*>(buffer.data()) };
	const auto converted = swr_convert(
		_resampler.get(),
		out,
		count,
		(const uint8_t**)frame->extended_data,
		frame->nb_samples);
	if (converted <= 0) {
		return;
	}
	void *write[] = { buffer.data() };
	av_audio_fifo_write(_fifo, write, converted);
	drainAudio();
}

void Transcoder::drainAudio() {
	const auto size = _audioEncoder->frame_size;
	while (av_audio_fifo_size(_fifo) >= size) {
		auto frame = FFmpeg::MakeFramePointer();
		frame->nb_samples = size;
		frame->format = _audioEncoder->sample_fmt;
		frame->sample_rate = _audioEncoder->sample_rate;
		av_channel_layout_copy(&frame->ch_layout, &_audioEncoder->ch_layout);
		if (av_frame_get_buffer(frame.get(), 0) < 0) {
			return;
		}
		av_audio_fifo_read(_fifo, (void**)frame->data, size);
		frame->pts = _audioPts;
		_audioPts += size;
		encode(_audioEncoder.get(), _audioStream, frame.get());
	}
}

Result Transcoder::run(QRectF crop) {
	if (!valid() || !initOutput()) {
		return {};
	}
	auto frame = FFmpeg::MakeFramePointer();
	auto packet = FFmpeg::Packet();
	while (!_finished && readPacket(packet)) {
		const auto index = packet.fields().stream_index;
		if (index == _videoIndex) {
			if (avcodec_send_packet(
					_videoDecoder.get(),
					&packet.fields()) >= 0) {
				while (!_finished
					&& avcodec_receive_frame(
						_videoDecoder.get(),
						frame.get()) >= 0) {
					writeVideo(frame.get(), crop);
				}
			}
		} else if (index == _audioIndex && _audioEncoder) {
			if (avcodec_send_packet(
					_audioDecoder.get(),
					&packet.fields()) >= 0) {
				while (avcodec_receive_frame(
						_audioDecoder.get(),
						frame.get()) >= 0) {
					writeAudio(frame.get());
				}
			}
		}
		av_packet_unref(&packet.fields());
	}
	encode(_videoEncoder.get(), _videoStream, nullptr);
	if (_audioEncoder) {
		encode(_audioEncoder.get(), _audioStream, nullptr);
	}
	av_write_trailer(_result.get());
	if (_fifo) {
		av_audio_fifo_free(_fifo);
		_fifo = nullptr;
	}
	_result = nullptr;
	return {
		.content = std::move(_output.content),
		.duration = _duration,
	};
}

void ShowCropBox(
		not_null<Window::SessionController*> controller,
		QString path,
		QImage frame,
		Api::SendAction action) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_exd_round_video());
		box->setWidth(st::boxWideWidth);
		struct State {
			QPointF center = { 0.5, 0.5 };
			float64 side = 1.;
			std::optional<QPointF> drag;
		};
		const auto state = box->lifetime().make_state<State>();
		const auto width = st::boxWideWidth
			- st::boxRowPadding.left()
			- st::boxRowPadding.right();
		const auto height = std::min(
			width * frame.height() / std::max(frame.width(), 1),
			width);
		const auto shown = frame.scaled(
			QSize(width, height) * style::DevicePixelRatio(),
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		const auto area = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, height));
		const auto imageRect = [=] {
			const auto size = shown.size() / style::DevicePixelRatio();
			return QRect(
				(area->width() - size.width()) / 2,
				(area->height() - size.height()) / 2,
				size.width(),
				size.height());
		};
		const auto selection = [=] {
			const auto r = imageRect();
			const auto side = state->side * std::min(r.width(), r.height());
			return QRectF(
				r.x() + state->center.x() * r.width() - side / 2.,
				r.y() + state->center.y() * r.height() - side / 2.,
				side,
				side);
		};
		const auto clamp = [=] {
			const auto r = imageRect();
			const auto side = state->side * std::min(r.width(), r.height());
			const auto halfX = side / 2. / std::max(r.width(), 1);
			const auto halfY = side / 2. / std::max(r.height(), 1);
			state->center.setX(
				std::clamp(state->center.x(), halfX, 1. - halfX));
			state->center.setY(
				std::clamp(state->center.y(), halfY, 1. - halfY));
		};
		area->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(area);
			auto hq = PainterHighQualityEnabler(p);
			const auto r = imageRect();
			p.drawImage(r, shown);
			auto outside = QPainterPath();
			outside.addRect(r);
			auto circle = QPainterPath();
			circle.addEllipse(selection());
			p.fillPath(outside.subtracted(circle), QColor(0, 0, 0, 140));
			p.setPen(QPen(QColor(255, 255, 255), st::lineWidth * 2));
			p.setBrush(Qt::NoBrush);
			p.drawEllipse(selection());
		}, area->lifetime());
		area->setCursor(style::cur_sizeall);
		area->events(
		) | rpl::on_next([=](not_null<QEvent*> e) {
			const auto type = e->type();
			if (type == QEvent::MouseButtonPress) {
				state->drag = static_cast<QMouseEvent*>(e.get())->position();
			} else if (type == QEvent::MouseButtonRelease) {
				state->drag = std::nullopt;
			} else if (type == QEvent::MouseMove && state->drag) {
				const auto now = static_cast<QMouseEvent*>(e.get())->position();
				const auto r = imageRect();
				const auto delta = now - *state->drag;
				state->drag = now;
				state->center += QPointF(
					delta.x() / std::max(r.width(), 1),
					delta.y() / std::max(r.height(), 1));
				clamp();
				area->update();
			} else if (type == QEvent::Wheel) {
				const auto delta = static_cast<QWheelEvent*>(
					e.get())->angleDelta().y();
				state->side = std::clamp(
					state->side + (delta > 0 ? 0.05 : -0.05),
					kMinSide,
					1.);
				clamp();
				area->update();
			}
		}, area->lifetime());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_exd_round_video_hint(),
				st::boxDividerLabel),
			st::boxRowPadding);
		box->addButton(tr::lng_send_button(), [=] {
			const auto r = imageRect();
			const auto side = state->side * std::min(r.width(), r.height());
			const auto crop = QRectF(
				state->center.x() - side / 2. / r.width(),
				state->center.y() - side / 2. / r.height(),
				state->side,
				state->side);
			box->closeBox();
			controller->showToast(tr::lng_exd_round_video_preparing(tr::now));
			const auto weak = base::make_weak(controller);
			crl::async([=] {
				auto transcoder = Transcoder(path);
				auto result = transcoder.run(crop);
				crl::on_main([=, result = std::move(result)]() mutable {
					const auto strong = weak.get();
					if (!strong) {
						return;
					} else if (result.content.isEmpty()) {
						strong->showToast(
							tr::lng_exd_round_video_failed(tr::now));
						return;
					}
					strong->session().api().sendVoiceMessage(
						std::move(result.content),
						VoiceWaveform(),
						result.duration,
						true,
						action);
				});
			});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::CloseOther);
}

} // namespace

void ChooseAndSend(
		not_null<Window::SessionController*> controller,
		Api::SendAction action) {
	const auto weak = base::make_weak(controller);
	FileDialog::GetOpenPath(
		QPointer<QWidget>(controller->widget().get()),
		tr::lng_exd_round_video(tr::now),
		FileDialog::PhotoVideoFilesFilter(),
		[=](FileDialog::OpenResult &&result) {
			const auto strong = weak.get();
			if (!strong || result.paths.isEmpty()) {
				return;
			}
			ShowForPath(strong, result.paths.front(), action);
		});
}

void ShowForPath(
		not_null<Window::SessionController*> controller,
		const QString &path,
		Api::SendAction action) {
	auto transcoder = Transcoder(path);
	const auto frame = transcoder.firstFrame();
	if (frame.isNull()) {
		controller->showToast(tr::lng_exd_round_video_failed(tr::now));
		return;
	}
	ShowCropBox(controller, path, frame, action);
}

} // namespace Extera::RoundVideo
