#!/bin/sh
set -eu

REPO="metawka/exDesktop"
ASSUME_YES=0
OPT_MENU=""
OPT_TGWS=""
OPT_AYU=""
PREFIX=""

for arg in "$@"; do
	case "$arg" in
		-y|--yes) ASSUME_YES=1 ;;
		--menu) OPT_MENU=1 ;;
		--no-menu) OPT_MENU=0 ;;
		--tgws) OPT_TGWS=1 ;;
		--no-tgws) OPT_TGWS=0 ;;
		--ayu) OPT_AYU=1 ;;
		--no-ayu) OPT_AYU=0 ;;
		--prefix=*) PREFIX="${arg#--prefix=}" ;;
		-h|--help)
			echo "Usage: install.sh [--yes] [--prefix=DIR] [--menu|--no-menu] [--tgws|--no-tgws] [--ayu|--no-ayu]"
			exit 0 ;;
		*) echo "Unknown option: $arg" >&2; exit 1 ;;
	esac
done

case "${LANG:-}${LC_ALL:-}" in
	*ru*) RU=1 ;;
	*) RU=0 ;;
esac

say() {
	if [ "$RU" = 1 ]; then printf '%s\n' "$2"; else printf '%s\n' "$1"; fi
}

fail() {
	say "Error: $1" "Ошибка: $2" >&2
	exit 1
}

ask() {
	current="$1"
	if [ -n "$current" ]; then
		[ "$current" = 1 ]
		return
	fi
	if [ "$ASSUME_YES" = 1 ] || [ ! -r /dev/tty ]; then
		[ "$4" = 1 ]
		return
	fi
	if [ "$RU" = 1 ]; then prompt="$3"; else prompt="$2"; fi
	if [ "$4" = 1 ]; then hint="[Y/n]"; else hint="[y/N]"; fi
	printf '%s %s ' "$prompt" "$hint" > /dev/tty
	read -r answer < /dev/tty || answer=""
	case "$answer" in
		[YyДд]*) return 0 ;;
		[NnНн]*) return 1 ;;
		*) [ "$4" = 1 ] ;;
	esac
}

fetch() {
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL "$1" -o "$2"
	elif command -v wget >/dev/null 2>&1; then
		wget -qO "$2" "$1"
	else
		fail "curl or wget is required" "нужен curl или wget"
	fi
}

OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS" in
	Linux)
		[ "$ARCH" = "x86_64" ] || fail "only x86_64 builds are published for Linux" "для Linux публикуются только сборки x86_64"
		PATTERN='exDesktop-linux-x64\.[0-9.]*\.tar\.gz'
		[ -n "$PREFIX" ] || PREFIX="$HOME/.local/share/exDesktop/app"
		DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/exDesktop"
		;;
	Darwin)
		[ "$ARCH" = "arm64" ] || fail "only Apple Silicon builds are published for macOS" "для macOS публикуются только сборки Apple Silicon"
		PATTERN='exDesktop-macos-arm64\.[0-9.]*\.zip'
		[ -n "$PREFIX" ] || PREFIX="/Applications"
		DATA_DIR="$HOME/Library/Application Support/exDesktop"
		;;
	*)
		fail "unsupported system $OS, use the Windows installer from the releases page" "система $OS не поддерживается, для Windows используйте установщик со страницы релизов"
		;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

say "Looking for the latest release..." "Поиск последнего релиза..."
fetch "https://api.github.com/repos/$REPO/releases/latest" "$TMP/release.json"
URL="$(grep -o "\"browser_download_url\": *\"[^\"]*$PATTERN\"" "$TMP/release.json" | head -n 1 | sed 's/.*"\(https[^"]*\)"$/\1/')"
[ -n "$URL" ] || fail "release file for this system was not found" "файл релиза для этой системы не найден"
NAME="${URL##*/}"
NAME="${NAME%.tar.gz}"
NAME="${NAME%.zip}"
VERSION="${NAME#*.}"
say "Version: $VERSION" "Версия: $VERSION"

if [ "$OS" = "Linux" ]; then
	ask "$OPT_MENU" "Add exDesktop to the application menu?" "Добавить exDesktop в меню приложений?" 1 && OPT_MENU=1 || OPT_MENU=0
fi
ask "$OPT_TGWS" "Install TG WS Proxy dependencies (python3 and pip required)?" "Установить зависимости TG WS Proxy (нужны python3 и pip)?" 1 && OPT_TGWS=1 || OPT_TGWS=0
ask "$OPT_AYU" "Enable AyuGram features (ghost mode, saving deleted messages)?" "Включить функции AyuGram (режим призрака, сохранение удалённых сообщений)?" 0 && OPT_AYU=1 || OPT_AYU=0

