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
#define HL_NORMAL      0 /* Any text that is not categorized */
#define HL_NONPRINT    1 /* Non-printable characters */
#define HL_COMMENT     2 /* Single line comment. */
#define HL_ML_COMMENT  3 /* Multi-line comment. */
#define HL_KEYWORD1    4
#define HL_KEYWORD2    5
#define HL_KEYWORD3    6
#define HL_KEYWORD4    7
#define HL_STRING      8
#define HL_NUMBER      9

/* Flags */
#define HL_HIGHLIGHT_STRINGS (1 << 0)
#define HL_HIGHLIGHT_NUMBERS (1 << 1)

/* This structure represents a single line of the file we are editing. */
typedef struct {
    int idx;            /* Row index in the file, zero-based. */
    int size;           /* Size of the row, excluding the null terminator. */
    int rsize;          /* Size of the rendered row. */
    char* chars;        /* Row content. */
    char* render;       /* Row content "rendered" for screen (for TABs). */
    unsigned char* hl;  /* Syntax highlight type for each character in render.*/
} Row;

typedef struct {
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
    int cx,cy;         /* Cursor x and y position in characters */
    int row_offset;    /* Offset of row displayed. */
    int col_offset;    /* Offset of column displayed. */
    int num_rows;      /* Number of rows in the file */
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
    CTRL_D = 4,         /* Ctrl-D */
    CTRL_F = 6,         /* Ctrl-F */
    CTRL_H = 8,         /* Ctrl-H */
    TAB = 9,            /* Tab */
    CTRL_L = 12,        /* Ctrl+L */
    ENTER = 13,         /* Enter */
    CTRL_Q = 17,        /* Ctrl-Q */
    CTRL_S = 19,        /* Ctrl-S */
    CTRL_U = 21,        /* Ctrl-U */
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
 * Otherwise, the pattern is just searched inside the filename, like "Makefile").
 *
 * The list of keywords to highlight is just a list of words, however if
 * a trailing '|' character is added at the end, they are highlighted in
 * a different color, so that you can have two different sets of keywords.
 *
 * Finally add a stanza in the HLDB global variable with two arrays
 * of strings, and a set of flags in order to enable highlighting of
 * comments and numbers.
 *
 * The characters for single and multi line comments must be exactly two
 * and must be provided as well (see the C language example).
 *
 * There is no support to highlight patterns currently. */
HL_Syntax HL_DB[] = {
    /* C */
    {
        (char*[]) {"Makefile", "makefile", NULL},
        NULL, NULL, NULL, NULL,
        "K", "K", "L",
        0
    },
    {
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
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HL_DB_ENTRIES (sizeof(HL_DB) / sizeof(HL_DB[0]))

#ifdef _WIN32
DWORD terminal_mode; /* In order to restore at exit.*/
#endif

#ifdef __linux__
struct termios terminal_mode; /* In order to restore at exit.*/
#endif

EditorData EditorInit(void) {
    EditorData editor;
    editor.screen_rows = 0;
    editor.screen_cols = 0;
    editor.in_raw_mode = 0;
    editor.f_info = (FileInfo) {0};
    return editor;
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

void LoadFile(EditorData *e, const char* filename) {
    char* fn = malloc(strlen(filename) + 1);
    if (fn == NULL) exit(2);

    strcpy(fn, filename);
    e->f_info = (FileInfo) {
        .cx = 0, .cy = 0,
        .filename = fn,
        .syntax = NULL,
        .dirty = 0,
    };
}

/**
 * Select the syntax highlight scheme depending on the filename.
 */
void FileSelectSyntax(EditorData *e) {
    for (unsigned int i = 0; i < HL_DB_ENTRIES; i++) {
        HL_Syntax syntax = HL_DB[i];
        char* ext = "";
        for (unsigned int j = 0;
             ext != NULL;
             j++) {
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

void LoadFileContents(EditorData *e) {

}

void EditorDestroy(const EditorData *e) {
    free(e->f_info.filename);
}

Buffer BufferCreate(void) {
    Buffer buffer = {NULL, 0};
    return buffer;
}

int BufferAppend(Buffer *buf, char* str) {
    const size_t str_len = strlen(str);
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

void EditorClearScreen(void) {
    Buffer buf = BufferCreate();
    BufferAppend(&buf, "\x1b[2J");
    BufferAppend(&buf, "\x1b[H");
    // write(STDOUT_FILENO, ab.b, ab.len);
    BufferFree(&buf);
}

void EditorRefreshScreen(void) {

}

int EditorProcessInput(void) {
    int key = 0;
    switch (key) {
    case CTRL_Q:
        return EXIT_SIGNAL;
    }
    return 0;
}

void EditorRunLoop(void) {
    int exit = 0;
    while (exit != EXIT_SIGNAL) {
        EditorRefreshScreen();
        exit = EditorProcessInput();
    }
}

int main(int argc, char* argv[]) {
    char* filename = {0};
    if (argc == 1) {
        // Pass that for now, will be changed later
        fprintf(stderr, "Usage: ./kilo <filename>\n");
        exit(1);
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
    LoadFile(&editor, filename);
    FileSelectSyntax(&editor);
    EnableRawMode(&editor);

    /* Run */
    EditorRunLoop();

    /* Undo changes and exit */
    DisableRawMode(&editor);
    EditorDestroy(&editor);

    return 0;
}
