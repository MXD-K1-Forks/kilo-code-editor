/* Kilo -- A very simple editor in less than 1-kilo lines of code (as counted
 *         by "cloc"). Does not depend on libcurses, directly emits VT100
 *         escapes on the terminal.
 *
 * -----------------------------------------------------------------------
 *
 * Copyright (C) 2016 Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2026, Mohammed Al-Shugaa (a.k.a MXD-K1) <hmdoonwork71@gmail.com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <ctype.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <sys/ioctl.h>
#include <termios.h>
#endif

#define KILO_VERSION "0.0.2"

#define EXIT_SIGNAL 1 /* Used to signal program end */

/* Syntax highlight types */
enum HL_Type {
    HL_NORMAL,     /* Any text that is not categorized */
    HL_NONPRINT,   /* Non-printable characters */
    HL_COMMENT,    /* Single line comment. */
    HL_ML_COMMENT, /* Multi-line comment. */
    HL_KEYWORD1,
    HL_KEYWORD2,
    HL_KEYWORD3,
    HL_KEYWORD4,
    HL_STRING,
    HL_NUMBER,
    HL_MATCH,
};

/* Flags */
#define HL_HIGHLIGHT_STRINGS             (1 << 0)
#define HL_HIGHLIGHT_NUMBERS             (1 << 1)
#define HL_HIGHLIGHT_NUMBER_LEADING_DOT  (1 << 2)
#define HL_HIGHLIGHT_NUMBER_TRAILING_DOT (1 << 3)

/* This structure represents a single line of the file we are editing. */
typedef struct {
    size_t idx;         /* Row index in the file, zero-based. */
    size_t size;        /* Size of the row, excluding the null terminator. */
    size_t rsize;       /* Size of the rendered row. */
    char* chars;        /* Row content. */
    char* render;       /* Row content "rendered" for screen (for TABs). */
    enum HL_Type* hl;   /* Syntax highlight type for each character in render.*/
} Row;

typedef struct {
    char* name;
    char** file_extensions;

    char** keywords_t1;
    char** keywords_t2;
    char** keywords_t3;
    char** keywords_t4;

    char line_comment_start[8];      /* Single line comment start */
    char multiline_comment_start[8]; /* Multiline comment start */
    char multiline_comment_end[8];   /* Multiline comment end */

    int flags;
} HL_Syntax;

typedef struct {
    int cx, cy;        /* Cursor x and y position in characters */
    int row_offset;    /* Offset of row displayed. */
    int col_offset;    /* Offset of column displayed. */
    size_t num_rows;   /* Number of rows in the file */
    Row* rows;         /* Rows */
    int dirty;         /* File modified but not saved. */
    char* filename;    /* Currently open filename */
    int is_crlf;       /* Does file lines end with CRLF? */
    HL_Syntax *syntax; /* Current syntax highlight, or NULL. */
} FileInfo;

typedef struct {
    int screen_rows;   /* Number of rows that we can show */
    int screen_cols;   /* Number of cols that we can show */
    int in_raw_mode;   /* Is terminal raw mode enabled? */
    FileInfo f_info;   /* Currently open file data */
} EditorData;

enum KEY_ACTION {
    KEY_NULL = 0,       /* NULL */
    CTRL_C = 3,         /* Ctrl-C */
    CTRL_F = 6,         /* Ctrl-F */
    TAB = 9,            /* Tab */
    CTRL_L = 12,        /* Ctrl+L */
    ENTER = 13,         /* Enter */
    CTRL_Q = 17,        /* Ctrl-Q */
    CTRL_S = 19,        /* Ctrl-S */
    ESC = 27,           /* Escape */
    BACKSPACE =  127,   /* Backspace */

