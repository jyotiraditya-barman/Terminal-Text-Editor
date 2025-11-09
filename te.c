//Make it a detailed  documtation with eash function code snippet and detail analysis of the errors and just exclude the simple function 
/***************************************************************************************************************
programer :Jyotiraditya Barman
Help : Deepseek to the cursor movment only to correct the movment bug, KILO TYPE input-output guid resource
*****************************************************************************************************************/
//INCLUDE FILES

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include<ctype.h>
#include <pwd.h>
#include <errno.h>

//DEFINE
#define CTRL_KEYS(k) ((k) & 0x1f)
#define MAX_LINE 1000
#define MAX_COL 512

//function declaration
void process_keypress();
char getch();
int getWindowSize(int *rows, int *cols) ;
void enable_raw_mode();
void reset();
void initBuffer();
void initEditor();
void no_echo();
void clrscr();
void move_cursor(int x, int y);
void draw_sc();
void disable_raw_mode() ;

//global variable
int FILE_SAVED =0;
char filename[256], expanded[512];
char *lines[MAX_LINE];
int cx = 0, cy = 0;
int row_offset = 0, col_offset = 0;  // Fixed typo: clo_offset -> col_offset

//global struct

struct termios orig_termios;
struct editorConfig {
int screenrows;
int screencols;
struct termios orig_termios;
};
struct editorConfig E;

int main() {
initEditor();
if (!isatty(STDOUT_FILENO)) {
fprintf(stderr, "Not a terminal!\n");
}
enable_raw_mode();
initBuffer();
no_echo();
clrscr();
move_cursor(1, 1);
while (1) {
//draw();
draw_sc(); //  draw_Buffer();
//scroll_if_needed();
//  draw_screen();
process_keypress();
}
exit:
disable_raw_mode();
reset();
system("clear");
return 0;
}

int ROW(){
struct winsize w;
ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
return w.ws_row;
}

int COL(){
struct winsize w;
ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
return w.ws_col;
}

