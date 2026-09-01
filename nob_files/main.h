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
      "-std=gnu23",                            \
      "-I", "deps", "-fno-sanitize=vla-bound", \
      "-g"

//{timespec
#include <time.h>
typedef struct timespec timespec;
typedef long long ts_int;
static const ts_int ts_int_ns = 1;
static const ts_int ts_int_us = ts_int_ns * 1000;
static const ts_int ts_int_ms = ts_int_us * 1000;
static const ts_int ts_int_s = ts_int_ms * 1000;
static const ts_int ts_int_min = ts_int_s * 60;
static const ts_int ts_int_hour = ts_int_min * 60;
static const ts_int ts_int_day = ts_int_hour * 24;

static inline ts_int timespec_int(timespec in) { return ((ts_int)in.tv_sec * ts_int_s) + in.tv_nsec; }
static inline timespec int_timespec(ts_int in) {
  typedef __typeof__(((timespec *)0)->tv_sec) secs;
  typedef __typeof__(((timespec *)0)->tv_nsec) nsecs;
  return (timespec){(secs)(in / ts_int_s), (nsecs)(in % ts_int_s)};
}
static inline ts_int now() {
  struct timespec ts = {};
  assert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
  return timespec_int(ts);
}
//}

#define let __auto_type
#define cmd_imm(...) ({Cmd _r = {};cmd_append(&_r , __VA_ARGS__);_r; })
bool check_last_update() {
  let f = fopen("nob_files/last_run.txt", "rb+");
  if (!f) f = fopen("nob_files/last_run.txt", "wb+");
  if (!f) return (nob_log(ERROR, "couldnt read last_run.txt , couldnt open file"), false);

  ts_int saved[1] = {};
  int len = fread(saved, sizeof(char), sizeof(saved), f);
  fseek(f, 0, SEEK_SET);

  let diff = now() - saved[0];
  *saved = now();
  if (diff > ts_int_min * 30)
    return (fwrite(saved, sizeof(char), sizeof(saved), f), fclose(f), false);
  return (fclose(f), true);
}
bool update_deps() {
  // i modify wheels enough for this to matter
  if (!check_last_update()) {
    nob_log(INFO, "syncing deps...");

    Procs p = {};

    nob_da_append(&p, cmd_run_async(cmd_imm("git", "-C", "deps/wheels", "pull", "--autostash")));

    return nob_procs_wait(p);
  } else return (nob_log(INFO, "skipping sync..."), true);
}
int build_main(int nargs, char **args) {

  if (!mkdir_if_not_exists("build")) return false;

  Procs p = {};

  // clang-format off
  nob_da_append(&p, cmd_run_async(cmd_imm(
    CC, COMMON_CFLAGS,
    "-c", "main.c",
    "-o", "build/main.o"
  )));
  nob_da_append(&p, cmd_run_async(cmd_imm(
    CC, COMMON_CFLAGS,
    "-D_GNU_SOURCE",
    "-I", "deps/tree-sitter/lib/include",
    "-c", "deps/tree-sitter/lib/src/lib.c",
    "-o", "build/tree_sitter.o"
  )));
  nob_da_append(&p, cmd_run_async(cmd_imm(
    CC, COMMON_CFLAGS,
    "-I", "tsversion/src",
    "-c", "tsversion/src/parser.c",
    "-o", "build/parser.o"
  )));
  nob_da_append(&p, cmd_run_async(cmd_imm(
    CC, COMMON_CFLAGS,
    "-I", "deps/tree-sitter/lib/include",
    "-c", "ts_adapt.c",
    "-o", "build/ts_adapt.o"
  )));
  // clang-format on

  if (!nob_procs_wait(p)) return false;
  let merge = cmd_imm(
      CC,
      COMMON_CFLAGS,
      "-o",
      "build/compiler" EXTENSION,

      "build/main.o",
      "build/tree_sitter.o",
      "build/parser.o",
      "build/ts_adapt.o"
  );
  if (!nob_cmd_run_sync(merge)) return false;

  Cmd runcmd = {};
  cmd_append(&runcmd, "./build/compiler" EXTENSION);
  for (int i = 0; i < nargs; i++)
    cmd_append(&runcmd, args[i]);
  return !cmd_run_sync(runcmd);
}

int build_tests() {
  Cmd cmd = {};
  if (!mkdir_if_not_exists("build"))
    return false;
#if defined(_WIN32)
  cmd_append(&cmd, CC, COMMON_CFLAGS, "-o", "build/tests" EXTENSION, "tests.c", "deps/wheels/c11threads/c11threads_win32.c");
#else
  cmd_append(&cmd, CC, COMMON_CFLAGS, "-o", "build/tests" EXTENSION, "tests.c");
#endif
  if (!cmd_run_sync(cmd)) return false;

  Cmd runcmd = {};
  cmd_append(&runcmd, "./build/tests" EXTENSION);
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

  if (!update_deps())
    return 1;

  if (!strcmp(subcmd, "test"))
    return build_tests();
  else if (!strcmp(subcmd, "run"))
    return build_main(argc, argv);

  nob_log(ERROR, "Unknown subcommand %s", subcmd);
  usage(program);
  return 1;
}

#define NOB_IMPLEMENTATION
#include "../deps/nob.h/nob.h"