    /* The following are just soft codes, not really reported by the terminal directly. */
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* We define a very simple "append buffer" structure, that is a heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
typedef struct {
    char* str;
    size_t len;
} Buffer;

/* =========================== Syntax highlights DB =========================
 *
 * In order to add a new syntax, define two arrays with a list of file name
 * matches and keywords. The file name matches are used in order to match
 * a given syntax with a given file name: if a match pattern starts with a
 * dot, it is matched as the last past of the filename, for example ".c".
 * Otherwise, the pattern is just searched inside the filename, like "Makefile".
 *
 * The four lists of keywords to highlight is just a list of words, is a way to have four different
 * categories of keywords so that you can have proper highlighting.
 *
 * Finally add a stanza in the HL_DB global variable with five arrays
 * of strings, and a set of flags in order to enable highlighting of
 * strings and numbers.
 *
 * The characters for single and multiline comments must be provided.
 *
 * There is no support to highlight patterns currently.
 *
 * For more see the C language example.
 */
HL_Syntax HL_DB[] = {
    /* C */
    {
        "C",
        (char*[]) {".c", ".h", NULL},
        (char*[]) {
            "if", "else", "goto", "switch", "case", "default", "for", "while", "do", "break", "continue",
            "return", "enum", "struct", "union", "typedef", "static", "extern", "register", "volatile", "sizeof",
            NULL
        },
        (char*[]) {
            "void", "char", "int", "short", "long", "float", "double", "bool", "auto",
            "const", "signed", "unsigned",
            NULL
        },
        (char*[]) {
            "true", "false", "NULL", NULL
        },
        (char*[]) {
            "#if", "#ifdef", "#endif", "#elif", "#ifndef", "#include", "#pragma", "#line",
            "#define", "#undef", "#warning", "#error", NULL
        },
        "//", "/*", "*/",
        HL_HIGHLIGHT_STRINGS | HL_HIGHLIGHT_NUMBERS |
            HL_HIGHLIGHT_NUMBER_LEADING_DOT | HL_HIGHLIGHT_NUMBER_TRAILING_DOT
    },
};

#define HL_DB_ENTRIES (sizeof(HL_DB) / sizeof(HL_DB[0]))

#ifdef _WIN32
DWORD terminal_mode; /* In order to restore at exit.*/
#endif

#ifdef __linux__
struct termios terminal_mode; /* In order to restore at exit.*/
#endif

/**
 * Replacement for posix's getline that works in both Windows and POSIX.
 */
ssize_t getline(char** lineptr, size_t *n , FILE *stream) {
    if (stream == NULL || n == NULL) return -1;

    if (*n == 0) {
        *n = 64;
    }

    if (*lineptr == NULL) {
        *n = 64;
        *lineptr = malloc(*n);
        if (*lineptr == NULL) return -1;
    }

    size_t len = 0;
    int c;

    while ((c = fgetc(stream)) != EOF) {
        if (len + 1 >= *n) {
            char *tmp  = realloc(*lineptr, *n * 2);
            if (tmp == NULL) return -1;
            *lineptr = tmp;
            *n *= 2;
        }

        (*lineptr)[len++] = (char) c;
        if (c == '\n') break;
    }

    if (c == EOF) {
        if (ferror(stream))
            return -1;

        if (len == 0)
            return -1;
    }

    (*lineptr)[len] = '\0';
    return (ssize_t) len;
}

int EnableRawMode(EditorData *e) {
    if (e->in_raw_mode) return 0; /* Already enabled. */
    if (!isatty(STDIN_FILENO)) return -1;

    #ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 1;

    if (!GetConsoleMode(h, &terminal_mode)) return -1;

    DWORD raw = terminal_mode;

    /* Modes: canonical off, echoing off, no signal chars (^Z,^C), disable post-processing */
    raw &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_PROCESSED_OUTPUT);

    /* Put terminal in raw mode */
    if (!SetConsoleMode(h, raw)) return -1;

    #endif

    #ifdef __linux__
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &terminal_mode) == -1) return -1; /* Save file descriptor state */

    raw = terminal_mode;

    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* output modes - disable post-processing */
    raw.c_oflag &= ~(OPOST);

    /* control modes - set 8 bit chars */
    raw.c_cflag |= (CS8);

    /* local modes - echoing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* control chars - set return condition: min number of bytes and timer. */
    raw.c_cc[VMIN] = 0; /* Return each byte, or zero for timeout. */
    raw.c_cc[VTIME] = 1; /* 100 ms timeout (unit is tens of second). */

    /* Put terminal in raw mode after flushing */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) return -1;
    #endif

    e->in_raw_mode = 1;
    return 0;
}

