#pragma once

class QJsonObject;

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Extera::Views {

void ShowSheet(const QString &payload);
bool FillBox(not_null<Ui::GenericBox*> box, int64 id, const QJsonObject &rootJson);
void Update(const QString &payload);
void DismissSheet(int64 id);
void RenderCanvas(int64 id, int width, int height, const QString &ops);
double MeasureText(const QString &text, int sizePx, bool bold);
QString UiMetrics();
int64 ThemeColor(const QString &name);

} // namespace Extera::Views
