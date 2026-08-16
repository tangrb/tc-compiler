/*
 * main.c — tc-aot 命令行入口：TC → C99 转译
 *
 * 读取 .tc 文件，经 libtc 编译后再转译为等价的 C99 源码。
 * 可选编译并运行生成的 C 代码（依赖 host cc）。
 *
 * 用法：
 *   tc-aot [options] <file.tc>
 *
 * 选项：
 *   -o, --output FILE     输出路径（默认 <input>.c）
 *   -H, --header FILE     生成嵌入模式头文件（仅在 --embed 时有效）
 *   -c, --check           仅静态分析
 *   -r, --run             编译并运行生成的 C
 *   -I, --include PATH    添加模块搜索路径
 *   -e, --embed           嵌入库模式（不生成 main()，生成非 static 符号 + 函数表）
 *   -h, --help            显示帮助
 *   -V, --version         显示版本号
 *
 * 退出码：0 成功，1 失败。
 */
#include "tc_aot_codegen.h"
#include "tc_diagnostic.h"
#include "tc_io.h"
#include "tc_lib.h"
#include "tc_version.h"
#include "tc_warning.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif

#define TC_AOT_MAX_INCLUDE_PATHS 64

static void tc_aot_print_usage(const char *program) {
    fprintf(stderr,
            "Usage: %s [options] <file.tc>\n"
            "\n"
            "TC ahead-of-time compiler (TC → C99).\n"
            "\n"
            "Options:\n"
            "  -o, --output FILE      write generated C to FILE (default: <input>.c)\n"
            "  -H, --header FILE      write embed header to FILE (requires --embed)\n"
            "  -c, --check            static analysis only, do not emit C\n"
            "  -r, --run              compile and run generated C (requires host C compiler)\n"
            "  -I, --include <path>   add module search path (repeatable)\n"
            "  -e, --embed            embed library mode (no main(), public symbols + func table)\n"
            "  -h, --help             show this help\n"
            "  -V, --version          show version\n"
            "\n"
            "Notes:\n"
            "  --check uses the same libtc batch-language acceptance set as tc-vm --check.\n",
            program);
}

/** 根据输入路径构造默认输出路径（.tc → .c） */
static char *tc_aot_default_output_path(const char *input_path) {
    size_t len = strlen(input_path);
    char *out = (char *)malloc(len + 3);
    if (!out) {
        return NULL;
    }
    strcpy(out, input_path);
    if (len >= 3 && strcmp(input_path + len - 3, ".tc") == 0) {
        out[len - 3] = '\0';
    }
    strcat(out, ".c");
    return out;
}

/** 编译并运行生成的 C 代码（需 host C 编译器）。
 * 非 Windows（glibc / macOS）：拼一条 system() 命令编译 + 运行。
 * Windows（MinGW / msys2）：改用 _spawnv 直接以 argv 数组启动宿主 cc
 * 与生成的 exe，绕开 cmd.exe / bash 的命令行词法解析（实测 msys2 的
 * system() 对含多路径 / 引号 / && 的长 gcc 命令解析不稳定）。 */
