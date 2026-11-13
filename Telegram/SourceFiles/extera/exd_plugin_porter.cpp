#include "extera/exd_plugin_porter.h"

#include "base/weak_qptr.h"
#include "extera/extera_config.h"
#include "extera/extera_plugins.h"
#include "extera/extera_plugins_ui.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Exd::PluginPorter {
namespace {

using Extera::Int;
using Extera::String;
using Extera::Plugins::Call;

constexpr auto kAgentAttempts = 4;

struct Provider {
	bool anthropic = false;
	QString endpoint;
	QString model;
	bool local = false;
	bool ollama = false;
};

const auto kProviders = std::array{
	Provider(),
	Provider{
		false,
		u"https://api.openai.com/v1/chat/completions"_q,
		u"gpt-4.1"_q,
		false },
	Provider{
		true,
		u"https://api.anthropic.com/v1/messages"_q,
		u"claude-sonnet-5-5"_q,
		false },
	Provider{
		false,
		u"https://openrouter.ai/api/v1/chat/completions"_q,
		u"openrouter/auto"_q,
		false },
	Provider{
		false,
		u"https://api.groq.com/openai/v1/chat/completions"_q,
		u"openai/gpt-oss-120b"_q,
		false },
	Provider{
		false,
		u"https://api.deepseek.com/chat/completions"_q,
		u"deepseek-chat"_q,
		false },
	Provider{
		false,
		u"http://localhost:11434/api/chat"_q,
		u"qwen2.5-coder:7b"_q,
		true,
		true },
	Provider{
		false,
		u"http://localhost:1234/v1/chat/completions"_q,
		QString(),
		true },
	Provider{
		false,
		QString(),
		QString(),
		true },
	Provider{
		false,
		u"https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"_q,
		u"gemini-2.5-flash"_q,
		false },
};

struct Settings {
	Provider provider;
	QString key;
	bool agent = true;
};

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(qApp);
	return result;
}

[[nodiscard]] int ProviderIndex() {
	return std::clamp(
		Int(u"port_llm_provider"_q),
		0,
		int(kProviders.size()) - 1);
}

[[nodiscard]] QString NormalizeEndpoint(QString endpoint, bool anthropic) {
	endpoint = endpoint.trimmed();
	if (endpoint.isEmpty()) {
		return endpoint;
	}
	if (!endpoint.startsWith(u"http://"_q)
		&& !endpoint.startsWith(u"https://"_q)) {
		endpoint = u"http://"_q + endpoint;
	}
	while (endpoint.endsWith('/')) {
		endpoint.chop(1);
	}
	if (endpoint.endsWith(u"/api/chat"_q)) {
		return endpoint;
	}
	if (anthropic) {
		if (!endpoint.endsWith(u"/messages"_q)) {
			endpoint += endpoint.endsWith(u"/v1"_q)
				? u"/messages"_q
				: u"/v1/messages"_q;
		}
	} else if (!endpoint.endsWith(u"/chat/completions"_q)) {
		endpoint += u"/chat/completions"_q;
	}
	return endpoint;
}

[[nodiscard]] Settings CurrentSettings() {
	auto result = Settings();
	result.provider = kProviders[ProviderIndex()];
	const auto endpoint = String(u"port_llm_endpoint"_q).trimmed();
	if (!endpoint.isEmpty()) {
		result.provider.endpoint = NormalizeEndpoint(
			endpoint,
			result.provider.anthropic);
		result.provider.ollama = result.provider.endpoint.endsWith(
			u"/api/chat"_q);
	}
	const auto model = String(u"port_llm_model"_q).trimmed();
	if (!model.isEmpty()) {
		result.provider.model = model;
	}
	result.key = String(u"port_llm_key"_q).trimmed();
	result.agent = (Int(u"port_llm_mode"_q) == 0);
	return result;
}

