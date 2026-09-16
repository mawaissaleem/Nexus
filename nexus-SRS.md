# Linux Productivity Launcher

A fast, keyboard-first productivity launcher for Linux, designed to provide a unified interface for launching applications, searching information, executing commands, and accessing everyday workflows.

The project is inspired by modern launcher applications such as Flow Launcher, Alfred, and Raycast, but is designed specifically around **Linux, extensibility, performance, privacy, and intelligent search**.

The objective is not to build another application launcher.

The objective is to build a **general-purpose local productivity platform** that can become the primary keyboard-driven interface for a Linux desktop.

---

# 1. Project Vision

Modern Linux desktops provide many separate tools for performing simple tasks:

- Application menus for launching applications
- File managers for finding files
- Terminals for commands
- Browsers for web searches
- Calculator applications for calculations
- Clipboard managers for previous copies
- System settings for system actions

This project brings these workflows into a single interface.

The user should be able to press one global shortcut, type what they want, and immediately receive the most relevant action.

For example:

```text
Alt + Space
```

followed by:

```text
code
```

should launch Visual Studio Code.

But the same interface should also support:

```text
calc 125 * 37
```

```text
file thesis.pdf
```

```text
> docker ps
```

```text
web reinforcement learning robotics
```

```text
clipboard password
```

The launcher should eventually become the user's **command center for the Linux desktop**.

---

# 2. Core Goal

The primary goal is to build a launcher that is:

- Fast
- Keyboard-first
- Extensible
- Intelligent
- Reliable
- Local-first
- Privacy-friendly
- Resource-efficient
- Highly customizable
- Useful enough for everyday personal use

The project should demonstrate strong engineering skills rather than simply demonstrate UI development.

The implementation should therefore emphasize:

- Software architecture
- Search systems
- Information retrieval
- Ranking
- Indexing
- Concurrency
- Linux integration
- Plugin architecture
- Persistence
- Performance engineering
- Testing
- Observability

---

# 3. Project Objectives

## 3.1 Primary objectives

The system must:

1. Provide a global keyboard shortcut to open the launcher.
2. Discover installed Linux applications automatically.
3. Maintain a persistent searchable index.
4. Provide fast fuzzy search.
5. Rank results according to relevance.
6. Learn from user interaction.
7. Execute selected actions.
8. Support extensible plugins.
9. Perform expensive work outside the UI thread.
10. Remain responsive during indexing and searching.
11. Store user data locally by default.
12. Provide useful diagnostics and performance measurements.

## 3.2 Secondary objectives

The system should eventually support:

- File search
- Calculator
- Shell commands
- Web search
- Clipboard history
- URL handling
- System actions
- Custom commands
- Semantic search
- Context-aware ranking
- Plugin management
- User-defined aliases
- Multiple search providers

---

# 4. Design Philosophy

## 4.1 Keyboard first

The launcher should be completely usable without a mouse.

Typical workflow:

```text
Global Shortcut
      ↓
Type
      ↓
Navigate
      ↓
Enter
```

The mouse should be optional.

---

## 4.2 Speed over visual complexity

The launcher is a productivity tool.

Animations and visual effects should never compromise responsiveness.

A simple interface that appears instantly is preferable to an impressive interface that feels slow.

---

## 4.3 Local first

Core functionality should work without an internet connection.

Local functionality should include:

- Application search
- File search
- Calculator
- Shell commands
- Usage history
- Ranking
- Configuration

Internet-dependent functionality should be implemented as optional providers.

---

## 4.4 Modular by design

The core application should not contain hard-coded implementations for every feature.

Instead:

```text
Core
 ├── Search Engine
 ├── Ranking Engine
 ├── Index Manager
 ├── Plugin Manager
 └── Executor
```

Features should be implemented through providers/plugins whenever practical.

---

## 4.5 No unnecessary AI

AI should not be added simply to make the project look like an AI project.

Traditional search should remain the foundation.

Semantic search can be introduced only if it provides measurable improvements.

The system should remain fully usable without AI.

---

# 5. Target Users

The initial target user is a technical Linux user who:

