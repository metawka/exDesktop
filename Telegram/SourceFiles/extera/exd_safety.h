#pragma once

namespace Ui {
class RpWidget;
class InputField;
} // namespace Ui

namespace Exd::Safety {

void SetupSecretBar(
	not_null<Ui::RpWidget*> parent,
	not_null<Ui::InputField*> field,
	rpl::producer<QRect> area);

void MarkSuspiciousLinks(TextWithEntities &text);

[[nodiscard]] bool LooksLikeSecret(const QString &text);
[[nodiscard]] QString LinkWarning(const QString &text, const QString &url);

} // namespace Exd::Safety
