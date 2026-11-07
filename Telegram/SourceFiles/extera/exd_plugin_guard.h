#pragma once

namespace Exd::PluginGuard {

struct Finding {
	QString rule;
	QString level;
	QString detail;
	int line = 0;
};

struct Report {
	bool ok = false;
	QString error;
	int score = 100;
	QString level;
	std::vector<Finding> findings;
	int malwareChunks = 0;
	int suspiciousChunks = 0;
	int scannedChunks = 0;
	bool modelUsed = false;
	QString modelVersion;
	QString vectorVerdict;
	int cleanVectors = 0;
	int malwareVectors = 0;
	float meanSimilarity = 0.f;
	float minSimilarity = 0.f;
	float maxMalwareSimilarity = 0.f;
	QStringList endpoints;
	double llmConfidence = 0.;
	bool llmUsed = false;
	QString llmVerdict;
	QString llmReason;
	QString llmError;
	QString sha256;
};

[[nodiscard]] int Mode();
[[nodiscard]] bool AutoOnInstall();
[[nodiscard]] QString FileHash(const QString &path);
[[nodiscard]] bool IsBlocked(const QString &sha256);
void MarkBlocked(const QString &sha256);
void Unblock(const QString &sha256);
void Scan(const QString &path, Fn<void(Report)> done);

[[nodiscard]] QString LevelTitle(const QString &level);
[[nodiscard]] QString RuleTitle(const QString &rule);
[[nodiscard]] QString Summary(const Report &report);

} // namespace Exd::PluginGuard