int DisableRawMode(EditorData *e) {
    if (!e->in_raw_mode) return 0; /* Already disabled. */

    #ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) return 1;

    if (!SetConsoleMode(h, terminal_mode)) return -1;
    #endif

    #ifdef __linux__
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal_mode) < 0) return -1;
    #endif

    e->in_raw_mode = 0;
    return 0;
}

Buffer BufferCreate(void) {
    Buffer buffer = {NULL, 0};
    return buffer;
}

int BufferAppend(Buffer *buf, const char* str, const size_t len) {
    const size_t str_len = len ? len : strlen(str);
    char* new = realloc(buf->str, buf->len + str_len);
    if (new == NULL) return -1;

    memcpy(new + buf->len, str, str_len);
    buf->str = new;
    buf->len += str_len;
    return 0;
}

void BufferFree(const Buffer *buf) {
    free(buf->str);
}

/* ============================ Window Utilities ============================ */

/**
 * Query the system to get the current cursor position.
 *
 * @return 0 on success, -1 on failure.
 */
int GetCursorPosition(int *rows, int *cols) {
    if (write(STDOUT_FILENO, "\033[6n", 4) != 4) return -1;

    ssize_t n;
    char buf[16];
    if ((n = read(STDIN_FILENO, buf, sizeof(buf) - 1)) <= 0) return -1;
    buf[n] = '\0';

    if (sscanf(buf, "\033[%d;%dR", rows, cols) != 2) return -1;
    return 0;
}

/**
 * Try to get the number of rows and columns in the current terminal.
 *
 * If the system did not return terminal dimensions,
 * the function will try to calculate terminal dimensions manually.
 *
 * @return 0 on success, -1 on error.
 */
int GetWindowSize(int *rows, int *cols) {
    #ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (GetConsoleScreenBufferInfo(h_stdout, &csbi)) {
        *rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        *cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        return 0;
    }
    #endif

    #ifdef __linux__
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
        return 0;
    }
    #endif


    if (write(STDOUT_FILENO, "\033[999;999H", 10) != 10) return -1;
    if (GetCursorPosition(rows, cols) == -1) return -1;
    return 0;
}

int UpdateWindowSize(EditorData *e) {
    if (GetWindowSize(&e->screen_rows, &e->screen_cols) == -1) return -1;
    e->screen_rows -= 2; /* Get room for status bar. */
    return 0;
}

void EditorClearScreen(void) {
    Buffer buf = BufferCreate();
    BufferAppend(&buf, "\033[2J", 0);
    BufferAppend(&buf, "\033[H", 0);
    write(STDOUT_FILENO, buf.str, buf.len);
    BufferFree(&buf);
}

/* ========================================================================== */

#define HL_MEM_SIZE sizeof(enum HL_Type) * row->rsize

void HL_Set(const Row *row, const enum HL_Type hl,
    const size_t start, const size_t end) {
    for (size_t i = start; i < end; i++) {
        row->hl[i] = hl;
    }
}

/**
 * Set every byte of row->hl that corresponds to every character
 * in the row to the right syntax highlight type.
 */