- Uses keyboard shortcuts heavily
- Frequently launches applications
- Works with terminals and development tools
- Searches files regularly
- Wants a fast workflow
- Values local/private software
- Wants customization

The project will initially be optimized for the developer/technical-user workflow rather than trying to support every possible desktop workflow.

---

# 6. Functional Requirements

## FR-01 — Global Launcher

The system shall provide a configurable global keyboard shortcut.

Default:

```text
Alt + Space
```

Pressing the shortcut shall display the launcher window.

The shortcut must work regardless of which application currently has focus.

---

## FR-02 — Launcher Window

The launcher shall provide:

- Search input
- Search results
- Result icons
- Result title
- Optional subtitle/metadata
- Keyboard navigation
- Selection indicator
- Loading state when required

Required keyboard controls:

```text
↑ / ↓       Navigate
Enter       Execute
Esc         Close
Ctrl/Cmd?   Reserved for future shortcuts
```

---

# 7. Application Search

## FR-03 — Application Discovery

The launcher shall automatically discover applications using Linux `.desktop` entries.

Potential locations include:

```text
/usr/share/applications
/usr/local/share/applications
~/.local/share/applications
```

The implementation must account for desktop-entry metadata such as:

- Name
- GenericName
- Comment
- Exec
- Icon
- Categories
- Keywords
- NoDisplay
- Hidden

---

## FR-04 — Application Index

Applications shall be indexed rather than parsed from disk for every query.

The index shall contain normalized application metadata.

Example:

```text
ID
Name
GenericName
Comment
Keywords
Exec
Icon
Categories
DesktopFile
```

The index should be persistent.

---

# 8. Search Engine

## FR-05 — Query Processing

The search engine shall normalize user queries where appropriate.

Possible normalization:

- Lowercase conversion
- Whitespace normalization
- Tokenization
- Alias resolution

The system must avoid destructive normalization for commands, URLs, and other syntax-sensitive input.

---

## FR-06 — Fuzzy Search

The search engine shall support fuzzy matching.

For example:

```text
vsc
```

may return:

```text
Visual Studio Code
VS Code Insiders
```

The fuzzy algorithm should consider:

- Character similarity
- Character order
- Prefix matching
- Token matching
- Word boundaries
- Exact matches

---

## FR-07 — Search Providers

The search engine shall support multiple providers.

Initial providers:

```text
ApplicationProvider
CalculatorProvider
ShellProvider
```

Future providers:

```text
FileProvider
WebProvider
ClipboardProvider
SystemProvider
PluginProvider
```

Each provider shall return a common `Result` representation.

---

# 9. Result Model

All search providers should produce a normalized result.

Conceptually:

```python
Result(
    id=...,
    title=...,
    subtitle=...,
    icon=...,
    provider=...,
    score=...,
    metadata=...,
    action=...
)
```

The UI should not need to understand where a result came from.

---

# 10. Ranking Engine

Search and ranking must be separate concerns.

The search engine retrieves candidates.

The ranking engine determines which candidates should appear first.

Initial ranking signals:

```text
Exact match
Prefix match
Token match
Fuzzy similarity
Keyword match
Usage frequency
Recency
Provider confidence
Query history
```

Conceptually:

```text
Final Score =
    lexical relevance
  + fuzzy relevance
  + usage boost
  + recency boost
  + contextual boost
```

The weighting system should be configurable and measurable.

---

# 11. Personalized Ranking

The launcher should learn from actual usage.

Example:

The user repeatedly searches:

```text
code
```

and selects:

```text
Visual Studio Code
```

The system should gradually increase the ranking of Visual Studio Code for that query.

However:

> Personalization must not overpower strong relevance.

An exact match should not be pushed below an unrelated application simply because the unrelated application was frequently used.

---

# 12. Search History

The system may store:

```text
Query
Selected Result
Timestamp
Provider
```

This data can be used to improve ranking.

History should be:

- Local
- Configurable
- Deletable
- Optional

The user should be able to disable history collection.

---

# 13. Calculator

The calculator provider shall recognize mathematical expressions.

Examples:

```text
25 * 17
```

