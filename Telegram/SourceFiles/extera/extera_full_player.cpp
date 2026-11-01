#include "extera/extera_full_player.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "ui/effects/animations.h"
#include "ui/image/image.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "window/window_session_controller.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_session.h"
#include "media/media_common.h"
#include "ui/layers/box_content.h"
#include "base/call_delayed.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include "styles/style_layers.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Extera::FullPlayer {
namespace {

using Type = AudioMsgId::Type;

[[nodiscard]] int S(int value) {
	return style::ConvertScale(value);
}

struct Line {
	crl::time time = 0;
	QString text;
	int top = 0;
	int height = 0;
};

enum class Lyrics {
	Loading,
	Synced,
	Plain,
	None,
};

enum class Button {
	None,
	Previous,
	Play,
	Next,
	Slider,
	Line,
	Shuffle,
	Repeat,
	ShowMessage,
	CopyLyrics,
};

[[nodiscard]] std::vector<Line> ParseSynced(const QString &text) {
	static const auto re = QRegularExpression(
		u"^\\[(\\d+):(\\d+(?:\\.\\d+)?)\\]\\s*(.*)$"_q);
	auto result = std::vector<Line>();
	for (const auto &line : text.split('\n')) {
		const auto match = re.match(line.trimmed());
		if (!match.hasMatch()) {
			continue;
		}
		result.push_back({
			.time = match.captured(1).toInt() * crl::time(60000)
				+ crl::time(match.captured(2).toDouble() * 1000),
			.text = match.captured(3).trimmed(),
		});
	}
	return result;
}

[[nodiscard]] QString CleanTitle(QString text) {
	static const auto brackets = QRegularExpression(
		u"\\s*[\\(\\[\\{][^\\)\\]\\}]*[\\)\\]\\}]"_q);
	static const auto feat = QRegularExpression(
		u"\\s+(feat\\.?|ft\\.?|featuring)\\s+.*$"_q,
		QRegularExpression::CaseInsensitiveOption);
	static const auto extra = QRegularExpression(
		u"\\s*-\\s*(official|lyrics?|audio|video|remaster).*$"_q,
		QRegularExpression::CaseInsensitiveOption);
	text.replace(brackets, QString());
	text.replace(feat, QString());
	text.replace(extra, QString());
	text.replace('_', ' ');
	return text.simplified();
}

[[nodiscard]] QString FirstArtist(const QString &artist) {
	static const auto split = QRegularExpression(
		u"\\s*(,|&|\\bx\\b|\\band\\b|;)\\s*"_q,
		QRegularExpression::CaseInsensitiveOption);
	const auto parts = artist.split(split, Qt::SkipEmptyParts);
	return parts.isEmpty() ? artist : parts.front().trimmed();
}

[[nodiscard]] QString FormatTime(crl::time ms) {
	const auto seconds = std::max(ms, crl::time(0)) / 1000;
	return u"%1:%2"_q
		.arg(seconds / 60)
		.arg(seconds % 60, 2, 10, QChar('0'));
}

[[nodiscard]] QColor Mix(QColor a, QColor b, float64 progress) {
	return QColor(
		anim::interpolate(a.red(), b.red(), progress),
		anim::interpolate(a.green(), b.green(), progress),
		anim::interpolate(a.blue(), b.blue(), progress));
}

[[nodiscard]] QColor Darken(QColor color, float64 amount) {
	return QColor(
		int(color.red() * amount),
		int(color.green() * amount),
		int(color.blue() * amount));
}

class View final : public Ui::RpWidget {
public:
	View(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void loadTrack();
	void checkCover();
	void applyCover(QImage image);
	void requestLyrics();
	void tryLyrics(
		int requestId,
		std::vector<std::pair<QUrl, bool>> urls,
		int index,
		int attempt = 0);
	void tryNetEase(int requestId);
	void loadNetEaseLyric(int requestId, qint64 id);
	bool applyLyrics(const QJsonObject &object);
	void layoutLines();
	void updateCurrentLine();
	void seekTo(float64 progress);
	void paintControls(QPainter &p);
	void paintLyrics(QPainter &p);
	[[nodiscard]] Button buttonAt(QPoint point, int *line = nullptr) const;
	[[nodiscard]] crl::time position() const;
	[[nodiscard]] crl::time length() const;
	[[nodiscard]] QRect coverRect() const;
	[[nodiscard]] QRect sliderRect() const;
	[[nodiscard]] QPoint buttonCenter(Button button) const;
	[[nodiscard]] QRect lyricsRect() const;

	const not_null<Window::SessionController*> _controller;
	const int _height = 0;
	const style::font _titleFont;
	const style::font _subtitleFont;
	const style::font _timeFont;
	const style::font _lineFont;
	const style::font _currentFont;

	DocumentData *_document = nullptr;
	FullMsgId _context;
	std::shared_ptr<Data::DocumentMedia> _media;
	QString _title;
	QString _performer;
	QImage _cover;
	QImage _backdrop;
	QColor _top;
	QColor _bottom;
	QColor _wasTop;
	QColor _wasBottom;
	Ui::Animations::Simple _colors;

	Media::Player::TrackState _state;
	bool _seeking = false;
	float64 _seekValue = 0.;
	Button _over = Button::None;

	QNetworkAccessManager *_network = nullptr;
	int _requestId = 0;
	Lyrics _lyrics = Lyrics::Loading;
	std::vector<Line> _lines;
	int _linesWidth = 0;
	int _linesHeight = 0;
	int _current = -1;
	float64 _scroll = 0.;
	float64 _scrollFrom = 0.;
	float64 _scrollTo = 0.;
	Ui::Animations::Simple _scrollAnimation;

};

View::View(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _height(S(560))
, _titleFont(S(20), style::FontFlag::Bold, 0)
, _subtitleFont(S(15), style::FontFlags(), 0)
, _timeFont(S(12), style::FontFlags(), 0)
, _lineFont(S(17), style::FontFlag::Bold, 0)
, _currentFont(S(19), style::FontFlag::Bold, 0)
, _top(QColor(40, 40, 48))
, _bottom(QColor(18, 18, 22))
, _wasTop(_top)
, _wasBottom(_bottom)
, _network(new QNetworkAccessManager(this)) {
	setMouseTracking(true);

	const auto instance = Media::Player::instance();
	instance->updatedNotifier(
	) | rpl::filter([](const Media::Player::TrackState &state) {
		return state.id.type() == Type::Song;
	}) | rpl::on_next([=](const Media::Player::TrackState &state) {
		_state = state;
		if (state.id.audio() != _document) {
			loadTrack();
		}
		updateCurrentLine();
		update();
	}, lifetime());

	instance->trackChanged(
	) | rpl::filter(
		rpl::mappers::_1 == Type::Song
	) | rpl::on_next([=] {
		loadTrack();
	}, lifetime());

	controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		checkCover();
	}, lifetime());

