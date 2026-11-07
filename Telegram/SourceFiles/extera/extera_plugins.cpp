#include "extera/extera_plugins.h"
#include "extera/extera_menu.h"
#include "extera/extera_plugins_ui.h"

#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")

#include "api/api_common.h"
#include "api/api_editing.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "extera/extera_views.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "settings.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_entity.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include "extera/extera_plugin_trust.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <thread>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Extera::Plugins {
namespace {

struct State {
	bool started = false;
	bool available = false;
	QString error;
	PyThreadState *mainThread = nullptr;
	PyObject *dispatch = nullptr;
	rpl::event_stream<> listChanges;
	rpl::event_stream<QString> settingsChanges;
	bool eventInFlight = false;
	bool intentInFlight = false;
	bool urlInFlight = false;
	bool urlFromPlugin = false;
	std::deque<QJsonObject> recent;
	rpl::lifetime lifetime;
};

constexpr auto kHangTimeout = crl::time(10000);
std::atomic<crl::time> CallStarted = 0;
std::atomic<bool> HangReported = false;
std::atomic<bool> WatchdogRunning = false;

State &Instance() {
	static auto result = State();
	return result;
}

bool BypassFlag = false;
std::atomic<int64> CurrentDialog = 0;

class Gil final {
public:
	Gil() : _state(PyGILState_Ensure()) {
	}
	~Gil() {
		PyGILState_Release(_state);
	}

private:
	PyGILState_STATE _state;

};

[[nodiscard]] QString RuntimePath() {
#ifdef Q_OS_WIN
	return cExeDir() + u"python/"_q;
#elif defined(Q_OS_MAC)
	const auto resources = cExeDir() + u"../Resources/python/"_q;
	return QFile::exists(resources + u"extera/extera_host.py"_q)
		? resources
		: (cExeDir() + u"python/"_q);
#else
	// Linux / generic Unix: check local candidates where extera_host.py might reside
	const auto localToExe = cExeDir() + u"python/"_q;
	if (QFile::exists(localToExe + u"extera/extera_host.py"_q)) {
		return localToExe;
	}
	const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/python/"_q;
	if (QFile::exists(appData + u"extera/extera_host.py"_q)) {
		return appData;
	}
	const auto localShare = QDir::homePath() + u"/.local/share/exDesktop/python/"_q;
	if (QFile::exists(localShare + u"extera/extera_host.py"_q)) {
		return localShare;
	}
	const auto systemShare = u"/usr/share/exDesktop/python/"_q;
	if (QFile::exists(systemShare + u"extera/extera_host.py"_q)) {
		return systemShare;
	}
	return QFile::exists(localToExe) ? localToExe : appData;
#endif
}

[[nodiscard]] QString DataPath() {
#ifdef Q_OS_WIN
	return cWorkingDir() + u"tdata/plugins/"_q;
#else
	if (cWorkingDir().isEmpty()) {
		return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/plugins/"_q;
	}
	return cWorkingDir() + u"tdata/plugins/"_q;
#endif
}

[[nodiscard]] QString StartMarkerPath() {
	return DataPath() + u"starting.lock"_q;
}

[[nodiscard]] QString HangMarkerPath() {
	return DataPath() + u"plugin_hang.lock"_q;
}

void ShowAutoSafeModeBox(bool hang = false) {
	base::call_delayed(3000, [=] {
		const auto window = Core::App().activePrimaryWindow();
		const auto controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return;
		}
		controller->show(Ui::MakeConfirmBox({
			.text = (hang
				? tr::lng_exd_safe_mode_hang()
				: tr::lng_extera_safe_mode_auto()),
			.confirmed = [=](Fn<void()> close) {
				close();
				UrlClickHandler::Open(u"tg://settings/extera/plugins"_q);
			},
			.confirmText = tr::lng_extera_safe_mode_open(),
			.title = tr::lng_extera_safe_mode_title(),
		}));
	});
}

void EnterAutoSafeMode(bool hang) {
	if (Extera::Bool(u"plugins_safe_mode"_q)
		|| Extera::Bool(u"plugins_no_auto_safe"_q)) {
		return;
	}
	Extera::Config::Instance().setBool(u"plugins_safe_mode"_q, true);
	Extera::Config::Instance().saveNow();
	ShowAutoSafeModeBox(hang);
}

void StartWatchdog() {
	if (WatchdogRunning.exchange(true)) {
		return;
	}
	const auto path = HangMarkerPath();
	std::thread([path] {
		while (WatchdogRunning.load()) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			const auto started = CallStarted.load();
			if (started
				&& !HangReported.load()
				&& (crl::now() - started) > kHangTimeout) {
				HangReported = true;
				auto marker = QFile(path);
				if (marker.open(QIODevice::WriteOnly)) {
					marker.write("1");
				}
			}
		}
	}).detach();
}

[[nodiscard]] QString FromPy(PyObject *object) {
	if (!object || !PyUnicode_Check(object)) {
		return QString();
	}
	Py_ssize_t size = 0;
	const auto data = PyUnicode_AsUTF8AndSize(object, &size);
	return data ? QString::fromUtf8(data, int(size)) : QString();
}

[[nodiscard]] Window::SessionController *ActiveController() {
	if (const auto window = Core::App().activePrimaryWindow()) {
		return window->sessionController();
	}
	return nullptr;
}

void ShowToast(const QString &text) {
	if (const auto controller = ActiveController()) {
		controller->showToast(text);
	}
}

[[nodiscard]] PeerData *ResolveDialog(
		not_null<Main::Session*> session,
		int64 dialogId) {
	const auto owner = &session->data();
	if (dialogId > 0) {
		if (const auto user = owner->peerLoaded(peerFromUser(UserId(dialogId)))) {
			return user;
		}
		return owner->peer(peerFromUser(UserId(dialogId)));
	} else if (dialogId < 0) {
		const auto bare = BareId(-dialogId);
		if (const auto channel = owner->peerLoaded(
				peerFromChannel(ChannelId(bare)))) {
			return channel;
		}
		if (const auto chat = owner->peerLoaded(peerFromChat(ChatId(bare)))) {
			return chat;
		}
		return owner->peer(peerFromChannel(ChannelId(bare)));
	}
	return nullptr;
}

[[nodiscard]] EntityType TypeFromKind(
		const QString &kind,
		bool collapsed) {
	if (kind == u"bold"_q) {
		return EntityType::Bold;
	} else if (kind == u"italic"_q) {
		return EntityType::Italic;
	} else if (kind == u"underline"_q) {
		return EntityType::Underline;
	} else if (kind == u"strike"_q || kind == u"strikethrough"_q) {
		return EntityType::StrikeOut;
	} else if (kind == u"code"_q) {
		return EntityType::Code;
	} else if (kind == u"pre"_q) {
		return EntityType::Pre;
	} else if (kind == u"blockquote"_q || kind == u"blockquote_collapsed"_q) {
		return EntityType::Blockquote;
	} else if (kind == u"spoiler"_q) {
		return EntityType::Spoiler;
	} else if (kind == u"url"_q || kind == u"text_link"_q) {
		return EntityType::CustomUrl;
	} else if (kind == u"custom_emoji"_q) {
		return EntityType::CustomEmoji;
	}
	return EntityType::Invalid;
}

[[nodiscard]] EntitiesInText EntitiesFromJsonArray(const QJsonArray &array) {
	auto result = EntitiesInText();
	for (const auto &value : array) {
		auto type = EntityType::Invalid;
		auto offset = 0;
		auto length = 0;
		auto data = QString();
		const auto entry = value.toObject();
		if (!entry.isEmpty()) {
			type = TypeFromKind(
				entry.value(u"type"_q).toString(),
				entry.value(u"collapsed"_q).toBool());
			offset = entry.value(u"offset"_q).toInt();
			length = entry.value(u"length"_q).toInt();
			const auto document = entry.value(u"document_id"_q);
			if (entry.contains(u"url"_q)) {
				data = entry.value(u"url"_q).toString();
			} else if (entry.contains(u"language"_q)) {
				data = entry.value(u"language"_q).toString();
			} else if (document.isString()) {
				data = document.toString();
			} else if (!document.isUndefined()) {
				data = QString::number(double(document.toDouble()), 'f', 0);
			}
			if (type == EntityType::Blockquote
				&& entry.value(u"collapsed"_q).toBool()) {
				data = u"1"_q;
			}
		} else {
			const auto legacy = value.toArray();
			if (legacy.size() < 3) {
				continue;
			}
			const auto legacyKind = legacy[0].toString();
			type = TypeFromKind(
				legacyKind,
				legacyKind == u"blockquote_collapsed"_q);
			offset = legacy[1].toInt();
			length = legacy[2].toInt();
			data = legacy.size() > 3 ? legacy[3].toString() : QString();
			if (type == EntityType::Blockquote
				&& legacyKind == u"blockquote_collapsed"_q) {
				data = u"1"_q;
			}
		}
		if (type != EntityType::Invalid && length > 0) {
			result.push_back({ type, offset, length, data });
		}
	}
	return result;
}

[[nodiscard]] EntitiesInText ParseEntities(const QString &json) {
	return EntitiesFromJsonArray(
		QJsonDocument::fromJson(json.toUtf8()).array());
}

