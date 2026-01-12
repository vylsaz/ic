#define MINILINE_IMPLEMENTATION
#define MINILINE_IGNORE_ZWJ
#define MINILINE_HISTORY_SKIP_DUPLICATES
#include "miniline.h"

#include "libtcc/libtcc.h"
#define NOB_IMPLEMENTATION
#define NOB_NO_ECHO
#define NOB_DA_INIT_CAP 16
#include "nob.h"
#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#ifndef _WIN32
    #include <dlfcn.h>
#endif

#define CMD_SIGN ";"
#define SHL_SIGN ">"
#define CPP_SIGN "#"

#define IC_EMBED

typedef uint64_t usz;
typedef Nob_String_Builder StrBuilder;

char *GetExePath(void)
{
    return nob_temp_dir_name(nob_temp_running_executable_path());
}

char *GetTempDir(void)
{
    char const *exePath = GetExePath();
    return nob_temp_sprintf("%s/temp", exePath);
}

char LastChar(StrBuilder *sb)
{
    usz i;
    for (i = 0; i<sb->count; ++i) {
        char c = sb->items[sb->count-i-1];

        if (c=='/' &&
            i+1<sb->count && sb->items[sb->count-i-2]=='*') {
            usz j;
            for (j = i+2; j<sb->count; ++j) {
                if (sb->items[sb->count-j-1]=='*' &&
                    j+1<sb->count && sb->items[sb->count-j-2]=='/') {
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
    static enum Braces braceFromChar[] = {
        ['('] = Paren,
        [')'] = Paren,
        ['['] = Brack,
        [']'] = Brack,
        ['{'] = Curly,
        ['}'] = Curly,
    };

    enum CompleteResult r = Incomplete;
    bool sgStr = 0, dbStr = 0, lnCom = 0, blCom = 0, esc = 0;
    struct BracesStack {
        uint8_t *items;
        usz count;
        usz capacity;
    } stack = {0};

    usz i;
    for (i = 0; i<sb->count-1; ++i) {
        char c = sb->items[i];
        if (esc==1) {
            esc = 0;
            continue;
        }
        if (sgStr || dbStr) {
            if (esc==0 && c=='\\') esc = 1;
            if (c!=(sgStr?'\'':'\"')) continue;
        }
        if (lnCom) {
            if (c=='\n') lnCom = 0;
            continue;
        }
        if (blCom) {
            if (c=='*' && i+1<sb->count && sb->items[i+1]=='/') {
                blCom = 0;
            }
            continue;
        }
        if (c=='(' || c=='[' || c=='{') {
            enum Braces lef = braceFromChar[(int)c];
            nob_da_append(&stack, lef);
        } else if (c==')' || c==']' || c=='}') {
            enum Braces rig = braceFromChar[(int)c];
            enum Braces lef = NoBrace;
            if (stack.count > 0) {
                stack.count -= 1;
                lef = stack.items[stack.count];
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
            if (i+1<sb->count) {
                char nc = sb->items[i+1];
                if (nc=='/') {
                    lnCom = 1;
                } else if (nc=='*') {
                    blCom = 1;
                    i += 1;
                }
            }
        }
    }
    if (lnCom || blCom) {
        r = Incomplete;
    } else if (dbStr || sgStr) {
        r = Invalid;
    } else if (stack.count > 0) {
        r = Incomplete;
    } else if (sb->items[sb->count-2]=='\\') {
        r = Incomplete;
    } else {
        r = Complete;
    }
defer:
    nob_da_free(stack);
    return r;
}

bool IsCppOf(Nob_String_View sv, char const *cstr)
{
    sv = nob_sv_trim_left(sv);
    if (!nob_sv_starts_with(sv, nob_sv_from_cstr("#"))) return false;
    sv = nob_sv_from_parts(sv.data + 1, sv.count - 1);
    sv = nob_sv_trim_left(sv);
    return nob_sv_starts_with(sv, nob_sv_from_cstr(cstr));
}

enum CompleteResult IsCppComplete(StrBuilder *sb) {
    int64_t stack = 0;

    usz i;
    for (i = 0; i<sb->count; ++i) {
        Nob_String_View sv;
        bool cont = 0;
        usz j;
        for (j = 0; i+j<sb->count; ++j) {
            char c = sb->items[i+j];
            if (cont) {
                cont = 0;
                continue;
            }
            if (cont==0 && c=='\\') {
                cont = 1;
            }
            if (c=='\n') break;
        }
        sv = nob_sv_from_parts(&sb->items[i], j);
        if (IsCppOf(sv, "if")) {
            stack += 1;
        } else if (IsCppOf(sv, "endif")) {
            stack -= 1;
        }
        i += j;
    }
    if (stack<0) {
        return Invalid;
    } else if (stack>0) {
        return Incomplete;
    } else {
        return Complete;
    }
}

int SbReadLine(StrBuilder *out, char const *prompt)
{
#if 0
    char inp[256] = {0};
    usz const inpLen = sizeof(inp)-1;
    printf("%s", prompt);
    do {
        if (fgets(inp, inpLen, stdin)==NULL) return 0;
        nob_sb_append_cstr(out, inp);
    } while (nob_da_last(out)!='\n');
    return 1;
#else
    char *inp = mlReadLine(prompt);
    if (inp==NULL) return 0;
    nob_sb_append_cstr(out, inp);
    nob_da_append(out, '\n');
    free(inp);
    return 1;
#endif
}

bool TrimPrefixSv(Nob_String_View sv, char const *cstr)
{
    sv = nob_sv_trim_left(sv);
    return nob_sv_starts_with(sv, nob_sv_from_cstr(cstr));
}

bool TrimPrefix(StrBuilder *out, char const *cstr)
{
    Nob_String_View sv = nob_sv_from_parts(out->items, out->count);
    return TrimPrefixSv(sv, cstr);
}

bool IsEmpty(StrBuilder *out)
{
    usz i;
    for (i = 0; i<out->count; ++i) {
        if (!isspace(out->items[i])) return false;
    }
    return true;
}

enum InputKind {
    Empty,
    Expr,
    Stmt,
    Cmd,
    Pre,
    Shell,
    InputEnd,
};
enum InputKind GetInput(StrBuilder *out, usz *outLine, bool isTop, bool isTimed)
{
    enum InputKind r = Empty;
    size_t mark = nob_temp_save();
    char *prompt;
    
    bool isCmd = false;
    usz line = *outLine;
    char c;

    out->count = 0;
    for (;;) {
        if (isCmd) {
            prompt = SHL_SIGN" ";
        } else {
            prompt = nob_temp_sprintf("%s%2zu%c ", isTimed? "t:": "", 1+line, isTop? ']': ')');
        }
        if (SbReadLine(out, prompt)==0) {
            r = InputEnd;
            break;
        }
        if (out->items[out->count-2]=='\\') {
            if (out->items[0]==SHL_SIGN[0]) {
                out->count -= 2;
                isCmd = true;
            }
            continue;
        }
        line += 1;
        if (out->items[0]==CMD_SIGN[0]) {
            r = Cmd;
            break;
        }
        if (out->items[0]==SHL_SIGN[0]) {
            r = Shell;
            break;
        }
        if (TrimPrefix(out, CPP_SIGN)) {
            if (IsCppComplete(out)) {
                r = Pre;
                break;
            }
        } else if (IsComplete(out)) break;
    }
    nob_temp_rewind(mark);
    *outLine = line;
    if (r!=Empty) return r;
    if (IsEmpty(out)) return Empty;
    c = LastChar(out);
    if (c=='}' || c==';') return Stmt;
    return Expr;
}

void SimpleQuote(char const **opt, usz optLen, StrBuilder *out)
{
    for (usz i = 0; i<optLen; ++i) {
        char const *o = opt[i];
        usz len = strlen(o);
        nob_da_append(out, '\"');
        for (usz j = 0; j < len; ++j) {
            char x = o[j];
            switch (x) {
            case '\\': nob_sb_append_cstr(out, "\\\\"); break;
            case '\"': nob_sb_append_cstr(out, "\\\""); break;
            default:   nob_da_append(out, x);           break;
            }
        }
        nob_da_append(out, '\"');
        if (i+1<optLen) nob_da_append(out, ' ');
    }
}

// temp
void ParseShell(char const *sh, usz len, Nob_Cmd *cmd)
{
    usz i;
    int inQuote = 0, wasSpace = 0;
    Nob_String_View sv = nob_sv_from_parts(sh, len);
    StrBuilder sb = {0};

    #define FINISH_ARG do {\
        nob_sb_append_null(&sb);\
        nob_da_append(cmd, nob_temp_strdup(sb.items));\
        sb.count = 0;} while (0)

    sv = nob_sv_trim(sv);
    for (i = 0; i<sv.count; ++i) {
        char c = sv.data[i];
        if (inQuote==1) {
            wasSpace = 0;
            if (c!='\'') {
                nob_da_append(&sb, c);
            } else if (i+1<sv.count && sv.data[i+1]=='\'') {
                nob_da_append(&sb, c); ++i;
            } else {
                inQuote = 0;
            }
        } else if (c=='\'') {
            inQuote = 1;
            wasSpace = 0;
        } else if (c==' ') {
            if (wasSpace==0) FINISH_ARG;
            wasSpace = 1;
        } else { 
            wasSpace = 0;
            nob_da_append(&sb, c);
        }
    }
    FINISH_ARG;

    #undef FINISG_ARG
    nob_sb_free(sb);
}

void Pwd(void)
{
    char const *cwd = nob_get_current_dir_temp();
    printf("Current directory: %s\n", cwd);
}

void Cd(Nob_Cmd *cmd)
{                   
    if (cmd->count>2) {
        nob_log(NOB_ERROR, "cd: too many arguments");
        return;
    }
    if (cmd->count==2) {
        char const *dir = cmd->items[1];
        nob_set_current_dir(dir);
    }
    Pwd();
}

// let leak
struct {
    char **items;
    uint64_t count;
    uint64_t capacity;
} DirStack = {0};

void Dirs(void)
{
    Pwd();
    for (uint64_t i = DirStack.count; i>0; --i) {
        printf("%s\n", DirStack.items[i-1]);
    }
}

void Pushd(Nob_Cmd *cmd)
{
    if (cmd->count>2) {
        nob_log(NOB_ERROR, "pushd: too many arguments");
        return;
    }

    char *cwd = strdup(nob_get_current_dir_temp());
    if (cmd->count==1) {
        if (DirStack.count==0) {
            nob_log(NOB_ERROR, "pushd: no other directory");
            return;
        }
        char *dir = nob_da_last(&DirStack);
        nob_set_current_dir(dir);
        nob_da_last(&DirStack) = cwd;
        free(dir);
    } else {
        nob_da_append(&DirStack, cwd);
        char const *dir = cmd->items[1];
        nob_set_current_dir(dir);
    }
    Dirs();
}

void Popd(void)
{
    if (DirStack.count==0) {
        nob_log(NOB_ERROR, "popd: directory stack empty");
        return;
    }
    char *dir = nob_da_last(&DirStack);
    DirStack.count -= 1;
    nob_set_current_dir(dir);
    free(dir);
    Dirs();
}

void Ls(Nob_Cmd *cmd)
{
    if (cmd->count>2) {
        nob_log(NOB_ERROR, "ls: too many arguments");
        return;
    }
    Nob_File_Paths entries = {0};
    char const *cwd; 
    if (cmd->count==2) {
        cwd = cmd->items[1];
    } else {
        cwd = nob_get_current_dir_temp();
    }
    if (!nob_read_entire_dir(cwd, &entries)) {
        nob_log(NOB_ERROR, "ls: could not read directory %s", cwd);
        return;
    }
    for (usz i = 0; i<entries.count; ++i) {
        printf("%s\n", entries.items[i]);
    }
    nob_da_free(entries);
}

bool SetEnvTemp(char const *name, char const *value);
char *GetEnvTemp(char const *name);

#ifdef _WIN32
#define MY_PATH_SEP ";"
#else
#define MY_PATH_SEP ":"
#endif

void PathPrepend(char const *newPath)
{
    char *oldPath = GetEnvTemp("PATH");
    if (oldPath==NULL) {
        nob_log(NOB_ERROR, "path: could not get PATH environment variable");
        return;
    }
    char *combined = nob_temp_sprintf("%s" MY_PATH_SEP "%s", newPath, oldPath);
    if (!SetEnvTemp("PATH", combined)) {
        nob_log(NOB_ERROR, "path: could not set PATH environment variable");
        return;
    }
    printf("New PATH:\n%s\n", GetEnvTemp("PATH"));
}

void PathCmd(Nob_Cmd *cmd)
{
    if (cmd->count==1) {
        printf("PATH:\n%s\n", GetEnvTemp("PATH"));
    } else if (cmd->count==3 && strcmp(cmd->items[1], "+=")==0) {
        PathPrepend(cmd->items[2]);
    } else if (cmd->count==3 && strcmp(cmd->items[1], "=")==0) {
        if (!SetEnvTemp("PATH", cmd->items[2])) {
            nob_log(NOB_ERROR, "path: could not set PATH environment variable");
            return;
        }
        printf("New PATH:\n%s\n", GetEnvTemp("PATH"));
    } else {
        nob_log(NOB_ERROR, "path: invalid arguments");
    }
}

void ShellHelp(void)
{
    printf("%s",
        "Built-in shell commands:\n"
        "  pwd              Print current directory\n"
        "  cd [dir]         Change current directory to 'dir'\n"
        "                   or print current directory\n"
        "  dirs             Print directory stack\n"
        "  pushd [dir]      Push current directory to stack and change to 'dir'\n"
        "                   or swap current directory with top of stack\n"
        "  popd             Pop directory from stack and change to it\n"
        "  ls [dir]         (Windows only) List contents of 'dir'\n"
        "                   or current directory\n"
        "  path [=/+= dir]  Print or modify PATH environment variable\n"
        "                   += prepends 'dir' to PATH\n"
        "Others:            Call the executable with arguments\n"
        "                   e.g. > vim file.c\n"
    );
}

void SpawnShell(char const *sh, usz len)
{
    Nob_Cmd cmd = {0};
    size_t mark = nob_temp_save();
    ParseShell(sh, len, &cmd);
    if (strcmp(cmd.items[0], "")==0) {
        ShellHelp();
    } else if (strcmp(cmd.items[0], "cd")==0) {
        Cd(&cmd);
    } else if (strcmp(cmd.items[0], "pwd")==0) {
        Pwd();
    } else if (strcmp(cmd.items[0], "dirs")==0) {
        Dirs();
    } else if (strcmp(cmd.items[0], "pushd")==0) {
        Pushd(&cmd);
    } else if (strcmp(cmd.items[0], "popd")==0) {
        Popd();
    } else if (strcmp(cmd.items[0], "path")==0) {
        PathCmd(&cmd);
#ifdef _WIN32
    } else if (strcmp(cmd.items[0], "ls")==0) {
        Ls(&cmd);
#endif
    } else {
        nob_cmd_run(&cmd);
    }
    nob_temp_rewind(mark);
    nob_da_free(cmd);
}

#ifdef _WIN32

// temp
WCHAR *WstrFromCstr(char const *str)
{
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    WCHAR *wstr = nob_temp_alloc(len * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, len);
    return wstr;
}

// temp
char *CstrFromWstr(WCHAR const *wstr)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char *str = nob_temp_alloc(len);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL);
    return str;
}

#endif

bool SetEnvTemp(char const *name, char const *value)
{
#ifdef _WIN32
    WCHAR *wname = WstrFromCstr(name);
    WCHAR *wvalue = WstrFromCstr(value);
    return SetEnvironmentVariableW(wname, wvalue);
#else
    return 0==setenv(name, value, 1);
#endif
}

char *GetEnvTemp(char const *name)
{
#ifdef _WIN32
    WCHAR *wname = WstrFromCstr(name);
    DWORD dwSz = GetEnvironmentVariableW(wname, NULL, 0);
    if (dwSz==0) {
        return NULL;
    }

    WCHAR *wstr = nob_temp_alloc(dwSz * sizeof(WCHAR));
    GetEnvironmentVariableW(wname, wstr, dwSz);
    return CstrFromWstr(wstr);
#else
    char *str = getenv(name);
    if (str==NULL) {
        return NULL;
    }
    return nob_temp_strdup(str);
#endif
}

// temp
char *GetCC(void)
{
#ifdef _WIN32
    return GetEnvTemp("CC");
#else
    return GetEnvTemp("CC");
#endif
}

void ErrFunc(void *opaque, const char *msg)
{
    (void) opaque;
    fprintf(stderr, "%s\n", msg);
}

// temp
bool Embed(StrBuilder *res, StrBuilder *in)
{
    usz i;
    for (i = 0; i<in->count; ++i) {
        Nob_String_View sv;
        bool cont = 0;
        usz j;
        for (j = 0; i+j<in->count; ++j) {
            char c = in->items[i+j];
            if (cont) {
                cont = 0;
                continue;
            }
            if (cont==0 && c=='\\') {
                cont = 1;
            }
            if (c=='\n') break;
        }
        sv = nob_sv_from_parts(&in->items[i], j);
        if (IsCppOf(sv, "embed")) {
            usz k, p = 0;
            bool inQuote = false;
            for (k = 0; k<sv.count; ++k) {
                char c = sv.data[k];
                if (c=='\"') {
                    if (inQuote) {
                        break;
                    } else {
                        inQuote = true;
                        p = k+1;
                    }
                }
            }
            if (p==0 || p >= k) {
                nob_log(NOB_ERROR, "Invalid #embed directive");
                return false;
            }
            usz n = k - p;
            StrBuilder filePath = {0}, fileContent = {0};
            nob_sb_append_buf(&filePath, sv.data+p, n);
            nob_sb_append_null(&filePath);
            if (!nob_read_entire_file(filePath.items, &fileContent)) {
                return false;
            }
            for (usz x = 0; x<fileContent.count; ++x) {
                unsigned char c = fileContent.items[x];
                nob_sb_append_cstr(res, nob_temp_sprintf("0x%02X", c));
                if (x+1<fileContent.count) { nob_da_append(res, ','); }
            }
            nob_sb_append_cstr(res, "\n");
            nob_sb_free(filePath);
            nob_sb_free(fileContent);
        } else {
            nob_sb_append_buf(res, sv.data, sv.count);
            nob_sb_append_cstr(res, "\n");
        }
        i += j;
    }
    return true;
}

// assume two's complement
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

#ifdef _WIN32
#define PTR_FMT "0x%p"
#else
#define PTR_FMT "%p"
#endif

#define PATCH(FUNC) \
"#define "#FUNC"(...) "\
    "do {"\
        "if (__LINE__>LASTLINE) "#FUNC"(__VA_ARGS__);"\
    "} while(0)\n"

// temp
bool PrepareCString(usz line, StrBuilder *pre, StrBuilder *first,
    StrBuilder *src, StrBuilder *last, StrBuilder *sb)
{
    static char include[] = 
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdbool.h>\n"
        "#include <stdalign.h>\n"
        "#include <math.h>\n"
        "#include <inttypes.h>\n"
        "#include <float.h>\n"
        "#include <wchar.h>\n"
        // nob helpers
        "#define NOB_REBUILD_URSELF\n"
        "#define NOB_IMPLEMENTATION\n"
    ;
    
    static char prolog[] = 
        "#line 1 \"nowhere\"\n"
        BIN_FUNCTION(8) BIN_FUNCTION(16) BIN_FUNCTION(32) BIN_FUNCTION(64)
        "char *__binfloat32(float x) {"
            "union {uint32_t u; float f;} v; v.f = x; return __bin32(v.u);}\n"
        "char *__binfloat64(double x) {"
            "union {uint64_t u; double f;} v; v.f = x; return __bin64(v.u);}\n"
        "#define BIN(X) _Generic((X),"
            GEN_BIN(8) GEN_BIN(16) GEN_BIN(32) GEN_BIN(64) 
            "float:__binfloat32,double:__binfloat64,default:__bin64)(X)\n"
        "#define __IC_STRINGIFY1(...) #__VA_ARGS__\n"
        "#define __IC_STRINGIFY(...) __IC_STRINGIFY1(__VA_ARGS__)\n"
        "#define ONCE_LINE (__LINE__>LASTLINE)\n"
        "#define ONCE if (__LINE__>LASTLINE)\n"
        "#define PRINT(X) "
        "do {"
            "if (__LINE__>LASTLINE) _Generic((X),"
                "int8_t:__printi8,int16_t:__printi16,"
                "int32_t:__printi32,int64_t:__printi64,"
                "uint8_t:__printu8,uint16_t:__printu16,"
                "uint32_t:__printu32,uint64_t:__printu64,"
            #ifdef _WIN32
                "long:__printil,unsigned long:__printul,"
            #else
                "long long:__printill,unsigned long long:__printull,"
            #endif
                "float:__printf32,double:__printf64,long double:__printld,"
                "bool:__printb,char:__printc,"
                "char*:__prints,char const*:__printcs,"
                "default:__printp)(X);"
        "} while (0)\n"
        "#define WPRINT(X) "
        "do {"
            "if (__LINE__>LASTLINE) _Generic((X),"
            "wchar_t*:__printws,wchar_t const*:__printwcs,"
            "wchar_t:__printwc,default:__printp)(X);"
        "} while(0)\n"
        "void __printi8(int8_t x) {"
            "printf(\"(int8_t) %\"PRId8\" = 0x%02\"PRIX8\"\\n\",x,(uint8_t)x);}\n"
        "void __printi16(int16_t x) {"
            "printf(\"(int16_t) %\"PRId16\" = 0x%04\"PRIX16\"\\n\",x,(uint16_t)x);}\n"
        "void __printi32(int32_t x) {"
            "printf(\"(int32_t) %\"PRId32\" = 0x%08\"PRIX32,x,x);"
            "switch(x) {"
            "case 34:case 39:case 92: printf(\" = '\\\\%c'\",x); break;"
            "case '\\a': printf(\" = '\\\\a'\"); break;"
            "case '\\b': printf(\" = '\\\\b'\"); break;"
            "case '\\f': printf(\" = '\\\\n'\"); break;"
            "case '\\n': printf(\" = '\\\\n'\"); break;"
            "case '\\r': printf(\" = '\\\\r'\"); break;"
            "case '\\t': printf(\" = '\\\\t'\"); break;"
            "case '\\v': printf(\" = '\\\\v'\"); break;"
            "default: if (x<=126 && x>=32) printf(\" = '%c'\",x);}"
            "puts(\"\");}\n"
        "void __printi64(int64_t x) {"
            "printf(\"(int64_t) %\"PRId64\" = 0x%016\"PRIX64\"\\n\",x,x);}\n"
        "void __printu8(uint8_t x) {"
            "printf(\"(uint8_t) %\"PRIu8\" = 0x%02\"PRIX8\"\\n\",x,x);}\n"
        "void __printu16(uint16_t x) {"
            "printf(\"(uint16_t) %\"PRIu16\" = 0x%04\"PRIX16\"\\n\",x,x);}\n"
        "void __printu32(uint32_t x) {"
            "printf(\"(uint32_t) %\"PRIu32\" = 0x%08\"PRIX32\"\\n\",x,x);}\n"
        "void __printu64(uint64_t x) {"
            "printf(\"(uint64_t) %\"PRIu64\" = 0x%016\"PRIX64\"\\n\",x,x);}\n"
    #ifdef _WIN32
        "void __printil(long x) {printf(\"(long) %ld = \",x);"
            "if (4==sizeof(long)) printf(\"0x%08lX\\n\",x);"
            "else printf(\"0x%016lX\\n\",x);}\n"
        "void __printul(unsigned long x) {printf(\"(unsigned long) %lu = \",x);"
            "if (4==sizeof(long)) printf(\"0x%08lX\\n\",x);"
            "else printf(\"0x%016lX\\n\",x);}\n"
    #else
        "void __printill(long long x) {printf(\"(long long) %lld = \",x);"
            "if (8==sizeof(long long)) printf(\"0x%016llX\\n\",x);"
            "else printf(\"0x%08llX\\n\",x);}\n"
        "void __printull(unsigned long long x) {printf(\"(unsigned long long) %llu = \",x);"
            "if (8==sizeof(unsigned long long)) printf(\"0x%016llX\\n\",x);"
            "else printf(\"0x%08llX\\n\",x);}\n"
    #endif
        "void __printf32(double x) {printf(\"(float) %g\\n\",x);}\n"
        "void __printf64(float x) {printf(\"(double) %g\\n\",x);}\n"
        "void __printld(long double x) {printf(\"(long double) %Lg\\n\",x);}\n"
        "void __printb(_Bool x) {printf(\"%s\\n\",x?\"true\":\"false\");}\n"
        "void __printc(char x) {"
            "switch (x) {"
            "case 34:case 39:case 92: printf(\"'\\\\%c'\",x); break;"
            "case '\\a': printf(\"'\\\\a'\"); break;"
            "case '\\b': printf(\"'\\\\b'\"); break;"
            "case '\\f': printf(\"'\\\\n'\"); break;"
            "case '\\n': printf(\"'\\\\n'\"); break;"
            "case '\\r': printf(\"'\\\\r'\"); break;"
            "case '\\t': printf(\"'\\\\t'\"); break;"
            "case '\\v': printf(\"'\\\\v'\"); break;"
            "default:"
                "if (x<=126 && x>=32) printf(\"'%c'\",x);"
                "else printf(\"'\\\\x%02X'\",(int)(unsigned char)x);}"
            "puts(\"\");}\n"
        "void __prints(char *x) {printf(\"%s\\n\",x);}\n"
        "void __printcs(char const*x) {printf(\"%s\\n\",x);}\n"
        "void __printwc(wchar_t x) {printf(\"%lc\\n\",x);}\n"
        "void __printws(wchar_t *x) {printf(\"%ls\\n\",x);}\n"
        "void __printwcs(wchar_t const*x) {printf(\"%ls\\n\",x);}\n"
        "void __printp(void *x) {printf(\""PTR_FMT"\\n\",x);}\n"
        "void __printmem(void *x, size_t sz) {"
            "size_t i, j, k; uint8_t *a = x;"
            "puts(\"Offset(h)  00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f  Decoded Text\");"
            "for (i = 0; i<sz; ++i) {"
                "if (i%16==0) printf(\"%09"PRIx64"  \", i);"
                "printf(\"%02"PRIx8" \", a[i]);"
                "if ((i+1)%16==0) {printf(\" \");"
                    "for (j = 16*(i/16); j<=i; ++j)" 
                        "if (a[j]<=126 && a[j]>=33) printf(\"%c\",a[j]); else printf(\".\");"
                    "puts(\"\");}}"
            "if ((k = i%16)!=0) {"
                "for (j = 0; j<16-k; ++j) printf(\"   \"); printf(\" \");"
                "for (j = i-k; j<i; ++j)" 
                    "if (a[j]<=126 && a[j]>=33) printf(\"%c\",a[j]); else printf(\".\");"
                "puts(\"\");}}\n"
        PATCH(printf) PATCH(puts) PATCH(putchar)
    #ifdef _WIN32
        "__declspec(dllexport)"
    #endif
        "int ic_main(int argc, char **argv) {\n"
        "(void) argc; (void) argv;\n"
        ;

    static char epilog[] = "return 0;\n}\n";

    char *lastline = nob_temp_sprintf("#define LASTLINE %zu\n", line);

#ifdef IC_EMBED
#define IC_APPEND_BUF(SRC) if (!Embed(sb, SRC)) return false;
#else
#define IC_APPEND_BUF(SRC) nob_sb_append_buf(sb, (SRC)->items, (SRC)->count)
#endif

    nob_sb_append_cstr(sb, include);
    IC_APPEND_BUF(pre);
    IC_APPEND_BUF(first);
    nob_sb_append_cstr(sb, prolog);
    nob_sb_append_cstr(sb, lastline);
    IC_APPEND_BUF(src);
    IC_APPEND_BUF(last);
    nob_sb_append_cstr(sb, epilog);

    return true;
}

char const *myCompiler = NULL;

char const *GetCompiler(void)
{
    if (myCompiler!=NULL) {
        return myCompiler;
    }
    char *compiler = GetCC();
    if (compiler!=NULL) {
        return compiler;
    }
    Nob_Cmd test = {
        .capacity = 1,
        .count = 0,
        .items = nob_temp_alloc(sizeof(char *) * 1),
    };
    nob_cc(&test);
    return test.items[0];
}

char const *exePath;
char const *tempDir;

char const *tccPath;
char const *incPath;
char const *libPath;

char const *outRedirect;
char const *errRedirect;

char const *rawOutPath;
char const *outPath;

char const *inpPath;

void SetupPaths(void)
{
    exePath = GetExePath();
    tempDir = GetTempDir();

    // tcc paths
    tccPath = GetExePath();
    incPath = nob_temp_sprintf("%s/include", tccPath);
    libPath = nob_temp_sprintf("%s/lib", tccPath);

    // for compiler detection & cl.exe output redirection
    outRedirect = nob_temp_sprintf("%s/_cc_out.txt", tempDir);
    errRedirect = nob_temp_sprintf("%s/_cc_err.txt", tempDir);

    // output path without extension for cl.exe
    rawOutPath = nob_temp_sprintf("%s/ic", tempDir);

    // output ic shared library
    char const *ext = 
#ifdef _WIN32
    ".dll"
#else
    ".so"
#endif
    ;
    outPath = nob_temp_sprintf("%s%s", rawOutPath, ext);

    // input file for cc
    inpPath = nob_temp_sprintf("%s/_ic.c", tempDir);
}

enum CompilerType {
    COMPILER_UNDECIDED,
    CL_EXE,
    OTHER_COMPILER,
} compilerType = COMPILER_UNDECIDED;

void SetCompilerType(void)
{
    Nob_Cmd cc = {0};
    nob_da_append(&cc, GetCompiler());
    nob_da_append(&cc, "--version");
    Nob_Log_Level old = nob_minimal_log_level;
    nob_minimal_log_level = NOB_NO_LOGS;
    nob_cmd_run(&cc, .stdout_path=outRedirect, .stderr_path=errRedirect);
    nob_minimal_log_level = old;
    nob_da_free(cc);

    StrBuilder sb = {0};
    if (!nob_read_entire_file(errRedirect, &sb)) { return; }
    if (nob_sv_starts_with(nob_sv_from_parts(sb.items, sb.count), 
            nob_sv_from_cstr("Microsoft (R) C/C++ Optimizing Compiler"))) {
        compilerType = CL_EXE;
    } else {
        compilerType = OTHER_COMPILER;
    }
    nob_sb_free(sb);
}

void CompilerSetup(Nob_Cmd *cc)
{
    if (compilerType==CL_EXE) {
        nob_da_append(cc, "/nologo");
        nob_da_append(cc, "/std:c17");
    }
}

void TranslateWerror(Nob_Cmd *cc)
{
    if (compilerType==CL_EXE) {
        nob_da_append(cc, "/WX");
    } else  {
        nob_da_append(cc, "-Werror");
    }
}

void TranslateDllOutput(Nob_Cmd *cc)
{
    if (compilerType==CL_EXE) {
        nob_cmd_append(cc, "/LD",
            nob_temp_sprintf("/Fo:%s", rawOutPath),
            nob_temp_sprintf("/Fe:%s", rawOutPath));
    } else  {
        nob_cmd_append(cc, "-shared", "-o", outPath);
    }
}

bool TranslateCompile(Nob_Cmd *cc)
{
    Nob_Cmd_Opt ccOpt = {0};
    if (compilerType==CL_EXE) {
        ccOpt.stdout_path = outRedirect;
    }
    if (!nob_cmd_run_opt(cc, ccOpt)) {
        if (compilerType==CL_EXE) {
            StrBuilder ccErr = {0};
            if (nob_read_entire_file(ccOpt.stdout_path, &ccErr)) {
                if (nob_da_last(&ccErr)=='\n') ccErr.count -= 1;
                if (nob_da_last(&ccErr)=='\r') ccErr.count -= 1;
                nob_sb_append_null(&ccErr);
                nob_log(NOB_ERROR, "%s", ccErr.items);
            }
            nob_sb_free(ccErr);
        }
        return false;
    }
    return true;
}

#ifdef _WIN32

IMAGE_SECTION_HEADER *FindSectionByRVA(
    IMAGE_SECTION_HEADER *sections, WORD num_sections, DWORD rva) {
    for (WORD i = 0; i < num_sections; ++i) {
        IMAGE_SECTION_HEADER section_header = sections[i];
        if ((section_header.VirtualAddress <= rva) && 
            (rva < (section_header.VirtualAddress + section_header.SizeOfRawData))) {
            return &sections[i];
        }
    }
    return NULL;
}

struct MyListOfStrings {
    char const **items;
    usz count;
    usz capacity;
};

// temp
bool FindDllExports(char const *dll, struct MyListOfStrings *func_names)
{
    Nob_String_Builder sb = {0};
    nob_read_entire_file(dll, &sb);
    
    char const *data_base = sb.items;
    
    IMAGE_DOS_HEADER *dos_header = (IMAGE_DOS_HEADER *)data_base;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    IMAGE_NT_HEADERS *nt_headers = (IMAGE_NT_HEADERS *)(data_base + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    IMAGE_FILE_HEADER file_header = nt_headers->FileHeader;

    IMAGE_OPTIONAL_HEADER optional_header = nt_headers->OptionalHeader;

    IMAGE_DATA_DIRECTORY export_directory = optional_header.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    DWORD export_rva = export_directory.VirtualAddress;
    // DWORD export_size = export_directory.Size;

    WORD num_sections = file_header.NumberOfSections;
    IMAGE_SECTION_HEADER *sections = malloc(sizeof(IMAGE_SECTION_HEADER) * num_sections);
    memcpy(sections, 
        data_base + dos_header->e_lfanew + sizeof(IMAGE_NT_HEADERS), 
        sizeof(IMAGE_SECTION_HEADER) * num_sections
    );

    // static char const export_sig[6] = {'.','e','d','a','t','a'};

    #define RVAtoPtr(ptr, rva) do { \
        IMAGE_SECTION_HEADER *section = FindSectionByRVA(sections, num_sections, (rva)); \
        assert(section != NULL); \
        (ptr) = (void *)(data_base+(uintptr_t)(section->PointerToRawData+((rva)-section->VirtualAddress))); \
    } while(0)

    IMAGE_EXPORT_DIRECTORY *exports;
    RVAtoPtr(exports, export_rva);

    DWORD num_name_rvas = exports->NumberOfNames;
    DWORD *name_rvas;
    RVAtoPtr(name_rvas, exports->AddressOfNames);
    for (DWORD i = 0; i < num_name_rvas; ++i) {
        char const *func_name;
        RVAtoPtr(func_name, name_rvas[i]);
        nob_da_append(func_names, nob_temp_strdup(func_name));
    }

    free(sections);
    nob_sb_free(sb);
    return true;

    #undef RVAtoPtr
}

bool WinSearchDlls(Nob_Cmd *opt, struct MyListOfStrings *full_dlls)
{
    static struct MyListOfStrings search_paths = {0}, dlls = {0};
    static char const dash_L[] = "-L";
    size_t const dash_L_len = sizeof(dash_L)-1;
    static char const dash_l[] = "-l";
    size_t const dash_l_len = sizeof(dash_l)-1;
    static char const dash_l_colon[] = "-l:";
    size_t const dash_l_colon_len = sizeof(dash_l_colon)-1;

    search_paths.count = 0;
    dlls.count = 0;

    for (usz i = 0; i<opt->count; ++i) {
        if (strcmp(opt->items[i], dash_L)==0 && i+1<opt->count) {
            ++i;
            nob_da_append(&search_paths, opt->items[i]);
        } else if (strncmp(opt->items[i], dash_L, dash_L_len)==0) {
            nob_da_append(&search_paths, opt->items[i]+dash_L_len);
        } else if (strncmp(opt->items[i], dash_l_colon, dash_l_colon_len)==0) {
            char const *dll = opt->items[i]+dash_l_colon_len;
            if (dll[0]=='\0') {
                nob_log(NOB_ERROR, "invalid -l: option");
                return false;
            } else {
                // use as is
                nob_da_append(&dlls, dll);
            }
        } else if (strcmp(opt->items[i], dash_l)==0 && i+1<opt->count) {
            ++i;
            char const *dll = nob_temp_sprintf("%s.dll", opt->items[i]);
            nob_da_append(&dlls, dll);
        } else if (strncmp(opt->items[i], dash_l, dash_l_len)==0) {
            char const *dll = nob_temp_sprintf("%s.dll", opt->items[i]+dash_l_len);
            nob_da_append(&dlls, dll);
        }
    }

    // nob_da_append(&search_paths, exePath);
    nob_da_append(&search_paths, nob_temp_sprintf("%s\\system32", GetEnvTemp("SystemRoot")));

    full_dlls->count = 0;

    for (usz i = 0; i<dlls.count; ++i) {
        char const *dll_name = dlls.items[i];
        bool found = false;
        for (usz j = 0; j<search_paths.count; ++j) {
            char const *dir = search_paths.items[j];
            char *full_path = nob_temp_sprintf("%s/%s", dir, dll_name);
            if (nob_file_exists(full_path)) {
                nob_da_append(full_dlls, full_path);
                found = true;
                break;
            }
        }
        if (!found) {
            nob_log(NOB_WARNING, "could not find DLL '%s'", dll_name);
        }
    }

    return true;
}

struct MyHMODULEs {
    HMODULE *items;
    usz count;
    usz capacity;
};

bool WinLoadDlls(struct MyListOfStrings *full_dlls, struct MyHMODULEs *hs)
{
    for (usz i = 0; i<full_dlls->count; ++i) {
        char const *dll = full_dlls->items[i];

        HMODULE module = LoadLibraryA(dll);
        if (module==NULL) {
            nob_log(NOB_ERROR, "failed to load DLL '%s'", dll);
            return false;
        }
        nob_da_append(hs, module);
    }
    return true;
}

bool WinImportDlls(TCCState *s, struct MyListOfStrings *full_dlls, struct MyHMODULEs *hs)
{
    static struct MyListOfStrings func_names = {0};

    for (usz i = 0; i<full_dlls->count; ++i) {
        char const *dll = full_dlls->items[i];
        func_names.count = 0;
        if (!FindDllExports(dll, &func_names)) {
            nob_log(NOB_ERROR, "failed to read exports from '%s'", dll);
            return false;
        }

        HMODULE module = hs->items[i];
        for (usz j = 0; j<func_names.count; ++j) {
            char const *func_name = func_names.items[j];
            FARPROC f = GetProcAddress(module, func_name);
            if (f==NULL) {
                nob_log(NOB_ERROR, "failed to import '%s' from '%s'", func_name, dll);
                return false;
            }
            tcc_add_symbol(s, func_name, f);
        }
    }

    return true;
}

#endif

typedef enum RunType {
    RT_MEM,
    RT_DLL,
    RT_CC,
} RunType;

int Run(RunType rt, usz line,
    Nob_Cmd *opt, Nob_Cmd *arg, 
    StrBuilder *pre, StrBuilder *first,
    StrBuilder *src, StrBuilder *last,
    bool werror)
{
    typedef int (*IcMain)(int, char **);
    IcMain ic_main;
    TCCState *s = NULL;
    static StrBuilder sbSrc = {0}, sbOpt = {0};
    int r = -1;
    usz i, mark = nob_temp_save();
    int myArgsLen = 1+(int)arg->count; assert(arg->count<INT32_MAX);
    char **myArgs = nob_temp_alloc((myArgsLen)*sizeof(char *));
#ifdef _WIN32
    HMODULE h = NULL;
    struct MyHMODULEs loadedDlls = {0};
    struct MyListOfStrings full_dlls = {0};
#else
    void *h = NULL;
#endif

    // args to main
    myArgs[0] = nob_temp_running_executable_path();
    for (i = 0; i<arg->count; ++i) {
        myArgs[1+i] = nob_temp_strdup(arg->items[i]);
    }

    // prepare in memory c src code
    sbSrc.count = 0;
    if (!PrepareCString(line, pre, first, src, last, &sbSrc)) goto end;
    if (rt!=RT_CC) nob_sb_append_null(&sbSrc);

    if (rt==RT_CC) {
        Nob_Cmd cc = {0};
        // write to inpPath
        if (!nob_write_entire_file(inpPath, sbSrc.items, sbSrc.count)) goto end;

        if (compilerType==COMPILER_UNDECIDED) {
            SetCompilerType();
        }

        nob_da_append(&cc, GetCompiler());
        CompilerSetup(&cc);
        if (werror) {
            TranslateWerror(&cc);
        }
        for (usz i = 0; i<opt->count; ++i) {
            nob_da_append(&cc, opt->items[i]);
        }
        nob_cc_inputs(&cc, inpPath);
        TranslateDllOutput(&cc);
        nob_da_append(&cc, nob_temp_sprintf("-I%s", exePath)); // for nob.h

        bool rslt = TranslateCompile(&cc);
        nob_da_free(cc);
        if (!rslt) goto end;
    } else {
        // prepare quoted options
        sbOpt.count = 0;
        if (werror) {
            nob_sb_append_cstr(&sbOpt, "-Werror ");
        }
        SimpleQuote(opt->items, opt->count, &sbOpt);
        nob_sb_append_null(&sbOpt);
        
        // compile by tcc
        s = tcc_new();
        tcc_set_output_type(s, rt==RT_DLL? TCC_OUTPUT_DLL: TCC_OUTPUT_MEMORY);
        tcc_set_options(s, sbOpt.items);
        tcc_set_lib_path(s, tccPath);
        tcc_add_sysinclude_path(s, incPath);
        tcc_add_library_path(s, libPath);
    #ifndef _WIN32
        tcc_add_library_path(s, tccPath);
    #endif
        tcc_add_include_path(s, exePath); // for nob.h

    #ifdef _WIN32
        if (rt==RT_MEM) {
            if (!WinSearchDlls(opt, &full_dlls)) goto end;
            if (!WinLoadDlls(&full_dlls, &loadedDlls)) goto end;
            if (!WinImportDlls(s, &full_dlls, &loadedDlls)) goto end;
        }
    #endif

        r = tcc_compile_string(s, sbSrc.items);
        if (r==-1) goto end;
    }

    if (rt==RT_DLL) {
        r = tcc_output_file(s, outPath);
        if (r==-1) goto end;
    } else if (rt==RT_MEM) {
        r = tcc_relocate(s);
        if (r==-1) goto end;
    }

    if (rt==RT_MEM) {
        ic_main = tcc_get_symbol(s, "ic_main");
    } else {
    #ifdef _WIN32
        h = LoadLibraryA(outPath);
        ic_main = (void *)GetProcAddress(h, "ic_main");
    #else
        h = dlopen(outPath, RTLD_NOW);
        ic_main = (void *)dlsym(h, "ic_main");
    #endif
    }

    if (ic_main!=NULL) {
        r = ic_main(myArgsLen, myArgs);
    } else {
        nob_log(NOB_ERROR, "%s", "failed to get compiled function");
        r = -1;
    }

end:
    if (h!=NULL) {
    #ifdef _WIN32
        FreeLibrary(h);
    #else
        dlclose(h);
    #endif
    }
#ifdef _WIN32
    for (usz i = 0; i<loadedDlls.count; ++i) {
        FreeLibrary(loadedDlls.items[i]);
    }
    nob_da_free(loadedDlls);
    nob_da_free(full_dlls);
#endif
    if (s!=NULL) tcc_delete(s);
    nob_temp_rewind(mark);

    return r;
}

void AppendLineNum(StrBuilder *sb, usz line)
{
    size_t mark = nob_temp_save();
    char *buf = nob_temp_sprintf("#line %zu \"<string>\"\n", line);
    nob_sb_append_cstr(sb, buf);
    nob_temp_rewind(mark);
}

void AppendTiming(StrBuilder *first, StrBuilder *last, usz line, bool once, StrBuilder *reps, StrBuilder *out)
{
    first->count = 0;
    last->count = 0;
    AppendLineNum(last, 1+line);
#ifdef _WIN32
    nob_sb_append_cstr(first, "#include <windows.h>\n");
#else
    nob_sb_append_cstr(first, "#include <time.h>\n");
#endif
    nob_sb_append_cstr(first, 
        "static void __icPrintTime(double ns) {"
        "if      (ns<1e3) printf(\"%.5gns\\n\", ns);"
        "else if (ns<1e6) printf(\"%.4gus\\n\", ns/1e3);"
        "else if (ns<1e9) printf(\"%.4gms\\n\", ns/1e6);"
        "else             printf(\"%.5gs\\n\", ns/1e9);}\n");
#ifdef _WIN32
    nob_sb_append_cstr(last, "LARGE_INTEGER __icFreq, __icStart, __icEnd;\n");
    nob_sb_append_cstr(last, "QueryPerformanceFrequency(&__icFreq);\n");
    nob_sb_append_cstr(last, "QueryPerformanceCounter(&__icStart);\n");
#else
    nob_sb_append_cstr(last, "struct timespec __icStart, __icEnd;\n");
    nob_sb_append_cstr(last, "clock_gettime(CLOCK_MONOTONIC, &__icStart);\n");
#endif
    if (!once) {
        nob_sb_append_cstr(last, "uint64_t __icReps = (");
        nob_sb_append_buf(last, reps->items, reps->count);
        nob_sb_append_cstr(last, ");\n");
        nob_sb_append_cstr(last, "for (uint64_t __icI = 0; __icI<__icReps; ++__icI) {\n");
    }
    nob_sb_append_buf(last, out->items, out->count);
    if (!once) {
        nob_sb_append_cstr(last, "}\n");
    }
#ifdef _WIN32
    nob_sb_append_cstr(last, "QueryPerformanceCounter(&__icEnd);\n");
    nob_sb_append_cstr(last, "double __icTimeNs = "
        "1e9 * (__icEnd.QuadPart - __icStart.QuadPart) / __icFreq.QuadPart;\n");
#else
    nob_sb_append_cstr(last, "clock_gettime(CLOCK_MONOTONIC, &__icEnd);\n");
    nob_sb_append_cstr(last, "double __icTimeNs = "
        "1e9 * (__icEnd.tv_sec - __icStart.tv_sec) + (__icEnd.tv_nsec - __icStart.tv_nsec);\n");
#endif
    nob_sb_append_cstr(last, "printf(\"Elapsed time: \");__icPrintTime(__icTimeNs);\n");
    if (!once) {
        nob_sb_append_cstr(last, "printf(\"Average time: \");__icPrintTime(__icTimeNs/__icReps);\n");
    }
}

void Help(void)
{
    printf("%s",
        "Commands:\n"
        CMD_SIGN"h      -- show this help message\n"
        CMD_SIGN"q      -- quit\n"
        CMD_SIGN"l      -- list recorded code\n"
        CMD_SIGN"c      -- clear recorded code\n"
        CMD_SIGN"o      -- list current compiler options\n"
        CMD_SIGN"o[...] -- append new compiler options\n"
        CMD_SIGN"O      -- clear compiler options\n"
        CMD_SIGN"a      -- list current arguments\n"
        CMD_SIGN"a[...] -- append new arguments\n"
        CMD_SIGN"A      -- clear arguments\n"
        CMD_SIGN"p expr -- print a struct or array\n"
        CMD_SIGN"P x,sz -- print memory x with size sz\n"
        CMD_SIGN"t[:n]  -- time the following statement\n"
        CMD_SIGN"f      -- start a top level statement\n"
        CMD_SIGN"m expr -- print out expanded macros\n"
        CMD_SIGN";      -- rerun the recorded code\n"
        CMD_SIGN"r[mdc] -- run as memory (m), dll (d) or use cc (c)\n"
        CMD_SIGN"w      -- warnings as errors (default)\n"
        CMD_SIGN"W      -- warnings not as errors\n"
        SHL_SIGN"[...]  -- execute shell command\n"
        CPP_SIGN"[...]  -- C preprocessor\n"
        "Macros:\n"
        "  ONCE_LINE  -- true only the first time on the line\n"
        "  ONCE       -- execute the following statement only once\n"
        "  PRINT(X)   -- print the value of X\n"
        "  WPRINT(X)  -- print the value of X (wchar_t related)\n"
        "  BIN(X)     -- get binary representation of integer X\n"
    );
}

void CompleteFunc(char const *buf, int cursorPos, mlCompletions *comp, void *userdata)
{
    static char const *const cpp[] = {
        "define", "undef", "if", "ifdef", "ifndef", "else", "elif", "endif",
        "include", "embed", "line", "error", "pragma",
    };

    static char const *const keywords[] = {
        "if", "else", 
        "for", "do", "while",
        "switch", "case", "default", 
        "return", "goto", "auto",
    };

    static char const *const stmtLike[] = {
        "break", "continue",
    };

    static char const *const typeLike[] = {
        "int", "char", "float", "double", "void", "short", "long", "signed",
        "unsigned", "struct", "union", "enum", "const", "volatile", "static",
        "extern", "register", "inline", "restrict",
        // C11
        "bool",
        // known types
        "size_t", "ssize_t", "wchar_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        // stdint.h
        "intmax_t", "intptr_t", "uintmax_t", "uintptr_t",
        // defined by me
        "ONCE",
    };

    static char const *const funcLike[] = {
        // standard functions
        "printf", "scanf", "malloc", "free", "realloc", "calloc",
        "memcpy", "memset", "puts", "putchar",
        "atoi", "atol", "atoll", "strtol", "strtoll",
        "strtoul", "strtoull", "strcpy", "strncpy", "strlen",
        // math functions
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "exp", "pow", "log", "sqrt", "cbrt", "lgamma", "tgamma",
        "abs", "labs", "llabs", "fabs", "fmod", "fmin", "fmax",
        "ceil", "floor", "round", "trunc",
        // inttypes.h
        "INT8_C", "INT16_C", "INT32_C", "INT64_C",
        "UINT8_C", "UINT16_C", "UINT32_C", "UINT64_C",
        // others
        "sizeof", "alignof", "alignas",
        // defined by me
        "PRINT", "BIN", "WPRINT",
    };

    static char const *const constLike[] = {
        "NULL", "true", "false",
        "SIZE_MAX", "WCHAR_MAX", "WCHAR_MIN",
        "CHAR_BIT", "SCHAR_MAX", "SCHAR_MIN", "UCHAR_MAX",
        // inttypes.h
        "INT8_MAX", "INT16_MAX", "INT32_MAX", "INT64_MAX",
        "INT8_MIN", "INT16_MIN", "INT32_MIN", "INT64_MIN",
        "UINT8_MAX", "UINT16_MAX", "UINT32_MAX", "UINT64_MAX",
        "PRId8", "PRId16", "PRId32", "PRId64", "PRIdPTR",
        "PRIu8", "PRIu16", "PRIu32", "PRIu64", "PRIuPTR",
        "PRIx8", "PRIx16", "PRIx32", "PRIx64", "PRIxPTR",
        "SCNd8", "SCNd16", "SCNd32", "SCNd64", "SCNdPTR",
        "SCNu8", "SCNu16", "SCNu32", "SCNu64", "SCNuPTR",
        "SCNx8", "SCNx16", "SCNx32", "SCNx64", "SCNxPTR",
        // float.h
        "FLT_MAX", "FLT_MIN", "DBL_MAX", "DBL_MIN", "LDBL_MAX", "LDBL_MIN",
        "FLT_EPSILON", "DBL_EPSILON", "LDBL_EPSILON",
    };

    (void) userdata;

    if (buf[0]==CMD_SIGN[0] && cursorPos==1) {
        mlSetCompletionStart(comp, 1);
        static char const cmds[] = "qhflpPmt;";
        char s[2] = {0};
        for (usz i = 0; i<sizeof(cmds)-1; ++i) {
            s[0] = cmds[i];
            mlAddCompletion(comp, s, s);
        }
        return;
    }

    int start;
    for (start = cursorPos-1; start>=0; --start) {
        char c = buf[start];
        if (isalnum(c) || c=='_') { continue; }
        break;
    }
    start += 1;
    mlSetCompletionStart(comp, start);

    int count = cursorPos - start;
    if (count==0) return;

    size_t mark = nob_temp_save();
    #define FIND_IN_LIST(LIST, FMT) \
        for (usz i = 0; i<sizeof(LIST)/sizeof(*LIST); ++i) { \
            if (strncmp(buf+start, LIST[i], count)==0) { \
                char *replacement = nob_temp_sprintf(FMT, LIST[i]); \
                mlAddCompletion(comp, replacement, LIST[i]); \
            } \
        }
    if (TrimPrefixSv(nob_sv_from_cstr(buf), CPP_SIGN)) {
        FIND_IN_LIST(cpp, "%s ")
    } else {
        FIND_IN_LIST(keywords, "%s ")
        FIND_IN_LIST(stmtLike, "%s;")
        FIND_IN_LIST(typeLike, "%s ")
        FIND_IN_LIST(funcLike, "%s(")
        FIND_IN_LIST(constLike, "%s")
    }
    nob_temp_rewind(mark);
}


int main(int argc, char **argv)
{
    int ok, argStart = 0;
    usz line = 0;
    StrBuilder out = {0}, pre = {0}, first = {0}, 
        src = {0}, last = {0}, temp = {0}, tempCc = {0};
    Nob_Cmd opt = {0}, arg = {0};
    RunType rt = RT_MEM;
    bool werror = true;

    SetupPaths();
    if (!nob_mkdir_if_not_exists(tempDir)) return 1;

    mlSetCompletionMode(mlCompleteMode_Circular);
    mlSetCompletionCallback(CompleteFunc, NULL);

    for (int i = 1; i<argc; ++i) {
        char *a = argv[i];
        if (strcmp(a, "-h")==0 || strcmp(a, "--help")==0) {
            printf(
                "Usage: %s [dll | cc[=...]] [compiler options...] [-- program argv...]\n"
                , argv[0]
            );
            return 0;
        }
        if (strcmp(a, "dll")==0) {
            rt = RT_DLL;
            continue;
        }
        if (strcmp(a, "cc")==0) {
            rt = RT_CC;
            continue;
        }
        if (strncmp(a, "cc=", 3)==0) {
            myCompiler = a+3;
            rt = RT_CC;
            continue;
        }
        if (strcmp(a, "--")==0) argStart = 1;
        else if (argStart) nob_da_append(&arg, a);
        else nob_da_append(&opt, a);
    }

    puts("Type \""CMD_SIGN"h\" for help");
    for (;;) {
        usz outLine = line;
        enum InputKind kind = GetInput(&out, &outLine, false, false);
        enum InputKind kind2;
        bool once = false;
        if (kind==Empty) {
            continue;
        } else if (kind==InputEnd) {
            break;
        } else if (kind==Shell) {
            SpawnShell(out.items+1, out.count-1);
        } else if (kind==Cmd) {
            switch (out.items[1]) {
            default:
                if (isspace(out.items[1])) {
                    Help();
                    continue;
                }
                printf("Unknown command \"%.*s\"\n", (int)out.count-1, out.items);
            break; case 'h':
                Help();
            break; case 'q':
                goto endloop;
            break; case 'l':
                printf("/* top */\n");
                printf("%.*s", (int)pre.count, pre.items);
                printf("/* main */\n");
                printf("%.*s", (int)src.count, src.items);
            break; case 'c':
                pre.count = 0;
                src.count = 0;
                line = 0;
            break; case 'A':
                arg.count = 0;
                puts("cleared arguments");
            break; case 'a':
                if (out.count-1>2) {
                    ParseShell(out.items+2, out.count-2, &arg);
                }
                printf("current arguments:");
                for (usz i = 0; i<arg.count; ++i) {
                    printf(" '%s'", arg.items[i]);
                }
                puts("");
            break; case 'O':
                opt.count = 0;
                puts("cleared options");
            break; case 'o':
                if (out.count-1>2) {
                    ParseShell(out.items+2, out.count-2, &opt);
                }
                printf("current options:");
                for (usz i = 0; i<opt.count; ++i) {
                    printf(" '%s'", opt.items[i]);
                }
                puts("");
            break; case 'p':
                if (out.count-1>2) {
                    first.count = 0;
                    last.count = 0;
                    AppendLineNum(&last, 1+line);
                    nob_sb_append_cstr(&last, "__printmem(&(");
                    nob_sb_append_buf(&last, out.items+2, out.count-3);
                    nob_sb_append_cstr(&last, "),sizeof(");
                    nob_sb_append_buf(&last, out.items+2, out.count-3);
                    nob_sb_append_cstr(&last, "));\n");
                    goto run_label;
                }
            break; case 'P':
                if (out.count-1>2) {
                    first.count = 0;
                    last.count = 0;
                    AppendLineNum(&last, 1+line);
                    nob_sb_append_cstr(&last, "__printmem(");
                    nob_sb_append_buf(&last, out.items+2, out.count-3);
                    nob_sb_append_cstr(&last, ");\n");
                    goto run_label;
                }
            break; case 't':
                if (out.count-1>2 && out.items[2]==':') {
                    // time multiple
                    temp.count = 0;
                    nob_sb_append_buf(&temp, out.items+3, out.count-4);
                } else {
                    // time once
                    once = true;
                }
                outLine = line;
                kind2 = GetInput(&out, &outLine, false, true);
                if (kind2==Expr) {
                    nob_sb_append_cstr(&out, ";");
                } else if (kind2!=Stmt) {
                    printf("Expected statement or expression after \""CMD_SIGN"t\"\n");
                    continue;
                }
                AppendTiming(&first, &last, line, once, &temp, &out);
                goto run_label;
            break; case 'f':
                outLine = line;
                kind2 = GetInput(&out, &outLine, true, false);
                if (kind2==Stmt) {
                    kind = Pre;
                    goto prep_label;
                } else {
                    printf("Expected statement after \""CMD_SIGN"f\"\n");
                    continue;
                }
            break; case 'm':
                if (out.count-1>2) {
                    first.count = 0;
                    last.count = 0;
                    AppendLineNum(&last, 1+line);
                    nob_sb_append_cstr(&last, "printf(\"%s\\n\", __IC_STRINGIFY(");
                    nob_sb_append_buf(&last, out.items+2, out.count-3);
                    nob_sb_append_cstr(&last, "));\n");
                    goto run_label;
                }
            break; case ';':
                // reload last
                goto run_label;
            break; case 'r':
                switch (out.items[2]) {
                default:
                break; case 'c': rt = RT_CC;
                    compilerType = COMPILER_UNDECIDED; // reset compiler type
                    if (out.items[3]=='=') {
                        if (out.items[4]=='\n') {
                            myCompiler = NULL;
                        } else {
                            Nob_String_View sv = nob_sv_trim(
                                nob_sv_from_parts(out.items+4, out.count-4));
                            tempCc.count = 0;
                            nob_sb_append_buf(&tempCc, sv.data, sv.count);
                            nob_sb_append_null(&tempCc);
                            myCompiler = tempCc.items;
                        }
                    }
                break; case 'd': rt = RT_DLL;
                break; case 'm': rt = RT_MEM;
                }
                printf("run type: %s, ",
                    rt==RT_CC? "cc":
                    rt==RT_DLL? "dll":
                    "mem");
                printf("compiler: %s\n",
                    rt==RT_CC? GetCompiler():
                    "tcc");
            break; case 'w':
                werror = true;
                printf("warnings as errors: on\n");
            break; case 'W':
                werror = false;
                printf("warnings as errors: off\n");
            }
        } else {
        prep_label:
            last.count = 0;
            if (kind==Stmt) {
                AppendLineNum(&last, 1+line);
                nob_sb_append_buf(&last, out.items, out.count);
            } else if (kind==Expr) {
                AppendLineNum(&last, 1+line);
                nob_sb_append_cstr(&last, "PRINT((");
                nob_sb_append_buf(&last, out.items, out.count-1);
                nob_sb_append_cstr(&last, "));\n");
            }

            first.count = 0;
            if (kind==Pre) {
                AppendLineNum(&first, 1+line);
                nob_sb_append_buf(&first, out.items, out.count);
            }
        run_label:
            ok = Run(rt, line,
                &opt, &arg,
                &pre, &first,
                &src, &last,
                werror
            ) >= 0;
            if (ok) {
                if (kind==Stmt) {
                    line = outLine;
                    nob_sb_append_buf(&src, last.items, last.count);
                } else if (kind==Pre) {
                    line = outLine;
                    nob_sb_append_buf(&pre, first.items, first.count);
                }
            }
        }
    }
endloop:
    return 0;
}
