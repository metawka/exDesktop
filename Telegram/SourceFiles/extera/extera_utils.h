#pragma once

namespace Extera {

[[nodiscard]] QString FormatTime(const QTime &time);
[[nodiscard]] bool HasZalgo(const QString &text);
[[nodiscard]] QString FilterZalgo(QString text);
void FilterZalgo(TextWithEntities &textWithEntities);
[[nodiscard]] std::optional<QString> InlineMathValue(const QString &text);
[[nodiscard]] std::optional<QString> InlineMathResult(
	const QString &text);

} // namespace Extera