	_state = instance->getState(Type::Song);
	loadTrack();
}

int View::resizeGetHeight(int newWidth) {
	_linesWidth = 0;
	return _height;
}

crl::time View::length() const {
	if (_state.length > 0 && _state.frequency > 0) {
		return _state.length * 1000 / _state.frequency;
	}
	return _document ? _document->duration() : 0;
}

crl::time View::position() const {
	if (_seeking) {
		return crl::time(_seekValue * length());
	}
	return (_state.frequency > 0)
		? (_state.position * 1000 / _state.frequency)
		: 0;
}

void View::loadTrack() {
	const auto current = Media::Player::instance()->current(Type::Song);
	const auto document = current.audio();
	if (document == _document && current.contextId() == _context) {
		return;
	}
	_document = document;
	_context = current.contextId();
	_cover = QImage();
	_backdrop = QImage();
	_media = nullptr;
	_title = QString();
	_performer = QString();
	if (const auto song = document ? document->song() : nullptr) {
		_title = song->title;
		_performer = song->performer;
	}
	if (document && _title.isEmpty()) {
		_title = document->filename();
		const auto dot = _title.lastIndexOf('.');
		if (dot > 0) {
			_title = _title.mid(0, dot);
		}
	}
	if (_performer.isEmpty()) {
		const auto split = _title.indexOf(u" - "_q);
		if (split > 0) {
			_performer = _title.mid(0, split).trimmed();
			_title = _title.mid(split + 3).trimmed();
		}
	}
	if (document) {
		_media = document->createMediaView();
		_media->thumbnailWanted(Data::FileOriginMessage(_context));
	}
	applyCover(QImage());
	checkCover();
	requestLyrics();
	update();
}

void View::checkCover() {
	if (!_media || !_cover.isNull()) {
		return;
	}
	if (const auto image = _media->thumbnail()) {
		applyCover(image->original());
	}
}

void View::applyCover(QImage image) {
	_wasTop = _top;
	_wasBottom = _bottom;
	if (image.isNull()) {
		_top = QColor(52, 52, 64);
		_bottom = QColor(18, 18, 24);
	} else {
		_cover = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
		const auto tiny = _cover.scaled(
			2,
			2,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation);
		_top = Darken(tiny.pixelColor(0, 0), 0.72);
		_bottom = Darken(tiny.pixelColor(1, 1), 0.38);
		_backdrop = _cover.scaled(
			12,
			12,
			Qt::KeepAspectRatioByExpanding,
			Qt::SmoothTransformation);
	}
	_colors.stop();
	_colors.start([=] { update(); }, 0., 1., 450, anim::easeOutCirc);
}

void View::requestLyrics() {
	const auto requestId = ++_requestId;
	_lyrics = Lyrics::Loading;
	_lines.clear();
	_linesWidth = 0;
	_current = -1;
	_scroll = _scrollFrom = _scrollTo = 0.;
	if (_title.isEmpty()) {
		_lyrics = Lyrics::None;
		return;
	}
	const auto title = CleanTitle(_title);
	const auto artist = CleanTitle(_performer);
	const auto firstArtist = FirstArtist(artist);
	const auto duration = int(length() / 1000);
	auto urls = std::vector<std::pair<QUrl, bool>>();
	const auto add = [&](
			const QString &path,
			std::vector<std::pair<QString, QString>> items,
			bool single) {
		auto query = QUrlQuery();
		for (const auto &[key, value] : items) {
			if (!value.isEmpty()) {
				query.addQueryItem(key, value);
			}
		}
		auto url = QUrl(u"https://lrclib.net/api/"_q + path);
		url.setQuery(query);
		urls.emplace_back(url, single);
	};
	if (!artist.isEmpty()) {
		add(u"get"_q, {
			{ u"track_name"_q, title },
			{ u"artist_name"_q, artist },
			{ u"duration"_q, duration ? QString::number(duration) : QString() },
		}, true);
		add(u"search"_q, {
			{ u"track_name"_q, title },
			{ u"artist_name"_q, firstArtist },
		}, false);
		add(u"search"_q, { { u"q"_q, firstArtist + ' ' + title } }, false);
	}
	add(u"search"_q, { { u"track_name"_q, title } }, false);
	add(u"search"_q, { { u"q"_q, _title } }, false);
	tryLyrics(requestId, std::move(urls), 0);
}

void View::tryLyrics(
		int requestId,
		std::vector<std::pair<QUrl, bool>> urls,
		int index,
		int attempt) {
	if (index >= int(urls.size())) {
		tryNetEase(requestId);
		return;
	}
	auto request = QNetworkRequest(urls[index].first);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"exDesktop"_q);
	const auto single = urls[index].second;
	const auto reply = _network->get(request);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (requestId != _requestId) {
			return;
		}
		const auto status = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto failed = (status == 503 || status == 429 || status == 0)
			&& reply->error() != QNetworkReply::NoError;
		if (failed && attempt < 4) {
			base::call_delayed(1500 * (attempt + 1), this, [=] {
				if (requestId == _requestId) {
					tryLyrics(requestId, urls, index, attempt + 1);
				}
			});
			return;
		}
		const auto document = QJsonDocument::fromJson(reply->readAll());
		if (single) {
			if (applyLyrics(document.object())) {
				return;
			}
		} else {
			const auto duration = length() / 1000;
			auto best = QJsonObject();
			auto bestScore = -1;
			for (const auto &entry : document.array()) {
				const auto object = entry.toObject();
				const auto synced = !object.value(
					u"syncedLyrics"_q).toString().isEmpty();
				const auto plain = !object.value(
					u"plainLyrics"_q).toString().isEmpty();
				if (!synced && !plain) {
					continue;
				}
				const auto diff = duration
					? std::abs(object.value(u"duration"_q).toDouble()
						- duration)
					: 0.;
				if (duration && diff > 15.) {
					continue;
				}
				const auto score = (synced ? 100 : 0)
					+ (diff <= 3. ? 50 : diff <= 8. ? 20 : 0);
				if (score > bestScore) {
					bestScore = score;
					best = object;
				}
			}
			if (bestScore >= 0 && applyLyrics(best)) {
				return;
			}
		}
		tryLyrics(requestId, urls, index + 1);
	});
}

