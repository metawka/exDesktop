# SDK плагинов exDesktop

exDesktop запускает плагины exteraGram на компьютере. Движок плагинов совместим с SDK exteraGram 1.4.6.2 (`_sdk_version.__version__`): плагин из каталога exteraGram устанавливается без изменений, а его функции работают в той мере, в которой у них есть аналог на ПК.

Документ описывает, что поддерживается, как устроена защита и чем ПК отличается от Android.

## Форматы плагинов

| Формат | Расширения | Поддержка |
|---|---|---|
| Однофайловый плагин | `.plugin`, `.py` | полная |
| Elyx | `.elyx`, `.eaf`, `.elyx.zip`, `.eaf.zip` | полная, кроме зашифрованных архивов |

Метаданные однофайлового плагина читаются из констант верхнего уровня (`__id__`, `__name__`, `__version__`, `__description__`, `__author__`, `__icon__`, `__app_version__`, `__min_version__`, `__sdk_version__`, `__requirements__`, `__category__`) без выполнения кода.

## Что работает так же, как на Android

- Жизненный цикл: `on_plugin_load`, `on_plugin_unload`, `on_app_event`.
- Хуки запросов и обновлений Telegram: `add_hook`, `pre_request_hook`, `post_request_hook`, `on_update_hook`, `on_updates_hook`. Имена запросов совпадают с TL-схемой (`TL_messages_sendMessage` и другие).
- Хук отправки сообщения: `add_on_send_message_hook`, `on_send_message_hook`, стратегии `HookStrategy.DEFAULT`, `CANCEL`, `MODIFY`, `MODIFY_FINAL`.
- Пункты меню `MenuItemType`: контекстное меню сообщения, меню чата (плюс значок плагинов в шапке чата: одно действие выполняется сразу, несколько открывают список), боковое меню и меню профиля.
- Настройки `ui.settings`: `Header`, `Divider`, `Switch`, `Selector`, `Input`, `Text`, `EditText`, вложенные страницы, `get_setting`, `set_setting`, `export_settings`, `import_settings`.
- `client_utils`: `send_request`, `send_text`, `send_message`, `send_photo`, `send_document`, `send_video`, `send_audio`, `edit_message`, `run_on_queue`, мультиаккаунт (`get_selected_account`, `get_hook_account`, `client(account)`).
- `android_utils`: `run_on_ui_thread`, `copy_to_clipboard`, `R`, `OnClickListener`, `OnLongClickListener`.
- `ui.bulletin.BulletinHelper` и `ui.alert.AlertDialogBuilder`: заголовок, текст, пункты, три кнопки, `make_button_red`, `set_view` (Android-вью внутри окна), окно загрузки с `set_progress`, `set_cancelable`, `set_canceled_on_touch_outside`, `dismiss` закрывает окно, `get_button`, `get_context`.
- `log_utils`: `get_logger`, `Logger` с уровнями, `child`, `timed`, `dump`, обработчиками; `BasePlugin.logger` возвращает такой логгер.
- `markdown_utils.parse_markdown` возвращает `ParsedMessage` с `RawEntity`; `RawEntity.to_tlrpc_object()` создаёт TL-объект сущности. Разметка как в exteraGram: `**жирный**`, `__курсив__`, `--подчёркнутый--`, `~~зачёркнутый~~`, `||спойлер||`, экранирование символов через `\`. `extera_utils.text_formatting.HTML.unparse` и `Markdown.unparse` собирают разметку обратно из текста и сущностей.
- `BasePlugin`: `name`, `description`, `author`, `version`, `min_version`, `icon`, `requirements` из метаданных, `initialized`, `stop_event` (устанавливается при выгрузке), `register_cleanup`; `PluginMetadata`, `PluginError`.
- `plugin_settings`: `get_setting`, `set_setting`, `get_all_settings`, `set_all_settings`, `clear_settings`; `hook_utils.get_private_field`, `set_private_field` и статические варианты читают и меняют поля объектов.
- `file_utils`: папки плагина, чтение, запись, список файлов. `FilesController.register(FileInfo(ext, on_click, whitelist_places=..., blacklist_places=...))` и `BasePlugin.add_file_hook`: нажатие на скачанный документ с этим расширением в чате вызывает `on_click` с `OnClickArgs` (`place`, `file`, `file_name`, `message`, `activity`, `parent_fragment`) вместо открытия файла.
- `extera_utils.text_formatting.parse_text` с режимами HTML и Markdown.
- `intents`: обработка ссылок `tg://`. `IntentsManager.HandlerInfo` с `scheme`, `host`, `path` (шаблоны вида `/user/{id}`), `required_path_args_names`, флагами и приоритетом; `BasePlugin.add_intent_hook(info, IntentHookType.BEFORE)` и `remove_intent_hook`.
- Зависимости `__requirements__`: устанавливаются только чистые Python-пакеты, как и на Android, загрузка идёт в фоне. Встроены `requests`, `beautifulsoup4`, `lxml`, `pillow`, `PyYAML`, `packaging`.
- Плагины-библиотеки (`zwylib` и другие): плагин импортирует библиотеку по её id. Если библиотека не установлена, в карточке плагина указывается, какую библиотеку нужно установить.

## Эмуляция Java и Android

Код плагина импортирует Java-классы (`from java.util import ArrayList`, `find_class("android.graphics.Color")`). На ПК Java нет, поэтому самые используемые классы реализованы на Python и ведут себя как оригиналы.

