# Changelog

All notable changes to Nexus are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/): while the major
version is `0`, expect breaking changes between minor versions.

---

## [0.8.0] — YYYY-MM-DD

Nexus ships as a `.deb` and an AppImage, and no longer reads its config,
aliases, database or plugins from the current directory or `/tmp`.

### Added

- `.deb` and AppImage packages, attached to each GitHub release. The `.deb`
  recommends the Qt Wayland platform plugin; the AppImage bundles it. Both
  need Python 3 on the system for the shipped plugins. See "Install a
  package" in the README.
- Inside an AppImage, Nexus finds the bundled web and system plugins in the
  image.
- Nexus has its own icon in application menus; `cmake --install` and the
  packages install it.
- Test suites that run the real search pipeline (integration), broken-input
  cases (failure) and a performance check, each with a `ctest` label
  (`unit`, `integration`, `failure`, `perf`).

### Changed

- The single-instance socket moved from a shared name in `/tmp` to your
  runtime directory (`XDG_RUNTIME_DIR`) and is reachable only by you. If a
  0.7 instance is still running when you first start 0.8, a second instance
  opens: quit the old one first.
- Config, aliases, database, plugin and application folders ignore relative
  `XDG_*` paths and a relative or missing `HOME`. With no usable config
  directory, settings and aliases are not saved (`nexus dir add` and
  `nexus alias add` report the failure) instead of being written to `/tmp`
  or the current directory. With no usable data directory Nexus keeps its
  database in memory.
- An alias target that starts with `~` (`~/bin/tool`, `~/notes.txt`) now
  opens or runs from your home directory, also when your home path contains
  spaces or quotes; a `~` later in a plain command is still passed as plain
  text. Without a usable `HOME` such an alias is not offered.
- Programs launched from an AppImage no longer inherit the AppImage's own
  environment variables (`APPDIR`, `APPIMAGE`, `ARGV0`, `OWD`,
  `QT_QPA_PLATFORMTHEME`).

### Fixed

- The destructive-command check now sees long `sudo` and `env` options that
  take a separate value (`sudo --user bob rm -rf x`).
- The Wayland portal shortcut no longer keeps listening for answers to
  requests it already received.

### Known issues

- Inside an AppImage, your own `QT_QPA_PLATFORMTHEME` is not passed on to
  launched programs.
- **Open Folder** in Settings hands the folder to the system directly, so
  under an AppImage that file manager still inherits the AppImage's
  variables.
- The AppImage does not bundle Python 3; the web and system plugins need
  `python3` on the system.

## [0.7.0] — 2026-11-24

Nexus's global hotkey now works on Wayland, and the destructive-command
check gets fewer false alarms and misses.

### Added

- Global hotkey on Wayland. On desktops that offer the `GlobalShortcuts`
  portal (for example KDE Plasma 5.27+ and GNOME 48+) Nexus registers your
  configured shortcut through it. Once bound, the desktop owns the key, so
  change it in your system keyboard settings if Settings does not move it.
  Wayland compositors without the portal (other than GNOME) still need
  `nexus --toggle` bound by hand in their shortcut settings; Nexus shows a
  tray notice about it at start.
- On GNOME without the portal (for example GNOME 42), Settings has a
  **Set up GNOME shortcut** button. It adds one "Nexus" custom shortcut that
  runs `nexus --toggle`; clicking again updates it and leaves your other
  shortcuts alone. Nothing is written unless you click. `Alt+Space` is
  refused there because GNOME uses it for the window menu.
- Building now needs the Qt `DBus` module (see Requirements in the README).

### Changed

- Plain command aliases (no leading `>`) are checked for destructive
  commands on what is actually launched, not on a shell reading of the text.
  So `rm\s-rf x` (where `\s` is a space) now asks first, and
  `echo a;rm -rf /`, which launches `echo` with `a;rm` as one argument, no
  longer does.
- Internal: the `Executor` gained a small runner for `gsettings`, used only
  by the GNOME shortcut button.

