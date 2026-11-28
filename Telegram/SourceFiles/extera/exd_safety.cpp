#include "extera/exd_safety.h"

#include "lang/lang_keys.h"
#include "extera/extera_config.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/effects/animations.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "styles/style_basic.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

namespace Exd::Safety {
namespace {

[[nodiscard]] QRegularExpression Re(const char *pattern, bool icase = false) {
	return QRegularExpression(
		QString::fromLatin1(pattern),
		icase
			? QRegularExpression::CaseInsensitiveOption
			: QRegularExpression::NoPatternOption);
}

[[nodiscard]] QRegularExpression ReU(const QString &pattern) {
	return QRegularExpression(
		pattern,
		QRegularExpression::CaseInsensitiveOption
			| QRegularExpression::UseUnicodePropertiesOption);
}

[[nodiscard]] const std::vector<QRegularExpression> &SecretPatterns() {
	static const auto result = std::vector<QRegularExpression>{
		Re(R"((?<![0-9])\d{6,12}:AA[A-Za-z0-9_-]{30,})"),
		Re(R"((?<![0-9])\d{6,12}:[A-Za-z0-9_-]{33,})"),
		Re(R"(\bsk-(?:ant|proj|or|svcacct|admin|live|test|or-v1)-[A-Za-z0-9_-]{4,})"),
		Re(R"(\bsk-[A-Za-z0-9_-]{16,})"),
		Re(R"(\b(?:sk|pk|rk)_(?:live|test)_[A-Za-z0-9]{10,})"),
		Re(R"(\bwhsec_[A-Za-z0-9]{16,})"),
		Re(R"(\bAIza[0-9A-Za-z_-]{20,})"),
		Re(R"(\bya29\.[A-Za-z0-9_-]{20,})"),
		Re(R"(\bAQ\.[A-Za-z0-9_-]{16,})"),
		Re(R"(\bGOCSPX-[A-Za-z0-9_-]{16,})"),
		Re(R"(\bgh[pousr]_[A-Za-z0-9]{20,})"),
		Re(R"(\bgithub_pat_[A-Za-z0-9_]{20,})"),
		Re(R"(\bgl(?:pat|dt|rt|ptt|cbt|soat|ft|imt|agent)-[A-Za-z0-9_-]{16,})"),
		Re(R"(\bATBB[A-Za-z0-9]{20,})"),
		Re(R"(\bATATT3[A-Za-z0-9_=-]{20,})"),
		Re(R"(\bgsk_[A-Za-z0-9]{16,})"),
		Re(R"(\bhf_[A-Za-z0-9]{20,})"),
		Re(R"(\br8_[A-Za-z0-9]{20,})"),
		Re(R"(\bxai-[A-Za-z0-9]{20,})"),
		Re(R"(\bpplx-[A-Za-z0-9]{20,})"),
		Re(R"(\bfw_[A-Za-z0-9]{16,})"),
		Re(R"(\bnvapi-[A-Za-z0-9_-]{20,})"),
		Re(R"(\bnpm_[A-Za-z0-9]{20,})"),
		Re(R"(\bpypi-[A-Za-z0-9_-]{20,})"),
		Re(R"(\bdckr_pat_[A-Za-z0-9_-]{16,})"),
		Re(R"(\bdop_v1_[a-f0-9]{32,})"),
		Re(R"(\bdo[or]_v1_[a-f0-9]{32,})"),
		Re(R"(\bsbp_[a-f0-9]{20,})"),
		Re(R"(\bsb_secret_[A-Za-z0-9_-]{16,})"),
		Re(R"(\bnfp_[A-Za-z0-9]{20,})"),
		Re(R"(\bhrku-[A-Za-z0-9_-]{20,})"),
		Re(R"(\bvercel_[A-Za-z0-9_-]{20,})"),
		Re(R"(\blin_api_[A-Za-z0-9]{20,})"),
		Re(R"(\b(?:secret|ntn)_[A-Za-z0-9]{30,})"),
		Re(R"(\bfigd_[A-Za-z0-9_-]{20,})"),
		Re(R"(\bshp(?:at|ca|pa|ss)_[a-fA-F0-9]{20,})"),
		Re(R"(\bxox[abposre]-[A-Za-z0-9-]{10,})"),
		Re(R"(\bxapp-\d-[A-Za-z0-9-]{10,})"),
		Re(R"(hooks\.slack\.com/services/[A-Za-z0-9/]{20,})"),
		Re(R"(discord(?:app)?\.com/api/webhooks/\d+/[A-Za-z0-9_-]{20,})"),
		Re(R"(\b[MNO][A-Za-z0-9_-]{23,27}\.[A-Za-z0-9_-]{6}\.[A-Za-z0-9_-]{27,})"),
		Re(R"(\bSG\.[A-Za-z0-9_-]{16,}\.[A-Za-z0-9_-]{16,})"),
		Re(R"(\b(?:SK|AC)[a-f0-9]{32}\b)"),
		Re(R"(\bkey-[a-f0-9]{32}\b)"),
		Re(R"(\b[a-f0-9]{32}-us\d{1,2}\b)"),
		Re(R"(\b(?:AKIA|ASIA|AGPA|AIDA|AROA)[0-9A-Z]{16}\b)"),
		Re(R"(\bPMAK-[a-f0-9]{24}-[a-f0-9]{34}\b)"),
		Re(R"(\bsntry[su]_[A-Za-z0-9_=-]{20,})"),
		Re(R"(\bgl(?:sa|c)_[A-Za-z0-9_=-]{20,})"),
		Re(R"(\bNRAK-[A-Z0-9]{20,})"),
		Re(R"(\by0_[A-Za-z0-9_-]{20,})"),
		Re(R"(\bAQVN[A-Za-z0-9_-]{20,})"),
		Re(R"(\bt1\.[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,})"),
		Re(R"(\beyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,})"),
		Re(R"(-----BEGIN [A-Z ]*PRIVATE KEY( BLOCK)?-----)"),
		Re(R"(\b(?:0x)?[a-fA-F0-9]{64}\b)"),
		Re(R"(\b(?:postgres(?:ql)?|mysql|mariadb|mongodb(?:\+srv)?|rediss?|amqps?|mssql|sqlserver|ftp|sftp|ssh|smtp)://[^\s:/@]+:[^\s@/]+@)", true),
		Re(R"(\b[\w.+-]+@[\w-]+(?:\.[\w-]+)+:[^\s:]{4,})", true),
	};
	return result;
}

[[nodiscard]] QString Keyword() {
	return QString::fromUtf8(R"((?:api[_ -]?key|apikey|api[_ -]?secret|secret[_ -]?key|client[_ -]?secret|access[_ -]?key|access[_ -]?token|auth[_ -]?token|bearer|bot[_ -]?token|refresh[_ -]?token|private[_ -]?key|token|secret|password|passwd|passcode|pass|pwd|pin|cvv|cvc|seed|mnemonic|пароль|пароли|пасс|пасворд|пин|пин-код|пинкод|токен|ключ|секрет|код доступа|сид-фраза|сид))");
}

[[nodiscard]] int Classes(const QString &text) {
	auto upper = false;
	auto lower = false;
	auto digit = false;
	auto other = false;
	for (const auto ch : text) {
		if (ch.isUpper()) {
			upper = true;
		} else if (ch.isLower()) {
			lower = true;
		} else if (ch.isDigit()) {
			digit = true;
		} else if (!ch.isLetterOrNumber()) {
			other = true;
		}
	}
	return int(upper) + int(lower) + int(digit) + int(other);
}

[[nodiscard]] bool StrongValue(QString value, bool assign) {
	static const auto trail = QString::fromLatin1(R"("'.,;))");
	while (!value.isEmpty() && trail.contains(value.back())) {
		value.chop(1);
	}
	if (value.size() < 4) {
		return false;
	}
	auto digits = true;
	for (const auto ch : value) {
		if (!ch.isDigit()) {
			digits = false;
			break;
		}
	}
	if (assign && digits) {
		return true;
	}
	return Classes(value) >= 2;
}

[[nodiscard]] const QRegularExpression &UrlRe() {
	static const auto result = ReU(QString::fromLatin1(
		R"((?:https?://|www\.)\S+|\b[\w-]+(?:\.[\w-]+)*\.[a-z]{2,}/\S*)"));
	return result;
}

[[nodiscard]] bool HasRandomWord(QString text) {
	text.replace(UrlRe(), u" "_q);
	static const auto word = Re(R"([A-Za-z0-9_+/=-]{20,})");
	static const auto hex = Re(R"(^(?:[0-9a-f]+|[0-9A-F]+)$)");
	auto i = word.globalMatch(text);
	while (i.hasNext()) {
		const auto value = i.next().captured();
		auto upper = false;
		auto lower = false;
		auto digits = 0;
		for (const auto ch : value) {
			if (ch.isUpper()) {
				upper = true;
			} else if (ch.isLower()) {
				lower = true;
			} else if (ch.isDigit()) {
				++digits;
			}
		}
		if (upper && lower && digits >= 2) {
			return true;
		}
		if ((upper || lower)
			&& digits >= 6
			&& value.size() >= 24
			&& !hex.match(value).hasMatch()) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] bool LonePassword(const QString &text) {
	const auto t = text.trimmed();
	if (t.size() < 8 || t.size() > 64) {
		return false;
	}
	for (const auto ch : t) {
		if (ch.isSpace()) {
			return false;
		}
	}
	if (t.startsWith('@')
		|| t.startsWith('#')
		|| t.startsWith('/')
		|| t.startsWith('+')
		|| t.contains(u"://"_q)
		|| UrlRe().match(t).hasMatch()) {
		return false;
	}
	auto letters = false;
	auto digits = false;
	auto symbol = false;
	auto upper = false;
	auto lower = false;
	for (const auto ch : t) {
		if (ch.isLetter()) {
			letters = true;
			if (ch.isUpper()) {
				upper = true;
			} else if (ch.isLower()) {
				lower = true;
			}
		} else if (ch.isDigit()) {
			digits = true;
		} else {
			symbol = true;
		}
	}
	return letters && digits && ((upper && lower) || (symbol && lower));
}

[[nodiscard]] bool HasKeywordSecret(const QString &text) {
	static const auto bearer = ReU(QString::fromLatin1(
		R"(\bBearer\s+[A-Za-z0-9._~+/=-]{16,})"));
	static const auto seed = ReU(QString::fromUtf8(
		R"((?:seed|mnemonic|сид[- ]?фраза|сид|recovery phrase|фраза восстановления)\s*[:=-]?\s*(?:[a-z]{3,8}\s+){2,}[a-z]{3,8})"));
	static const auto assign = ReU(Keyword() + QString::fromLatin1(
		R"([^\n:=]{0,40}?(?:\s*[:=]|\s+-)\s*["']?(\S{3,}))"));
	static const auto space = ReU(QString::fromLatin1(R"((?<![\w]))")
		+ Keyword()
		+ QString::fromUtf8(
			R"(\s+(?:от\s+\S+\s+|for\s+\S+\s+|is\s+|это\s+)?["']?(\S{4,}))"));
	if (bearer.match(text).hasMatch() || seed.match(text).hasMatch()) {
		return true;
	}
	const auto check = [&](const QRegularExpression &re, bool strict) {
		auto i = re.globalMatch(text);
		while (i.hasNext()) {
			if (StrongValue(i.next().captured(1), strict)) {
				return true;
			}
		}
		return false;
	};
	return check(assign, true) || check(space, false);
}

[[nodiscard]] QString Normalized(QString text) {
	text = text.trimmed();
	if (!text.contains(u"://"_q)) {
		text = u"https://"_q + text;
	}
	const auto url = QUrl(text);
	auto host = url.host().toLower();
	if (host.startsWith(u"www."_q)) {
		host = host.mid(4);
	}
	if (host == u"telegram.me"_q || host == u"telegram.dog"_q) {
		host = u"t.me"_q;
	}
	auto path = url.path();
	while (path.endsWith('/')) {
		path.chop(1);
	}
	const auto query = url.query();
	return (host + path + (query.isEmpty() ? QString() : (u"?"_q + query)))
		.toLower();
}

[[nodiscard]] QString HostOf(QString text) {
	text = text.trimmed().toLower();
	if (!text.contains(u"://"_q)) {
		text = u"https://"_q + text;
	}
	auto host = QUrl(text).host();
	if (host.startsWith(u"www."_q)) {
		host = host.mid(4);
	}
	return host;
}

[[nodiscard]] bool LooksLikeDomain(const QString &text) {
	static const auto re = QRegularExpression(
		u"^(?:https?://)?(?:www\\.)?[a-z0-9-]+(?:\\.[a-z0-9-]+)*\\.[a-z]{2,}(?:[/?#].*)?$"_q,
		QRegularExpression::CaseInsensitiveOption);
	return re.match(text.trimmed()).hasMatch();
}

[[nodiscard]] bool SameSite(const QString &a, const QString &b) {
	return (a == b) || a.endsWith('.' + b) || b.endsWith('.' + a);
}

[[nodiscard]] bool HasReferral(const QUrl &url) {
	static const auto keys = std::vector<QString>{
		u"ref"_q, u"referral"_q, u"ref_id"_q, u"refid"_q, u"aff"_q,
		u"affiliate"_q, u"aff_id"_q, u"partner"_q, u"promo"_q,
		u"invite"_q, u"inviter"_q, u"utm_source"_q, u"clickid"_q,
		u"click_id"_q, u"subid"_q, u"sub_id"_q,
	};
	const auto query = QUrlQuery(url);
	for (const auto &[key, value] : query.queryItems()) {
		const auto lower = key.toLower();
		for (const auto &known : keys) {
			if (lower == known) {
				return true;
			}
		}
	}
	const auto host = url.host().toLower();
	if (host == u"t.me"_q || host == u"telegram.me"_q) {
		const auto path = url.path();
		const auto query = QUrlQuery(url);
		for (const auto &key : {
				u"start"_q,
				u"startapp"_q,
				u"startgroup"_q,
				u"startattach"_q }) {
			static const auto refLike = QRegularExpression(
				u"^(r|inv|invite|aff|partner|promo|bonus)([_-]|$)|ref|^[0-9]{5,}$"_q,
				QRegularExpression::CaseInsensitiveOption);
			const auto value = query.queryItemValue(key);
			if (!value.isEmpty() && refLike.match(value).hasMatch()) {
				return true;
			}
		}
		Q_UNUSED(path);
	}
	return false;
}

} // namespace

bool LooksLikeSecret(const QString &text) {
	if (text.trimmed().size() < 6) {
		return false;
	}
	for (const auto &pattern : SecretPatterns()) {
		if (pattern.match(text).hasMatch()) {
			return true;
		}
	}
	return HasKeywordSecret(text)
		|| HasRandomWord(text)
		|| LonePassword(text);
}

QString LinkWarning(const QString &text, const QString &url) {
	const auto parsed = QUrl::fromUserInput(url);
	auto warnings = QStringList();
	const auto shown = text.trimmed();
	if (LooksLikeDomain(shown)) {
		const auto shownHost = HostOf(shown);
		const auto realHost = HostOf(url);
		if (!shownHost.isEmpty()
			&& !realHost.isEmpty()
			&& !SameSite(shownHost, realHost)) {
			warnings.push_back(tr::lng_exd_link_hint_mismatch(tr::now));
		} else if (Normalized(shown) != Normalized(url)) {
			warnings.push_back(tr::lng_exd_link_hint_hidden(tr::now));
		}
	} else if (shown.startsWith('@') && shown.size() > 1) {
		const auto username = shown.mid(1).toLower();
		const auto host = parsed.host().toLower();
		const auto path = parsed.path().mid(1).section('/', 0, 0).toLower();
		if (!((host == u"t.me"_q || host == u"telegram.me"_q)
			&& path == username)) {
			warnings.push_back(tr::lng_exd_link_hint_mention(tr::now));
		}
	}
	const auto visible = !shown.isEmpty()
		&& (Normalized(shown) == Normalized(url));
	if (!visible && HasReferral(parsed)) {
		warnings.push_back(tr::lng_exd_link_hint_referral(tr::now));
	}
	return warnings.join('\n');
}

void SetupSecretBar(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::InputField*> field,
		rpl::producer<QRect> area) {
	struct State {
		Ui::Animations::Simple animation;
		bool shown = false;
		QRect area;
		QString text;
	};
	const auto bar = Ui::CreateChild<Ui::RpWidget>(parent.get());
	const auto state = bar->lifetime().make_state<State>();
	state->text = tr::lng_exd_secret_bar(tr::now);
	bar->setAttribute(Qt::WA_TransparentForMouseEvents);
	bar->hide();
	const auto &icon = st::menuIconAntispam;
	const auto height = style::ConvertScale(30);
	const auto padding = style::ConvertScale(12);
	const auto margin = style::ConvertScale(8);
	const auto slide = style::ConvertScale(10);
	const auto layout = [=] {
		const auto progress = state->animation.value(state->shown ? 1. : 0.);
		const auto full = state->area;
		const auto width = std::min(
			full.width() - margin * 2,
			padding + icon.width() + style::ConvertScale(4)
				+ st::normalFont->width(state->text) + padding);
		if (width <= 0 || (progress == 0. && !state->shown)) {
			bar->hide();
			return;
		}
		bar->setGeometry(
			full.x() + (full.width() - width) / 2,
			(full.y()
				+ full.height()
				- height
				- margin
				+ int(base::SafeRound((1. - progress) * slide))),
			width,
			height);
		if (bar->isHidden()) {
			bar->show();
		}
		bar->raise();
		bar->update();
	};
	bar->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(bar);
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(state->animation.value(state->shown ? 1. : 0.));
		p.setPen(Qt::NoPen);
		p.setBrush(anim::color(
			st::windowBg->c,
			st::attentionButtonFg->c,
			0.14));
		const auto radius = bar->height() / 2.;
		p.drawRoundedRect(bar->rect(), radius, radius);
		const auto iconLeft = padding - style::ConvertScale(5);
		icon.paint(
			p,
			iconLeft,
			(bar->height() - icon.height()) / 2,
			bar->width(),
			st::attentionButtonFg->c);
		p.setPen(st::windowFg);
		p.setFont(st::normalFont);
		const auto left = iconLeft + icon.width() + style::ConvertScale(2);
		const auto available = bar->width() - left - padding;
		p.drawText(
			QRect(left, 0, available, bar->height()),
			Qt::AlignVCenter | Qt::AlignLeft,
			st::normalFont->elided(state->text, available));
	}, bar->lifetime());
	const auto refresh = [=] {
		auto text = field->getLastText();
		if (text.size() > 4096) {
			text = text.left(4096);
		}
		const auto show = field->isVisible()
			&& Extera::Bool(u"exd_paste_guard"_q)
			&& LooksLikeSecret(text);
		if (state->shown == show) {
			return;
		}
		state->shown = show;
		state->animation.start(
			layout,
			show ? 0. : 1.,
			show ? 1. : 0.,
			crl::time(220),
			anim::easeOutCubic);
		layout();
	};
	field->changes() | rpl::on_next(refresh, bar->lifetime());
	std::move(area) | rpl::on_next([=](QRect rect) {
		state->area = rect;
		layout();
	}, bar->lifetime());
	refresh();
}

void MarkSuspiciousLinks(TextWithEntities &text) {
	auto inserts = std::vector<int>();
	for (const auto &entity : text.entities) {
		if (entity.type() == EntityType::CustomUrl) {
			const auto shown = text.text.mid(entity.offset(), entity.length());
			if (!LinkWarning(shown, entity.data()).isEmpty()) {
				inserts.push_back(entity.offset() + entity.length());
			}
		} else if (entity.type() == EntityType::Url) {
			const auto url = text.text.mid(entity.offset(), entity.length());
			if (!LinkWarning(QString(), url).isEmpty()) {
				inserts.push_back(entity.offset() + entity.length());
			}
		}
	}
	if (inserts.empty()) {
		return;
	}
	ranges::sort(inserts, std::greater<>());
	const auto mark = QString::fromUtf8(" \xE2\x9A\xA0");
	for (const auto position : inserts) {
		text.text.insert(position, mark);
		auto updated = EntitiesInText();
		updated.reserve(text.entities.size());
		for (const auto &entity : text.entities) {
			const auto from = entity.offset();
			const auto till = from + entity.length();
			if (from >= position) {
				updated.push_back(EntityInText(
					entity.type(),
					from + mark.size(),
					entity.length(),
					entity.data()));
			} else if (till > position) {
				updated.push_back(EntityInText(
					entity.type(),
					from,
					entity.length() + mark.size(),
					entity.data()));
			} else {
				updated.push_back(entity);
			}
		}
		text.entities = std::move(updated);
	}
}

} // namespace Exd::Safety