static int tc_aot_run_generated(const char *c_path) {
    char cmd[8192];
    const char *cc;
    const char *exe_name;
    int n;
#ifdef TC_AOT_HAVE_FENV
    const char *fenv_flag = "-DTC_HAVE_FENV=1 ";
#else
    const char *fenv_flag = "";
#endif
#if defined(__APPLE__)
    /* Apple libSystem 内含 libm */
    const char *libm_flag = "";
#else
    /* glibc/MinGW 需要显式 -lm（tc_sem_cast.c 使用 trunc 等） */
    const char *libm_flag = "-lm ";  /* 尾随空格，避免与后续 -o 粘连 */
#endif

    cc = getenv("CC");
    if (cc == NULL || cc[0] == '\0') {
        cc = "cc";
    }
#ifdef _WIN32
    exe_name = ".exe";
#else
    exe_name = "";
#endif

#ifdef _WIN32
    {
        /*
         * Windows：直接拼 `cmd && cmd` 传给 msys2 的 system() 会在 cmd.exe
         * 词法层失败（实测报"文件名、目录名或卷标语法不正确"）。改用最
         * 经典的方案：把编译＋运行命令写入临时 .bat 文件，再让 system()
         * 执行该 .bat。cmd.exe 对 .bat 文件内容的词法解析与参数模式一致、
         * 引号完整保留，是 Windows 上执行含多路径/&& 命令的可靠出路。
         */
        char bat_path[MAX_PATH > 0 ? MAX_PATH : 4096];
        char bat_cmd[8192];
        char *bpath;
        FILE *bf = NULL;
        int rc;

        (void)snprintf(bat_path, sizeof(bat_path),
                       "%s.out.bat", c_path);
        bf = fopen(bat_path, "wb");
        if (!bf) {
            return -1;
        }
        /* 两条命令分行：编译成功后执行生成的 exe。
         * 注意：%errorlevel% 需写成 %%errorlevel%%（snprintf 把 %% → %，
         * 且 %%e 不触发浮点格式符），使 .bat 最后一行真实返回子进程码。 */
        (void)snprintf(bat_cmd, sizeof(bat_cmd),
            "@echo off\r\n"
            "\"%s\" -std=c99 -Wall -Wextra -Werror -pedantic "
            "-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE "
            "%s"
            "-I\"%s\" -I\"%s/runtime\" "
            "\"%s\" \"%s/tc_aot_rt.c\" "
            "\"%s/runtime/tc_types.c\" "
            "\"%s/runtime/tc_diagnostic.c\" "
            "\"%s/runtime/tc_semantics.c\" "
            "\"%s/runtime/tc_sem_int.c\" "
            "\"%s/runtime/tc_sem_fp.c\" "
            "\"%s/runtime/tc_sem_cast.c\" "
            "\"%s/runtime/tc_sem_bitwise.c\" "
            "\"%s/runtime/tc_io.c\" "
            "%s"
            "-o \"%s.out%s\"\r\n"
            "if errorlevel 1 exit /b 1\r\n"
            "\"%s.out%s\"\r\n"
            "exit /b %%errorlevel%%\r\n",
            cc, fenv_flag,
            TC_AOT_RT_DIR, TC_VM_DIR,
            c_path, TC_AOT_RT_DIR,
            TC_VM_DIR, TC_VM_DIR, TC_VM_DIR, TC_VM_DIR,
            TC_VM_DIR, TC_VM_DIR, TC_VM_DIR, TC_VM_DIR,
            libm_flag,
            c_path, exe_name,
            c_path, exe_name);
        if (fwrite(bat_cmd, 1, strlen(bat_cmd), bf) != strlen(bat_cmd)) {
            fclose(bf);
            (void)remove(bat_path);
            return -1;
        }
        fclose(bf);

        if (getenv("TC_AOT_DEBUG_CMD") != NULL) {
            fprintf(stderr, "tc-aot .bat: %s\n%s\n", bat_path, bat_cmd);
        }
        bpath = (char *)malloc(strlen(bat_path) + 1);
        if (!bpath) {
            (void)remove(bat_path);
            return -1;
        }
        strcpy(bpath, bat_path);
        rc = system(bpath);
        free(bpath);
        (void)remove(bat_path);
        return rc == 0 ? 0 : -1;
    }
#else
    n = snprintf(cmd, sizeof(cmd),
             "\"%s\" -std=c99 -Wall -Wextra -Werror -pedantic "
             /* 特征宏：宿主 cc 编译 runtime 时，glibc/MinGW 严格 ANSI 下
              * 会隐藏 strdup/strndup 等声明（与主构建同因），必须显式带上 */
             "-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE "
             "%s"
             "-I\"" TC_AOT_RT_DIR "\" -I\"" TC_VM_DIR "/runtime\" "
             "\"%s\" \"" TC_AOT_RT_DIR "/tc_aot_rt.c\" "
             "\"" TC_VM_DIR "/runtime/tc_types.c\" "
             "\"" TC_VM_DIR "/runtime/tc_diagnostic.c\" "
             "\"" TC_VM_DIR "/runtime/tc_semantics.c\" "
             "\"" TC_VM_DIR "/runtime/tc_sem_int.c\" "
             "\"" TC_VM_DIR "/runtime/tc_sem_fp.c\" "
             "\"" TC_VM_DIR "/runtime/tc_sem_cast.c\" "
             "\"" TC_VM_DIR "/runtime/tc_sem_bitwise.c\" "
             "\"" TC_VM_DIR "/runtime/tc_io.c\" "
             /* -lm 必须位于对象文件之后（GNU ld 单遍扫描） */
             "%s"
             "-o \"%s.out%s\" && \"%s.out%s\"",
             cc, fenv_flag, c_path, libm_flag, c_path, exe_name, c_path, exe_name);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        return -1;
    }
    if (getenv("TC_AOT_DEBUG_CMD") != NULL) {
        fprintf(stderr, "tc-aot host cc cmd: %s\n", cmd);
    }
    return system(cmd);
#endif
}