### Fixed

- `sudo` option clusters that end in a value option (`sudo -iu bob rm -rf x`)
  no longer hide the command from the destructive-command check.
- `command -v dd` and similar lookups no longer ask for confirmation.

## [0.6.0] — 2026-11-23

Nexus opens web addresses and converts units, asks before running
destructive shell commands, tells you when a command fails, and installs
with a desktop entry and a systemd user unit. Also fixes the known issues
from 0.5.0 and makes unknown command-line arguments an error.

### Added

- Typing a web address (`github.com/x`, `https://...`, `localhost:8080` or
  `192.168.1.1`) offers "Open URL", which opens it in your browser. Bare
  `localhost` and IP addresses open as `http://`, other bare addresses as
  `https://`; bare file names such as `notes.txt` don't count, and
  addresses with leading zeros such as `010.0.0.1` are not treated as IP
  addresses.
- Unit conversion: type `120 km in miles`, `5 GB to MB` or `-5 c to f` (`in`
  or `to`, space after the number optional) and the answer appears at the
  top; Enter copies the number. Covers length, weight, temperature, data
  size (`KB` = 1000 bytes, `KiB` = 1024; `MB` is megabytes, `Mb` megabits),
  time, volume (litres and US gallons) and speed. Unknown units or mixing
  kinds (`km to kg`) show nothing.
- Shell commands that look destructive (`rm -rf`, `dd`, `mkfs`, `shutdown`,
  `reboot` and similar, also after `sudo`, `;`, `&&` or `|`, or inside
  `sh -c "..."`) now ask yes/no before running, and so do aliases whose
  target is such a command, with or without a leading `>`; the default is
  No. The check is a safety net for typos, not a security boundary.
- When a command run with `>` exits with an error or is killed by a signal,
  Nexus shows a tray message with the command and its exit status.
  Successful commands stay silent.
- `cmake --install` now installs the `nexus` binary, a `nexus.desktop`
  entry and a systemd user unit (`systemctl --user enable --now
  nexus.service`), as well as the plugins. CI checks the exact list of
  installed files.
- `nexus --version` (or `-V`) prints the installed version.

### Changed

- An unknown command-line argument (`nexus --bogus`, `nexus foo`) now
  prints an error and the usage text and exits with a non-zero status,
  instead of opening the launcher. Qt's own single-dash options such as
  `-style fusion` or `-platform xcb` still work when given first.
