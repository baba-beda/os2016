#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void error_cat(const char* error_str) {
  fprintf(stderr, "%s\n", error_str);
  exit(1);
}

int main() {
  char buffer[4096];
  size_t read_cnt;
  do {
    read_cnt = (size_t) read(STDIN_FILENO, buffer, sizeof(buffer));
    if (read_cnt == -1) {
      error_cat(strerror(errno));

    }
    size_t write_cnt = (size_t) write(STDOUT_FILENO, buffer, read_cnt);
    if (write_cnt < read_cnt) {
      error_cat("Unexpected EOF");
    }
  } while (read_cnt > 0);

  return 0;
}
