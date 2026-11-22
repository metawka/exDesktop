/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_pills.h"

#include "extera/extera_streamer.h"
#include "data/data_saved_music.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "extera/extera_config.h"
#include "main/main_session.h"
#include "ui/image/image.h"
#include "ui/image/image_prepare.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history_item.h"
#include "info/profile/info_profile_balance_pill.h"
#include "info/profile/info_profile_music_button.h"
#include "info/profile/info_profile_top_bar.h"
#include "ui/text/format_song_document_name.h"
#include "ui/rect.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/effects/animation_value.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "ui/abstract_button.h"
#include "ui/text/text_utilities.h"
#include "styles/style_layers.h"
#include "wallet/wallet_card_view.h"

#include "styles/style_info.h"
#include "styles/style_info_profile_top_bar.h"
#include "styles/style_widgets.h"
#include "base/event_filter.h"
#include "ui/widgets/popup_menu.h"

namespace Info::Profile {
namespace {

void ShowMusicViewMenu(not_null<QWidget*> parent, bool toCard) {
	const auto menu = Ui::CreateChild<Ui::PopupMenu>(
		parent.get(),
		st::popupMenuWithIcons);
	menu->deleteOnHide(true);
	menu->addAction(
		(toCard
			? tr::lng_exd_music_view_card
			: tr::lng_exd_music_view_standard)(tr::now),
		[=] {
			Extera::Config::Instance().setBool(
				u"exd_profile_music_card"_q,
				toCard);
		});
	menu->popup(QCursor::pos());
}

void SetupMusicCover(
		not_null<MusicButton*> music,
		not_null<DocumentData*> document,
		not_null<UserData*> user,
		rpl::lifetime &lifetime) {
	lifetime.destroy();
	music->setCover(QImage(), std::nullopt);
	if (!Extera::Bool(u"profile_music_cover"_q)) {
		return;
	}
	const auto media = document->createMediaView();
	document->loadThumbnail(Data::FileOriginFullUser{ peerToUser(user->id) });
	const auto apply = [=] {
		const auto image = media->thumbnail();
		if (!image || image->isNull()) {
			return false;
		}
		const auto ratio = style::DevicePixelRatio();
		const auto side = st::infoProfileBalancePillHeight
			- 2 * style::ConvertScale(3);
		auto source = image->original();
		const auto square = std::min(source.width(), source.height());
		source = source.copy(
			(source.width() - square) / 2,
			(source.height() - square) / 2,
			square,
			square);
		auto scaled = source.scaled(
			QSize(side, side) * ratio,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation).convertToFormat(
				QImage::Format_ARGB32_Premultiplied);
		scaled.setDevicePixelRatio(ratio);
		const auto color = source.scaled(
			1,
			1,
			Qt::IgnoreAspectRatio,
			Qt::SmoothTransformation).pixelColor(0, 0);
		music->setCover(Images::Circle(std::move(scaled)), color);
		return true;
	};
	if (apply()) {
		return;
	}
	document->session().downloaderTaskFinished(
	) | rpl::filter([=] {
		return apply();
	}) | rpl::take(1) | rpl::on_next([] {
	}, lifetime);
}

} // namespace
namespace {

constexpr auto kFadeStart = 0.6;

[[nodiscard]] rpl::producer<DocumentData*> ProfileMusicValue(
		not_null<UserData*> user) {
	const auto savedMusic = &user->owner().savedMusic();
	const auto peerId = user->id;
	return rpl::single(
		peerId
	) | rpl::then(
		savedMusic->changed() | rpl::filter(rpl::mappers::_1 == peerId)
	) | rpl::map([=]() -> DocumentData* {
		const auto &list = savedMusic->list(peerId);
		const auto media = list.empty() ? nullptr : list.front()->media();
		return media ? media->document() : nullptr;
	}) | rpl::distinct_until_changed();
}

} // namespace

PillsRow::PillsRow(
	not_null<Ui::RpWidget*> parent,
	not_null<Window::SessionController*> controller,
	not_null<UserData*> user,
	rpl::producer<std::optional<QColor>> edgeColor,
	Fn<void()> showSavedMusic,
	bool underButtons)
: _parent(parent)
, _balance(std::make_unique<BalancePill>(
	parent,
	user,
	rpl::duplicate(edgeColor)))
, _music(std::make_unique<MusicButton>(parent)) {
	const auto balance = _balance.get();
	balance->setClickedCallback([=] {
		if (balance->actionable()) {
			Wallet::ShowCardView(controller, user, balance);
		}
	});
	const auto music = _music.get();
	music->setClickedCallback([=] {
		if (music->actionable() && showSavedMusic) {
			showSavedMusic();
		}
	});

	std::move(
		edgeColor
	) | rpl::on_next([=](std::optional<QColor> color) {
		music->setOverrideBg(color);
	}, _lifetime);

	base::install_event_filter(music, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ContextMenu) {
			return base::EventFilterResult::Continue;
		}
		ShowMusicViewMenu(music, true);
		return base::EventFilterResult::Cancel;
	});
	rpl::combine(
		ProfileMusicValue(user),
		rpl::single(
			Extera::Bool(u"exd_profile_music_card"_q)
		) | rpl::then(Extera::Config::Instance().boolChanges(
			u"exd_profile_music_card"_q))
	) | rpl::on_next([=](DocumentData *document, bool card) {
		if (document && !card) {
			music->updateData({ Ui::Text::FormatSongNameFor(document) });
			SetupMusicCover(music, document, user, _coverLifetime);
			music->show();
		} else {
			music->hide();
		}
	}, _lifetime);

	// Without the action buttons the row fits the gap below the status.
	if (underButtons) {
		_reserve = rpl::combine(
			balance->shownValue(),
			music->shownValue()
		) | rpl::map([](bool hasBalance, bool hasMusic) {
			return (hasBalance || hasMusic)
				? st::infoProfileTopBarPillsReserve
				: 0;
		});
	}

	rpl::merge(
		balance->naturalWidthValue() | rpl::to_empty,
		balance->shownValue() | rpl::to_empty,
		music->naturalWidthValue() | rpl::to_empty,
		music->shownValue() | rpl::to_empty
	) | rpl::on_next([=] {
		layout();
	}, _lifetime);
}

