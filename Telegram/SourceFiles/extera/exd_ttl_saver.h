#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Exd::TtlSaver {

void Start(not_null<Main::Session*> session);

} // namespace Exd::TtlSaver
