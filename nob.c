#define NOB_IMPLEMENTATION

#if !defined CC
  #define CC "zcc"
#endif
#if defined(_WIN32)
  #define EXTENSION ".exe"
#else
  #define EXTENSION ""
#endif
#if __has_include("deps/nob.h/nob.h")
  // {compilation
  #include "deps/nob.h/nob.h"

  #if defined(_WIN32)
    // #define COMMON_CFLAGS "-std=c2y", "-fdefer-ts" , "-ldbghelp"
    #define COMMON_CFLAGS "-std=c2y", "-fdefer-ts", "-DnoAssertMessage"
  #else
    #define COMMON_CFLAGS "-std=c2y", "-fdefer-ts"
  #endif

bool build_main(Cmd *cmd, Procs *procs) {
  if (!mkdir_if_not_exists("build"))
    return false;
  cmd_append(cmd, CC, COMMON_CFLAGS, "-o", "build/compiler" EXTENSION, "main.c", "-I", "deps", "-w", "-g", );
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
                "        Build and run the compiler\n");
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
  }

  nob_log(ERROR, "Unknown subcommand %s", subcmd);
  usage(program);
  return 1;
}
// }
#else
  // {bootstrap
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>

int main(int argc, char **argv) {
  printf("Missing dependencies. Cloning...\n");
  int res = system("git clone https://github.com/tsoding/nob.h deps/nob.h");
  if (res) {
    printf("Failed to clone dependencies.\n");
    return 1;
  }
  res = system("git clone https://codeberg.org/mintoya/wheels deps/wheels");
  if (res) {
    printf("Failed to clone dependencies.\n");
    return 1;
  }

  res = system(CC " -o nob" EXTENSION " nob.c");
  if (res) {
    printf("Failed to recompile self\n");
    return 1;
  }
  char cmd[1024] = "./nob" EXTENSION;
  for (int i = 1; i < argc; ++i) {
    strcat(cmd, " ");
    strcat(cmd, argv[i]);
  }

  return system(cmd);
}
// }
#endif