| Пакет | Классы |
|---|---|
| `java.lang` | `Object`, `Integer`, `Long`, `Short`, `Byte`, `Float`, `Double`, `Boolean`, `Character`, `String`, `StringBuilder`, `System`, `Thread`, `Math`, `Class`, `Runnable` |
| `java.util` | `ArrayList`, `LinkedList`, `List`, `HashMap`, `LinkedHashMap`, `TreeMap`, `Map`, `HashSet`, `Set`, `Locale`, `Date`, `Random`, `UUID`, `Collections`, `Arrays` |
| `java.io`, `java.nio` | `File`, `FileInputStream`, `FileOutputStream`, `ByteArrayInputStream`, `ByteArrayOutputStream`, `ByteBuffer`, `ByteOrder` |
| `android.graphics` | `Color` (`parseColor`, `argb`, `rgb`, компоненты, HSV), `BitmapFactory` (`decodeFile`, `decodeByteArray`, `decodeStream`, `Options`), `Bitmap` (`getWidth`, `getHeight`, `compress`, `createScaledBitmap`), `PorterDuff`, `PorterDuffColorFilter` |
| `android.content` | `Context` (константы сервисов, `getSystemService`), `ClipData`, `ClipboardManager`, `DialogInterface` |
| `android.os` | `Build` (`SDK_INT = 34`), `Build.VERSION_CODES`, `Looper`, `Handler`, `Bundle`, `SystemClock` |
| `android.util`, `android.view`, `android.text` | `TypedValue`, `Log`, `Base64`, `MotionEvent`, `TextUtils`, `InputType`, `ViewGroup.LayoutParams` |
| `org.telegram.messenger` | `R`, `UserObject`, `ChatObject`, `LocaleController`, `BuildVars`, `FileLog`, `Utilities` (`Callback`, очереди), `MessagesController`, `UserConfig`, `NotificationCenter`, `SendMessagesHelper`, `FileLoader` |
| `com.exteragram.messenger` | `ExteraConfig`, `PluginsController`, `Plugin`, `PythonPluginsEngine` |
| `de.robv.android.xposed` | `XposedBridge`, `XposedHelpers`, `XC_MethodHook` с `MethodHookParam` и `Unhook`, `XC_MethodReplacement` (`returnConstant`, `DO_NOTHING`) |
| `com.exteragram.messenger.plugins` | `PyMethodHook`, `PyMethodReplacement`, `hooks.HookFilter` |
| `org.telegram.tgnet` | `TLRPC`, `TLObject`, `ConnectionsManager`, `RequestDelegate` |

`LocaleController.getInstance().getCurrentLocale().getLanguage()` и `Locale.getDefault()` возвращают язык интерфейса exDesktop.

Классы Telegram, которые работают по-настоящему:

- `NotificationCenter.getInstance(account).addObserver(delegate, NotificationCenter.didReceiveNewMessages)`: делегат получает `didReceivedNotification(id, account, dialog_id, messages, scheduled)` для каждого нового сообщения. `postNotificationName` передаёт собственные уведомления между плагинами.
- `SendMessagesHelper.getInstance(account).sendMessage(SendMessageParams.of(text, dialog_id))` отправляет текст, `prepareSendingDocument` и `prepareSendingPhoto` отправляют файл.
- `PluginsController.getInstance()`: список `plugins`, `getPlugin`, `isPluginEnabled`, `setPluginEnabled`. `PythonPluginsEngine.loadPlugin`, `unloadPlugin`, `isPluginActive` загружают и выгружают плагин, `loadPluginFromFile` устанавливает файл после проверки PluginGuard (файл с критичными или высокими угрозами не устанавливается).
- `Context.getSystemService(Context.CLIPBOARD_SERVICE).setPrimaryClip(ClipData.newPlainText(label, text))` копирует текст в буфер обмена.
- `BitmapFactory` читает изображения через Pillow, `Bitmap.compress` сохраняет их в PNG, JPEG или WEBP.
- `R.drawable.имя` и `R.string.имя` возвращают имя ресурса.
- `FileLoader` возвращает пути в кэше плагина: файлы Telegram для плагинов не загружаются.

Эмулированные классы находятся и через `find_class`, `jclass` и `Class.forName`, а не только через `import`. У них есть методы рефлексии (`getName`, `getDeclaredMethods`, `getDeclaredMethod`), поэтому плагин может передать такой метод в `hook_method`. `org.telegram.ui.ActionBar.AlertDialog.Builder` с `setMessage`, `setItems`, `setPositiveButton` и другими кнопками показывает настоящее окно exDesktop, `LayoutHelper.createLinear` и `createFrame` задают размеры элементов.

Класс, которого нет в эмуляции, по-прежнему заменяется пустым объектом: обращение к нему не вызывает ошибку, но и ничего не делает. Плагин продолжает работать, а функция, завязанная на такой класс, недоступна.

## Хуки методов (Xposed)

На Android плагин может перехватить любой Java-метод Telegram (`hook_method`, `hook_all_methods`, `hook_all_constructors`). В exDesktop нет Java-классов Android-клиента, поэтому срабатывают только перехваты из списка ниже и перехваты методов эмулированных классов. Остальные вызовы проходят без ошибки, но перехват не выполняется. Функции, построенные на перехвате интерфейса Android (ячейки сообщений, отрисовка, камера), на ПК недоступны.

Методы эмулированных классов (`AlertDialog.Builder`, `BottomSheet`, `AndroidUtilities`, `Browser` и другие из раздела «Эмуляция Java и Android») перехватываются по-настоящему: `before_hooked_method` и `after_hooked_method` получают аргументы и результат, `param.setResult()` заменяет результат и отменяет вызов оригинала, `replace_hooked_method` заменяет метод целиком.

### API перехвата

Перехват устроен так же, как в exteraGram:

