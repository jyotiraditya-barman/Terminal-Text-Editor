# TinyEdit — README & Developer Guide

**Author:** Jyotiraditya Barman
**Purpose:** Tiny, Kilo-inspired terminal text editor with double-buffered ANSI drawing and BASIC editing features.
**Scope of this README:** detailed documentation, implementation notes, function-level code snippets for the non-trivial functions, and a focused *deep-dive* into the **cursor movement bug** and the fixes you can apply.
*(You asked to exclude "simple" functions — I’ve omitted tiny getters/setters and trivial wrappers like `no_echo()`, `reset()`, and `ROW()/COL()` except when needed to explain interactions.)*

---

# Table of contents

1. Quick start (build & run)
2. High-level architecture
3. Important data structures & globals
4. Key functions (code + explained)

   * Terminal setup / raw mode
   * Window size / cursor probe
   * Buffer initialization
   * Double-buffered renderer (`draw_sc`) — full snippet & explanation
   * Cursor movement & input handling (`process_keypress`) — full snippet & analysis
   * File open / save (I/O) — issues & fixes
   * Line manipulation helpers (insert/delete/split/join)
5. Deep dive: Cursor movement bug — diagnosis and fixes
6. Known bugs & recommended fixes (detailed)
7. Testing checklist & debugging tips
8. Contributing & license

---

# 1) Quick start

Requirements:

* POSIX-like environment with a TTY (Linux, macOS).
* `gcc` or clang.

Build:

```bash
gcc -std=c11 -Wall -Wextra -o tinyedit tinyedit.c
```

Run:

```bash
./tinyedit
```

(Ctrl-Q to quit, Ctrl-O to open file, Ctrl-S to save — as implemented.)

---

# 2) High-level architecture

* Terminal mode management: `enable_raw_mode()` / `disable_raw_mode()` (wrap `termios`).
* In-memory text store: `lines[MAX_LINE]` each of `MAX_COL`.
* Input loop: `process_keypress()` reads raw bytes, interprets ASCII printable chars, newline, backspace, and ANSI arrow sequences.
* Rendering: `draw_sc()` — double-buffered renderer producing minimal ANSI writes by diffing a previous frame buffer `draw_front` vs `draw_back`.
* Cursor tracking: `cx`, `cy` (column and row in buffer), with `row_offset`/`col_offset` handling visible viewport.
* File IO: `open_file()`, `save_file()` (several variants in source; the README describes canonical, fixed behavior).

---

# 3) Important data structures & globals (excerpt)

```c
#define MAX_LINE 1000
#define MAX_COL 512

char *lines[MAX_LINE];
int cx = 0, cy = 0;             // cursor position in buffer (0-based)
int row_offset = 0, col_offset = 0; // viewport offsets
int FILE_SAVED = 0;
char filename[256], expanded[512];

struct editorConfig {
    int screenrows;
    int screencols;
    struct termios orig_termios;
} E;
```

---

# 4) Key functions — code and explanation

Below are the important functions (non-trivial) copied from your source with commentary and recommended fixes where necessary. I kept code snippets short, then explained behaviors and pitfalls.

---

## `enable_raw_mode()` & `disable_raw_mode()`

**Behavior:** switch terminal into raw mode (no echo, canonical off, signals off) to read single bytes.

```c
void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}
void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}
```

**Notes**

* `atexit(disable_raw_mode)` ensures cleanup on normal exit — good.
* `VMIN=1` with `VTIME=0` is appropriate for blocking single byte reads.

---

## `getWindowSize()` and `getCursorPosition()` — probing the terminal

**Use:** determine screen size using `ioctl()` fallback to cursor probe (`\x1b[6n`) when ioctl fails.

Key snippet (original with commentary):

```c
int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
    return 0;
}
```

**Pitfalls / fixes**

* This read will block until the terminal responds. If the terminal is in raw mode and the program has other pending input, behavior can be tricky. Usually works but ensure no stray bytes (e.g., if previous reads consumed something) — robust implementations set a short `VTIME` or temporarily set non-blocking reads and retry.
* **Conservative fix:** temporarily set the terminal to canonical/cooked mode (or adjust `VMIN/VTIME`) while probing; or use `ioctl()` first (which you do) and only probe when necessary.

`getWindowSize()`:

```c
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
```

**Notes**

* Using `\x1b[999C\x1b[999B` to force cursor to bottom-right is a common trick; subsequent `\x1b[6n` probes position.
* If `getCursorPosition` blocks, the fallback can hang.

