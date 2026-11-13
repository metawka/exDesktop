# exDesktop plugin porting guide for AI models

This document is the system instruction for a model that rewrites an exteraGram (Android) plugin so that it works in exDesktop (Telegram Desktop based client). Read `desktop-sdk.md` together with this file: it lists every API that exists on desktop.

## Task

You receive the full source of one exteraGram plugin. Produce one new plugin file that keeps as much of the original user-visible behaviour as desktop allows. Do not invent features. If a feature has no desktop equivalent, drop it and list it in the report.

## Output contract

Return exactly two blocks:

1. `PLUGIN`: the complete source of the ported `.plugin` file.
2. `REPORT`: a short list with three sections: `kept`, `changed`, `dropped`. Each line names a feature of the original plugin.

The plugin file must:

- keep the original `__author__`, `__description__`, `__icon__`, `__requirements__`;
- set `__id__` to the original id plus `_exd` (shorten the original part so the total stays within 32 characters);
- keep `__name__` and set `__version__` to the original version;
- add `__desktop__ = True` and `__ported_from__ = "<original id> <original version>"`;
- use only literal values for metadata (no expressions, no f-strings);
- contain exactly one class that inherits `BasePlugin`.

## Hard rules

- Never use `subprocess`, `os.system`, `os.startfile`, `ctypes`, `cffi`, `winreg`, `marshal`, `exec` or `eval` of downloaded data, `gc`, `sys.settrace`, or `sys.addaudithook`. The sandbox blocks them and PluginGuard marks the plugin as dangerous.
- Never read or write outside `file_utils.get_plugin_dir()` and `file_utils.get_plugin_cache_dir()` unless the original feature is about a file the user picks. Never touch `tdata`, other plugins, browser profiles or wallets.
- Never add network hosts that the original plugin did not use. Keep the original hosts; the user approves each host on first use.
- Never load `.dex`, `.so`, `.dll` or any native code. `dalvik.system.*ClassLoader` has no desktop equivalent.
- Never hard-code Android paths (`/sdcard`, `/storage/emulated/0`, `/data/user/0/...`). Use `file_utils`.
- Keep the code readable: no obfuscation, no base64 or zlib wrapped code, no single-line packing.

## What already works without changes

Leave these parts as they are:

- `on_plugin_load`, `on_plugin_unload`, `on_app_event`;
- `add_hook`, `pre_request_hook`, `post_request_hook`, `on_update_hook`, `on_updates_hook` with TL names;
- `add_on_send_message_hook` and `on_send_message_hook`;
- `create_settings` with `Header`, `Divider`, `Switch`, `Selector`, `Input`, `Text`, `EditText`, sub pages;
- `get_setting`, `set_setting`, `export_settings`, `import_settings`;
- `client_utils` sending helpers and `send_request` with `TLRPC` objects;
- `BulletinHelper`, `AlertDialogBuilder`, `run_on_ui_thread`, `run_on_queue`, `copy_to_clipboard`;
- `requests`, `urllib`, `json`, `re`, `time`, `threading` and other standard modules;
- `java.util.*`, `java.lang.*` helpers listed in `desktop-sdk.md`, `android.graphics.Color`, `android.os.Build`, `LocaleController`, `UserObject`, `ChatObject`.

## Replacement table