void SendFromPlugin(
		int64 dialogId,
		const QString &text,
		const QString &entities,
		int64 replyTo) {
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	const auto session = &controller->session();
	const auto peer = (dialogId != 0)
		? ResolveDialog(session, dialogId)
		: (controller->activeChatCurrent().peer());
	if (!peer) {
		ShowToast(tr::lng_extera_plugins_no_chat(tr::now));
		return;
	}
	const auto history = session->data().history(peer);
	auto message = Api::MessageToSend(Api::SendAction(history));
	auto parsed = TextWithEntities{ text, ParseEntities(entities) };
	message.textWithTags = TextWithTags{
		parsed.text,
		TextUtilities::ConvertEntitiesToTextTags(parsed.entities),
	};
	if (replyTo > 0) {
		message.action.replyTo.messageId = FullMsgId(peer->id, MsgId(replyTo));
	}
	BypassFlag = true;
	session->api().sendMessage(std::move(message));
	BypassFlag = false;
}

void ShowAlert(const QString &payload) {
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	const auto data = QJsonDocument::fromJson(payload.toUtf8()).object();
	const auto id = data.value(u"id"_q).toInt();
	const auto title = data.value(u"title"_q).toString();
	const auto message = data.value(u"message"_q).toString();
	const auto items = data.value(u"items"_q).toArray();
	const auto buttons = data.value(u"buttons"_q).toArray();
	const auto answered = std::make_shared<bool>(false);
	const auto answer = [=](QJsonValue which) {
		if (*answered) {
			return;
		}
		*answered = true;
		Call(u"alert_click"_q, QJsonObject{
			{ u"id"_q, id },
			{ u"which"_q, which },
		});
	};
	if (data.value(u"exd"_q).isObject()) {
		ShowPermissionAlert(controller, data, answer);
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->boxClosing() | rpl::on_next([=] {
			answer(u"dismiss"_q);
		}, box->lifetime());
		if (!title.isEmpty()) {
			box->setTitle(rpl::single(title));
		}
		if (!message.isEmpty()) {
			box->addRow(object_ptr<Ui::FlatLabel>(
				box,
				rpl::single(message),
				st::boxLabel))->setSelectable(true);
		}
		for (auto i = 0; i != items.size(); ++i) {
			const auto button = box->verticalLayout()->add(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(items[i].toString()),
					st::settingsButtonNoIcon));
			button->setClickedCallback([=] {
				answer(i);
				box->closeBox();
			});
		}
		for (const auto &value : buttons) {
			const auto entry = value.toObject();
			const auto which = entry.value(u"which"_q).toInt();
			box->addButton(
				rpl::single(entry.value(u"text"_q).toString()),
				[=] {
					answer(which);
					box->closeBox();
				});
		}
		if (buttons.isEmpty()) {
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		}
	}));
}

// Native

PyObject *NativeLog(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	LOG(("Plugins: %1").arg(QString::fromUtf8(text)));
	return Py_BuildValue("");
}

PyObject *NativeToast(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] { ShowToast(value); });
	return Py_BuildValue("");
}

// Sandbox

struct SandboxEntry {
	uint64 token = 0;
	QString owner;
};

PyObject *SandboxDecide = nullptr;
PyObject *SandboxOwnerFn = nullptr;
PyObject *SandboxQueryFn = nullptr;
PyObject *SandboxAnswerFn = nullptr;
PyObject *SandboxCreateDynamic = nullptr;
PyObject *SandboxCreateBuiltin = nullptr;
PyObject *SandboxPermissionError = nullptr;
PyObject *SandboxImportError = nullptr;
PyObject *SandboxEmptyTuple = nullptr;
std::thread::id SandboxMain;
std::mutex SandboxMutex;
std::map<uint64, QString> SandboxTickets;
thread_local int SandboxDepth = 0;
thread_local bool SandboxMarked = false;
thread_local std::vector<SandboxEntry> SandboxOwners;

[[nodiscard]] uint64 SandboxToken() {
	return base::RandomValue<uint64>() | 1ULL;
}