---

## Buffer initialization: `initBuffer()`

```c
void initBuffer() {
    for (int i = 0; i < MAX_LINE; i++) {
        lines[i] = calloc(MAX_COL, 1);
        if (!lines[i]) { perror("calloc"); exit(1); }
        lines[i][0] = '\0';
    }
}
```

**Notes**

* Good: using `calloc` ensures `\0` initialisation.
* Remember to `free` on exit (not present in your main) to avoid leaks. Call free for each `lines[i]` and free draw buffers.

---

## Double-buffered renderer: `draw_sc()` — full function (present in your code)

This is the most sophisticated piece and is present in your source. I include a summary of the algorithm and then the important correctness notes.

**Algorithm summary**

1. Ensure viewport offsets consistent with `cx/cy` via `ensure__viewport()`.
2. Ensure draw buffers are allocated and match `E.screenrows`/`E.screencols`, else fallback to naive `printf` version.
3. Copy visible slices of `lines[]` into `draw_back` (back buffer).
4. Construct a single output buffer `out` that contains only the changed lines compared to `draw_front` (front buffer) plus the status line, plus cursor-move sequences.
5. Write `out` in one `write()` and swap the buffers.

**Important correctness notes & potential problems**

* `ensure__viewport()` must be called **after** any cursor movement. In your main loop you call `draw_sc()` each iteration before `process_keypress()` — that’s OK, but only if `process_keypress()` changes `cx/cy` and next iteration draws. If fast program flow or other loops exist, make sure `ensure__viewport()` runs immediately after movement.
* The current status line is created into `status_buf` and freed — good.
* There is an allocation `char *out = malloc(out_estimate)` and immediate write — handle `malloc` failure gracefully (current fallback prints full screen).
* **Edge case:** If `cx`/`cy` are outside `MAX_LINE` or negative, the code clamps later for cursor printing but earlier `ensure__viewport()` should prevent out-of-range copying. You still need to ensure `cy` can't exceed `MAX_LINE - 1`.

---

## Cursor-movement & input: `process_keypress()` (core for movement bug)

**Original (trimmed)**

```c
void process_keypress() {
    char c = getch();
    if (c == CTRL_KEYS('q')) { if (want_to_exit()) { disable_raw_mode(); reset(); system("clear"); exit(0); } }
    if (c == CTRL_KEYS('o')) open_file();
    if (c == CTRL_KEYS('s')) save_file();

    else if (c == 127 || c == '\b') { ... } // Backspace
    else if (c == '\r' || c == '\n') { split_line_at(cy, cx); cy++; cx = 0; }
    else if (c == '\033') {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) == 0) return;
        if (read(STDIN_FILENO, &seq[1], 1) == 0) return;
        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': if (cy > 0) { cy--; int len = strlen(lines[cy]); if (cx > len) cx = len; } break;
                case 'B': if (cy < MAX_LINE - 1) { cy++; int len = strlen(lines[cy]); if (cx > len) cx = len; } break;
                case 'C': { int len = strlen(lines[cy]); if (cx < len) cx++; else if (cx == len && cy < MAX_LINE - 1) { cy++; cx = 0; } break; }
                case 'D': if (cx > 0) { cx--; } else if (cx == 0 && cy > 0) { cy--; cx = strlen(lines[cy]); } break;
            }
        }
    } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
        size_t len = strlen(lines[cy]); if (len < MAX_COL - 2) { insert_char_at(cy, cx, c); cx++; }
    }
    // clamp bounds
    if (cx < 0) cx = 0; if (cy < 0) cy = 0; if (cy >= MAX_LINE) cy = MAX_LINE - 1; if (cx >= MAX_COL - 1) cx = MAX_COL - 2;
}
```

**Problems & fixes (cursor movement-specific)**

1. **Escape sequence reading assumes exactly two bytes after ESC**

   * Many terminals emit longer sequences (e.g., `\x1b[1;5C` for Ctrl-Right, or `\x1bO` sequences for some keys). Reading exactly two bytes (`seq[0]` & `seq[1]`) is fragile.
   * **Fix:** read into a buffer with a small timeout or loop until a non-digit/letter terminator is seen. Or use a robust parser:

     ```c
     if (c == '\x1b') {
         char buf[8];
         int n = 0;
         // read first char(s) non-blocking or with VTIME short
         if (read(STDIN_FILENO, &buf[n], 1) == 1) n++;
         if (buf[0] == '[') {
             // read until letter (A..Z a..z)
             while (n < 7) {
                 if (read(STDIN_FILENO, &buf[n], 1) != 1) break;
                 if ((buf[n] >= 'A' && buf[n] <= 'Z') || (buf[n] >= 'a' && buf[n] <= 'z')) { n++; break; }
                 n++;
             }
             // now parse buf[0..n-1]
         }
     }
     ```
   * This reduces missed arrow keys and handles sequences like `ESC [ 1 ; 5 C`.