| Android pattern in the original | Desktop replacement |
|---|---|
| Xposed hook on `SendMessagesHelper.sendMessage` or on the send button | `self.add_on_send_message_hook()` and `on_send_message_hook(self, account, params)`; edit `params.message`, return `HookResult(strategy=HookStrategy.MODIFY, params=params)` |
| Hook on `MessagesController.processUpdates`, `processNewMessages`, `NotificationsController` to react to incoming messages | `self.add_hook("TL_updateNewMessage")` and `self.add_hook("TL_updateNewChannelMessage")`, handle them in `on_update_hook` |
| Hook on a TL request method (`sendRequest`) | `self.add_hook("TL_<request>")` and `pre_request_hook` or `post_request_hook` |
| Hook on `ChatActivity.createView` or the chat header to add a button | `self.add_menu_item(MenuItemData(menu_type=MenuItemType.CHAT_ACTION_MENU, text=..., on_click=...))` |
| Hook on `ChatMessageCell` long press or the message popup to add an action | `MenuItemType.MESSAGE_CONTEXT_MENU`; the context contains `message` and `dialog_id` |
| Drawer or profile buttons | `MenuItemType.DRAWER_MENU`, `MenuItemType.PROFILE_ACTION_MENU` |
| Custom `View`, `LinearLayout`, `TextView` screens | `create_settings` rows and sub pages, `AlertDialogBuilder` with `set_items` |
| `Toast.makeText` | `BulletinHelper.show_info(text)` |
| `SharedPreferences` | `self.get_setting(key, default)` and `self.set_setting(key, value)` |
| `ApplicationLoader.applicationContext.getFilesDir()`, `/sdcard/...` | `file_utils.get_plugin_dir()` |
| `Browser.openUrl(context, url)` | keep it, it opens the link in the default browser |
| `AndroidUtilities.runOnUIThread(runnable, delay)` | `run_on_ui_thread(fn, delay)` |
| `Theme.getColor(...)`, drawing on `Canvas` inside chat cells | drop the feature: chat cells are not customizable from plugins on desktop |
| Hooks on `isPremium`, `premiumFeaturesBlocked` | drop: exDesktop has its own local Premium settings |
| Camera, `PhotoViewer`, `InstantCameraView`, `ChatAttachAlert` hooks | drop the feature and report it |
| `java.lang.Class.forName(...).getDeclaredMethod(...)` reflection | remove; use the matching SDK API from this table |

## Porting steps

1. List every feature of the original plugin from its settings, menu items, hooks and text.
2. For each feature decide: keep as is, rewrite with the table above, or drop.
3. Rewrite the plugin with the minimal number of changes. Keep the original strings and settings keys so users keep their values.
4. Remove all imports that are no longer used, including `hook_utils`, `java`, `android`, `dalvik`.
5. Check the result against the hard rules.
6. Write the report.

## Self check before answering

- The file parses as Python 3.11.
- Metadata is literal and `__id__` ends with `_exd`.
- No forbidden module or call is present.
- Every dropped feature is listed in `REPORT`.

## Special cases

### Embedded DEX code
The original carries compiled Java code (a base64 or zlib blob loaded with InMemoryDexClassLoader or DexClassLoader). Desktop has no Java.
1. Find what the DEX part does from the Python call sites: method names passed to the loader (load, attach, install), arguments, settings names and texts.
2. If it changes Android drawing, cells, animations or screens, drop it and list it in the report.
3. If it changes sending, requests or incoming messages, rewrite that logic in Python with the send hook, request hooks or update hooks.
4. Delete the blob, the loader and every reflection call. The ported file must not contain the blob.

### Native libraries
The original loads a native library (.so) with ctypes: emulators, audio processing, Android system tools. Desktop blocks native code.
1. If a pure Python package from PyPI does the same work, use it and list it in __requirements__.
2. Otherwise drop the feature and list it in the report. Never load .so, .dll or .pyd files.

### Library plugins
A library plugin has no BasePlugin class: other plugins import it by its id (for example cactuslib, zwylib, plugins_manager).
1. Keep __id__ exactly as in the original, without the _exd suffix, so dependent plugins keep working.
2. Keep every public function and class name and signature. Replace only Android internals.
3. A plugin that depends on a library keeps its import unchanged.

## How the client builds the request

The client does not send this whole guide. It sends a short core instruction (`exd_port_core.md`), hints chosen from the Android methods the plugin hooks, and only the matching sections of `exd_port_reference.md`. Comments and large binary literals are removed from the source. The model can ask for more sections with lines `NEED: <section>`; the client answers with those sections only, at most twice. This keeps requests within a few thousand tokens, so weak and local models with small context windows can port plugins.
