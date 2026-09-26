# Changelog

All notable changes to Nexus are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [Semantic Versioning](https://semver.org/): while the major
version is `0`, expect breaking changes between minor versions.

---

## [0.2.0] — 2026-09-27

Closes out Phase 2 (search architecture) from `nexus-SRS.md`. No new
user-facing features — this release is entirely about fixing how search
runs internally: it no longer blocks the UI, cancellation actually works,
and ranking is no longer distorted by inconsistent provider score scales.

### Fixed

- **UI freezing during search (D-12).** `LauncherWindow` previously called
  `SearchEngine::search()` directly and synchronously on the Qt main
  thread, so every provider's work — including any filesystem access —
  ran on the same thread responsible for the window responding to input.
  Search is now dispatched through a new `QueryManager` (UI layer) backed
  by `AsyncSearchCoordinator` (Qt-free, in `nexus_core`), so provider work
  runs off the UI thread and results are marshaled back safely.
- **Broken search cancellation (D-14).** `SearchEngine::search_async()`
  and `cancel()` shared a single reused cancellation token across
  overlapping calls, so a newer search could reset the flag before an
  older, still-running search ever read it — meaning cancellation
  frequently did nothing. Every search now gets its own independently
  owned cancellation token and a generation number; a superseded search's
  results are discarded even if the search itself doesn't stop in time.
  `search_async()` and `cancel()` are left in place, unused — see Known
  issues.
- **Ranking dominated by arbitrary provider score scale (D-29).** A
  provider's raw `res.score` (ranging from ~400 to 2500 depending on
  provider) was added directly to `RankingEngine`'s own lexical/usage
  boosts (max ~800 combined), so a provider's arbitrary choice of raw
  score could overwhelm actual match-quality signals regardless of how
  well something matched the query. Provider scores are now normalized to
  a 0.0–1.0 relevance value before being combined, so provider score
  scale and ranking weight tuning are independent again.

### Added

- Unit tests for `Query`, `Executor`, and `SearchEngine` (D-40) — three
  previously untested, central classes now have dedicated coverage.
- `AsyncSearchCoordinator` tests covering generation ordering, discarding
  superseded results, and cancellation token delivery.
- Two new `RankingEngine` tests verifying a low-scoring provider with an
  exact lexical match now correctly outranks a high-scoring provider with
  no match, and that score normalization clamps correctly.
- Test count: 8 → 12.

### Known issues

Carried forward from 0.1.0 except where noted resolved below. See
`nexus-SRS.md` for full detail.

- `SearchEngine::search_async()` and `cancel()` still exist, unused —
  superseded by `AsyncSearchCoordinator`/`QueryManager` rather than
  repaired or removed. Removing them is a candidate for future cleanup.
- **File search is still slow to run** (though it no longer freezes the
  UI while doing so). Each `file `-prefixed search still performs a live
  filesystem walk rather than querying an index — the index itself is
  Phase 3 work, not yet started.
- No plugin system, no web/clipboard providers, no system actions, no URL
  detection, no Wayland support, no configurable shortcut, no schema
  migrations, no incremental indexing, no history controls, no unit
  conversion, minimal settings coverage, dark theme only, no accessibility
  pass — all unchanged from 0.1.0.



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