```text
sqrt(144)
```

```text
120 km in miles
```

The calculator should return the result directly.

The implementation must avoid unsafe execution of arbitrary Python or shell code.

---

# 14. Shell Command Provider

A dedicated command mode should allow users to execute terminal commands.

Example:

```text
> docker ps
```

The `>` prefix explicitly identifies shell execution.

This is important because arbitrary command execution should never happen accidentally from normal search.

Potential features:

- Command preview
- Working directory
- Environment handling
- Exit status
- Output preview
- Confirmation for dangerous commands

---

# 15. File Search

A future file provider should support:

```text
file thesis.pdf
```

The system should use an index rather than recursively scanning the entire filesystem for every query.

Requirements:

- Incremental indexing
- Filesystem watching
- Exclusion rules
- Hidden-file configuration
- Search scopes

Potential search locations:

```text
Home directory
Configured directories
External drives
```

---

# 16. Clipboard History

The launcher may provide clipboard history.

Example:

```text
clip github
```

Requirements:

- Local storage
- Searchable history
- Configurable retention
- Clear history
- Optional disabling

Sensitive clipboard content should not be stored indefinitely by default.

---

# 17. Web Search

The web provider should allow users to search the web without manually opening a browser first.

Example:

```text
web reinforcement learning robotics
```

The provider should open the configured browser or provide a selectable search engine.

Supported search engines should be configurable.

---

# 18. URL Handling

The launcher should recognize URLs.

Example:

```text
https://github.com
```

The default action should open the URL using the user's configured browser.

---

# 19. System Actions

Future built-in actions may include:

```text
shutdown
restart
lock
logout
sleep
settings
```

Dangerous actions should require explicit confirmation where appropriate.

---

# 20. Plugin System

One of the most important architectural goals is extensibility.

The launcher shall expose a public plugin API.

A plugin should be able to add:

- Search providers
- Commands
- Actions
- Custom result types
- Configuration
- Optional background functionality

Example:

```text
Plugin
 ├── Metadata
 ├── Search
 ├── Execute
 └── Configuration
```

A developer should be able to create a plugin without modifying the launcher core.

---

# 21. Plugin Isolation

A broken plugin must not crash the launcher.

Plugin execution should have:

- Exception isolation
- Logging
- Timeouts where appropriate
- Enable/disable state
- Dependency validation

The plugin manager should be responsible for lifecycle management.

```text
Discover
   ↓
Validate
   ↓
Load
   ↓
Initialize
   ↓
Active
   ↓
Disable
```

---

# 22. Plugin Discovery

Plugins should be discoverable from configured locations.

Possible structure:

```text
plugins/
├── calculator/
├── web/
├── docker/
└── github/
```

The exact packaging mechanism may evolve as the project matures.

---

# 23. Background Processing

The GTK/UI thread must never perform expensive operations.

Background operations include:

- Application indexing
- File indexing
- Database operations
- Plugin initialization
- Filesystem watching
- Semantic embedding generation
- Slow external providers

Architecture:

```text
             UI Thread
                 │
                 ▼
          Query Manager
                 │
        ┌────────┴────────┐
        ▼                 ▼
   Fast Providers    Background Jobs
        │                 │
        └────────┬────────┘
                 ▼
             Results
```

---

# 24. Search Cancellation

Users can type faster than slow providers can respond.

Example:

```text
c
co
cod
code
```

Results for:

```text
c
```

should not overwrite newer results for:

```text
code
```

The system should support cancellation or stale-result rejection.

---

# 25. Database

SQLite will be used for local persistent state.

Potential tables:

```text
applications
usage_events
query_history
plugins
settings
schema_version
```

The database schema should be versioned.

Database migrations must be supported.

---

# 26. Indexing Strategy

The indexer should support:

### Initial indexing

Build the index when the application is installed or first launched.

### Incremental indexing

Only update entries that have changed.

### File watching

Where practical, filesystem events should trigger incremental updates.

### Manual rebuild

Provide a command to rebuild the index.

Example:

```text
launcher --rebuild-index
```

---

# 27. Performance Requirements

Performance is a core product requirement.

