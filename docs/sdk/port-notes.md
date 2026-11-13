# Совместимость плагинов каталога exteraGram

Журнал для документации и для переноса плагинов нейросетью. Данные собраны стендом совместимости: каждый плагин загружается в изолированной песочнице Windows Sandbox без сети, затем вызываются его настройки, пункты меню и хук отправки.

Корпус: 478 файлов из каталога exteraGram, из них 466 совпадают по SHA-256 со списком доверенных хэшей exDesktop.

## Результаты прогонов

| Прогон | ok | Частично | Ошибка в настройках или меню | Не загрузился | Зависание |
|---|---|---|---|---|---|
| 1, без защиты | 53,9% | | | | |
| 4, с песочницей | 54,4% | 27,6% | 5,0% | 10,3% | 11 |
| 5 | 55,6% | 28,5% | 5,2% | 10,3% | 0 |
| 6 | 57,1% | 28,7% | 4,4% | 9,4% | 0 |

В прогоне 6 загружаются и работают полностью или частично 85,8% плагинов. «Частично» означает, что плагин ставит перехваты интерфейса Android, которые на ПК не выполняются; остальные его функции работают.

Оставшиеся ошибки: встроенный DEX-код, нативные библиотеки, пакеты pip (на стенде нет сети), библиотеки-плагины, которые на стенде не установлены, и частные случаи.

## Классы результата

| Класс | Смысл |
|---|---|
| ok | загрузился, настройки, меню и хук отправки отработали без ошибок, Xposed не использует |
| partial_xposed | загрузился, но часть функций держится на перехвате Java-методов и на ПК не работает |
| exercise_error | загрузился, но настройки, меню или хук отправки завершились ошибкой |
| load_error | не загрузился |
| timeout | загрузка заняла больше 30 секунд |

## Потолок ПК

Эти плагины не заработают без переписывания, потому что их функция существует только на Android.

### Встроенный Android-код (DEX)

Плагин несёт внутри скомпилированный Java-код и загружает его через `InMemoryDexClassLoader`. На ПК Java-машины нет.

`local_gift_hider`, `mention_avatars`, `monocolorui`, `plugstonav`, `wp`, `advanced_search`, `ayu_rofls`, `camera_enhancer`, `floating_attach_button`, `wide_posts`, `spam_collapse`, `bot_login`, `custom_profile`, `motion_blur_fx`, `fuck_now_playing`, `jpeg_quality`, `QPSwipe`.

Подсказка для переноса: определить, что делает DEX-часть, и заменить её хуком запроса или пунктом меню. Если DEX меняет отрисовку интерфейса, функцию переносить не нужно.

### Нативные библиотеки

Плагин загружает `.so` через `ctypes`: эмуляторы, обработка звука, системные утилиты Android. Песочница запрещает загрузку нативного кода.

`exitfy`, `exitFy_v2`, `extera_doom_native`, `nes_emulator`, `rescue_call`, `shareui_accountunread`, `shareui_bettersearch`, `voice_changer_rt`, `voice_changer_rust`, `unlim_accounts`.

### Перехват Android-интерфейса

Около 27% плагинов ставят хуки на классы интерфейса Android: `ChatActivity`, `ChatMessageCell`, `DialogsActivity`, `ChatAttachAlert`, `PhotoViewer`, `ProfileActivity`, камера. Плагин загружается, но перехват не выполняется. Самые частые цели:

- `ChatActivity` (27 плагинов), `DialogsActivity` (20), `ChatAttachAlertPhotoLayout` (18), `ChatMessageCell` (14), `LaunchActivity` (14), `MediaController` (14), `ProfileActivity` (11), `PhotoViewer` (11), `ChatActivityEnterView` (10), `InstantCameraView` (10).

Подсказка для переноса: кнопка в шапке чата переносится в `CHAT_ACTION_MENU`, действие над сообщением в `MESSAGE_CONTEXT_MENU`, реакция на новые сообщения в хук `TL_updateNewMessage`, изменение отправки в `on_send_message_hook`. Изменения отрисовки (ячейки, анимации, цвета) не переносятся.

## Частые цели Xposed

Подсчёт по корпусу: какие методы Android-клиента плагины перехватывают чаще всего (число плагинов).

| Метод | Плагинов | На ПК |
|---|---|---|
| `onResume`, `onPause`, `createView` фрагментов | 15, 7, 19 | нет, жизненный цикл экранов Android |
| `onDraw`, `dispatchDraw`, `onMeasure`, `onLayout` | 7, 4, 5, 4 | нет, отрисовка |
| `onActivityResult` | 11 | нет |
| `MessagesController.processNewMessages` | 6 | да, виртуальный хук с эмуляцией `MessageObject` |
| `SendMessagesHelper.sendMessage` | 6 | да, виртуальный хук |
| `UniversalAdapter.fillItems`, `updateRowsIds` | 11, 5 | нет, списки настроек Android |
| `UserConfig.isPremium` | 3 | не нужен, в exDesktop свои настройки Premium |

