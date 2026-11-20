---
name: exdesktop-plugins
description: Write new exDesktop plugins or port exteraGram (Android) plugins to exDesktop, the Telegram Desktop based client with the exteraGram Python plugin API. Use when the user asks to create, fix, port or review a .plugin file for exDesktop or exteraGram on desktop.
---

# exDesktop plugins

exDesktop runs exteraGram plugins (Python, SDK 1.4.6.2) on Windows, Linux and macOS. The plugin API is the same as on Android. There is no Java and no Android UI: the most used Java and Android classes are emulated in Python, and a large set of Android method hooks is mapped to desktop events with real Xposed semantics. Hooks outside that set are accepted but never fire. Everything that depends on them must be rewritten with the plugin API.

Full documentation: https://exdesktop.xyz/docs/

## Task

1. List every feature of the plugin from its settings, menu items, hooks and texts.
2. For each feature decide: keep as is, keep through a supported hook, rewrite with the replacement table, or drop.
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
- Exactly one class that inherits `BasePlugin`. A library plugin (imported by other plugins, for example `zwylib`) has no such class and keeps its original id.

## Windows, Linux and macOS

- Build paths with `os.path.join(file_utils.get_plugin_dir(), "data.json")` or `pathlib`. Never use backslashes, drive letters, `%APPDATA%`, `os.environ["APPDATA"]`, `/home/...` or `~/Library`.
- File names are case-sensitive on Linux: `Data.json` and `data.json` are different files.
- Open text files with `encoding="utf-8"`. Windows uses another default encoding.
- `winreg`, `msvcrt`, `winsound` exist only on Windows, `fcntl`, `termios`, `pwd` only on Linux and macOS. Import them inside `try/except ImportError` and keep a fallback, or do not use them.
- Open links with `Browser.openUrl` or `webbrowser.open`.
- Use `time.monotonic()` for intervals, `os.linesep` only when writing files for other programs.

## Hard rules

The sandbox enforces these rules, and PluginGuard blocks installation of a plugin that breaks the last three.

- Never use `subprocess`, `os.system`, `os.startfile`, `ctypes`, `cffi`, `winreg`, `marshal`, `gc`, `sys.settrace`, `sys.addaudithook`, `exec` or `eval`.
- Read and write files only in `file_utils.get_plugin_dir()` and `file_utils.get_plugin_cache_dir()`, or in files the user picked with `file_utils.pick_file`. Other folders need the user's permission, `tdata`, other plugins, browser profiles, wallets and SSH keys are never available.
- Never hard-code Android paths (`/sdcard`, `/storage/emulated/0`, `/data/user/0/...`).
- Never add network hosts that the original plugin did not use. Internet access needs no prompt, but local network devices and UDP need the user's permission, and sending session keys, login codes, phone numbers, private keys, seed phrases or browser data is stopped until the user decides.
- Never load `.dex`, `.so`, `.dll` or `.pyd` files, and never download code (DEX, native libraries, Python modules, other plugins) to load it at runtime. Such plugins are blocked because the downloaded code cannot be checked.
- Never embed executables or archives with code.
- No obfuscation: no base64, zlib or XOR wrapped code. Encrypted plugins are decoded for the check and install only after a safe verdict of the AI arbiter.

## API that works as on Android

