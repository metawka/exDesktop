---
name: exdesktop-plugins
description: Write new exDesktop plugins or port exteraGram (Android) plugins to exDesktop, the Telegram Desktop based client with the exteraGram Python plugin API. Use when the user asks to create, fix, port or review a .plugin file for exDesktop or exteraGram on desktop.
---

# exDesktop plugins

exDesktop runs exteraGram plugins (Python, SDK 1.4.x) on Windows, Linux and macOS. The plugin API is the same as on Android, but there is no Java, no Android UI and no Xposed. Some Android classes are emulated in Python and a fixed set of Android hooks is mapped to desktop events. Everything else must be rewritten with the plugin API.

Full documentation: https://exdesktop.xyz/docs/

## Task

1. List every feature of the plugin from its settings, menu items, hooks and texts.
2. For each feature decide: keep as is, rewrite with the replacement table, or drop.
3. Write the plugin with the minimal number of changes. Keep the original strings, settings keys and network hosts.
4. Check the result against the hard rules and the self check.
5. After the code, write a short REPORT with three sections: kept, changed, dropped.

## Plugin file

```python
from base_plugin import BasePlugin, HookResult, HookStrategy, MenuItemData, MenuItemType
from ui.settings import Header, Divider, Switch, Selector, Input, Text
from ui.bulletin import BulletinHelper
from client_utils import send_text, send_request, run_on_queue

__id__ = "example_exd"
__name__ = "Example"
__version__ = "1.0"
__author__ = "@author"
__description__ = "What the plugin does"
__icon__ = "exteraPlugins/1"
__desktop__ = True
__ported_from__ = "example 1.0"
__desktop_os__ = "windows"


class ExamplePlugin(BasePlugin):
    def on_plugin_load(self):
        self.add_on_send_message_hook()

    def on_send_message_hook(self, account, params):
        if params.message and params.message.startswith(".hi"):
            params.message = "Hello"
            return HookResult(strategy=HookStrategy.MODIFY, params=params)
        return HookResult()
```

Rules for the file:
- Metadata is literal: no expressions, no f-strings.
- A ported plugin keeps `__author__`, `__description__`, `__icon__`, `__requirements__`, `__name__` and `__version__`. `__id__` is the original id plus `_exd`, at most 32 characters. `__ported_from__` is `"<original id> <original version>"`.
- `__desktop_os__` is `windows`, `linux` or `macos`: the system the plugin was tested on. The same file must work on all three systems.

## Windows, Linux and macOS

- Build paths with `os.path.join(file_utils.get_plugin_dir(), "data.json")` or `pathlib`. Never use backslashes, drive letters, `%APPDATA%`, `os.environ["APPDATA"]`, `/home/...` or `~/Library`.
- File names are case-sensitive on Linux: `Data.json` and `data.json` are different files.
- Open text files with `encoding="utf-8"`. Windows uses another default encoding.
- `winreg`, `msvcrt`, `winsound` exist only on Windows, `fcntl`, `termios`, `pwd` only on Linux and macOS. Import them inside `try/except ImportError` and keep a fallback, or do not use them.
- `ctypes`, `subprocess`, `os.startfile` and shell commands are blocked everywhere. Open links with `Browser.openUrl` or `webbrowser.open`.
- Use `time.monotonic()` for intervals, `os.linesep` only when writing files for other programs.
- Exactly one class that inherits `BasePlugin`. A library plugin (imported by other plugins, for example `zwylib`) has no such class and keeps its original id.
- Hooks are methods of the plugin: `self.add_hook(...)`, `self.add_on_send_message_hook()`, `self.add_menu_item(...)`, `self.get_setting(...)`, `self.set_setting(...)`, `self.log(...)`.

## Hard rules

- Never use `subprocess`, `os.system`, `os.startfile`, `ctypes`, `cffi`, `winreg`, `marshal`, `gc`, `sys.settrace`, `sys.addaudithook`, `exec` or `eval`. The sandbox blocks them.
- Never load `.dex`, `.so`, `.dll` or `.pyd` files. `dalvik.system.*ClassLoader` has no desktop equivalent.
- Read and write files only in `file_utils.get_plugin_dir()` and `file_utils.get_plugin_cache_dir()`, or in files the user picked with `file_utils.pick_file`. Never touch `tdata`, other plugins, browser profiles or wallets.
- Never hard-code Android paths (`/sdcard`, `/storage/emulated/0`, `/data/user/0/...`).
- Never add network hosts that the original plugin did not use. The user approves each host on first use.
- No obfuscation, no base64 or zlib wrapped code.