Initial engineering targets:

| Operation | Target |
|---|---:|
| Shortcut → launcher visible | < 100 ms |
| Normal application query | < 50 ms |
| Cached query | < 20 ms |
| UI blocking | None |
| Normal startup | No complete index rebuild |

These are targets, not claims.

Published benchmark numbers must be measured on known hardware and software configurations.

---

# 28. Resource Requirements

The launcher should remain lightweight.

It should avoid:

- Constant CPU-heavy background work
- Unnecessary network requests
- Repeated filesystem scans
- Excessive memory usage
- Permanent heavyweight AI models

Advanced features must be evaluated against their resource cost.

---

# 29. Semantic Search

Semantic search is an advanced feature.

Example:

```text
"code editor"
```

could return:

```text
Visual Studio Code
PyCharm
Neovim
```

even when the exact words are not present in application metadata.

The architecture should allow semantic search to be added without replacing the existing lexical search system.

Potential architecture:

```text
Lexical Search
       +
Semantic Search
       ↓
Hybrid Ranking
```

Semantic search should remain optional.

---

# 30. Context-Aware Ranking

A future ranking system may consider context.

Potential signals:

```text
Time of day
Current workspace
Recent applications
Recent queries
Usage frequency
Last-used time
```

Example:

If a user frequently opens:

```text
VS Code
Terminal
Browser
```

together during development sessions, the ranking engine may learn these patterns.

This feature must remain explainable and configurable.

---

# 31. Privacy

Privacy is a fundamental design principle.

By default:

- Search history stays local.
- Usage statistics stay local.
- Clipboard history stays local.
- No account is required.
- No analytics server is required.
- No user data is uploaded.

Network functionality should only occur when explicitly requested by a provider.

---

# 32. Configuration

The launcher should provide configurable settings for:

- Global shortcut
- Theme
- Result count
- Search providers
- Plugin enable/disable
- Search history
- Usage tracking
- File search directories
- Exclusion patterns
- Browser
- Search engine
- Ranking weights

Configuration should be human-readable where practical.

---

# 33. Theming

The UI should support at least:

- Light theme
- Dark theme
- System theme

The project should avoid excessive visual complexity.

The UI should communicate:

```text
Fast
Clean
Focused
Technical
```

---

# 34. Accessibility

The launcher should support:

- Keyboard-only operation
- Clear focus state
- Readable text
- Sufficient contrast
- Screen-reader-compatible UI where supported by the toolkit
- Configurable font scaling where practical

---

# 35. Error Handling

Errors should never silently crash the application.

Examples:

```text
Broken desktop entry
Missing application
Plugin exception
Database error
Unavailable web provider
Permission denied
Invalid configuration
```

Errors should be:

- Logged
- Isolated
- Recoverable where possible
- Presented to the user only when actionable

---

# 36. Logging

Logging should support multiple levels:

```text
DEBUG
INFO
WARNING
ERROR
```

Logs should help diagnose:

- Startup problems
- Plugin failures
- Search failures
- Indexing problems
- Performance problems

Debug logging should be configurable.

---

# 37. Observability

The project should measure real system behavior.

Important metrics:

```text
Startup time
Shortcut latency
Query latency
Indexing duration
Database latency
Plugin execution time
Memory usage
CPU usage
```

Performance optimization must be based on measurements rather than assumptions.

---

# 38. Testing Strategy

The project should contain:

### Unit tests

For:

- Query parsing
- Fuzzy matching
- Ranking
- `.desktop` parsing
- Database operations
- Plugin lifecycle

### Integration tests

For:

```text
Index → Search
Search → Ranking
Result → Execution
Plugin → Search Engine
Filesystem Change → Index Update
```

### Failure tests

For:

- Malformed desktop entries
- Missing icons
- Broken plugins
- Database failures
- Missing files
- Invalid commands

### Performance tests

Measure:

- Search latency
- Indexing time
- Database operations
- Startup behavior

---

# 39. Security Requirements

The launcher can eventually execute shell commands and system actions, therefore security must be considered carefully.

Requirements:

