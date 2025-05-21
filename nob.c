#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#ifdef _WIN32
#define TCC_DIR "./tinycc/win32"
#define TCC_EXE "./tcc.exe"
#define TCC_LIB "./libtcc.dll"
#else
#define TCC_DIR "./tinycc"
#define TCC_EXE "./tcc"
#define TCC_LIB "./libtcc.a"
#endif


int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Cmd cmd = {0};

    if (!file_exists("./tinycc")) {
        cmd_append(&cmd, "git", "clone", "git://repo.or.cz/tinycc.git");
        if (!cmd_run_sync_and_reset(&cmd)) return 1;
    }
    
    int rebuild_is_needed = 0;
    if (!file_exists(TCC_EXE)) {
        rebuild_is_needed = 1;
    }

    if (rebuild_is_needed) {
        #ifdef _WIN32
        set_current_dir("./tinycc/win32");
        cmd_append(&cmd, "cmd.exe", "/c", ".\\build-tcc.bat");
        if (!cmd_run_sync_and_reset(&cmd)) return 1;
        set_current_dir("../..");
        #else
        set_current_dir("./tinycc");
        cmd_append(&cmd, "./configure");
        if (!cmd_run_sync_and_reset(&cmd)) return 1;
        cmd_append(&cmd, "make");
        if (!cmd_run_sync_and_reset(&cmd)) return 1;
        set_current_dir("..");
        #endif

        copy_file(TCC_DIR"/"TCC_EXE, TCC_EXE);
        copy_file(TCC_DIR"/"TCC_LIB, TCC_LIB);
        
        mkdir_if_not_exists("./libtcc");
        mkdir_if_not_exists("./lib");
        mkdir_if_not_exists("./include");
        copy_directory_recursively(TCC_DIR"/libtcc", "./libtcc");
        copy_directory_recursively(TCC_DIR"/lib", "./lib");
        copy_directory_recursively(TCC_DIR"/include", "./include");
    }

    cmd_append(&cmd, TCC_EXE);
    nob_cc_output(&cmd, "ic.exe");
    nob_cc_inputs(&cmd, "./ic.c");
    cmd_append(&cmd, "-I./include");
    cmd_append(&cmd, "-L./lib");
    cmd_append(&cmd, "-L.");
    cmd_append(&cmd, "-ltcc");
    nob_cc_flags(&cmd);
    if (!cmd_run_sync_and_reset(&cmd)) return 1;

    return 0;
}
