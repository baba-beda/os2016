#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <cstdlib>
#include <netinet/in.h>
#include <string.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <stdlib.h>
#include <netdb.h>

struct instruction {
  const char ** argv;
};

void error_netsh(const char * error_str) {
  perror(error_str);
  exit(EXIT_FAILURE);
}

int spawn_proc(int in_fd, int out_fd, struct instruction *instr) {
  pid_t pid;

  if ((pid = fork()) == 0) {
    if (in_fd != STDIN_FILENO) {
      dup2(in_fd, STDIN_FILENO);
      close(in_fd);
    }

    if (out_fd != STDOUT_FILENO) {
      dup2(out_fd, STDOUT_FILENO);
      close(out_fd);
    }
    return execvp(instr->argv[0], (char * const *) instr->argv );
  }
  return pid;
}

int fork_pipes(int n, int socket_fd, struct instruction *instr) {
  pid_t pid;
  int in, fd[2];
  in = socket_fd;

  for (int i = 0; i < n - 1; i++) {
    pipe(fd);
    spawn_proc(in, fd[1], instr + i);
    close(fd[1]);
    in = fd[0];
  }

  if (in != STDIN_FILENO) {
    dup2(in, STDIN_FILENO);
  }
  dup2(socket_fd, STDOUT_FILENO);

  return execvp(instr[n - 1].argv[0], (char * const *)instr[n - 1].argv);
}

//create socket and bind it
int connect(char * port) {
  struct sockaddr_in server;
  int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_fd == -1) {
    error_netsh("Creating socket failed");
  }
  memset(&server, 0, sizeof(struct sockaddr_in));
  int portno = atoi(port);
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(portno);

  if (bind(socket_fd, (struct sockaddr *) &server, sizeof(struct sockaddr)) == -1) {
    error_netsh("Binding socket failed");
  }

  return socket_fd;
}

//make read and write operations in socket not block each other
int unblock_socket(int socket_fd) {
  int flags, s;

  flags = fcntl(socket_fd, F_GETFL, 0);

  if (flags == -1) {
    error_netsh("fcntl error while getting flags");
  }

  flags |= O_NONBLOCK;

  s = fcntl(socket_fd, F_SETFL, flags);
  if (s == -1) {
    error_netsh("fcntl error while setting flags");
  }

  return 0;
}

