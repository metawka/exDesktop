## Build instructions for macOS

### Prepare folder

Choose a folder for the future build, for example **/Users/user/TBuild**. It will be named ***BuildPath*** in the rest of this document. All commands will be launched from Terminal.

**Note about disk space:** the full build needs roughly **55 GB** of free space (libraries plus the compiled app in `out`). The first run of `mac.sh` takes hours, mostly building Qt and ffmpeg; later runs skip every finished stage.

### Obtain your API credentials

You will require **api_id** and **api_hash** to access the Telegram API servers. To learn how to obtain them [click here][api_credentials].

### Requirements

- Full **Xcode** (not only the command line tools)
- [Homebrew](https://brew.sh) packages:

      brew install git automake libtool cmake wget pkg-config gnu-tar ninja nasm meson python3

- Xcode selected as the active developer directory:

      sudo -S -p '' xcode-select -s /Applications/Xcode.app/Contents/Developer

### Clone source code and prepare libraries

`prepare.py` expects the repository folder to be called `tdesktop`, so either clone into that name or add a symlink. Go to ***BuildPath*** and run

    git clone --recursive https://github.com/metawka/exDesktop.git
    ln -s exDesktop tdesktop
    ./tdesktop/Telegram/build/prepare/mac.sh silent

`silent` skips the interactive stage prompt, which fails without a terminal.

### Building the project

Go to ***BuildPath*/tdesktop/Telegram** and run (using [your **api_id** and **api_hash**](#obtain-your-api-credentials))

    ./configure.sh -D CMAKE_OSX_ARCHITECTURES=arm64 -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH

Then build from the command line

    cd ../out
    cmake --build . --config Release --target Telegram

or open ***BuildPath*/tdesktop/out/Telegram.xcodeproj** in Xcode. The result is `out/Release/Telegram.app`.

### Notes

- The plugin system embeds Python, which CMake takes from your machine (`find_package(Python3)`). Homebrew's Python on Apple Silicon is arm64-only, so the build above is arm64-only. A universal build would need a universal2 Python.
- The app dynamically loads that Python, so it has the `disable-library-validation` entitlement.

[api_credentials]: api_credentials.md