## API that works as on Android

- Lifecycle: `on_plugin_load`, `on_plugin_unload`, `on_app_event`.
- Telegram requests and updates: `self.add_hook("TL_messages_sendMessage")` with `pre_request_hook(self, request_name, account, request)` and `post_request_hook(self, request_name, account, response, error)`; `self.add_hook("TL_updateNewMessage")` with `on_update_hook(self, update_name, account, update)`; `on_updates_hook`. `add_hook` takes one TL name only.
- Sending: `self.add_on_send_message_hook()` and `on_send_message_hook(self, account, params)` with `HookStrategy.DEFAULT`, `CANCEL`, `MODIFY`, `MODIFY_FINAL`.
- Menus: `self.add_menu_item(MenuItemData(menu_type=MenuItemType.MESSAGE_CONTEXT_MENU, text="...", on_click=fn))`. Types: `MESSAGE_CONTEXT_MENU` (context has `message` and `dialog_id`), `CHAT_ACTION_MENU`, `DRAWER_MENU`, `PROFILE_ACTION_MENU`.
- Settings: `create_settings(self)` returns a list of `Header`, `Divider`, `Switch`, `Selector`, `Input`, `Text`, `EditText`; sub pages through `Text(..., create_sub_fragment=fn)`.
- `client_utils`: `send_request`, `send_text`, `send_message`, `send_photo`, `send_document`, `send_video`, `send_audio`, `edit_message`, `run_on_queue`, `get_messages_controller`, `get_user_config`, `get_last_fragment`, `get_selected_account`.
- `android_utils`: `run_on_ui_thread(fn, delay=0)`, `copy_to_clipboard(text)`.
- `ui.bulletin.BulletinHelper.show_info / show_success / show_error`, `ui.alert.AlertDialogBuilder` with `set_title`, `set_message`, `set_items`, buttons.
- `file_utils`: `get_plugin_dir`, `get_plugin_cache_dir`, `pick_file(callback, title, filters, multiple)`, read, write, list.
- `hook_utils`: `find_class`, `get_private_field`, `set_private_field`.
- `extera_utils.text_formatting.parse_text`, `markdown_utils`, `intents` for `tg://` links.
- `__requirements__`: pure Python packages from PyPI. `requests`, `beautifulsoup4`, `lxml`, `pillow`, `PyYAML`, `packaging` are built in.
- Standard modules: `json`, `re`, `time`, `threading`, `sqlite3`, `urllib` and others.

## Emulated Java and Android

`java.lang` (Integer, String, StringBuilder, System, Thread, Math, Class, Runnable), `java.util` (ArrayList, HashMap, HashSet, Locale, Date, Random, UUID, Collections, Arrays), `java.io` (File, FileInputStream, FileOutputStream, ByteArrayInputStream, ByteArrayOutputStream), `java.nio.ByteBuffer`, `android.graphics` (Color, BitmapFactory, Bitmap with `getWidth`, `compress`, `createScaledBitmap`, PorterDuff, PorterDuffColorFilter), `android.graphics.drawable.GradientDrawable`, `android.os` (Build, Looper, Handler, Bundle, SystemClock), `android.util` (TypedValue, Log, Base64), `android.text` (TextUtils, InputType), `android.content` (Context with `getSystemService(Context.CLIPBOARD_SERVICE)`, ClipData, ClipboardManager, DialogInterface), `android.view.ViewGroup.LayoutParams`, `org.telegram.messenger` (R, UserObject, ChatObject, LocaleController, MessagesController, UserConfig, AndroidUtilities, ApplicationLoader, Utilities with `Callback` and queues), `org.telegram.tgnet` (TLRPC, TLObject, ConnectionsManager), `org.telegram.ui.ActionBar.AlertDialog.Builder`, `BottomSheet`, `org.telegram.messenger.browser.Browser`, `LayoutHelper`, `com.exteragram.messenger.ExteraConfig`. Any other class is a silent stub: calls do nothing. Prefer plain Python.

