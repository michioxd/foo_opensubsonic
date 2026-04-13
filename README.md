# foo_opensubsonic

[OpenSubsonic](https://github.com/opensubsonic/open-subsonic-api)/[Navidrome](https://github.com/navidrome/navidrome/) client implementation for [foobar2000](https://www.foobar2000.org/). In heavy development, but already functional for basic music playback.

![preview](https://github.com/user-attachments/assets/427b8525-2ee1-4c15-8710-ba7f8a715ab2)

## Features

- Connect to OpenSubsonic-compatible servers such as Navidrome
- Stream tracks directly in foobar2000 through `subsonic://` paths
- Sync remote library metadata into a local foobar2000 playlist
- Import and sync remote playlists
- Load and cache album artwork with fallback support for streamed items

## Current status

The component is usable for day-to-day playback and library syncing, but it is still under active development. Expect rough edges and occasional breaking changes.

## Requirements

- foobar2000 v2.0 or later (Windows x86,x64,ARM64. macOS soon...)
- An OpenSubsonic-compatible server (Latest Navidrome is recommended for best compatibility)

## Usage

1. Download the latest [`foo_opensubsonic.fb2k-component`](https://github.com/michioxd/foo_opensubsonic/releases/latest/download/foo_opensubsonic.fb2k-component) from the [Releases page](https://github.com/michioxd/foo_opensubsonic/releases).
2. Double click the downloaded file to install it. Or open foobar2000, go to `Preferences` -> `Components`, and click `Install`. Select the downloaded [`foo_opensubsonic.fb2k-component`](https://github.com/michioxd/foo_opensubsonic/releases/latest/download/foo_opensubsonic.fb2k-component) file to install the component directly from the archive.
3. Open `Preferences` -> `Tools` -> `OpenSubsonic`.
4. Enter your server URL, username, and password.
5. Click `Sync now` to fetch library data and remote playlists.
6. Play tracks from the generated playlists inside foobar2000.

[Click here to download latest version of `foo_opensubsonic.fb2k-component`](https://github.com/michioxd/foo_opensubsonic/releases/latest/download/foo_opensubsonic.fb2k-component)

## Development

### Environment

- Microsoft Visual Studio 2026 or later
- MSVC toolset v145 or later
- [foobar2000 SDK 2025-03-07](https://www.foobar2000.org/downloads/SDK-2025-03-07.7z)
- [Windows Template Library v10.01](https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.01%20Release/WTL10_01_Release.zip/download)
- [nlohmann/json v3.12.0](https://github.com/nlohmann/json/releases/tag/v3.12.0)

### Setup

- Clone this repository

    ```bash
    git clone https://github.com/michioxd/foo_opensubsonic.git
    ```

- Create `lib` directory and place the required dependencies there:
  - `foobar2000_sdk` (from the official foobar2000 SDK download)
  - `WTL` (from the WTL v10.01 release)
  - `nlohmann/json.hpp` (from the nlohmann/json v3.12.0 release)

### Building

1. Open `foo_opensubsonic.sln` in Visual Studio.
2. Build the `foo_opensubsonic` project in Release mode.
3. The resulting `foo_opensubsonic.dll` will be located in the `Release` directory.

## License

MIT License. See [LICENSE](LICENSE) for details.
