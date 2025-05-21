#include "libtcc/libtcc.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <ctype.h>

typedef int64_t isz;

#define CHARRAY(S) (S), (sizeof(S)-1)

typedef struct StrBuilder {
    isz len, cap;
    char *cstr;
} StrBuilder;

void StrBuilderAppend(StrBuilder *sb, char const *str, isz len)
{
    if (sb->len+len+1 > sb->cap) {
        if (sb->cap==0) {
            sb->cap = 256;
        }
        while (sb->len+len+1 > sb->cap) sb->cap *= 2;
        sb->cstr = realloc(sb->cstr, sb->cap);
        assert(sb->cstr);
    }
    memcpy(sb->cstr+sb->len, str, len);
    sb->len += len;
    sb->cstr[sb->len] = '\0';
}

isz LastPathSep(char const *str, isz len)
{
    isz i;
    for (i = 0; i<len; ++i) {
        char c = str[len-i-1];
        if (c=='\\' || c=='/') return len-i-1;
    }
    return -1;
}

char SimpleLast(StrBuilder *sb) 
{
    if (sb->len<2) return '\0';
    return sb->cstr[sb->len-2];
}

char LastChar(StrBuilder *sb)
{
    isz i;
    for (i = 0; i<sb->len; ++i) {
        char c = sb->cstr[sb->len-i-1];

        if (c=='/' &&
            i+1<sb->len && sb->cstr[sb->len-i-2]=='*') {
            isz j;
            for (j = i+2; j<sb->len; ++j) {
                if (sb->cstr[sb->len-j-1]=='*' &&
                    j+1<sb->len && sb->cstr[sb->len-j-2]=='/') {
                    i = j+1;
                    break;
                }
            }
        } else if (c!=' ' && c!='\n' && c!='\t') {
            return c;
        }
    }
    return '\0';
}

enum CompleteResult {
    Incomplete = 0,
    Complete = 1,
    Invalid = 2,
};
enum CompleteResult IsComplete(StrBuilder *sb)
{
    enum Braces {
        NoBrace,
        Paren,
        Brack,
        Curly,
    };
    enum Braces braceFromChar[] = {
        ['('] = Paren,
        [')'] = Paren,
        ['['] = Brack,
        [']'] = Brack,
        ['{'] = Curly,
        ['}'] = Curly,
    };
    
    enum CompleteResult r = Incomplete;
    int sgStr = 0, dbStr = 0, lnCom = 0, blCom = 0, esc = 0;
    isz len = 0, cap = 8;
    enum Braces *stack = calloc(cap, sizeof(enum Braces));

    isz i;
    for (i = 0; i<sb->len-1; ++i) {
        char c = sb->cstr[i];
        if (esc==1) {
            esc = 0;
            continue;
        }
        if (sgStr) {
            if (esc==0 && c=='\\') {
                esc = 1;
            }
            if (c!='\'') continue;
        }
        if (dbStr) {
            if (esc==0 && c=='\\') {
                esc = 1;
            }
            if (c!='\"') continue;
        }
        if (lnCom) {
            if (c=='\n') lnCom = 0;
            continue;
        }
        if (blCom) {
            if (c=='*' && i+1<sb->len && sb->cstr[i+1]=='/') {
                blCom = 0;
            }
            continue;
        }
        if (c=='(' || c=='[' || c=='{') {
            enum Braces lef = braceFromChar[c];
            if (len+1 > cap) {
                cap *= 2;
                stack = realloc(stack, cap*sizeof(enum Braces));
                assert(stack);
            }
            stack[len] = lef;
            len += 1;
        } else if (c==')' || c==']' || c=='}') {
            enum Braces rig = braceFromChar[c];
            enum Braces lef = NoBrace;
            if (len > 0) {
                len -= 1;
                lef = stack[len];
            }
            if (rig!=lef) {
                r = Invalid;
                goto defer;
            }
        } else if (c=='\'') {
            sgStr = !sgStr;
        } else if (c=='\"') {
            dbStr = !dbStr;
        } else if (c=='/') {
            if (i+1<sb->len) {
                char nc = sb->cstr[i+1];
                if (nc=='/') {
                    lnCom = 1;
                } else if (nc=='*') {
                    blCom = 1;
                    i += 1;
                }
            }
        }
    }
    if (sgStr || dbStr || lnCom || blCom) {
        r = Incomplete;
    } else if (len!=0) {
        r = Incomplete;
    } else if (SimpleLast(sb)=='\\') {
        r = Incomplete;
    } else {
        r = Complete;
    }
defer:
    free(stack);
    return r;
}