[[nodiscard]] bool SandboxWatched(std::string_view event) {
	static const auto exact = std::set<std::string_view>{
		"open", "os.listdir", "os.scandir", "glob.glob", "os.remove",
		"os.rmdir", "os.mkdir", "os.chmod", "os.chown", "os.utime",
		"os.truncate", "os.mkfifo", "os.mknod", "os.setxattr",
		"os.removexattr", "shutil.rmtree", "shutil.chown", "os.symlink",
		"os.link", "os.rename", "shutil.move", "shutil.copyfile",
		"shutil.copytree", "shutil.copymode", "shutil.copystat",
		"shutil.make_archive", "shutil.unpack_archive", "tempfile.mkstemp",
		"tempfile.mkdtemp", "subprocess.Popen", "os.system", "os.exec",
		"os.spawn", "os.posix_spawn", "os.startfile", "os.fork",
		"os.forkpty", "os.kill", "os.killpg", "pty.spawn", "import",
		"compile", "sqlite3.connect", "sqlite3.enable_load_extension",
		"sqlite3.load_extension", "socket.getaddrinfo",
		"socket.gethostbyname", "socket.gethostbyname_ex",
		"socket.gethostbyaddr", "socket.getnameinfo", "socket.gethostname",
		"socket.sethostname", "socket.connect", "socket.bind",
		"socket.sendto", "socket.sendmsg", "urllib.Request",
		"http.client.connect", "ftplib.connect", "smtplib.connect",
		"poplib.connect", "imaplib.open", "nntplib.connect",
		"telnetlib.Telnet.open", "code.__new__", "marshal.loads",
		"marshal.load", "sys.settrace", "sys.setprofile", "gc.get_objects",
		"gc.get_referrers", "gc.get_referents", "sys._current_frames",
		"sys.addaudithook", "cpython.PyInterpreterState_Clear", "os.chdir",
		"os.chroot", "os.add_dll_directory", "os.putenv", "os.unsetenv",
	};
	if (exact.contains(event)) {
		return true;
	}
	for (const auto prefix : {
			std::string_view("ctypes."),
			std::string_view("winreg."),
			std::string_view("_winapi."),
			std::string_view("msvcrt."),
			std::string_view("fcntl."),
			std::string_view("exd.") }) {
		if (event.starts_with(prefix)) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] QByteArray SandboxVerdict(const char *event, PyObject *args) {
	if (!SandboxDecide) {
		return QByteArray();
	}
	++SandboxDepth;
	PyObject *type = nullptr;
	PyObject *value = nullptr;
	PyObject *trace = nullptr;
	PyErr_Fetch(&type, &value, &trace);
	auto result = QByteArray();
	const auto verdict = PyObject_CallFunction(
		SandboxDecide,
		"sO",
		event,
		args ? args : SandboxEmptyTuple);
	if (verdict) {
		if (PyUnicode_Check(verdict)) {
			if (const auto utf = PyUnicode_AsUTF8(verdict)) {
				result = QByteArray(utf);
			}
		}
		Py_DECREF(verdict);
	}
	PyErr_Clear();
	PyErr_Restore(type, value, trace);
	--SandboxDepth;
	return result;
}

void SandboxRaise(const QByteArray &verdict) {
	const auto import = verdict.startsWith("import:");
	const auto text = import ? verdict.mid(7) : verdict;
	PyErr_SetString(
		import ? SandboxImportError : SandboxPermissionError,
		text.constData());
}

int SandboxAudit(const char *event, PyObject *args, void *data) {
	if (!SandboxDecide || SandboxDepth > 0 || !SandboxWatched(event)) {
		return 0;
	}
	const auto verdict = SandboxVerdict(event, args);
	if (verdict.isEmpty()) {
		return 0;
	}
	SandboxRaise(verdict);
	return -1;
}

[[nodiscard]] QString SandboxOwnerNow() {
	if (!SandboxOwnerFn) {
		return QString();
	}
	++SandboxDepth;
	PyObject *type = nullptr;
	PyObject *value = nullptr;
	PyObject *trace = nullptr;
	PyErr_Fetch(&type, &value, &trace);
	auto result = QString();
	if (const auto owner = PyObject_CallNoArgs(SandboxOwnerFn)) {
		if (PyUnicode_Check(owner)) {
			if (const auto utf = PyUnicode_AsUTF8(owner)) {
				result = QString::fromUtf8(utf);
			}
		}
		Py_DECREF(owner);
	}
	PyErr_Clear();
	PyErr_Restore(type, value, trace);
	--SandboxDepth;
	return result;
}

void SandboxPushOwner(const QString &owner, uint64 token) {
	SandboxOwners.push_back({ token, owner });
}

void SandboxPopOwner(uint64 token) {
	for (auto i = int(SandboxOwners.size()) - 1; i >= 0; --i) {
		if (SandboxOwners[i].token == token) {
			SandboxOwners.resize(i);
			return;
		}
	}
}

void SandboxAnswerCall(const QString &token, const QString &value) {
	if (!SandboxAnswerFn) {
		return;
	}
	auto gil = Gil();
	++SandboxDepth;
	const auto result = PyObject_CallFunction(
		SandboxAnswerFn,
		"ss",
		token.toUtf8().constData(),
		value.toUtf8().constData());
	Py_XDECREF(result);
	PyErr_Clear();
	--SandboxDepth;
}

[[nodiscard]] QString SandboxPluginName(const QString &id) {
	for (const auto &entry : Call(u"list"_q).toArray()) {
		const auto info = entry.toObject();
		if (info.value(u"id"_q).toString() == id) {
			const auto name = info.value(u"name"_q).toString();
			return name.isEmpty() ? id : name;
		}
	}
	return id;
}

PyObject *NativeSbxInstall(PyObject *self, PyObject *args) {
	PyObject *decide = nullptr;
	PyObject *owner = nullptr;
	PyObject *query = nullptr;
	PyObject *answer = nullptr;
	PyObject *dynamic = nullptr;
	PyObject *builtin = nullptr;
	PyObject *permission = nullptr;
	PyObject *importError = nullptr;
	if (!PyArg_ParseTuple(
			args,
			"OOOOOOOO",
			&decide,
			&owner,
			&query,
			&answer,
			&dynamic,
			&builtin,
			&permission,
			&importError)) {
		return nullptr;
	}
	if (SandboxDecide) {
		return PyBool_FromLong(0);
	}
	for (const auto object : {
			decide, owner, query, answer, dynamic, builtin, permission, importError }) {
		Py_INCREF(object);
	}
	SandboxEmptyTuple = PyTuple_New(0);
	SandboxOwnerFn = owner;
	SandboxQueryFn = query;
	SandboxAnswerFn = answer;
	SandboxCreateDynamic = dynamic;
	SandboxCreateBuiltin = builtin;
	SandboxPermissionError = permission;
	SandboxImportError = importError;
	SandboxMain = std::this_thread::get_id();
	SandboxDecide = decide;
	PySys_AddAuditHook(SandboxAudit, nullptr);
	return PyBool_FromLong(1);
}

PyObject *NativeSbxState(PyObject *self, PyObject *args) {
	const auto owner = SandboxOwners.empty()
		? Py_BuildValue("")
		: PyUnicode_FromString(SandboxOwners.back().owner.toUtf8().constData());
	return Py_BuildValue(
		"(NNN)",
		owner,
		PyBool_FromLong(SandboxMarked ? 1 : 0),
		PyBool_FromLong(std::this_thread::get_id() == SandboxMain ? 1 : 0));
}

PyObject *NativeSbxPush(PyObject *self, PyObject *args) {
	const char *owner = nullptr;
	if (!PyArg_ParseTuple(args, "s", &owner)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(owner);
	if (value.isEmpty()) {
		PyErr_SetString(SandboxPermissionError, "exDesktop sandbox: invalid owner");
		return nullptr;
	}
	const auto token = SandboxToken();
	SandboxPushOwner(value, token);
	return PyLong_FromUnsignedLongLong(token);
}

PyObject *NativeSbxPop(PyObject *self, PyObject *args) {
	unsigned long long token = 0;
	if (!PyArg_ParseTuple(args, "K", &token)) {
		return nullptr;
	}
	SandboxPopOwner(uint64(token));
	return Py_BuildValue("");
}

PyObject *NativeSbxOwner(PyObject *self, PyObject *args) {
	return PyUnicode_FromString(SandboxOwnerNow().toUtf8().constData());
}

PyObject *NativeSbxTicket(PyObject *self, PyObject *args) {
	const auto owner = SandboxOwnerNow();
	const auto token = SandboxToken();
	{
		std::lock_guard lock(SandboxMutex);
		SandboxTickets[token] = owner;
	}
	return PyLong_FromUnsignedLongLong(token);
}

PyObject *NativeSbxThreadEntry(PyObject *self, PyObject *args) {
	unsigned long long ticket = 0;
	PyObject *callable = nullptr;
	PyObject *callArgs = nullptr;
	PyObject *callKwargs = nullptr;
	if (!PyArg_ParseTuple(args, "KOOO", &ticket, &callable, &callArgs, &callKwargs)) {
		return nullptr;
	}
	auto owner = u"<unknown>"_q;
	{
		std::lock_guard lock(SandboxMutex);
		const auto i = SandboxTickets.find(uint64(ticket));
		if (i != end(SandboxTickets)) {
			owner = i->second;
			SandboxTickets.erase(i);
		}
	}
	SandboxMarked = true;
	const auto token = SandboxToken();
	SandboxPushOwner(owner, token);
	const auto kwargs = PyDict_Check(callKwargs) ? callKwargs : nullptr;
	const auto result = PyTuple_Check(callArgs)
		? PyObject_Call(callable, callArgs, kwargs)
		: PyObject_Call(callable, SandboxEmptyTuple, kwargs);
	SandboxPopOwner(token);
	return result;
}

PyObject *SandboxGuardedCall(const char *event, PyObject *real, PyObject *args) {
	if (!real) {
		PyErr_SetString(SandboxPermissionError, "exDesktop sandbox: not available");
		return nullptr;
	}
	if (SandboxDepth == 0) {
		const auto verdict = SandboxVerdict(event, args);
		if (!verdict.isEmpty()) {
			SandboxRaise(verdict);
			return nullptr;
		}
	}
	return PyObject_Call(real, args, nullptr);
}

PyObject *NativeSbxCreateDynamic(PyObject *self, PyObject *args) {
	return SandboxGuardedCall("exd.create_dynamic", SandboxCreateDynamic, args);
}

PyObject *NativeSbxCreateBuiltin(PyObject *self, PyObject *args) {
	return SandboxGuardedCall("exd.create_builtin", SandboxCreateBuiltin, args);
}

PyObject *NativeSbxPrompt(PyObject *self, PyObject *args) {
	const char *kind = nullptr;
	const char *plugin = nullptr;
	const char *target = nullptr;
	const char *message = nullptr;
	const char *buttons = nullptr;
	const char *token = nullptr;
	if (!PyArg_ParseTuple(args, "ssssss", &kind, &plugin, &target, &message, &buttons, &token)) {
		return nullptr;
	}
	const auto kindValue = QString::fromUtf8(kind);
	const auto pluginValue = QString::fromUtf8(plugin);
	const auto targetValue = QString::fromUtf8(target);
	const auto messageValue = QString::fromUtf8(message);
	const auto buttonsValue = QJsonDocument::fromJson(QByteArray(buttons)).array();
	const auto tokenValue = QString::fromUtf8(token);
	crl::on_main([=] {
		const auto controller = ActiveController();
		if (!controller) {
			SandboxAnswerCall(tokenValue, u"deny"_q);
			return;
		}
		auto values = std::map<int, QString>();
		for (const auto &entry : buttonsValue) {
			const auto object = entry.toObject();
			values[object.value(u"which"_q).toInt()]
				= object.value(u"value"_q).toString();
		}
		const auto data = QJsonObject{
			{ u"exd"_q, QJsonObject{
				{ u"kind"_q, kindValue },
				{ u"plugin"_q, pluginValue },
				{ u"target"_q, targetValue },
			} },
			{ u"message"_q, QString(messageValue).replace(
				u"{name}"_q,
				SandboxPluginName(pluginValue)) },
			{ u"buttons"_q, buttonsValue },
		};
		const auto answered = std::make_shared<bool>(false);
		ShowPermissionAlert(controller, data, [=](QJsonValue which) {
			if (*answered) {
				return;
			}
			*answered = true;
			const auto i = which.isDouble()
				? values.find(which.toInt())
				: end(values);
			SandboxAnswerCall(
				tokenValue,
				(i != end(values)) ? i->second : u"dismiss"_q);
		});
	});
	return Py_BuildValue("");
}

PyObject *NativeSbxToast(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	const char *plugin = nullptr;
	if (!PyArg_ParseTuple(args, "ss", &text, &plugin)) {
		return nullptr;
	}
	const auto textValue = QString::fromUtf8(text);
	const auto pluginValue = QString::fromUtf8(plugin);
	crl::on_main([=] {
		ShowToast(QString(textValue).replace(
			u"{name}"_q,
			SandboxPluginName(pluginValue)));
	});
	return Py_BuildValue("");
}

[[nodiscard]] bool SandboxRequestAllowed(const QVector<mtpPrime> &primes) {
	if (primes.isEmpty()) {
		return false;
	}
	static const auto denied = base::flat_set<mtpTypeId>{
		mtpTypeId(mtpc_auth_acceptLoginToken),
		mtpTypeId(mtpc_auth_exportLoginToken),
		mtpTypeId(mtpc_auth_importLoginToken),
		mtpTypeId(mtpc_auth_exportAuthorization),
		mtpTypeId(mtpc_auth_importAuthorization),
		mtpTypeId(mtpc_auth_importBotAuthorization),
		mtpTypeId(mtpc_auth_importWebTokenAuthorization),
		mtpTypeId(mtpc_auth_logOut),
		mtpTypeId(mtpc_auth_resetAuthorizations),
		mtpTypeId(mtpc_auth_sendCode),
		mtpTypeId(mtpc_auth_resendCode),
		mtpTypeId(mtpc_auth_cancelCode),
		mtpTypeId(mtpc_auth_signIn),
		mtpTypeId(mtpc_auth_signUp),
		mtpTypeId(mtpc_auth_checkPassword),
		mtpTypeId(mtpc_auth_bindTempAuthKey),
		mtpTypeId(mtpc_auth_dropTempAuthKeys),
		mtpTypeId(mtpc_auth_recoverPassword),
		mtpTypeId(mtpc_auth_requestPasswordRecovery),
		mtpTypeId(mtpc_auth_requestFirebaseSms),
		mtpTypeId(mtpc_auth_resetLoginEmail),
		mtpTypeId(mtpc_account_resetAuthorization),
		mtpTypeId(mtpc_account_getAuthorizations),
		mtpTypeId(mtpc_account_setAuthorizationTTL),
		mtpTypeId(mtpc_account_changeAuthorizationSettings),
		mtpTypeId(mtpc_account_getWebAuthorizations),
		mtpTypeId(mtpc_account_resetWebAuthorization),
		mtpTypeId(mtpc_account_resetWebAuthorizations),
		mtpTypeId(mtpc_account_deleteAccount),
		mtpTypeId(mtpc_account_getPassword),
		mtpTypeId(mtpc_account_getPasswordSettings),
		mtpTypeId(mtpc_account_updatePasswordSettings),
		mtpTypeId(mtpc_account_confirmPasswordEmail),
		mtpTypeId(mtpc_account_resendPasswordEmail),
		mtpTypeId(mtpc_account_cancelPasswordEmail),
		mtpTypeId(mtpc_account_resetPassword),
		mtpTypeId(mtpc_account_declinePasswordReset),
		mtpTypeId(mtpc_account_getTmpPassword),
		mtpTypeId(mtpc_account_changePhone),
		mtpTypeId(mtpc_account_sendChangePhoneCode),
		mtpTypeId(mtpc_account_sendConfirmPhoneCode),
		mtpTypeId(mtpc_account_confirmPhone),
		mtpTypeId(mtpc_account_sendVerifyEmailCode),
		mtpTypeId(mtpc_account_verifyEmail),
		mtpTypeId(mtpc_account_updateDeviceLocked),
		mtpTypeId(mtpc_account_initTakeoutSession),
		mtpTypeId(mtpc_account_finishTakeoutSession),
		mtpTypeId(mtpc_account_getAuthorizationForm),
		mtpTypeId(mtpc_account_acceptAuthorization),
		mtpTypeId(mtpc_account_getAllSecureValues),
		mtpTypeId(mtpc_account_getSecureValue),
		mtpTypeId(mtpc_account_saveSecureValue),
		mtpTypeId(mtpc_account_deleteSecureValue),
		mtpTypeId(mtpc_messages_requestUrlAuth),
		mtpTypeId(mtpc_messages_acceptUrlAuth),
		mtpTypeId(mtpc_messages_sendPaidReaction),
		mtpTypeId(mtpc_payments_sendStarsForm),
		mtpTypeId(mtpc_payments_sendPaymentForm),
		mtpTypeId(mtpc_payments_transferStarGift),
		mtpTypeId(mtpc_payments_convertStarGift),
		mtpTypeId(mtpc_payments_upgradeStarGift),
		mtpTypeId(mtpc_payments_saveStarGift),
		mtpTypeId(mtpc_payments_updateStarGiftPrice),
		mtpTypeId(mtpc_payments_getStarGiftWithdrawalUrl),
		mtpTypeId(mtpc_payments_getStarsRevenueWithdrawalUrl),
		mtpTypeId(mtpc_payments_sendStarGiftOffer),
		mtpTypeId(mtpc_payments_resolveStarGiftOffer),
		mtpTypeId(mtpc_payments_craftStarGift),
		mtpTypeId(mtpc_bots_exportBotToken),
		mtpTypeId(mtpc_initConnection),
		mtpTypeId(mtpc_invokeWithLayer),
		mtpTypeId(mtpc_invokeAfterMsg),
		mtpTypeId(mtpc_invokeAfterMsgs),
		mtpTypeId(mtpc_invokeWithoutUpdates),
		mtpTypeId(mtpc_invokeWithTakeout),
		mtpTypeId(mtpc_invokeWithMessagesRange),
		mtpTypeId(mtpc_invokeWithBusinessConnection),
		mtpTypeId(mtpc_invokeWithGooglePlayIntegrity),
		mtpTypeId(mtpc_invokeWithApnsSecret),
		mtpTypeId(mtpc_invokeWithReCaptcha),
	};
	return !denied.contains(mtpTypeId(primes[0]));
}

PyObject *NativeOpenUrl(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] {
		Instance().urlFromPlugin = true;
		HiddenUrlClickHandler::Open(value);
		Instance().urlFromPlugin = false;
	});
	return Py_BuildValue("");
}