PillsRow::~PillsRow() = default;

rpl::producer<int> PillsRow::reserveValue() const {
	return _reserve.value();
}

void PillsRow::place(int top, float64 progress) {
	const auto opacity = std::clamp(
		(progress - kFadeStart) / (1. - kFadeStart),
		0.,
		1.);
	_balance->setOpacity(opacity);
	_music->setOpacity(opacity);
	_top = top;
	if (_outerWidth != _parent->width()) {
		layout();
	} else {
		_balance->move(_balance->x(), _top);
		_music->move(_music->x(), _top);
	}
}

rpl::lifetime &PillsRow::lifetime() {
	return _lifetime;
}

void PillsRow::layout() {
	_outerWidth = _parent->width();
	const auto &padding = st::infoProfileTopBarActionButtonsPadding;
	const auto available = _outerWidth - rect::m::sum::h(padding);
	if (available <= 0) {
		return;
	}
	const auto hasBalance = !_balance->isHidden();
	const auto hasMusic = !_music->isHidden();
	const auto gap = (hasBalance && hasMusic)
		? st::infoProfileTopBarPillsGap
		: 0;
	auto balanceWidth = 0;
	if (hasBalance) {
		const auto chrome = hasMusic ? _music->minimumWidth() : 0;
		_balance->resizeToNaturalWidth(
			std::max(available - gap - chrome, 0));
		const auto fitted = _balance->fittedWidth();
		if (fitted < _balance->width()) {
			_balance->resizeToWidth(fitted);
		}
		balanceWidth = _balance->width();
	}
	auto musicWidth = 0;
	if (hasMusic) {
		_music->resizeToNaturalWidth(
			std::max(available - gap - balanceWidth, 0));
		musicWidth = _music->width();
	}
	const auto left = padding.left()
		+ (available - musicWidth - gap - balanceWidth) / 2;
	_music->move(left, _top);
	_balance->move(left + musicWidth + gap, _top);
}

void TopBar::setupPills(
		not_null<Window::SessionController*> controller,
		Fn<void()> showSavedMusic) {
	const auto user = _peer->asUser();
	if (!user
		|| _savedMessages
		|| _topic
		|| (user->isSelf() && Extera::Streamer::HideProfile())
		|| !(_source == Source::Profile
			|| (_source == Source::Stories && user->isSelf()))) {
		return;
	}
	_pills = std::make_unique<PillsRow>(
		this,
		controller,
		user,
		_edgeColor.value(),
		std::move(showSavedMusic),
		_hasActions);
	_pills->reserveValue(
	) | rpl::on_next([=](int reserve) {
		applyPillsReserve(reserve);
	}, _pills->lifetime());
}