void View::tryNetEase(int requestId) {
	auto query = QUrlQuery();
	query.addQueryItem(
		u"s"_q,
		(CleanTitle(_performer) + ' ' + CleanTitle(_title)).trimmed());
	query.addQueryItem(u"type"_q, u"1"_q);
	query.addQueryItem(u"limit"_q, u"8"_q);
	auto url = QUrl(u"https://music.163.com/api/search/get"_q);
	url.setQuery(query);
	auto request = QNetworkRequest(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"Mozilla/5.0"_q);
	const auto reply = _network->get(request);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (requestId != _requestId) {
			return;
		}
		const auto songs = QJsonDocument::fromJson(reply->readAll()).object(
		).value(u"result"_q).toObject().value(u"songs"_q).toArray();
		const auto duration = length();
		auto best = qint64(0);
		auto bestDiff = crl::time(-1);
		for (const auto &entry : songs) {
			const auto song = entry.toObject();
			const auto diff = duration
				? std::abs(crl::time(song.value(u"duration"_q).toDouble())
					- duration)
				: crl::time(0);
			if (duration && diff > 15000) {
				continue;
			}
			if (bestDiff < 0 || diff < bestDiff) {
				bestDiff = diff;
				best = qint64(song.value(u"id"_q).toDouble());
			}
		}
		if (best) {
			loadNetEaseLyric(requestId, best);
		} else {
			_lyrics = Lyrics::None;
			update();
		}
	});
}

