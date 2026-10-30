#include "extera/extera_plugins.h"

#pragma push_macro("slots")
#undef slots
#include <Python.h>
#pragma pop_macro("slots")

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extera/extera_config.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings.h"
#include "ui/basic_click_handlers.h"
#include "ui/boxes/confirm_box.h"
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

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>

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
};

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
	return cExeDir() + u"python/"_q;
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

void ShowAutoSafeModeBox() {
	base::call_delayed(3000, [] {
		const auto window = Core::App().activePrimaryWindow();
		const auto controller = window ? window->sessionController() : nullptr;
		if (!controller) {
			return;
		}
		controller->show(Ui::MakeConfirmBox({
			.text = tr::lng_extera_safe_mode_auto(),
			.confirmed = [=](Fn<void()> close) {
				close();
				UrlClickHandler::Open(u"tg://settings/extera/plugins"_q);
			},
			.confirmText = tr::lng_extera_safe_mode_open(),
			.title = tr::lng_extera_safe_mode_title(),
		}));
	});
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

[[nodiscard]] EntitiesInText ParseEntities(const QString &json) {
	auto result = EntitiesInText();
	const auto array = QJsonDocument::fromJson(json.toUtf8()).array();
	for (const auto &value : array) {
		const auto entry = value.toArray();
		if (entry.size() < 3) {
			continue;
		}
		const auto kind = entry[0].toString();
		const auto offset = entry[1].toInt();
		const auto length = entry[2].toInt();
		const auto data = (entry.size() > 3) ? entry[3].toString() : QString();
		const auto type = (kind == u"bold"_q)
			? EntityType::Bold
			: (kind == u"italic"_q)
			? EntityType::Italic
			: (kind == u"underline"_q)
			? EntityType::Underline
			: (kind == u"strike"_q)
			? EntityType::StrikeOut
			: (kind == u"code"_q)
			? EntityType::Code
			: (kind == u"pre"_q)
			? EntityType::Pre
			: (kind == u"blockquote"_q || kind == u"blockquote_collapsed"_q)
			? EntityType::Blockquote
			: (kind == u"spoiler"_q)
			? EntityType::Spoiler
			: (kind == u"url"_q)
			? EntityType::CustomUrl
			: EntityType::Invalid;
		if (type != EntityType::Invalid && length > 0) {
			result.push_back({ type, offset, length, data });
		}
	}
	return result;
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
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto answered = std::make_shared<bool>(false);
		const auto answer = [=](int which) {
			if (*answered) {
				return;
			}
			*answered = true;
			Call(u"alert_click"_q, QJsonObject{
				{ u"id"_q, id },
				{ u"which"_q, which },
			});
		};
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

PyObject *NativeOpenUrl(PyObject *self, PyObject *args) {
	const char *text = nullptr;
	if (!PyArg_ParseTuple(args, "s", &text)) {
		return nullptr;
	}
	const auto value = QString::fromUtf8(text);
	crl::on_main([=] { UrlClickHandler::Open(value); });
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
	const auto run = [=] {
		auto gil = Gil();
		const auto result = PyObject_CallNoArgs(callable);
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

PyObject *NativeCurrentPeer(PyObject *self, PyObject *args) {
	if (const auto controller = ActiveController()) {
		if (const auto peer = controller->activeChatCurrent().peer()) {
			return PyLong_FromLongLong(DialogId(peer));
		}
	}
	return PyLong_FromLongLong(CurrentDialog.load());
}

PyMethodDef NativeMethods[] = {
	{ "log", NativeLog, METH_VARARGS, nullptr },
	{ "toast", NativeToast, METH_VARARGS, nullptr },
	{ "open_url", NativeOpenUrl, METH_VARARGS, nullptr },
	{ "copy", NativeCopy, METH_VARARGS, nullptr },
	{ "send_message", NativeSendMessage, METH_VARARGS, nullptr },
	{ "run_on_main", NativeRunOnMain, METH_VARARGS, nullptr },
	{ "show_alert", NativeShowAlert, METH_VARARGS, nullptr },
	{ "settings_changed", NativeSettingsChanged, METH_VARARGS, nullptr },
	{ "current_peer", NativeCurrentPeer, METH_VARARGS, nullptr },
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

[[nodiscard]] bool InitInterpreter(const QString &runtime) {
	auto config = PyConfig();
#ifdef Q_OS_WIN
	PyConfig_InitIsolatedConfig(&config);
#else
	PyConfig_InitPythonConfig(&config);
#endif
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

void Start() {
	auto &state = Instance();
	if (state.started || !Extera::Bool(u"plugins_enabled"_q)) {
		return;
	}
	if (!Available()) {
		state.error = u"Python runtime not found"_q;
		return;
	}
#ifdef Q_OS_WIN
	const auto dll = QDir::toNativeSeparators(
		RuntimePath() + u"python311.dll"_q).toStdWString();
	if (!LoadLibraryW(dll.c_str())) {
		state.error = u"python311.dll could not be loaded"_q;
		return;
	}
#endif // Q_OS_WIN
	if (PyImport_AppendInittab("_extera", InitNativeModule) == -1) {
		state.error = u"Native module registration failed"_q;
		return;
	}
	if (!InitInterpreter(RuntimePath())) {
		LOG(("Plugins Error: %1").arg(state.error));
		return;
	}
	const auto module = PyImport_ImportModule("extera_host");
	if (!module) {
		PyErr_Print();
		state.error = u"extera_host import failed"_q;
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
	});
	state.listChanges.fire({});
}

void Shutdown() {
	auto &state = Instance();
	if (!state.started) {
		return;
	}
	Call(u"shutdown"_q);
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
	{
		auto gil = Gil();
		const auto result = PyObject_CallFunction(
			state.dispatch,
			"ss",
			method.toUtf8().constData(),
			payload.constData());
		if (!result) {
			PyErr_Print();
			return QJsonValue();
		}
		response = FromPy(result);
		Py_DECREF(result);
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
	static const auto trusted = std::array<uint64, 4>{
		2562664432ULL,
		2661845134ULL,
		4433892778ULL,
		4478798161ULL,
	};
	return ranges::contains(trusted, bare);
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
			{ u"*.plugin"_q, u"*.py"_q },
			QDir::Files)) {
		const auto target = QDir(folder).filePath(info.fileName());
		QFile::remove(target);
		if (QFile::copy(info.absoluteFilePath(), target)) {
			++count;
		}
	}
	return count;
}

} // namespace Extera::Plugins
