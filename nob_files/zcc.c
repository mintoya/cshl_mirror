#include <stdlib.h>
#include <string.h>
int main(int nargs, char *args[nargs]) {
  char buffer[1024] = "zig cc";
  for (int i = 1; i < nargs; i++)
    strcat(strcat(buffer, " "), args[i]);
  return system(buffer);
}
