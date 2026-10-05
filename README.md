# WinEventListener

A small Win32 GUI tool that listens for accessibility/window events system-wide via
[`SetWinEventHook`](https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwineventhook)
and shows them in a searchable, filterable list view.

## Features

- Hooks all events (`EVENT_MIN`..`EVENT_MAX`) out-of-context, skipping its own process.
- For each event, shows:

  | Column | Description |
  |---|---|
  | `#` | Sequence number |
  | Time | `dwmsEventTime` from the callback |
  | Event | Raw event `DWORD` (hex) |
  | Event Name | Symbolic name, e.g. `EVENT_OBJECT_FOCUS` (or AIA/OEM/UIA range) |
  | HWND | Window handle |
  | idObject | Object id, with `OBJID_*` name when known |
  | idChild | Child id |
  | PID / TID | Owning process/thread (falls back to the event thread when there is no HWND) |
  | Process | Executable name of the owning process |
  | Window Type | `Child`, `Popup`, `Topmost` (combined) or `Overlapped` |
  | Class | Window class name |
  | Title | Window text |

- Virtual (owner-data) list view that handles hundreds of thousands of rows; oldest
  entries are trimmed after 500,000 events.
- Pause, Auto-scroll, and Clear controls, plus a status line (`Showing X of Y events`).
- `Ctrl+A` selects all rows, `Ctrl+C` copies selected rows as tab-separated text.

## Filtering

Type into the **Filter** box; the list updates live. Terms are separated by spaces and
all must match (case-insensitive substring):

| Syntax | Meaning |
|---|---|
| `text` | Any column contains `text` |
| `-text` | No column contains `text` |
| `col:text` | The given column contains `text` |
| `-col:text` | The given column does not contain `text` |

Column keys: `seq`, `time`, `event` (`ev`), `name`, `hwnd`, `obj` (`idobject`),
`child` (`idchild`), `pid`, `tid`, `proc` (`process`), `type`, `class`, `title`.

Examples:

```
focus -proc:explorer
-name:locationchange type:topmost
class:Chrome name:namechange
```

## Notes

- Info such as process name, class, title and window type is captured at the time the
  event is received; windows may already be gone, in which case those fields are blank.
- Process names are cached per PID until **Clear** is pressed.
- Elevated processes may show `<unknown>` as the process name unless the tool runs elevated.

## Building

Requirements:

- Windows 10/11
- Visual Studio 2026 (or Visual Studio 2022 17.14+, which supports `.slnx`) with

Steps:

1. Open `WinEventListener.slnx` in Visual Studio.
2. Select a configuration (`Debug`/`Release`) and platform (`x64` recommended).
3. Build with **Build > Build Solution** (`Ctrl+Shift+B`).

Or from a Developer PowerShell:

```powershell
msbuild WinEventListener.slnx /p:
```

The executable is written to `x64\Release\WinEventListener.exe` (or the matching
configuration folder).

## Usage

Run `WinEventListener.exe`. Events start appearing immediately. Use the filter box to
narrow results, **Pause** to freeze capture, and **Clear** to reset.
