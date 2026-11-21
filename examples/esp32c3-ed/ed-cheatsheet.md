# ed cheat sheet

For V7 `ed` (1979) on the ESP32-C3. It has no prompt and no help, and its only
error message is `?`. Your text is safe after an error: just carry on.

## A first session

```
a
hello world
.
1s/world/wasmc/p
w notes.txt
q
```

`.` alone on a line ends text input. If nothing seems to respond, you are
probably still adding text: type `.`.

## Adding and changing text

| Command | What it does |
|---|---|
| `a` | adds text after the current line, ending with `.` |
| `i` | inserts text before the current line, ending with `.` |
| `c` | replaces the current line, ending with `.` |
| `d` | deletes the current line |
| `j` | joins the current line with the next one |
| `u` | undoes the last `s`, only if the current line is the one it changed |

## Viewing and moving

| Command | What it does |
|---|---|
| `p` | prints the current line |
| `l` | prints it with tabs and control characters visible |
| `=` | prints the number of lines |
| `.=` | prints the number of the current line |
| `5` | goes to line 5 and prints it |
| Enter | moves to the next line and prints it |

## Addresses

Most commands accept one or two addresses in front of them.

| Address | Meaning | Example |
|---|---|---|
| `.` | current line | `.p` |
| `$` | last line | `$p` |
| `3` | line 3 | `3d` |
| `3,7` | lines 3 to 7 | `3,7p` |
| `1,$` | the whole file (`,` and `%` alone do not work) | `1,$p` |
| `+2`, `-2` | two lines after or before the current one | `-2,+2p` |
| `/text/` | next line containing `text` | `/text/p` |
| `?text?` | previous line containing `text` | `?text?p` |
| `'a` | the line marked `a` | `'a,$p` |

## Searching and replacing

| Command | What it does |
|---|---|
| `s/old/new/` | replaces the first match on the current line |
| `s/old/new/g` | replaces all matches on the line |
| `1,$s/old/new/g` | replaces them in the whole file |
| `s/old/new/p` | replaces, then prints the line |
| `g/text/p` | prints all lines containing `text` |
| `g/text/d` | deletes all lines containing `text` |
| `g/text/s/old/new/` | replaces only on the lines containing `text` |
| `v/text/p` | prints all lines that do not contain `text` |

`s` answers `?` if nothing matches.

## Patterns

| Pattern | Matches |
|---|---|
| `.` | any character |
| `*` | the previous item, repeated zero or more times |
| `^`, `$` | start and end of the line |
| `[abc]`, `[a-z]` | one of these characters |
| `[^abc]` | any other character |
| `\.` | a literal `.` (the same for the other special characters) |
| `\(...\)` | a group, for `\1` to `\5` |

There is no `+`, `?` or `|`. In the replacement, `&` is the whole match and
`\1` is the first group: `s/\(a\)\(b\)/\2\1/` turns `ab` into `ba`.

## Moving lines

| Command | What it does |
|---|---|
| `2m5` | moves line 2 to after line 5 |
| `2t5` | copies line 2 to after line 5 |
| `2m0` | moves line 2 to the top |
| `ka` | marks the current line as `a` (`a` to `z`) |

## Files and quitting

| Command | What it does |
|---|---|
| `w` | saves to the current file |
| `w name` | saves to a file |
| `W name` | appends to a file |
| `e name` | opens a file, replacing the buffer |
| `E name` | the same, discarding unsaved changes without asking |
| `r name` | reads a file into the buffer, after the last line |
| `2r name` | reads it after line 2 |
| `f` | shows the current file name |
| `q` | quits; answers `?` once if there are unsaved changes |
| `Q` | quits without saving |

The commands that read or write print the number of bytes. A missing file gives
`?name`.

## On the ESP32-C3

| Key | What it does |
|---|---|
| Backspace | erases a character |
| `Ctrl-U` | erases the line |
| `Ctrl-D` | at the start of a line, end of input: like `q` |

- `q` starts a new, empty `ed`. The `files:` line above `ed ready` lists the
  saved files, and it also appears after a reset.
- A reset loses the text you have not saved. Saved files stay.
- Files cannot be deleted, and there is no shell (`!`).
- Text is 7-bit ASCII: `è` and the like only ring the bell.
- A line holds at most 509 characters, and a file name at most 63.

## Errors beyond a plain `?`

| Message | Meaning | What to do |
|---|---|---|
| `?name` | the file cannot be opened | check the name on the `files:` line |
| `?TMP` | the temp file is full, at about 127 KB | `w name`, then `e` to reload |
| `?MEM?` | too many lines, about 13000 | split the text into smaller files |
