# Writing Nexus plugins

A Nexus plugin is a program in any language that talks to Nexus over stdin and stdout, one JSON object per line. Nexus starts it on demand, sends it the text the user typed, and shows the results it sends back next to the built-in ones. Two plugins ship with Nexus and are the reference for everything below: `plugins/web/` (web search) and `plugins/system/` (lock, sleep, log out, restart, shut down). Both are short Python 3 scripts using only the standard library.

## Trust model: read this first

**Plugins are not sandboxed.** A plugin runs as a normal process with your user's rights. It can read your files, use the network and run anything you can run.

- A plugin can start a shell itself, or return a result whose `launch` argv is `["sh", "-c", "..."]`. Nexus runs a `launch` argv directly and cannot tell a harmless program from a shell.
- The `permissions` list in the manifest only restricts which kinds of result a plugin may return (see below). It does not restrict what the plugin process itself does.
- A plugin whose manifest claims the empty prefix `""` receives every query the user types that does not start with another provider's prefix, that is nearly every keystroke. The shipped `system` plugin does this on purpose.
- Only install plugins you trust and have read, exactly as you would any program you run.

What the host does enforce is listed under [What Nexus enforces](#what-nexus-enforces). That protects Nexus from a buggy plugin, not you from a malicious one.

## Where plugins live

Each plugin is a directory named after its id, containing `plugin.json` and the executable. Nexus looks, in this order, in:

1. `$XDG_DATA_HOME/nexus/plugins/` (default `~/.local/share/nexus/plugins/`)
2. `nexus/plugins/` under each entry of `$XDG_DATA_DIRS` (default `/usr/local/share:/usr/share`)

If the same id appears twice, the earlier directory wins. A plugin with an invalid manifest, or one whose id is already used by a built-in provider (or is `settings` or `aliases`), is skipped with a warning in the log and the others still load. Discovery only reads manifests; a plugin process is started on its first search and not before. Plugins are found at startup, so restart Nexus after adding one.

## The manifest

`plugin.json`, at most 64 KiB, a JSON object. This is the web plugin's:

```json
{
  "id": "web",
  "name": "Web Search",
  "version": "1.0.0",
  "api_version": 1,
  "exec": "web.py",
  "prefixes": ["web "],
  "permissions": ["open"]
}
```

| Field | Rules |
|---|---|
| `id` | Required. Only `a-z`, `0-9`, `_`, `-`. Must equal the directory name. Nexus stamps it on every result as the provider, so a plugin cannot pose as another one. |
| `name` | Display name. Optional (falls back to the id). |
| `version` | Free text. Optional. |
| `api_version` | Required. Must be `1`. Anything else makes the plugin skipped. |
| `exec` | Required. A path **relative** to the plugin directory. No absolute paths, no `..` component. After resolving symlinks it must still be inside the plugin directory, be a regular file, and be executable (`chmod +x`). It is run directly with no arguments and no shell, so it needs a shebang line (or be a binary). The working directory is the plugin directory. |
| `prefixes` | Optional list of strings. A query that starts with one of them goes to this plugin only (see the gotcha below). `""` means "every query that has no prefix". If two providers claim the same non-empty prefix, the first one registered keeps it. A plugin with no prefixes (and no `""`) never receives a search. An empty query is never sent to anyone. |
| `permissions` | Optional list of result kinds the plugin may return: `launch`, `open`, `copy`, `internal`. Anything else, including `shell`, makes the whole plugin refused. With no permissions a plugin can only return results that do nothing when chosen. For a plugin, `internal` behaves like `none`: Nexus does nothing itself and only sends you `executed`. |

### Prefix gotcha

Nexus trims the typed text before it looks for a prefix. A prefix that ends in a space, like `"web "`, is therefore not matched when the user has typed only `web ` (the trailing space is gone, leaving `web`). The plugin gets nothing until the user types a character after the space. That is why the web plugin returns nothing for an empty query, and why you will not see a "type something" hint from such a plugin. A prefix without a trailing space (for example `"emoji"`) does not have this problem, but then it also matches `emojis` and `emoji:smile`.

## The conversation

Messages are UTF-8 JSON objects, one per line, ending in `\n`. Each line, both ways, must be at most 64 KiB.

Nexus to plugin:

| `type` | Fields | Meaning |
|---|---|---|
| `hello` | `api_version` | First message after start. |
| `search` | `id`, `text`, `prefix`, `normalized` | Run a search. `text` is what the user typed with the prefix and surrounding spaces removed. `prefix` is the matched prefix, or `""`. `normalized` is `text` lower-cased. `id` is an integer. |
| `cancel` | `id` | The search with that id is no longer wanted. Optional to honour; any reply you still send is simply ignored. While your plugin is still busy with one search, Nexus skips it for newer keystrokes instead of queueing them. |
| `executed` | `result_id` | The user chose a result of yours; `result_id` is its `id`. Informational. |
| `shutdown` | | Exit now. |

Plugin to Nexus:

| `type` | Fields | Meaning |
|---|---|---|
| `hello` | `api_version` | Send it first; you do not have to wait for Nexus's `hello`. Until it arrives Nexus skips any other valid JSON object you send. `api_version` must be `1`, otherwise the plugin is disabled until Nexus restarts. |
| `results` | `id`, `results` | The answer to the `search` with the same `id`. |

Unknown `type` values, in either direction, are ignored: a plugin should skip messages it does not know, and skip lines that are not JSON objects.

### Result fields

Each entry of `results`:

| Field | Rules |
|---|---|
| `id` | Required string, unique among your results; sent back in `executed`. |
| `title` | Required string. |
| `subtitle`, `icon` | Optional strings. |
| `score` | Optional number, default 0. Clamped to the range [0, 1500]. Built-in scores are on the same scale, and 1500 stays below an exact match of the user's own alias, so a plugin cannot outrank it. A `score` that is not a number drops the result. |
| `action` | Optional: `launch`, `open`, `copy`, `internal` or `none` (default). Must be one of the manifest `permissions` unless `none`. |
| `payload` | For `open`: the URL or path to open. For `copy`: the text to put on the clipboard. |
| `argv` | For `launch`: required, a non-empty array of strings, the first non-empty. The program is run directly with these arguments. No shell, no string splitting, no expansion. |
| `terminal` | For `launch`: `true` to run it in a terminal. Default false. |
| `confirm` | `true` makes Nexus ask yes/no ("Run "<title>"?") before doing the action. Default false. Use it for anything destructive. |
| `metadata` | Optional object. Only string values are kept. |

### Example session with the web plugin

The commands below were run from `plugins/web/` (they open nothing; the plugin only builds a URL and the host decides what to do with it). Every line starting with `{"type": ...` below the command is the plugin's real output, and the first one is its `hello`.

```sh
printf '%s\n' \
  '{"type":"hello","api_version":1}' \
  '{"type":"search","id":1,"text":"rust lifetimes","prefix":"web ","normalized":"rust lifetimes"}' \
  '{"type":"search","id":2,"text":"","prefix":"web ","normalized":""}' \
  '{"type":"cancel","id":2}' \
  '{"type":"executed","result_id":"web"}' \
  '[]' \
  'garbage' \
  '{"type":"search"}' \
  '{"type":"search","id":3,"text":"c++ & \"x\"","prefix":"web ","normalized":"c++ & \"x\""}' \
  '{"type":"shutdown"}' | python3 web.py
```

```
{"type": "hello", "api_version": 1}
{"type": "results", "id": 1, "results": [{"id": "web", "title": "Search the web for rust lifetimes", "subtitle": "https://duckduckgo.com/?q=rust+lifetimes", "score": 1000, "action": "open", "payload": "https://duckduckgo.com/?q=rust+lifetimes"}]}
{"type": "results", "id": 2, "results": []}
{"type": "results", "id": 3, "results": [{"id": "web", "title": "Search the web for c++ & \"x\"", "subtitle": "https://duckduckgo.com/?q=c%2B%2B+%26+%22x%22", "score": 1000, "action": "open", "payload": "https://duckduckgo.com/?q=c%2B%2B+%26+%22x%22"}]}
```

Reading the output: search 1 returns one `open` result. Search 2 (empty text) returns an empty list, which is a valid answer. `cancel` and `executed` produce no reply, and the web plugin ignores them. The `[]`, `garbage` and id-less `search` lines are skipped. Search 3 shows the text being URL-encoded. `shutdown` ends the loop and the plugin exits.

The whole plugin is this loop. Everything else is building the result list:

```python
send({"type": "hello", "api_version": 1})
for line in sys.stdin:
    try:
        message = json.loads(line)
    except ValueError:
        continue
    if not isinstance(message, dict):
        continue
    if message.get("type") == "search" and "id" in message:
        send({"type": "results", "id": message["id"], "results": results(message.get("text", ""))})
    elif message.get("type") == "shutdown":
        break
```

Always flush stdout after each line (`print(..., flush=True)` in Python). A plugin that buffers its output looks like one that never answers.

### A `launch` result

The system plugin returns this kind. Nexus runs the `argv` as-is, and `confirm` makes it ask first:

```json
{"id": "shutdown", "title": "Shut down", "subtitle": "systemctl poweroff", "score": 700,
 "action": "launch", "argv": ["systemctl", "poweroff"], "confirm": true}
```

You can try the system plugin the same way, here from the top of the Nexus source tree. Its output depends on your session, but it never runs anything itself, it only describes what Nexus would run:

```sh
printf '%s\n' '{"type":"search","id":1,"text":"shut"}' | python3 plugins/system/system.py
```

## What Nexus enforces

Nexus checks everything a plugin sends. The checks on a single result drop only that result; the rest of the reply is kept.

**A result is dropped when:**
- it is not an object, or `id` or `title` is missing or not a string;
- `score` is present but not a number;
- `action` is not a string, is unknown, is `shell`, or is not in the manifest `permissions`;
- the action is `launch` and `argv` is missing, empty, has a non-string entry, an empty first entry, or any string containing a NUL byte;
- a `payload` contains a NUL byte.

**Applied to what is kept:**
- At most 50 results per reply; extras are discarded.
- `score` clamped to [0, 1500].
- `provider_id` is always the manifest id.
- `launch` runs `argv` directly, never through a shell. There is no `shell` kind for plugins.
- `confirm: true` always shows the yes/no dialog first; answering No does nothing.

**The plugin is killed and counted as a failure when:**
- it cannot be started, or Nexus cannot write its `hello` to it;
- its `hello` does not arrive within the first search's 200 ms (this includes the search being cancelled during start-up);
- it sends a line that is not valid JSON or not an object, or a line over 64 KiB;
- it exits;
- Nexus cannot write a message to it (for example because it stopped reading stdin and the pipe is full);
- it misses three searches in a row (see below).

A reply whose `id` does not match a search that is still waiting is ignored (a late answer to an old keystroke is harmless).

## Time, restarts and disabling

- **Budget.** A search gets 200 ms. On the first search (and after every restart) that includes starting the process and the `hello` handshake, and a slow start is not a miss: no `hello` in time is a failure, as listed above. A plugin that does not reply to a search in time contributes nothing to it; Nexus does not wait for it. Typing quickly cancels earlier searches (you will see `cancel`). Keep your answers fast; do slow work off the hot path.
- **Misses.** Three searches in a row with no reply in time count as one failure (the plugin is killed). Any reply resets the count.
- **Restart backoff.** After any failure from the list above the next search starts the plugin again, but only after waiting 1 s the first time, then 5 s, then 30 s. During the wait the plugin returns nothing. A failure after those three is the last one: the plugin is **disabled until Nexus restarts**. A good reply resets the failure count, so a plugin that works in between is not penalised for old failures.
- **Version mismatch.** A `hello` with the wrong `api_version` disables the plugin immediately.
- **Shutdown.** On exit Nexus sends `shutdown` to every plugin, gives all of them a shared 500 ms to quit, then kills the rest.

## Settings and logs

- **Settings.** There is no settings API. A plugin that needs configuration reads its own file; by convention `config.json` in its own directory (the working directory is the plugin directory, and the web plugin looks next to its script). For the web plugin:

  ```json
  {"engine": "https://www.google.com/search?q={query}"}
  ```

  Without the file, or with a template that has no `{query}`, it uses `https://duckduckgo.com/?q={query}`. Edit it by hand; nothing in Nexus reads this file. If you install into a system directory, copy the plugin to `~/.local/share/nexus/plugins/<id>/` to put a `config.json` next to it, because the earlier directory wins on the same id.
- **Logs.** Anything the plugin writes to stderr goes to `plugin.log` in the plugin directory (permissions `0600`). The file is emptied each time the plugin starts, so it holds the last run only. If Nexus cannot open that file, stderr is discarded instead: that happens when the plugin directory is not writable for you (a system directory such as `/usr/share/nexus/plugins/<id>/`) or when `plugin.log` is a symlink. To debug such a plugin, copy it to `~/.local/share/nexus/plugins/<id>/`, which takes precedence. Nexus's own messages about the plugin (failed, disabled, skipped) go to the Nexus log. Stdout is for protocol messages only: anything else printed there is invalid JSON and gets the plugin killed.

## Testing a plugin by hand

You do not need Nexus. A plugin is a program that reads lines and writes lines, so pipe lines into it, as in the web example above. Check that:

1. the first line out is a `hello` with `api_version` 1;
2. each `search` produces a `results` line with the same `id`, in well under 200 ms;
3. garbage lines, `[]` and unknown types do not crash it;
4. it exits after `shutdown` and when stdin closes.

`time` the pipe to see the cost of the first search, which includes interpreter start-up. Then copy the directory into `~/.local/share/nexus/plugins/<id>/`, make the script executable, restart Nexus and type your prefix. If nothing appears, look for the "skipped", "failed" or "disabled" line in the Nexus log, and at `plugin.log`.

## Checklist

- Directory name = `id`; `exec` relative and executable, with a shebang.
- Declare only the `permissions` you actually return.
- Send `hello` first, flush every line, skip unknown messages.
- Answer within 200 ms; answer with an empty list rather than not at all.
- Add `"confirm": true` to anything destructive.
- Remember the user's keystrokes go to you if you claim `""`: do not log or send them anywhere.
