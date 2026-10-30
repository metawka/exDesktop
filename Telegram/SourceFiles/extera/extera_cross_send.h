#pragma once

namespace Main {
class Account;
class Session;
} // namespace Main

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Extera {

[[nodiscard]] bool HasOtherAccounts(not_null<Main::Session*> from);

void AddSendFromAccountActions(
	not_null<Ui::PopupMenu*> menu,
	not_null<Main::Session*> from,
	MessageIdsList ids,
	Fn<void()> closeBox);

void SendFromAccount(
	not_null<Main::Session*> from,
	MessageIdsList ids,
	not_null<Main::Account*> to);

} // namespace Extera
