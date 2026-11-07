# SDK плагинов exDesktop

exDesktop запускает плагины exteraGram на компьютере. Движок плагинов совместим с SDK exteraGram 1.4.x: плагин из каталога exteraGram устанавливается без изменений, а его функции работают в той мере, в которой у них есть аналог на ПК.

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
- `ui.bulletin.BulletinHelper` и `ui.alert.AlertDialogBuilder`.
- `file_utils`: папки плагина, чтение, запись, список файлов.
- `extera_utils.text_formatting.parse_text` с режимами HTML и Markdown.
- `intents`: обработка ссылок `tg://`.
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
| `de.robv.android.xposed` | `XposedBridge` (`log`, `hookMethod`, `hookAllMethods`), `XC_MethodHook` |
| `org.telegram.tgnet` | `TLRPC`, `TLObject`, `ConnectionsManager`, `RequestDelegate` |

`LocaleController.getInstance().getCurrentLocale().getLanguage()` и `Locale.getDefault()` возвращают язык интерфейса exDesktop.

Классы Telegram, которые работают по-настоящему:

- `NotificationCenter.getInstance(account).addObserver(delegate, NotificationCenter.didReceiveNewMessages)`: делегат получает `didReceivedNotification(id, account, dialog_id, messages, scheduled)` для каждого нового сообщения. `postNotificationName` передаёт собственные уведомления между плагинами.
- `SendMessagesHelper.getInstance(account).sendMessage(SendMessageParams.of(text, dialog_id))` отправляет текст, `prepareSendingDocument` и `prepareSendingPhoto` отправляют файл.
- `PluginsController.getInstance()`: список `plugins`, `getPlugin`, `isPluginEnabled`, `setPluginEnabled`.
- `Context.getSystemService(Context.CLIPBOARD_SERVICE).setPrimaryClip(ClipData.newPlainText(label, text))` копирует текст в буфер обмена.
- `BitmapFactory` читает изображения через Pillow, `Bitmap.compress` сохраняет их в PNG, JPEG или WEBP.
- `R.drawable.имя` и `R.string.имя` возвращают имя ресурса.
- `FileLoader` возвращает пути в кэше плагина: файлы Telegram для плагинов не загружаются.

Эмулированные классы находятся и через `find_class`, `jclass` и `Class.forName`, а не только через `import`. У них есть методы рефлексии (`getName`, `getDeclaredMethods`, `getDeclaredMethod`), поэтому плагин может передать такой метод в `hook_method`. `org.telegram.ui.ActionBar.AlertDialog.Builder` с `setMessage`, `setItems`, `setPositiveButton` и другими кнопками показывает настоящее окно exDesktop, `LayoutHelper.createLinear` и `createFrame` задают размеры элементов.

Класс, которого нет в таблице, заменяется заглушкой: обращение к нему не вызывает ошибку, но и ничего не делает. Плагин продолжает работать, а функция, завязанная на такой класс, недоступна.

## Хуки методов (Xposed)

На Android плагин может перехватить любой Java-метод Telegram (`hook_method`, `hook_all_methods`, `hook_all_constructors`). В exDesktop нет Java-классов Android-клиента, поэтому срабатывают только перехваты из списка ниже и перехваты методов эмулированных классов. Остальные вызовы проходят без ошибки, но перехват не выполняется. Функции, построенные на перехвате интерфейса Android (ячейки сообщений, отрисовка, камера), на ПК недоступны.

Методы эмулированных классов (`AlertDialog.Builder`, `BottomSheet`, `AndroidUtilities`, `Browser` и другие из раздела «Эмуляция Java и Android») перехватываются по-настоящему: `before_hooked_method` и `after_hooked_method` получают аргументы и результат, `param.setResult()` заменяет результат и отменяет вызов оригинала, `replace_hooked_method` заменяет метод целиком.

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

Изменить интерфейс Android в этих хуках нельзя: обращения к его элементам ничего не делают.

Плагин, который ставит остальные хуки, отмечается в списке плагинов пометкой «Работает частично». По нажатию на пометку открывается список перехватов, недоступных на ПК.

Для переноса таких функций используется перенос плагина под ПК: логика переписывается на хуки запросов, хук отправки, меню и настройки. Порядок описан в руководстве по переносу для ИИ.

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

Первое подключение плагина к сайту вызывает окно «Доступ в интернет» с кнопками «Разрешить всегда», «Один раз» и «Запретить». Решение запоминается для пары «плагин и сайт». Подключения к `localhost` разрешены без вопроса. Открыть порт для входящих подключений из сети нельзя.

### Управление разрешениями

Выданные разрешения показываются в окне настроек плагина, раздел «Разрешения»: сайты с отметкой «Разрешено» или «Запрещено» и папки. Нажатие на строку сбрасывает разрешение, после чего плагин запросит его повторно. Кнопка «Сбросить все разрешения» очищает всё сразу. Если у плагина нет своих настроек, но есть разрешения, кнопка настроек в карточке всё равно показывается.

### Система

Запрещены запуск программ (`subprocess`, `os.system`, `os.startfile`), вызов системных функций через `ctypes`, запись в реестр Windows и доступ к внутренностям интерпретатора (`gc.get_objects`, `sys.settrace`, создание байткода вручную). Попытка блокируется, пользователь видит плашку с названием плагина.

Попытки обойти защиту отмечаются проверкой PluginGuard как критическая угроза.

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
- Файлы вне своей папки доступны только по разрешению пользователя.