- `hook_method(метод, хук, priority)` возвращает `XC_MethodHook.Unhook`; `unhook.unhook()` или `self.unhook_method(unhook)` снимает перехват. `hook_all_methods` и `hook_all_constructors` возвращают список таких объектов. Если перехват на ПК невозможен, возвращается `None`, как у оригинала.
- Хук задаётся классом `MethodHook`, `MethodReplacement`, `XC_MethodHook`, `XC_MethodReplacement`, функцией или аргументами `before=` и `after=`.
- Порядок как в Xposed: хуки с большим `priority` выполняются раньше в `before_hooked_method` и позже в `after_hooked_method`. `param.setResult()` или `param.setThrowable()` в `before_hooked_method` отменяет вызов оригинала, остальные `after_hooked_method` выполняются.
- `MethodHookParam`: `method`, `thisObject`, `args` (изменяемый список), `getResult`, `setResult`, `getThrowable`, `setThrowable`, `hasThrowable`, `getResultOrThrowable`, `getObjectExtra`, `setObjectExtra`.
- Конструкторы перехватываются через `hook_all_constructors` или `getDeclaredConstructor()`; в `after_hooked_method` `param.thisObject` содержит созданный объект.
- Рефлексия эмулированных классов возвращает настоящие объекты: `getDeclaredMethods`, `getMethods`, `getDeclaredMethod`, `getDeclaredConstructors`, `getDeclaredFields`, `getDeclaredField`. У метода есть `getName`, `getParameterCount`, `getParameterTypes`, `getModifiers`, `invoke`.

### Фильтры

Фильтры отсекают вызовы до запуска кода хука, как `HookFilter` в exteraGram:

```python
from base_plugin import HookFilter, hook_filters, MethodHook

class OnlyTrue(MethodHook):
    @hook_filters(HookFilter.ArgumentIsTrue(0), HookFilter.Condition("param.thisObject != null"))
    def before_hooked_method(self, param):
        ...

self.hook_method(method, after=on_after, after_filters=[HookFilter.RESULT_NOT_NULL])
```

Поддерживаются все фильтры SDK: `ArgumentIsNull`, `ArgumentNotNull`, `ArgumentIsTrue`, `ArgumentIsFalse`, `ArgumentEqual`, `ArgumentNotEqual`, `ArgumentIsInstanceOf`, `RESULT_IS_NULL`, `RESULT_NOT_NULL`, `RESULT_IS_TRUE`, `RESULT_IS_FALSE`, `ResultEqual`, `ResultNotEqual`, `ResultIsInstanceOf`, `Or`, `Condition`. Условие `Condition` записывается на MVEL, как на Android: доступны `param`, `result`, `object`, `this`, поля `thisObject` по имени, полные имена классов (`org.telegram.messenger.NotificationCenter.didReceiveNewMessages`), операторы `&&`, `||`, `!`, `==`, `instanceof`, `null`, `true`, `false`.

### XposedHelpers

`findClass`, `findClassIfExists`, `findMethodExact`, `findMethodBestMatch`, `findConstructorExact`, `findAndHookMethod`, `findAndHookConstructor`, `findField`, `getObjectField` и `setObjectField` (а также варианты для `int`, `long`, `boolean` и других типов и статических полей), `callMethod`, `callStaticMethod`, `newInstance`, `setAdditionalInstanceField`, `getAdditionalInstanceField`, `removeAdditionalInstanceField` и статические варианты. `XposedBridge.hookMethod`, `hookAllMethods`, `hookAllConstructors`, `unhookMethod`, `invokeOriginalMethod`, `log`.

`PyMethodHook` и `PyMethodReplacement` из `com.exteragram.messenger.plugins.xposed` работают как в exteraGram, включая `setBeforeHookedFilters` и `setAfterHookedFilters`: такие хуки используют библиотеки вроде ZwyLib.

### Перехват действий клиента

Действия пользователя в exDesktop проходят через те же методы, что и на Android, поэтому их перехват срабатывает:

| Метод | Когда вызывается |
|---|---|
| `PluginsController.setPluginEnabled(id, enabled)` | включение или выключение плагина в списке |
| `PythonPluginsEngine.loadPlugin(id, path)` | загрузка плагина |
| `PythonPluginsEngine.unloadPlugin(id)` | выгрузка плагина, в том числе при удалении и обновлении |
| `LocaleController.applyLanguage(info, ...)` | смена языка через `applyLanguage` |

`param.setResult(None)` в `before_hooked_method` отменяет действие.

### Наследование Java-классов

`extera_utils.classes` позволяет унаследовать эмулированный класс на Python:

```python
from extera_utils.classes import Base, java_subclass, joverride, jfield
from android.widget import FrameLayout

@java_subclass(FrameLayout)
class Cell(Base):
    size = jfield("int", default=48)

    def on_post_init(self, context):
        self.setWillNotDraw(False)

    @joverride()
    def onMeasure(self, width_spec, height_spec):
        ...
```

`self.java` возвращает Java-объект, который передаётся в методы клиента; вызовы его переопределённых методов попадают в Python. Поддерживаются `joverride`, `joverload` (имя Java-метода отличается от имени в Python), `jmethod`, `jfield` с `jgetmethod` и `jsetmethod`, `jconstructor`, `jpreconstructor`, методы на MVEL (`jMVELmethod`, `jMVELoverride`), `Base.from_java`, `new_instance`, `class_proxy` с `ClassInterceptor`, `release_plugin` и `collect_peers`. Объекты плагина освобождаются при его выгрузке.

Исключение составляет `SendMessagesHelper.sendMessage`: такой хук подключается к конвейеру отправки exDesktop. В `before_hooked_method` первый аргумент `param.args[0]` содержит параметры отправки, изменение `message` меняет текст, `param.setResult(None)` отменяет отправку. `replace_hooked_method` тоже отменяет отправку.