int GetLine(StrBuilder *out)
{
    char inp[256] = {0};
    isz const inpLen = sizeof(inp)-1;

    do {
        if (fgets(inp, inpLen, stdin)==NULL) return 0;
        StrBuilderAppend(out, inp, strlen(inp));
    } while (out->cstr[out->len-1]!='\n');
    return 1;
}

enum InputKind {
    Empty,
    Expr,
    Stmt,
    Cmd,
    Cpp,
    Pre,
    Shell,
};
enum InputKind GetInput(StrBuilder *out, isz *outLine)
{
    isz i, line = *outLine;
    char c;

    out->len = 0;
    for (i = 0;; ++i) {
        printf("%2lld) ", 1+line);
        if (GetLine(out)==0) break;
        line += 1;
        if (IsComplete(out)) break;
    }
    *outLine = line;
    if (out->len==1) return Empty;
    if (out->cstr[0]==';') return Cmd;
    if (out->cstr[0]=='#') return Cpp;
    if (out->cstr[0]==':') return Pre;
    if (out->cstr[0]=='>') return Shell;
    c = LastChar(out);
    if (c=='}' || c==';') return Stmt;
    return Expr;
}

void ErrFunc(void *opaque, const char *msg)
{
    (void) opaque;
    fprintf(stderr, "%s\n", msg);
}

#define PATCH(FUNC) \
"#define "#FUNC"(...) "\
    "do {"\
        "if (__LINE__>LASTLINE) "#FUNC"(__VA_ARGS__);"\
    "} while(0)\n"