- Lifecycle: `on_plugin_load`, `on_plugin_unload`, `on_app_event`. `self.stop_event` is set on unload, `self.register_cleanup(fn)` runs on unload.
- Telegram requests and updates: `self.add_hook("TL_messages_sendMessage")` with `pre_request_hook(self, request_name, account, request)` and `post_request_hook(self, request_name, account, response, error)`; `self.add_hook("TL_updateNewMessage")` with `on_update_hook(self, update_name, account, update)`; `on_updates_hook`. `add_hook` takes one TL name only.
- Sending: `self.add_on_send_message_hook()` and `on_send_message_hook(self, account, params)` with `HookStrategy.DEFAULT`, `CANCEL`, `MODIFY`, `MODIFY_FINAL`.
- Menus: `self.add_menu_item(MenuItemData(menu_type=MenuItemType.MESSAGE_CONTEXT_MENU, text="...", on_click=fn))`. Types: `MESSAGE_CONTEXT_MENU` (context has `message` and `dialog_id`), `CHAT_ACTION_MENU` (also shown as a plugin icon in the chat header), `DRAWER_MENU`, `PROFILE_ACTION_MENU`.
- Settings: `create_settings(self)` returns a list of `Header`, `Divider`, `Switch`, `Selector`, `Input`, `Text`, `EditText`; sub pages through `Text(..., create_sub_fragment=fn)`. `get_setting`, `set_setting`, `export_settings`, `import_settings`.
- `client_utils`: `send_request`, `send_text`, `send_message`, `send_photo`, `send_document`, `send_video`, `send_audio`, `edit_message`, `run_on_queue`, `get_messages_controller`, `get_media_controller`, `get_notifications_controller`, `get_user_config`, `get_last_fragment`, `get_selected_account`, `get_hook_account`, `client(account)`.
- `android_utils`: `run_on_ui_thread(fn, delay=0)`, `copy_to_clipboard(text)`, `R`, `OnClickListener`, `OnLongClickListener`.
- `ui.bulletin.BulletinHelper.show_info / show_success / show_error`. `ui.alert.AlertDialogBuilder` with title, message, items, three buttons, `make_button_red`, `set_view`, a progress dialog with `set_progress`, `set_cancelable`, `dismiss`, `get_button`.
- `ui.webapp.WebApp(title, html=..., width=..., height=..., on_message=fn)`: an HTML window with `exd.send`, `exd.onMessage`, `exd.close` in the page and `app.send`, `app.eval`, `app.close`, `on_load`, `on_close` in Python. `android.webkit.WebView` (`loadData`, `loadUrl` with `https://` only, `addJavascriptInterface`, `evaluateJavascript`, `WebViewClient`) opens in the same kind of window.
- `file_utils`: `get_plugin_dir`, `get_plugin_cache_dir`, `pick_file(callback, title, filters, multiple)`, read, write, list. `FilesController.register(FileInfo(ext, on_click, ...))` and `self.add_file_hook` handle clicks on downloaded documents with that extension.
- `hook_utils`: `find_class`, `get_private_field`, `set_private_field` and static variants.
- `log_utils.get_logger`, `self.logger`, `markdown_utils.parse_markdown`, `extera_utils.text_formatting` (`parse_text`, `HTML.unparse`, `Markdown.unparse`), `intents` with `self.add_intent_hook(IntentsManager.HandlerInfo(...), IntentHookType.BEFORE)` for `tg://` links.
- `__requirements__`: pure Python packages from PyPI. `requests`, `beautifulsoup4`, `lxml`, `pillow`, `PyYAML`, `packaging` are built in.
- Standard modules: `json`, `re`, `time`, `threading`, `sqlite3`, `urllib` and others.

## Emulated Java and Android

`java.lang`, `java.util`, `java.io`, `java.nio.ByteBuffer`, `android.graphics` (Color, BitmapFactory, Bitmap, PorterDuff, PorterDuffColorFilter, GradientDrawable), `android.os` (Build, Looper, Handler, Bundle, SystemClock), `android.util` (TypedValue, Log, Base64), `android.text` (TextUtils, InputType, TextWatcher), `android.content` (Context, ClipData, ClipboardManager, DialogInterface), `android.widget` (TextView, EditText, ImageView, Button, CheckBox, Switch, SeekBar, ProgressBar, LinearLayout, FrameLayout, ScrollView), `org.telegram.messenger` (R, UserObject, ChatObject, LocaleController, MessagesController, MediaController, NotificationCenter, SendMessagesHelper, UserConfig, AndroidUtilities, ApplicationLoader, FileLoader, Utilities), `org.telegram.tgnet` (TLRPC, TLObject, ConnectionsManager), `org.telegram.ui.ActionBar.AlertDialog.Builder`, `BottomSheet` and `BottomSheet.Builder` with `setItems`, `Browser`, `LayoutHelper`, `com.exteragram.messenger` (ExteraConfig, PluginsController, PythonPluginsEngine), `de.robv.android.xposed` (XposedBridge, XposedHelpers, XC_MethodHook, XC_MethodReplacement). Any other class is a silent stub: calls do nothing. Prefer plain Python.

