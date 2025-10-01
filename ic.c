#include "libtcc/libtcc.h"
#define NOB_IMPLEMENTATION
#include "nob.h"
#include <assert.h>
#include <inttypes.h>
#include <ctype.h>
#ifndef _WIN32
    #include <dlfcn.h>
#endif

typedef uint64_t usz;
typedef Nob_String_Builder StrBuilder;

typedef struct Options {
    char **items;
    usz capacity, count;
} Options;

usz LastPathSep(char const *str, usz len)
{
    usz i;
    for (i = 0; i<len; ++i) {
        char c = str[len-i-1];
        if (c=='\\' || c=='/') return len-i-1;
    }
    return -1;
}

char *GetExe(int *outlen)
{
    char *exe; int len;
#ifdef _WIN32
    assert(_get_pgmptr(&exe)==0);
    len = strlen(exe);
#elif defined(__linux__)
    char buf[1+FILENAME_MAX] = {0};
    len = readlink("/proc/self/exe", buf, sizeof buf);
    assert(len!=-1);
    exe = buf;
#else
#error TODO
#endif
    if (outlen!=NULL) *outlen = len;
    return exe;
}

char *GetExePath(void)
{
    char *exe; int len, sep;
    exe = GetExe(&len);
    sep = LastPathSep(exe, len);
    return nob_temp_sprintf("%.*s", sep, exe);
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
    usz len = 0, cap = 8;
    enum Braces *stack = calloc(cap, sizeof(enum Braces));

    usz i;
    for (i = 0; i<sb->count-1; ++i) {
        char c = sb->items[i];
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
            if (c=='*' && i+1<sb->count && sb->items[i+1]=='/') {
                blCom = 0;
            }
            continue;
        }
        if (c=='(' || c=='[' || c=='{') {
            enum Braces lef = braceFromChar[(int)c];
            if (len+1 > cap) {
                cap *= 2;
                stack = realloc(stack, cap*sizeof(enum Braces));
                assert(stack);
            }
            stack[len] = lef;
            len += 1;
        } else if (c==')' || c==']' || c=='}') {
            enum Braces rig = braceFromChar[(int)c];
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
    if (sgStr || dbStr || lnCom || blCom) {
        r = Incomplete;
    } else if (len!=0) {
        r = Incomplete;
    } else if (nob_da_last(sb)=='\\') {
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
    usz const inpLen = sizeof(inp)-1;

    do {
        if (fgets(inp, inpLen, stdin)==NULL) return 0;
        nob_sb_append_cstr(out, inp);
    } while (nob_da_last(out)!='\n');
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
enum InputKind GetInput(StrBuilder *out, usz *outLine)
{
    usz i, line = *outLine;
    char c;

    out->count = 0;
    for (i = 0;; ++i) {
        printf("%2zu) ", 1+line);
        if (GetLine(out)==0) break;
        line += 1;
        if (IsComplete(out)) break;
    }
    *outLine = line;
    if (out->count==1) return Empty;
    if (out->items[0]==';') return Cmd;
    if (out->items[0]=='#') return Cpp;
    if (out->items[0]==':') return Pre;
    if (out->items[0]=='>') return Shell;
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

void SpawnShell(char const *sh, usz len)
{
    Nob_Cmd cmd = {0};
    size_t mark = nob_temp_save();
    ParseShell(sh, len, &cmd);
    nob_cmd_run(&cmd);
    nob_temp_rewind(mark);
    nob_da_free(cmd);
}

void ErrFunc(void *opaque, const char *msg)
{
    (void) opaque;
    fprintf(stderr, "%s\n", msg);
}

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

void PrepareCString(usz line, StrBuilder *pre, StrBuilder *first,
    StrBuilder *src, StrBuilder *last, StrBuilder *sb)
{
    static char include[] = 
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdbool.h>\n"
        "#include <math.h>\n"
        "#include <inttypes.h>\n"
    ;
    
    static char prolog[] = 
        "#line 1 \"nowhere\"\n"
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
            #ifdef _WIN32
                "long:__printil,unsigned long:__printul,"
            #endif
                "float:__printf32,double:__printf64,"
                "bool:__printb,char:__printc,"
                "char*:__prints,char const*:__printcs,"
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
    #ifdef _WIN32
        "void __printil(long x) {printf(\"%ld = \",x);"
            "if (4==sizeof(long)) printf(\"0x%08\"PRIX32\"\\n\",x);"
            "else printf(\"0x%016\"PRIX64\"\\n\",x);}\n"
        "void __printul(unsigned long x) {printf(\"%lu = \",x);"
            "if (4==sizeof(long)) printf(\"0x%08\"PRIX32\"\\n\",x);"
            "else printf(\"0x%016\"PRIX64\"\\n\",x);}\n"
    #endif
        "void __printf32(double x) {printf(\"%g\\n\",x);}\n"
        "void __printf64(float x) {printf(\"%g\\n\",x);}\n"
        "void __printb(_Bool x) {printf(\"%s\\n\",x?\"true\":\"false\");}\n"
        "void __printc(char x) {printf(\"%c\\n\",x);}\n"
        "void __prints(char *x) {printf(\"%s\\n\",x);}\n"
        "void __printcs(char const*x) {printf(\"%s\\n\",x);}\n"
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

    usz mark = nob_temp_save();
    char *lastline = nob_temp_sprintf("#define LASTLINE %zu\n", line);

    nob_sb_append_cstr(sb, include);
    nob_sb_append_buf(sb, pre->items, pre->count);
    nob_sb_append_buf(sb, first->items, first->count);
    nob_sb_append_cstr(sb, prolog);
    nob_sb_append_cstr(sb, lastline);
    nob_sb_append_buf(sb, src->items, src->count);
    nob_sb_append_buf(sb, last->items, last->count);
    nob_sb_append_cstr(sb, epilog);

    nob_temp_rewind(mark);
}

typedef enum RunType {
    RT_MEM,
    RT_DLL,
    RT_CC,
} RunType;

int Run(RunType rt,
    char const *tccPath, char const *incPath, char const *libPath, usz line,
    Nob_Cmd *opt, Nob_Cmd *arg, 
    StrBuilder *pre, StrBuilder *first,
    StrBuilder *src, StrBuilder *last)
{
    typedef int (*IcMain)(int, char **);
    IcMain ic_main;
    TCCState *s = NULL;
    static StrBuilder sbSrc = {0}, sbOpt = {0};
    int r = -1;
    usz i, mark = nob_temp_save();
    int myArgsLen = 1+arg->count;
    char **myArgs = nob_temp_alloc((myArgsLen)*sizeof(char *));
#ifdef _WIN32
    HMODULE h = NULL;
    char const *outPath = nob_temp_sprintf("%s/ic.dll", GetExePath());
#else
    void *h = NULL;
    char const *outPath = nob_temp_sprintf("%s/ic.so", GetExePath());
#endif

    // args to main
    myArgs[0] = nob_temp_strdup(GetExe(NULL));
    for (i = 0; i<arg->count; ++i) {
        myArgs[1+i] = nob_temp_strdup(arg->items[i]);
    }

    // prepare in memory c src code
    sbSrc.count = 0;
    PrepareCString(line, pre, first, src, last, &sbSrc);
    if (rt!=RT_CC) nob_sb_append_null(&sbSrc);

    if (rt==RT_CC) {
        Nob_Log_Level old = nob_minimal_log_level;
        char const *inpPath = nob_temp_sprintf("%s/_ic.c", GetExePath());
        nob_write_entire_file(inpPath, sbSrc.items, sbSrc.count);

        Nob_Cmd cc = {0};
        nob_da_append(&cc, "cc");
        for (usz i = 0; i<opt->count; ++i) {
            nob_da_append(&cc, opt->items[i]);
        }
        nob_da_append(&cc, "-shared");
        nob_da_append(&cc, "-o");
        nob_da_append(&cc, outPath);
        nob_da_append(&cc, inpPath);
        nob_minimal_log_level = NOB_WARNING;

        if (!nob_cmd_run(&cc)) {
            nob_delete_file(inpPath);
            nob_minimal_log_level = old;
            goto end;
        }
        nob_delete_file(inpPath);
        nob_minimal_log_level = old;
    } else {
        // prepare quoted options
        sbOpt.count = 0;
        SimpleQuote(opt->items, opt->count, &sbOpt);
        nob_sb_append_null(&sbOpt);
        
        // compile by tcc
        s = tcc_new();
        tcc_set_output_type(s, rt==RT_DLL? TCC_OUTPUT_DLL: TCC_OUTPUT_MEMORY);
        tcc_set_options(s, sbOpt.items);
        tcc_set_lib_path(s, tccPath);
        tcc_add_sysinclude_path(s, incPath);
        tcc_add_library_path(s, libPath);
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
    }

end:
    if (h!=NULL) {
    #ifdef _WIN32
        FreeLibrary(h);
    #else
        dlclose(h);
    #endif
    }
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

int main(int argc, char **argv)
{
    int ok, argStart = 0;
    usz line = 0;
    StrBuilder out = {0}, pre = {0}, first = {0}, 
        src = {0}, last = {0};
    Nob_Cmd opt = {0}, arg = {0};
    char const *tccPath = GetExePath();
    char const *incPath = nob_temp_sprintf("%s/include", tccPath);
    char const *libPath = nob_temp_sprintf("%s/lib", tccPath);
    RunType rt = RT_MEM;

    for (int i = 1; i<argc; ++i) {
        char *a = argv[i];
        if (strcmp(a, "-h")==0 || strcmp(a, "--help")==0) {
            printf(
                "Usage: %s [cc | dll] [compiler options...] [-- program argv...]\n"
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
        if (strcmp(a, "--")==0) argStart = 1;
        else if (argStart) nob_da_append(&arg, a);
        else nob_da_append(&opt, a);
    }

    puts("Type \";h\" for help");
    for (;;) {
        usz outLine = line;
        enum InputKind kind = GetInput(&out, &outLine);
        if (kind==Empty) {
            continue;
        } else if (kind==Shell) {
            SpawnShell(out.items+1, out.count-1);
        } else if (kind==Cmd) {
            switch (out.items[1]) {
            default:
                printf("Unknown command \"%.*s\"\n", (int)out.count-1, out.items);
            break; case 'h':
                printf("%s",
                    ";h -- show this help message\n"
                    ";q -- quit\n"
                    ";l -- list recorded code\n"
                    ";c -- clear recorded code\n"
                    ";o      -- list current compiler options\n"
                    ";o[...] -- append new compiler options\n"
                    ";O      -- clear compiler options\n"
                    ";a      -- list current arguments\n"
                    ";a[...] -- append new arguments\n"
                    ";A      -- clear arguments\n"
                    ";p expr -- print a struct or array\n"
                    ";P x,sz -- print memory x with size sz\n"
                    "#[...]  -- C preprocessor\n"
                    ":[...]  -- Statements outside of main\n"
                    ">[...]  -- Execute shell command\n"
                );
            break; case 'q':
                goto endloop;
            break; case 'l':
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
            }
        } else {
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
                nob_sb_append_buf(&first, out.items+1, out.count-1);
            } else if (kind==Cpp) {
                AppendLineNum(&first, 1+line);
                nob_sb_append_buf(&first, out.items, out.count);
            }
        run_label:
            ok = Run(rt,
                tccPath, incPath, libPath, line,
                &opt, &arg,
                &pre, &first,
                &src, &last) >= 0;
            if (ok) {
                if (kind==Stmt) {
                    line = outLine;
                    nob_sb_append_buf(&src, last.items, last.count);
                } else if (kind==Pre || kind==Cpp) {
                    line = outLine;
                    nob_sb_append_buf(&pre, first.items, first.count);
                }
            }
        }
    }
endloop:
    return 0;
}