PyObject *NativePickFile(PyObject *self, PyObject *args) {
	const char *token = nullptr;
	const char *title = nullptr;
	const char *filter = nullptr;
	int multiple = 0;
	if (!PyArg_ParseTuple(args, "sssi", &token, &title, &filter, &multiple)) {
		return nullptr;
	}
	const auto tokenValue = QString::fromUtf8(token);
	const auto titleValue = QString::fromUtf8(title);
	const auto filterValue = QString::fromUtf8(filter);
	const auto owner = SandboxOwnerNow();
	crl::on_main([=] {
		const auto finish = [=](QStringList paths) {
			auto list = QJsonArray();
			for (const auto &path : paths) {
				list.push_back(path);
			}
			if (!owner.isEmpty() && owner != u"<unknown>"_q && !paths.isEmpty()) {
				SandboxQuery(u"grant"_q, owner, QString(), QString(), paths);
			}
			Call(u"file_picked"_q, QJsonObject{
				{ u"token"_q, tokenValue },
				{ u"paths"_q, list },
			});
		};
		const auto callback = [=](FileDialog::OpenResult &&result) {
			finish(result.paths);
		};
		const auto fail = [=] {
			finish({});
		};
		const auto caption = titleValue.isEmpty()
			? tr::lng_choose_file(tr::now)
			: titleValue;
		const auto filters = filterValue.isEmpty()
			? FileDialog::AllFilesFilter()
			: filterValue;
		if (multiple) {
			FileDialog::GetOpenPaths(
				Core::App().getFileDialogParent(),
				caption,
				filters,
				callback,
				fail);
		} else {
			FileDialog::GetOpenPath(
				Core::App().getFileDialogParent(),
				caption,
				filters,
				callback,
				fail);
		}
	});
	return Py_BuildValue("");
}

PyObject *NativeCopy(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] { QGuiApplication::clipboard()->setText(value); });
	return Py_BuildValue("");
}

PyObject *NativeSendMessage(PyObject *self, PyObject *args) {
	long long dialog = 0;
	const char *text = nullptr;
	const char *entities = nullptr;
	long long reply = 0;
	if (!PyArg_ParseTuple(args, "LssL", &dialog, &text, &entities, &reply)) {
		return nullptr;
	}
	const auto textValue = QString::fromUtf8(text);
	const auto entitiesValue = QString::fromUtf8(entities);
	crl::on_main([=] {
		SendFromPlugin(dialog, textValue, entitiesValue, reply);
	});
	return Py_BuildValue("");
}

PyObject *NativeRunOnMain(PyObject *self, PyObject *args) {
	PyObject *callable = nullptr;
	int delay = 0;
	if (!PyArg_ParseTuple(args, "O|i", &callable, &delay)) {
		return nullptr;
	}
	if (!PyCallable_Check(callable)) {
		return Py_BuildValue("");
	}
	Py_INCREF(callable);
	const auto owner = SandboxOwnerNow();
	const auto run = [=] {
		auto gil = Gil();
		const auto token = SandboxToken();
		if (!owner.isEmpty()) {
			SandboxPushOwner(owner, token);
		}
		const auto result = PyObject_CallNoArgs(callable);
		if (!owner.isEmpty()) {
			SandboxPopOwner(token);
		}
		if (!result) {
			PyErr_Print();
		}
		Py_XDECREF(result);
		Py_DECREF(callable);
	};
	if (delay > 0) {
		crl::on_main([=] { base::call_delayed(delay, run); });
	} else {
		crl::on_main(run);
	}
	return Py_BuildValue("");
}