void View::loadNetEaseLyric(int requestId, qint64 id) {
	auto url = QUrl(u"https://music.163.com/api/song/lyric"_q);
	auto query = QUrlQuery();
	query.addQueryItem(u"id"_q, QString::number(id));
	query.addQueryItem(u"lv"_q, u"1"_q);
	url.setQuery(query);
	auto request = QNetworkRequest(url);
	request.setHeader(QNetworkRequest::UserAgentHeader, u"Mozilla/5.0"_q);
	const auto reply = _network->get(request);
	connect(reply, &QNetworkReply::finished, this, [=] {
		reply->deleteLater();
		if (requestId != _requestId) {
			return;
		}
		const auto lyric = QJsonDocument::fromJson(reply->readAll()).object(
		).value(u"lrc"_q).toObject().value(u"lyric"_q).toString();
		auto object = QJsonObject();
		object.insert(u"syncedLyrics"_q, lyric);
		if (lyric.isEmpty() || !applyLyrics(object)) {
			_lyrics = Lyrics::None;
			update();
		}
	});
}

bool View::applyLyrics(const QJsonObject &object) {
	const auto synced = object.value(u"syncedLyrics"_q).toString();
	const auto plain = object.value(u"plainLyrics"_q).toString();
	if (!synced.isEmpty()) {
		_lines = ParseSynced(synced);
		if (!_lines.empty()) {
			_lyrics = Lyrics::Synced;
		}
	}
	if (_lines.empty() && !plain.isEmpty()) {
		for (const auto &text : plain.split('\n')) {
			_lines.push_back({ .time = -1, .text = text.trimmed() });
		}
		_lyrics = Lyrics::Plain;
	}
	if (_lines.empty()) {
		return false;
	}
	_linesWidth = 0;
	_current = -1;
	updateCurrentLine();
	update();
	return true;
}

QRect View::coverRect() const {
	const auto size = S(272);
	return QRect(S(40), S(36), size, size);
}

QRect View::sliderRect() const {
	const auto cover = coverRect();
	return QRect(cover.x(), S(392), cover.width(), S(4));
}