- `nexus --help` and `nexus --version` no longer create or touch your
  config directory. `--help` therefore shows the default hotkey ("Alt+Space
  by default") instead of the one you configured.
- Internal: comments cleaned up, C++ code reformatted with `clang-format`,
  and CI now checks C++ formatting.

### Fixed

- Typing only `clip ` (nothing after the space) now lists your whole
  clipboard history, newest first.
- Typing only `web ` no longer shows apps and files matching "web"; it
  waits for your search term.
- `nexus history clear` now also clears the clipboard history of a running
  launcher. A launcher started before the upgrade ignores this until it is
  restarted.
- Starting Nexus with a Qt option first (`nexus -style fusion --settings`,
  `nexus -session <id>`) no longer opens a second launcher when one is
  running; the running launcher handles the command (a session-restore
  start does nothing).
- The calculator read `1.5+1` as `1+1` when your system uses a decimal
  comma (German, French, Danish English and similar locales). Numbers now
  always use `.` as the decimal point.
- The calculator no longer answers part of an input it cannot fully read:
  `5..`, `5 .` and similar now show no result instead of `5`.

---

## [0.5.0] — 2026-11-16

Nexus can now be extended with plugins. Web search and system actions
ship as plugins, clipboard history is built in, and search runs the
sources in parallel.

### Added

- **Plugins.** A plugin is a program in any language that talks to Nexus
  over stdin and stdout, one JSON object per line. Nexus finds plugins in
  `~/.local/share/nexus/plugins/` and in `nexus/plugins/` under each
  `XDG_DATA_DIRS` entry (restart Nexus after adding one). Each has a
  `plugin.json` manifest with its prefixes and the kinds of result it may
  return (`permissions`); an invalid manifest skips only that plugin. A
  plugin starts on its first search. Each search gets 200 ms. After
  failures it restarts after 1 s, 5 s, then 30 s, and after that it is
  disabled until Nexus restarts. Plugins that crash, flood or send invalid
  data are stopped without affecting the launcher. Plugins are not
  sandboxed: they run with your rights, so install only ones you trust.
  See `docs/plugins.md`.
- **Web search plugin.** `web <text>` opens a search in your browser
  (DuckDuckGo by default; a `config.json` next to the plugin can set
  another engine). Needs Python 3.
- **System actions plugin.** `lock`, `sleep`, `logout`, `restart` and
  `shutdown` (partial words of two or more letters work; log out appears
  only inside a login session). Needs Python 3.
- **Clipboard history.** `clip <text>` searches what you copied while
  Nexus was running and Enter copies it again; `clip clear` empties it.
  It is kept in memory only, never written to disk. It keeps 50 entries
  (`"clipboard_retention"` in `config.json`), stops recording when history
  is off, and skips copies marked secret by password managers.
- **Confirm prompt.** A result can ask "Run ...?" before it acts; the
  default answer is No. Restart, shut down and log out use it.
- **Plugin guide** in `docs/plugins.md`.

### Changed

- **Providers are searched in parallel** with a 200 ms budget per search.
  A slow source no longer delays the others; its results are left out of
  that search.
- **Copy results now copy.** Results that offer a copy (for example the
  calculator) put their text on the clipboard when you press Enter.
- A pending search no longer refills the result list after the window is
  hidden.
- **Aliases only use a shell when the target starts with `>`.** Any other
  command alias is split into words and run directly, so pipes, `&&`,
  `$VAR`, redirects and `~` are no longer interpreted, single quotes no
  longer group words, and `%` sequences such as `%f` are removed (`%%` gives
  one `%`). Double quotes still group words (`code "My Projects"`). Existing
  aliases that rely on any of this need a leading `>` (for example
  `>ls | wc -l` or `>code ~/nexus`). Shell aliases are marked "runs in shell"
  in the result subtitle.
- `alias <name> <target>` typed in the launcher now keeps the whole rest of the
  line as the target, so multi-word and `>` aliases can be created there.
  `nexus alias add` also joins extra words into the target.
- A prefixed query (`>`, `calc `, `file `, `alias `) now reaches only the
  source that owns the prefix. `alias x y` shows just the "save alias" result,
  and `alias x` without a target shows nothing.

### Fixed

- Applications installed while Nexus was not running now appear at the
  next start, without a manual rebuild.
- `nexus history clear` now compacts the database file, so cleared query
  text can no longer be read from it. If that fails, the command says so.
- Aliases no longer show up in `>`, `file ` or `calc ` searches.
- `nexus query` now recognises `alias x y` typed after leading spaces.
- The application scan no longer aborts when a directory disappears while
  it is being read, and a hidden desktop-file override (`Hidden=true`) now
  hides the system copy of that application.

### Known issues

- Typing only `web ` or `clip ` shows nothing; type a character after the
  space.
- `cmake --install` does not install the `nexus` binary yet. Copy
  `build/nexus` by hand. It does install the web and system plugins.
- `nexus history clear` does not clear the clipboard history of a running
  launcher; use `clip clear` there.

---

## [0.4.0] — 2026-11-07

Personalisation and settings release. Ranking now learns from what you type, not just what you launch.
You can turn history off and clear it. There are light, dark and system
themes, and most options are now in the Settings dialog.

### Added

- **Query-aware ranking.** A launch now boosts an app only for searches
  related to the one you used. Launching an app from `co` raises it for
  `co` and `cod`, not for `fi`. Before, every launch boosted the app for
  every search.
- **Recency.** Recent launches count a little more. The boost halves every
  7 days and only breaks near-ties; it never outranks a better text match.
- **Exact matches always win over personalisation.** Usage and recency
  together are capped, so no amount of launches can push another result
  above one whose title exactly matches your search.
- **History controls.** Untick history in Settings, or set
  `"history_enabled": false` in `config.json`, and nothing more is
  recorded. It applies immediately. `nexus history clear` deletes usage
  events and query history and keeps the application and file indexes.
- **Themes.** Light, dark and system, chosen in Settings or with `"theme"`
  in `config.json`. Dark is the default and looks exactly as before. The
  theme changes live, without a restart.
- **Settings coverage.** A new Search tab sets how many results are listed
  (`"result_count"`, 1 to 15, default 7) and switches individual search
  sources off (`"disabled_providers"`). File exclusions can now be edited
  in Settings. All changes apply immediately.
- **Ranking evaluation suite.** A test runs fixed queries through the real
  search engine and fails if the expected top result changes, so future
  ranking changes are measured rather than guessed.
- Test count: 19 → 22.

### Changed

- `config.json` and `aliases.json` are read by a real JSON parser instead
  of line matching. Files written by 0.3.0 still load. One-line lists and
  values containing `"` or `\` now work.
- The tray menu item is now "Settings..." and `--help` lists `query, -q`
  and the history command.
- The default search directories no longer include the directory Nexus was
  started from. They are Documents, Downloads, Desktop and Projects when
  present, otherwise your home folder.

### Fixed

- **Config writer didn't escape strings.** A search directory or alias
  containing `"` or `\` produced an invalid file.
- **A broken config file could be overwritten with defaults.** If
  `config.json` or `aliases.json` can't be read, Nexus now copies it to
  `<file>.malformed` before falling back to defaults, so your settings are
  never silently lost.

### Known issues

- An alias with the same name as a command wins in shell mode: `>gh` opens
  the `gh` alias instead of running `gh`. Aliases also appear in `file `
  searches.
- `nexus history clear` deletes the rows, but the old data can still be
  recovered from the database file until SQLite reuses or compacts that
  space.
- The `system` theme follows the desktop on Qt 6.5+, or when a Qt platform
  theme (for example qgnomeplatform or qt5ct) reports the desktop palette.
  On a stock GNOME session with Qt 5 it stays light.
- Applications installed while Nexus was not running don't appear until
  you run `nexus --rebuild-index` or use **Rebuild Indexes** in the tray.
  Changes made while Nexus runs still show up within seconds.
- A calculator result can rank below an app whose name contains the
  expression, and an acronym such as `vsc` can rank below an app whose name
  starts with those letters.
- Browser, search engine, ranking weights and plugin settings from the
  specification are not built yet. They arrive with web search and the
  plugin system in 0.5.0.
- No plugin system, no web/clipboard providers, no system actions, no URL
  detection, no Wayland hotkey, no unit conversion, no accessibility pass.
  All unchanged from 0.3.0.

## [0.3.0] — 2026-11-04

Persistent index release. File search now uses a real index
that stays current while Nexus runs. Newly installed applications show up
without a manual rebuild. The database has a migration system, and there
is a benchmark harness with published numbers.

### Added

- **File index.** Files under your search directories are indexed into a
  new `files` table in SQLite. The index is built in the background at
  startup, so the window never waits for it. `file ` queries now read only
  from the database and no longer walk the disk. Results are ranked across
  every match, not just the first 30 found. Until the first build
  finishes, `file ` queries return nothing.
- **Live file watching.** Creating, deleting or renaming a file in a
  watched directory updates the index immediately, with no rebuild. This
  uses raw `inotify` on its own thread. If the kernel's
  `fs.inotify.max_user_watches` limit runs out, Nexus logs a warning with
  the `sysctl` command to raise it. Directories it could not watch are
  picked up by the next **Rebuild Indexes**.
- **Configurable file exclusions.** A new `file_exclusions` list in
  `config.json` controls which directory names are skipped. It defaults
  to the old hardcoded ignore list. An explicit empty list excludes
  nothing.
- **Incremental application indexing.** Adding or removing a `.desktop`
  file in any XDG application directory updates the app index within
  seconds. You no longer need `--rebuild-index` after installing an app.
- **Configurable global shortcut.** Set it in Settings or with
  `global_shortcut` in `config.json` (default `Alt+Space`). If the key
  combination is already grabbed by another program, Nexus tells you.
- **Wayland session notice.** Under Wayland, the tray shows one notice
  explaining that global shortcuts don't work there and how to bind
  `nexus --toggle` in your compositor instead.
- **Schema migrations.** The database now records its schema
  version and applies numbered migrations in a transaction at startup.
  Future schema changes no longer require deleting `nexus.db`.
- **Benchmark harness.** `bench_nexus` (in `bench/`, not run by
  `ctest`) measures app and file query p50/p95, index build time and
  startup time in an isolated temp directory. See
  [Performance](README.md#performance) for the numbers.
- Tests for `ApplicationProvider`, `AppIndexer`, the file indexer, both
  watchers, migrations and shortcut parsing. Test count: 12 → 19.

### Changed

- `--rebuild-index` and the tray's **Rebuild Indexes** action now rebuild
  both the application and file indexes.
- Adding or removing a search directory in Settings re-indexes files
  automatically.

### Fixed

- **Tray rebuild froze the window.** "Rebuild Indexes" ran the rescan on
  the UI thread. It now runs on a worker thread and shows a tray message
  when it finishes. A click while a rebuild is running is queued instead
  of starting a second one.
- **Per-result usage queries (partial).** App search ran one SQL
  query per matched application to fetch its launch count, and copied
  the whole application list on every keystroke. It now runs one grouped
  query per search and reads from a shared snapshot. Query cost still
  grows with the size of the usage history; that is tracked for a later
  release.
- **Applications launched through `sh -c`.** `.desktop` `Exec` lines are
  now tokenized into an argument vector and spawned directly, so no shell
  sees the command line. Shell execution remains only behind the `>`
  prefix.
- **Opening files, folders and web links.** `xdg-open` was spawned with
  broken file-descriptor handling, so some targets failed to open. The
  child process is now also reaped, so it no longer stays behind as a
  zombie.
- **Unsafe database fallback paths (security).** When the XDG data
  directory was unusable, Nexus fell back to `./.nexus.db` in the current
  directory, then to `/tmp/nexus.db`. Another local user could pre-plant
  that `/tmp` file with a malicious `Exec`. Nexus now falls back straight
  to an in-memory database and logs a warning.

### Removed

- The unused `SearchEngine::search_async()` and `cancel()` methods, which
  `AsyncSearchCoordinator` replaced in 0.2.0.

### Known issues

- Query cost grows with the usage history (about 1.9 ms at 50 000 launch
  events). This is still well inside the target, but it is not fixed.
- Short file queries that match a large share of names are the slowest
  case: about 13 ms with 50 000 indexed files.
- The file index is rebuilt fully on every start. Live watching keeps it
  current between starts.
- No plugin system, no web/clipboard providers, no system actions, no URL
  detection, no Wayland hotkey, no history controls, no unit conversion,
  dark theme only, no accessibility pass. All unchanged from 0.2.0.

## [0.2.0] — 2026-09-27

Search architecture release. No new
user-facing features — this release is entirely about fixing how search
runs internally: it no longer blocks the UI, cancellation actually works,
and ranking is no longer distorted by inconsistent provider score scales.

### Fixed

- **UI freezing during search.** `LauncherWindow` previously called
  `SearchEngine::search()` directly and synchronously on the Qt main
  thread, so every provider's work — including any filesystem access —
  ran on the same thread responsible for the window responding to input.
  Search is now dispatched through a new `QueryManager` (UI layer) backed
  by `AsyncSearchCoordinator` (Qt-free, in `nexus_core`), so provider work
  runs off the UI thread and results are marshaled back safely.
- **Broken search cancellation.** `SearchEngine::search_async()`
  and `cancel()` shared a single reused cancellation token across
  overlapping calls, so a newer search could reset the flag before an
  older, still-running search ever read it — meaning cancellation
  frequently did nothing. Every search now gets its own independently
  owned cancellation token and a generation number; a superseded search's
  results are discarded even if the search itself doesn't stop in time.
  `search_async()` and `cancel()` are left in place, unused — see Known
  issues.
- **Ranking dominated by arbitrary provider score scale.** A
  provider's raw `res.score` (ranging from ~400 to 2500 depending on
  provider) was added directly to `RankingEngine`'s own lexical/usage
  boosts (max ~800 combined), so a provider's arbitrary choice of raw
  score could overwhelm actual match-quality signals regardless of how
  well something matched the query. Provider scores are now normalized to
  a 0.0–1.0 relevance value before being combined, so provider score
  scale and ranking weight tuning are independent again.

### Added

- Unit tests for `Query`, `Executor`, and `SearchEngine` — three
  previously untested, central classes now have dedicated coverage.
- `AsyncSearchCoordinator` tests covering generation ordering, discarding
  superseded results, and cancellation token delivery.
- Two new `RankingEngine` tests verifying a low-scoring provider with an
  exact lexical match now correctly outranks a high-scoring provider with
  no match, and that score normalization clamps correctly.
- Test count: 8 → 12.

### Known issues

Carried forward from 0.1.0 except where noted resolved below.

- `SearchEngine::search_async()` and `cancel()` still exist, unused —
  superseded by `AsyncSearchCoordinator`/`QueryManager` rather than
  repaired or removed. Removing them is a candidate for future cleanup.
- **File search is still slow to run** (though it no longer freezes the
  UI while doing so). Each `file `-prefixed search still performs a live
  filesystem walk rather than querying an index — the index itself is
  not started yet.
- No plugin system, no web/clipboard providers, no system actions, no URL
  detection, no Wayland support, no configurable shortcut, no schema
  migrations, no incremental indexing, no history controls, no unit
  conversion, minimal settings coverage, dark theme only, no accessibility
  pass — all unchanged from 0.1.0.

## [0.1.0] — 2026-09-20

First tagged release. Nexus is a working, daily-usable launcher on X11. It is
not feature-complete against its own specification — see **Known issues**
below.

### Added

- Global hotkey (`Alt + Space`) via an X11 key grab, with single-instance
  behavior so relaunching toggles the existing window instead of starting a
  second copy.
- Application search across XDG desktop-entry directories, including Snap
  and Flatpak paths, with fuzzy matching and usage-based ranking.
- Calculator provider with a recursive-descent expression parser (operator
  precedence, functions, right-associative exponentiation) — no shell or
  external interpreter involved.
- File and folder search, invoked explicitly with the `file ` prefix.
- Shell command execution, invoked explicitly with the `>` prefix — never
  triggered by an ordinary search.
- Alias system: create, list, and remove aliases pointing at URLs, paths,
  files, or commands, with a settings-panel UI.
- Settings dialog covering search directories, aliases, and Ctrl+N/Ctrl+P
  navigation toggle.
- SQLite-backed persistence for the application index, usage history, and
  query history, with an XDG-path fallback chain.
- Tray icon with toggle, open-settings, rebuild-index, and quit actions.
- Command-line interface: `--toggle`, `--show`, `--hide`, `settings`,
  `--rebuild-index`, `--list-apps`, `query <term>`, `dir`/`alias` subcommands.
- Eight `ctest`-driven unit test binaries covering fuzzy matching, desktop-entry
  parsing, ranking, the calculator, the database layer, aliases, file search,
  and configuration.
- GitHub Actions CI, building and testing on both Qt 5.15.3 (matching the
  primary development environment) and Qt 6 (forward-compatibility check),
  on every push and pull request to `main`.
- MIT license and a public README covering installation, usage, architecture,
  and an explicit "not implemented yet" section.

### Fixed

- **Security:** `Executor::open_path_or_url` built a shell command by
  concatenating an unescaped file path into `sh -c`, so a maliciously named
  file in any indexed directory could execute arbitrary commands when opened.
  Replaced with direct process execution — no shell in the path.
- **Responsiveness:** `FileProvider` was running a full recursive filesystem
  walk on every keystroke of every search, including ordinary application
  searches, freezing the UI on any real-sized home directory. It is now
  gated to run only when the query explicitly starts with `file `.
- **Qt6 build failure:** `ModernCheckBox::enterEvent` used the Qt5 signature
  (`QEvent*`), which does not compile under Qt6 (`QEnterEvent*`). Fixed with
  a `QT_VERSION`-guarded signature so the same source builds correctly under
  both Qt 5.15.3 and Qt6.
- `tests/test_file_search.cpp` was written against the pre-fix behavior
  (searching without the `file ` prefix). Updated to match the corrected
  gating, and extended with an explicit regression test asserting that a
  plain, unprefixed query returns no results from `FileProvider`.

### Known issues

Documented here deliberately rather than discovered by the next person.

- **No plugin system.** All providers are compiled into the core binary.
  There is no public API or extension mechanism yet.
- **`web ` and `clip ` prefixes are parsed but unimplemented** — both return
  empty results. No web search or clipboard history provider exists yet.
- **No system actions** (shutdown, restart, lock, logout, sleep).
- **No URL detection** — a bare URL is only reachable via a manually created
  alias.
- **Wayland is not supported.** The global hotkey depends on an X11 key
  grab and will silently fail to register under a Wayland session.
- **The global shortcut is not configurable** — `Alt + Space` is hardcoded.
- **File search has no index.** It is gated behind the `file ` prefix as of
  this release (see Fixed, above), but each invocation still performs a
  live filesystem walk rather than querying a maintained index, so it
  remains slower than application search.
- **No schema migrations.** The database's `schema_version` table exists
  but is not yet read or written; a future schema change may require
  deleting and rebuilding the local database.
- **No incremental indexing or filesystem watching.** A newly installed
  application will not appear until `--rebuild-index` is run manually.
- **Ranking personalization is global, not per-query.** Frequently launched
  applications are boosted for all searches, not specifically the queries
  that were used to launch them.
- **History cannot be disabled or cleared from the UI.** Usage and query
  history are recorded unconditionally; deleting `nexus.db` manually is
  currently the only way to clear it.
- **No unit conversion** in the calculator (only arithmetic and functions).
- **Only two settings are exposed** (search directories, navigation key
  toggle) against the twelve described in the specification.
- **Themes:** dark only; no light or system-theme option.
- **Accessibility** (screen reader support, font scaling, focus indicators
  on all interactive elements) has not been addressed.
- **Test coverage gaps:** `SearchEngine`, `Query`, `Executor`,
  `ApplicationProvider`, and `AppIndexer` have no dedicated unit tests.
  No integration or performance test suites exist yet.

### Supported environment

| | |
|---|---|
| OS | Linux |
| Display server | **X11 only** — Wayland sessions are not supported (hotkey will not register) |
| Qt | Developed and tested against Qt 5.15.3. Qt 6 builds and passes CI, but has had far less real-world use |
| Compiler | C++20 — GCC 11+ or Clang 14+ |
| Build system | CMake ≥ 3.22 |
| Dependencies | Qt (`Core`, `Gui`, `Widgets`, `Network`), SQLite3, libX11 |
| Desktop environments tested | Whatever the maintainer runs daily (X11-based). Not yet verified across GNOME, KDE, and XFCE independently |

[0.8.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.8.0
[0.7.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.7.0
[0.6.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.6.0
[0.5.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.5.0
[0.4.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.4.0
[0.3.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.3.0
[0.2.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.2.0
[0.1.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.1.0
