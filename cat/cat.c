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

int main(int argc, char * argv[]) {
  char buffer[4096];
  size_t read_cnt;
  char * file = argv[1];
  int input_fd = open(file, O_RDONLY);
  if (input_fd == -1) {
    error_cat(strerror(errno));
  }
  
  do {
    read_cnt = (size_t) read(input_fd, buffer, sizeof(buffer));
    if (read_cnt == -1 && EINTR != errno) {
      error_cat(strerror(errno));

    }
    size_t write_cnt;
    size_t written = 0;
    do {
      write_cnt = (size_t) write(STDOUT_FILENO, buffer, read_cnt - written);
      if (write_cnt == -1 && EINTR != errno) {
        error_cat(strerror(errno));
      }
      written += write_cnt;
    } while (written != read_cnt);

  } while (read_cnt > 0 && EINTR == errno);

  return 0;
}