QPoint View::buttonCenter(Button button) const {
	const auto cover = coverRect();
	const auto x = cover.x() + cover.width() / 2;
	const auto y = S(460);
	const auto row = y + S(56);
	switch (button) {
	case Button::Previous: return QPoint(x - S(76), y);
	case Button::Next: return QPoint(x + S(76), y);
	case Button::Shuffle: return QPoint(x - S(105), row);
	case Button::Repeat: return QPoint(x - S(35), row);
	case Button::ShowMessage: return QPoint(x + S(35), row);
	case Button::CopyLyrics: return QPoint(x + S(105), row);
	default: return QPoint(x, y);
	}
}

QRect View::lyricsRect() const {
	const auto left = coverRect().right() + S(48);
	return QRect(left, S(24), width() - left - S(32), _height - S(48));
}

void View::layoutLines() {
	const auto rect = lyricsRect();
	if (_linesWidth == rect.width()) {
		return;
	}
	_linesWidth = rect.width();
	auto top = 0;
	for (auto &line : _lines) {
		const auto text = line.text.isEmpty() ? u"..."_q : line.text;
		line.top = top;
		line.height = QFontMetrics(_currentFont->f).boundingRect(
			QRect(0, 0, rect.width(), 10000),
			Qt::TextWordWrap,
			text).height();
		top += line.height + S(14);
	}
	_linesHeight = top;
}

void View::updateCurrentLine() {
	if (_lyrics != Lyrics::Synced || _lines.empty()) {
		return;
	}
	layoutLines();
	const auto now = position();
	auto current = -1;
	for (auto i = 0; i != int(_lines.size()); ++i) {
		if (_lines[i].time <= now) {
			current = i;
		} else {
			break;
		}
	}
	if (current == _current) {
		return;
	}
	_current = current;
	const auto rect = lyricsRect();
	const auto &line = _lines[std::max(current, 0)];
	const auto target = std::clamp(
		float64(line.top + line.height / 2 - rect.height() / 3),
		0.,
		float64(std::max(_linesHeight - rect.height() / 2, 0)));
	_scrollFrom = _scroll;
	_scrollTo = target;
	_scrollAnimation.stop();
	_scrollAnimation.start([=] {
		_scroll = _scrollFrom
			+ (_scrollTo - _scrollFrom) * _scrollAnimation.value(1.);
		update();
	}, 0., 1., 420, anim::easeOutCirc);
}

Button View::buttonAt(QPoint point, int *line) const {
	const auto radius = S(26);
	for (const auto button : { Button::Previous, Button::Play, Button::Next }) {
		const auto center = buttonCenter(button);
		if ((point - center).manhattanLength() <= radius * 1.4) {
			return button;
		}
	}
	for (const auto button : {
			Button::Shuffle,
			Button::Repeat,
			Button::ShowMessage,
			Button::CopyLyrics }) {
		const auto center = buttonCenter(button);
		if ((point - center).manhattanLength() <= S(22)) {
			return button;
		}
	}
	if (sliderRect().marginsAdded({ S(6), S(12), S(6), S(12) }).contains(point)) {
		return Button::Slider;
	}
	const auto rect = lyricsRect();
	if (_lyrics == Lyrics::Synced && rect.contains(point)) {
		const auto y = point.y() - rect.y() + int(_scroll);
		for (auto i = 0; i != int(_lines.size()); ++i) {
			const auto &entry = _lines[i];
			if (y >= entry.top && y < entry.top + entry.height) {
				if (line) {
					*line = i;
				}
				return Button::Line;
			}
		}
	}
	return Button::None;
}

void View::seekTo(float64 progress) {
	const auto instance = Media::Player::instance();
	instance->startSeeking(Type::Song);
	instance->finishSeeking(Type::Song, std::clamp(progress, 0., 1.));
}

void View::mouseMoveEvent(QMouseEvent *e) {
	if (_seeking) {
		const auto rect = sliderRect();
		_seekValue = std::clamp(
			float64(e->pos().x() - rect.x()) / rect.width(),
			0.,
			1.);
		update();
		return;
	}
	const auto over = buttonAt(e->pos());
	if (_over != over) {
		_over = over;
		setCursor((over == Button::None)
			? style::cur_default
			: style::cur_pointer);
		update();
	}
}