int main(int argc, char * argv[]) {
  if (argc != 2) {
    error_netsh("Invalid input");
  }

  //process shouldn't be group leader
  pid_t initial_fork_pid = fork();

  if (initial_fork_pid == -1) {
    error_netsh("Initial fork failed");
  }
  else if (initial_fork_pid > 0) {
    //rest in peace, father
    exit(0);
  }
  else {
    //let's get to new session, where we gonna be free from any terminals
    pid_t new_session_pid = setsid();

    if (new_session_pid == -1) {
      error_netsh("setsid failed");
    }

    //ok, now we are the leader, again
    pid_t second_fork_pid = fork();

    if (second_fork_pid == -1) {
      error_netsh("Failed to fork in new session");
    }
    else if (second_fork_pid > 0) {
      //rest_in_peace, temporary leader
      exit(0);
    }
    else {
      //whooh daemon is alive and prosperous
      pid_t daemon_pid = getpid();
      int tmp_fd = open("/tmp/netsh.pid", O_WRONLY | O_CREAT);
      char * buf = new char[64];
      int pid_to_write = sprintf(buf, "%d\n", daemon_pid);
      ssize_t written = write(tmp_fd, buf, pid_to_write);
      close(tmp_fd);
      if (written == -1) {
        error_netsh("Writing pid to file failed");
      }

      const int MAX_EVENTS = 64;
      struct epoll_event event;
      struct epoll_event *events;

      int socket_fd = connect(argv[1]);

      unblock_socket(socket_fd);

      if (listen(socket_fd, SOMAXCONN) == -1) {
        error_netsh("Listening to socket failed");
      }

      int epoll_fd = epoll_create1(0);
      if (epoll_fd == -1) {
        error_netsh("Epoll_create error");
      }
      event.data.fd = socket_fd;
      event.events = EPOLLIN | EPOLLET;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket_fd, &event) == -1) {
        error_netsh("Epoll_ctl error");
      }

      events = (struct epoll_event *)calloc(MAX_EVENTS, sizeof(event));

      while (1) {
        int n, i;

        n = epoll_wait (epoll_fd, events, MAX_EVENTS, -1);

        for (i = 0; i < n; i++) {
          if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))) {
              /* An error has occured on this fd, or the socket is not
                 ready for reading (why were we notified then?) */
              fprintf (stderr, "epoll error\n");
              close (events[i].data.fd);
              continue;
          }
          else if (socket_fd == events[i].data.fd) {
            /* We have a notification on the listening socket, which
               means one or more incoming connections. */
            while (1) {
              struct sockaddr in_addr;
              socklen_t in_len;
              int in_fd;
              char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

              in_len = sizeof in_addr;
              in_fd = accept(socket_fd, &in_addr, &in_len);
              if (in_fd == -1) {
                if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                  /* We have processed all incoming
                     connections. */
                  break;
                }
                else {
                  perror ("Failed to accept");
                  break;
                }
              }

              /* Make the incoming socket non-blocking and add it to the
                 list of fds to monitor. */
              if (unblock_socket(in_fd) == -1) {
                error_netsh("Failed to make socket nonblocking");
              }

              event.data.fd = in_fd;
              event.events = EPOLLIN | EPOLLET;
              if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, in_fd, &event) == -1) {
                error_netsh("Epoll_ctl error");
              }
            }
            continue;
          }
          else {
            /* We have data on the fd waiting to be read. Read and
               use it. We must read whatever data is available
               completely, as we are running in edge-triggered mode
               and won't get a notification again for the same
               data. */
            int done = 0;
            std::vector<struct instruction> commands;
            std::vector<char *> args;
            std::string last_arg;
            int args_count = 0;

            int end_of_line = 0;
            int is_quote = 0;

            while (!done) {
              ssize_t count;
              char buf[4096];

              int last_space_pos = 0;

              count = read(events[i].data.fd, buf, sizeof buf);
              if (count == -1) {
                /* If errno == EAGAIN, that means we have read all
                   data. So go back to the main loop. */
                if (errno != EAGAIN) {
                  perror ("read");
                  done = 1;
                }
                break;
              }

              for (int j = 0; j < count; j++) {
                if (buf[j] == '\'' && !is_quote) {
                  is_quote = 1;
                  last_space_pos = j + 1;
                  continue;
                }
                if (buf[j] == '\'' && is_quote || !is_quote && (buf[j] == ' ' || buf[j] == '|' || buf[j] == '\n')) {
                  if (j - last_space_pos > 0) {
                    last_arg.append(std::string(buf + last_space_pos, j - last_space_pos));
                    char * aux = new char[last_arg.size() + 1];
                    std::strcpy(aux, last_arg.c_str());
                    aux[last_arg.size()] = '\0';
                    args.push_back(aux);
                    args_count++;
                    last_arg = std::string();
                  }
                  last_space_pos = j + 1;
                  is_quote = 0;
                }
                if (!is_quote && (buf[j] == '|' || buf[j] == '\n')) {
                  char ** args_array = new char* [args_count + 1];
                  for (int k = 0; k < args_count; ++k) {
                    args_array[k] = args[k];
                  }
                  args_array[args_count] = NULL;
                  args.clear();
                  args_count = 0;
                  commands.push_back((struct instruction) {.argv = (const char **) args_array});
                }
                if (buf[j] == '\n') {
                  done = 1;
                  break;
                }
              }
              last_arg.append(std::string(buf + last_space_pos, count - last_space_pos));
            }

            pid_t pid = fork();

            if (pid == -1) {
              error_netsh("Failed to fork on input received");
            } else if (pid == 0) {
              fork_pipes (commands.size(), events[i].data.fd, &commands[0]);
            }

            if (done) {
              /* Closing the descriptor will make epoll remove it
                 from the set of descriptors which are monitored. */
              close (events[i].data.fd);
            }
          }
        }
      }
    }
  }
}