void TopBar::applyPillsReserve(int reserve) {
	const auto applied = _pillsReserve.current();
	if (applied == reserve) {
		return;
	}
	const auto base = QWidget::maximumHeight() - applied;
	QWidget::setMaximumHeight(base + reserve);
	_cachedGradient = QImage();
	_cachedClipPath = QPainterPath();
	_pillsReserve = reserve;
	updateLabelsPosition();
	update();
}

rpl::producer<int> TopBar::desiredHeightValue() const {
	return _pillsReserve.value() | rpl::map([=] {
		return QWidget::maximumHeight();
	});
}

namespace {

[[nodiscard]] QColor ExdAdjustLightness(QColor color, float64 factor) {
	auto h = 0.f;
	auto sat = 0.f;
	auto l = 0.f;
	color.getHslF(&h, &sat, &l);
	return QColor::fromHslF(h, sat, std::min(l * factor, 1.));
}

[[nodiscard]] float64 ExdLuminance(const QColor &color) {
	const auto channel = [](float64 value) {
		return (value <= 0.03928)
			? (value / 12.92)
			: std::pow((value + 0.055) / 1.055, 2.4);
	};
	return 0.2126 * channel(color.redF())
		+ 0.7152 * channel(color.greenF())
		+ 0.0722 * channel(color.blueF());
}

[[nodiscard]] float64 ExdContrastWithWhite(const QColor &color) {
	return 1.05 / (ExdLuminance(color) + 0.05);
}

[[nodiscard]] QColor ExdCoverBase(const QImage &image) {
	const auto small = image.scaled(
		24,
		24,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
	auto best = QColor();
	auto bestScore = -1.;
	auto sumR = 0.;
	auto sumG = 0.;
	auto sumB = 0.;
	auto count = 0;
	for (auto y = 0; y != small.height(); ++y) {
		for (auto x = 0; x != small.width(); ++x) {
			const auto color = QColor::fromRgb(small.pixel(x, y));
			sumR += color.redF();
			sumG += color.greenF();
			sumB += color.blueF();
			++count;
			auto h = 0.f;
			auto sat = 0.f;
			auto l = 0.f;
			color.getHslF(&h, &sat, &l);
			const auto score = sat * (1. - std::abs(l - 0.35) * 2.);
			if (score > bestScore) {
				bestScore = score;
				best = color;
			}
		}
	}
	auto base = (bestScore > 0.25 || !count)
		? best
		: QColor::fromRgbF(sumR / count, sumG / count, sumB / count);
	const auto contrast = ExdContrastWithWhite(base);
	if (contrast > 15.) {
		base = ExdAdjustLightness(base, 2.);
	} else if (contrast < 10.) {
		base = ExdAdjustLightness(base, 0.5);
	}
	if (ExdContrastWithWhite(base) < 3.) {
		base = anim::color(base, QColor(0, 0, 0), 0.3);
	}
	return base;
}

struct ExdCachedCover {
	QImage cover;
	QColor base;
};

[[nodiscard]] base::flat_map<DocumentId, ExdCachedCover> &ExdCoverCache() {
	static auto result = base::flat_map<DocumentId, ExdCachedCover>();
	return result;
}

class ProfileMusicCard final : public Ui::AbstractButton {
public:
	ProfileMusicCard(
		not_null<QWidget*> parent,
		not_null<UserData*> user);

	void setDocument(DocumentData *document);

private:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;
	void applyCover();

	const not_null<UserData*> _user;
	DocumentData *_document = nullptr;
	std::shared_ptr<Data::DocumentMedia> _media;
	QString _title;
	QString _performer;
	QImage _cover;
	std::optional<QColor> _base;
	rpl::lifetime _coverLifetime;
	Ui::Animations::Simple _coverFade;

};

ProfileMusicCard::ProfileMusicCard(
	not_null<QWidget*> parent,
	not_null<UserData*> user)
: AbstractButton(parent)
, _user(user) {
	setCursor(style::cur_pointer);
	style::PaletteChanged(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

void ProfileMusicCard::setDocument(DocumentData *document) {
	if (_document == document) {
		return;
	}
	_document = document;
	_coverLifetime.destroy();
	_cover = QImage();
	_base = std::nullopt;
	_media = nullptr;
	if (!document) {
		update();
		return;
	}
	const auto name = Ui::Text::FormatSongNameFor(document).composedName();
	_title = name.title.isEmpty()
		? document->filename()
		: name.title;
	_performer = name.performer;
	_media = document->createMediaView();
	_coverFade.stop();
	if (const auto i = ExdCoverCache().find(document->id)
		; i != end(ExdCoverCache())) {
		_cover = i->second.cover;
		_base = i->second.base;
		update();
		return;
	}
	document->loadThumbnail(
		Data::FileOriginFullUser{ peerToUser(_user->id) });
	applyCover();
	if (_cover.isNull()) {
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			if (_cover.isNull()) {
				applyCover();
			}
		}, _coverLifetime);
	}
	update();
}

void ProfileMusicCard::applyCover() {
	const auto image = _media ? _media->thumbnail() : nullptr;
	if (!image || image->isNull()) {
		return;
	}
	auto source = image->original();
	const auto square = std::min(source.width(), source.height());
	source = source.copy(
		(source.width() - square) / 2,
		(source.height() - square) / 2,
		square,
		square);
	_base = ExdCoverBase(source);
	const auto ratio = style::DevicePixelRatio();
	const auto side = style::ConvertScale(52);
	auto scaled = source.scaled(
		QSize(side, side) * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation).convertToFormat(
			QImage::Format_ARGB32_Premultiplied);
	scaled.setDevicePixelRatio(ratio);
	_cover = Images::Round(
		std::move(scaled),
		Images::CornersMask(style::ConvertScale(10)));
	_coverLifetime.destroy();
	if (_document) {
		ExdCoverCache()[_document->id] = ExdCachedCover{
			.cover = _cover,
			.base = *_base,
		};
	}
	if (isVisible() && !anim::Disabled()) {
		_coverFade.start(
			[=] { update(); },
			0.,
			1.,
			st::slideWrapDuration);
	}
	update();
}

int ProfileMusicCard::resizeGetHeight(int newWidth) {
	return style::ConvertScale(72);
}

void ProfileMusicCard::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	auto hq = PainterHighQualityEnabler(p);
	const auto radius = style::ConvertScale(18);
	const auto skip = style::ConvertScale(10);
	const auto side = style::ConvertScale(52);
	const auto full = QRectF(rect());
	auto path = QPainterPath();
	path.addRoundedRect(full, radius, radius);
	p.setClipPath(path);

	const auto hasCover = _base.has_value();
	const auto shown = hasCover ? _coverFade.value(1.) : 0.;
	if (shown < 1.) {
		p.fillRect(rect(), st::boxBg);
	}
	if (hasCover) {
		auto gradient = QRadialGradient(
			QPointF(width(), height() / 2.),
			width() * 2.);
		gradient.setColorAt(0., ExdAdjustLightness(*_base, 1.5));
		gradient.setColorAt(1., *_base);
		p.setOpacity(shown);
		p.fillRect(rect(), gradient);
		p.setOpacity(1.);
	}

	auto h = 0.f;
	auto sat = 0.f;
	auto l = 0.f;
	if (hasCover) {
		_base->getHslF(&h, &sat, &l);
	}
	const auto accent = !hasCover
		? st::windowFg->c
		: ExdAdjustLightness(
			*_base,
			(l <= 0.25) ? 2. : (l <= 0.5) ? 1.5 : (l <= 0.75) ? 1. : 0.5);
	static constexpr float64 kPattern[] = {
		-5.5, 20., 20., 0.35,
		-5.5, -20., 20., 0.35,
		-36., -42., 22., 0.375,
		-36., 0., 25., 0.425,
		-36., 42., 22., 0.375,
		-70., 22., 23., 0.35,
		-70., -22., 23., 0.35,
		-99., 46., 21., 0.275,
		-99., 0., 22., 0.325,
		-99., -46., 21., 0.275,
		-128., -23., 20., 0.225,
		-128., 23., 20., 0.225,
	};
	const auto patternAlpha = hasCover ? 1. : 0.4;
	auto noteFont = QFont(st::semiboldFont->f);
	for (auto i = 0; i != int(std::size(kPattern)); i += 4) {
		const auto x = style::ConvertScaleExact(kPattern[i]) + width();
		const auto y = style::ConvertScaleExact(kPattern[i + 1])
			+ height() / 2.;
		const auto size = style::ConvertScaleExact(kPattern[i + 2]);
		noteFont.setPixelSize(std::max(int(size), 1));
		p.setFont(noteFont);
		auto color = accent;
		color.setAlphaF(patternAlpha * kPattern[i + 3]);
		p.setPen(color);
		p.drawText(
			QRectF(x - size / 2., y - size / 2., size, size),
			Qt::AlignCenter,
			QString(QChar(0x266B)));
	}

	const auto coverRect = QRect(skip, (height() - side) / 2, side, side);
	if (!_cover.isNull() && shown >= 1.) {
		p.drawImage(coverRect, _cover);
	} else {
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			coverRect,
			style::ConvertScale(10),
			style::ConvertScale(10));
		auto note = QFont(st::semiboldFont->f);
		note.setPixelSize(side / 2);
		p.setFont(note);
		p.setPen(st::windowSubTextFg);
		p.drawText(coverRect, Qt::AlignCenter, QString(QChar(0x266B)));
		if (!_cover.isNull()) {
			p.setOpacity(shown);
			p.drawImage(coverRect, _cover);
			p.setOpacity(1.);
		}
	}