say "Downloading $URL" "Загрузка $URL"
FILE="$TMP/package"
fetch "$URL" "$FILE"

if [ "$OS" = "Linux" ]; then
	mkdir -p "$TMP/unpack"
	tar -xzf "$FILE" -C "$TMP/unpack"
	[ -x "$TMP/unpack/exDesktop/exDesktop" ] || fail "archive is damaged" "архив повреждён"
	rm -rf "$PREFIX"
	mkdir -p "$(dirname "$PREFIX")"
	mv "$TMP/unpack/exDesktop" "$PREFIX"
	TGWS_DIR="$PREFIX/python/tgws"
	mkdir -p "$HOME/.local/bin"
	ln -sf "$PREFIX/exDesktop" "$HOME/.local/bin/exdesktop"
	if [ "$OPT_MENU" = 1 ]; then
		APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
		ICONS="${XDG_DATA_HOME:-$HOME/.local/share}/icons/hicolor/256x256/apps"
		MIME="${XDG_DATA_HOME:-$HOME/.local/share}/mime/packages"
		mkdir -p "$APPS" "$ICONS" "$MIME"
		[ -f "$PREFIX/exDesktop.png" ] && cp "$PREFIX/exDesktop.png" "$ICONS/exdesktop.png"
		cat > "$APPS/exdesktop.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=exDesktop
Comment=Telegram client
Exec="$PREFIX/exDesktop" -- %u
Icon=exdesktop
Terminal=false
Categories=Network;InstantMessaging;Chat;
MimeType=x-scheme-handler/tg;application/x-exdesktop-plugin;
StartupWMClass=exDesktop
EOF
		cat > "$MIME/exdesktop-plugin.xml" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<mime-info xmlns="http://www.freedesktop.org/standards/shared-mime-info">
  <mime-type type="application/x-exdesktop-plugin">
    <comment>exDesktop plugin</comment>
    <glob pattern="*.plugin"/>
  </mime-type>
</mime-info>
EOF
		command -v update-mime-database >/dev/null 2>&1 && update-mime-database "${XDG_DATA_HOME:-$HOME/.local/share}/mime" >/dev/null 2>&1 || true
		command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" >/dev/null 2>&1 || true
	fi
else
	command -v ditto >/dev/null 2>&1 || fail "ditto is required" "нужна утилита ditto"
	mkdir -p "$TMP/unpack"
	ditto -x -k "$FILE" "$TMP/unpack"
	[ -d "$TMP/unpack/exDesktop.app" ] || fail "archive is damaged" "архив повреждён"
	TARGET="$PREFIX/exDesktop.app"
	if [ ! -w "$PREFIX" ]; then
		PREFIX="$HOME/Applications"
		TARGET="$PREFIX/exDesktop.app"
		mkdir -p "$PREFIX"
	fi
	rm -rf "$TARGET"
	ditto "$TMP/unpack/exDesktop.app" "$TARGET"
	xattr -dr com.apple.quarantine "$TARGET" 2>/dev/null || true
	TGWS_DIR="$DATA_DIR/tgws-lib"
fi

if [ "$OPT_TGWS" = 1 ]; then
	if command -v python3 >/dev/null 2>&1; then
		say "Installing TG WS Proxy dependencies..." "Установка зависимостей TG WS Proxy..."
		if [ "$OS" = "Linux" ]; then LIB="$TGWS_DIR/lib"; else LIB="$TGWS_DIR"; fi
		mkdir -p "$LIB"
		python3 -m pip install --quiet --disable-pip-version-check --target "$LIB" cryptography h2 httpx certifi \
			|| say "TG WS Proxy dependencies were not installed" "Зависимости TG WS Proxy не установлены"
	else
		say "python3 was not found, TG WS Proxy dependencies were skipped" "python3 не найден, зависимости TG WS Proxy пропущены"
	fi
fi

mkdir -p "$DATA_DIR"
printf 'ayu=%s\ntgws=%s\n' "$OPT_AYU" "$OPT_TGWS" > "$DATA_DIR/exd_install.conf"

if [ "$OS" = "Linux" ]; then
	say "exDesktop $VERSION is installed to $PREFIX. Run: exdesktop" "exDesktop $VERSION установлен в $PREFIX. Запуск: exdesktop"
else
	say "exDesktop $VERSION is installed to $TARGET" "exDesktop $VERSION установлен в $TARGET"
fi
