# foo_opensubsonic

OpenSubsonic/Navidrome client implementation for foobar2000. In heavy development, but already functional for basic music playback.

![preview](https://github.com/user-attachments/assets/bd2713c0-e7ae-4b9f-9287-5de8d0fe3d3b)

## Features

- Connect to OpenSubsonic-compatible servers such as Navidrome
- Stream tracks directly in foobar2000 through `subsonic://` paths
- Sync remote library metadata into a local foobar2000 playlist
- Import and sync remote playlists
- Load and cache album artwork with fallback support for streamed items
- Preferences page for server URL, username, password, client name, and insecure TLS toggle
- Manual actions for sync and cache clearing

## Current status

The component is usable for day-to-day playback and library syncing, but it is still under active development. Expect rough edges and occasional breaking changes.

## Requirements

- foobar2000 v2.0 or later
- An OpenSubsonic-compatible server (Latest Navidrome is recommended for best compatibility)

## Usage

1. Install the component in foobar2000.
2. Open `Preferences` -> `Tools` -> `OpenSubsonic`.
3. Enter your server URL, username, and password.
4. Click `Sync now` to fetch library data and remote playlists.
5. Play tracks from the generated playlists inside foobar2000.

## License

MIT License. See [LICENSE](LICENSE) for details.
