#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Extera {

struct AiChatMessage {
	bool user = false;
	bool error = false;
	QString text;
	TimeId date = 0;
};

void ShowAiChat(not_null<Window::SessionController*> controller);
[[nodiscard]] std::vector<AiChatMessage> AiChatHistory();
[[nodiscard]] QString AiChatPartial();
[[nodiscard]] bool AiChatWaiting();
[[nodiscard]] QString AiChatModel();
[[nodiscard]] rpl::producer<> AiChatUpdates();
void AiChatClear();
bool AiChatSend(
	not_null<Window::SessionController*> controller,
	const QString &text);
void ShowKeyBox(not_null<Window::SessionController*> controller);

[[nodiscard]] bool CanTranscribe();
void Transcribe(
	const QByteArray &data,
	const QString &mime,
	Fn<void(QString text, bool ok)> done);

} // namespace Extera
