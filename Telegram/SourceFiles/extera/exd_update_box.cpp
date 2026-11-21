#include "extera/exd_update_box.h"

#include "apiwrap.h"
#include "extera/extera_config.h"
#include "extera/extera_update.h"
#include "extera/exd_settings_sync.h"
#include "base/unixtime.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

namespace Exd {
namespace {

enum class Status {
	Checking,
	Latest,
	Available,
	Failed,
};

struct BoxState {
	Status status = Status::Checking;
	QString translated;
	QString translatedFor;
	bool showOriginal = false;
	bool translating = false;
	bool syncing = false;
	float64 progress = -1.;
	Fn<void()> render;
};

[[nodiscard]] QString FormatNotes(const QString &markdown) {
	auto lines = QStringList();
	for (auto line : markdown.split('\n')) {
		line = line.trimmed();
		line.replace(u"**"_q, QString());
		line.replace(u"__"_q, QString());
		while (line.startsWith('#')) {
			line = line.mid(1);
		}
		line = line.trimmed();
		if (line.startsWith(u"- "_q) || line.startsWith(u"* "_q)) {
			line = QString::fromUtf8("\xE2\x80\xA2 ") + line.mid(2).trimmed();
		}
		lines.push_back(line);
	}
	while (!lines.isEmpty() && lines.back().isEmpty()) {
		lines.pop_back();
	}
	return lines.join('\n');
}

[[nodiscard]] QString LanguageCode() {
	return Lang::LanguageIdOrDefault(Lang::Id()).left(2).toLower();
}

void PaintCard(not_null<Ui::RpWidget*> card) {
	card->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(card);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		const auto radius = st::exdUpdateCardRadius;
		p.drawRoundedRect(card->rect(), radius, radius);
	}, card->lifetime());
}

void Translate(
		not_null<Main::Session*> session,
		const QString &text,
		Fn<void(QString)> done) {
	using Flag = MTPmessages_TranslateText::Flag;
	session->api().request(MTPmessages_TranslateText(
		MTP_flags(Flag::f_text),
		MTP_inputPeerEmpty(),
		MTPVector<MTPint>(),
		MTP_vector<MTPTextWithEntities>(1, MTP_textWithEntities(
			MTP_string(text),
			MTP_vector<MTPMessageEntity>())),
		MTP_string(LanguageCode()),
		MTPstring()
	)).done([=](const MTPmessages_TranslatedText &result) {
		const auto &list = result.data().vresult().v;
		done(list.isEmpty()
			? QString()
			: qs(list.front().data().vtext()));
	}).fail([=] {
		done(QString());
	}).send();
}

} // namespace