1. Normal search must never execute arbitrary shell commands.
2. Shell execution should require explicit command-mode syntax.
3. Plugin permissions should be documented.
4. External network access should be explicit.
5. Sensitive data should not be logged.
6. Clipboard data should have configurable retention.
7. Dangerous system actions should require confirmation.
8. Plugins must not automatically gain unrestricted privileges.

---

# 40. Linux Compatibility

The initial target is Linux desktop environments.

The project should aim to support:

- X11
- Wayland

Desktop-environment-specific behavior should be isolated behind interfaces.

Potential desktop environments:

```text
GNOME
KDE Plasma
XFCE
Cinnamon
```

Support should be tested rather than assumed.

---

# 41. Technology Stack

Initial implementation:

```text
Language:       Python 3.12+
UI:             GTK4 / PyGObject
Database:       SQLite
Testing:        pytest
Formatting:     Ruff
Type Checking:  mypy
CI:             GitHub Actions
Packaging:      TBD
```

Potential future technologies:

```text
Rust
Vector database / vector index
Local embedding models
D-Bus
Wayland/X11 integrations
```

Technology choices should be driven by measured requirements.

---

# 42. Proposed Architecture

```text
                         ┌─────────────────────┐
                         │       GTK UI        │
                         └──────────┬──────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │   Query Manager     │
                         └──────────┬──────────┘
                                    │
                 ┌──────────────────┼──────────────────┐
                 │                  │                  │
                 ▼                  ▼                  ▼
        ┌────────────────┐ ┌────────────────┐ ┌────────────────┐
        │ Search Engine  │ │ Plugin Manager │ │ Action Manager │
        └───────┬────────┘ └───────┬────────┘ └───────┬────────┘
                │                  │                  │
                ▼                  ▼                  ▼
        ┌────────────────┐ ┌────────────────┐ ┌────────────────┐
        │ Search         │ │ Plugins        │ │ OS Integration │
        │ Providers      │ │                │ │                │
        └───────┬────────┘ └────────────────┘ └────────────────┘
                │
                ▼
        ┌────────────────┐
        │ Ranking Engine │
        └───────┬────────┘
                │
                ▼
        ┌────────────────┐
        │ Index / SQLite │
        └───────┬────────┘
                │
        ┌───────┴────────┐
        ▼                ▼
   Application       File Index
     Index              Index
```

---

# 43. Proposed Repository Structure

```text
linux-launcher/
│
├── launcher/
│   ├── app/
│   │   ├── main.py
│   │   ├── application.py
│   │   └── lifecycle.py
│   │
│   ├── ui/
│   │   ├── window.py
│   │   ├── search_entry.py
│   │   ├── result_list.py
│   │   └── keybindings.py
│   │
│   ├── core/
│   │   ├── query.py
│   │   ├── result.py
│   │   ├── search.py
│   │   ├── ranking.py
│   │   └── events.py
│   │
│   ├── index/
│   │   ├── desktop.py
│   │   ├── filesystem.py
│   │   ├── database.py
│   │   └── watcher.py
│   │
│   ├── plugins/
│   │   ├── api.py
│   │   ├── manager.py
│   │   └── builtin/
│   │
│   ├── services/
│   │   ├── executor.py
│   │   ├── history.py
│   │   ├── settings.py
│   │   └── usage.py
│   │
│   └── config/
│
├── tests/
│   ├── unit/
│   ├── integration/
│   └── performance/
│
├── docs/
│
├── pyproject.toml
├── README.md
└── LICENSE
```

---

# 44. Development Roadmap

## Phase 1 — Minimal Launcher

```text
Global shortcut
      ↓
GTK window
      ↓
Application index
      ↓
Fuzzy search
      ↓
Launch application
```

The result must already be usable every day.

---

## Phase 2 — Search Architecture

Implement:

- SearchProvider
- Result
- SearchEngine
- RankingEngine
- Executor
- QueryManager

Separate responsibilities properly before adding many features.

---

## Phase 3 — Persistent Index

Implement:

- SQLite
- Application index
- Incremental updates
- Database migrations
- Index rebuild
- Filesystem watching

---

