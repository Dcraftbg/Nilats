#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);
    Cmd cmd = { 0 };
    const char* cc = getenv_or_default("CC", DEFAULT_CC);
    const char* bindir = getenv_or_default("BINDIR", "bin");
    char* program_name = shift_args(&argc,&argv);

    bool gdb = false;
    bool run = false;
    File_Paths args_to_pass = {0};

    bool collecting_args = false;
    while(argc){
        char* arg = shift_args(&argc,&argv);
        if(collecting_args) da_append(&args_to_pass, arg);
        else if(strcmp(arg, "run") == 0) run = true;
        else if(strcmp(arg, "--") == 0) collecting_args = true;
        else if(strcmp(arg, "-gdb") == 0) gdb = true;
        else {
            nob_log(NOB_ERROR, "Unknown argument: %s", arg);
            return 1;
        }
    }

    nob_minimal_log_level = NOB_WARNING;
    if(!mkdir_if_not_exists(bindir)) return 1;
    if(!mkdir_if_not_exists(temp_sprintf("%s/nilats", bindir))) return 1;
    nob_minimal_log_level = NOB_INFO;
    
    File_Paths dirs = { 0 }, c_sources = { 0 };
    const char* src_dir = "src";
    size_t src_prefix_len = strlen(src_dir)+1;
    if(!walk_directory("src",
            { &dirs, .file_type = FILE_DIRECTORY },
            { &c_sources, .ext = ".c" },
       )) return 1;
    for(size_t i = 0; i < dirs.count; ++i) {
        nob_minimal_log_level = NOB_WARNING;
        if(!mkdir_if_not_exists(temp_sprintf("%s/nilats/%s", bindir, dirs.items[i] + src_prefix_len))) return 1;
        nob_minimal_log_level = NOB_INFO;
    }
    File_Paths objs = { 0 };
    String_Builder stb = { 0 };
    File_Paths pathb = { 0 };
    for(size_t i = 0; i < c_sources.count; ++i) {
        const char* src = c_sources.items[i];
        const char* out = temp_sprintf("%s/nilats/%.*s.o", bindir, (int)(strlen(src + src_prefix_len)-2), src + src_prefix_len);
        da_append(&objs, out);
        if(!nob_c_needs_rebuild1(&stb, &pathb, out, src)) continue;
        // C compiler
        cmd_append(&cmd, cc);
        // Warnings
        cmd_append(&cmd,
            "-Wall",
            "-Wextra",
            "-Wno-unused-function",
#ifdef _WIN32
            "-D_CRT_SECURE_NO_WARNINGS", "-Wno-deprecated-declarations",
#endif
        );
        // Actual compilation
        cmd_append(&cmd,
            "-MP", "-MMD", "-O1", "-g", "-c",
            "-I", "include",
            src,
            "-o", out,
        );
        if(!cmd_run_sync_and_reset(&cmd)) return 1;
    }
    const char* exe = temp_sprintf("%s/nilats/nilats" EXE_SUFFIX, bindir);
    if(needs_rebuild(exe, objs.items, objs.count)) {
        cmd_append(&cmd, cc, "-o", exe);
        da_append_many(&cmd, objs.items, objs.count);
        cmd_append(&cmd, "-L.", "-ldiscord", "-lcurl");
        if(!cmd_run_sync_and_reset(&cmd)) return 1;
    }

    if(run){
        const char* exe_path = temp_sprintf("./%s", exe);
        if(gdb) cmd_append(&cmd, "gdb", exe_path, "--args");
        cmd_append(&cmd, exe_path);
        da_append_many(&cmd, args_to_pass.items, args_to_pass.count);
        if(!cmd_run_sync_and_reset(&cmd)) return 1;
    }
}