[[nodiscard]] QNetworkReply *Send(
		const Settings &settings,
		const QString &system,
		const QJsonArray &messages) {
	auto request = QNetworkRequest(QUrl(settings.provider.endpoint));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setTransferTimeout(settings.provider.local ? 900000 : 300000);
	auto body = QJsonObject();
	if (settings.provider.anthropic) {
		request.setRawHeader("x-api-key", settings.key.toUtf8());
		request.setRawHeader("anthropic-version", "2023-06-01");
		body = QJsonObject{
			{ u"model"_q, settings.provider.model },
			{ u"max_tokens"_q, 32000 },
			{ u"system"_q, system },
			{ u"messages"_q, messages },
		};
	} else {
		if (!settings.key.isEmpty()) {
			request.setRawHeader(
				"Authorization",
				"Bearer " + settings.key.toUtf8());
		}
		auto all = QJsonArray{ QJsonObject{
			{ u"role"_q, u"system"_q },
			{ u"content"_q, system },
		} };
		for (const auto &message : messages) {
			all.push_back(message);
		}
		body = QJsonObject{ { u"messages"_q, all } };
		if (!settings.provider.model.isEmpty()) {
			body.insert(u"model"_q, settings.provider.model);
		}
		if (settings.provider.ollama) {
			body.insert(u"stream"_q, false);
			body.insert(u"options"_q, QJsonObject{
				{ u"num_ctx"_q, 32768 },
				{ u"temperature"_q, 0.2 },
			});
		}
	}
	return Network()->post(
		request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString ParseAnswer(
		const QByteArray &data,
		bool anthropic,
		QString *error) {
	const auto object = QJsonDocument::fromJson(data).object();
	auto result = QString();
	if (anthropic) {
		for (const auto &part : object.value(u"content"_q).toArray()) {
			const auto block = part.toObject();
			if (block.value(u"type"_q).toString() == u"text"_q) {
				result += block.value(u"text"_q).toString();
			}
		}
	} else if (object.contains(u"message"_q)) {
		result = object.value(u"message"_q).toObject().value(
			u"content"_q).toString();
	} else {
		result = object.value(u"choices"_q).toArray().at(0).toObject().value(
			u"message"_q).toObject().value(u"content"_q).toString();
	}
	if (result.isEmpty()) {
		auto value = object.value(u"error"_q);
		if (value.isArray()) {
			value = value.toArray().at(0).toObject().value(u"error"_q);
		}
		if (object.isEmpty()) {
			const auto list = QJsonDocument::fromJson(data).array();
			value = list.at(0).toObject().value(u"error"_q);
		}
		*error = value.isObject()
			? value.toObject().value(u"message"_q).toString()
			: value.toString();
	}
	return result;
}

[[nodiscard]] bool IsLimitError(const QString &error, int status) {
	const auto lower = error.toLower();
	return (status == 413)
		|| (status == 429)
		|| lower.contains(u"tokens per minute"_q)
		|| lower.contains(u"request too large"_q)
		|| lower.contains(u"rate limit"_q)
		|| lower.contains(u"rate_limit"_q)
		|| lower.contains(u"context length"_q)
		|| lower.contains(u"context_length"_q)
		|| lower.contains(u"maximum context"_q)
		|| lower.contains(u"quota"_q);
}

[[nodiscard]] QStringList Strings(const QJsonArray &list) {
	auto result = QStringList();
	for (const auto &value : list) {
		result.push_back(value.toString());
	}
	return result;
}

} // namespace

struct Job {
	Settings settings;
	QJsonObject prepared;
	QString system;
	QJsonArray messages;
	QPointer<QNetworkReply> reply;
	Progress progress;
	Fn<void(const Progress&)> update;
	Fn<void()> request;
	crl::time started = 0;
	crl::time requested = 0;
	int needRounds = 0;
	bool canceled = false;
};

Info CurrentInfo() {
	const auto index = ProviderIndex();
	const auto settings = CurrentSettings();
	return {
		.provider = index,
		.model = settings.provider.model,
		.endpoint = settings.provider.endpoint,
		.local = settings.provider.local,
		.agent = settings.agent,
	};
}

bool Configured() {
	const auto index = ProviderIndex();
	if (!index) {
		return false;
	}
	const auto settings = CurrentSettings();
	if (settings.provider.endpoint.isEmpty()) {
		return false;
	}
	return settings.provider.local || !settings.key.isEmpty();
}

bool Suggested(const QJsonObject &info) {
	if (info.value(u"desktop"_q).toBool()
		|| info.value(u"format"_q).toString() == u"elyx"_q) {
		return false;
	}
	const auto capabilities = info.value(u"capabilities"_q).toObject();
	return !info.value(u"unsupported"_q).toArray().isEmpty()
		|| capabilities.contains(u"hooks"_q)
		|| capabilities.contains(u"dex"_q)
		|| !info.value(u"error"_q).toString().isEmpty();
}

std::shared_ptr<Job> Run(
		const QString &path,
		Fn<void(const Progress&)> update,
		QString *error) {
	const auto prepared = Call(u"port_prepare"_q, QJsonObject{
		{ u"path"_q, path },
	}).toObject();
	if (prepared.isEmpty()) {
		*error = u"bad"_q;
		return nullptr;
	}
	if (const auto key = prepared.value(u"error"_q).toString();
		!key.isEmpty()) {
		*error = key;
		return nullptr;
	}
	const auto job = std::make_shared<Job>();
	job->settings = CurrentSettings();
	job->system = prepared.value(u"system"_q).toString();
	job->messages.push_back(QJsonObject{
		{ u"role"_q, u"user"_q },
		{ u"content"_q, prepared.value(u"user"_q).toString() },
	});
	auto stripped = prepared;
	stripped.remove(u"system"_q);
	stripped.remove(u"user"_q);
	job->prepared = stripped;
	job->progress.attempts = job->settings.agent ? kAgentAttempts : 1;
	job->progress.sections = Strings(stripped.value(u"sections"_q).toArray());
	job->progress.hints = Strings(stripped.value(u"hints"_q).toArray());
	job->progress.library = stripped.value(u"library"_q).toBool();
	job->update = std::move(update);
	job->started = crl::now();
	const auto weak = std::weak_ptr<Job>(job);
	const auto notify = [=] {
		if (const auto strong = weak.lock()) {
			strong->progress.total = int(crl::now() - strong->started);
			if (const auto update = strong->update) {
				const auto progress = strong->progress;
				update(progress);
			}
		}
	};
	const auto finish = [=](Progress::Stage stage) {
		if (const auto strong = weak.lock()) {
			strong->progress.stage = stage;
			notify();
			strong->request = nullptr;
		}
	};
	job->request = [=] {
		const auto strong = weak.lock();
		if (!strong || strong->canceled) {
			return;
		}
		++strong->progress.attempt;
		strong->progress.stage = (strong->progress.attempt > 1)
			? Progress::Stage::Fixing
			: Progress::Stage::Requesting;
		strong->requested = crl::now();
		notify();
		const auto reply = Send(
			strong->settings,
			strong->system,
			strong->messages);
		strong->reply = reply;
		QObject::connect(reply, &QNetworkReply::finished, [=] {
			reply->deleteLater();
			const auto job = weak.lock();
			if (!job || job->canceled) {
				return;
			}
			job->progress.elapsed = int(crl::now() - job->requested);
			const auto data = reply->readAll();
			auto error = QString();
			const auto answer = ParseAnswer(
				data,
				job->settings.provider.anthropic,
				&error);
			if (answer.isEmpty()) {
				if (error.isEmpty()) {
					error = (reply->error() != QNetworkReply::NoError)
						? reply->errorString()
						: tr::lng_exd_port_error_empty(tr::now);
				}
				const auto status = reply->attribute(
					QNetworkRequest::HttpStatusCodeAttribute).toInt();
				job->progress.limit = IsLimitError(error, status);
				job->progress.error = error;
				finish(Progress::Stage::Failed);
				return;
			}
			job->progress.stage = Progress::Stage::Checking;
			notify();
			const auto checked = Call(u"port_check"_q, QJsonObject{
				{ u"answer"_q, answer },
				{ u"prepared"_q, job->prepared },
			}).toObject();
			if (checked.isEmpty()) {
				job->progress.error = tr::lng_exd_port_error_empty(tr::now);
				finish(Progress::Stage::Failed);
				return;
			}
			job->progress.report = checked.value(u"parsed"_q).toObject();
			if (checked.value(u"nothing"_q).toBool()) {
				finish(Progress::Stage::Nothing);
				return;
			}
			const auto need = checked.value(u"need"_q).toArray();
			if (!need.isEmpty() && job->needRounds < 2) {
				++job->needRounds;
				--job->progress.attempt;
				auto sections = job->prepared.value(u"sections"_q).toArray();
				for (const auto &value : need) {
					sections.push_back(value);
					job->progress.requested.push_back(value.toString());
				}
				job->prepared.insert(u"sections"_q, sections);
				job->messages.push_back(QJsonObject{
					{ u"role"_q, u"assistant"_q },
					{ u"content"_q, answer },
				});
				job->messages.push_back(QJsonObject{
					{ u"role"_q, u"user"_q },
					{ u"content"_q, checked.value(u"fix"_q).toString() },
				});
				job->request();
				return;
			}
			const auto errors = Strings(checked.value(u"errors"_q).toArray());
			job->progress.errors = errors;
			if (errors.isEmpty()) {
				const auto saved = Call(u"port_save"_q, QJsonObject{
					{ u"code"_q, checked.value(u"code"_q) },
					{ u"prepared"_q, job->prepared },
				}).toString();
				if (saved.isEmpty()) {
					job->progress.error = tr::lng_extera_plugins_install_failed(
						tr::now);
					finish(Progress::Stage::Failed);
					return;
				}
				job->progress.path = saved;
				finish(Progress::Stage::Done);
				return;
			}
			job->progress.fixed += int(errors.size());
			if (job->progress.attempt >= job->progress.attempts) {
				finish(Progress::Stage::Failed);
				return;
			}
			job->messages.push_back(QJsonObject{
				{ u"role"_q, u"assistant"_q },
				{ u"content"_q, answer },
			});
			job->messages.push_back(QJsonObject{
				{ u"role"_q, u"user"_q },
				{ u"content"_q, checked.value(u"fix"_q).toString() },
			});
			job->request();
		});
	};
	crl::on_main([=] {
		if (const auto strong = weak.lock(); strong && strong->request) {
			strong->request();
		}
	});
	return job;
}

void Cancel(const std::shared_ptr<Job> &job) {
	if (!job) {
		return;
	}
	job->canceled = true;
	job->update = nullptr;
	job->request = nullptr;
	if (job->reply) {
		job->reply->abort();
	}
}

int ProvidersCount() {
	return int(kProviders.size());
}

QString DefaultModel(int provider) {
	return (provider >= 0 && provider < int(kProviders.size()))
		? kProviders[provider].model
		: QString();
}

bool ProviderLocal(int provider) {
	return (provider >= 0 && provider < int(kProviders.size()))
		&& kProviders[provider].local;
}

void Start(
		not_null<Window::SessionController*> controller,
		const QString &path) {
	Extera::Plugins::ShowPortSetupBox(controller, path);
}

} // namespace Exd::PluginPorter