Перехваты интерфейса составляют основную долю. Их функции переносятся только переписыванием плагина: через ИИ-перенос или вручную.

## Зависимости

### Другие плагины

Плагин импортирует библиотеку, которая сама является плагином. На стенде такие плагины падают, потому что загружаются по одному; в клиенте они работают, если библиотека установлена.

| Плагин | Нужна библиотека |
|---|---|
| `GiftParser`, `morefeatures` | `cactuslib` |
| `QuickSettings`, `debug` | `plugins_manager` |
| `zwyNoForwardLimit`, `zwyVolumeNormalizer` | `zwylib` |
| `nowfy` | пакет `nowfy` |

### Пакеты pip

На стенде нет сети, поэтому пакеты из `__requirements__` не ставятся. В клиенте они устанавливаются при первом запуске.

`cachetools` (`QuantaHut`, `ui_tweaks`), `mutagen` (`extended_message_details`, `femboyaudioscope`), `tinydb` (`remind`), `mpmath` (`shareui_advcalc`), `yt-dlp` (`downloader`, `playlist_to_profile`), `websocket-client` (`edge_tts_voiceover`), `tenacity` (`icon_themer_plugin`).

## Исправлено в SDK по результатам стенда

| Проблема | Плагины | Исправление |
|---|---|---|
| `ChatActivity.getClass()` на классе | 2 | `getClass` работает и у класса, и у объекта |
| `tdata` в слове `setData`, `metadata` | Media Downloader и др. | PluginGuard ищет имена файлов сессии целыми словами |
| Относительные пути и пути Android | `clawd_pet`, `quick_note2`, `stella`, `culprit_detector`, `shimeji`, `msg_stats` | пути перенаправляются в папку плагина в `open()` и функциях `os` |
| Общие файлы в папке плагинов (`cache`, `memes_db.json`) | `coin_flip`, `crypto_converter`, `crypto_analiz`, `dateaccount`, `meme_library`, `plugins_manager` | разрешены, кроме чужих исходников и папок данных других плагинов |
| Расширение `.Plugin` в верхнем регистре | `SendSound`, `Shtrix` | расширение проверяется без учёта регистра |
| `self.version`, `self.author` у плагина | `swag safe` | атрибуты берутся из метаданных |
| `RectF()` без аргументов, `setTypeface(tf, style)` | `vanguard`, `block_blast` | приведено к сигнатурам Android |
| Неизвестные методы `View`, `BottomSheet` | `chat_stats`, `extera_clean` | возвращают заглушку вместо ошибки |
| Лишние аргументы в `Switch`, `Input` и др. | `Local_Chat_Cleaner` | лишние аргументы сохраняются и не ломают загрузку |
| `java.util.Calendar` | `local_time_shift` | реализован |
| Бесконечный цикл по `getSuperclass()`, `getParent()`, `poll()`, `InputStream.read()` у заглушки | `AdBlock`, `Combined_Custom_Banner`, `QPS`, `auto_marquee`, `circlet`, `custom_fab`, `forward_pro`, `hide_forum_topics`, `quick_back`, `quotify_plus`, `similar_nft` | методы, которыми заканчивается обход, возвращают `None`, `-1` или `False`; `getDeclaredMethod` у неизвестного класса сохраняет имя метода |
| `hook_method` возвращал `None`, плагин считал перехват неудачным | `ayu_yandex_maps`, `open_street_map` | возвращается объект отмены перехвата |
| `BasePlugin` без импорта | `overlay_disabler`, `pokedex` | имена `base_plugin` доступны в модуле плагина без импорта |
| `ChatActivity | Stub` в аннотациях типов | `ios_like_selection` | заглушка поддерживает объединение типов |
| `getFilesDir()` возвращал пустой путь, SQLite не открывал базу | `quick_note2` | `getFilesDir()`, `getCacheDir()` указывают на папку плагина, `sqlite3.connect` переносит относительный путь |
| `setTextSize(unit, size)`, `Gravity.END` | `QPS`, `stella`, `block_blast` | добавлены |
| `Switch` без `key` и `default` | `Local_Chat_Cleaner` | недостающие поля получают пустые значения |
| Xposed-хук на `SendMessagesHelper.sendMessage` | 6 плагинов | обработчик подключается к конвейеру отправки exDesktop: изменение `param.args[0].message` меняет текст, `param.setResult(None)` отменяет отправку |

## Открытые вопросы

- `user_info_saver` импортирует закрытую функцию `client_utils._prepare_document`.
- `memory_monitor_pro` импортирует модуль `elyx`, хотя сам однофайловый.
- `meeow_reswaga.elyx` помечен как обфусцированный: проверить, не ложное ли срабатывание.