PyObject *NativeShowAlert(PyObject *self, PyObject *args) {
	const char *payload = nullptr;
	if (!PyArg_ParseTuple(args, "s", &payload)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(payload);
	crl::on_main([=] { ShowAlert(value); });
	return Py_BuildValue("");
}

PyObject *NativeSettingsChanged(PyObject *self, PyObject *args) {
	const char *id = nullptr;
	if (!PyArg_ParseTuple(args, "s", &id)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(id);
	crl::on_main([=] { Instance().settingsChanges.fire_copy(value); });
	return Py_BuildValue("");
}

PyObject *NativeOpenPluginSettings(PyObject *self, PyObject *args) {
	const char *id = nullptr;
	if (!PyArg_ParseTuple(args, "s", &id)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(id);
	crl::on_main([=] {
		if (const auto controller = ActiveController()) {
			ShowPluginSettings(controller, value);
		}
	});
	return Py_BuildValue("");
}

PyObject *NativePluginsChanged(PyObject *self, PyObject *args) {
	crl::on_main([] { notifyListChanged(); });
	return Py_BuildValue("");
}

PyObject *NativeCurrentPeer(PyObject *self, PyObject *args) {
	if (const auto controller = ActiveController()) {
		if (const auto peer = controller->activeChatCurrent().peer()) {
			return PyLong_FromLongLong(DialogId(peer));
		}
	}
	return PyLong_FromLongLong(CurrentDialog.load());
}

PyObject *NativeSelfId(PyObject *self, PyObject *args) {
	if (const auto controller = ActiveController()) {
		return PyLong_FromLongLong(
			int64(controller->session().userId().bare));
	}
	return PyLong_FromLongLong(0);
}

PyObject *NativeGetSelectedAccount(PyObject *self, PyObject *args) {
	return PyLong_FromLongLong(0);
}

PyObject *NativePeerInfo(PyObject *self, PyObject *args) {
	long long dialog = 0;
	if (!PyArg_ParseTuple(args, "L", &dialog)) {
		return nullptr;
	}
	const auto controller = ActiveController();
	const auto peer = controller ? ResolveDialog(&controller->session(), dialog) : nullptr;
	if (!peer) {
		return Py_BuildValue("s", "{}");
	}
	auto info = QJsonObject{
		{ u"id"_q, double(dialog) },
		{ u"dialog_id"_q, double(dialog) },
		{ u"type"_q, peer->isUser()
			? u"user"_q
			: peer->isChat() ? u"chat"_q : u"channel"_q },
		{ u"name"_q, peer->name() },
		{ u"username"_q, peer->username() },
	};
	if (const auto user = peer->asUser()) {
		info.insert(u"phone"_q, user->phone());
		info.insert(u"bot"_q, user->isBot());
		info.insert(u"bare_id"_q, QString::number(peerToUser(peer->id).bare));
		info.insert(u"access_hash"_q, QString::number(user->accessHash()));
		info.insert(u"self"_q, user->isSelf());
	} else if (const auto channel = peer->asChannel()) {
		info.insert(u"bare_id"_q, QString::number(peerToChannel(peer->id).bare));
		info.insert(u"access_hash"_q, QString::number(channel->accessHash()));
		info.insert(u"megagroup"_q, channel->isMegagroup());
	} else if (const auto chat = peer->asChat()) {
		info.insert(u"bare_id"_q, QString::number(peerToChat(peer->id).bare));
	}
	return Py_BuildValue("s",
		QJsonDocument(info).toJson(QJsonDocument::Compact).constData());
}

PyObject *NativeGetHistory(PyObject *self, PyObject *args) {
	long long dialog = 0;
	int limit = 20;
	if (!PyArg_ParseTuple(args, "L|i", &dialog, &limit)) {
		return nullptr;
	}
	limit = std::clamp(limit, 1, 500);
	auto &state = Instance();
	auto array = QJsonArray();
	for (auto it = state.recent.rbegin()
		; it != state.recent.rend() && array.size() < limit
		; ++it) {
		if (int64(it->value(u"dialog_id"_q).toDouble()) == dialog) {
			array.append(*it);
		}
	}
	return Py_BuildValue("s",
		QJsonDocument(array).toJson(QJsonDocument::Compact).constData());
}

PyObject *NativeSendMedia(PyObject *self, PyObject *args) {
	long long dialog = 0;
	const char *kind = nullptr;
	const char *path = nullptr;
	const char *caption = nullptr;
	const char *entities = nullptr;
	long long reply = 0;
	int highQuality = 0;
	if (!PyArg_ParseTuple(args, "LssssLp",
			&dialog, &kind, &path, &caption, &entities, &reply, &highQuality)) {
		return nullptr;
	}
	const auto kindValue = QString::fromUtf8(kind);
	const auto pathValue = QString::fromUtf8(path);
	const auto captionValue = QString::fromUtf8(caption);
	const auto entitiesValue = QString::fromUtf8(entities);
	crl::on_main([=] {
		const auto controller = ActiveController();
		if (!controller) {
			return;
		}
		const auto session = &controller->session();
		const auto peer = ResolveDialog(session, dialog);
		if (!peer) {
			ShowToast(tr::lng_extera_plugins_no_chat(tr::now));
			return;
		}
		auto file = Ui::PreparedFile(pathValue);
		file.caption = TextWithTags{
			captionValue,
			TextUtilities::ConvertEntitiesToTextTags(ParseEntities(entitiesValue)),
		};
		crl::async([=, file = std::move(file)]() mutable {
			Storage::PrepareDetails(file, 320, PhotoSideLimit());
			crl::on_main([=, file = std::move(file)]() mutable {
				const auto controller = ActiveController();
				if (!controller) {
					return;
				}
				const auto session = &controller->session();
				const auto peer = ResolveDialog(session, dialog);
				if (!peer) {
					return;
				}
				auto list = Ui::PreparedList();
				list.files.push_back(std::move(file));
				auto action = Api::SendAction(session->data().history(peer));
				if (reply > 0) {
					action.replyTo.messageId = FullMsgId(peer->id, MsgId(reply));
				}
				const auto photo = kindValue == u"photo"_q && !highQuality;
				BypassFlag = true;
				session->api().sendFiles(
					std::move(list),
					photo ? SendMediaType::Photo : SendMediaType::File,
					nullptr,
					action);
				BypassFlag = false;
			});
		});
	});
	return Py_BuildValue("");
}

PyObject *NativeEditMessage(PyObject *self, PyObject *args) {
	long long dialog = 0;
	long long msgId = 0;
	const char *text = nullptr;
	const char *entities = nullptr;
	if (!PyArg_ParseTuple(args, "LLss", &dialog, &msgId, &text, &entities)) {
		return nullptr;
	}
	const auto textValue = QString::fromUtf8(text);
	const auto entitiesValue = QString::fromUtf8(entities);
	crl::on_main([=] {
		const auto controller = ActiveController();
		if (!controller) {
			return;
		}
		const auto session = &controller->session();
		const auto peer = ResolveDialog(session, dialog);
		if (!peer) {
			return;
		}
		const auto item = session->data().message(FullMsgId(peer->id, MsgId(msgId)));
		if (!item) {
			ShowToast(u"Message not found"_q);
			return;
		}
		Api::EditTextMessage(
			item,
			TextWithEntities{ textValue, ParseEntities(entitiesValue) },
			{},
			{},
			nullptr,
			nullptr,
			false);
	});
	return Py_BuildValue("");
}

class RawRequestBare {
public:
	RawRequestBare() = default;
	explicit RawRequestBare(QVector<mtpPrime> data) : _data(std::move(data)) {
	}

	[[nodiscard]] uint32 type() const {
		return _data.isEmpty() ? 0 : uint32(_data[0]);
	}

	template <typename Accumulator>
	void write(Accumulator &to) const {
		for (auto i = 1; i < _data.size(); ++i) {
			tl::Writer<Accumulator>::Put(to, uint32(_data[i]));
		}
	}

	template <typename Prime>
	[[nodiscard]] bool read(
			const Prime *&from,
			const Prime *end,
			uint32 cons = 0) {
		_data.clear();
		_data.push_back(mtpPrime(cons));
		while (from < end) {
			_data.push_back(mtpPrime(*from++));
		}
		return true;
	}

	[[nodiscard]] QByteArray bytes() const {
		return QByteArray(
			reinterpret_cast<const char*>(_data.constData()),
			_data.size() * sizeof(mtpPrime));
	}

	using ResponseType = tl::boxed<RawRequestBare>;

private:
	QVector<mtpPrime> _data;

};

using RawRequest = tl::boxed<RawRequestBare>;

PyObject *NativeSendRawRequest(PyObject *self, PyObject *args) {
	const char *encoded = nullptr;
	const char *callbackId = nullptr;
	if (!PyArg_ParseTuple(args, "ss", &encoded, &callbackId)) {
		return nullptr;
	}
	const auto bytes = QByteArray::fromBase64(QByteArray(encoded));
	const auto callback = QString::fromUtf8(callbackId);
	const auto controller = ActiveController();
	if (!controller || bytes.size() < 4 || (bytes.size() % 4) != 0) {
		return PyLong_FromLongLong(0);
	}
	auto primes = QVector<mtpPrime>(bytes.size() / 4);
	memcpy(primes.data(), bytes.constData(), bytes.size());
	if (!SandboxRequestAllowed(primes)) {
		LOG(("Plugins: blocked request %1").arg(uint32(primes[0]), 8, 16, QChar('0')));
		crl::on_main([=] {
			Call(u"request_callback"_q, QJsonObject{
				{ u"id"_q, callback },
				{ u"error"_q, u"REQUEST_BLOCKED_BY_EXDESKTOP"_q },
				{ u"code"_q, 403 },
			});
		});
		return PyLong_FromLongLong(0);
	}
	const auto session = &controller->session();
	const auto id = session->api().request(
		RawRequest(RawRequestBare(std::move(primes)))
	).done([=](const RawRequest &result) {
		Call(u"request_callback"_q, QJsonObject{
			{ u"id"_q, callback },
			{ u"raw"_q, QString::fromLatin1(result.bytes().toBase64()) },
		});
	}).fail([=](const MTP::Error &error) {
		Call(u"request_callback"_q, QJsonObject{
			{ u"id"_q, callback },
			{ u"error"_q, error.type() },
			{ u"code"_q, error.code() },
		});
	}).send();
	return PyLong_FromLongLong(id);
}

PyObject *NativeSendRequest(PyObject *self, PyObject *args) {
	const char *requestJson = nullptr;
	const char *callbackId = nullptr;
	if (!PyArg_ParseTuple(args, "ss", &requestJson, &callbackId)) {
		return nullptr;
	}
	const auto payload = QJsonDocument::fromJson(
		QString::fromUtf8(requestJson).toUtf8()).object();
	const auto name = payload.value(u"request"_q).toString();
	const auto params = payload.value(u"params"_q).toObject();
	const auto callback = QString::fromUtf8(callbackId);
	const auto finish = [=](const QJsonObject &response, const QString &error) {
		Call(u"request_callback"_q, QJsonObject{
			{ u"id"_q, callback },
			{ u"response"_q, response },
			{ u"error"_q, error },
		});
	};
	const auto controller = ActiveController();
	if (!controller || name.isEmpty()) {
		return PyLong_FromLongLong(0);
	}
	const auto session = &controller->session();
	if (name == u"messages_setTyping"_q || name == u"setTyping"_q) {
		const auto peer = ResolveDialog(
			session,
			int64(params.value(u"peer"_q).toDouble()));
		if (!peer) {
			finish({}, u"peer not found"_q);
			return PyLong_FromLongLong(0);
		}
		const auto id = session->api().request(MTPmessages_SetTyping(
			MTP_flags(MTPmessages_SetTyping::Flag(0)),
			peer->input(),
			MTP_int(0),
			MTP_sendMessageTypingAction()
		)).done([=](const MTPBool &result) {
			finish({}, QString());
		}).fail([=](const MTP::Error &error) {
			finish({}, error.type());
		}).send();
		return PyLong_FromLongLong(id);
	}
	if (name == u"account_updateStatus"_q || name == u"updateStatus"_q) {
		const auto offline = params.value(u"offline"_q).toBool();
		const auto id = session->api().request(MTPaccount_UpdateStatus(
			MTP_bool(offline)
		)).done([=](const MTPBool &result) {
			finish({}, QString());
		}).fail([=](const MTP::Error &error) {
			finish({}, error.type());
		}).send();
		return PyLong_FromLongLong(id);
	}
	LOG(("Plugins: send_request: unknown request %1").arg(name));
	return PyLong_FromLongLong(0);
}

PyObject *NativeViewsShowSheet(PyObject *self, PyObject *args) {
	const char *payload = nullptr;
	if (!PyArg_ParseTuple(args, "s", &payload)) {
		return nullptr;
	}
	Extera::Views::ShowSheet(QString::fromUtf8(payload));
	return Py_BuildValue("");
}

PyObject *NativeViewsUpdate(PyObject *self, PyObject *args) {
	const char *payload = nullptr;
	if (!PyArg_ParseTuple(args, "s", &payload)) {
		return nullptr;
	}
	Extera::Views::Update(QString::fromUtf8(payload));
	return Py_BuildValue("");
}

PyObject *NativeViewsDismissSheet(PyObject *self, PyObject *args) {
	long long sheetId = 0;
	if (!PyArg_ParseTuple(args, "L", &sheetId)) {
		return nullptr;
	}
	Extera::Views::DismissSheet(sheetId);
	return Py_BuildValue("");
}

PyObject *NativeViewsRenderCanvas(PyObject *self, PyObject *args) {
	long long canvasId = 0;
	int width = 0;
	int height = 0;
	const char *ops = nullptr;
	if (!PyArg_ParseTuple(args, "Liis", &canvasId, &width, &height, &ops)) {
		return nullptr;
	}
	Extera::Views::RenderCanvas(
		canvasId, width, height, QString::fromUtf8(ops));
	return Py_BuildValue("");
}

PyObject *NativeMeasureText(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	int size = 16;
	int bold = 0;
	if (!PyArg_ParseTuple(args, "sii", &text, &size, &bold)) {
		return nullptr;
	}
	return PyFloat_FromDouble(Extera::Views::MeasureText(
		QString::fromUtf8(text), size, bool(bold)));
}

PyObject *NativeUiMetrics(PyObject *self, PyObject *args) {
	return Py_BuildValue("s", Extera::Views::UiMetrics().toUtf8().constData());
}

PyObject *NativeThemeColor(PyObject *self, PyObject *args) {
	const char *name = nullptr;
	if (!PyArg_ParseTuple(args, "s", &name)) {
		return nullptr;
	}
	return PyLong_FromLongLong(Extera::Views::ThemeColor(QString::fromUtf8(name)));
}

PyMethodDef NativeMethods[] = {
	{ "log", NativeLog, METH_VARARGS, nullptr },
	{ "toast", NativeToast, METH_VARARGS, nullptr },
	{ "open_url", NativeOpenUrl, METH_VARARGS, nullptr },
	{ "copy", NativeCopy, METH_VARARGS, nullptr },
	{ "pick_file", NativePickFile, METH_VARARGS, nullptr },
	{ "send_message", NativeSendMessage, METH_VARARGS, nullptr },
	{ "run_on_main", NativeRunOnMain, METH_VARARGS, nullptr },
	{ "show_alert", NativeShowAlert, METH_VARARGS, nullptr },
	{ "settings_changed", NativeSettingsChanged, METH_VARARGS, nullptr },
	{ "plugins_changed", NativePluginsChanged, METH_VARARGS, nullptr },
	{ "open_plugin_settings", NativeOpenPluginSettings, METH_VARARGS, nullptr },
	{ "current_peer", NativeCurrentPeer, METH_VARARGS, nullptr },
	{ "get_selected_account", NativeGetSelectedAccount, METH_VARARGS, nullptr },
	{ "self_id", NativeSelfId, METH_VARARGS, nullptr },
	{ "peer_info", NativePeerInfo, METH_VARARGS, nullptr },
	{ "get_history", NativeGetHistory, METH_VARARGS, nullptr },
	{ "send_media", NativeSendMedia, METH_VARARGS, nullptr },
	{ "edit_message", NativeEditMessage, METH_VARARGS, nullptr },
	{ "send_request", NativeSendRequest, METH_VARARGS, nullptr },
	{ "send_raw_request", NativeSendRawRequest, METH_VARARGS, nullptr },
	{ "views_show_sheet", NativeViewsShowSheet, METH_VARARGS, nullptr },
	{ "views_update", NativeViewsUpdate, METH_VARARGS, nullptr },
	{ "views_dismiss_sheet", NativeViewsDismissSheet, METH_VARARGS, nullptr },
	{ "views_render_canvas", NativeViewsRenderCanvas, METH_VARARGS, nullptr },
	{ "measure_text", NativeMeasureText, METH_VARARGS, nullptr },
	{ "ui_metrics", NativeUiMetrics, METH_VARARGS, nullptr },
	{ "theme_color", NativeThemeColor, METH_VARARGS, nullptr },
	{ "sbx_install", NativeSbxInstall, METH_VARARGS, nullptr },
	{ "sbx_state", NativeSbxState, METH_NOARGS, nullptr },
	{ "sbx_push", NativeSbxPush, METH_VARARGS, nullptr },
	{ "sbx_pop", NativeSbxPop, METH_VARARGS, nullptr },
	{ "sbx_owner", NativeSbxOwner, METH_NOARGS, nullptr },
	{ "sbx_ticket", NativeSbxTicket, METH_NOARGS, nullptr },
	{ "sbx_thread_entry", NativeSbxThreadEntry, METH_VARARGS, nullptr },
	{ "sbx_create_dynamic", NativeSbxCreateDynamic, METH_VARARGS, nullptr },
	{ "sbx_create_builtin", NativeSbxCreateBuiltin, METH_VARARGS, nullptr },
	{ "sbx_prompt", NativeSbxPrompt, METH_VARARGS, nullptr },
	{ "sbx_toast", NativeSbxToast, METH_VARARGS, nullptr },
	{ nullptr, nullptr, 0, nullptr },
};

PyModuleDef NativeModule = {
	PyModuleDef_HEAD_INIT,
	"_extera",
	nullptr,
	-1,
	NativeMethods,
};

PyObject *InitNativeModule() {
	return PyModule_Create(&NativeModule);
}

[[nodiscard]] QString PythonErrorText() {
	PyObject *type = nullptr;
	PyObject *value = nullptr;
	PyObject *trace = nullptr;
	PyErr_Fetch(&type, &value, &trace);
	if (!type) {
		return u"Unknown Python error"_q;
	}
	PyErr_NormalizeException(&type, &value, &trace);
	auto result = QString();
	if (const auto module = PyImport_ImportModule("traceback")) {
		if (const auto format = PyObject_GetAttrString(module, "format_exception")) {
			const auto none = Py_BuildValue("");
			const auto lines = PyObject_CallFunctionObjArgs(
				format,
				type,
				value ? value : none,
				trace ? trace : none,
				nullptr);
			Py_XDECREF(none);
			if (lines) {
				const auto count = PyList_Check(lines) ? PyList_Size(lines) : 0;
				for (auto i = Py_ssize_t(0); i != count; ++i) {
					if (const auto text = PyUnicode_AsUTF8(PyList_GetItem(lines, i))) {
						result += QString::fromUtf8(text);
					}
				}
				Py_DECREF(lines);
			}
			Py_DECREF(format);
		}
		Py_DECREF(module);
	}
	if (result.isEmpty() && value) {
		if (const auto text = PyObject_Str(value)) {
			if (const auto utf = PyUnicode_AsUTF8(text)) {
				result = QString::fromUtf8(utf);
			}
			Py_DECREF(text);
		}
	}
	PyErr_Clear();
	Py_XDECREF(type);
	Py_XDECREF(value);
	Py_XDECREF(trace);
	return result.trimmed();
}

[[nodiscard]] bool InitInterpreter(const QString &runtime) {
	auto config = PyConfig();
	PyConfig_InitIsolatedConfig(&config);
	const auto finish = [&](PyStatus status) {
		if (PyStatus_Exception(status)) {
			Instance().error = QString::fromUtf8(status.err_msg
				? status.err_msg
				: "Python init failed");
			PyConfig_Clear(&config);
			return false;
		}
		return true;
	};
#ifdef Q_OS_WIN
	const auto home = QDir::toNativeSeparators(runtime).toStdWString();
	if (!finish(PyConfig_SetString(&config, &config.home, home.c_str()))) {
		return false;
	}
	config.module_search_paths_set = 1;
	const auto paths = QStringList{
		runtime + u"python311.zip"_q,
		runtime,
		runtime + u"Lib/site-packages"_q,
		runtime + u"extera"_q,
	};
	for (const auto &path : paths) {
		const auto value = QDir::toNativeSeparators(path).toStdWString();
		if (!finish(PyWideStringList_Append(
				&config.module_search_paths,
				value.c_str()))) {
			return false;
		}
	}
#else
	{
		const auto executable = QFileInfo(cExeDir() + cExeName()).absoluteFilePath().toStdWString();
		if (!finish(PyConfig_SetString(
				&config,
				&config.executable,
				executable.c_str()))) {
			return false;
		}
	}
	for (const auto &candidate : {
			cExeDir() + u"../Resources/python311"_q,
			cExeDir() + u"python311"_q }) {
		if (QDir(candidate).exists(u"lib"_q)) {
			const auto home = QDir(candidate).absolutePath().toStdWString();
			if (!finish(PyConfig_SetString(
					&config,
					&config.home,
					home.c_str()))) {
				return false;
			}
			if (!QDir(candidate).exists(u"lib64"_q)
				&& !finish(PyConfig_SetString(
					&config,
					&config.platlibdir,
					L"lib"))) {
				return false;
			}
			break;
		}
	}
#endif
	config.write_bytecode = 0;
	config.install_signal_handlers = 0;
	if (!finish(Py_InitializeFromConfig(&config))) {
		return false;
	}
	PyConfig_Clear(&config);

#ifndef Q_OS_WIN
	// Add runtime and extera directories to sys.path so modules can be imported
	PyObject *sysPath = PySys_GetObject("path");
	if (sysPath && PyList_Check(sysPath)) {
		const auto extraPaths = QStringList{
			runtime + u"extera"_q,
			runtime,
			runtime + u"site-packages"_q,
			DataPath() + u"plugins"_q,
			QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/python/extera"_q,
			QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + u"/python"_q,
		};
		for (auto it = extraPaths.rbegin(); it != extraPaths.rend(); ++it) {
			const auto &path = *it;
			const auto uPath = QDir::toNativeSeparators(path).toUtf8();
			if (PyObject *pPath = PyUnicode_FromString(uPath.constData())) {
				PyList_Insert(sysPath, 0, pPath);
				Py_DECREF(pPath);
			}
		}
	}
#endif

	return true;
}

} // namespace

bool IsPluginFile(const QString &path) {
	for (const auto &extension : {
			u".plugin"_q,
			u".elyx"_q,
			u".eaf"_q,
			u".elyx.zip"_q,
			u".eaf.zip"_q }) {
		if (path.endsWith(extension, Qt::CaseInsensitive)) {
			return true;
		}
	}
	return false;
}

bool Available() {
#ifdef Q_OS_WIN
	return QFile::exists(RuntimePath() + u"python311.dll"_q)
		&& QFile::exists(RuntimePath() + u"extera/extera_host.py"_q);
#else // Q_OS_WIN
	return QFile::exists(RuntimePath() + u"extera/extera_host.py"_q);
#endif // Q_OS_WIN
}

bool Started() {
	return Instance().started;
}

QString RuntimeError() {
	return Instance().error;
}

void VerifyInstalledPlugins() {
	const auto controller = ActiveController();
	if (!controller) {
		return;
	}
	const auto session = &controller->session();
	for (const auto &entry : Call(u"list"_q, QJsonObject()).toArray()) {
		const auto info = entry.toObject();
		if (info.value(u"trusted"_q).toBool()) {
			continue;
		}
		const auto path = info.value(u"path"_q).toString();
		Extera::PluginTrust::Verify(session, path, [=](bool ok) {
			if (!ok) {
				return;
			}
			Call(u"trust"_q, QJsonObject{ { u"path"_q, path } });
			Call(u"unknown_sources"_q, QJsonObject{
				{ u"enabled"_q, Extera::Bool(u"plugins_unknown_sources"_q) },
			});
			Instance().listChanges.fire({});
		});
	}
}

void Start() {
	auto &state = Instance();
	if (state.started || !Extera::Bool(u"plugins_enabled"_q)) {
		return;
	}
	if (!Available()) {
		state.error = tr::lng_extera_plugins_runtime_missing(tr::now);
		return;
	}
#ifdef Q_OS_WIN
	const auto dll = QDir::toNativeSeparators(
		RuntimePath() + u"python311.dll"_q).toStdWString();
	if (!LoadLibraryExW(
			dll.c_str(),
			nullptr,
			LOAD_WITH_ALTERED_SEARCH_PATH)) {
		const auto code = GetLastError();
		state.error = tr::lng_exd_plugins_dll_failed(
			tr::now,
			lt_code,
			QString::number(code));
		return;
	}
#endif // Q_OS_WIN
	if (PyImport_AppendInittab("_extera", InitNativeModule) == -1) {
		state.error = u"Native module registration failed"_q;
		return;
	}
	LOG(("Plugins: runtime %1").arg(RuntimePath()));
	if (!InitInterpreter(RuntimePath())) {
		LOG(("Plugins Error: %1").arg(state.error));
		return;
	}
	const auto module = PyImport_ImportModule("extera_host");
	if (!module) {
		const auto details = PythonErrorText();
		LOG(("Plugins Error: extera_host import failed: %1").arg(details));
		state.error = u"extera_host import failed: "_q
			+ details.split('\n').last();
		state.mainThread = PyEval_SaveThread();
		return;
	}
	state.dispatch = PyObject_GetAttrString(module, "dispatch");
	Py_DECREF(module);
	state.mainThread = PyEval_SaveThread();
	state.started = (state.dispatch != nullptr);
	if (!state.started) {
		return;
	}
	QDir().mkpath(DataPath() + u"plugins"_q);
	if (QFile::exists(HangMarkerPath())) {
		QFile::remove(HangMarkerPath());
		EnterAutoSafeMode(true);
	}
	if (QFile::exists(StartMarkerPath())
		&& !Extera::Bool(u"plugins_safe_mode"_q)
		&& !Extera::Bool(u"plugins_no_auto_safe"_q)) {
		Extera::Config::Instance().setBool(u"plugins_safe_mode"_q, true);
		Extera::Config::Instance().saveNow();
		ShowAutoSafeModeBox();
	}
	{
		auto marker = QFile(StartMarkerPath());
		if (marker.open(QIODevice::WriteOnly)) {
			marker.write("1");
		}
	}
	base::call_delayed(30000, [] {
		QFile::remove(StartMarkerPath());
	});
	Call(u"init"_q, QJsonObject{
		{ u"plugins"_q, DataPath() + u"plugins"_q },
		{ u"settings"_q, DataPath() + u"settings"_q },
		{ u"safe_mode"_q, Extera::Bool(u"plugins_safe_mode"_q) },
		{ u"unknown_sources"_q, Extera::Bool(u"plugins_unknown_sources"_q) },
		{ u"lang"_q, Lang::Id() },
	});
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"app"_q },
		{ u"name"_q, u"app_start"_q },
	});
	Extera::Config::Instance().boolChanges(
		u"plugins_safe_mode"_q
	) | rpl::on_next([](bool enabled) {
		Call(u"safe_mode"_q, QJsonObject{ { u"enabled"_q, enabled } });
		Instance().listChanges.fire({});
	}, state.lifetime);
	Extera::Config::Instance().boolChanges(
		u"plugins_unknown_sources"_q
	) | rpl::on_next([](bool enabled) {
		Call(u"unknown_sources"_q, QJsonObject{ { u"enabled"_q, enabled } });
		Instance().listChanges.fire({});
	}, state.lifetime);
	StartWatchdog();
	base::call_delayed(15000, [] {
		VerifyInstalledPlugins();
	});
	state.listChanges.fire({});
}