void ShowUpdateBox(not_null<Window::SessionController*> controller) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setStyle(st::exteraInstallBox);
		box->setWidth(st::exteraInstallWidth);
		box->setNoContentMargin(true);
		box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

		const auto session = &controller->session();
		const auto state = box->lifetime().make_state<BoxState>();

		const auto header = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			st::exdUpdateHeaderPadding,
			style::al_justify);
		const auto logoSize = st::exdUpdateLogoSize;
		const auto texts = header->add(
			object_ptr<Ui::VerticalLayout>(header),
			style::margins(
				logoSize + st::exdUpdateLogoSkip,
				st::exdUpdateHeaderTextTop,
				0,
				0),
			style::al_justify);
		const auto spacer = header->add(object_ptr<Ui::RpWidget>(header));
		const auto logo = Ui::CreateChild<Ui::RpWidget>(header);
		logo->setGeometry(0, 0, logoSize, logoSize);
		logo->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(logo);
			auto hq = PainterHighQualityEnabler(p);
			static const auto image = QImage(u":/gui/art/logo_256.png"_q);
			p.drawImage(logo->rect(), image);
		}, logo->lifetime());
		texts->heightValue() | rpl::on_next([=](int height) {
			const auto full = height + st::exdUpdateHeaderTextTop;
			spacer->resize(spacer->width(), std::max(logoSize - full, 0));
		}, spacer->lifetime());
		texts->add(
			object_ptr<Ui::FlatLabel>(
				texts,
				rpl::single(u"exDesktop"_q),
				st::exdSideTitle),
			style::margins());
		texts->add(
			object_ptr<Ui::FlatLabel>(
				texts,
				tr::lng_exd_update_version(
					lt_version,
					rpl::single(Extera::Update::VersionText())),
				st::exdSideSubtitle),
			style::margins(0, st::exdSideSubtitleSkip, 0, 0));

		const auto body = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins(),
			style::al_justify);
		const auto buttons = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::exdUpdateButtonsPadding);
		buttons->resize(buttons->width(), st::exteraInstallButton.height);

		const auto check = std::make_shared<Fn<void()>>();
		const auto translateIfNeeded = [=] {
			const auto release = Extera::Update::LatestRelease();
			const auto notes = FormatNotes(release.notes);
			if (notes.isEmpty()
				|| LanguageCode() == u"en"_q
				|| state->translating
				|| state->translatedFor == release.version) {
				return;
			}
			state->translating = true;
			const auto weak = base::make_weak(box.get());
			Translate(session, notes, [=](QString text) {
				if (!weak) {
					return;
				}
				state->translating = false;
				state->translatedFor = release.version;
				state->translated = text;
				state->render();
			});
		};

		state->render = [=] {
			while (body->count()) {
				delete body->widgetAt(0);
			}
			for (const auto child : buttons->children()) {
				if (child->isWidgetType()) {
					child->deleteLater();
				}
			}
			const auto release = Extera::Update::LatestRelease();

			const auto statusCard = body->add(
				object_ptr<Ui::VerticalLayout>(body),
				st::exdUpdateCardMargin,
				style::al_justify);
			PaintCard(statusCard);
			const auto statusTitle = [&]() -> QString {
				switch (state->status) {
				case Status::Checking:
					return tr::lng_exd_update_checking(tr::now);
				case Status::Latest:
					return tr::lng_exd_update_latest(tr::now);
				case Status::Available:
					return tr::lng_exd_update_available(
						tr::now,
						lt_version,
						release.version);
				case Status::Failed:
					return tr::lng_exd_update_failed(tr::now);
				}
				return QString();
			}();
			auto statusAbout = QString();
			if (state->status == Status::Failed) {
				statusAbout = tr::lng_exd_update_failed_about(tr::now);
			} else if (state->status != Status::Checking
				&& release.published.isValid()) {
				statusAbout = tr::lng_exd_update_published(
					tr::now,
					lt_date,
					QLocale().toString(
						release.published,
						QLocale::ShortFormat));
				if (release.prerelease) {
					statusAbout += QString::fromUtf8(" \xE2\x80\xA2 ")
						+ tr::lng_exd_update_beta_badge(tr::now);
				}
			}
			statusCard->add(
				object_ptr<Ui::FlatLabel>(
					statusCard,
					rpl::single(statusTitle),
					st::exdUpdateCardTitle),
				statusAbout.isEmpty()
					? st::exdUpdateCardSinglePadding
					: st::exdUpdateCardPadding);
			if (!statusAbout.isEmpty()) {
				statusCard->add(
					object_ptr<Ui::FlatLabel>(
						statusCard,
						rpl::single(statusAbout),
						st::exdUpdateCardText),
					st::exdUpdateCardTextPadding);
			}

			const auto progress = body->add(
				object_ptr<Ui::RpWidget>(body),
				st::exdUpdateProgressMargin);
			progress->resize(progress->width(), st::exdUpdateProgressHeight);
			progress->paintRequest() | rpl::on_next([=] {
				auto p = QPainter(progress);
				auto hq = PainterHighQualityEnabler(p);
				const auto radius = progress->height() / 2.;
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgOver);
				p.drawRoundedRect(progress->rect(), radius, radius);
				const auto value = std::clamp(state->progress, 0., 1.);
				if (value > 0.) {
					auto filled = progress->rect();
					filled.setWidth(int(base::SafeRound(
						filled.width() * value)));
					p.setBrush(st::windowActiveTextFg);
					p.drawRoundedRect(filled, radius, radius);
				}
			}, progress->lifetime());
			progress->setVisible(state->progress >= 0. && state->progress < 1.);

			const auto notes = FormatNotes(release.notes);
			if (state->status == Status::Available && !notes.isEmpty()) {
				const auto card = body->add(
					object_ptr<Ui::VerticalLayout>(body),
					st::exdUpdateCardMargin,
					style::al_justify);
				PaintCard(card);
				card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						tr::lng_exd_update_notes(),
						st::exdUpdateCardTitle),
					st::exdUpdateCardPadding);
				const auto translated = !state->translated.isEmpty()
					&& (state->translatedFor == release.version);
				const auto shown = (translated && !state->showOriginal)
					? state->translated
					: notes;
				card->add(
					object_ptr<Ui::FlatLabel>(
						card,
						rpl::single(shown),
						st::exdUpdateNotes),
					st::exdUpdateCardTextPadding);
				if (translated) {
					const auto toggle = card->add(
						object_ptr<Ui::LinkButton>(
							card,
							state->showOriginal
								? tr::lng_exd_update_show_translation(tr::now)
								: tr::lng_exd_update_show_original(tr::now)),
						st::exdUpdateCardTextPadding);
					toggle->setClickedCallback([=] {
						state->showOriginal = !state->showOriginal;
						crl::on_main(box, [=] { state->render(); });
					});
				}
			}

			const auto syncCard = body->add(
				object_ptr<Ui::VerticalLayout>(body),
				st::exdUpdateCardMargin,
				style::al_justify);
			PaintCard(syncCard);
			syncCard->add(
				object_ptr<Ui::FlatLabel>(
					syncCard,
					tr::lng_exd_sync_title(),
					st::exdUpdateCardTitle),
				st::exdUpdateCardPadding);
			const auto syncDate = Exd::SettingsSync::LocalDate();
			syncCard->add(
				object_ptr<Ui::FlatLabel>(
					syncCard,
					rpl::single(state->syncing
						? tr::lng_exd_sync_running(tr::now)
						: (syncDate > 0)
						? tr::lng_exd_sync_last(
							tr::now,
							lt_date,
							QLocale().toString(
								base::unixtime::parse(syncDate),
								QLocale::ShortFormat))
						: tr::lng_exd_sync_never_done(tr::now)),
					st::exdUpdateCardText),
				st::exdUpdateCardTextPadding);
			const auto syncNow = syncCard->add(
				object_ptr<Ui::LinkButton>(
					syncCard,
					tr::lng_exd_sync_now(tr::now)),
				st::exdUpdateCardLinkPadding);
			syncNow->setClickedCallback([=] {
				if (state->syncing) {
					return;
				}
				state->syncing = true;
				crl::on_main(box, [=] { state->render(); });
				const auto weak = base::make_weak(box.get());
				Exd::SettingsSync::Upload(session, [=](QString error) {
					controller->showToast(error.isEmpty()
						? tr::lng_exd_sync_uploaded(tr::now)
						: tr::lng_exd_sync_failed(tr::now, lt_error, error));
					if (weak) {
						state->syncing = false;
						state->render();
					}
				});
			});

			const auto beta = body->add(object_ptr<Ui::SettingsButton>(
				body,
				tr::lng_exd_update_beta(),
				st::settingsButtonNoIcon));
			beta->toggleOn(rpl::single(Extera::Bool(u"exd_update_beta"_q)));
			beta->toggledChanges() | rpl::on_next([=](bool value) {
				Extera::Config::Instance().setBool(
					u"exd_update_beta"_q,
					value);
				crl::on_main(box, [=] { (*check)(); });
			}, beta->lifetime());
			body->add(
				object_ptr<Ui::FlatLabel>(
					body,
					tr::lng_exd_update_beta_about(),
					st::exdUpdateCardText),
				st::exdUpdateBetaAboutPadding);

			auto list = std::vector<not_null<Ui::RoundButton*>>();
			const auto add = [&](
					rpl::producer<QString> text,
					const style::RoundButton &st,
					Fn<void()> callback) {
				const auto button = Ui::CreateChild<Ui::RoundButton>(
					buttons,
					std::move(text),
					st);
				button->setFullRadius(true);
				button->setTextTransform(
					Ui::RoundButtonTextTransform::NoTransform);
				button->setClickedCallback(std::move(callback));
				button->show();
				list.push_back(button);
			};
			if (state->status == Status::Available) {
				add(tr::lng_exd_update_skip(), st::exteraInstallCancel, [=] {
					const auto version = release.version;
					Extera::Update::Skip();
					controller->showToast(tr::lng_exd_update_skipped(
						tr::now,
						lt_version,
						version));
					box->closeBox();
				});
			}
			add(tr::lng_exd_update_all(), st::exteraInstallCancel, [] {
				Extera::Update::OpenAllReleases();
			});
			if (state->status == Status::Available && release.installable) {
				const auto downloading = (state->progress >= 0.)
					&& (state->progress < 1.);
				add(
					downloading
						? tr::lng_exd_update_downloading(
							lt_percent,
							rpl::single(QString::number(
								int(state->progress * 100))))
						: tr::lng_exd_update_install(),
					st::exteraInstallButton,
					[=] {
						if (!downloading) {
							Extera::Update::Install();
						}
					});
			} else if (state->status == Status::Available) {
				add(tr::lng_exd_update_open(), st::exteraInstallButton, [] {
					Extera::Update::OpenRelease();
				});
			} else {
				add(
					tr::lng_exd_update_check(),
					st::exteraInstallButton,
					[=] {
						if (state->status != Status::Checking) {
							(*check)();
						}
					});
			}
			const auto gap = st::exteraInstallCancelMargin.top();
			const auto layout = [=](int width) {
				const auto count = int(list.size());
				const auto each = (width - gap * (count - 1)) / count;
				auto left = 0;
				for (const auto button : list) {
					button->setFullWidth(each);
					button->moveToLeft(left, 0);
					left += each + gap;
				}
			};
			layout(buttons->width());
			buttons->widthValue() | rpl::on_next(layout, list.back()->lifetime());
			body->resizeToWidth(body->width());
		};

		*check = [=] {
			state->status = Status::Checking;
			state->render();
			const auto weak = base::make_weak(box.get());
			Extera::Update::CheckNow([=](Extera::Update::CheckResult result) {
				if (!weak) {
					return;
				}
				using Result = Extera::Update::CheckResult;
				state->status = (result == Result::Available)
					? Status::Available
					: (result == Result::Latest)
					? Status::Latest
					: Status::Failed;
				state->render();
				if (state->status == Status::Available) {
					translateIfNeeded();
				}
			});
		};

		Extera::Update::ProgressValue(
		) | rpl::on_next([=](float64 value) {
			const auto was = state->progress;
			state->progress = value;
			const auto changedPercent = (int(was * 100) != int(value * 100));
			if (changedPercent || (was < 0.) != (value < 0.)) {
				crl::on_main(box, [=] { state->render(); });
			}
		}, box->lifetime());

		(*check)();
	}));
}

} // namespace Exd
