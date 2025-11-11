#ifndef MINILINE_H_
#define MINILINE_H_

#ifndef MINILINE_API
#define MINILINE_API
#endif

MINILINE_API char *mlReadLine(const char *prompt);

#endif // MINILINE_H_

#ifdef MINILINE_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

#define ML_ESC "\x1b"
#define ML_CSI ML_ESC"["

typedef struct mlHistoryEntry {
    int offset;
    int len;
} mlHistoryEntry;

typedef struct mlHistory {
    struct {
        mlHistoryEntry *els;
        int len, cap;
    } his;
    struct {
        int *els;
        int len, cap;
    } buf;
    int pos; // position in history
    int isRecall;
} mlHistory;

mlHistory mlHistoryGlobal = {0};

// typedef struct mlState mlState;
struct mlState {
    mlHistory *history;
#ifdef _WIN32
    HANDLE inp, out;
    DWORD dwOrigInpMode, dwOrigOutMode;
#else
    int ifd, ofd;
    struct termios origTerm;
#endif
} mlState;

struct mlState *st = &mlState;

enum mlKeySpecial {
    ML_KEY_CTRL_A = 1,
    ML_KEY_CTRL_B = 2,
    ML_KEY_CTRL_C = 3,
    ML_KEY_CTRL_D = 4,
    ML_KEY_CTRL_E = 5,
    ML_KEY_CTRL_F = 6,
    ML_KEY_CTRL_H = 8,
    ML_KEY_TAB    = 9,
    ML_KEY_CTRL_K = 11,
    ML_KEY_CTRL_L = 12,
    ML_KEY_ENTER  = 13,
    ML_KEY_CTRL_N = 14,
    ML_KEY_CTRL_P = 16,
    ML_KEY_CTRL_T = 20,
    ML_KEY_CTRL_U = 21,
    ML_KEY_CTRL_W = 23,
    ML_KEY_ESCAPE = 27,
    ML_KEY_BACKSPACE = 127,

    ML_KEY_NO_UNICODE = 0x10000000,
    ML_KEY_UP,
    ML_KEY_DOWN,
    ML_KEY_RIGHT,
    ML_KEY_LEFT,
    ML_KEY_DELETE,
    ML_KEY_HOME,
    ML_KEY_END,
};

#ifdef __cplusplus
#define ML_DECLTYPE_CAST(T) (decltype(T))
#else
#define ML_DECLTYPE_CAST(T)
#endif // __cplusplus

#define ML_DA_DEFAULT_CAP 16

#define mlDaReserve(da, new_cap, default_cap) \
    do {\
        if ((new_cap) > (da)->cap) { \
            if ((da)->cap == 0) { (da)->cap = (default_cap); } \
            while ((new_cap) > (da)->cap) { (da)->cap *= 2; } \
            (da)->els = ML_DECLTYPE_CAST((da)->els)realloc( \
                (da)->els, (da)->cap*sizeof(*(da)->els)); \
            assert((da)->els != NULL && "out of memory"); \
        } \
    } while (0)
#define mlDaAppend(da, el) \
    do { \
        mlDaReserve((da), (da)->len+1, ML_DA_DEFAULT_CAP); \
        (da)->els[(da)->len++] = (el); \
    } while (0)
#define mlDaAppendN(da, new_els, n) \
    do { \
        mlDaReserve((da), (da)->len+(n), ML_DA_DEFAULT_CAP); \
        memcpy((da)->els+(da)->len, (new_els), (n)*sizeof(*(da)->els)); \
        (da)->len += n; \
    } while (0)
#define mlDaLast(da) (da)->els[assert((da)->len>0 && "empty array"), (da)->len-1]
#define mlDaFree(da) free((da).els)


#ifdef _WIN32
static void mlEnableVt(void)
{
    DWORD dwMode;
    GetConsoleMode(st->out, &dwMode);
    dwMode |= ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(st->out, dwMode);
}
#endif

static void mlEnableRawMode(void)
{
#ifdef _WIN32
    DWORD dwMode;
    GetConsoleMode(st->inp, &dwMode);
    dwMode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(st->inp, dwMode);
    GetConsoleMode(st->out, &dwMode);
    dwMode &= ~(ENABLE_WRAP_AT_EOL_OUTPUT);
    SetConsoleMode(st->out, dwMode);
#else
    struct termios term;
    tcgetattr(st->ifd, &term);
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    term.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    term.c_cflag |= (CS8);
    /* local modes - choing off, canonical off, no extended functions,
     * no signal chars (^Z,^C) */
    term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    /* control chars - set return condition: min number of bytes and timer.
     * We want read to return every single byte, without timeout. */
    term.c_cc[VMIN] = 1; term.c_cc[VTIME] = 0; /* 1 byte, no timer */
    tcsetattr(st->ifd, TCSAFLUSH, &term);
#endif
}