void View::leaveEventHook(QEvent *e) {
	if (!_seeking && _over != Button::None) {
		_over = Button::None;
		setCursor(style::cur_default);
		update();
	}
}

void View::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (buttonAt(e->pos()) == Button::Slider && length() > 0) {
		_seeking = true;
		Media::Player::instance()->startSeeking(Type::Song);
		mouseMoveEvent(e);
	}
}

void View::mouseReleaseEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	if (_seeking) {
		_seeking = false;
		Media::Player::instance()->finishSeeking(Type::Song, _seekValue);
		update();
		return;
	}
	auto line = -1;
	const auto instance = Media::Player::instance();
	switch (buttonAt(e->pos(), &line)) {
	case Button::Previous: instance->previous(Type::Song); break;
	case Button::Next: instance->next(Type::Song); break;
	case Button::Play: instance->playPause(Type::Song); break;
	case Button::Shuffle: {
		auto &settings = Core::App().settings();
		settings.setPlayerOrderMode(
			(settings.playerOrderMode() == Media::OrderMode::Shuffle)
				? Media::OrderMode::Default
				: Media::OrderMode::Shuffle);
		Core::App().saveSettingsDelayed();
		update();
	} break;
	case Button::Repeat: {
		auto &settings = Core::App().settings();
		const auto mode = settings.playerRepeatMode();
		settings.setPlayerRepeatMode((mode == Media::RepeatMode::None)
			? Media::RepeatMode::All
			: (mode == Media::RepeatMode::All)
			? Media::RepeatMode::One
			: Media::RepeatMode::None);
		Core::App().saveSettingsDelayed();
		update();
	} break;
	case Button::ShowMessage:
		if (const auto item = _controller->session().data().message(
				_context)) {
			_controller->showMessage(item);
			for (auto w = parentWidget(); w; w = w->parentWidget()) {
				if (const auto box = dynamic_cast<Ui::BoxContent*>(w)) {
					box->closeBox();
					break;
				}
			}
		}
		break;
	case Button::CopyLyrics:
		if (!_lines.empty()) {
			auto text = QStringList();
			for (const auto &entry : _lines) {
				text.push_back(entry.text);
			}
			QGuiApplication::clipboard()->setText(text.join('\n'));
			_controller->showToast(tr::lng_exd_player_lyrics_copied(tr::now));
		}
		break;
	case Button::Line:
		if (line >= 0 && length() > 0) {
			seekTo(float64(_lines[line].time) / length());
		}
		break;
	default: break;
	}
}

void View::wheelEvent(QWheelEvent *e) {
	if (_lyrics != Lyrics::Plain && _lyrics != Lyrics::Synced) {
		return;
	}
	layoutLines();
	const auto rect = lyricsRect();
	_scrollAnimation.stop();
	_scroll = std::clamp(
		_scroll - e->angleDelta().y() / 2.,
		0.,
		float64(std::max(_linesHeight - rect.height() / 2, 0)));
	update();
}

void View::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);

	const auto progress = _colors.value(1.);
	auto gradient = QLinearGradient(0, 0, width() / 3, height());
	gradient.setColorAt(0., Mix(_wasTop, _top, progress));
	gradient.setColorAt(1., Mix(_wasBottom, _bottom, progress));
	const auto radius = st::boxRadius;
	auto clip = QPainterPath();
	clip.addRoundedRect(rect(), radius, radius);
	p.setClipPath(clip);
	p.fillRect(rect(), gradient);
	if (!_backdrop.isNull()) {
		p.setOpacity(0.32 * progress);
		p.drawImage(rect(), _backdrop);
		p.setOpacity(1.);
		auto shade = QLinearGradient(0, 0, 0, height());
		shade.setColorAt(0., QColor(0, 0, 0, 40));
		shade.setColorAt(1., QColor(0, 0, 0, 140));
		p.fillRect(rect(), shade);
	}

	const auto cover = coverRect();
	auto path = QPainterPath();
	path.addRoundedRect(cover, S(16), S(16));
	if (!_cover.isNull()) {
		p.save();
		p.setClipPath(path);
		const auto scaled = _cover.size().scaled(
			cover.size(),
			Qt::KeepAspectRatioByExpanding);
		p.drawImage(
			QRect(
				cover.x() - (scaled.width() - cover.width()) / 2,
				cover.y() - (scaled.height() - cover.height()) / 2,
				scaled.width(),
				scaled.height()),
			_cover);
		p.restore();
	} else {
		p.fillPath(path, QColor(255, 255, 255, 28));
		p.setPen(QColor(255, 255, 255, 150));
		p.setFont(style::font(S(72), style::FontFlags(), 0)->f);
		p.drawText(cover, Qt::AlignCenter, QString(QChar(0x266B)));
	}

	paintControls(p);
	paintLyrics(p);
}

