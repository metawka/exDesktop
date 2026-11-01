#pragma once

namespace Extera::Views {

void ShowSheet(const QString &payload);
void Update(const QString &payload);
void DismissSheet(int64 id);
void RenderCanvas(int64 id, int width, int height, const QString &ops);
double MeasureText(const QString &text, int sizePx, bool bold);
QString UiMetrics();
int64 ThemeColor(const QString &name);

} // namespace Extera::Views