void disable_raw_mode() {
tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
tcgetattr(STDIN_FILENO, &orig_termios);
atexit(disable_raw_mode);
struct termios raw = orig_termios;
raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
raw.c_oflag &= ~(OPOST);
raw.c_cflag |= (CS8);
raw.c_cc[VMIN] = 1;
raw.c_cc[VTIME] = 0;
tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void initBuffer() {
for (int i = 0; i < MAX_LINE; i++) {
lines[i] = calloc(MAX_COL, 1);
if (!lines[i]) { perror("calloc"); exit(1); }
lines[i][0] = '\0';
}
}

void no_echo() {
struct termios t;
tcgetattr(STDIN_FILENO, &t);
t.c_lflag &= ~ECHO;
tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void reset() {
struct termios t;
// get current attributes
tcgetattr(STDIN_FILENO, &t);
// restore canonical input & echo
t.c_lflag |= (ECHO | ICANON);
// restore signals (Ctrl+C, Ctrl+Z)
t.c_lflag |= ISIG;
// restore extended input processing
t.c_lflag |= IEXTEN;
// apply changes immediately
tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

void move_cursor(int x, int y) {
if (y < 1) y = 1;
if (cx < 1) x = 1;
printf("\033[%d;%dH", y, x);
}

void mvprintw(int row, int col, const char *fmt, ...) {
va_list ap;
va_start(ap, fmt);
printf("\033[%d;%dH", row, col);
vprintf(fmt, ap);
va_end(ap);
fflush(stdout);
}

void initEditor() {
if (getWindowSize(&E.screenrows, &E.screencols) == -1) exit(2);
}

void editorDrawRows() {
int y;
for (y = 0; y < E.screenrows; y++) {

write(STDOUT_FILENO, "~", 1);  
  
//write(STDOUT_FILENO, "\x1b[0m]", 1);  
if (y < E.screenrows - 1) {  
  write(STDOUT_FILENO, "\r\n", 2);  
}

}
}

void clrscr() {
//  void editorRefreshScreen() {
//printf("\033[46m");
system("clear");
write(STDOUT_FILENO, "\x1b[2J", 4);
write(STDOUT_FILENO, "\x1b[H", 3);
editorDrawRows();
write(STDOUT_FILENO, "\x1b[H", 3);
}
void expand_path(const char *input, char *output, size_t size) {
if (input[0] == '~') {
const char *home = getenv("HOME");
if (!home) home = getpwuid(getuid())->pw_dir;
snprintf(output, size, "%s%s", home, input + 1);
} else {
snprintf(output, size, "%s", input);
}
}

void open_file() {
//char filename[256];

//char filename[256], expanded[512];
// clear status bar
for (int k = 0; k < COL(); k++) {
mvprintw(ROW(), k, " ");
}
mvprintw(ROW(), 1, "open as: ");
move_cursor(10,E.screencols);
disable_raw_mode();   // normal typing
reset();               // show user input
move_cursor(10,E.screencols);//curs_set(1);          // show cursor
if (fgets(filename, sizeof(filename), stdin) == NULL) {
enable_raw_mode();
no_echo();
return;
}
// remove newline at end
filename[strcspn(filename, "\n")] = 0;
enable_raw_mode();
no_echo();
// curs_set(0);
if (strlen(filename) == 0) {
mvprintw(ROW()-1, 1, "open canceled.");
return;
}
// expand path (support ~)
expand_path(filename, expanded, sizeof(expanded));
FILE *fp = fopen(expanded, "r");
if (!fp) {
mvprintw(ROW()-1, 1, "Error opening file: %s", strerror(errno));
return;
}else{
// clear buffer
for (int i = 0; i < MAX_LINE; i++) {
memset(lines[i], 0, MAX_COL);
}
// load file line by line
int row = 0;
while (row < MAX_LINE && fgets(lines[row], MAX_COL, fp)) {
// remove trailing newline
lines[row][strcspn(lines[row], "\n")] = '\0';
row++;
}
fclose(fp);
cx = cy = 0; // reset cursor
mvprintw(ROW() - 1, 1, "Opened file: %s", filename);
}

// return to raw + no echo  
no_echo();  
enable_raw_mode();

}
void save_file() {FILE_SAVED=0;
//  char filename[256], expanded[512];
// clear status bar
if(strlen(filename)== 0){
for (int k = 0; k < COL(); k++) {
mvprintw(ROW(), k, " ");
}
mvprintw(ROW(), 1, "Save as: ");
move_cursor(10,E.screencols);
disable_raw_mode();   // normal typing
reset();               // show user input
//curs_set(1);          // show cursor
if (fgets(filename, sizeof(filename), stdin) == NULL) {
enable_raw_mode();
no_echo();
return;
}
// remove newline at end
filename[strcspn(filename, "\n")] = 0;
enable_raw_mode();
no_echo();
// curs_set(0);
if (strlen(filename) == 0) {
mvprintw(ROW()-1, 1, "Save canceled.");
return;
}
// expand path (support ~)
expand_path(filename, expanded, sizeof(expanded));
}else{
FILE *fp = fopen(expanded, "w");
if (!fp) {
mvprintw(ROW()-1, 1, "Error saving file: %s", strerror(errno));
return;
}
for (int i = 0; i < MAX_LINE; i++) {
if (strlen(lines[i]) > 0)
fprintf(fp, "%s\n", lines[i]);
}
fclose(fp);
FILE_SAVED = 1;
mvprintw(ROW()-1, 1, "Saved to %s", expanded);
}}
void saveFile() {
char filename[256];
printf("\x1b[47;30m");
// clear last line (status bar)
for (int k = 1; k <= COL(); k++) {
mvprintw(ROW(), k, " ");
}
// prompt at bottom
mvprintw(ROW(), 1, "Save filename: ");
fflush(stdout);
// switch to cooked mode
disable_raw_mode();
reset();
if (fgets(filename, sizeof(filename), stdin) != NULL) {
// strip newline
filename[strcspn(filename, "\n")] = '\0';
FILE *fp = fopen(filename, "w");
if (fp == NULL) {
mvprintw(ROW() - 1, 1, "Error: could not save file '%s'", filename);
} else {
// write buffer to file
for (int i = 0; i < MAX_LINE; i++) {
if (lines[i] && strlen(lines[i]) > 0) {
fprintf(fp, "%s\n", lines[i]);
}
}
fclose(fp);
mvprintw(E.screencols, 1, "Saved file: %s", filename);

}  
}  
  printf("\x1b[0m");  
// back to raw + no echo  
no_echo();  
enable_raw_mode();

}

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

char getch() {
char ch = 0;
if (read(STDIN_FILENO, &ch, 1) <= 0) return 0;
return ch;
}

void insert_char_at(int row, int col, char c) {
size_t len = strlen(lines[row]);
if (len >= MAX_COL - 2) return;
if ((size_t)col > len) col = len;
memmove(&lines[row][col + 1], &lines[row][col], len - col + 1);
lines[row][col] = c;
FILE_SAVED =1;
}
void delete_char_at(int row, int col) {
size_t len = strlen(lines[row]);
if (col < 0 || (size_t)col > len) return;
if (col == (int)len) return;
memmove(&lines[row][col], &lines[row][col + 1], len - col);
}
void make_room_for_line(int at) {
if (at < 0 || at >= MAX_LINE) return;
for (int i = MAX_LINE - 1; i > at; --i) {
strncpy(lines[i], lines[i - 1], MAX_COL - 1);
lines[i][MAX_COL - 1] = '\0';
}
lines[at][0] = '\0';
}
void remove_line_and_shift_up(int at) {
if (at < 0 || at >= MAX_LINE) return;
for (int i = at; i < MAX_LINE - 1; ++i) {
strncpy(lines[i], lines[i + 1], MAX_COL - 1);
lines[i][MAX_COL - 1] = '\0';
}
lines[MAX_LINE - 1][0] = '\0';
}
void split_line_at(int row, int col) {
if (row < 0 || row >= MAX_LINE - 1) return;
size_t len = strlen(lines[row]);
make_room_for_line(row + 1);
if ((size_t)col < len) {
strncpy(lines[row + 1], lines[row] + col, MAX_COL - 1);
lines[row + 1][MAX_COL - 1] = '\0';
lines[row][col] = '\0';
} else {
lines[row + 1][0] = '\0';
}
}
void join_line_with_prev(int row) {
if (row <= 0 || row >= MAX_LINE) return;
size_t prev_len = strlen(lines[row - 1]);
size_t cur_len = strlen(lines[row]);
size_t space_left = (MAX_COL - 1) - prev_len;
if (space_left > 0) {
size_t to_copy = cur_len < space_left ? cur_len : space_left;
strncat(lines[row - 1], lines[row], to_copy);
lines[row - 1][MAX_COL - 1] = '\0';
}
remove_line_and_shift_up(row);
}
int want_to_exit() {
if (FILE_SAVED == 1) {
// clear status line (bottom row)
for (int k = 1; k <= COL(); k++) {
mvprintw(ROW(), k, " ");
}
mvprintw(ROW(), 1, "File not saved. Do you want to exit (Y/N): ");
fflush(stdout);
// temporarily cooked mode for normal key input

int ch = getchar();  
    // back to raw mode  
   // no_echo();  
    //enable_raw_mode();  
    if (ch == 'y' || ch == 'Y') {  
        FILE_SAVED = 0;  
        return 1;   // exit allowed  
    } else {  
        return 0;   // stay in editor  
    }  
}  
return 1; // if FILE_SAVED == 0, no prompt, allow exit

}









/* double buffer state */
static char *draw_front = NULL;   /* previously-drawn lines (front buffer) */
static char *draw_back  = NULL;   /* what we'll draw this frame (back buffer) */
static int buf_rows = 0;
static int buf_cols = 0;
static int first_frame = 1;









static char screen_buffer_a[1024 * 1024];
static char screen_buffer_b[1024 * 1024];
static char *front_buffer = screen_buffer_a;
static char *back_buffer = screen_buffer_b;
/* --- Ensure viewport bounds --- */
static void ensure__viewport() {
    int screen_rows = E.screenrows - 1;
    int screen_cols = E.screencols;
    if (cy < row_offset) row_offset = cy;
    if (cy >= row_offset + screen_rows) row_offset = cy - screen_rows + 1;
    if (cx < col_offset) col_offset = cx;
}

/* --- Double-buffered ANSI draw --- */

/* --- Buffer management --- */
static int init_draw_buffers(int rows, int cols) {
    if (rows <= 0 || cols <= 0) return -1;
    if (rows == buf_rows && cols == buf_cols && draw_front && draw_back) return 0;

    /* free any existing */
    if (draw_front) { free(draw_front); draw_front = NULL; }
    if (draw_back)  { free(draw_back);  draw_back  = NULL;  }

    /* allocate contiguous memory: rows * (cols + 1) to store null-terminated lines */
    size_t per_line = (size_t)cols + 1;
    size_t total = (size_t)rows * per_line;
    draw_front = (char*)malloc(total);
    draw_back  = (char*)malloc(total);
    if (!draw_front || !draw_back) {
        free(draw_front); free(draw_back);
        draw_front = draw_back = NULL;
        buf_rows = buf_cols = 0;
        return -1;
    }

    /* initialize with spaces so comparisons work */
    for (int r = 0; r < rows; ++r) {
        memset(draw_front + r*per_line, ' ', cols);
        draw_front[r*per_line + cols] = '\0';
        memset(draw_back  + r*per_line, ' ', cols);
        draw_back[r*per_line + cols] = '\0';
    }

    buf_rows = rows;
    buf_cols = cols;
    first_frame = 1;
    return 0;
}

static void free_draw_buffers(void) {
    if (draw_front) { free(draw_front); draw_front = NULL; }
    if (draw_back)  { free(draw_back);  draw_back  = NULL;  }
    buf_rows = buf_cols = 0;
}

/* Make sure viewport/buffer sizes match E.* */
static int ensure_buffers_match_screen(void) {
    return init_draw_buffers(E.screenrows, E.screencols);
}

/* --- Main drawing function (double buffer + minimal writes) --- */
void draw_sc(void) {
    /* ensure user visible viewport updated (you already had this function) */
    ensure__viewport();

    int screen_rows = E.screenrows;
    int screen_cols = E.screencols;

    if (screen_rows <= 0 || screen_cols <= 0) return;

    if (ensure_buffers_match_screen() != 0) {
        /* allocation failed - fallback to simple clear+print */
        /* (rare; try best-effort fallback) */
        printf("\x1b[2J\x1b[H");
        for (int y = 0; y < screen_rows; ++y) {
            int idx = y + row_offset;
            if (idx >= MAX_LINE) {
                for (int i = 0; i < screen_cols; ++i) putchar(' ');
                putchar('\n');
                continue;
            }
            size_t len = strlen(lines[idx]);
            if ((int)len <= col_offset) {
                for (int i = 0; i < screen_cols; ++i) putchar(' ');
                putchar('\n');
                continue;
            }
            char *start = lines[idx] + col_offset;
            size_t avail = len - col_offset;
            size_t to_write = avail > (size_t)screen_cols ? (size_t)screen_cols : avail;
            fwrite(start, 1, to_write, stdout);
            for (int k = (int)to_write; k < screen_cols; ++k) putchar(' ');
            putchar('\n');
        }
        fflush(stdout);
        return;
    }

    size_t per_line = (size_t)screen_cols + 1;
    /* Fill back buffer lines */
    for (int y = 0; y < screen_rows; ++y) {
        char *lineptr = draw_back + y * per_line;
        int idx = y + row_offset;

        if (idx < 0 || idx >= MAX_LINE) {
            /* fill spaces */
            memset(lineptr, ' ', screen_cols);
            lineptr[screen_cols] = '\0';
            continue;
        }

        size_t len = strlen(lines[idx]);
        if ((int)len <= col_offset) {
            memset(lineptr, ' ', screen_cols);
            lineptr[screen_cols] = '\0';
        } else {
            char *start = lines[idx] + col_offset;
            size_t avail = len - col_offset;
            size_t to_write = avail > (size_t)screen_cols ? (size_t)screen_cols : avail;
            /* copy substring */
            memcpy(lineptr, start, to_write);
            if (to_write < (size_t)screen_cols) {
                memset(lineptr + to_write, ' ', screen_cols - (int)to_write);
            }
            lineptr[screen_cols] = '\0';
        }
    }

    /* status line content (we treat it as an extra "row" at screen_rows) */
    char status_line[1024];
    int status_y = screen_rows + 1;
    snprintf(status_line, sizeof(status_line), " TinyEdit 2.5 rows:%d cols:%d ", cy + 1, cx + 1);
    int status_len = (int)strlen(status_line);
    char *status_buf = (char*)malloc((size_t)screen_cols + 1);
    if (!status_buf) status_buf = NULL;
    if (status_buf) {
        if (status_len >= screen_cols) {
            memcpy(status_buf, status_line, screen_cols);
        } else {
            memcpy(status_buf, status_line, status_len);
            memset(status_buf + status_len, ' ', screen_cols - status_len);
        }
        status_buf[screen_cols] = '\0';
    }

    /* Build a single out buffer with only changed lines */
    size_t out_estimate = (size_t)screen_rows * (screen_cols + 8) + 1024;
    char *out = (char*)malloc(out_estimate);
    if (!out) {
        /* If can't allocate out buffer, fallback to printing everything at once */
        printf("\x1b[H"); /* move home */
        for (int y = 0; y < screen_rows; ++y) {
            fwrite(draw_back + y*per_line, 1, screen_cols, stdout);
            putchar('\n');
        }
        if (status_buf) {
            printf("\x1b[%d;1H\x1b[47;30m%.*s\x1b[0m", status_y, screen_cols, status_buf);
            free(status_buf);
        }
        /* position cursor */
        int screen_y = cy - row_offset;
        int screen_x = cx - col_offset;
        int cursor_row = screen_y + 1;
        int cursor_col = screen_x + 1;
        if (cursor_row < 1) cursor_row = 1;
        if (cursor_row > screen_rows) cursor_row = screen_rows;
        if (cursor_col < 1) cursor_col = 1;
        if (cursor_col > screen_cols) cursor_col = screen_cols;
        printf("\x1b[%d;%dH", cursor_row, cursor_col);
        fflush(stdout);
        return;
    }

    char *p = out;
    size_t remaining = out_estimate;

    /* hide cursor while writing to reduce flicker */
    int n = snprintf(p, remaining, "\x1b[?25l");
    p += (n >= 0 ? n : 0); remaining = (size_t)((out + out_estimate) - p);

    /* Update only changed lines */
    for (int y = 0; y < screen_rows; ++y) {
        char *front_line = draw_front + y * per_line;
        char *back_line  = draw_back  + y * per_line;
        if (first_frame || memcmp(front_line, back_line, (size_t)screen_cols) != 0) {
            /* move to line y+1, column 1 and write the line */
            n = snprintf(p, remaining, "\x1b[%d;1H", y + 1);
            if (n < 0) n = 0;
            p += n; remaining = (size_t)((out + out_estimate) - p);
            /* append the exact line content (no trailing newline) */
            memcpy(p, back_line, screen_cols);
            p += screen_cols; remaining = (size_t)((out + out_estimate) - p);
        }
    }

    /* status line - always draw it with inverted background */
    if (status_buf) {
        n = snprintf(p, remaining, "\x1b[%d;1H\x1b[47;30m", status_y);
        if (n < 0) n = 0;
        p += n; remaining = (size_t)((out + out_estimate) - p);
        memcpy(p, status_buf, screen_cols);
        p += screen_cols; remaining = (size_t)((out + out_estimate) - p);
        n = snprintf(p, remaining, "\x1b[0m");
        if (n < 0) n = 0;
        p += n; remaining = (size_t)((out + out_estimate) - p);
        free(status_buf);
    }

    /* move cursor to correct location (visible coordinates) and show cursor */
    int screen_y = cy - row_offset;
    int screen_x = cx - col_offset;
    int cursor_row = screen_y + 1;
    int cursor_col = screen_x + 1;
    if (cursor_row < 1) cursor_row = 1;
    if (cursor_row > screen_rows) cursor_row = screen_rows;
    if (cursor_col < 1) cursor_col = 1;
    if (cursor_col > screen_cols) cursor_col = screen_cols;

    n = snprintf(p, remaining, "\x1b[%d;%dH\x1b[?25h", cursor_row, cursor_col);
    if (n < 0) n = 0;
    p += n;

    /* single write to terminal */
    size_t out_len = (size_t)(p - out);
    ssize_t w = write(STDOUT_FILENO, out, out_len);
    (void)w; /* ignore partial write here; typical terminals accept whole buffer */

    free(out);

    /* swap buffers: what we drew becomes the "front" */
    {
        char *tmp = draw_front;
        draw_front = draw_back;
        draw_back  = tmp;
    }

    first_frame = 0;
}


















//chatgpt o[en file
void process_keypress() {
char c = getch();
if (c == CTRL_KEYS('q')) {
//   clrscr();struct termios t; tcgetattr(STDIN_FILENO, &t); t.c_lflag |= ECHO; tcsetattr(STDIN_FILENO, TCSANOW, &t);
if (want_to_exit()) {
struct termios t; tcgetattr(STDIN_FILENO, &t); t.c_lflag |= ECHO; tcsetattr(STDIN_FILENO, TCSANOW, &t);
disable_raw_mode();
reset();
system("clear");
exit(0);
}}

if(c==CTRL_KEYS('o')){  
    open_file();  
}  
if(c==CTRL_KEYS('s')){  
    save_file();  
}  
 else if (c == 127 || c == '\b') {  
    if (cx > 0) {  
        cx--;  
        delete_char_at(cy, cx);  
    } else if (cy > 0) {  
        int prev_len = strlen(lines[cy - 1]);  
        join_line_with_prev(cy);  
        cy--;  
        cx = prev_len;  
    }  
} else if (c == '\r' || c == '\n') {  
    split_line_at(cy, cx);  
    cy++;  
    cx = 0;  
} else if (c == '\033') {  
    char seq[2];  
    if (read(STDIN_FILENO, &seq[0], 1) == 0) return;  
    if (read(STDIN_FILENO, &seq[1], 1) == 0) return;  
    if (seq[0] == '[') {  
        switch (seq[1]) {  
            case 'A':  
                if (cy > 0) {  
                    cy--;  
                    int len = strlen(lines[cy]);  
                    if (cx > len) cx = len;  
                }  
                break;  
            case 'B':  
                if (cy < MAX_LINE - 1) {  
                    cy++;  
                    int len = strlen(lines[cy]);  
                    if (cx > len) cx = len;  
                }  
                break;  
            case 'C': {  
                int len = strlen(lines[cy]);  
                if (cx < len) {  
                    cx++;  
                } else if (cx == len && cy < MAX_LINE - 1) {  
                    cy++;  
                    cx = 0;  
                }  
                break;  
            }  
            case 'D':  
                if (cx > 0) {  
                    cx--;  
                } else if (cx == 0 && cy > 0) {  
                    cy--;  
                    cx = strlen(lines[cy]);  
                }  
                break;  
        }  
    }  
} else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {  
    size_t len = strlen(lines[cy]);  
    if (len < MAX_COL - 2) {  
        insert_char_at(cy, cx, c);  
        cx++;  
    }  
}  
if (cx < 0) cx = 0;  
if (cy < 0) cy = 0;  
if (cy >= MAX_LINE) cy = MAX_LINE - 1;  
if (cx >= MAX_COL - 1) cx = MAX_COL - 2;

}

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

//FUNCTION PROTOTYPES FAILED OR WORKED BUT NOT USED

/*
void open_file(){
char filename[20];
for(int k=0;k<E.screencols;k++){
mvprintw(E.screencols,k," ");}
mvprintw(E.screencols,1,"Filename: ");
move_cursor(10,E.screencols);
disable_raw_mode();
sscanf("%s",filename);
enable_raw_mode();
if(filename != NULL){
mvprintw(5,5,"saved!!!");
}
}
*/

//int getWindowSize(int *rows, int *cols) {
/*struct winsize ws;
if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
return -1;
} else {
*cols = ws.ws_col;
rows = ws.ws_row;
return 0;
}/
/*void draw_screen(){
clrscr();
int screen_rows = ROW()-1;
int screen_cols = COL()-1;
foe(int y=0;y<screen_rows;y++){
int file_y=row_offset+file_y;
if (file_y>=MAX_LINE)break;
int len= strlen(lines[file_y]);
if(col_offset<len){
mvprintw(cy)
}
}
}
*/