Хук на `MessagesController.processNewMessages` или `NotificationsController.processNewMessages` вызывается для каждого нового сообщения. `param.args[0]` содержит список из одного `MessageObject` с полями `messageOwner.id`, `messageOwner.message`, `messageOwner.date`, `messageOwner.out`, `messageOwner.dialog_id`, `messageOwner.peer_id`, `messageOwner.from_id` и методами `getId()`, `getDialogId()`, `isOut()`, `getFromChatId()`. Отменить получение сообщения нельзя.

Хуки на события экранов выполняются по сигналам exDesktop:

| Хук Android | Когда вызывается на ПК | `param.thisObject` |
|---|---|---|
| `ChatActivity.onResume`, `createView`, `onFragmentCreate` | открытие чата | `ChatActivity` с `getDialogId()`, `getCurrentUser()`, `getCurrentChat()` |
| `ChatActivity.onPause`, `onFragmentDestroy` | переход в другой чат | то же |
| `ProfileActivity.onResume`, `createView` | открытие профиля | `ProfileActivity` с `getUserId()`, `getChatId()` |
| `DialogsActivity.onResume`, `createView` | закрытие чата, показ списка | `DialogsActivity` |
| `LaunchActivity.onActivityResult`, `BaseFragment.onActivityResultFragment` | после выбора файла | `param.args`: код запроса, `RESULT_OK` (-1) или `RESULT_CANCELED` (0), `Intent` с `getData()` |
| `ProfileActivity.onPause`, `onFragmentDestroy` | переход из профиля в чат | `ProfileActivity` |
| `DialogsActivity.onPause`, `onFragmentDestroy` | открытие чата из списка | `DialogsActivity` |
| `LaunchActivity.onCreate` | сразу после загрузки плагина | `LaunchActivity` |
| `LaunchActivity.onResume`, `onStart` | окно exDesktop стало активным | `LaunchActivity` |
| `LaunchActivity.onPause`, `onStop` | окно exDesktop потеряло фокус | `LaunchActivity` |
| `Browser.openUrl` | нажатие на любую внешнюю ссылку в клиенте | `param.args[1]`: адрес; `param.setResult(None)` не даёт открыть ссылку |
| `LaunchActivity.handleIntent` | открытие ссылки `tg://` в клиенте | `param.args[0]`: `Intent` с `getData()`; `param.setResult(True)` отменяет обработку ссылки |
| `MediaController.playMessage` | запуск аудио, голосового или видеосообщения в чате | `param.args[0]`: `MessageObject`; `param.setResult(False)` отменяет воспроизведение |
| `AudioPlayerAlert.updateTitle` | после запуска аудио | `AudioPlayerAlert` |
| `ChatActivityEnterView.setReplyingMessageObject` | выбор сообщения для ответа | `param.args[0]`: `MessageObject`; `param.setResult(None)` отменяет выбор |
| `VoIPHelper.startCall` | начало исходящего звонка | `param.args[0]`: пользователь `TL_user`, `param.args[1]`: видеозвонок; `param.setResult(None)` отменяет звонок |
| `ChatMessageCell.setMessageObject`, `setMessageContent` | построение текста сообщения в чате | `param.args[0]`: `MessageObject`; присваивание `messageText` или `messageOwner.message` меняет показанный текст (только на экране, сообщение не меняется) |
| `ChatAvatarContainer.updateSubtitle`, `setSubtitle` | обновление статуса в шапке чата | `ChatAvatarContainer` с `getDialogId()`, `setSubtitle()`, `getSubtitleTextView().setText()`; новый текст показывается вместо статуса |
| `ChatActivity.createMenu`, `fillMessageMenu` | правый клик по сообщению | `ChatActivity`; `param.args[0]`: ячейка с `getMessageObject()`; `param.setResult(None)` не даёт открыть меню |
| `TranscribeButton.onTap` | нажатие на кнопку расшифровки голосового или видеосообщения | `TranscribeButton` с `getMessageObject()`; `param.setResult(None)` отменяет расшифровку |
| `SendMessagesHelper.sendReaction` | постановка или снятие реакции | `param.args[0]`: `MessageObject`, `param.args[1]`: список `VisibleReaction` (`emojicon`, `documentId`), пустой при снятии; `param.setResult(None)` отменяет реакцию |
| `NotificationsController.showOrUpdateNotification` | показ уведомления о новом сообщении | `NotificationsController` с `pushMessages`; `param.setResult(None)` скрывает уведомление |
| `ChatAvatarContainer.onAvatarClick`, `ChatActivity.openProfile` | нажатие на шапку чата | `ChatAvatarContainer` с `getDialogId()`; `param.setResult(None)` не даёт открыть профиль |
| `MediaController.playNextMessage`, `playNextMessageWithoutOrder`, `playPreviousMessage` | переход к следующему или предыдущему треку, в том числе автоматический | `MediaController`; `param.setResult(None)` отменяет переход |
| `MediaController.cleanupPlayer` | закрытие плеера | `MediaController`; `param.setResult(None)` оставляет плеер открытым |
| `MediaController.setPlaylist` | запуск аудио, перед `playMessage` | `param.args[0]`: список сообщений, `param.args[1]`: текущее |
| `AudioPlayerAlert` (конструктор), `AudioPlayerAlert.onSubItemClick` | открытие полноэкранного плеера и выбор пункта его меню | `AudioPlayerAlert` с полем `optionsButton`; пункты из `optionsButton.addSubItem(id, icon, text)` показываются по правому клику в плеере, `setSubItemShown` скрывает пункт |
| `AndroidUtilities.openForView` | открытие файла из чата | `param.args[0]`: `MessageObject`; `param.setResult(None)` отменяет открытие |
| `LaunchActivity.onNewIntent` | открытие ссылки `tg://` | то же, что `handleIntent` |
| `MainTabsActivity.onResume`, `createView` | показ списка чатов | `DialogsActivity` |
| `ChatActivity.onTransitionAnimationEnd` | открытие чата | `ChatActivity` |
| `ProfileActivity.updateProfileData` | открытие профиля | `ProfileActivity` |
| `ChatMessageCell.setMessageObjectInternal` | построение текста сообщения | как у `setMessageObject` |
| `ChatActivity.getMessageContent` | «Копировать текст» в меню сообщения | `param.args[0]`: `MessageObject`; результат, заданный через `param.setResult(text)` в `after_hooked_method`, попадает в буфер обмена |
| `SettingsActivity.fillItems`, `SettingsActivity.onClick` | построение главного меню exDesktop и выбор пункта | `param.args[0]`: список `UItem` с пунктом `id == -1`; пункты, добавленные плагином (`UItem.asButton(id, icon, text)`), показываются в главном меню, клик вызывает `onClick` с `param.args[0]`: выбранный `UItem` |
| `ProfileActivity.updateRowsIds`, `ProfileActivity$ListAdapter.onBindViewHolder` | построение блока информации в профиле | у `ProfileActivity` есть поля `rowCount`, `phoneRow`, `usernameRow`, `bioRow` (у канала `channelInfoRow`), `infoHeaderRow`, `infoSectionRow`, `userId`, `chatId`, их можно читать и менять через рефлексию; `onBindViewHolder` получает `holder.itemView` типа `TextDetailCell`, `setTextAndValue` в строке телефона, юзернейма или описания заменяет текст, в новой строке (после увеличения `rowCount`) добавляет строку в конец блока |
| `ChatActivity.addToSelectedMessages` | выделение сообщения | `param.args[0]`: `MessageObject`; `param.setResult(None)` не даёт выделить |
| `ChatActivity.sendMedia`, `SendMessagesHelper.prepareSendingMedia`, `prepareSendingDocuments` | отправка файлов и фото | `param.args[0]`: первый `PhotoEntry` с `path` и `caption`, `param.args[5]`: все файлы; `param.setResult(None)` отменяет отправку |
| `DialogCell.update`, `buildLayout`, `setDialog` | построение имени чата в списке чатов | `DialogCell` с `currentDialogId`, `getCurrentUser()`, `getCurrentChat()`, `nameString`; изменение `first_name`, `last_name` пользователя из `MessagesController.getUser` или `title` чата в `before_hooked_method` меняет показанное имя, как в Android-плагинах локального переименования |
| `ChatAvatarContainer.setTitle`, `ChatActivity.updateTitle` | построение названия в шапке чата | `ChatAvatarContainer` с `getDialogId()`; изменённый `param.args[0]` или вызов `setTitle()` меняет показанное название |