int Run(
    char *tccPath, char *incPath, char *libPath, 
    int argc, char **argv, isz line,
    char const *pre, isz preLen, 
    char const *first, isz firstLen, 
    char const *src, isz srcLen, 
    char const *last, isz lastLen)
{
    static char include[] = 
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <math.h>\n"
        "#include <inttypes.h>\n"
    ;
    #define BIN_FUNCTION(BITS) \
        "char *__bin"#BITS"(int"#BITS"_t x) {"\
            "static char b["#BITS"+"#BITS"/4] = {0};"\
            "int o = 0;" \
            "for (int i = 0; i<"#BITS"; ++i) {" \
                "if (i>0 && i%4==0) b[o++] = '_';" \
                "b[o++] = x<0?'1':'0'; x <<= 1;"\
            "}"\
            "b[o] = 0; return b;"\
        "}\n"
    #define GEN_BIN(BITS) "int"#BITS"_t:__bin"#BITS",uint"#BITS"_t:__bin"#BITS","
    static char prolog[] = 
        BIN_FUNCTION(8) BIN_FUNCTION(16) BIN_FUNCTION(32) BIN_FUNCTION(64)
        "#define BIN(X) _Generic((X),"
            GEN_BIN(8) GEN_BIN(16) GEN_BIN(32) GEN_BIN(64) "default:__bin64)(X)\n"
        "#define PRINT(X) "
        "do {"
            "if (__LINE__>LASTLINE) _Generic((X),"
                "int8_t:__printi8,int16_t:__printi16,"
                "int32_t:__printi32,int64_t:__printi64,"
                "uint8_t:__printu8,uint16_t:__printu16,"
                "uint32_t:__printu32,uint64_t:__printu64,"
                "long:__printil,unsigned long:__printul,"
                "float:__printf32,double:__printf64,"
                "_Bool:__printb,char:__printc,char*:__prints,"
                "default:__printp)(X);"
        "} while (0)\n"
        "void __printi8(int8_t x) {"
            "printf(\"%\"PRId8\" = 0x%02\"PRIX8\"\\n\",x,x);}\n"
        "void __printi16(int16_t x) {"
            "printf(\"%\"PRId16\" = 0x%04\"PRIX16\"\\n\",x,x);}\n"
        "void __printi32(int32_t x) {"
            "printf(\"%\"PRId32\" = 0x%08\"PRIX32,x,x);"
            "if (x<=126 && x>=33) printf(\" = '%c'\",x); puts(\"\");}\n"
        "void __printi64(int64_t x) {"
            "printf(\"%\"PRId64\" = 0x%016\"PRIX64\"\\n\",x,x);}\n"
        "void __printu8(uint8_t x) {"
            "printf(\"%\"PRIu8\" = 0x%02\"PRIX8\"\\n\",x,x);}\n"
        "void __printu16(uint16_t x) {"
            "printf(\"%\"PRIu16\" = 0x%04\"PRIX16\"\\n\",x,x);}\n"
        "void __printu32(uint32_t x) {"
            "printf(\"%\"PRIu32\" = 0x%08\"PRIX32\"\\n\",x,x);}\n"
        "void __printu64(uint64_t x) {"
            "printf(\"%\"PRIu64\" = 0x%016\"PRIX64\"\\n\",x,x);}\n"
        "void __printil(long x) {printf(\"%ld = \",x);"
            "if (4==sizeof(long)) printf(\"0x%08\"PRIX32\"\\n\",x);"
            "else printf(\"0x%016\"PRIX64\"\\n\",x);}\n"
        "void __printul(unsigned long x) {printf(\"%lu = \",x);"
            "if (4==sizeof(long)) printf(\"0x%08\"PRIX32\"\\n\",x);"
            "else printf(\"0x%016\"PRIX64\"\\n\",x);}\n"
        "void __printf32(double x) {printf(\"%g\\n\",x);}\n"
        "void __printf64(float x) {printf(\"%g\\n\",x);}\n"
        "void __printb(_Bool x) {printf(\"%s\\n\",x?\"true\":\"false\");}\n"
        "void __printc(char x) {printf(\"%c\\n\",x);}\n"
        "void __prints(char *x) {printf(\"%s\\n\",x);}\n"
        "void __printp(void *x) {printf(\"0x%p\\n\",x);}\n"
        PATCH(printf) PATCH(puts) PATCH(putchar)
        "int main(int argc, char **argv) {\n"
        "(void) argc; (void) argv;\n"
        ;
    static char epilog[] = "return 0;\n}\n";
    char lastline[256] = {0};
    static StrBuilder sb = {0};
    int r = -1;
    TCCState *s = tcc_new();

    sprintf(lastline, "#define LASTLINE %lld\n", line);

    sb.len = 0;
    StrBuilderAppend(&sb, CHARRAY(include));
    StrBuilderAppend(&sb, pre, preLen);
    StrBuilderAppend(&sb, first, firstLen);
    StrBuilderAppend(&sb, CHARRAY(prolog));
    StrBuilderAppend(&sb, lastline, strlen(lastline));
    StrBuilderAppend(&sb, src, srcLen);
    StrBuilderAppend(&sb, last, lastLen);
    StrBuilderAppend(&sb, CHARRAY(epilog));

    tcc_set_error_func(s, NULL, ErrFunc);
    tcc_set_lib_path(s, tccPath);
    tcc_add_sysinclude_path(s, incPath);
    tcc_add_library_path(s, libPath);
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    for (int i = 1; i<argc; ++i) {
        tcc_set_options(s, argv[i]);
    }
    r = tcc_compile_string(s, sb.cstr);
    if (r!=-1) {
        r = tcc_run(s, 0, (char *[]){NULL});
    }
    tcc_delete(s);

    return r;
}

void AppendLineNum(StrBuilder *sb, isz line)
{
    char buf[100] = {0};
    sprintf(buf, "#line %d\n", line);
    StrBuilderAppend(sb, buf, strlen(buf));
}

void PrintErr(char const *msg)
{
    char* buffer = NULL;
    DWORD dwError = GetLastError();
    DWORD dwFlags = FORMAT_MESSAGE_MAX_WIDTH_MASK   // no newlines
        | FORMAT_MESSAGE_ALLOCATE_BUFFER            // allocate memory
        | FORMAT_MESSAGE_FROM_SYSTEM                // get error message
        | FORMAT_MESSAGE_IGNORE_INSERTS;            // no argument (NULL)
    DWORD dwResult = FormatMessageA(dwFlags, NULL, dwError, 0, (LPSTR)&buffer, 0, NULL);
    if (dwResult==0 || buffer==NULL) {
        fprintf(stderr, "Failed to get error message from FormatMessageA()\n");
        return;
    }
    fprintf(stderr, "%s: %s\n", msg, buffer);
    LocalFree(buffer);
}