Working Telegram classes:
- `NotificationCenter.getInstance(account).addObserver(delegate, NotificationCenter.didReceiveNewMessages)`: the delegate gets `didReceivedNotification(id, account, dialog_id, messages, scheduled)` for every new message. `postNotificationName` delivers custom notifications between plugins.
- `SendMessagesHelper.getInstance(account).sendMessage(SendMessagesHelper.SendMessageParams.of(text, dialog_id))` sends a text message, `prepareSendingDocument` and `prepareSendingPhoto` send files.
- `MessagesController`: `getAllDialogs`, `getDialogs(folder_id)`, `getDialog(id)`, `getDialogFilters`, `isDialogMuted`, `getUser`, `getChat`, `openByUserName`, `deleteMessages`, `pinDialog`, `markDialogAsRead`.
- `MediaController` controls the desktop player: `getPlayingMessageObject`, `playMessage`, `pauseMessage`, `resumeAudio`, `playNextMessage`, `playPreviousMessage`, `cleanupPlayer`, `seekToProgress`.
- `ChatActivity.getChatActivityEnterView()`: `getFieldText()`, `setFieldText(text)`, `getEditField().getText()` read and change the message field of the open chat.
- `PluginsController.getInstance()`: `plugins`, `getPlugin(id)`, `isPluginEnabled(id)`, `setPluginEnabled(id, enabled)`.
- `presentFragment(DialogsActivity(args))` with `DialogsActivityDelegate` opens a chat picker, `GroupCreateActivity` with `GroupCreateActivityDelegate` picks several users or chats, `PluginSettingsActivity(plugin)` opens the plugin settings.
- `FileLoader` paths point to the plugin cache: Telegram files are not downloaded for plugins.

## Method hooks

`self.hook_method(method, handler, priority)`, `self.hook_all_methods(cls, name, handler)`, `self.hook_all_constructors(cls, handler)` and `XposedBridge` / `XposedHelpers.findAndHookMethod` work as in exteraGram. The handler is a `MethodHook` or `XC_MethodHook` with `before_hooked_method(self, param)` and `after_hooked_method(self, param)`, a `MethodReplacement` with `replace_hooked_method`, or `before=` / `after=` functions. Higher `priority` runs earlier before and later after. `param.setResult()` in `before_hooked_method` cancels the original. `HookFilter` and `hook_filters` work, including MVEL `Condition`. The returned `Unhook` removes the hook.

Hooks that fire on desktop:

| Hook | Called on desktop | Notes |
|---|---|---|
| `SendMessagesHelper.sendMessage` | every outgoing message | `param.args[0].message` is the text, `param.setResult(None)` cancels |
| `MessagesController.processNewMessages`, `NotificationsController.processNewMessages` | every new message | `param.args[0]` is a list with one `MessageObject` |
| `ChatActivity.onResume`, `createView`, `onFragmentCreate`, `onTransitionAnimationEnd` / `onPause`, `onFragmentDestroy` | chat opened / left | `param.thisObject.getDialogId()` |
| `ProfileActivity.onResume`, `createView`, `updateProfileData` / `onPause` | profile opened / closed | `getUserId()`, `getChatId()` |
| `ProfileActivity.updateRowsIds`, `ProfileActivity$ListAdapter.onBindViewHolder` | profile info rows | change rows through `rowCount` and `TextDetailCell.setTextAndValue` |
| `DialogsActivity.onResume`, `MainTabsActivity.onResume` / `DialogsActivity.onPause` | chat list shown / chat opened | |
| `DialogCell.update`, `buildLayout`, `setDialog` | chat name in the chat list | change `first_name`, `last_name` or `title` in `before_hooked_method` to rename locally |
| `ChatAvatarContainer.setTitle`, `ChatActivity.updateTitle` | chat header title | change `param.args[0]` or call `setTitle()` |
| `ChatAvatarContainer.updateSubtitle`, `setSubtitle` | chat header status | `setSubtitle()` replaces the status |
| `ChatAvatarContainer.onAvatarClick`, `ChatActivity.openProfile` | click on the chat header | `param.setResult(None)` keeps the profile closed |
| `ChatMessageCell.setMessageObject`, `setMessageContent`, `setMessageObjectInternal` | message text built in the chat | assigning `messageText` changes the shown text only |
| `ChatActivity.createMenu`, `fillMessageMenu` | right click on a message | `param.setResult(None)` keeps the menu closed |
| `ChatActivity.getMessageContent` | "Copy text" in the message menu | `param.setResult(text)` in `after_hooked_method` changes the copied text |
| `ChatActivity.addToSelectedMessages` | message selected | `param.setResult(None)` cancels |
| `ChatActivityEnterView.setReplyingMessageObject` | reply chosen | `param.setResult(None)` cancels |
| `ChatActivity.sendMedia`, `SendMessagesHelper.prepareSendingMedia`, `prepareSendingDocuments` | files and photos sent | `param.setResult(None)` cancels |
| `SendMessagesHelper.sendReaction` | reaction set or removed | `param.args[1]` lists `VisibleReaction`, `param.setResult(None)` cancels |
| `NotificationsController.showOrUpdateNotification` | new message notification | `param.setResult(None)` hides it |
| `MediaController.playMessage`, `setPlaylist`, `playNextMessage`, `playPreviousMessage`, `cleanupPlayer` | player actions | `param.setResult(...)` cancels |
| `AudioPlayerAlert` constructor, `updateTitle`, `onSubItemClick` | full screen player | `optionsButton.addSubItem` adds items to its right click menu |
| `TranscribeButton.onTap` | voice transcription button | `param.setResult(None)` cancels |
| `VoIPHelper.startCall` | outgoing call | `param.setResult(None)` cancels |
| `AndroidUtilities.openForView` | file opened from a chat | `param.setResult(None)` cancels |
| `SettingsActivity.fillItems`, `onClick` | main menu built and clicked | add `UItem.asButton(id, icon, text)` items |
| `LaunchActivity.onCreate` / `onResume`, `onStart` / `onPause`, `onStop` | plugin loaded / window activated / deactivated | |
| `LaunchActivity.onActivityResult`, `BaseFragment.onActivityResultFragment` | after a file was picked | request code, result code, `Intent.getData()` |
| `LaunchActivity.handleIntent`, `onNewIntent` | `tg://` link opened | `param.setResult(True)` stops handling |
| `Browser.openUrl` | every external link click | `param.args[1]` is the url, `param.setResult(None)` stops opening |
| `PluginsController.setPluginEnabled`, `PythonPluginsEngine.loadPlugin`, `unloadPlugin` | plugin enabled, loaded, unloaded | `param.setResult(None)` cancels |
| methods of emulated classes | when called | arguments, result and `setResult` work |

