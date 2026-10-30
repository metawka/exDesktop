#pragma once

namespace Extera::WsProxy {

[[nodiscard]] bool Available();
[[nodiscard]] bool Enabled();
void Start();
void SetEnabled(bool enabled);

} // namespace Extera::WsProxy