Методы эмулированных классов перехватываются напрямую: хук на `BottomSheet.show`, `MediaController.playMessage` или любой другой реализованный метод срабатывает при каждом его вызове из плагина или из exDesktop. Получение метода через рефлексию (`find_class(...).getClass().getDeclaredMethod(name)`) возвращает тот же метод.

`MessageObject` в аргументах хуков содержит данные сообщения: тип (`type`, `isPhoto()`, `isVideo()`, `isVoice()`, `isMusic()`, `isSticker()`, `isGif()`, `isRoundVideo()`, `isDocument()`), документ (`getDocument()` с `id`, `mime_type`, `size` и атрибутами имени файла, аудио, видео и стикера), `getMusicTitle()`, `getMusicAuthor()`, `getDuration()`, `getStickerEmoji()`, сущности текста в `messageOwner.entities`, ответ (`isReply()`, `getReplyMsgId()`), пересылку (`isForwarded()`, `messageOwner.fwd_from`), `isEdited()`, `getGroupId()`, `isPost()`.

`MessagesController` отдаёт данные клиента: `getAllDialogs()`, `getDialogs(folder_id)`, `getDialog(id)` (`TL_dialog` с `unread_count`, `pinned`, `folder_id`, `top_message`), `getDialogFilters()` (папки с `name` и `dialogs`), `isDialogMuted`, `isForum`, и выполняет действия: `openByUserName`, `deleteMessages`, `pinDialog`, `markDialogAsRead`.

Поле ввода текущего чата доступно через `ChatActivity.getChatActivityEnterView()`: `getFieldText()`, `setFieldText(text)`, `hasText()`, `getEditField().getText()` (с `append`, `insert`, `replace`, `clear`) и `getEditField().setText(text)` читают и меняют текст поля в exDesktop.

`MediaController` управляет плеером exDesktop: `getPlayingMessageObject()`, `isPlayingMessage`, `isMessagePaused`, `playMessage`, `pauseMessage`, `resumeAudio`, `playNextMessage`, `playPreviousMessage`, `cleanupPlayer`, `seekToProgress`. `BottomSheet` показывает окно с `setCustomView`, `setTitle`, `show`, `dismiss`, `isShowing` и `setOnDismissListener`; `BottomSheet.Builder` поддерживает `setItems`: список открывается окном exDesktop с заголовком из `setTitle`, выбор пункта вызывает слушатель с номером пункта. `client_utils.get_media_controller()`, `get_notifications_controller()` и другие функции возвращают эти же классы.

Эти хуки работают с семантикой Xposed: все `before_hooked_method` в порядке приоритета, затем действие клиента, если его не отменили, затем `after_hooked_method`. Изменить интерфейс Android в них нельзя: обращения к его элементам ничего не делают.

Плагин, который ставит остальные хуки, отмечается в списке плагинов пометкой «Работает частично». По нажатию на пометку открывается список перехватов, недоступных на ПК.