void Shutdown() {
	auto &state = Instance();
	if (!state.started) {
		return;
	}
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"app"_q },
		{ u"name"_q, u"app_stop"_q },
	});
	Call(u"shutdown"_q);
	WatchdogRunning = false;
	state.started = false;
	QFile::remove(StartMarkerPath());
}

QJsonValue Call(const QString &method, const QJsonObject &args) {
	auto &state = Instance();
	if (!state.started) {
		return QJsonValue();
	}
	const auto payload = QJsonDocument(args).toJson(QJsonDocument::Compact);
	auto response = QString();
	const auto outer = (CallStarted.load() == 0);
	if (outer) {
		CallStarted = crl::now();
	}
	{
		auto gil = Gil();
		const auto result = PyObject_CallFunction(
			state.dispatch,
			"ss",
			method.toUtf8().constData(),
			payload.constData());
		if (result) {
			response = FromPy(result);
			Py_DECREF(result);
		} else {
			PyErr_Print();
		}
	}
	if (outer) {
		CallStarted = 0;
		if (HangReported.exchange(false)) {
			QFile::remove(HangMarkerPath());
			LOG(("Plugins Error: %1 blocked the interface").arg(method));
			EnterAutoSafeMode(true);
		}
	}
	if (response.isEmpty()) {
		return QJsonValue();
	}
	const auto object = QJsonDocument::fromJson(response.toUtf8()).object();
	if (!object.value(u"ok"_q).toBool()) {
		LOG(("Plugins Error: %1 failed: %2"
			).arg(method
			).arg(object.value(u"error"_q).toString()));
		return QJsonValue();
	}
	return object.value(u"result"_q);
}

