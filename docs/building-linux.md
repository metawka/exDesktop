# Инструкция по сборке exDesktop под Linux (x86_64)

exDesktop — это нативный клиент Telegram с расширенными функциями (меню настроек, лента каналов, режим стримера, интеграция с Gemini и движок Python-плагинов).

---

## 1. Предварительные требования и зависимости

### Системные пакеты (для нативной сборки на хосте)
- **Компилятор C++:** `GCC >= 12` или `Clang >= 16` (требуется поддержка стандарта C++20)
- **Сборщик:** `CMake >= 3.25` и `Ninja`
- **Python:** `Python >= 3.10` с заголовочными файлами (`python3-dev` / `python3-devel`)
- **Qt6:** `qtbase`, `qtsvg`, `qtimageformats`, `qtwayland` (с поддержкой Wayland и X11)
- **Медиа и кодеки:** `ffmpeg`, `libvpx`, `openh264`, `opus`, `rnnoise`, `openal`
- **Системные библиотеки:** `openssl`, `libfido2`, `xxhash`, `lz4`, `cmark-gfm`, `ada`, `minizip`, `kcoreaddons`

Для Debian/Ubuntu:
```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build python3-dev \
    qt6-base-dev qt6-base-private-dev qt6-wayland-dev libqt6svg6-dev \
    libavcodec-dev libavformat-dev libswscale-dev libopus-dev libvpx-dev \
    libssl-dev libfido2-dev libxxhash-dev liblz4-dev libopenal-dev
```

Для Fedora/RHEL:
```bash
sudo dnf install -y gcc-c++ cmake ninja-build python3-devel \
    qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtwayland-devel qt6-qtsvg-devel \
    ffmpeg-free-devel libvpx-devel opus-devel openssl-devel libfido2-devel
```

Для Arch Linux:
```bash
sudo pacman -S --needed base-devel cmake ninja python qt6-base qt6-wayland qt6-svg \
    ffmpeg libvpx opus openssl libfido2 xxhash lz4 openal
```

---

## 2. API Credentials Telegram

Без своих ключей сборка использует тестовые ключи tdesktop (TEST ONLY). Для релизов получите свои на https://my.telegram.org и передайте их через переменные окружения:
```bash
export TDESKTOP_API_ID="..."
export TDESKTOP_API_HASH="..."
```

---

## 3. Проверка движка плагинов

Движок `extera_plugins` на Linux использует нативный Python C API (`Python.h`, `PyConfig_InitPythonConfig`), что исключает зависимость от Windows DLL (`python311.dll` / `LoadLibraryW`).

Для автономной верификации работы движка запустите встроенный тест:
```bash
./build_linux.sh test
```

Тест проверяет:
1. Регистрацию встроенного C++ модуля `_extera`.
2. Корректную инициализацию Python рантайма в среде Linux.
3. Поиск и импорт хост-модуля `extera_host.py`.
4. Загрузку плагинов формата `.plugin`.
5. Работу перехватчиков сообщений (`send_hook`) и модификацию исходящего текста.
6. Корректное завершение работы (`shutdown`).

---

## 4. Сборка клиента

### Способ А: Сборка в официальном изолированном Docker-контейнере (Рекомендуется)
Этот способ гарантирует сборку всех 36 сабмодулей (кастомный WebRTC, tgcalls, патченный Qt6) без засорения хост-системы:

```bash
# Клонирование сабмодулей
git submodule update --init --recursive

# Запуск герметичной сборки
./build_linux.sh docker
```

Бинарник будет создан в каталоге: `out/Release/root/Telegram`.

### Способ Б: Нативная сборка на хосте
Если в вашей системе уже установлены библиотеки `telegram-desktop` (например, Arch, Fedora, openSUSE или Gentoo):

```bash
# Инициализация необходимых сабмодулей (lib_ui, cmake_helpers)
git submodule update --init Telegram/lib_ui cmake

# Конфигурация и компиляция
./build_linux.sh configure
./build_linux.sh build
```

Готовый бинарник будет доступен по пути `out/bin/Telegram`.

---

## 5. Расположение плагинов и данных в Linux

- **Код хоста и вспомогательные скрипты Python:**
  - `<папка с бинарником>/python/extera/`
  - или `~/.local/share/exDesktop/python/extera/`
  - или `/usr/share/exDesktop/python/extera/`
- **Пользовательские плагины:**
  - `~/.local/share/exDesktop/tdata/plugins/` (файлы с расширением `.plugin`)
- **Настройки плагинов:**
  - `~/.local/share/exDesktop/tdata/settings/`

---

## 6. Наложение патча на чистый исходный код

Полный патч адаптации сохранен в файле [patches/exdesktop_linux_port.patch](file:///home/rorka/Projects/exDesktop/patches/exdesktop_linux_port.patch).
Для наложения на свежий репозиторий:
```bash
git apply patches/exdesktop_linux_port.patch
```


---

## 7. Ускорение повторных сборок (ccache / sccache)

Проект поддерживает автоматическое кэширование объектных файлов (`.o` / `.obj`) через `ccache` или `sccache` для всех операционных систем.

При наличии `ccache` или `sccache` повторная сборка проекта ускоряется приблизительно в 2–5 раз:
```bash
# Установка ccache на Linux:
sudo apt install ccache   # Ubuntu/Debian
sudo dnf install ccache   # Fedora
sudo pacman -S ccache     # Arch Linux
```
CMake автоматически находит `ccache`/`sccache` и активирует `CMAKE_<LANG>_COMPILER_LAUNCHER`.