void View::paintControls(QPainter &p) {
	const auto cover = coverRect();
	p.setPen(Qt::white);
	p.setFont(_titleFont->f);
	p.drawText(
		QRect(cover.x(), S(328), cover.width(), _titleFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		_titleFont->elided(_title, cover.width()));
	p.setPen(QColor(255, 255, 255, 170));
	p.setFont(_subtitleFont->f);
	p.drawText(
		QRect(cover.x(), S(356), cover.width(), _subtitleFont->height),
		Qt::AlignLeft | Qt::AlignVCenter,
		_subtitleFont->elided(_performer, cover.width()));

	const auto slider = sliderRect();
	const auto total = length();
	const auto now = position();
	const auto value = (total > 0)
		? std::clamp(float64(now) / total, 0., 1.)
		: 0.;
	const auto sliderRadius = slider.height() / 2.;
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(255, 255, 255, 60));
	p.drawRoundedRect(slider, sliderRadius, sliderRadius);
	p.setBrush(Qt::white);
	const auto filled = int(slider.width() * value);
	p.drawRoundedRect(
		QRect(slider.x(), slider.y(), filled, slider.height()),
		sliderRadius,
		sliderRadius);
	if (_over == Button::Slider || _seeking) {
		p.drawEllipse(
			QPointF(slider.x() + filled, slider.y() + sliderRadius),
			S(7),
			S(7));
	}
	p.setFont(_timeFont->f);
	p.setPen(QColor(255, 255, 255, 150));
	const auto timesTop = slider.bottom() + S(8);
	p.drawText(
		QRect(slider.x(), timesTop, slider.width(), _timeFont->height),
		Qt::AlignLeft,
		FormatTime(now));
	p.drawText(
		QRect(slider.x(), timesTop, slider.width(), _timeFont->height),
		Qt::AlignRight,
		u"-"_q + FormatTime(total - now));

	const auto paintSkip = [&](QPoint center, bool forward, bool over) {
		p.setBrush(QColor(255, 255, 255, over ? 255 : 210));
		const auto h = S(9);
		const auto w = S(11);
		const auto dir = forward ? 1 : -1;
		for (auto i = 0; i != 2; ++i) {
			const auto start = center.x() - dir * w + dir * i * w;
			auto triangle = QPolygonF();
			triangle << QPointF(start, center.y() - h)
				<< QPointF(start + dir * w, center.y())
				<< QPointF(start, center.y() + h);
			p.drawPolygon(triangle);
		}
		const auto barX = forward
			? (center.x() + w)
			: (center.x() - w - S(3));
		p.drawRect(QRectF(barX, center.y() - h, S(3), 2 * h));
	};
	paintSkip(
		buttonCenter(Button::Previous),
		false,
		_over == Button::Previous);
	paintSkip(buttonCenter(Button::Next), true, _over == Button::Next);

	const auto &settings = Core::App().settings();
	const auto shuffle = (settings.playerOrderMode()
		== Media::OrderMode::Shuffle);
	const auto repeat = settings.playerRepeatMode();
	const auto paintIcon = [&](
			Button button,
			const style::icon &icon,
			bool active) {
		const auto center = buttonCenter(button);
		const auto size = S(36);
		const auto rect = QRect(
			center.x() - size / 2,
			center.y() - size / 2,
			size,
			size);
		if (active || _over == button) {
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(255, 255, 255, active ? 46 : 24));
			p.drawRoundedRect(rect, size / 2., size / 2.);
		}
		icon.paintInCenter(
			p,
			rect,
			QColor(255, 255, 255, (active || _over == button) ? 255 : 170));
	};
	paintIcon(Button::Shuffle, st::mediaPlayerShuffleIcon, shuffle);
	paintIcon(
		Button::Repeat,
		(repeat == Media::RepeatMode::One)
			? st::mediaPlayerRepeatOneIcon
			: st::mediaPlayerRepeatButton.icon,
		repeat != Media::RepeatMode::None);
	paintIcon(Button::ShowMessage, st::menuIconShowInChat, false);
	paintIcon(Button::CopyLyrics, st::menuIconCopy, false);

	const auto play = buttonCenter(Button::Play);
	const auto playRadius = S(30);
	p.setBrush(QColor(255, 255, 255, (_over == Button::Play) ? 255 : 235));
	p.drawEllipse(QPointF(play), playRadius, playRadius);
	p.setBrush(Mix(_wasBottom, _bottom, _colors.value(1.)));
	if (Media::Player::ShowPauseIcon(_state.state)) {
		const auto h = S(11);
		p.drawRoundedRect(
			QRectF(play.x() - S(9), play.y() - h, S(6), 2 * h),
			S(2),
			S(2));
		p.drawRoundedRect(
			QRectF(play.x() + S(3), play.y() - h, S(6), 2 * h),
			S(2),
			S(2));
	} else {
		const auto h = S(12);
		auto triangle = QPolygonF();
		triangle << QPointF(play.x() - S(7), play.y() - h)
			<< QPointF(play.x() + S(13), play.y())
			<< QPointF(play.x() - S(7), play.y() + h);
		p.drawPolygon(triangle);
	}
}

