## send
Change or cancel outgoing messages:
```python
def on_plugin_load(self):
    self.add_on_send_message_hook()

def on_send_message_hook(self, account, params):
    params.message = params.message.upper()
    return HookResult(strategy=HookStrategy.MODIFY, params=params)
```
`HookStrategy.CANCEL` cancels sending. `params` has `message`, `peer`, `entities`, `replyToMsg`.

## requests
Change or block any Telegram API request (TL names from the schema):
```python
def on_plugin_load(self):
    self.add_hook("TL_messages_sendReaction")

def pre_request_hook(self, request_name, account, request):
    request.big = True
    return HookResult(strategy=HookStrategy.MODIFY, request=request)

def post_request_hook(self, request_name, account, response, error):
    return HookResult()
```
Send a request: `client_utils.send_request(request, callback)` where `callback(response, error)`.

## updates
React to incoming updates:
```python
def on_plugin_load(self):
    self.add_hook("TL_updateNewMessage")
    self.add_hook("TL_updateNewChannelMessage")

def on_update_hook(self, update_name, account, update):
    return HookResult()
```

## menus
```python
self.add_menu_item(MenuItemData(menu_type=MenuItemType.MESSAGE_CONTEXT_MENU, text=..., on_click=lambda context: ...))
```
Types: MESSAGE_CONTEXT_MENU (context has message, dialog_id, account), CHAT_ACTION_MENU (dialog_id), DRAWER_MENU, PROFILE_ACTION_MENU (dialog_id).

## settings
```python
from ui.settings import Header, Divider, Switch, Selector, Input, Text

def create_settings(self):
    return [Header(text="Main"), Switch(key="enabled", text="Enabled", default=True),
            Selector(key="mode", text="Mode", default=0, items=["A", "B"]),
            Input(key="token", text="Token", default="")]
```
Read with `self.get_setting("enabled", True)`, write with `self.set_setting(key, value)`.

## ui
`from ui.bulletin import BulletinHelper` and `BulletinHelper.show_info(text)`, `show_error`, `show_success`.
`from ui.alert import AlertDialogBuilder`: `builder = AlertDialogBuilder(None)`, `set_title`, `set_message`, `set_items(items, on_click)`, `set_positive_button(text, on_click)`, `show()`.
`from android_utils import run_on_ui_thread, copy_to_clipboard`.
Custom Android views, Canvas, cells and screens are not available.

## client
`from client_utils import send_text, send_message, send_photo, send_document, edit_message, run_on_queue, get_last_fragment, get_selected_account`.
`send_text(dialog_id, text)`, `run_on_queue(fn)` runs work in background.

## files
Paths must work on Windows, Linux and macOS: os.path.join(get_plugin_dir(), "data.json"), never "folder\\file" or "C:/...". Linux file names are case-sensitive. Text files: open(path, encoding="utf-8").
`from file_utils import get_plugin_dir, get_plugin_cache_dir, pick_file`. `pick_file(callback, title, filters, multiple)` opens a file dialog, callback gets a list of paths. Relative paths in open() and sqlite3.connect() point to the plugin folder. Folders outside need user consent.

## hooks
These Android hooks work on desktop with `self.hook_all_methods(find_class("org.telegram.ui.ChatActivity"), "onResume", Handler())` or `self.hook_method(cls.getDeclaredMethod("onResume"), Handler())`, where Handler has before_hooked_method(self, param) and after_hooked_method(self, param):
- SendMessagesHelper.sendMessage: param.args[0].message is the outgoing text, param.setResult(None) cancels sending.
- MessagesController.processNewMessages: param.args[0] is a list with one MessageObject.
- ChatActivity onResume/createView (chat opened), onPause/onFragmentDestroy (chat left): param.thisObject.getDialogId().
- ProfileActivity onResume/createView (profile opened), onPause (profile closed).
- DialogsActivity onResume (chat list shown), onPause (chat opened from the list).
- LaunchActivity onCreate (plugin loaded), onResume (window activated), onPause (window deactivated).
- Browser.openUrl: param.args[1] is the url of every clicked link, param.setResult(None) stops opening it.
- Methods of emulated classes (AlertDialog.Builder, BottomSheet, AndroidUtilities, Browser and others in section java).
Hooks on cells, views and drawing (ChatMessageCell, DialogCell, onDraw, setMessageObject) do nothing on desktop.

## java
Emulated: java.lang basics, java.util collections, File, FileInputStream, FileOutputStream, ByteArrayOutputStream, ByteBuffer, android.graphics.Color, BitmapFactory (decodeFile, decodeByteArray, Options), Bitmap (getWidth, compress), PorterDuff, android.os.Build, Handler, Looper, Bundle, Log, Base64, InputType, DialogInterface, Context (getSystemService(Context.CLIPBOARD_SERVICE) with setPrimaryClip(ClipData.newPlainText(label, text))), R (R.drawable.name gives the name), UserObject, ChatObject, LocaleController, MessageObject fields.
Working Telegram classes: NotificationCenter.getInstance(account).addObserver(delegate, NotificationCenter.didReceiveNewMessages) delivers new messages to didReceivedNotification(id, account, dialog_id, messages, scheduled); postNotificationName works between plugins. SendMessagesHelper.getInstance(account).sendMessage(SendMessagesHelper.SendMessageParams.of(text, dialog_id)) sends text. PluginsController.getInstance().plugins, getPlugin(id), isPluginEnabled(id), setPluginEnabled(id, bool). Utilities.Callback, Utilities.stageQueue.postRunnable. FileLoader paths point to the plugin cache: files are not downloaded. Prefer plain Python.

## network
`requests` and `urllib` work. The user approves each host on first use.

## dex
The original carries compiled Java code (a base64 or zlib blob loaded with InMemoryDexClassLoader or DexClassLoader). Desktop has no Java.
1. Find what the DEX part does from the Python call sites: method names passed to the loader (load, attach, install), arguments, settings names and texts.
2. If it changes Android drawing, cells, animations or screens, drop it and list it in the report.
3. If it changes sending, requests or incoming messages, rewrite that logic in Python with the send hook, request hooks or update hooks.
4. Delete the blob, the loader and every reflection call. The ported file must not contain the blob.

## native
The original loads a native library (.so) with ctypes: emulators, audio processing, Android system tools. Desktop blocks native code.
1. If a pure Python package from PyPI does the same work, use it and list it in __requirements__.
2. Otherwise drop the feature and list it in the report. Never load .so, .dll or .pyd files.

## libraries
A library plugin has no BasePlugin class: other plugins import it by its id (for example cactuslib, zwylib, plugins_manager).
1. Keep __id__ exactly as in the original, without the _exd suffix, so dependent plugins keep working.
2. Keep every public function and class name and signature. Replace only Android internals.
3. A plugin that depends on a library keeps its import unchanged.