QJsonValue SandboxQuery(
		const QString &method,
		const QString &first,
		const QString &second,
		const QString &third,
		const QStringList &paths) {
	auto &state = Instance();
	if (!state.started || !SandboxQueryFn) {
		return QJsonValue();
	}
	auto response = QByteArray();
	{
		auto gil = Gil();
		++SandboxDepth;
		const auto list = PyList_New(0);
		for (const auto &path : paths) {
			const auto item = PyUnicode_FromString(path.toUtf8().constData());
			PyList_Append(list, item);
			Py_XDECREF(item);
		}
		const auto result = PyObject_CallFunction(
			SandboxQueryFn,
			"ssssO",
			method.toUtf8().constData(),
			first.toUtf8().constData(),
			second.toUtf8().constData(),
			third.toUtf8().constData(),
			list);
		Py_XDECREF(list);
		if (result && PyUnicode_Check(result)) {
			if (const auto utf = PyUnicode_AsUTF8(result)) {
				response = QByteArray(utf);
			}
		}
		Py_XDECREF(result);
		PyErr_Clear();
		--SandboxDepth;
	}
	const auto document = QJsonDocument::fromJson(response);
	if (document.isObject()) {
		return document.object();
	} else if (document.isArray()) {
		return document.array();
	}
	return QJsonValue(response == "true");
}