int EditorUpdateSyntax(Row *row, const HL_Syntax *syntax) {
    enum HL_Type* tmp = realloc(row->hl, HL_MEM_SIZE);
    if (tmp == NULL) return -1;
    row->hl = tmp;

    HL_Set(row, HL_NORMAL, 0, row->rsize);

    if (syntax == NULL) return 0; /* No syntax, everything is HL_NORMAL. */

    const char *lcs = syntax->line_comment_start;
    const char *mcs = syntax->multiline_comment_start;
    const char *mce = syntax->multiline_comment_end;
    int flags = syntax->flags;

    int in_token = 0, in_string = 0, in_number = 0, in_word = 0;

    size_t i = 0;
    size_t token_start = 0;
    char *p = row->render;

    while (1) {
        /* Handle non-printable chars. */
        if (!isprint(*p) && *p != '\0') {
            row->hl[i] = HL_NONPRINT;
            continue;
        }

        /* Handle single line comments */
        if (strncmp(p, lcs, strlen(lcs)) == 0) {
            /* From here to end is a comment */
            HL_Set(row, HL_COMMENT, i, row->rsize);
            break;
        }

        in_token = in_string || in_number || in_word;

        /* Handle strings ("" and '') */
        if (flags & HL_HIGHLIGHT_STRINGS && !in_token && (*p == '"' || *p == '\'')) {
            in_string = (int) *p; /* we assign to *p in order to know the closing pair */
            token_start = i;
        } else if (in_string && *p == in_string) {
            /* Check if the string is closed */
            HL_Set(row, HL_STRING, token_start, i + 1);
            in_string = 0;
        }

        /* Handle numbers */
        int is_number = 0;
        if (isdigit(*p)) is_number = 1;
        if (flags & HL_HIGHLIGHT_NUMBERS && !in_token && is_number) {
            in_number = 1; /* we assign to *p in order to know the closing pair */
            token_start = i;
        } else if (in_number && !is_number) {
            /* Check if the string is closed */
            HL_Set(row, HL_NUMBER, token_start, i);
            in_number = 0;
        }

        /* Handle keywords */
        if (!in_token && (isalpha(*p) || *p == '_' || *p == '#')) {
            /* '#' is a special case made to highlight c/c++ preprocessors. */
            in_word = 1;
            token_start = i;
        } else if (in_word && !(isalnum(*p) || *p == '_')) {
            HL_Set(row, HL_KEYWORD1, token_start, i);
            in_word = 0;
        }

        /* Handle multiline comments */
        // TODO

        /* Check if we reached EOL */
        if (*p == '\0') break;

        p++;
        i++;
    }

    return 0;
}

/**
 * Maps syntax highlight token types to terminal colors.
 */
int EditorMapSyntaxToColor(const enum HL_Type hl) {
    switch (hl) {
    case HL_COMMENT:
    case HL_ML_COMMENT: return 90;  /* gray */

    case HL_KEYWORD1: return 33;    /* yellow */
    case HL_KEYWORD2: return 32;    /* green */
    case HL_KEYWORD3: return 31;    /* red */
    case HL_KEYWORD4: return 34;    /* blue */

    case HL_STRING: return 35;      /* magenta */
    case HL_NUMBER: return 36;      /* cyan */

    case HL_MATCH: return 91;       /* bright red */

    case HL_NORMAL:
    default: return 37;             /* white */
    }
}

/* ======================= Editor rows implementation ======================= */

/**
 * Update the rendered version and the syntax highlight of a row.
 */
int EditorUpdateRow(const EditorData *e, Row *row) {
    char *tmp = realloc(row->render, row->size + 1);
    if (tmp == NULL) return -1;
    row->rsize = row->size;
    row->render = tmp;

    memcpy(row->render, row->chars, row->rsize);
    row->render[row->rsize] = '\0';

    /* Update the syntax highlighting attributes of the row. */
    if (EditorUpdateSyntax(row, e->f_info.syntax) == -1) return -1;

    return 0;
}

/**
 * Insert a row at the specified position, shifting the other rows on the bottom
 * if required.
 */
int EditorInsertRow(EditorData *e, const size_t at, const char *s, const size_t len) {
    /* Reallocate memory for the new row */
    Row* tmp = realloc(e->f_info.rows, sizeof(Row) * (e->f_info.num_rows + 1));
    if (tmp == NULL) return -1;
    e->f_info.rows = tmp;

    /* Shift the other rows below this row */
    memmove(e->f_info.rows + at + 1, e->f_info.rows + at, sizeof(Row) * (e->f_info.num_rows - at));
    for (size_t i = at + 1; i <= e->f_info.num_rows; i++) e->f_info.rows[i].idx++;

    /* Build the new row */
    e->f_info.rows[at].idx = at;
    e->f_info.rows[at].size = len;
    e->f_info.rows[at].rsize = 0;
    e->f_info.rows[at].render = NULL;
    e->f_info.rows[at].hl = NULL;

    char* tmp_str = malloc(len + 1);
    if (tmp_str == NULL) return -1;
    e->f_info.rows[at].chars = tmp_str;
    memcpy(e->f_info.rows[at].chars, s, len + 1);

    if (EditorUpdateRow(e, &e->f_info.rows[at]) == -1) return -1;
    e->f_info.num_rows++;

    return 0;
}

