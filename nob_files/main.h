#include "../deps/nob.h/nob.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#if __has_include(<stdcountof.h>)
  #include <stdcountof.h>
#else
  #define countof(x) (sizeof(x) / sizeof(x[0]))
#endif

#if !defined CC
  #define CC "clang"
#endif

#if defined(_WIN32)
  #define EXTENSION ".exe"
#else
  #define EXTENSION ""
#endif

#if !defined INPUT_FLAGS
  #define INPUT_FLAGS ""
#endif

#define COMMON_CFLAGS                          \
  INPUT_FLAGS,                                 \
      "-std=c23",                              \
      "-I", "deps", "-fno-sanitize=vla-bound", \
      "-g"

#define let __auto_type
#define cmd_imm(...) ({Cmd _r = {};cmd_append(&_r , __VA_ARGS__);_r; })
#define nob_da_appends(procs, items) \
  nob_da_append_many(procs, items, countof(items))
Procs build_compiler(void) {
  Procs p = {};
  // clang-format off
  nob_da_appends(&p, ((Nob_Proc[]) {
    cmd_run_async(cmd_imm(
        CC, COMMON_CFLAGS,
        "-D_GNU_SOURCE",
        "-I", "deps/tree-sitter/lib/src",
        "-I", "deps/tree-sitter/lib/include",
        "-c", "deps/tree-sitter/lib/src/lib.c",
        "-o", "build/tree_sitter.o"
    )),
    cmd_run_async(cmd_imm(
        CC, COMMON_CFLAGS,
        "-I", "deps/tree-sitter/lib/src",
        "-I", "tsversion/src",
        "-c", "tsversion/src/parser.c",
        "-o", "build/parser.o"
    )),
    cmd_run_async(cmd_imm(
        CC, COMMON_CFLAGS,
        "-I", "deps/tree-sitter/lib/include",
        "-c", "ts_adapt.c",
        "-o", "build/ts_adapt.o"
    ))
  }));
  // clang-format on
  return p;
}
int build_main(int nargs, char **args) {
  if (!mkdir_if_not_exists("build")) return false;

  Procs p = build_compiler();

  // clang-format off
  nob_da_append(&p, cmd_run_async(cmd_imm(
    CC, COMMON_CFLAGS,
    "-c", "main.c",
    "-o", "build/main.o"
  )));
  // clang-format on

  if (!nob_procs_wait(p)) return false;
  if (!nob_cmd_run_sync(cmd_imm(
          CC,
          COMMON_CFLAGS,
          "-o",
          "build/compiler" EXTENSION,

          "build/main.o",
          "build/tree_sitter.o",
          "build/parser.o",
          "build/ts_adapt.o"
      ))) return false;

  Cmd runcmd = {};
  cmd_append(&runcmd, "./build/compiler" EXTENSION);
  for (int i = 0; i < nargs; i++)
    cmd_append(&runcmd, args[i]);
  return !cmd_run_sync(runcmd);
}

int build_tests(int nargs, char **args) {
  if (!mkdir_if_not_exists("build")) return false;

  Procs p = build_compiler();

  // clang-format off
  nob_da_append(&p, cmd_run_async(cmd_imm(
    CC, COMMON_CFLAGS,
    "-c", "tests.c",
    "-o", "build/tests.o"
  )));
  // clang-format on

  if (!nob_procs_wait(p)) return false;

  Cmd cmd = {};
#if defined(_WIN32)
  cmd_append(&cmd, CC, COMMON_CFLAGS, "-o", "build/tests" EXTENSION, "build/tests.o", "build/tree_sitter.o", "build/parser.o", "build/ts_adapt.o", );
#else
  cmd_append(&cmd, CC, COMMON_CFLAGS, "-o", "build/tests" EXTENSION, "build/tests.o", "build/tree_sitter.o", "build/parser.o", "build/ts_adapt.o", );
#endif
  if (!cmd_run_sync(cmd)) return false;

  Cmd runcmd = {};
  cmd_append(&runcmd, "./build/tests" EXTENSION);
  for (int i = 0; i < nargs; i++)
    cmd_append(&runcmd, args[i]);
  return !cmd_run_sync(runcmd);
}

void usage(const char *program) {
  nob_log(INFO, "Usage: %s [<subcommand>]", program);
  nob_log(INFO, "Subcommands:               \n"
                "    help                   \n"
                "        Print this message \n"
                "    run [args]             \n"
                "        Build and run the compiler\n"
                "    test [args]             \n"
                "        Build and run the tests\n");
}

int actualmain(int argc, char **argv) {
  const char *program = shift_args(&argc, &argv);

  if (!argc) return build_main(argc, argv);

  const char *subcmd = shift_args(&argc, &argv);

  if (!strcmp(subcmd, "help")) return (usage(program), 0);

  if (!strcmp(subcmd, "test"))
    return build_tests(argc, argv);
  else if (!strcmp(subcmd, "run"))
    return build_main(argc, argv);

  nob_log(ERROR, "Unknown subcommand %s", subcmd);
  usage(program);
  return 1;
}

#define NOB_IMPLEMENTATION
#include "../deps/nob.h/nob.h"