Hooks on drawing and layout (`onDraw`, `onMeasure`, `onLayout`, `Theme` colors of cells, camera, `PhotoViewer`, `ChatAttachAlert`) never fire. Remove them and use the replacement table. A plugin that keeps unsupported hooks is marked "Works partially" in the plugin list.

## Replacement table

| Android pattern | Desktop replacement |
|---|---|
| Hook on the send button | `add_on_send_message_hook` and `on_send_message_hook`, edit `params.message`, return `HookResult(strategy=HookStrategy.MODIFY, params=params)` |
| Hook on `processUpdates` | `self.add_hook("TL_updateNewMessage")`, `self.add_hook("TL_updateNewChannelMessage")`, `on_update_hook` |
| Hook on a TL request | `self.add_hook("TL_<request>")` and `pre_request_hook` / `post_request_hook` |
| Button added to the chat header view | `MenuItemType.CHAT_ACTION_MENU` |
| Action on a message by long press | `MenuItemType.MESSAGE_CONTEXT_MENU` |
| Drawer or profile buttons | `MenuItemType.DRAWER_MENU`, `MenuItemType.PROFILE_ACTION_MENU` |
| Custom `View` screens | `create_settings` rows and sub pages, `AlertDialogBuilder` with `set_items` or `set_view`, `WebApp` for rich screens |
| `Toast.makeText` | `BulletinHelper.show_info(text)` |
| `SharedPreferences` | `self.get_setting(key, default)`, `self.set_setting(key, value)` |
| `getFilesDir()`, `/sdcard/...` | `file_utils.get_plugin_dir()` |
| `AndroidUtilities.runOnUIThread(r, delay)` | `run_on_ui_thread(fn, delay)` |
| Drawing on `Canvas` in chat cells | drop, or change text through `ChatMessageCell.setMessageObject` |
| Hooks on `isPremium`, `premiumFeaturesBlocked` | drop: exDesktop has local Premium settings |
| Camera, `PhotoViewer`, `ChatAttachAlert` | drop |
| Embedded DEX code | find what it does from the Python call sites; rewrite sending, request or update logic in Python, drop drawing and screens, delete the blob |
| DEX or native code downloaded at runtime | rewrite in Python or drop: such plugins are blocked |
| Native libraries through `ctypes` | use a pure Python package from PyPI or drop |

## Self check

- The file parses as Python 3.11 and every name used is imported or defined.
- Metadata is literal, `__desktop__ = True`, `__desktop_os__` is set, a ported `__id__` ends with `_exd`.
- No forbidden module, call, path, new host, embedded binary or runtime code download.
- `add_hook` gets one TL name. Android method hooks are only from the supported table or methods of emulated classes.
- Every dropped feature is listed in REPORT.