2. **`move_cursor` function bug (logic error)**
   Your `move_cursor`:

   ```c
   void move_cursor(int x, int y) {
       if (y < 1) y = 1;
       if (cx < 1) x = 1;   // <-- BUG: uses 'cx' instead of 'x'
       printf("\033[%d;%dH", y, x);
   }
   ```

   * **Symptom:** calling `move_cursor(5, 1)` might set `x` to 1 erroneously if `cx < 1`. This corrupts explicit cursor moves.
   * **Fix:**

     ```c
     void move_cursor(int x, int y) {
         if (x < 1) x = 1;
         if (y < 1) y = 1;
         printf("\033[%d;%dH", y, x);
         fflush(stdout);
     }
     ```

3. **Viewport update timing**

   * `ensure__viewport()` is responsible for adjusting `row_offset`/`col_offset` given `cx/cy`. Ensure you call it *after* any cursor update (move) or before rendering. In your main loop you call `draw_sc()` then `process_keypress()`; that means any movement from `process_keypress()` isn't immediately reflected until the next `draw_sc()` — which is fine if you have a tight loop, but could cause perceived lag if input handling or other blocking occurs.
   * **Recommendation:** after processing a key that moves the cursor, call `ensure__viewport()` (or simply call `draw_sc()` immediately after processing the key).

4. **Right-arrow behavior at end of line**

   * Current code moves cursor to start of next line when `cx == len`. That is desirable, but ensure `cy < MAX_LINE` and that when moving to next line you clamp `cx` to the new line's length if needed.

5. **Cursor column vs buffer column collision with `col_offset`**

   * When `cx` grows beyond the visible `col_offset + screencols`, the viewport needs to horizontally scroll by updating `col_offset`. `ensure__viewport()` currently only checks vertical repositioning for `row_offset` and only compares `cx < col_offset` but the function in code appears to only set `if (cx < col_offset) col_offset = cx;` and doesn't clamp the right side properly (function in your code had `if (cy >= row_offset + screen_rows) row_offset = cy - screen_rows + 1; if (cx < col_offset) col_offset = cx;` — ensure also `if (cx >= col_offset + screen_cols) col_offset = cx - screen_cols + 1;`).
   * **Fix:** Expand `ensure__viewport()` as:

     ```c
     static void ensure__viewport() {
         int screen_rows = E.screenrows - 1;
         int screen_cols = E.screencols;
         if (cy < row_offset) row_offset = cy;
         if (cy >= row_offset + screen_rows) row_offset = cy - screen_rows + 1;
         if (cx < col_offset) col_offset = cx;
         if (cx >= col_offset + screen_cols) col_offset = cx - screen_cols + 1;
     }
     ```

---

## File open and save (`open_file()` / `save_file()`) — issues & recommended canonical implementation

**Problems seen in your code:**

* Mixed use of `filename` and `expanded` with inconsistent initialization.
* `save_file()` branch logic is inverted: the `if (strlen(filename)==0)` block asks for filename, otherwise it opens `expanded` which won't be set if `filename` previously empty.
* Using `disable_raw_mode()` + `reset()` is correct to allow normal `fgets()` input, but you must ensure you re-enable raw mode and `no_echo()` after.

**Canonical simplified & fixed `save_file()` flow:**

1. If `filename` empty → prompt user for a name (in cooked mode) → expand path → open file to write.
2. Otherwise use existing `expanded` (you must store expanded path after first open/save).
3. Write lines.

**Suggested corrected snippet:**