## Phase 4 — Plugin Architecture

Implement:

- Plugin API
- Plugin discovery
- Plugin lifecycle
- Plugin isolation
- Plugin configuration

Build several plugins as proof that the architecture actually works.

---

## Phase 5 — Personalization

Implement:

- Usage events
- Query history
- Frequency
- Recency
- Query-specific ranking

Evaluate ranking using real usage data.

---

## Phase 6 — Productivity Features

Implement:

- Calculator
- Shell
- File search
- Web search
- Clipboard
- System actions
- URL handling

---

## Phase 7 — Advanced Search

Implement:

- Semantic search
- Embeddings
- Hybrid retrieval
- Context-aware ranking

Only proceed if benchmarks show meaningful improvements.

---

## Phase 8 — Production Quality

Implement:

- Robust error handling
- Packaging
- Configuration UI
- Themes
- Accessibility
- Documentation
- Automated releases
- Performance benchmarks

---

# 45. Definition of Done

The project should not be considered complete merely because the launcher works.

A production-quality release should have:

- [ ] Daily usability
- [ ] Stable global shortcut
- [ ] Fast application search
- [ ] Persistent indexing
- [ ] Fuzzy search
- [ ] Ranking
- [ ] Usage personalization
- [ ] Plugin system
- [ ] Multiple useful plugins
- [ ] Background processing
- [ ] Search cancellation
- [ ] SQLite persistence
- [ ] Configuration
- [ ] Error isolation
- [ ] Automated tests
- [ ] CI
- [ ] Performance benchmarks
- [ ] Linux installation process
- [ ] Documentation
- [ ] Security review
- [ ] Demo video/GIF

---

# 46. Portfolio Requirements

The repository should demonstrate engineering, not just functionality.

The final GitHub repository should contain:

```text
README
Architecture Diagram
Screenshots
Demo GIF
Installation Guide
Plugin Development Guide
Performance Report
Testing Documentation
Architecture Decision Records
Benchmark Results
Release Notes
```

The README should clearly communicate:

> What problem does this solve?

> Why does it exist?

> How does it work?

> Why was the architecture designed this way?

> What makes it technically interesting?

---

# 47. Portfolio Description

Do not describe the project as:

> "A Flow Launcher clone for Linux."

Instead:

> **A keyboard-first Linux productivity platform built around modular search, persistent indexing, intelligent ranking, and an extensible plugin architecture.**

Short version:

> **A fast, privacy-first productivity launcher for Linux with fuzzy search, usage-aware ranking, background indexing, and a plugin system.**

---

# 48. Engineering Principles

The project follows these principles:

### Measure before optimizing.

### Keep the UI responsive.

### Separate retrieval from ranking.

### Separate core logic from OS integration.

### Prefer interfaces over hard-coded implementations.

### Keep plugins isolated.

### Keep user data local.

### Avoid unnecessary dependencies.

### Do not add AI without a measurable reason.

### Build for actual daily use.

### Prefer simple systems that can be understood and tested.

---

# 49. Long-Term Vision

The long-term goal is for this project to evolve from an application launcher into a **Linux productivity runtime**.

The user should be able to interact with their computer through one consistent interface:

```text
                    ┌─────────────────────┐
                    │      Launcher       │
                    └──────────┬──────────┘
                               │
       ┌───────────────┬───────┼────────┬───────────────┐
       ▼               ▼       ▼        ▼               ▼
 Applications       Files   Commands  Web          Clipboard
       │               │       │        │               │
       └───────────────┴───────┼────────┴───────────────┘
                               ▼
                         Ranking Engine
                               │
                               ▼
                         Best Action
```

The launcher should not simply tell the user where something is.

It should determine:

> **What is the most useful action for what the user just typed?**

That distinction is the core idea behind the project.

---

# 50. Final Success Criterion

The ultimate test is simple:

**If the developer stops using the normal application menu and starts using this launcher for their everyday Linux workflow, the project has succeeded.**

The second test is equally important:

**If another developer can understand the architecture, install the launcher, create a plugin, and contribute without rewriting the core, the project has succeeded as a software-engineering portfolio project.**