Для переноса таких функций используется перенос плагина под ПК: логика переписывается на хуки запросов, хук отправки, меню и настройки. Порядок описан в руководстве по переносу для ИИ.

## Веб-окна плагинов

Плагин может показать свой интерфейс на HTML в отдельном окне exDesktop. Это замена мобильных всплывающих окон с `WebView`:

```python
from ui.webapp import WebApp

app = WebApp("Статистика", html="<button onclick=\"exd.send({click: 1})\">OK</button>",
             width=480, height=640, on_message=lambda data: app.send({"reply": data}))
app.show()
```

- В странице доступен объект `exd`: `exd.send(data)` передаёт данные в `on_message` плагина, `exd.onMessage(fn)` получает данные из `app.send(data)`, `exd.close()` закрывает окно. `app.eval(js)` выполняет JavaScript, `app.close()` закрывает окно, `on_close` и `on_load` вызываются при закрытии и загрузке.
- `android.webkit.WebView` работает так же, как на Android: `loadData`, `loadDataWithBaseURL`, `loadUrl` (только `https://`), `addJavascriptInterface` (вызовы из JS выполняются асинхронно), `evaluateJavascript`, `WebViewClient.shouldOverrideUrlLoading` и `onPageFinished`. `WebView` внутри `AlertDialog`, `BottomSheet` или `AlertDialogBuilder.set_view` открывается в веб-окне.
- Защита: окно открывается от имени плагина, которому оно принадлежит. Сетевые запросы из страницы разрешены только по HTTPS и запрещены полностью, если плагину отключён интернет в разрешениях. Переход по ссылке передаётся плагину, иначе ссылка открывается в браузере с подтверждением, как обычная ссылка из плагина. Окна закрываются при выключении плагина.

## Виджеты Android

Кроме `TextView`, `EditText`, `ImageView`, `LinearLayout`, `FrameLayout` и `ScrollView` в окнах плагинов отображаются `Button`, `CheckBox`, `Switch` (`setChecked`, `setOnCheckedChangeListener`), `SeekBar` (`setMax`, `setProgress`, `OnSeekBarChangeListener`) и `ProgressBar` (в том числе `setIndeterminate`). `TextWatcher` у `EditText` получает `beforeTextChanged`, `onTextChanged` и `afterTextChanged`.

Контекст Android (`ApplicationLoader.applicationContext`, `LaunchActivity`) возвращает рабочие объекты: `getClassLoader().loadClass`, `getResources()` (`getIdentifier`, `getDisplayMetrics`, `getConfiguration`, `getString`, `getColor`), `getPackageManager().getPackageInfo`, `getWindow()`, `getSystemService` для `window`, `connectivity`, `audio`, `notification` (уведомление показывается плашкой в клиенте), `power`, `input_method`, `activity`, `clipboard`.

## Выбор чатов и пользователей

Экраны выбора из Android открываются окном выбора exDesktop, когда плагин показывает их через `presentFragment`:

- `DialogsActivity(args)` с делегатом `DialogsActivityDelegate`: выбор одного чата. Аргументы `allowUsers`, `allowBots`, `allowGroups`, `allowChannels` ограничивают список, `selectAlertString` задаёт заголовок. После выбора вызывается `didSelectDialogs` со списком из одного `TopicKey` (`dialogId`, `topicId`).
- `GroupCreateActivity` с делегатом `GroupCreateActivityDelegate`: выбор нескольких пользователей, групп и каналов, `select(ids)` отмечает выбранных заранее. После выбора вызывается `didSelectUsers` со списком id.

При закрытии окна без выбора делегат не вызывается. `presentFragment(PluginSettingsActivity(plugin))` открывает окно настроек плагина.

## Перенос при помощи ИИ

В карточке плагина с Android-хуками, DEX-кодом или ошибкой загрузки и в окне его установки есть действие «Перенести при помощи ИИ». Перед запуском открывается окно с объёмом запроса и подсказками, в нём выбираются сервис, модель, ключ API и режим проверки. Те же параметры задаются в настройках движка плагинов, раздел «ИИ-перенос плагинов»:

| Сервис | Ключ API | Адрес по умолчанию |
|---|---|---|
| ChatGPT (OpenAI) | нужен | `api.openai.com` |
| Claude (Anthropic) | нужен | `api.anthropic.com` |
| OpenRouter, Groq, DeepSeek | нужен | адрес сервиса |
| Ollama | не нужен | `localhost:11434` |
| LM Studio | не нужен | `localhost:1234` |
| Свой адрес | по необходимости | OpenAI-совместимый API |
| Gemini (Google) | нужен, есть бесплатный ключ Google AI Studio | `generativelanguage.googleapis.com` |

Используется только ключ пользователя. Модель и адрес можно задать вручную. Бесплатных тарифов Groq и OpenRouter (несколько тысяч токенов на запрос) хватает только на маленькие плагины. Для Ollama по умолчанию используется `qwen2.5-coder:7b` с контекстом 32 тысячи токенов.

Порядок работы:

1. Клиент отправляет модели короткую инструкцию, подсказки по найденным в плагине перехватам, подходящие разделы справочника и исходный код без комментариев и двоичных вставок. Модель может запросить дополнительные разделы строкой `NEED: раздел`.
2. Ответ проверяется: синтаксис, импорты из модулей SDK, метаданные (`__id__` с суффиксом `_exd`, `__desktop__`, `__ported_from__`, `__desktop_os__`), запрещённые модули и вызовы, пути Android, новые сетевые адреса, критичные находки PluginGuard. Недостающие импорты SDK дописываются, а неподдерживаемые перехваты удаляются автоматически, без лишней попытки.
3. В режиме «С проверкой и исправлением» ошибки возвращаются модели, до четырёх попыток. В режиме «Один запрос» результат с ошибками отклоняется сразу.
4. Прошедший проверку плагин сохраняется в папку `ai_ports` и устанавливается отдельно от оригинала через обычное окно установки с проверкой PluginGuard. В окне установки и в карточке он отмечен «Перенесено с Android при помощи ИИ».