```c
void save_file() {
    // If no filename known, ask user
    if (strlen(filename) == 0) {
        // clear status bar
        for (int k = 1; k <= COL(); k++) mvprintw(ROW(), k, " ");
        mvprintw(ROW(), 1, "Save as: ");
        fflush(stdout);
        disable_raw_mode();
        reset();
        if (fgets(filename, sizeof(filename), stdin) == NULL) {
            enable_raw_mode();
            no_echo();
            return;
        }
        filename[strcspn(filename, "\n")] = '\0';
        if (strlen(filename) == 0) {
            mvprintw(ROW()-1, 1, "Save canceled.");
            enable_raw_mode();
            no_echo();
            return;
        }
        expand_path(filename, expanded, sizeof(expanded));
        enable_raw_mode();
        no_echo();
    }
    // Now write to expanded
    FILE *fp = fopen(expanded, "w");
    if (!fp) { mvprintw(ROW()-1, 1, "Error saving file: %s", strerror(errno)); return; }
    for (int i = 0; i < MAX_LINE; i++) {
        if (lines[i] && lines[i][0] != '\0') fprintf(fp, "%s\n", lines[i]);
    }
    fclose(fp);
    FILE_SAVED = 1;
    mvprintw(ROW()-1, 1, "Saved to %s", expanded);
}
```

---

## Line-manipulation helpers (insert/delete/split/join)

These are mostly correct, but note:

* `insert_char_at()` uses `memmove` to shift tail; ensure `col` not negative and not beyond `len`.
* `delete_char_at()` returns if `col == len` — that's reasonable (you can't delete beyond end).
* `split_line_at()` must ensure there is space (calls `make_room_for_line()`), good.
* `join_line_with_prev()` concatenates but respects `MAX_COL - 1`.

**Potential improvements**

* If `insert_char_at()` is used with `cy` out of range it will crash. Add defensive checks:

```c
void insert_char_at(int row, int col, char c) {
    if (row < 0 || row >= MAX_LINE) return;
    size_t len = strlen(lines[row]);
    if (len >= MAX_COL - 2) return;
    if (col < 0) col = 0;
    if ((size_t)col > len) col = len;
    memmove(&lines[row][col + 1], &lines[row][col], len - col + 1);
    lines[row][col] = c;
    FILE_SAVED = 0; // mark unsaved (I recommend 0 = unsaved, 1 = saved)
}
```

*(Note: your code sets `FILE_SAVED = 1` on insert; that contradicts convention — see "File saved flag" section below.)*

---

# 5) Deep dive: Cursor movement bug — diagnosis & fix

You mentioned: *"Deepseek to the cursor movment only to correct the movment bug, KILO TYPE input-output guid resource"*

Here is a focused diagnosis and recommended patch-list.

### Symptoms you might observe

* Arrow keys sometimes not moving the cursor or moving unexpectedly.
* Horizontal scrolling not triggered when `cx` moves beyond visible columns.
* `move_cursor()` behavior not matching requested coordinates.
* Race or lag where cursor position visually lags behind actual position.
* Unexpected jumps when moving left at column 0 or right beyond line end.

### Root causes (found in your code)

1. **`move_cursor()` bug** — uses `cx` instead of `x` when clamping the x coordinate.
2. **Insufficient ANSI CSI parsing** — reading exactly two bytes after `ESC` fails for multi-byte CSI sequences.
3. **`ensure__viewport()` missing right-side horizontal scroll logic** — so moving right off-screen doesn't update `col_offset`.
4. **Order of `draw_sc()` vs `process_keypress()`** in main loop can cause minor visual lag — movement is applied, but not drawn until next loop if loop timing is uneven.
5. **`FILE_SAVED` flag inverted/used inconsistently** — not directly a cursor bug, but cause for odd prompts that may disrupt flow.
6. **`cx/cy` boundary handling** — sometimes `cx` can be clamped incorrectly after a line join/split, leading to cursor jump.

### Minimal complete fixes (apply these in order)

1. **Fix `move_cursor()`**

```c
void move_cursor(int x, int y) {
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    printf("\033[%d;%dH", y, x);
    fflush(stdout);
}
```

2. **Improve `ensure__viewport()` to handle horizontal scroll**

```c
static void ensure__viewport() {
    int screen_rows = E.screenrows - 1;
    int screen_cols = E.screencols;
    if (cy < row_offset) row_offset = cy;
    if (cy >= row_offset + screen_rows) row_offset = cy - screen_rows + 1;
    if (cx < col_offset) col_offset = cx;
    if (cx >= col_offset + screen_cols) col_offset = cx - screen_cols + 1;
}
```

3. **Robustly parse escape sequences in `process_keypress()`**
   Replace the 2-byte reads with a small buffer parser with a short read timeout (or temporary VTIME tweak) to collect the complete CSI sequence before deciding:

```c
else if (c == '\x1b') {
    char seq[16];
    int n = 0;
    // read next bytes, but ensure we don't block forever
    // caller already has terminal in raw mode with VMIN=1, so read will block;
    // so read first byte, then read additional bytes until alpha terminator
    if (read(STDIN_FILENO, &seq[n], 1) == 1) {
        n++;
        if (seq[0] == '[') {
            // read until a letter A-Z or a-z
            while (n < (int)sizeof(seq)-1) {
                if (read(STDIN_FILENO, &seq[n], 1) != 1) break;
                if ((seq[n] >= 'A' && seq[n] <= 'Z') || (seq[n] >= 'a' && seq[n] <= 'z')) {
                    n++;
                    break;
                }
                n++;
            }
            // now seq[0..n-1] has CSI body; examine last char
            char final = seq[n-1];
            if (final == 'A') { /* up */ }
            else if (final == 'B') { /* down */ }
            else if (final == 'C') { /* right */ }
            else if (final == 'D') { /* left */ }
            // add Home/End/PageUp/PageDown parsing if needed
        }
    }
}
```

4. **After processing movement, immediately call `ensure__viewport()` and `draw_sc()`**
   This makes the movement reflect immediately visually. Example at the bottom of `process_keypress()`:

```c
// after clamp
if (cx < 0) cx = 0;
if (cy < 0) cy = 0;
if (cy >= MAX_LINE) cy = MAX_LINE - 1;
if (cx >= MAX_COL - 1) cx = MAX_COL - 2;

ensure__viewport();
draw_sc();
```

*(Keep in mind `draw_sc()` is heavy; but a single immediate refresh after movement is acceptable.)*

5. **Fix `FILE_SAVED` flag semantics**

* Use `FILE_SAVED = 1` to mean "on disk matches buffer" and `0` to mean "unsaved". Set `FILE_SAVED = 0` on any edit (insert/delete/split/join) and `FILE_SAVED = 1` after a successful save. Your current code sometimes sets to 1 on edit. Audit places that modify it.

6. **Fix `getCursorPosition()` blocking risk (optional but recommended)**

* Temporarily set `VMIN=0` and `VTIME=5` to give a small timeout to the read during probe, or temporarily switch to cooked mode when probing.
* Example: save current termios, set `raw.c_cc[VMIN] = 0; raw.c_cc[VTIME] = 5; tcsetattr(...);` then send `\x1b[6n]` and read; restore attributes.

---

# 6) Known bugs in code & recommended fixes (detailed list)

1. **`move_cursor()` uses `cx` for x-check**

   * Fix: use `if (x < 1) x = 1;`

2. **`open_file()` & `save_file()` inconsistent use of `filename`/`expanded` and `FILE_SAVED` semantics**

   * Fix: unify behavior: expand on input, store expanded path in `expanded[]`, set `filename` to basename for status, ensure `FILE_SAVED` flag is set correctly.

3. **`process_keypress()` escape parsing reads exactly two bytes**

   * Fix: use the robust parser shown earlier.

4. **`ensure__viewport()` missing right-side scroll logic**

   * Fix: add check `if (cx >= col_offset + screen_cols) col_offset = cx - screen_cols + 1;`

5. **`init_draw_buffers()` allocates `draw_front`/`draw_back` with `malloc` but not freed on exit**

   * Fix: call `free_draw_buffers()` on shutdown; also call `free` for `lines[]`.

6. **`status_y` calculation and position printing**

   * In `draw_sc()` you use `status_y = screen_rows + 1;` but the terminal rows are `1..screen_rows`. Make sure status line row index is within terminal bounds; when you print status with `\x1b[%d;1H` use a valid value `screen_rows` for bottom row (or `E.screenrows`).

7. **Potential buffer overflow in `status_line`**

   * `snprintf(status_line, sizeof(status_line), " TinyEdit 2.5 rows:%d cols:%d ", cy + 1, cx + 1);` — this is safe given size 1024, but sanitize values if screen_cols > int.

8. **`getCursorPosition()` & `getWindowSize()` fallback may block**

   * Add small timeout or temporary cooked mode when probing.

9. **`join_line_with_prev()` may overflow if previous line near `MAX_COL`**

   * You already check `space_left` but ensure `strncat` uses `to_copy` not default behavior.

10. **`want_to_exit()` logic inverted**

    * Your function checks `if (FILE_SAVED == 1) { prompt "File not saved..." }` — that reads inverted. Should be `if (FILE_SAVED == 0) { prompt }`.

