#include "../deps/nob.h/nob.h"
#include <stdcountof.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if !defined CC
  #define CC "clang"
#endif

#if defined(_WIN32)
  #define EXTENSION ".exe"
#else
  #define EXTENSION ""
#endif

#if !defined INPUT_FLAGS
  #define INPUT_FLAGS "",
#endif

#define COMMON_CFLAGS INPUT_FLAGS "-std=c23", "-fdefer-ts", "-DnoAssertMessage", "-I", "deps"

//{timespec
#include <time.h>
typedef struct timespec timespec;
typedef _BitInt(sizeof(size_t) * 8 + 30) ts_int;
static const ts_int ts_int_ns = 1;
static const ts_int ts_int_us = ts_int_ns * 1000;
static const ts_int ts_int_ms = ts_int_us * 1000;
static const ts_int ts_int_s = ts_int_ms * 1000;
static const ts_int ts_int_min = ts_int_s * 60;
static const ts_int ts_int_hour = ts_int_min * 60;
static const ts_int ts_int_day = ts_int_hour * 24;

static inline ts_int timespec_int(timespec in) { return ((ts_int)in.tv_sec * ts_int_s) + in.tv_nsec; }
static inline timespec int_timespec(ts_int in) { return (timespec){(in / ts_int_s), (in % ts_int_s)}; }
static inline ts_int now() {
  struct timespec ts = {};
  assert(timespec_get(&ts, TIME_UTC) == TIME_UTC);
  return timespec_int(ts);
}
char *ts_int_cstr(char *s, ts_int in) {
  if (!in) return strcat(s, "0");
  strcat(s, in < 0 ? (in = -in, "-") : "");

  while (in) {
    strcat(s, (char[2]){'0' + (in % 10), 0});
    in /= 10;
  }
  return s;
}
ts_int cstr_ts_int(char *s) {
  ts_int out = 0, m = 1, n = *s == '-' ? (s++, 1) : 0;
  while (*s) {
    out += (*s++ - '0') * m;
    m *= 10;
  }
  return n ? -out : out;
}
//}

bool check_last_update() {
  __auto_type f = fopen("nob_files/last_run.txt", "rb+");
  if (!f) f = fopen("nob_files/last_run.txt", "wb+");
  if (!f) return (nob_log(ERROR, "couldnt read last_run.txt , couldnt open file"), false);

  char ftext[100];
  int len = fread(ftext, sizeof(ftext[0]), countof(ftext), f);
  if (len >= 99) return (fclose(f), nob_log(ERROR, "couldnt read last_run.txt , something is wrong with the file"), false);
  fseek(f, 0, SEEK_SET);
  ftext[len] = 0;

  __auto_type lasttime = cstr_ts_int(ftext);
  __auto_type diff = now() - lasttime;
  if (diff > ts_int_min * 5)
    return (fputs(ts_int_cstr((char[1024]){}, now()), f), fclose(f), false);
  return (fclose(f), true);
}
bool update_deps() {
  // i modify wheels enough for this to matter
  if (!check_last_update()) {
    nob_log(INFO, "syncing deps...");

    Procs p = {};

    Cmd wheels = {};
    cmd_append(&wheels, "git", "-C", "deps/wheels", "pull", "--autostash");
    nob_da_append(&p, cmd_run_async(wheels));

    Cmd nob = {};
    cmd_append(&nob, "git", "-C", "deps/nob.h", "pull", "--autostash");
    nob_da_append(&p, cmd_run_async(nob));

    return nob_procs_wait(p);
  } else return (nob_log(INFO, "skipping sync..."), true);
}
bool build_main(Cmd *cmd, Procs *procs) {
  if (!mkdir_if_not_exists("build"))
    return false;
  cmd_append(cmd, CC, COMMON_CFLAGS, "-o", "build/compiler" EXTENSION, "main.c", "-g");
  return cmd_run(cmd, .async = procs);
}

bool build_tests(Cmd *cmd, Procs *procs) {
  if (!mkdir_if_not_exists("build"))
    return false;
#if defined(_WIN32)
  cmd_append(cmd, CC, COMMON_CFLAGS, "-o", "build/compiler" EXTENSION, "tests.c", "deps/wheels/c11threads/c11threads_win32.c");
#else
  cmd_append(cmd, CC, COMMON_CFLAGS, "-o", "build/compiler" EXTENSION, "tests.c");
#endif
  if (!cmd_run(cmd, .async = procs))
    return false;
  return true;
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

int main(int argc, char **argv) {
  NOB_GO_REBUILD_URSELF(argc, argv);

  Cmd cmd = {0};
  Procs procs = {0};

  const char *program = shift_args(&argc, &argv);

  if (!argc) {
    if (!build_main(&cmd, &procs))
      return 1;
    return 0;
  }

  const char *subcmd = shift_args(&argc, &argv);

  if (!strcmp(subcmd, "help")) {
    usage(program);
    return 0;
  }

  do {
    if (!update_deps())
      return 1;
    if (!nob_procs_wait(procs))
      return 1;
  } while (0);

  if (!strcmp(subcmd, "run")) {
    if (!build_main(&cmd, &procs))
      return 1;
    if (!nob_procs_wait(procs))
      return 1;
    cmd.count = 0;
    cmd_append(&cmd, "./build/compiler" EXTENSION);
    for (int i = 0; i < argc; ++i)
      cmd_append(&cmd, argv[i]);
    if (!cmd_run_sync(cmd))
      return 1;
    return 0;
  } else if (!strcmp(subcmd, "test")) {
    if (!build_tests(&cmd, &procs))
      return 1;
    if (!nob_procs_wait(procs))
      return 1;

    cmd.count = 0;
    cmd_append(&cmd, "./build/compiler" EXTENSION);
    if (!cmd_run_sync(cmd))
      return 1;
    return 0;
  }

  nob_log(ERROR, "Unknown subcommand %s", subcmd);
  usage(program);
  return 1;
}

#define NOB_IMPLEMENTATION
#include "../deps/nob.h/nob.h"
