#include "extera/exd_plugin_guard.h"

#include "extera/extera_config.h"
#include "extera/extera_plugins.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "base/zlib_help.h"
#include "base/unixtime.h"
#include "settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <zlib.h>

namespace Exd::PluginGuard {
namespace {

using Extera::Int;
using Extera::String;
using Extera::Config;

const auto kModelBase = u"https://raw.githubusercontent.com/"
	"abova8382993/PluginGuard-model/main/"_q;
const auto kVectorsFile = u"norm_vectors_8bit.npz"_q;
const auto kMetaFile = u"norm_metadata_8bit.json"_q;
constexpr auto kCheckInterval = 24 * 60 * 60;

struct Provider {
	QString endpoint;
	QString model;
};

const auto kProviders = std::array{
	Provider(),
	Provider{
		u"https://api.groq.com/openai/v1/chat/completions"_q,
		u"openai/gpt-oss-120b"_q },
	Provider{
		u"https://openrouter.ai/api/v1/chat/completions"_q,
		u"openrouter/free"_q },
	Provider{
		u"https://api.deepseek.com/chat/completions"_q,
		u"deepseek-chat"_q },
	Provider{
		u"https://api.openai.com/v1/chat/completions"_q,
		u"gpt-4o-mini"_q },
	Provider{
		u"https://api.mistral.ai/v1/chat/completions"_q,
		u"mistral-small-latest"_q },
};

const auto kKeywords = std::array{
	"socket", "connect", "sendall", "recv", "eval", "exec", "__import__",
	"userconfig", "messagesstorage", "accountinstance", "killprocess",
	"database", "sqlite3", "urlopen", "requests", "aiohttp", "httpx",
	"base64", "zlib", "sys.modules", "find_class", "dup2", "subprocess",
	"castleteam", "cache4.db", "tgnet.dat", "userconfig.xml",
	"getactivesessions", "installpluginbottomsheet", "trusted",
	"b85decode", "rc4", "marshal", "sendpaidreaction",
	"updatestargiftprice", "paidreactionprivacyanonymous",
	"transferstargift", "inputsavedstargift", "token_flood_loop",
	"token_rapid_send", "token_unsolicited_spammer",
	"token_session_extract", "token_db_theft", "token_c2_post",
	"token_dynamic_stager", "token_payload_exec", "token_trust_forgery",
	"token_crypto_clipper", "token_hostile_proc_kill", "token_phone_leak",
	"token_auth_hijack", "token_stars_drainer", "token_gift_hijack",
};

struct RulePenalty {
	const char *rule;
	int penalty;
};

const auto kPenalties = std::array{
	RulePenalty{ "TELEGRAM_DATABASE_EXFILTRATION", 100 },
	RulePenalty{ "UNAUTHORIZED_DATA_EXFILTRATION", 100 },
	RulePenalty{ "CLIPBOARD_CRYPTO_CLIPPER", 100 },
	RulePenalty{ "RAW_IP_C2_ENDPOINT", 15 },
	RulePenalty{ "C2_TELEGRAM_BOT_API", 100 },
	RulePenalty{ "EMBEDDED_TELEGRAM_BOT_TOKEN", 100 },
	RulePenalty{ "REMOTE_CODE_EXECUTION_STAGER", 100 },
	RulePenalty{ "SERVICE_2FA_CODE_INTERCEPTION", 100 },
	RulePenalty{ "OBFUSCATED_CODE_STRUCTURE", 100 },
	RulePenalty{ "LLM_MALICIOUS", 100 },
	RulePenalty{ "TELEGRAM_INTERNAL_STORAGE_THEFT", 15 },
	RulePenalty{ "PRIVILEGED_COMMAND_EXECUTION", 15 },
	RulePenalty{ "DYNAMIC_CODE_EXECUTION", 20 },
	RulePenalty{ "DYNAMIC_CODE_COMPILATION", 20 },
	RulePenalty{ "CTYPES_NATIVE_ACCESS", 8 },
	RulePenalty{ "HIGH_ENTROPY_LITERAL_DETECTED", 6 },
	RulePenalty{ "EXTERNAL_IP_QUERY_SERVICE", 5 },
	RulePenalty{ "SUSPICIOUS_SIGNATURE_MATCH", 35 },
};

struct Matrix {
	std::vector<float> values;
	int rows = 0;
};

struct Model {
	bool loaded = false;
	int dim = 384;
	float threshold = 0.649f;
	float malwareThreshold = 0.78f;
	float minContrast = 0.04f;
	Matrix clean;
	Matrix malware;
};

[[nodiscard]] QString ModelDir() {
	return cWorkingDir() + u"tdata/plugin_guard/"_q;
}

[[nodiscard]] QNetworkAccessManager *Network() {
	static const auto result = new QNetworkAccessManager(qApp);
	return result;
}

[[nodiscard]] Model &Loaded() {
	static auto result = Model();
	return result;
}

[[nodiscard]] float HalfToFloat(uint16 h) {
	const auto sign = uint32(h & 0x8000) << 16;
	const auto exponent = (h >> 10) & 0x1F;
	const auto mantissa = uint32(h & 0x3FF);
	auto bits = uint32();
	if (!exponent) {
		if (!mantissa) {
			bits = sign;
		} else {
			auto e = -1;
			auto m = mantissa;
			do {
				++e;
				m <<= 1;
			} while (!(m & 0x400));
			bits = sign | (uint32(127 - 15 - e) << 23) | ((m & 0x3FF) << 13);
		}
	} else if (exponent == 31) {
		bits = sign | 0x7F800000 | (mantissa << 13);
	} else {
		bits = sign | (uint32(exponent - 15 + 127) << 23) | (mantissa << 13);
	}
	auto result = 0.f;
	memcpy(&result, &bits, sizeof(result));
	return result;
}

[[nodiscard]] Matrix ParseNpy(const QByteArray &raw, int dim) {
	auto result = Matrix();
	if (raw.size() < 12 || !raw.startsWith("\x93NUMPY")) {
		return result;
	}
	const auto major = uchar(raw[6]);
	auto offset = 0;
	auto headerLength = 0;
	if (major == 1) {
		headerLength = uchar(raw[8]) | (uchar(raw[9]) << 8);
		offset = 10 + headerLength;
	} else {
		headerLength = uchar(raw[8])
			| (uchar(raw[9]) << 8)
			| (uchar(raw[10]) << 16)
			| (uchar(raw[11]) << 24);
		offset = 12 + headerLength;
	}
	if (offset > raw.size()) {
		return result;
	}
	const auto header = QString::fromLatin1(raw.mid(0, offset));
	const auto bytes = header.contains(u"<f2"_q) ? 2 : 1;
	const auto step = dim * bytes;
	result.rows = (raw.size() - offset) / step;
	result.values.resize(size_t(result.rows) * dim);
	const auto data = reinterpret_cast<const uchar*>(raw.constData() + offset);
	for (auto i = 0; i != result.rows * dim; ++i) {
		result.values[i] = (bytes == 1)
			? (data[i] / 255.f)
			: HalfToFloat(uint16(data[i * 2] | (data[i * 2 + 1] << 8)));
	}
	return result;
}

bool LoadModel() {
	auto &model = Loaded();
	if (model.loaded) {
		return true;
	}
	auto meta = QFile(ModelDir() + kMetaFile);
	if (meta.open(QIODevice::ReadOnly)) {
		const auto object = QJsonDocument::fromJson(meta.readAll()).object();
		model.dim = object.value(u"dim"_q).toInt(384);
		model.threshold = object.value(u"threshold"_q).toDouble(0.649);
		model.malwareThreshold = object.value(
			u"malware_threshold"_q).toDouble(0.78);
		model.minContrast = object.value(
			u"min_contrast_diff"_q).toDouble(0.04);
	}
	auto file = QFile(ModelDir() + kVectorsFile);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	auto zip = zlib::FileToRead(file.readAll());
	const auto clean = zip.readFileContent("vectors.npy", 0, 256 * 1024 * 1024);
	zip.clearError();
	const auto malware = zip.readFileContent(
		"malware_vectors.npy",
		0,
		256 * 1024 * 1024);
	model.clean = ParseNpy(clean, model.dim);
	model.malware = ParseNpy(malware, model.dim);
	model.loaded = (model.clean.rows > 0);
	return model.loaded;
}

[[nodiscard]] std::vector<std::pair<int, float>> HashToken(
		const QString &text,
		int dim) {
	auto vec = std::vector<float>(dim, 0.f);
	static const auto space = QRegularExpression(u"\\s+"_q);
	const auto words = text.split(space, Qt::SkipEmptyParts);
	const auto crc = [](const QByteArray &bytes) {
		return uint32(crc32(
			0,
			reinterpret_cast<const Bytef*>(bytes.constData()),
			uInt(bytes.size())));
	};
	for (const auto &word : words) {
		const auto lower = word.toLower();
		auto weight = 1.f;
		for (const auto keyword : kKeywords) {
			if (lower.contains(QLatin1String(keyword))) {
				weight = 5.f;
				break;
			}
		}
		vec[crc(word.toUtf8()) % dim] += weight;
		const auto points = word.toUcs4();
		if (points.size() >= 3) {
			for (auto i = 0; i + 2 < points.size(); ++i) {
				const auto gram = QString::fromUcs4(
					reinterpret_cast<const char32_t*>(points.constData() + i),
					3).toUtf8();
				vec[crc(gram) % dim] += weight * 0.5f;
			}
		}
	}
	auto sum = 0.;
	for (const auto v : vec) {
		sum += double(v) * v;
	}
	auto result = std::vector<std::pair<int, float>>();
	if (sum <= 0.) {
		return result;
	}
	const auto norm = float(std::sqrt(sum));
	for (auto i = 0; i != dim; ++i) {
		if (vec[i] > 0.f) {
			result.emplace_back(i, vec[i] / norm);
		}
	}
	return result;
}

struct VectorStats {
	bool used = false;
	int scanned = 0;
	int malware = 0;
	int suspicious = 0;
	int outliers = 0;
	float meanAvg = 1.f;
	float minAvg = 1.f;
	float maxAvg = 0.f;
	float maxMalware = 0.f;
	float threshold = 0.649f;
};

[[nodiscard]] VectorStats ScanVectors(const QJsonArray &chunks) {
	auto result = VectorStats();
	if (!LoadModel()) {
		return result;
	}
	const auto &model = Loaded();
	result.used = true;
	result.threshold = model.threshold;
	auto sumAvg = 0.;
	for (const auto &entry : chunks) {
		const auto code = entry.toArray().at(2).toString();
		const auto vec = HashToken(code, model.dim);
		if (vec.empty()) {
			continue;
		}
		const auto dot = [&](const Matrix &matrix, int row) {
			const auto base = matrix.values.data() + size_t(row) * model.dim;
			auto sum = 0.f;
			for (const auto &[index, value] : vec) {
				sum += base[index] * value;
			}
			return sum;
		};
		auto top = std::array<float, 5>{};
		for (auto r = 0; r != model.clean.rows; ++r) {
			const auto value = dot(model.clean, r);
			if (value > top[0]) {
				top[0] = value;
				std::sort(top.begin(), top.end());
			}
		}
		auto mal = 0.f;
		for (auto r = 0; r != model.malware.rows; ++r) {
			mal = std::max(mal, dot(model.malware, r));
		}
		const auto mean = (top[0] + top[1] + top[2] + top[3] + top[4]) / 5.f;
		const auto maxClean = top[4];
		++result.scanned;
		sumAvg += mean;
		result.maxAvg = std::max(result.maxAvg, mean);
		result.minAvg = std::min(result.minAvg, mean);
		result.maxMalware = std::max(result.maxMalware, mal);
		if (code.size() > 30
			&& mal >= model.malwareThreshold
			&& (mal - maxClean) >= model.minContrast) {
			++result.malware;
		} else if (code.size() > 30
			&& mal >= (model.malwareThreshold - model.minContrast)
			&& (mal - maxClean) >= (model.minContrast / 2.f)) {
			++result.suspicious;
		} else if (mean < model.threshold && code.size() > 40) {
			++result.outliers;
		}
	}
	if (result.scanned) {
		result.meanAvg = float(sumAvg / result.scanned);
	}
	return result;
}

[[nodiscard]] int PenaltyFor(const Finding &finding) {
	for (const auto &entry : kPenalties) {
		if (finding.rule == QLatin1String(entry.rule)) {
			return entry.penalty;
		}
	}
	if (finding.level == u"CRITICAL"_q) {
		return 100;
	} else if (finding.level == u"HIGH"_q) {
		return 15;
	} else if (finding.level == u"INFO"_q) {
		return 0;
	}
	return 4;
}

void Score(Report &report, const VectorStats &stats, const QString &entropy) {
	auto seen = base::flat_map<QString, int>();
	auto warn = 0.;
	auto other = 0.;
	for (const auto &finding : report.findings) {
		const auto penalty = PenaltyFor(finding);
		if (penalty >= 100 || finding.level == u"CRITICAL"_q) {
			report.score = 0;
			report.level = u"high"_q;
			return;
		}
		const auto count = seen[finding.rule]++;
		const auto effective = count
			? std::min(1., penalty * 0.15)
			: double(penalty);
		if (finding.level == u"WARNING"_q) {
			warn += effective;
		} else {
			other += effective;
		}
	}
	auto staticScore = std::max(0., 60. - other - std::min(18., warn));
	staticScore = std::max(
		0.,
		staticScore - ((entropy == u"HIGH"_q)
			? 7.
			: (entropy == u"ELEVATED"_q)
			? 5.
			: 0.));
	auto vectorPenalty = 0.;
	const auto vectorVerdict = stats.malware
		? u"MALICIOUS"_q
		: (stats.suspicious || stats.outliers)
		? u"SUSPICIOUS"_q
		: u"CLEAN"_q;
	if (stats.scanned && vectorVerdict != u"CLEAN"_q) {
		if (stats.maxAvg < stats.threshold) {
			vectorPenalty += std::min(
				20.,
				(stats.threshold - stats.maxAvg) * 100.);
		} else if (stats.meanAvg < stats.threshold - 0.10) {
			vectorPenalty += std::min(
				10.,
				(stats.threshold - stats.meanAvg) * 50.);
		}
		if (stats.maxMalware >= 0.80) {
			vectorPenalty += std::min(
				20.,
				(stats.maxMalware - 0.78) * 100.);
		}
	}
	const auto vectorScore = std::max(0., 40. - vectorPenalty);
	report.score = std::clamp(
		int(std::round(staticScore + vectorScore)),
		0,
		100);
	if (stats.malware >= 3) {
		report.level = u"high"_q;
		report.score = std::min(report.score, 30);
		return;
	} else if (stats.malware == 2) {
		report.score = std::min(report.score, 64);
	} else if (stats.malware == 1) {
		report.score = std::min(report.score, 79);
	}
	if (report.score >= 80) {
		report.level = u"clean"_q;
	} else if (report.score >= 65) {
		report.level = u"low"_q;
	} else {
		report.level = u"medium"_q;
	}
}

[[nodiscard]] QString SystemPrompt() {
	const auto russian = Lang::Id().startsWith(u"ru"_q);
	return u"You are an expert security arbiter for exDesktop plugins. "
		"exDesktop is a Telegram Desktop client that runs Python plugins "
		"compatible with exteraGram. You receive static findings, network "
		"endpoints, vector similarity results and a slice of the plugin "
		"code. Separate legitimate functionality from real threats.\n"
		"LEGITIMATE (never malicious by itself): method hooks of the client, "
		"embedded UI resources, settings, requests to well known public APIs "
		"that match the plugin description, sending messages only on the "
		"user's explicit action.\n"
		"MALICIOUS: reading or uploading session data (tdata, key_datas, "
		"auth keys, cache4.db, tgnet.dat, userconfig.xml); reading messages "
		"from 777000 or parsing login codes; sending tokens, messages, "
		"contacts or files to external servers, webhooks or bots without "
		"user intent; background mass messaging or flood loops; "
		"resetAuthorizations or deleteAccount; downloading code from the "
		"network and running it with exec, eval or compile; clipboard "
		"wallet substitution; stars or gift transfers without consent; "
		"obfuscated or XOR encrypted payloads that are executed.\n"
		"SUSPICIOUS: dynamic code execution, process launching, native "
		"code access or hidden network dependencies without a clear reason.\n"
		"Output ONLY a JSON object: {\"verdict\": \"SAFE\" | \"SUSPICIOUS\" | "
		"\"MALICIOUS\", \"confidence\": 0.0-1.0, \"reason\": \"short "
		"technical explanation\"}. "_q
		+ (russian
			? u"The reason field must be written in Russian."_q
			: u"The reason field must be written in English."_q);
}

void Arbitrate(
		Report report,
		const QJsonObject &scan,
		Fn<void(Report)> done) {
	const auto provider = std::clamp(
		Int(u"guard_llm_provider"_q),
		0,
		int(kProviders.size()) - 1);
	const auto key = String(u"guard_llm_key"_q).trimmed();
	if (!provider || key.isEmpty()) {
		done(std::move(report));
		return;
	}
	auto endpoint = kProviders[provider].endpoint;
	auto model = String(u"guard_llm_model"_q).trimmed();
	if (model.isEmpty()) {
		model = kProviders[provider].model;
	}
	auto findings = QJsonArray();
	for (const auto &finding : report.findings) {
		findings.push_back(finding.rule + u": "_q + finding.detail);
	}
	if (report.malwareChunks) {
		findings.push_back(u"VECTOR_MALWARE_SIMILARITY: "_q
			+ QString::number(report.malwareChunks)
			+ u" chunks"_q);
	}
	const auto user = QJsonObject{
		{ u"static_threat_findings"_q, findings },
		{ u"network_endpoints"_q, scan.value(u"endpoints"_q) },
		{ u"vector_score"_q, report.score },
		{ u"code_slice"_q, scan.value(u"slice"_q).toString().left(14000) },
	};
	auto request = QNetworkRequest(QUrl(endpoint));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
	request.setTransferTimeout(30000);
	auto body = QJsonObject{
		{ u"model"_q, model },
		{ u"messages"_q, QJsonArray{
			QJsonObject{
				{ u"role"_q, u"system"_q },
				{ u"content"_q, SystemPrompt() },
			},
			QJsonObject{
				{ u"role"_q, u"user"_q },
				{ u"content"_q, QString::fromUtf8(
					QJsonDocument(user).toJson(QJsonDocument::Compact)) },
			},
		} },
		{ u"temperature"_q, 0. },
		{ u"max_tokens"_q, 1500 },
		{ u"response_format"_q, QJsonObject{
			{ u"type"_q, u"json_object"_q },
		} },
	};
	const auto reply = Network()->post(
		request,
		QJsonDocument(body).toJson(QJsonDocument::Compact));
	QObject::connect(reply, &QNetworkReply::finished, [=]() mutable {
		reply->deleteLater();
		const auto data = reply->readAll();
		const auto object = QJsonDocument::fromJson(data).object();
		const auto content = object.value(u"choices"_q).toArray().at(0)
			.toObject().value(u"message"_q).toObject()
			.value(u"content"_q).toString();
		auto start = content.indexOf('{');
		auto end = content.lastIndexOf('}');
		const auto parsed = (start >= 0 && end > start)
			? QJsonDocument::fromJson(
				content.mid(start, end - start + 1).toUtf8()).object()
			: QJsonObject();
		if (parsed.isEmpty()) {
			const auto error = object.value(u"error"_q).toObject()
				.value(u"message"_q).toString();
			report.llmError = error.isEmpty() ? reply->errorString() : error;
			done(std::move(report));
			return;
		}
		report.llmUsed = true;
		report.llmVerdict = parsed.value(u"verdict"_q).toString().toUpper();
		report.llmReason = parsed.value(u"reason"_q).toString();
		const auto confidence = parsed.value(u"confidence"_q).toDouble(0.5);
		report.llmConfidence = confidence;
		if (report.llmVerdict == u"MALICIOUS"_q && confidence >= 0.7) {
			report.findings.push_back({
				u"LLM_MALICIOUS"_q,
				u"CRITICAL"_q,
				report.llmReason,
			});
			report.level = u"high"_q;
			report.score = std::min(report.score, 20);
		} else if (report.llmVerdict == u"MALICIOUS"_q
			|| report.llmVerdict == u"SUSPICIOUS"_q) {
			if (report.level == u"clean"_q || report.level == u"low"_q) {
				report.level = u"medium"_q;
				report.score = std::min(report.score, 64);
			}
		} else if (report.llmVerdict == u"SAFE"_q
			&& confidence >= 0.7
			&& report.level != u"high"_q) {
			const auto severe = ranges::any_of(report.findings, [](
					const Finding &finding) {
				return (finding.level == u"CRITICAL"_q)
					|| (finding.level == u"HIGH"_q
						&& finding.rule != u"SUSPICIOUS_SIGNATURE_MATCH"_q);
			});
			if (!severe) {
				report.level = u"clean"_q;
				report.score = std::max(report.score, 80);
			}
		}
		done(std::move(report));
	});
}

void EnsureModel(Fn<void()> done) {
	QDir().mkpath(ModelDir());
	const auto exists = QFile::exists(ModelDir() + kVectorsFile)
		&& QFile::exists(ModelDir() + kMetaFile);
	const auto now = base::unixtime::now();
	if (exists && (now - Int(u"guard_model_check"_q)) < kCheckInterval) {
		done();
		return;
	}
	const auto get = [](const QString &name) {
		auto request = QNetworkRequest(QUrl(kModelBase + name));
		request.setTransferTimeout(100000);
		return Network()->get(request);
	};
	const auto version = get(u"version.txt"_q);
	QObject::connect(version, &QNetworkReply::finished, [=] {
		version->deleteLater();
		const auto remote = QString::fromUtf8(version->readAll()).trimmed();
		if (version->error() != QNetworkReply::NoError || remote.isEmpty()) {
			done();
			return;
		}
		Config::Instance().setInt(u"guard_model_check"_q, int(now));
		if (exists && remote == String(u"guard_model_version"_q)) {
			done();
			return;
		}
		const auto meta = get(kMetaFile);
		QObject::connect(meta, &QNetworkReply::finished, [=] {
			meta->deleteLater();
			const auto metaBytes = meta->readAll();
			if (meta->error() != QNetworkReply::NoError
				|| !QJsonDocument::fromJson(metaBytes).isObject()) {
				done();
				return;
			}
			const auto vectors = get(kVectorsFile);
			QObject::connect(vectors, &QNetworkReply::finished, [=] {
				vectors->deleteLater();
				const auto bytes = vectors->readAll();
				if (vectors->error() == QNetworkReply::NoError
					&& bytes.startsWith("PK")
					&& bytes.size() < 64 * 1024 * 1024) {
					auto file = QFile(ModelDir() + kVectorsFile);
					auto metaFile = QFile(ModelDir() + kMetaFile);
					if (file.open(QIODevice::WriteOnly)
						&& metaFile.open(QIODevice::WriteOnly)) {
						file.write(bytes);
						metaFile.write(metaBytes);
						Config::Instance().setString(
							u"guard_model_version"_q,
							remote);
						Loaded() = Model();
					}
				}
				done();
			});
		});
	});
}

[[nodiscard]] QStringList Blocked() {
	return String(u"plugins_guard_blocked"_q).split(',', Qt::SkipEmptyParts);
}

} // namespace

int Mode() {
	return Int(u"plugins_guard"_q);
}

bool AutoOnInstall() {
	return (Mode() == 0);
}

QString FileHash(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	hash.addData(&file);
	return QString::fromLatin1(hash.result().toHex());
}

bool IsBlocked(const QString &sha256) {
	return !sha256.isEmpty() && Blocked().contains(sha256);
}

void MarkBlocked(const QString &sha256) {
	if (sha256.isEmpty() || IsBlocked(sha256)) {
		return;
	}
	auto list = Blocked();
	list.push_back(sha256);
	Config::Instance().setString(u"plugins_guard_blocked"_q, list.join(','));
}

void Scan(const QString &path, Fn<void(Report)> done) {
	EnsureModel([=] {
		auto report = Report();
		report.sha256 = FileHash(path);
		const auto scan = Extera::Plugins::Call(u"guard_scan"_q, QJsonObject{
			{ u"path"_q, path },
		}).toObject();
		if (scan.isEmpty()) {
			report.error = u"scan failed"_q;
			done(std::move(report));
			return;
		}
		report.ok = true;
		for (const auto &entry : scan.value(u"findings"_q).toArray()) {
			const auto object = entry.toObject();
			report.findings.push_back({
				object.value(u"rule"_q).toString(),
				object.value(u"level"_q).toString(),
				object.value(u"detail"_q).toString(),
				object.value(u"line"_q).toInt(),
			});
		}
		for (const auto &url : scan.value(u"endpoints"_q).toArray()) {
			report.endpoints.push_back(url.toString());
		}
		const auto chunks = scan.value(u"chunks"_q).toArray();
		const auto entropy = scan.value(u"entropy"_q).toString();
		crl::async([=, report = std::move(report)]() mutable {
			const auto stats = ScanVectors(chunks);
			report.modelUsed = stats.used;
			report.modelVersion = String(u"guard_model_version"_q);
			report.cleanVectors = Loaded().clean.rows;
			report.malwareVectors = Loaded().malware.rows;
			report.meanSimilarity = stats.meanAvg;
			report.minSimilarity = stats.minAvg;
			report.maxMalwareSimilarity = stats.maxMalware;
			report.vectorVerdict = !stats.used
				? QString()
				: stats.malware
				? u"MALICIOUS"_q
				: (stats.suspicious || stats.outliers)
				? u"ANOMALY"_q
				: u"CLEAN"_q;
			report.scannedChunks = stats.scanned;
			report.malwareChunks = stats.malware;
			report.suspiciousChunks = stats.suspicious;
			if (stats.suspicious) {
				report.findings.push_back({
					u"SUSPICIOUS_SIGNATURE_MATCH"_q,
					u"HIGH"_q,
					QString::number(stats.suspicious),
				});
			}
			Score(report, stats, entropy);
			crl::on_main([=, report = std::move(report)]() mutable {
				Arbitrate(std::move(report), scan, done);
			});
		});
	});
}

QString LevelTitle(const QString &level) {
	if (level == u"clean"_q) {
		return tr::lng_exd_guard_level_clean(tr::now);
	} else if (level == u"low"_q) {
		return tr::lng_exd_guard_level_low(tr::now);
	} else if (level == u"medium"_q) {
		return tr::lng_exd_guard_level_medium(tr::now);
	}
	return tr::lng_exd_guard_level_high(tr::now);
}

QString RuleTitle(const QString &rule) {
	if (rule == u"TELEGRAM_DATABASE_EXFILTRATION"_q
		|| rule == u"TELEGRAM_INTERNAL_STORAGE_THEFT"_q) {
		return tr::lng_exd_guard_rule_session(tr::now);
	} else if (rule == u"UNAUTHORIZED_DATA_EXFILTRATION"_q
		|| rule == u"RAW_IP_C2_ENDPOINT"_q
		|| rule == u"C2_TELEGRAM_BOT_API"_q
		|| rule == u"EMBEDDED_TELEGRAM_BOT_TOKEN"_q) {
		return tr::lng_exd_guard_rule_exfiltration(tr::now);
	} else if (rule == u"REMOTE_CODE_EXECUTION_STAGER"_q) {
		return tr::lng_exd_guard_rule_remote_code(tr::now);
	} else if (rule == u"DYNAMIC_CODE_EXECUTION"_q
		|| rule == u"DYNAMIC_CODE_COMPILATION"_q) {
		return tr::lng_exd_guard_rule_dynamic(tr::now);
	} else if (rule == u"SERVICE_2FA_CODE_INTERCEPTION"_q) {
		return tr::lng_exd_guard_rule_codes(tr::now);
	} else if (rule == u"CLIPBOARD_CRYPTO_CLIPPER"_q) {
		return tr::lng_exd_guard_rule_clipper(tr::now);
	} else if (rule == u"OBFUSCATED_CODE_STRUCTURE"_q
		|| rule == u"HIGH_ENTROPY_LITERAL_DETECTED"_q) {
		return tr::lng_exd_guard_rule_obfuscation(tr::now);
	} else if (rule == u"PRIVILEGED_COMMAND_EXECUTION"_q
		|| rule == u"CTYPES_NATIVE_ACCESS"_q) {
		return tr::lng_exd_guard_rule_system(tr::now);
	} else if (rule == u"EXTERNAL_IP_QUERY_SERVICE"_q) {
		return tr::lng_exd_guard_rule_ip(tr::now);
	} else if (rule == u"SUSPICIOUS_SIGNATURE_MATCH"_q) {
		return tr::lng_exd_guard_rule_similar(tr::now);
	} else if (rule == u"LLM_MALICIOUS"_q) {
		return tr::lng_exd_guard_rule_ai(tr::now);
	}
	return rule;
}

QString Summary(const Report &report) {
	auto lines = QStringList();
	auto seen = QStringList();
	for (const auto &finding : report.findings) {
		const auto title = RuleTitle(finding.rule);
		if (!seen.contains(title)) {
			seen.push_back(title);
			lines.push_back(u"- "_q + title);
		}
	}
	if (report.malwareChunks) {
		lines.push_back(u"- "_q + tr::lng_exd_guard_rule_malware(
			tr::now,
			lt_amount,
			QString::number(report.malwareChunks)));
	}
	if (report.llmUsed && !report.llmReason.isEmpty()) {
		lines.push_back(QString());
		lines.push_back(report.llmReason);
	}
	return lines.join('\n');
}

} // namespace Exd::PluginGuard
