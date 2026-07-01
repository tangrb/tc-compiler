/*
 * main.c — tc-vm 命令行入口
 *
 * TC-VM 可执行文件的 main 函数：解析命令行参数，调用驱动层运行 .tc 文件。
 *
 * 用法：
 *   tc-vm [options] [<file.tc>]
 *   tc-vm -i|--repl
 *
 * 选项：
 *   -c, --check    仅静态分析，不执行
 *   -i, --repl     进入交互式 REPL
 *   -h, --help     显示帮助
 *   -V, --version  显示版本号
 *
 * 退出码：0 成功，1 失败（错误信息输出到 stderr）。
 *
 * 作者：唐荣兵
 * 联系邮箱：yanhuang8923@qq.com
 */
#include "tc_diagnostic.h"
#include "tc_driver.h"
#include "tc_repl.h"

#include <getopt.h>
#include <stdio.h>

#define TC_VM_VERSION "1.0"

/*
 * @brief 打印 tc-vm 版本号到 stdout
 */
static void tc_print_version(void) {
    printf("tc-vm %s\n", TC_VM_VERSION);
}

/*
 * @brief 打印 tc-vm 命令行用法帮助到 stderr
 * @param program 程序名（argv[0]）
 */
static void tc_print_usage(const char *program) {
    fprintf(stderr,
            "Usage: %s [options] [<file.tc>]\n"
            "\n"
            "TC language direct execution engine.\n"
            "\n"
            "Options:\n"
            "  -c, --check       static analysis only, do not execute\n"
            "  -i, --repl        interactive REPL mode\n"
            "  -h, --help        show this help and exit\n"
            "  -V, --version     show version and exit\n"
            "\n"
            "Examples:\n"
            "  %s tests/valid/example.tc\n"
            "  %s --check tests/valid/example.tc\n"
            "  %s --repl\n",
            program, program, program, program);
}

/*
 * @brief tc-vm 程序入口
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 成功返回 0；失败返回 1（错误信息输出到 stderr）
 * @note 使用 getopt_long 解析命令行选项，支持 -c/--check、-i/--repl、-h/--help、-V/--version
 * @note 文件模式必须且仅能指定一个 .tc 输入文件；REPL 模式不可与文件参数同用
 */
int main(int argc, char **argv) {
    static const struct option longopts[] = {
        {"check", no_argument, NULL, 'c'},
        {"repl", no_argument, NULL, 'i'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {NULL, 0, NULL, 0},
    };

    int check_only = 0;
    int repl_mode = 0;
    const char *path = NULL;
    TcDiagnostic diag;
    int opt;
    int rc = 0;

    tc_diagnostic_init(&diag);

    /* 解析命令行选项 */
    while ((opt = getopt_long(argc, argv, "cihV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'c':
            check_only = 1;
            break;
        case 'i':
            repl_mode = 1;
            break;
        case 'h':
            tc_print_usage(argv[0]);
            tc_diagnostic_clear(&diag);
            return 0;
        case 'V':
            tc_print_version();
            tc_diagnostic_clear(&diag);
            return 0;
        default:
            tc_print_usage(argv[0]);
            tc_diagnostic_clear(&diag);
            return 1;
        }
    }

    if (repl_mode) {
        if (optind < argc) {
            fprintf(stderr, "%s: cannot use --repl with input file\n", argv[0]);
            tc_print_usage(argv[0]);
            tc_diagnostic_clear(&diag);
            return 1;
        }
        if (check_only) {
            fprintf(stderr, "%s: cannot use --check with --repl\n", argv[0]);
            tc_print_usage(argv[0]);
            tc_diagnostic_clear(&diag);
            return 1;
        }
        return tc_repl_run(&diag);
    }

    /* 必须且仅能指定一个输入文件 */
    if (optind >= argc) {
        fprintf(stderr, "%s: missing input file\n", argv[0]);
        tc_print_usage(argv[0]);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    if (optind + 1 < argc) {
        fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[optind + 1]);
        tc_print_usage(argv[0]);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    path = argv[optind];
    rc = tc_run_file(path, check_only, &diag);
    if (rc != 0) {
        tc_diagnostic_print(&diag, stderr);
        tc_diagnostic_clear(&diag);
        return 1;
    }

    tc_diagnostic_clear(&diag);
    return 0;
}