Слабые и локальные модели переносят хуже: часть функций может не сохраниться.

## Защита

Плагин выполняется в защитной среде. Она ограничивает доступ к файлам, сети и системе и не зависит от того, что написано в коде плагина.

### Файлы

| Где | Доступ |
|---|---|
| Своя папка плагина (`file_utils.get_plugin_dir()`), свой кэш, общие папки медиа плагинов | чтение и запись |
| Папки, в имени которых есть id плагина, внутри папки файлов и домашней папки | чтение и запись |
| Библиотеки Python, SDK, установленные зависимости | только чтение |
| Данные Telegram и exDesktop (`tdata`), чужие плагины и их данные, настройки плагинов | запрещено всегда |
| Профили браузеров, кошельки, ключи SSH и GPG, менеджеры паролей, Discord, Steam | запрещено всегда |
| Остальные папки (Рабочий стол, Загрузки, Документы и другие) | по разрешению пользователя |

При обращении к обычной папке пользователь видит окно «Доступ к файлам» с кнопками «Разрешить на свой страх и риск» и «Запретить». Разрешение запоминается для папки.

Пути Android перенаправляются в папку плагина: `/sdcard/...`, `/storage/emulated/0/...`, `/data/user/0/com.exteragram.messenger/...`.

### Выбор файла

`file_utils.pick_file(callback, title="", filters="", multiple=False)` открывает системный диалог, `callback` получает список путей. Путь Android тоже работает: `startActivityForResult` с `Intent.ACTION_GET_CONTENT`, `ACTION_OPEN_DOCUMENT` или `ACTION_PICK` открывает диалог, результат приходит в хук `onActivityResult`, файл читается через `getContentResolver().openInputStream(uri)` или по `uri.getPath()`. Плагин получает доступ только к выбранным файлам, до перезапуска клиента.

Относительный путь в `open()` и `sqlite3.connect()` отсчитывается от папки плагина, как и на Android. `getFilesDir()`, `getFilesDirFixed()` и `getCacheDir()` возвращают папку плагина и его кэш. Базы SQLite проверяются по тем же правилам, что и обычные файлы.

### Сеть

Плагин подключается к сайтам в интернете без запросов. Подключения к `localhost` тоже разрешены. Разрешение пользователя нужно в двух случаях:

- подключение к устройству в локальной сети (частные адреса, имена `.local`, `.lan`, `.home`, `.internal`): окно «Запрос доступа к локальной сети»;
- отправка UDP-пакетов на внешний адрес: окно «Запрос отправки UDP-пакетов».

В обоих окнах есть кнопки «Разрешить всегда», «Один раз» и «Запретить», решение запоминается для пары «плагин и адрес». Открыть порт для входящих подключений из сети нельзя.

### Защищённые данные

Всё, что плагин отправляет в сеть или в Telegram (`send_message`, `send_request`, `edit_message`, отправка файлов), проверяется на защищённые данные:

- ключи сессии Telegram;
- код входа в Telegram;
- номер телефона аккаунта и номера телефонов контактов;
- приватные ключи и сид-фразы криптокошельков;
- данные браузера;
- данные компьютера (аппаратные адреса и идентификаторы);
- токены ботов и API-ключи, если в настройках движка плагинов включена «Защита токенов ботов и API-ключей».

При совпадении отправка останавливается до решения пользователя. В окне «Отправка защищённых данных» указано, что и куда отправляется, и есть кнопки «Запретить», «Один раз», «Разрешить всегда» и «Отключить плагин». Если ответить некому (неизвестный источник вызова), отправка блокируется.

### Разрешения и журнал

В меню плагина есть раздел «Разрешения и журнал».

- «Разрешения»: доступ в интернет целиком, буфер обмена, уведомления, открытие ссылок без подтверждения, а также выданные решения по адресам, UDP, защищённым данным и папкам. Нажатие на строку сбрасывает решение, после чего плагин запросит его повторно. Кнопка «Сбросить все разрешения» очищает всё сразу.
- «Журнал»: подключения к адресам, открытые папки и заблокированные действия (запуск программ, прямой доступ к системе, чтение реестра, отправка защищённых данных). Журнал ведётся с момента запуска клиента. Из журнала можно сразу запретить подключения к адресу.

Если у плагина нет своих настроек, кнопка настроек в карточке всё равно показывается ради этого раздела.

### Система

Запрещены запуск программ (`subprocess`, `os.system`, `os.startfile`), вызов системных функций через `ctypes`, запись в реестр Windows и доступ к внутренностям интерпретатора (`gc.get_objects`, `sys.settrace`, создание байткода вручную). Попытка блокируется, пользователь видит плашку с названием плагина.

Попытки обойти защиту отмечаются проверкой PluginGuard как критическая угроза.

## Проверка PluginGuard

Перед установкой каждый плагин проверяется без запуска его кода. Проверка идёт в три этапа:

1. Статические правила разбирают код: сеть, файлы, вызовы системы, работа с данными аккаунта, обфускация.
2. Векторная модель сравнивает фрагменты кода с базой чистых и вредоносных плагинов.
3. ИИ-арбитр получает находки и спорные фрагменты и отделяет реальную угрозу от обычной функциональности. Арбитр включается в настройках движка плагинов: сервис (Groq, OpenRouter, DeepSeek, OpenAI, Mistral) и свой ключ API.