	const auto textLeft = coverRect.x() + side + skip;
	const auto textWidth = width() - textLeft - skip;
	if (textWidth <= 0) {
		return;
	}
	const auto textColor = anim::color(
		st::windowFg->c,
		QColor(255, 255, 255),
		shown);
	auto titleFont = QFont(st::semiboldFont->f);
	titleFont.setPixelSize(style::ConvertScale(15));
	auto artistFont = QFont(st::normalFont->f);
	artistFont.setPixelSize(style::ConvertScale(13));
	const auto titleMetrics = QFontMetrics(titleFont);
	const auto artistMetrics = QFontMetrics(artistFont);
	const auto gap = style::ConvertScale(2);
	const auto block = titleMetrics.height()
		+ (_performer.isEmpty() ? 0 : (gap + artistMetrics.height()));
	auto top = (height() - block) / 2;
	p.setPen(textColor);
	p.setFont(titleFont);
	p.drawText(
		textLeft,
		top + titleMetrics.ascent(),
		titleMetrics.elidedText(_title, Qt::ElideRight, textWidth));
	if (!_performer.isEmpty()) {
		top += titleMetrics.height() + gap;
		auto faded = textColor;
		faded.setAlphaF(0.6);
		p.setPen(faded);
		p.setFont(artistFont);
		p.drawText(
			textLeft,
			top + artistMetrics.ascent(),
			artistMetrics.elidedText(_performer, Qt::ElideRight, textWidth));
	}
}

} // namespace