void EditorDelRow(EditorData *e, size_t at) {}

void EditorFreeRow(const Row *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
}

void EditorRowInsertChar(EditorData *e, Row *row, size_t at, int c) {}
void EditorRowAppendString(EditorData *e, Row *row, char *s, size_t len) {}
void EditorRowDelChar(EditorData *e, Row *row, size_t at) {}
void EditorInsertChar(EditorData *e, int c) {}
void EditorInsertNewline(EditorData *e) {}
void EditorDelChar(EditorData *e) {}


/**
 * Turn the editor rows into a single heap-allocated string.
 * Returns the pointer to the heap-allocated string and populate the
 * integer pointed by 'buffer_len' with the size of the string, excluding
 * the final null terminator.
 */
char* EditorRowsToString(EditorData *e, int *buffer_len) {
    // TODO
    return "";
}

/* ========================================================================== */

/* ========================= Editor events handling  ======================== */

void FileSave(EditorData *e);
void EditorFind(EditorData *e);

int EditorReadKey(void) {
    char c;
    ssize_t bytes;
    while ((bytes = read(STDIN_FILENO, &c, 1)) == 0) {} /* Ensure there is some input to process */

    switch (c) {
    case ESC: // TODO
        break;
    default:
        return c;
    }
}

/**
 * Handle cursor position change when arrow keys are pressed.
 */
void EditorMoveCursor(EditorData *e, int key) {}

/* When the file is modified, requires Ctrl-Q to be pressed `KILO_QUIT_TIMES` times before quitting. */
const int KILO_QUIT_TIMES = 3;

/**
 * Process events arriving from the standard input (by user).
 */
int EditorProcessInput(EditorData *e) {
    static int quit_times = KILO_QUIT_TIMES;

    int key = EditorReadKey();
    if (key == -1) return -1;

    switch (key) {
    case ENTER:
        EditorInsertNewline(e);
        break;
    case BACKSPACE:
    case DEL_KEY:
        EditorDelChar(e);
        break;
    case CTRL_Q:
        if (e->f_info.dirty && quit_times) {
            // TODO: Set message
            quit_times--;
        }
        EditorClearScreen();
        return EXIT_SIGNAL;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        EditorMoveCursor(e, key);
        break;
    case PAGE_UP:
    case PAGE_DOWN:
        break;   // TODO
    case TAB:    // TODO
    case CTRL_L: // TODO
    case KEY_NULL:
    case CTRL_C: /* Ignore Ctrl-C */
    case ESC:    /* Nothing to do for ESC in this mode. */
        break;
    case CTRL_S:
        FileSave(e);
        break;
    case CTRL_F:
        EditorFind(e);
        break;
    default:
        EditorInsertChar(e, key);
        break;
    }

    quit_times = KILO_QUIT_TIMES; /* Reset it to the original value. */
    return 0;
}

/* ========================================================================== */

int FileLoadContents(EditorData *e) {
    FILE *fp = fopen(e->f_info.filename, "r");
    if (fp == NULL) return -1;

    char *line = NULL;
    size_t n = 0;
    ssize_t line_len;
    while((line_len = getline(&line, &n, fp)) != -1) {
        if (line_len >= 2 && line[line_len - 2] == '\r' && line[line_len - 1] == '\n') {
            e->f_info.is_crlf = 1;
            line_len--;
        }

        if (line_len && (line[line_len - 1] == '\r' || line[line_len - 1] == '\n')) {
            line[--line_len] = '\0';
        }

        if (EditorInsertRow(e, e->f_info.num_rows, line, line_len) == -1) return  -1;
    }

    free(line);
    fclose(fp);
    return 0;
}

/**
 * Select the syntax highlight scheme depending on the filename.
 */
