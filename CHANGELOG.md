# Changelog

All notable changes to Nexus are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/): while the major
version is `0`, expect breaking changes between minor versions.

---

## [0.1.0] — 2026-09-20

First tagged release. Nexus is a working, daily-usable launcher on X11. It is
not feature-complete against its own specification — see **Known issues**
below and `nexus-SRS.md` for the full roadmap.

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
See `nexus-SRS.md` for full detail and `docs/` (once written) for tracking.

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

[0.1.0]: https://github.com/mawaissaleem/Nexus/releases/tag/v0.1.0
