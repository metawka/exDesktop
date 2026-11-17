#pragma once

namespace Extera::PluginWeb {

void Open(const QString &payload);
void Eval(int64 id, const QString &script);
void Close(int64 id);
void CloseAll();

} // namespace Extera::PluginWeb