int main(int argc, char **argv) {
    tc_io_init();
    static const struct option longopts[] = {
        {"output", required_argument, NULL, 'o'},
        {"header", required_argument, NULL, 'H'},
        {"check", no_argument, NULL, 'c'},
        {"run", no_argument, NULL, 'r'},
        {"include", required_argument, NULL, 'I'},
        {"embed", no_argument, NULL, 'e'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };

    const char *input_path = NULL;
    const char *output_path = NULL;
    const char *header_path = NULL;
    char *owned_output_path = NULL;
    char *include_paths[TC_AOT_MAX_INCLUDE_PATHS];
    size_t include_count = 0;
    int check_only = 0;
    int run_mode = 0;
    int embed_mode = 0;
    TcDiagnostic diag;
    TcTypedProgram program;
    FILE *out_file = NULL;
    FILE *header_file = NULL;
    int opt;
    int rc = 0;

    while ((opt = getopt_long(argc, argv, "o:H:crI:ehV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'o':
            output_path = optarg;
            break;
        case 'H':
            header_path = optarg;
            break;
        case 'c':
            check_only = 1;
            break;
        case 'r':
            run_mode = 1;
            break;
        case 'I':
            if (include_count >= TC_AOT_MAX_INCLUDE_PATHS) {
                fprintf(stderr, "%s: too many -I paths (max %d)\n", argv[0],
                        TC_AOT_MAX_INCLUDE_PATHS);
                tc_aot_print_usage(argv[0]);
                return 1;
            }
            include_paths[include_count++] = optarg;
            break;
        case 'e':
            embed_mode = 1;
            break;
        case 'h':
            tc_aot_print_usage(argv[0]);
            return 0;
        case 'V':
            printf("tc-aot %s\n", TC_AOT_VERSION);
            return 0;
        default:
            tc_aot_print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing input file\n", argv[0]);
        tc_aot_print_usage(argv[0]);
        return 1;
    }
    if (optind + 1 < argc) {
        fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[optind + 1]);
        return 1;
    }

    input_path = argv[optind];

    /* 校验：--embed + -r 不兼容 */
    if (embed_mode && run_mode) {
        fprintf(stderr, "%s: --embed and --run are mutually exclusive\n", argv[0]);
        return 1;
    }
    /* 校验：-H 需要 --embed */
    if (header_path && !embed_mode) {
        fprintf(stderr, "%s: --header requires --embed\n", argv[0]);
        return 1;
    }

    tc_diagnostic_init(&diag);

    if (include_count > 0) {
        if (tc_set_module_search_paths(include_paths, include_count, &diag) != 0) {
            tc_diagnostic_print(&diag, stderr);
            tc_diagnostic_clear(&diag);
            return 1;
        }
    }

    /* 编译 */
    if (tc_compile_file(input_path, &program, &diag) != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (program.warnings.count > 0) {
        tc_warning_list_print(&program.warnings, stderr);
    }

    if (check_only) {
        tc_typed_program_free(&program);
        tc_diagnostic_clear(&diag);
        return 0;
    }

    /* 确定输出路径 */
    if (!output_path) {
        owned_output_path = tc_aot_default_output_path(input_path);
        if (!owned_output_path) {
            fprintf(stderr, "%s: memory allocation failed\n", argv[0]);
            tc_typed_program_free(&program);
            tc_diagnostic_clear(&diag);
            return 1;
        }
        output_path = owned_output_path;
    }

    /* 代码生成 */
    out_file = fopen(output_path, "w");
    if (!out_file) {
        fprintf(stderr, "%s: cannot open output file '%s'\n", argv[0], output_path);
        free(owned_output_path);
        tc_typed_program_free(&program);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (tc_aot_emit_c(out_file, &program, input_path, embed_mode) != 0) {
        fprintf(stderr, "%s: code generation failed\n", argv[0]);
        fclose(out_file);
        free(owned_output_path);
        tc_typed_program_free(&program);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    fclose(out_file);

    /* 嵌入模式头文件 */
    if (embed_mode && header_path) {
        header_file = fopen(header_path, "w");
        if (!header_file) {
            fprintf(stderr, "%s: cannot open header file '%s'\n", argv[0], header_path);
            free(owned_output_path);
            tc_typed_program_free(&program);
            tc_diagnostic_clear(&diag);
            return 1;
        }
        if (tc_aot_emit_embed_header(header_file, &program, input_path) != 0) {
            fprintf(stderr, "%s: header generation failed\n", argv[0]);
            fclose(header_file);
            free(owned_output_path);
            tc_typed_program_free(&program);
            tc_diagnostic_clear(&diag);
            return 1;
        }
        fclose(header_file);
    }

    tc_typed_program_free(&program);

    /* 可选：编译并运行 */
    if (run_mode) {
        rc = tc_aot_run_generated(output_path);
        if (rc != 0) {
            fprintf(stderr, "%s: run failed (exit %d)\n", argv[0], rc);
        }
    }

    free(owned_output_path);
    tc_diagnostic_clear(&diag);
    return rc == 0 ? 0 : 1;
}
