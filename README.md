<p align="center">
  <a href="https://github.com/metawka/exDesktop/stargazers"><img src="docs/banner.png" alt="Star exDesktop on GitHub" width="100%"></a>
</p>

<p align="center">
  <img src="docs/logo.png" width="120" alt="exDesktop">
</p>

<h1 align="center">exDesktop</h1>

<p align="center">
  Unofficial Telegram client for Windows, Linux and macOS, built from the official
  <a href="https://github.com/telegramdesktop/tdesktop">Telegram Desktop</a> sources.<br>
  Features from exteraGram and other community clients, adapted for Desktop.
</p>

<p align="center">
  <a href="https://github.com/metawka/exDesktop/releases/latest"><img src="https://img.shields.io/github/v/release/metawka/exDesktop?label=Release&color=ee6b54" alt="Release"></a>
  <a href="https://github.com/metawka/exDesktop/releases"><img src="https://img.shields.io/github/downloads/metawka/exDesktop/total?label=Downloads&color=ee6b54" alt="Downloads"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0-ee6b54" alt="License"></a>
  <br>
  <a href="https://t.me/exDesktop"><img src="https://img.shields.io/badge/Channel-@exDesktop-2aabee?logo=telegram&logoColor=white" alt="Channel"></a>
  <a href="https://t.me/exDesktopChat"><img src="https://img.shields.io/badge/Chat-@exDesktopChat-2aabee?logo=telegram&logoColor=white" alt="Chat"></a>
  <a href="https://exteradesktop.xyz"><img src="https://img.shields.io/badge/Website-exteradesktop.xyz-2aabee" alt="Website"></a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/English-selected-555?style=for-the-badge" alt="English">
  <a href="README.ru.md"><img src="https://img.shields.io/badge/Русский-open-ee6b54?style=for-the-badge" alt="Русский"></a>
</p>

<p align="center">
  <img src="docs/screenshots/exdesktop.png" width="100%" alt="exDesktop">
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/screenshots/settings.png" alt="Settings"></td>
    <td width="50%"><img src="docs/screenshots/appearance.png" alt="Appearance"></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/screenshots/chats.png" alt="Chats"></td>
    <td width="50%"><img src="docs/screenshots/plugins.png" alt="Plugins"></td>
  </tr>
</table>

## Features

<table>
  <tr>
    <td width="50%" valign="top">
      <h3>Appearance</h3>
      <ul>
        <li>Material Design 3 elements</li>
        <li>Custom app icon settings</li>
        <li>UI elements and avatar settings</li>
        <li>Sticker size adjustments</li>
        <li>Hide the "All Chats" tab</li>
        <li>Support for system fonts and emoji</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>Messages</h3>
      <ul>
        <li>Quick actions bar</li>
        <li>Message details</li>
        <li>Built-in QuoteReply</li>
        <li>Scheduled posting for channel editors</li>
        <li>Forwarding messages from other accounts</li>
        <li>Message translation preserving formatting</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>Input and Media</h3>
      <ul>
        <li>Calculations in the input field</li>
        <li>Character counter</li>
        <li>Favorite emoji</li>
        <li>Emoji pack ordering</li>
        <li>Local premium emoji</li>
        <li>Create video notes from videos</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>Privacy</h3>
      <ul>
        <li>Hidden phone number</li>
        <li>Show user IDs</li>
        <li>Show registration date</li>
        <li>Streamer mode</li>
        <li>Show hidden group members</li>
        <li>Hide archive</li>
      </ul>
    </td>
  </tr>
  <tr>
    <td width="50%" valign="top">
      <h3>Network and Notifications</h3>
      <ul>
        <li>Built-in tg-ws-proxy on Windows</li>
        <li>Faster downloads and uploads</li>
        <li>Notification visual settings</li>
        <li>Custom notification sound</li>
        <li>Removed account limit</li>
        <li>Automatic updates</li>
      </ul>
    </td>
    <td width="50%" valign="top">
      <h3>Plugins and AI</h3>
      <ul>
        <li>Python plugin engine [in development]</li>
        <li>Plugin settings</li>
        <li>Plugin export</li>
        <li>Install plugins from file</li>
        <li>AI chat</li>
      </ul>
    </td>
  </tr>
</table>

## Downloads

| Platform | File |
|:--|:--|
| Windows | `exDesktop-setup-x64.*.exe` installer or `exDesktop-portable-x64.*.zip` portable version |
| Linux | `exDesktop-linux-x64.*.tar.gz`, run `exDesktop` from the archive |
| macOS | `exDesktop-macos-arm64.*.zip` for Apple Silicon |

All files are available on the [releases page](https://github.com/metawka/exDesktop/releases/latest). exDesktop installs beside Telegram Desktop and keeps its data separate.

## Building

<details>
<summary><b>API credentials</b></summary>

Get your own API ID and hash on [Telegram's API credentials page](https://core.telegram.org/api/obtaining_api_id). Pass them to CMake or the configure script when generating project files:

```bash
-D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH
```

</details>

<details>
<summary><b>Linux</b></summary>

**Dependencies**

- CMake 3.16 or newer and a C++20 compiler (GCC 12 or Clang 15 and newer)
- Qt 6: Core, Gui, Widgets, Network, OpenGL, DBus, Wayland, Svg, ImageFormats
- Python 3.11 to 3.14 development headers (`python3-dev` / `dev-lang/python`)
- Telegram Desktop libraries: `tg_owt`, `ffmpeg`, `openal`, `opus`, `rnnoise`, `libjpeg-turbo`, `xxhash`, `minizip`, `ada`, `cld3`, `wallet-engine-telegram`

**Build with CMake**

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_VERSION_MAJOR=6 \
    -DDESKTOP_APP_DISABLE_QT_PLUGINS=ON \
    -DTDESKTOP_API_ID="YOUR_API_ID" \
    -DTDESKTOP_API_HASH="YOUR_API_HASH"

cmake --build build --target Telegram
```

**Gentoo**

```bash
emerge net-im/exdesktop
```

**Python plugin runtime**

Copy the runtime files to `/usr/share/exDesktop/python/extera/`, or keep them next to the binary in `python/extera/`:

```bash
sudo mkdir -p /usr/share/exDesktop/python/extera
sudo cp -r Telegram/SourceFiles/extera/python/* /usr/share/exDesktop/python/extera/
```

</details>

<details>
<summary><b>Windows</b></summary>

1. Follow the [Telegram Desktop Windows build guide](https://github.com/telegramdesktop/tdesktop/blob/dev/docs/building-win.md).
2. Put the embedded Python 3.11 runtime in `Libraries/win64/python311`, including the headers, `python311.lib` and the `embed` folder.
3. Generate the project files with your API ID and hash:

```cmd
python configure.py x64 -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH
```

4. Build the `Telegram` target:

```cmd
cmake --build out --config Release --target Telegram
```

The `python` folder next to `Telegram.exe` is required for plugins.

</details>

## Localization

Most strings follow Telegram Desktop translations. exDesktop strings are available in English and Russian.

## Credits

| Project | Use in exDesktop |
|:--|:--|
| [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) | Client sources |
| [exteraGram](https://github.com/exteraSquad/exteraGram) | Features and plugin format |
| [AyuGram Desktop](https://github.com/AyuGram/AyuGramDesktop) | Desktop implementation references |
| [tg-ws-proxy](https://github.com/Flowseal/tg-ws-proxy) | Built-in WebSocket proxy, MIT License |

## License

exDesktop is distributed under the [GNU General Public License v3.0](LICENSE).