---

# 7) Testing checklist & debugging tips

**Reproduce cursor movement bugs**

1. Start the editor with `./tinyedit`.
2. Insert text on multiple lines with varying lengths.
3. Move right repeatedly beyond visible screen width — ensure horizontal scroll occurs.
4. Move left from column 0 — should go to previous line end.
5. Use Home/End and PageUp/PageDown if you implement — ensure parsing.

**Unit tests to add (manual)**

* Insert characters at middle of line, verify `lines[row]` content.
* Split at middle of long line, verify next line saved tail.
* Join line with prev, verify concatenation and space/overflow behavior.
* Save/load round trip: save file, clear buffer, open file, confirm contents.

**Debugging with prints**

* Temporarily add debug lines printing `cx, cy, row_offset, col_offset` on status line.
* Use `fflush(stdout)` after raw-mode writes.

---

# 8) Contributing & license

If you want, I can:

* Prepare a cleaned up, refactored single-file `tinyedit.c` with the fixes applied (move cursor fix, robust CSI parsing, viewport fix, save/open consistency), and a test suite.
* Add comments and inline documentation.
* Add automatic `free()` on exit.

License: choose an open-source license (MIT recommended). Tell me which license and I will append the `LICENSE` file and update the README.

---

# Appendix — Quick patches (copy-paste)

**Fix `move_cursor()`**

```c
void move_cursor(int x, int y) {
    if (x < 1) x = 1;
    if (y < 1) y = 1;
    printf("\033[%d;%dH", y, x);
    fflush(stdout);
}
```

**Fix `ensure__viewport()`**

```c
static void ensure__viewport() {
    int screen_rows = E.screenrows - 1;
    int screen_cols = E.screencols;
    if (cy < row_offset) row_offset = cy;
    if (cy >= row_offset + screen_rows) row_offset = cy - screen_rows + 1;
    if (cx < col_offset) col_offset = cx;
    if (cx >= col_offset + screen_cols) col_offset = cx - screen_cols + 1;
}
```

**Robust escape parsing (replace your ESC handling)**

```c
else if (c == '\x1b') {
    char seq[16];
    int n = 0;
    if (read(STDIN_FILENO, &seq[n], 1) != 1) return;
    n++;
    if (seq[0] == '[') {
        // read until letter A-Za-z or end
        while (n < (int)sizeof(seq)-1) {
            if (read(STDIN_FILENO, &seq[n], 1) != 1) break;
            if ((seq[n] >= 'A' && seq[n] <= 'Z') || (seq[n] >= 'a' && seq[n] <= 'z')) {
                n++;
                break;
            }
            n++;
        }
        char final = seq[n-1];
        if (final == 'A') { if (cy > 0) { cy--; if (cx > (int)strlen(lines[cy])) cx = strlen(lines[cy]); } }
        else if (final == 'B') { if (cy < MAX_LINE - 1) { cy++; if (cx > (int)strlen(lines[cy])) cx = strlen(lines[cy]); } }
        else if (final == 'C') { int len = strlen(lines[cy]); if (cx < len) cx++; else if (cx == len && cy < MAX_LINE - 1) { cy++; cx = 0; } }
        else if (final == 'D') { if (cx > 0) cx--; else if (cx == 0 && cy > 0) { cy--; cx = strlen(lines[cy]); } }
        // handle other sequences if desired (Home/End/Del/PageUp/PageDown)
    }
}
```

**Fix `want_to_exit()` logic**

```c
int want_to_exit() {
    // if file unsaved (FILE_SAVED == 0) -> prompt
    if (FILE_SAVED == 0) {
        for (int k = 1; k <= COL(); k++) mvprintw(ROW(), k, " ");
        mvprintw(ROW(), 1, "File not saved. Do you want to exit (Y/N): ");
        fflush(stdout);
        disable_raw_mode();
        reset();
        int ch = getchar();
        enable_raw_mode();
        no_echo();
        if (ch == 'y' || ch == 'Y') {
            return 1;
        } else {
            return 0;
        }
    }
    return 1; // if FILE_SAVED==1, allow exit
}
```

---

If you want, I can:

* produce a PR-ready single-file patch with all the changes applied,
* or generate a cleaned, refactored `tinyedit.c` implementing everything above (including freeing memory on exit, correct `FILE_SAVED` semantics, and robust escape parsing).

Which would you like me to do next — produce the patched source file, or open a list of incremental commits/patches?
