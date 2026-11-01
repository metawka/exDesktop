#pragma once

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Extera::PluginTrust {

struct Source {
	uint64 id = 0;
	QString username;
};

[[nodiscard]] std::vector<Source> Sources();
[[nodiscard]] std::vector<uint64> TrustedChannels();
[[nodiscard]] DocumentId TrustedEmoji();
[[nodiscard]] bool IsTrustedChannel(not_null<PeerData*> peer);
[[nodiscard]] QString Sha256(const QString &path);
void Verify(
	not_null<Main::Session*> session,
	const QString &path,
	Fn<void(bool)> done);

} // namespace Extera::PluginTrust
