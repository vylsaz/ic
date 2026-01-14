#define NOB_IMPLEMENTATION
#include "nob.h"

#ifdef _WIN32
#   define TCC_DIR "./tinycc/win32"
#   define TCC_LIB "./libtcc.dll"
#else
#   define TCC_DIR "./tinycc"
#   ifdef __ANDROID__
#       define TCC_LIB "./libtcc.so"
#   else
#       define TCC_LIB "./libtcc.a"
#   endif
#endif

void download_file(char const *url, char const *out_path)
{
    Nob_Cmd cmd = {0};
#ifdef _WIN32
    cmd_append(&cmd, "Powershell.exe", "-Command",
        nob_temp_sprintf("Invoke-WebRequest \"%s\" -OutFile '%s'", url, out_path));
#else
    cmd_append(&cmd, "curl", "-L", "-o", out_path, url);
#endif
    if (!cmd_run(&cmd)) {
        nob_log(NOB_ERROR, "Failed to download %s", url);
    }

    da_free(cmd);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "update-nob") == 0) {
        download_file(
            "https://raw.githubusercontent.com/tsoding/nob.h/main/nob.h",
            "./nob.h");
        argc -= 1;
    }

    GO_REBUILD_URSELF_PLUS(argc, argv, "./nob.h");

    Cmd cmd = {0};

    if (!file_exists("./tinycc")) {
        cmd_append(&cmd, "git", "clone", "git://repo.or.cz/tinycc.git");
        if (!cmd_run(&cmd)) return 1;
    }
    
    int rebuild_is_needed = 0;
    if (!file_exists(TCC_LIB)) {
        rebuild_is_needed = 1;
    }

    if (rebuild_is_needed) {
        #ifdef _WIN32
        set_current_dir("./tinycc/win32");
        cmd_append(&cmd, "cmd.exe", "/c", ".\\build-tcc.bat");
        if (!cmd_run(&cmd)) return 1;
        set_current_dir("../..");
        #else
        set_current_dir("./tinycc");
        cmd_append(&cmd, "./configure");
        if (!cmd_run(&cmd)) return 1;
        cmd_append(&cmd, "make");
        if (!cmd_run(&cmd)) return 1;
        set_current_dir("..");
        #endif

        copy_file(TCC_DIR"/"TCC_LIB, TCC_LIB);
        copy_directory_recursively(TCC_DIR"/lib", "./lib");
        copy_directory_recursively(TCC_DIR"/include", "./include");
        #ifdef _WIN32
        copy_directory_recursively(TCC_DIR"/libtcc", "./libtcc");
        #else
        mkdir_if_not_exists("./libtcc");
        copy_file(TCC_DIR"/libtcc.h", "./libtcc/libtcc.h");
        copy_file(TCC_DIR"/runmain.o", "./runmain.o");
        copy_file(TCC_DIR"/libtcc1.a", "./libtcc1.a");
        #endif
    }

    nob_cc(&cmd);
    nob_cc_flags(&cmd);
    nob_cc_output(&cmd, "ic");
    nob_cc_inputs(&cmd, "./ic.c");
    cmd_append(&cmd, "-L.", "-ltcc", "-lm");
#ifdef __ANDROID__
    cmd_append(&cmd, temp_sprintf("-Wl,-R%s", get_current_dir_temp()));
#endif

    if (!cmd_run(&cmd)) return 1;

    // if (!mkdir_if_not_exists("./temp")) return 1;

    return 0;
}