void SpawnShell(char const *sh, isz len)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    DWORD exit_status;
    char *arg; 
    isz i, j;

    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    for (i = 0; i<len; ++i) { if (!isspace(sh[i])) break; }
    for (j = 0; j<len; ++j) { if (!isspace(sh[len-1-j])) break; }
    arg = calloc(1+len-i-j, sizeof(char));
    memcpy(arg, sh+i, len-i-j);

    if (CreateProcessA(NULL, arg, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)==0) {
        PrintErr("Could not run command");
        goto final;
    }
    CloseHandle(pi.hThread);

    if (WaitForSingleObject(pi.hProcess, INFINITE)==WAIT_FAILED) {
        PrintErr("Could not wait on child process");
        goto final;
    }
    if (GetExitCodeProcess(pi.hProcess, &exit_status)==0) {
        PrintErr("Could not get process exit code");
        goto final;
    }
    if (exit_status!=0) {
        fprintf(stderr, "Command exited with exit code %lu\n", exit_status);
    }
    CloseHandle(pi.hProcess);
final:
    free(arg);
}

int main(int argc, char **argv)
{
    char const inc[] = "include";
    isz const incLen = sizeof(inc)-1;
    char const lib[] = "lib";
    isz const libLen = sizeof(lib)-1;
    isz line = 0;
    StrBuilder out = {0}, pre = {0}, first = {0}, 
        src = {0}, last = {0};
    char *tccPath, *incPath, *libPath;
    isz sep = 1+LastPathSep(argv[0], strlen(argv[0]));

    tccPath = calloc(1+sep, sizeof(char));
    memcpy(tccPath, argv[0], sep);
    incPath = calloc(1+sep+incLen, sizeof(char));
    memcpy(incPath, tccPath, sep);
    memcpy(incPath+sep, inc, incLen);
    libPath = calloc(1+sep+libLen, sizeof(char));
    memcpy(libPath, tccPath, sep);
    memcpy(libPath+sep, lib, libLen);

    puts("Type \";h\" for help");

    StrBuilderAppend(&pre, "", 0);
    StrBuilderAppend(&first, "", 0);
    StrBuilderAppend(&src, "", 0);
    StrBuilderAppend(&last, "", 0);
    for (;;) {
        isz len, outLine = line;
        char *s;
        enum InputKind kind = GetInput(&out, &outLine);
        s = out.cstr;
        len = out.len;
        if (kind==Empty) {
            continue;
        } else if (kind==Shell) {
            SpawnShell(s+1, len-1);
        } else if (kind==Cmd) {
            switch (s[1]) {
            default:
                printf("Unknown command \"%.*s\"\n", len-1, s);
            break; case 'h':
                printf("%s",
                    ";h -- show this help message\n"
                    ";q -- quit\n"
                    ";l -- list recorded code\n"
                    ";c -- clear recorded code\n"
                    "#[...] -- C preprocessor\n"
                    ":[...] -- Statements outside of main\n"
                    ">[...] -- Execute shell command\n"
                );
            break; case 'q':
                goto endloop;
            break; case 'l':
                printf("%.*s", pre.len, pre.cstr);
                printf("/* main */\n");
                printf("%.*s", src.len, src.cstr);
            break; case 'c':
                pre.len = 0;
                src.len = 0;
                line = 0;
            }
        } else {
            int ok;

            last.len = 0;
            if (kind==Stmt) {
                AppendLineNum(&last, 1+line);
                StrBuilderAppend(&last, s, len);
            } else if (kind==Expr) {
                AppendLineNum(&last, 1+line);
                StrBuilderAppend(&last, CHARRAY("PRINT(("));
                StrBuilderAppend(&last, s, len-1);
                StrBuilderAppend(&last, CHARRAY("));\n"));
            }

            first.len = 0;
            if (kind==Pre) {
                AppendLineNum(&first, 1+line);
                StrBuilderAppend(&first, s+1, len-1);
            } else if (kind==Cpp) {
                AppendLineNum(&first, 1+line);
                StrBuilderAppend(&first, s, len);
            }

            ok = Run(
                tccPath, incPath, libPath,
                argc, argv, line,
                pre.cstr, pre.len,
                first.cstr, first.len,
                src.cstr, src.len,
                last.cstr, last.len) >= 0;
            if (ok) {
                if (kind==Stmt) {
                    line = outLine;
                    StrBuilderAppend(&src, last.cstr, last.len);
                } else if (kind==Pre || kind==Cpp) {
                    line = outLine;
                    StrBuilderAppend(&pre, first.cstr, first.len);
                }
            }
        }
    }
endloop:
    return 0;
}