void FileSelectSyntax(EditorData *e) {
    for (unsigned int i = 0; i < HL_DB_ENTRIES; i++) {
        HL_Syntax syntax = HL_DB[i];
        char* ext = "";
        for (unsigned int j = 0; ext != NULL; j++) {
            ext = syntax.file_extensions[j];

            /* Compare extensions only if filename starts with a dot */
            if (ext && ext[0] == '.') {
                char* f_ext = strstr(e->f_info.filename, ".");
                if (f_ext == NULL) continue;
                if (strcmp(ext, f_ext) == 0) {
                    e->f_info.syntax = &HL_DB[i];
                    return;
                }
            }

            /* Otherwise, compare the whole name against the filename */
            if (ext && strcmp(ext, e->f_info.filename) == 0) {
                e->f_info.syntax = &HL_DB[i];
                return;
            }
        }
    }
}

void EditorDestroy(const EditorData *e) {
    free(e->f_info.filename);
}

int FileLoad(EditorData *e, const char* filename) {
    char* fn = malloc(strlen(filename) + 1);
    if (fn == NULL) exit(1);
    strcpy(fn, filename);

    e->f_info = (FileInfo) {
        .cx = 0, .cy = 0,
        .row_offset = 0,
        .col_offset = 0,
        .filename = fn,
        .num_rows = 0,
        .syntax = NULL,
        .rows = NULL,
        .is_crlf = 0,
        .dirty = 0,
    };

    FileSelectSyntax(e);
    if (FileLoadContents(e) == -1) return -1;
    return 0;
}

void FileSave(EditorData *e) {}

/* This function writes the whole screen using VT100 escape characters
 * starting from the logical state of the editor in the 'e'. */
void EditorRefreshScreen(const EditorData *e) {
    Buffer buf = BufferCreate();

    BufferAppend(&buf, "\033[?25l", 0); /* Hide cursor. */
    BufferAppend(&buf, "\033[H", 0);    /* Go home. */

    for (size_t i = 0; i < e->f_info.num_rows; i++) {
        const Row *row = &e->f_info.rows[i];
        const size_t len = row->rsize;

        int current_color = 37;
        for (size_t j = 0; j < len; j++) {
            int color = EditorMapSyntaxToColor(row->hl[j]);
            if (color != current_color) {
                current_color = color;

                char tmp[8];
                sprintf(tmp, "\033[%dm", current_color);
                BufferAppend(&buf, tmp, 0);
            }

            BufferAppend(&buf, row->render + j, 1);
        }

        BufferAppend(&buf, "\033[39m", 0);
        BufferAppend(&buf, "\r\n", 0);
    }

    BufferAppend(&buf, "\033[?25h", 0); /* Show cursor. */
    write(STDOUT_FILENO, buf.str, buf.len);
    BufferFree(&buf);
}

void EditorRunLoop(EditorData *e) {
    int exit = 0;
    while (exit != EXIT_SIGNAL) {
        EditorRefreshScreen(e);
        exit = EditorProcessInput(e);
    }
}

EditorData EditorInit(void) {
    EditorData editor;
    editor.screen_rows = 0;
    editor.screen_cols = 0;
    editor.in_raw_mode = 0;
    editor.f_info = (FileInfo) {0};
    UpdateWindowSize(&editor);
    return editor;
}

/* =============================== Find mode ================================ */

#define KILO_QUERY_LEN 256

void EditorFind(EditorData *e) {}

/* ========================================================================== */

int main(int argc, char* argv[]) {
    char* filename = {0};
    if (argc == 1) {
        char input[256];
        printf("Enter file: ");
        scanf("%s", input);
        printf("\n");
        filename = input;
    }
    else if (argc == 2) {
        filename = argv[1];
    }
    else {
        fprintf(stderr, "Usage: ./kilo <filename>\n");
        exit(1);
    }

    /* Setup terminal and get required info */
    EditorData editor = EditorInit();
    if (FileLoad(&editor, filename) == -1) return -1;
    if (EnableRawMode(&editor) == -1) {
        DisableRawMode(&editor);
        return -1;
    }

    /* Run */
    EditorRunLoop(&editor);

    /* Undo changes and exit */
    DisableRawMode(&editor);
    EditorDestroy(&editor);

    return 0;
}