object_ptr<Ui::RpWidget> CreateProfileMusicCard(
		not_null<Ui::RpWidget*> parent,
		not_null<UserData*> user,
		Fn<void()> showSavedMusic) {
	auto result = object_ptr<Ui::SlideWrap<Ui::RpWidget>>(
		parent,
		object_ptr<ProfileMusicCard>(parent, user),
		st::infoProfileMusicCardPadding);
	const auto wrap = result.data();
	const auto card = static_cast<ProfileMusicCard*>(wrap->entity());
	const auto savedMusic = &user->owner().savedMusic();
	if (Data::SavedMusic::Supported(user->id)
		&& savedMusic->list(user->id).empty()) {
		if (user->isSelf()) {
			savedMusic->reload(user->id);
		} else if (!savedMusic->fullyLoaded(user->id)) {
			savedMusic->loadMore(user->id);
		}
	}
	card->setClickedCallback([=] {
		if (showSavedMusic) {
			showSavedMusic();
		}
	});
	base::install_event_filter(card, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::ContextMenu) {
			return base::EventFilterResult::Continue;
		}
		ShowMusicViewMenu(card, false);
		return base::EventFilterResult::Cancel;
	});
	rpl::combine(
		ProfileMusicValue(user),
		rpl::single(
			Extera::Bool(u"exd_profile_music_card"_q)
		) | rpl::then(Extera::Config::Instance().boolChanges(
			u"exd_profile_music_card"_q))
	) | rpl::on_next([=](DocumentData *document, bool shown) {
		card->setDocument(document);
		wrap->toggle(shown && document != nullptr, anim::type::instant);
	}, wrap->lifetime());
	return result;
}

} // namespace Info::Profile