void View::paintLyrics(QPainter &p) {
	const auto rect = lyricsRect();
	if (_lyrics == Lyrics::Loading || _lyrics == Lyrics::None) {
		p.setPen(QColor(255, 255, 255, 140));
		p.setFont(_subtitleFont->f);
		p.drawText(
			rect,
			Qt::AlignCenter | Qt::TextWordWrap,
			(_lyrics == Lyrics::Loading)
				? tr::lng_exd_player_lyrics_loading(tr::now)
				: tr::lng_exd_player_lyrics_none(tr::now));
		return;
	}
	layoutLines();
	p.save();
	p.setClipRect(rect);
	const auto synced = (_lyrics == Lyrics::Synced);
	for (auto i = 0; i != int(_lines.size()); ++i) {
		const auto &line = _lines[i];
		const auto top = rect.y() + line.top - int(_scroll);
		if (top + line.height < rect.y() || top > rect.bottom()) {
			continue;
		}
		const auto current = synced && (i == _current);
		const auto passed = synced && (i < _current);
		const auto edge = std::min(
			top + line.height - rect.y(),
			rect.bottom() - top);
		const auto edgeFade = std::clamp(edge / float64(S(48)), 0., 1.);
		const auto alpha = current
			? 255
			: !synced
			? 220
			: passed
			? 90
			: 120;
		p.setPen(QColor(255, 255, 255, int(alpha * edgeFade)));
		p.setFont((current ? _currentFont : _lineFont)->f);
		p.drawText(
			QRect(rect.x(), top, rect.width(), line.height),
			Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
			line.text.isEmpty() ? QString(QChar(0x266A)) : line.text);
	}
	p.restore();
}

} // namespace

class PlayerBox final : public Ui::BoxContent {
public:
	PlayerBox(QWidget*, not_null<Window::SessionController*> controller)
	: _controller(controller) {
	}

protected:
	void prepare() override {
		const auto view = Ui::CreateChild<View>(this, _controller);
		view->resizeToWidth(S(760));
		view->moveToLeft(0, 0);
		view->show();
		setStyle(st::exteraPlayerBox);
		setNoContentMargin(true);
		setDimensions(S(760), view->height());
	}

private:
	const not_null<Window::SessionController*> _controller;

};

void Show(not_null<Window::SessionController*> controller) {
	controller->show(Box<PlayerBox>(controller));
}

} // namespace Extera::FullPlayer