bool Bypassing() {
	return BypassFlag;
}

int64 DialogId(not_null<PeerData*> peer) {
	if (const auto user = peer->asUser()) {
		return int64(peerToUser(user->id).bare);
	} else if (const auto chat = peer->asChat()) {
		return -int64(peerToChat(chat->id).bare);
	} else if (const auto channel = peer->asChannel()) {
		return -int64(peerToChannel(channel->id).bare);
	}
	return 0;
}

bool IsTrustedSource(not_null<PeerData*> peer) {
	const auto channel = peer->asChannel();
	if (!channel) {
		return false;
	}
	const auto bare = uint64(peerToChannel(channel->id).bare);
	return ranges::contains(Extera::PluginTrust::TrustedChannels(), bare);
}

SendResult OnSendMessage(
		not_null<History*> history,
		const QString &text,
		MsgId replyTo) {
	if (!Instance().started || BypassFlag) {
		return {};
	}
	const auto dialog = DialogId(history->peer);
	CurrentDialog = dialog;
	const auto result = Call(u"send_hook"_q, QJsonObject{
		{ u"peer"_q, double(dialog) },
		{ u"text"_q, text },
		{ u"reply_to"_q, double(replyTo.bare) },
	}).toObject();
	const auto strategy = result.value(u"strategy"_q).toString();
	if (strategy == u"cancel"_q) {
		return { SendStrategy::Cancel };
	} else if (strategy == u"modify"_q) {
		return {
			SendStrategy::Modify,
			result.value(u"text"_q).toString(),
			EntitiesFromJsonArray(result.value(u"entities"_q).toArray()),
			result.value(u"quote"_q).toBool(),
		};
	}
	return {};
}

std::vector<MenuItem> MenuItems(const QString &type) {
	auto result = std::vector<MenuItem>();
	const auto list = Call(u"menu_list"_q, QJsonObject{
		{ u"type"_q, type },
	}).toArray();
	for (const auto &value : list) {
		const auto entry = value.toObject();
		result.push_back({
			entry.value(u"id"_q).toString(),
			entry.value(u"text"_q).toString(),
		});
	}
	return result;
}

void ClickMenuItem(const QString &id, QJsonObject context) {
	if (context.contains(u"peer"_q)) {
		CurrentDialog = int64(context.value(u"peer"_q).toDouble());
	}
	Call(u"menu_click"_q, QJsonObject{
		{ u"id"_q, id },
		{ u"context"_q, context },
	});
}

rpl::producer<> listChanges() {
	return Instance().listChanges.events();
}

rpl::producer<QString> settingsChanges() {
	return Instance().settingsChanges.events();
}

void notifyListChanged() {
	crl::on_main([] {
		Instance().listChanges.fire({});
	});
}

int ExportAll(const QString &folder) {
	auto count = 0;
	const auto source = QDir(DataPath() + u"plugins"_q);
	for (const auto &info : source.entryInfoList(
			{ u"*.plugin"_q, u"*.py"_q, u"*.elyx"_q, u"*.eaf"_q },
			QDir::Files)) {
		const auto target = QDir(folder).filePath(info.fileName());
		QFile::remove(target);
		if (QFile::copy(info.absoluteFilePath(), target)) {
			++count;
		}
	}
	return count;
}

[[nodiscard]] QJsonObject MessageToJson(not_null<HistoryItem*> item) {
	const auto peer = item->history()->peer;
	const auto from = item->from();
	return QJsonObject{
		{ u"id"_q, double(item->id.bare) },
		{ u"dialog_id"_q, double(DialogId(peer)) },
		{ u"out"_q, item->out() },
		{ u"date"_q, double(item->date()) },
		{ u"sender_id"_q, double(from ? DialogId(from) : 0) },
		{ u"text"_q, item->originalText().text },
	};
}

void NotifyNewMessage(not_null<HistoryItem*> item) {
	auto &state = Instance();
	if (!state.started || state.eventInFlight) {
		return;
	}
	const auto json = MessageToJson(item);
	state.recent.push_back(json);
	while (state.recent.size() > 500) {
		state.recent.pop_front();
	}
	state.eventInFlight = true;
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"update"_q },
		{ u"name"_q, u"updateNewMessage"_q },
		{ u"account"_q, 0 },
		{ u"update"_q, json },
	});
	state.eventInFlight = false;
}

void NotifyScreen(const QString &screen, int64 dialogId) {
	auto &state = Instance();
	if (!state.started || state.eventInFlight) {
		return;
	}
	state.eventInFlight = true;
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"screen"_q },
		{ u"screen"_q, screen },
		{ u"dialog_id"_q, double(dialogId) },
	});
	state.eventInFlight = false;
}

void NotifyAppEvent(const QString &name) {
	auto &state = Instance();
	if (!state.started || state.eventInFlight) {
		return;
	}
	state.eventInFlight = true;
	Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"app"_q },
		{ u"name"_q, name },
	});
	state.eventInFlight = false;
}

bool InterceptUrl(const QString &url) {
	auto &state = Instance();
	if (!state.started || state.urlInFlight || state.urlFromPlugin) {
		return false;
	}
	state.urlInFlight = true;
	const auto handled = Call(u"open_url_hook"_q, QJsonObject{
		{ u"url"_q, url },
	}).toBool();
	state.urlInFlight = false;
	return handled;
}

bool InterceptLocalUrl(const QString &url) {
	auto &state = Instance();
	if (!state.started || state.intentInFlight) {
		return false;
	}
	const auto parsed = QUrl(url);
	auto query = QJsonObject();
	const auto items = QUrlQuery(parsed).queryItems();
	for (const auto &[key, value] : items) {
		query.insert(key, value);
	}
	state.intentInFlight = true;
	const auto blocked = Call(u"event"_q, QJsonObject{
		{ u"type"_q, u"intent"_q },
		{ u"before"_q, true },
		{ u"context"_q, QJsonObject{
			{ u"url"_q, url },
			{ u"scheme"_q, parsed.scheme() },
			{ u"host"_q, parsed.host() },
			{ u"path"_q, parsed.path() },
			{ u"query_args"_q, query },
		} },
	}).toBool();
	state.intentInFlight = false;
	return blocked;
}

} // namespace Extera::Plugins