void mlBegin(void)
{
    if (st->history == NULL) {
        st->history = &mlHistoryGlobal;
    }
#ifdef _WIN32
    st->inp = GetStdHandle(STD_INPUT_HANDLE);
    st->out = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleMode(st->inp, &st->dwOrigInpMode);
    GetConsoleMode(st->out, &st->dwOrigOutMode);
    mlEnableVt();
#else
    st->ifd = STDIN_FILENO;
    st->ofd = STDOUT_FILENO;
    tcgetattr(st->ifd, &st->origTerm);
#endif
    mlEnableRawMode();
}

void mlEnd(void)
{
#ifdef _WIN32
    SetConsoleMode(st->inp, st->dwOrigInpMode);
    SetConsoleMode(st->out, st->dwOrigOutMode);
#else
    tcsetattr(st->ifd, TCSAFLUSH, &st->origTerm);
#endif
}

int mlGetColumns(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(st->out, &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize ws;
    if (ioctl(st->ofd, TIOCGWINSZ, &ws) == -1) {
        return 80; // default
    }
    return ws.ws_col;
#endif
}

#ifndef _WIN32
char mlReadUtf8Char(void)
{
    char c;
    ssize_t nread;
    while ((nread = read(st->ifd, &c, 1)) == 0);
    assert(nread == 1);
    return c;
}
int mlReadUtf32(void)
{
    int cp;
    char c = mlReadUtf8Char();
    if ((c & 0x80) == 0) {
        cp = c;
    } else if ((c & 0xE0) == 0xC0) {
        char c2 = mlReadUtf8Char();
        cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
    } else if ((c & 0xF0) == 0xE0) {
        char c2 = mlReadUtf8Char();
        char c3 = mlReadUtf8Char();
        cp = ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
    } else if ((c & 0xF8) == 0xF0) {
        char c2 = mlReadUtf8Char();
        char c3 = mlReadUtf8Char();
        char c4 = mlReadUtf8Char();
        cp = ((c & 0x07) << 18) | ((c2 & 0x3F) << 12) | ((c3 & 0x3F) << 6) | (c4 & 0x3F);
    } else {
        assert(0 && "invalid utf8");
    }
}
#endif

int mlReadKey(void)
{
#ifdef _WIN32
    DWORD count;
    int highSurrogate = 0, c = 0;
    INPUT_RECORD rec;
    for (;;) {
        int key;
        DWORD event = WaitForSingleObject(st->inp, INFINITE);
        assert(event==WAIT_OBJECT_0);
        ReadConsoleInputW(st->inp, &rec, 1, &count);
        if (rec.EventType != KEY_EVENT) {
            continue;
        }
        // Windows provides for entry of characters that are not on your keyboard by sending the
        // Unicode characters as a "key up" with virtual keycode 0x12 (VK_MENU == Alt key) ...
        // accept these characters, otherwise only process characters on "key down"
        if (!rec.Event.KeyEvent.bKeyDown && (rec.Event.KeyEvent.wVirtualKeyCode != VK_MENU)) {
            continue;
        }
        key = rec.Event.KeyEvent.uChar.UnicodeChar;
        if (key == 0) {
            switch (rec.Event.KeyEvent.wVirtualKeyCode) {
            case VK_UP:     return ML_KEY_UP;
            case VK_DOWN:   return ML_KEY_DOWN;
            case VK_RIGHT:  return ML_KEY_RIGHT;
            case VK_LEFT:   return ML_KEY_LEFT;
            case VK_DELETE: return ML_KEY_DELETE;
            case VK_HOME:   return ML_KEY_HOME;
            case VK_END:    return ML_KEY_END;
            default:
                continue;
            }
        } else if (key >= 0xD800 && key <= 0xDBFF) {
            highSurrogate = key - 0xD800;
            continue;
        } else {
            // we got a real character, return it
            if (key >= 0xDC00 && key <= 0xDFFF) {
                key -= 0xDC00;
                key |= highSurrogate << 10;
                key += 0x10000;
            }
            c = key;
            break;
        }
    }
    return c;
#else
    int seq[3];
    int key = mlReadUtf32();
    if (key == ML_KEY_ESCAPE) {
        // escape sequence
        seq[0] = mlReadUtf32();
        if (seq[0] == '[') {
            seq[1] = mlReadUtf32();
            if (seq[1] >= '0' && seq[1] <= '9') {
                seq[2] = mlReadUtf32();
                if (seq[2] == '~') {
                    switch (seq[1]) {
                    case '1': return ML_KEY_HOME;
                    case '3': return ML_KEY_DELETE;
                    case '4': return ML_KEY_END;
                    case '7': return ML_KEY_HOME;
                    case '8': return ML_KEY_END;
                    }
                }
            } else {
                switch (seq[1]) {
                case 'A': return ML_KEY_UP;
                case 'B': return ML_KEY_DOWN;
                case 'C': return ML_KEY_RIGHT;
                case 'D': return ML_KEY_LEFT;
                case 'H': return ML_KEY_HOME;
                case 'F': return ML_KEY_END;
                }
            }
        } else if (seq[0] == 'O') {
            seq[1] = mlReadUtf32();
            switch (seq[1]) {
            case 'H': return ML_KEY_HOME;
            case 'F': return ML_KEY_END;
            }
        }
        return ML_KEY_ESCAPE;
    }
    return key;
#endif
}

void mlHistoryFree(mlHistory h)
{
    mlDaFree(h.his);
    mlDaFree(h.buf);
}

mlHistoryEntry mlHistoryAtPos(mlHistory *h)
{
    int index = h->his.len - 1 - h->pos;
    assert(index >= 0 && index <= h->his.len);
    // if (index == h->his.len) {
    //     mlHistoryEntry empty = {.offset = h->buf.len, .len = 0};
    //     return empty;
    // }
    return h->his.els[index];
}

void mlHistoryReset(mlHistory *h)
{
    // reset position to the end
    h->pos = 0;
    // not in recall mode
    h->isRecall = 0;
}

void mlHistoryPush(mlHistory *h, int const *entry, int entry_len)
{
    // skip duplicates
    if (h->his.len > 0) {
        mlHistoryEntry last = mlDaLast(&h->his);
        if (entry_len == last.len) {
            if (memcmp(entry, &h->buf.els[last.offset], entry_len*sizeof(int)) == 0) {
                return;
            }
        }
    }
    int buflen = h->buf.len;
    if (entry_len > 0) {
        mlDaAppendN(&h->buf, entry, entry_len);
    }
    mlHistoryEntry he = {
        .offset = buflen,
        .len = entry_len,
    };
    mlDaAppend(&h->his, he);
}

void mlHistoryPop(mlHistory *h)
{
    if (h->his.len == 0) return;
    mlHistoryEntry he = mlDaLast(&h->his);
    h->buf.len = he.offset;
    h->his.len -= 1;
}

static int mlWcwidth(int ucs);

typedef struct mlOutputBuilder {
#ifdef _WIN32
    // use WCHAR for Windows to avoid conversion later
    WCHAR *els;
#else
    char *els;
#endif
    int len, cap;
} mlOutputBuilder;

static void mlObSimpStr(mlOutputBuilder *ob, char const *s)
{
#ifdef _WIN32
    while (*s) {
        mlDaAppend(ob, (WCHAR)(*s));
        s++;
    }
#else
    mlDaAppendN(ob, s, (int)strlen(s));
#endif
}

#define mlAppendUtf8(da, cp) \
    if ((cp) < 0x80) { \
        mlDaAppend((da), (char)(cp)); \
    } else if ((cp) < 0x800) { \
        mlDaAppend((da), (char)(0xC0 | ((cp) >> 6))); \
        mlDaAppend((da), (char)(0x80 | ((cp) & 0x3F))); \
    } else if ((cp) < 0x10000) { \
        mlDaAppend((da), (char)(0xE0 | ((cp) >> 12))); \
        mlDaAppend((da), (char)(0x80 | (((cp) >> 6) & 0x3F))); \
        mlDaAppend((da), (char)(0x80 | ((cp) & 0x3F))); \
    } else { \
        mlDaAppend((da), (char)(0xF0 | ((cp) >> 18))); \
        mlDaAppend((da), (char)(0x80 | (((cp) >> 12) & 0x3F))); \
        mlDaAppend((da), (char)(0x80 | (((cp) >> 6) & 0x3F))); \
        mlDaAppend((da), (char)(0x80 | ((cp) & 0x3F))); \
    }

static void mlObCodePoint(mlOutputBuilder *ob, int cp)
{
#ifdef _WIN32
    // surrogate pair
    if (cp >= 0x10000) {
        mlDaAppend(ob, (WCHAR)(0xD800+((cp-0x10000) >> 10)));
        mlDaAppend(ob, (WCHAR)(0xDC00+((cp-0x10000)&0x3FF)));
    } else {
        mlDaAppend(ob, (WCHAR)(cp));
    }
#else
    mlAppendUtf8(ob, cp);
#endif
}

static void mlObWrite(mlOutputBuilder *ob)
{
#ifdef _WIN32
    DWORD written;
    assert(WriteConsoleW(st->out, ob->els, ob->len, &written, NULL));
#else
    assert(write(st->ofd, ob->els, ob->len)!=-1);
#endif
}

static char *mlTempSprintf(const char *fmt, ...)
{
    va_list ap;
    static char buf[4096];
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

typedef struct mlEditBuf {
    int *els;
    int len, cap;
    int pos;
    int fixed; // number of fixed elements at start (prompt)
} mlEditBuf;

static void mlEditBufFromCstr(mlEditBuf *eb, char const *cstr)
{
    unsigned char const *s = (unsigned char const *)cstr;
    ssize_t slen = strlen(cstr);
    while (*s) {
        int cp;
        if ((*s & 0x80) == 0) {
            cp = *s;
            s += 1;
            slen -= 1;
        } else if ((*s & 0xE0) == 0xC0 && slen >= 2) {
            cp = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
            slen -= 2;
        } else if ((*s & 0xF0) == 0xE0 && slen >= 3) {
            cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
            slen -= 3;
        } else if ((*s & 0xF8) == 0xF0 && slen >= 4) {
            cp = ((*s & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
            s += 4;
            slen -= 4;
        } else {
            assert(0 && "invalid utf8");
        }
        mlDaAppend(eb, cp);
    }
}

static char *mlStrFromEditBuf(mlEditBuf *eb)
{
    struct StringBuilder {
        char *els;
        int len, cap;
    } sb = {0};
    for (int i = eb->fixed; i < eb->len; i++) {
        mlAppendUtf8(&sb, eb->els[i]);
    }
    mlDaAppend(&sb, '\0');
    return sb.els;
}

static void mlRefreshLine(mlEditBuf *eb)
{
    int columns = mlGetColumns();
    int totalWidth = 0;
    int curPos = 0;
    mlOutputBuilder ob = {0};
    mlObSimpStr(&ob, "\r"); // move to column 0
    mlObSimpStr(&ob, ML_CSI "0K"); // clear line from cursor right
    // put the code points
    // move the cursor
    for (int i = 0; i < eb->len; i++) {
        int const cp = eb->els[i];
        int const w = mlWcwidth(cp);
        if (w < 0) continue;
        totalWidth += w;
        if (totalWidth > columns) break;
        if (i < eb->pos) curPos += w;
        mlObCodePoint(&ob, cp);
    }
    // move cursor to col curPos+1
    mlObSimpStr(&ob, mlTempSprintf(ML_CSI "%dG", curPos+1)); 
    mlObWrite(&ob);
    mlDaFree(ob);
}

void mlEditSetPrompt(mlEditBuf *eb, char const *prompt)
{
    eb->len = 0;
    mlEditBufFromCstr(eb, prompt);
    eb->fixed = eb->len;
    eb->pos = eb->len;
    mlRefreshLine(eb);
}

static void mlEditInsert(mlEditBuf *eb, int c)
{
    int dif = eb->len - eb->pos;
    if (dif < 0) return;
    mlDaReserve(eb, eb->len+1, ML_DA_DEFAULT_CAP);
    memmove(&eb->els[eb->pos+1], &eb->els[eb->pos], dif*sizeof(int));
    eb->els[eb->pos] = c;
    eb->len += 1;
    eb->pos += 1;
    mlRefreshLine(eb);
}

static void mlEditBackspace(mlEditBuf *eb)
{
    if (eb->pos == eb->fixed) return;
    if (eb->len == eb->fixed) return;
    int dif = eb->len - eb->pos;
    eb->pos -= 1;
    memmove(&eb->els[eb->pos], &eb->els[eb->pos+1], dif*sizeof(int));
    eb->len -= 1;
    mlRefreshLine(eb);
}

static void mlEditDelete(mlEditBuf *eb)
{
    if (eb->len == eb->fixed) return;
    if (eb->pos >= eb->len) return;
    int dif = eb->len - eb->pos - 1;
    memmove(&eb->els[eb->pos], &eb->els[eb->pos+1], dif*sizeof(int));
    eb->len -= 1;
    mlRefreshLine(eb);
}

static void mlEditDeletePrevWord(mlEditBuf *eb)
{
    if (eb->pos == eb->fixed) return;
    int orig_pos = eb->pos;
    // move pos to start of previous word
    while (eb->pos > eb->fixed && eb->els[eb->pos-1] == ' ') {
        eb->pos -= 1;
    }
    while (eb->pos > eb->fixed && eb->els[eb->pos-1] != ' ') {
        eb->pos -= 1;
    }
    int dif = orig_pos - eb->pos;
    if (dif <= 0) return;
    memmove(&eb->els[eb->pos], &eb->els[orig_pos], (eb->len - orig_pos)*sizeof(int));
    eb->len -= dif;
    mlRefreshLine(eb);
}

static void mlEditDeleteToEnd(mlEditBuf *eb)
{
    if (eb->len == eb->fixed) return;
    if (eb->pos >= eb->len) return;
    eb->len = eb->pos;
    mlRefreshLine(eb);
}

static void mlEditDeleteLine(mlEditBuf *eb)
{
    if (eb->len == eb->fixed) return;
    eb->len = eb->fixed;
    eb->pos = eb->fixed;
    mlRefreshLine(eb);
}

static void mlEditSwapChars(mlEditBuf *eb)
{
    if (eb->pos <= eb->fixed) return;
    if (eb->pos >= eb->len) return;
    int tmp = eb->els[eb->pos-1];
    eb->els[eb->pos-1] = eb->els[eb->pos];
    eb->els[eb->pos] = tmp;
    eb->pos += 1;
    mlRefreshLine(eb);
}

static void mlEditMoveLeft(mlEditBuf *eb)
{
    if (eb->pos > eb->fixed) {
        eb->pos -= 1;
        mlRefreshLine(eb);
    }
}

static void mlEditMoveRight(mlEditBuf *eb)
{
    if (eb->pos < eb->len) {
        eb->pos += 1;
        mlRefreshLine(eb);
    }
}

static void mlEditMoveHome(mlEditBuf *eb)
{
    eb->pos = eb->fixed;
    mlRefreshLine(eb);
}

static void mlEditMoveEnd(mlEditBuf *eb)
{
    eb->pos = eb->len;
    mlRefreshLine(eb);
}

static void mlEditHistoryCommit(mlEditBuf *eb, mlHistory *h)
{
    mlHistoryPop(h);
    if (h->isRecall && h->pos != 0) return;
    int entry_len = eb->len - eb->fixed;
    if (entry_len == 0) return;
    mlHistoryPush(h, &eb->els[eb->fixed], entry_len);
    h->pos = 0;
}

static void mlEditHistoryPut(mlEditBuf *eb, mlHistory *h)
{
    if (h->isRecall) return;
    int entry_len = eb->len - eb->fixed;
    if (entry_len == 0) return;
    mlHistoryPop(h);
    mlHistoryPush(h, &eb->els[eb->fixed], entry_len);
    h->pos = 0;
}

// dir: +1 for previous, -1 for next
static void mlEditHistoryMove(mlEditBuf *eb, mlHistory *h, int dir)
{
    if (h->his.len <= 1) return;
    mlEditHistoryPut(eb, h);

    h->pos += dir;
    if (h->pos < 0) {
        h->pos = 0;
        return;
    }
    if (h->pos > h->his.len-1) {
        h->pos = h->his.len-1;
        return;
    }
    eb->len = eb->fixed;
    mlHistoryEntry he = mlHistoryAtPos(h);
    if (he.len > 0) {
        mlDaAppendN(eb, &h->buf.els[he.offset], he.len);
    }
    eb->pos = eb->len;
    mlRefreshLine(eb);
}

static void mlEditClearScreen(void)
{
    mlOutputBuilder ob = {0};
    mlObSimpStr(&ob, ML_CSI "H" ML_CSI "2J");
    mlObWrite(&ob);
    mlDaFree(ob);
}

char *mlEditInAction = "mlEditInAction YOU SHOULD NOT SEE THIS";

char *mlEditFeed(mlEditBuf *eb)
{
    int key = mlReadKey();
    int isRecall = 0;
    switch (key) {
    default:
        mlEditInsert(eb, key);
        break;
    case ML_KEY_ENTER:
        mlEditHistoryCommit(eb, st->history);
        return mlStrFromEditBuf(eb);
    case ML_KEY_CTRL_C:
        mlHistoryPop(st->history);
        return NULL;
    case ML_KEY_BACKSPACE:
    case ML_KEY_CTRL_H:
        mlEditBackspace(eb);
        break;
    case ML_KEY_CTRL_D:
        if (eb->len > eb->fixed) {
            mlEditDelete(eb);
        } else {
            mlEditHistoryCommit(eb, st->history);
            return NULL;
        }
        break;
    case ML_KEY_LEFT:
    case ML_KEY_CTRL_B:
        mlEditMoveLeft(eb);
        break;
    case ML_KEY_RIGHT:
    case ML_KEY_CTRL_F:
        mlEditMoveRight(eb);
        break;
    case ML_KEY_HOME:
    case ML_KEY_CTRL_A:
        mlEditMoveHome(eb);
        break;
    case ML_KEY_END:
    case ML_KEY_CTRL_E:
        mlEditMoveEnd(eb);
        break;
    case ML_KEY_CTRL_W:
        mlEditDeletePrevWord(eb);
        break;
    case ML_KEY_CTRL_L:
        mlEditClearScreen();
        mlRefreshLine(eb);
        break;
    case ML_KEY_DELETE:
        mlEditDelete(eb);
        break;
    case ML_KEY_CTRL_U:
        mlEditDeleteLine(eb);
        break;
    case ML_KEY_CTRL_K:
        mlEditDeleteToEnd(eb);
        break;
    case ML_KEY_CTRL_T:
        mlEditSwapChars(eb);
        break;
    case ML_KEY_UP:
        isRecall = 1;
        mlEditHistoryMove(eb, st->history, +1);
        break;
    case ML_KEY_DOWN:
        isRecall = 1;
        mlEditHistoryMove(eb, st->history, -1);
        break;
    }
    st->history->isRecall = isRecall;
    return mlEditInAction;
}

char *mlReadLine(char const *prompt)
{
    char *res;
    mlEditBuf eb = {0};
    mlBegin();
    mlEditSetPrompt(&eb, prompt);
    mlHistoryReset(st->history);
    mlHistoryPush(st->history, NULL, 0); // empty entry
    for (;;) {
        res = mlEditFeed(&eb);
        if (res != mlEditInAction) {
            break;
        }
    }
    mlEnd();
    puts("");
    mlDaFree(eb);
    return res;
}

/*
 * This is an implementation of wcwidth() and wcswidth() (defined in
 * IEEE Std 1002.1-2001) for Unicode.
 *
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcwidth.html
 * http://www.opengroup.org/onlinepubs/007904975/functions/wcswidth.html
 *
 * In fixed-width output devices, Latin characters all occupy a single
 * "cell" position of equal width, whereas ideographic CJK characters
 * occupy two such cells. Interoperability between terminal-line
 * applications and (teletype-style) character terminals using the
 * UTF-8 encoding requires agreement on which character should advance
 * the cursor by how many cell positions. No established formal
 * standards exist at present on which Unicode character shall occupy
 * how many cell positions on character terminals. These routines are
 * a first attempt of defining such behavior based on simple rules
 * applied to data provided by the Unicode Consortium.
 *
 * For some graphical characters, the Unicode standard explicitly
 * defines a character-cell width via the definition of the East Asian
 * FullWidth (F), Wide (W), Half-width (H), and Narrow (Na) classes.
 * In all these cases, there is no ambiguity about which width a
 * terminal shall use. For characters in the East Asian Ambiguous (A)
 * class, the width choice depends purely on a preference of backward
 * compatibility with either historic CJK or Western practice.
 * Choosing single-width for these characters is easy to justify as
 * the appropriate long-term solution, as the CJK practice of
 * displaying these characters as double-width comes from historic
 * implementation simplicity (8-bit encoded characters were displayed
 * single-width and 16-bit ones double-width, even for Greek,
 * Cyrillic, etc.) and not any typographic considerations.
 *
 * Much less clear is the choice of width for the Not East Asian
 * (Neutral) class. Existing practice does not dictate a width for any
 * of these characters. It would nevertheless make sense
 * typographically to allocate two character cells to characters such
 * as for instance EM SPACE or VOLUME INTEGRAL, which cannot be
 * represented adequately with a single-width glyph. The following
 * routines at present merely assign a single-cell width to all
 * neutral characters, in the interest of simplicity. This is not
 * entirely satisfactory and should be reconsidered before
 * establishing a formal standard in this area. At the moment, the
 * decision which Not East Asian (Neutral) characters should be
 * represented by double-width glyphs cannot yet be answered by
 * applying a simple rule from the Unicode database content. Setting
 * up a proper standard for the behavior of UTF-8 character terminals
 * will require a careful analysis not only of each Unicode character,
 * but also of each presentation form, something the author of these
 * routines has avoided to do so far.
 *
 * http://www.unicode.org/unicode/reports/tr11/
 *
 * Markus Kuhn -- 2007-05-26 (Unicode 5.0)
 *
 * Permission to use, copy, modify, and distribute this software
 * for any purpose and without fee is hereby granted. The author
 * disclaims all warranties with regard to this software.
 *
 * Latest version: http://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c
 */

struct mlInterval
{
    int first;
    int last;
};

/* auxiliary function for binary search in interval table */
static int mlBisearch(int ucs, const struct mlInterval *table, int max)
{
    int min = 0;
    int mid;

    if (ucs < table[0].first || ucs > table[max].last)
        return 0;
    while (max >= min) {
        mid = (min + max) / 2;
        if (ucs > table[mid].last)
            min = mid + 1;
        else if (ucs < table[mid].first)
            max = mid - 1;
        else
            return 1;
    }

    return 0;
}

/* The following two functions define the column width of an ISO 10646
 * character as follows:
 *
 *    - The null character (U+0000) has a column width of 0.
 *
 *    - Other C0/C1 control characters and DEL will lead to a return
 *      value of -1.
 *
 *    - Non-spacing and enclosing combining characters (general
 *      category code Mn or Me in the Unicode database) have a
 *      column width of 0.
 *
 *    - SOFT HYPHEN (U+00AD) has a column width of 1.
 *
 *    - Other format characters (general category code Cf in the Unicode
 *      database) and ZERO WIDTH SPACE (U+200B) have a column width of 0.
 *
 *    - Hangul Jamo medial vowels and final consonants (U+1160-U+11FF)
 *      have a column width of 0.
 *
 *    - Spacing characters in the East Asian Wide (W) or East Asian
 *      Full-width (F) category as defined in Unicode Technical
 *      Report #11 have a column width of 2.
 *
 *    - All remaining characters (including all printable
 *      ISO 8859-1 and WGL4 characters, Unicode control characters,
 *      etc.) have a column width of 1.
 *
 * This implementation assumes that int characters are encoded
 * in ISO 10646.
 */

int mlWcwidth(int ucs)
{
    /* sorted list of non-overlapping intervals of non-spacing characters */
    /* generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c" */
    static const struct mlInterval combining[] = {
        { 0x0300, 0x036F }, { 0x0483, 0x0486 }, { 0x0488, 0x0489 },
        { 0x0591, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 },
        { 0x05C4, 0x05C5 }, { 0x05C7, 0x05C7 }, { 0x0600, 0x0603 },
        { 0x0610, 0x0615 }, { 0x064B, 0x065E }, { 0x0670, 0x0670 },
        { 0x06D6, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED },
        { 0x070F, 0x070F }, { 0x0711, 0x0711 }, { 0x0730, 0x074A },
        { 0x07A6, 0x07B0 }, { 0x07EB, 0x07F3 }, { 0x0901, 0x0902 },
        { 0x093C, 0x093C }, { 0x0941, 0x0948 }, { 0x094D, 0x094D },
        { 0x0951, 0x0954 }, { 0x0962, 0x0963 }, { 0x0981, 0x0981 },
        { 0x09BC, 0x09BC }, { 0x09C1, 0x09C4 }, { 0x09CD, 0x09CD },
        { 0x09E2, 0x09E3 }, { 0x0A01, 0x0A02 }, { 0x0A3C, 0x0A3C },
        { 0x0A41, 0x0A42 }, { 0x0A47, 0x0A48 }, { 0x0A4B, 0x0A4D },
        { 0x0A70, 0x0A71 }, { 0x0A81, 0x0A82 }, { 0x0ABC, 0x0ABC },
        { 0x0AC1, 0x0AC5 }, { 0x0AC7, 0x0AC8 }, { 0x0ACD, 0x0ACD },
        { 0x0AE2, 0x0AE3 }, { 0x0B01, 0x0B01 }, { 0x0B3C, 0x0B3C },
        { 0x0B3F, 0x0B3F }, { 0x0B41, 0x0B43 }, { 0x0B4D, 0x0B4D },
        { 0x0B56, 0x0B56 }, { 0x0B82, 0x0B82 }, { 0x0BC0, 0x0BC0 },
        { 0x0BCD, 0x0BCD }, { 0x0C3E, 0x0C40 }, { 0x0C46, 0x0C48 },
        { 0x0C4A, 0x0C4D }, { 0x0C55, 0x0C56 }, { 0x0CBC, 0x0CBC },
        { 0x0CBF, 0x0CBF }, { 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD },
        { 0x0CE2, 0x0CE3 }, { 0x0D41, 0x0D43 }, { 0x0D4D, 0x0D4D },
        { 0x0DCA, 0x0DCA }, { 0x0DD2, 0x0DD4 }, { 0x0DD6, 0x0DD6 },
        { 0x0E31, 0x0E31 }, { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E },
        { 0x0EB1, 0x0EB1 }, { 0x0EB4, 0x0EB9 }, { 0x0EBB, 0x0EBC },
        { 0x0EC8, 0x0ECD }, { 0x0F18, 0x0F19 }, { 0x0F35, 0x0F35 },
        { 0x0F37, 0x0F37 }, { 0x0F39, 0x0F39 }, { 0x0F71, 0x0F7E },
        { 0x0F80, 0x0F84 }, { 0x0F86, 0x0F87 }, { 0x0F90, 0x0F97 },
        { 0x0F99, 0x0FBC }, { 0x0FC6, 0x0FC6 }, { 0x102D, 0x1030 },
        { 0x1032, 0x1032 }, { 0x1036, 0x1037 }, { 0x1039, 0x1039 },
        { 0x1058, 0x1059 }, { 0x1160, 0x11FF }, { 0x135F, 0x135F },
        { 0x1712, 0x1714 }, { 0x1732, 0x1734 }, { 0x1752, 0x1753 },
        { 0x1772, 0x1773 }, { 0x17B4, 0x17B5 }, { 0x17B7, 0x17BD },
        { 0x17C6, 0x17C6 }, { 0x17C9, 0x17D3 }, { 0x17DD, 0x17DD },
        { 0x180B, 0x180D }, { 0x18A9, 0x18A9 }, { 0x1920, 0x1922 },
        { 0x1927, 0x1928 }, { 0x1932, 0x1932 }, { 0x1939, 0x193B },
        { 0x1A17, 0x1A18 }, { 0x1B00, 0x1B03 }, { 0x1B34, 0x1B34 },
        { 0x1B36, 0x1B3A }, { 0x1B3C, 0x1B3C }, { 0x1B42, 0x1B42 },
        { 0x1B6B, 0x1B73 }, { 0x1DC0, 0x1DCA }, { 0x1DFE, 0x1DFF },
        { 0x200B, 0x200F }, { 0x202A, 0x202E }, { 0x2060, 0x2063 },
        { 0x206A, 0x206F }, { 0x20D0, 0x20EF }, { 0x302A, 0x302F },
        { 0x3099, 0x309A }, { 0xA806, 0xA806 }, { 0xA80B, 0xA80B },
        { 0xA825, 0xA826 }, { 0xFB1E, 0xFB1E }, { 0xFE00, 0xFE0F },
        { 0xFE20, 0xFE23 }, { 0xFEFF, 0xFEFF }, { 0xFFF9, 0xFFFB },
        { 0x10A01, 0x10A03 }, { 0x10A05, 0x10A06 }, { 0x10A0C, 0x10A0F },
        { 0x10A38, 0x10A3A }, { 0x10A3F, 0x10A3F }, { 0x1D167, 0x1D169 },
        { 0x1D173, 0x1D182 }, { 0x1D185, 0x1D18B }, { 0x1D1AA, 0x1D1AD },
        { 0x1D242, 0x1D244 }, { 0xE0001, 0xE0001 }, { 0xE0020, 0xE007F },
        { 0xE0100, 0xE01EF }
    };
    /* test for 8-bit control characters */
    if (ucs == 0)
        return 0;
    if (ucs < 32 || (ucs >= 0x7f && ucs < 0xa0))
        return -1;

    /* binary search in table of non-spacing characters */
    if (mlBisearch(ucs, combining,
                   sizeof(combining) / sizeof(struct mlInterval) - 1))
        return 0;

    /* if we arrive here, ucs is not a combining or C0/C1 control character */

    return 1 +
           (ucs >= 0x1100 &&
            (ucs <= 0x115f || /* Hangul Jamo init. consonants */
             ucs == 0x2329 || ucs == 0x232a ||
             (ucs >= 0x2e80 && ucs <= 0xa4cf &&
              ucs != 0x303f) ||                  /* CJK ... Yi */
             (ucs >= 0xac00 && ucs <= 0xd7a3) || /* Hangul Syllables */
             (ucs >= 0xf900 && ucs <= 0xfaff) || /* CJK Compatibility Ideographs */
             (ucs >= 0xfe10 && ucs <= 0xfe19) || /* Vertical forms */
             (ucs >= 0xfe30 && ucs <= 0xfe6f) || /* CJK Compatibility Forms */
             (ucs >= 0xff00 && ucs <= 0xff60) || /* Fullwidth Forms */
             (ucs >= 0xffe0 && ucs <= 0xffe6) ||
             (ucs >= 0x20000 && ucs <= 0x2fffd) ||
             (ucs >= 0x30000 && ucs <= 0x3fffd)));
}


#endif // MINILINE_IMPLEMENTATION