Working Telegram classes:
- `NotificationCenter.getInstance(account).addObserver(delegate, NotificationCenter.didReceiveNewMessages)`: the delegate gets `didReceivedNotification(id, account, dialog_id, messages, scheduled)` for every new message. `postNotificationName` delivers custom notifications between plugins.
- `SendMessagesHelper.getInstance(account).sendMessage(SendMessagesHelper.SendMessageParams.of(text, dialog_id))` sends a text message.
- `PluginsController.getInstance()`: `plugins`, `getPlugin(id)`, `isPluginEnabled(id)`, `setPluginEnabled(id, enabled)`.
- `XposedBridge.log`, `XposedBridge.hookMethod` and `hookAllMethods` follow the same rules as `hook_method`.
- `FileLoader` paths point to the plugin cache: Telegram files are not downloaded for plugins.

## Android hooks that work on desktop

Use `self.hook_all_methods(find_class("org.telegram.ui.ChatActivity"), "onResume", Handler())` or `self.hook_method(cls.getDeclaredMethod("onResume"), Handler())`. The handler class has `before_hooked_method(self, param)` and `after_hooked_method(self, param)`.

| Hook | Called on desktop | Notes |
|---|---|---|
| `SendMessagesHelper.sendMessage` | every outgoing message | `param.args[0].message` is the text, `param.setResult(None)` cancels |
| `MessagesController.processNewMessages` | every new message | `param.args[0]` is a list with one `MessageObject` |
| `ChatActivity.onResume`, `createView`, `onFragmentCreate` | chat opened | `param.thisObject.getDialogId()` |
| `ChatActivity.onPause`, `onFragmentDestroy` | chat left | |
| `ProfileActivity.onResume`, `createView` / `onPause` | profile opened / closed | `getUserId()`, `getChatId()` |
| `DialogsActivity.onResume` / `onPause` | chat list shown / chat opened | |
| `LaunchActivity.onCreate` / `onResume` / `onPause` | plugin loaded / window activated / window deactivated | |
| `LaunchActivity.onActivityResult` | after a file was picked | request code, result code, `Intent.getData()` |
| `Browser.openUrl` | every external link click | `param.args[1]` is the url, `param.setResult(None)` stops opening |
| methods of emulated classes | when called | arguments, result and `setResult` work |

Hooks on cells, views and drawing (`ChatMessageCell`, `DialogCell`, `onDraw`, `setMessageObject`, `ChatActivityEnterView`) do nothing on desktop. Remove them and use the replacement table.

## Replacement table

| Android pattern | Desktop replacement |
|---|---|
| Hook on `SendMessagesHelper.sendMessage` or the send button | `add_on_send_message_hook` and `on_send_message_hook`, edit `params.message`, return `HookResult(strategy=HookStrategy.MODIFY, params=params)` |
| Hook on `processUpdates`, `NotificationsController` | `self.add_hook("TL_updateNewMessage")`, `self.add_hook("TL_updateNewChannelMessage")`, `on_update_hook` |
| Hook on a TL request | `self.add_hook("TL_<request>")` and `pre_request_hook` / `post_request_hook` |
| Button in the chat header (`ChatActivity.createView`) | `MenuItemType.CHAT_ACTION_MENU` |
| Action on a message (`ChatMessageCell` long press) | `MenuItemType.MESSAGE_CONTEXT_MENU` |
| Drawer or profile buttons | `MenuItemType.DRAWER_MENU`, `MenuItemType.PROFILE_ACTION_MENU` |
| Custom `View`, `LinearLayout`, `TextView` screens | `create_settings` rows and sub pages, `AlertDialogBuilder` with `set_items` |
| `Toast.makeText` | `BulletinHelper.show_info(text)` |
| `SharedPreferences` | `self.get_setting(key, default)`, `self.set_setting(key, value)` |
| `getFilesDir()`, `/sdcard/...` | `file_utils.get_plugin_dir()` |
| `AndroidUtilities.runOnUIThread(r, delay)` | `run_on_ui_thread(fn, delay)` |
| Drawing on `Canvas` in chat cells, `Theme` colors of cells | drop |
| Hooks on `isPremium`, `premiumFeaturesBlocked` | drop: exDesktop has local Premium settings |
| Camera, `PhotoViewer`, `ChatAttachAlert` | drop |
| Embedded DEX code | find what it does from the Python call sites; rewrite sending, request or update logic in Python, drop drawing and screens, delete the blob |
| Native libraries through `ctypes` | use a pure Python package from PyPI or drop |

## Self check

- The file parses as Python 3.11 and every name used is imported or defined.
- Metadata is literal, `__desktop__ = True`, `__desktop_os__` is set, a ported `__id__` ends with `_exd`.
- No forbidden module, call, path or new host.
- `add_hook` gets one TL name. Android hooks are only from the supported list.
- Every dropped feature is listed in REPORT.
