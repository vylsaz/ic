#ifndef MINILINE_H_
#define MINILINE_H_

#ifndef MINILINE_API
#define MINILINE_API
#endif

MINILINE_API char *mlReadLine(const char *prompt);

// flags:
// #define MINILINE_IGNORE_ZWJ

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
#else
    // https://github.com/antirez/linenoise/blob/880b94130ffa5f8236392392b447ff2234b11983/linenoise.c#L220
    struct termios term;
    tcgetattr(st->ifd, &term);
    /* input modes: no break, no CR to NL, no parity check, no strip char,
     * no start/stop output control. */
    term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* output modes - disable post processing */
    term.c_oflag &= ~(OPOST);
    /* control modes - set 8 bit chars */
    term.c_cflag |= (CS8);
    /* local modes - echoing off, canonical off, no extended functions,
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
    return cp;
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

static int mlMkWcwidth(int ucs);

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
    int oldY, oldRows;
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
    for (int i = eb->fixed; i < eb->len; ++i) {
        mlAppendUtf8(&sb, eb->els[i]);
    }
    mlDaAppend(&sb, '\0');
    return sb.els;
}

static void mlRefreshLine(mlEditBuf *eb)
{
    mlOutputBuilder ob = {0};
    int cols = mlGetColumns();
    int curX = 0, curY = 0;
    int rows = 0;
    int oldY = eb->oldY;
    int oldRows = eb->oldRows; 
    // if one line, rows=0, two lines, rows=1, etc.

    int goDown = oldRows - oldY;
    // move cursor to bottom
    if (goDown > 0) {
        mlObSimpStr(&ob, mlTempSprintf(ML_CSI "%dB", goDown));
    }
    for (int i = 0; i<oldRows; ++i) {
        mlObSimpStr(&ob, "\r" ML_CSI "0K"); // clear entire line
        mlObSimpStr(&ob, ML_CSI "A"); // move up
    }

    mlObSimpStr(&ob, "\r" ML_CSI "0K"); // clear entire line

    int accum = 0;
    // put the code points
    for (int i = 0; i < eb->len; ++i) {
        int const cp = eb->els[i];
        int const w = mlMkWcwidth(cp);
        if (w < 0) continue;
        if (eb->pos == i) {
            curX = accum;
            curY = rows;
        }
        accum += w;
        if (accum > cols) { 
            // go to next line (wide)
            accum = w;
            rows += 1;
        }
    #ifdef MINILINE_IGNORE_ZWJ
        if (cp != 0x200D)
    #endif
        // assume the terminal has the same width and new line 
        // behavior as we do...
        // (when it fails, it fails horribly)
        mlObCodePoint(&ob, cp); 
        if (accum == cols) {
            // go to next line (exact)
            accum = 0;
            rows += 1;
        }
    }
    int posAtEnd = (eb->pos == eb->len);
    if (accum == 0 && rows > 0) {
        if (posAtEnd) {
            // at the start of a new line
            mlObSimpStr(&ob, "\r\n");
        } else {
            rows -= 1;
        }
    }
    if (posAtEnd) {
        curX = accum;
        curY = rows;
    }

    // move cursor up to the correct row
    int goUp = rows - curY;
    if (goUp > 0) {
        mlObSimpStr(&ob, mlTempSprintf(ML_CSI "%dA", goUp));
    }
    // move cursor to col curX+1
    mlObSimpStr(&ob, mlTempSprintf(ML_CSI "%dG", curX+1)); 
    mlObWrite(&ob);
    mlDaFree(ob);

    eb->oldY = curY;
    eb->oldRows = rows;
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
        mlEditMoveEnd(eb);
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
            mlHistoryPop(st->history);
            return NULL;
        }
        break;
    case ML_KEY_LEFT:
    case ML_KEY_CTRL_B:
        isRecall = st->history->isRecall;
        mlEditMoveLeft(eb);
        break;
    case ML_KEY_RIGHT:
    case ML_KEY_CTRL_F:
        isRecall = st->history->isRecall;
        mlEditMoveRight(eb);
        break;
    case ML_KEY_HOME:
    case ML_KEY_CTRL_A:
        isRecall = st->history->isRecall;
        mlEditMoveHome(eb);
        break;
    case ML_KEY_END:
    case ML_KEY_CTRL_E:
        isRecall = st->history->isRecall;
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

char *mlReadLineTTY(char const *prompt)
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

char *mlReadLineNoTTY(char const *prompt)
{
    char inp[256] = {0};
    struct Out {
        char *els;
        size_t len, cap;
    } out = {0};
    size_t const inpLen = sizeof(inp)-1;
    printf("%s", prompt);
    do {
        if (fgets(inp, inpLen, stdin)==NULL) return 0;
        mlDaAppendN(&out, inp, strlen(inp));
    } while (out.els[out.len-1] != '\n');
    out.els[out.len-1] = '\0'; // remove newline
    printf("%s\n", out.els);
    return out.els;
}

int mlIsATTY(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(fileno(stdin)) != 0;
#endif
}

char *mlReadLine(char const *prompt)
{
    if (mlIsATTY()) {
        return mlReadLineTTY(prompt);
    } else {
        return mlReadLineNoTTY(prompt);
    }
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
int mlMkIsWideChar(int ucs)
{
    static const struct mlInterval wide[] = {
        {0x1100, 0x115f}, {0x231a, 0x231b}, {0x2329, 0x232a},
        {0x23e9, 0x23ec}, {0x23f0, 0x23f0}, {0x23f3, 0x23f3},
        {0x25fd, 0x25fe}, {0x2614, 0x2615}, {0x2648, 0x2653},
        {0x267f, 0x267f}, {0x2693, 0x2693}, {0x26a1, 0x26a1},
        {0x26aa, 0x26ab}, {0x26bd, 0x26be}, {0x26c4, 0x26c5},
        {0x26ce, 0x26ce}, {0x26d4, 0x26d4}, {0x26ea, 0x26ea},
        {0x26f2, 0x26f3}, {0x26f5, 0x26f5}, {0x26fa, 0x26fa},
        {0x26fd, 0x26fd}, {0x2705, 0x2705}, {0x270a, 0x270b},
        {0x2728, 0x2728}, {0x274c, 0x274c}, {0x274e, 0x274e},
        {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
        {0x27b0, 0x27b0}, {0x27bf, 0x27bf}, {0x2b1b, 0x2b1c},
        {0x2b50, 0x2b50}, {0x2b55, 0x2b55}, {0x2e80, 0x2fdf},
        {0x2ff0, 0x303e}, {0x3040, 0x3247}, {0x3250, 0x4dbf},
        {0x4e00, 0xa4cf}, {0xa960, 0xa97f}, {0xac00, 0xd7a3},
        {0xf900, 0xfaff}, {0xfe10, 0xfe19}, {0xfe30, 0xfe6f},
        {0xff01, 0xff60}, {0xffe0, 0xffe6}, {0x16fe0, 0x16fe1},
        {0x17000, 0x18aff}, {0x1b000, 0x1b12f}, {0x1b170, 0x1b2ff},
        {0x1f004, 0x1f004}, {0x1f0cf, 0x1f0cf}, {0x1f18e, 0x1f18e},
        {0x1f191, 0x1f19a}, {0x1f200, 0x1f202}, {0x1f210, 0x1f23b},
        {0x1f240, 0x1f248}, {0x1f250, 0x1f251}, {0x1f260, 0x1f265},
        {0x1f300, 0x1f320}, {0x1f32d, 0x1f335}, {0x1f337, 0x1f37c},
        {0x1f37e, 0x1f393}, {0x1f3a0, 0x1f3ca}, {0x1f3cf, 0x1f3d3},
        {0x1f3e0, 0x1f3f0}, {0x1f3f4, 0x1f3f4}, {0x1f3f8, 0x1f43e},
        {0x1f440, 0x1f440}, {0x1f442, 0x1f4fc}, {0x1f4ff, 0x1f53d},
        {0x1f54b, 0x1f54e}, {0x1f550, 0x1f567}, {0x1f57a, 0x1f57a},
        {0x1f595, 0x1f596}, {0x1f5a4, 0x1f5a4}, {0x1f5fb, 0x1f64f},
        {0x1f680, 0x1f6c5}, {0x1f6cc, 0x1f6cc}, {0x1f6d0, 0x1f6d2},
        {0x1f6eb, 0x1f6ec}, {0x1f6f4, 0x1f6f8}, {0x1f910, 0x1f93e},
        {0x1f940, 0x1f94c}, {0x1f950, 0x1f96b}, {0x1f980, 0x1f997},
        {0x1f9c0, 0x1f9c0}, {0x1f9d0, 0x1f9e6}, {0x20000, 0x2fffd},
        {0x30000, 0x3fffd},
    };

    if (mlBisearch(ucs, wide, sizeof(wide) / sizeof(struct mlInterval) - 1)) {
        return 1;
    }

    return 0;
}

int mlMkWcwidth(int ucs)
{
    /* sorted list of non-overlapping intervals of non-spacing characters */
    /* generated by "uniset +cat=Me +cat=Mn +cat=Cf -00AD +1160-11FF +200B c" */
    static const struct mlInterval combining[] = {
        {0x00ad, 0x00ad}, {0x0300, 0x036f}, {0x0483, 0x0489},
        {0x0591, 0x05bd}, {0x05bf, 0x05bf}, {0x05c1, 0x05c2},
        {0x05c4, 0x05c5}, {0x05c7, 0x05c7}, {0x0610, 0x061a},
        {0x061c, 0x061c}, {0x064b, 0x065f}, {0x0670, 0x0670},
        {0x06d6, 0x06dc}, {0x06df, 0x06e4}, {0x06e7, 0x06e8},
        {0x06ea, 0x06ed}, {0x0711, 0x0711}, {0x0730, 0x074a},
        {0x07a6, 0x07b0}, {0x07eb, 0x07f3}, {0x0816, 0x0819},
        {0x081b, 0x0823}, {0x0825, 0x0827}, {0x0829, 0x082d},
        {0x0859, 0x085b}, {0x08d4, 0x08e1}, {0x08e3, 0x0902},
        {0x093a, 0x093a}, {0x093c, 0x093c}, {0x0941, 0x0948},
        {0x094d, 0x094d}, {0x0951, 0x0957}, {0x0962, 0x0963},
        {0x0981, 0x0981}, {0x09bc, 0x09bc}, {0x09c1, 0x09c4},
        {0x09cd, 0x09cd}, {0x09e2, 0x09e3}, {0x0a01, 0x0a02},
        {0x0a3c, 0x0a3c}, {0x0a41, 0x0a42}, {0x0a47, 0x0a48},
        {0x0a4b, 0x0a4d}, {0x0a51, 0x0a51}, {0x0a70, 0x0a71},
        {0x0a75, 0x0a75}, {0x0a81, 0x0a82}, {0x0abc, 0x0abc},
        {0x0ac1, 0x0ac5}, {0x0ac7, 0x0ac8}, {0x0acd, 0x0acd},
        {0x0ae2, 0x0ae3}, {0x0afa, 0x0aff}, {0x0b01, 0x0b01},
        {0x0b3c, 0x0b3c}, {0x0b3f, 0x0b3f}, {0x0b41, 0x0b44},
        {0x0b4d, 0x0b4d}, {0x0b56, 0x0b56}, {0x0b62, 0x0b63},
        {0x0b82, 0x0b82}, {0x0bc0, 0x0bc0}, {0x0bcd, 0x0bcd},
        {0x0c00, 0x0c00}, {0x0c3e, 0x0c40}, {0x0c46, 0x0c48},
        {0x0c4a, 0x0c4d}, {0x0c55, 0x0c56}, {0x0c62, 0x0c63},
        {0x0c81, 0x0c81}, {0x0cbc, 0x0cbc}, {0x0cbf, 0x0cbf},
        {0x0cc6, 0x0cc6}, {0x0ccc, 0x0ccd}, {0x0ce2, 0x0ce3},
        {0x0d00, 0x0d01}, {0x0d3b, 0x0d3c}, {0x0d41, 0x0d44},
        {0x0d4d, 0x0d4d}, {0x0d62, 0x0d63}, {0x0dca, 0x0dca},
        {0x0dd2, 0x0dd4}, {0x0dd6, 0x0dd6}, {0x0e31, 0x0e31},
        {0x0e34, 0x0e3a}, {0x0e47, 0x0e4e}, {0x0eb1, 0x0eb1},
        {0x0eb4, 0x0eb9}, {0x0ebb, 0x0ebc}, {0x0ec8, 0x0ecd},
        {0x0f18, 0x0f19}, {0x0f35, 0x0f35}, {0x0f37, 0x0f37},
        {0x0f39, 0x0f39}, {0x0f71, 0x0f7e}, {0x0f80, 0x0f84},
        {0x0f86, 0x0f87}, {0x0f8d, 0x0f97}, {0x0f99, 0x0fbc},
        {0x0fc6, 0x0fc6}, {0x102d, 0x1030}, {0x1032, 0x1037},
        {0x1039, 0x103a}, {0x103d, 0x103e}, {0x1058, 0x1059},
        {0x105e, 0x1060}, {0x1071, 0x1074}, {0x1082, 0x1082},
        {0x1085, 0x1086}, {0x108d, 0x108d}, {0x109d, 0x109d},
        {0x1160, 0x11ff}, {0x135d, 0x135f}, {0x1712, 0x1714},
        {0x1732, 0x1734}, {0x1752, 0x1753}, {0x1772, 0x1773},
        {0x17b4, 0x17b5}, {0x17b7, 0x17bd}, {0x17c6, 0x17c6},
        {0x17c9, 0x17d3}, {0x17dd, 0x17dd}, {0x180b, 0x180e},
        {0x1885, 0x1886}, {0x18a9, 0x18a9}, {0x1920, 0x1922},
        {0x1927, 0x1928}, {0x1932, 0x1932}, {0x1939, 0x193b},
        {0x1a17, 0x1a18}, {0x1a1b, 0x1a1b}, {0x1a56, 0x1a56},
        {0x1a58, 0x1a5e}, {0x1a60, 0x1a60}, {0x1a62, 0x1a62},
        {0x1a65, 0x1a6c}, {0x1a73, 0x1a7c}, {0x1a7f, 0x1a7f},
        {0x1ab0, 0x1abe}, {0x1b00, 0x1b03}, {0x1b34, 0x1b34},
        {0x1b36, 0x1b3a}, {0x1b3c, 0x1b3c}, {0x1b42, 0x1b42},
        {0x1b6b, 0x1b73}, {0x1b80, 0x1b81}, {0x1ba2, 0x1ba5},
        {0x1ba8, 0x1ba9}, {0x1bab, 0x1bad}, {0x1be6, 0x1be6},
        {0x1be8, 0x1be9}, {0x1bed, 0x1bed}, {0x1bef, 0x1bf1},
        {0x1c2c, 0x1c33}, {0x1c36, 0x1c37}, {0x1cd0, 0x1cd2},
        {0x1cd4, 0x1ce0}, {0x1ce2, 0x1ce8}, {0x1ced, 0x1ced},
        {0x1cf4, 0x1cf4}, {0x1cf8, 0x1cf9}, {0x1dc0, 0x1df9},
        {0x1dfb, 0x1dff}, {0x200b, 0x200f}, {0x202a, 0x202e},
        {0x2060, 0x2064}, {0x2066, 0x206f}, {0x20d0, 0x20f0},
        {0x2cef, 0x2cf1}, {0x2d7f, 0x2d7f}, {0x2de0, 0x2dff},
        {0x302a, 0x302d}, {0x3099, 0x309a}, {0xa66f, 0xa672},
        {0xa674, 0xa67d}, {0xa69e, 0xa69f}, {0xa6f0, 0xa6f1},
        {0xa802, 0xa802}, {0xa806, 0xa806}, {0xa80b, 0xa80b},
        {0xa825, 0xa826}, {0xa8c4, 0xa8c5}, {0xa8e0, 0xa8f1},
        {0xa926, 0xa92d}, {0xa947, 0xa951}, {0xa980, 0xa982},
        {0xa9b3, 0xa9b3}, {0xa9b6, 0xa9b9}, {0xa9bc, 0xa9bc},
        {0xa9e5, 0xa9e5}, {0xaa29, 0xaa2e}, {0xaa31, 0xaa32},
        {0xaa35, 0xaa36}, {0xaa43, 0xaa43}, {0xaa4c, 0xaa4c},
        {0xaa7c, 0xaa7c}, {0xaab0, 0xaab0}, {0xaab2, 0xaab4},
        {0xaab7, 0xaab8}, {0xaabe, 0xaabf}, {0xaac1, 0xaac1},
        {0xaaec, 0xaaed}, {0xaaf6, 0xaaf6}, {0xabe5, 0xabe5},
        {0xabe8, 0xabe8}, {0xabed, 0xabed}, {0xfb1e, 0xfb1e},
        {0xfe00, 0xfe0f}, {0xfe20, 0xfe2f}, {0xfeff, 0xfeff},
        {0xfff9, 0xfffb}, {0x101fd, 0x101fd}, {0x102e0, 0x102e0},
        {0x10376, 0x1037a}, {0x10a01, 0x10a03}, {0x10a05, 0x10a06},
        {0x10a0c, 0x10a0f}, {0x10a38, 0x10a3a}, {0x10a3f, 0x10a3f},
        {0x10ae5, 0x10ae6}, {0x11001, 0x11001}, {0x11038, 0x11046},
        {0x1107f, 0x11081}, {0x110b3, 0x110b6}, {0x110b9, 0x110ba},
        {0x11100, 0x11102}, {0x11127, 0x1112b}, {0x1112d, 0x11134},
        {0x11173, 0x11173}, {0x11180, 0x11181}, {0x111b6, 0x111be},
        {0x111ca, 0x111cc}, {0x1122f, 0x11231}, {0x11234, 0x11234},
        {0x11236, 0x11237}, {0x1123e, 0x1123e}, {0x112df, 0x112df},
        {0x112e3, 0x112ea}, {0x11300, 0x11301}, {0x1133c, 0x1133c},
        {0x11340, 0x11340}, {0x11366, 0x1136c}, {0x11370, 0x11374},
        {0x11438, 0x1143f}, {0x11442, 0x11444}, {0x11446, 0x11446},
        {0x114b3, 0x114b8}, {0x114ba, 0x114ba}, {0x114bf, 0x114c0},
        {0x114c2, 0x114c3}, {0x115b2, 0x115b5}, {0x115bc, 0x115bd},
        {0x115bf, 0x115c0}, {0x115dc, 0x115dd}, {0x11633, 0x1163a},
        {0x1163d, 0x1163d}, {0x1163f, 0x11640}, {0x116ab, 0x116ab},
        {0x116ad, 0x116ad}, {0x116b0, 0x116b5}, {0x116b7, 0x116b7},
        {0x1171d, 0x1171f}, {0x11722, 0x11725}, {0x11727, 0x1172b},
        {0x11a01, 0x11a06}, {0x11a09, 0x11a0a}, {0x11a33, 0x11a38},
        {0x11a3b, 0x11a3e}, {0x11a47, 0x11a47}, {0x11a51, 0x11a56},
        {0x11a59, 0x11a5b}, {0x11a8a, 0x11a96}, {0x11a98, 0x11a99},
        {0x11c30, 0x11c36}, {0x11c38, 0x11c3d}, {0x11c3f, 0x11c3f},
        {0x11c92, 0x11ca7}, {0x11caa, 0x11cb0}, {0x11cb2, 0x11cb3},
        {0x11cb5, 0x11cb6}, {0x11d31, 0x11d36}, {0x11d3a, 0x11d3a},
        {0x11d3c, 0x11d3d}, {0x11d3f, 0x11d45}, {0x11d47, 0x11d47},
        {0x16af0, 0x16af4}, {0x16b30, 0x16b36}, {0x16f8f, 0x16f92},
        {0x1bc9d, 0x1bc9e}, {0x1bca0, 0x1bca3}, {0x1d167, 0x1d169},
        {0x1d173, 0x1d182}, {0x1d185, 0x1d18b}, {0x1d1aa, 0x1d1ad},
        {0x1d242, 0x1d244}, {0x1da00, 0x1da36}, {0x1da3b, 0x1da6c},
        {0x1da75, 0x1da75}, {0x1da84, 0x1da84}, {0x1da9b, 0x1da9f},
        {0x1daa1, 0x1daaf}, {0x1e000, 0x1e006}, {0x1e008, 0x1e018},
        {0x1e01b, 0x1e021}, {0x1e023, 0x1e024}, {0x1e026, 0x1e02a},
        {0x1e8d0, 0x1e8d6}, {0x1e944, 0x1e94a}, {0xe0001, 0xe0001},
        {0xe0020, 0xe007f}, {0xe0100, 0xe01ef},
    };

    /* test for 8-bit control characters */
    if (ucs == 0) {
        return 0;
    }
    if ((ucs < 32) || ((ucs >= 0x7f) && (ucs < 0xa0))) {
        return -1;
    }

    /* binary search in table of non-spacing characters */
    if (mlBisearch(ucs, combining, sizeof(combining) / sizeof(struct mlInterval) - 1)) {
        return 0;
    }

    /* if we arrive here, ucs is not a combining or C0/C1 control character */
    return (mlMkIsWideChar(ucs) ? 2 : 1);
}



#endif // MINILINE_IMPLEMENTATION

