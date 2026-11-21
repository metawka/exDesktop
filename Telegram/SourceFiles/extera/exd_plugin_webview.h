#pragma once

namespace Extera::PluginWeb {

void Open(const QString &payload);
[[nodiscard]] bool Owns(int64 id, const QString &owner);
void Eval(int64 id, const QString &script);
void Close(int64 id);
void CloseAll();

} // namespace Extera::PluginWeb