Итог показывается одним из уровней: «угроз нет», «низкий риск», «средний риск», «критическая угроза». Плагин с критической угрозой не устанавливается и помечается нежелательным, повторная установка того же файла тоже невозможна.

Установку блокируют:

- явная обфускация кода;
- поведение, характерное для вредоносных плагинов (сходство с базой в нескольких опасных фрагментах);
- отправка защищённых данных (сессии, кодов входа, ключей) на сторонние адреса;
- дропперы: плагин скачивает файл, сохраняет его и запускает или подключает;
- загрузка кода во время работы: плагин скачивает DEX, нативную библиотеку, Python-модуль или другой плагин и загружает его. Такой код нельзя проверить до установки, поэтому плагин блокируется, даже если сам загрузчик выглядит безобидно;
- встроенные исполняемые файлы Windows или Linux вместе с кодом их загрузки.

Одно лишь сходство с вредоносными плагинами установку не блокирует, если ИИ-арбитр признал плагин безопасным.

### Зашифрованные плагины

Зашифрованный или упакованный код (base64, zlib, XOR, вложенные `exec` и подобные слои) расшифровывается статически, без выполнения. Расшифровка ограничена по глубине, объёму и времени.

- Если код не удалось расшифровать полностью, плагин блокируется.
- Полностью расшифрованный код проверяется теми же правилами. Плагин устанавливается, только если в расшифрованном коде нет находок среднего уровня и выше и ИИ-арбитр признал его безопасным. Без настроенного арбитра такой плагин не устанавливается.

Окно просмотра кода в окне установки показывает расшифрованный код под исходным. Двоичные вставки (DEX, нативные библиотеки) заменяются строкой с их типом и размером.

## Сервер разработки

Переключатель «Сервер разработки плагинов» в настройках движка плагинов запускает `dev_server.DevServer` на `127.0.0.1:42690`. Протокол совпадает с exteraGram, поэтому клиент горячей перезагрузки из `exteragram-utils` работает без изменений (команда `adb forward` на ПК не нужна): сообщения `{"@": команда, "#": номер, ...}`, ответ содержит тот же `#` и `success`.

| Команда | Аргументы | Действие |
|---|---|---|
| `ping` | | проверка соединения |
| `get_plugins` | | список установленных плагинов с именем, версией и состоянием |
| `write_plugin` | `plugin_id`, `content` | установка или обновление плагина после проверки PluginGuard; критичные и высокие угрозы блокируют установку |
| `reload_plugin` | `plugin_id` | перезапуск плагина |
| `enable_plugin`, `disable_plugin` | `plugin_id` | включение и выключение |
| `remove_plugin` | `plugin_id` | удаление |
| `start_debugger`, `stop_debugger` | `host`, `port`, `platform` (`vscode` или `pycharm`) | подключение отладчика (`debugpy` для VS Code) |

Сервер принимает только локальные подключения. Запустить его или отладчик может только exDesktop: вызов `DevServer.start_server()` из плагина отклоняется, а импорт `dev_server` в плагине отмечается проверкой.

## Elyx

Поддерживаются:

- `refmap.yaml`, `refmap.yml`, `refmap.json` с ключами `main`, `metainfo`, `assets`, `strings`, `wheels`; ключи без значения получают значения по умолчанию;
- метаданные YAML и JSON, подстановка `{ключ}` из строк локализации в названии и описании;
- отдельное пространство модулей `ElyxPlugins.<id>`: локальные модули разных плагинов с одинаковыми именами не пересекаются;
- обычный и относительный импорт локальных модулей, импорт JSON, YAML и TXT как модулей, `elyx.import_module`;
- `from elyx import metainfo, refmap, assets, strings, settings, import_module, get_environment`, классы `Strings`, `SettingsController`, `Assets`, `Asset`;
- колёса `.whl` из папки `wheels`;
- проверка зависимостей `requires` с минимальной версией.

Не поддерживаются зашифрованные архивы и файлы `.dex`. Методы ассетов, которые на Android создают `Drawable` (`to_drawable`, `to_svg_drawable` и другие), возвращают путь к файлу.

## Windows, Linux и macOS

Движок плагинов одинаково работает на всех трёх системах. Встроенные пакеты (`requests`, `beautifulsoup4`, `lxml`, `pillow`, `PyYAML`, `packaging`) входят в сборки для всех систем. Зависимости `__requirements__` с нативным кодом устанавливаются из колёс для текущей системы и процессора.

Чтобы плагин работал везде:

- пути строятся через `os.path.join` и `file_utils.get_plugin_dir()`, без обратной косой черты, букв дисков, `%APPDATA%`, `/home/...` и `~/Library`;
- имена файлов на Linux различают регистр;
- текстовые файлы открываются с `encoding="utf-8"`;
- модули одной системы (`winreg`, `msvcrt`, `winsound`, `fcntl`, `termios`) импортируются внутри `try/except ImportError`.

ИИ-перенос проверяет эти правила и указывает систему, для которой плагин перенесён, в `__desktop_os__`.

При ошибке запуска движка её текст выводится в разделе плагинов настроек, а полный трейс Python записывается в `log.txt`.

## Отличия от Android

- Нет Java и Chaquopy: модули `java`, `android`, `org.telegram` эмулируются.
- Нет Xposed: срабатывают только перехваты из раздела «Хуки методов» и методы эмулированных классов.
- Нет `View` Android: собственные экраны и `Custom` в настройках отображаются в упрощённом виде.
- Плагин не может запускать программы и загружать нативные библиотеки.
- Плагин не может скачивать и загружать код во время работы: такие плагины не проходят проверку PluginGuard.
- Файлы вне своей папки доступны только по разрешению пользователя.
