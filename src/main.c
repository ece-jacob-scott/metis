/**
 * metis - A simple file watcher
 *
 * This program is a simple file watcher that can be used to run a command when
 * a file is modified.
 *
 * The program is written in C and uses the inotify API to watch for file
 * changes.
 *
 * The program is designed to be used in a similar way to nodemon, a popular
 * file watcher for Node.js.
 *
 * Usage:
 * - metis -c "echo 'File modified'" test.txt
 * - metis -c "echo 'File modified'" .
 */

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
bool execute = true;

char *strdup(const char *s) {
  size_t len = strlen(s) + 1;
  char *new = (char *)malloc(len);
  if (new == NULL)
    return NULL;

  strcpy(new, s);
  return new;
}

char *strreplace(char *orig, char *rep, char *with) {
  char *result;  // the return string
  char *ins;     // the next insert point
  char *tmp;     // varies
  int len_rep;   // length of rep (the string to remove)
  int len_with;  // length of with (the string to replace rep with)
  int len_front; // distance between rep and end of last rep
  int count;     // number of replacements

  // sanity checks and initialization
  if (!orig || !rep)
    return NULL;
  len_rep = strlen(rep);
  if (len_rep == 0)
    return NULL; // empty rep causes infinite loop during count
  if (!with)
    with = "";
  len_with = strlen(with);

  // count the number of replacements needed
  ins = orig;
  for (count = 0; (tmp = strstr(ins, rep)); ++count) {
    ins = tmp + len_rep;
  }

  tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

  if (!result)
    return NULL;

  // first time through the loop, all the variable are set correctly
  // from here on,

  //    tmp points to the end of the result string
  //    ins points to the next occurrence of rep in orig
  //    orig points to the remainder of orig after "end of rep"
  while (count--) {
    ins = strstr(orig, rep);
    len_front = ins - orig;
    tmp = strncpy(tmp, orig, len_front) + len_front;
    tmp = strcpy(tmp, with) + len_with;
    orig += len_front + len_rep; // move to next "end of rep"
  }
  strcpy(tmp, orig);
  return result;
}

typedef struct options {
  char *command;
  char **paths;
  int paths_len;
} OPTIONS;

OPTIONS *parse_options(int argc, char **argv) {
  OPTIONS *options = (OPTIONS *)malloc(sizeof(OPTIONS));
  options->command = NULL;
  options->paths = NULL;
  options->paths_len = 0;

  int c;
  while ((c = getopt(argc, argv, "c:")) != -1) {
    switch (c) {
    case 'c':
      options->command = strdup(optarg);
      break;
    default:
      abort();
    }
  }

  // initialize paths array
  options->paths = (char **)malloc(sizeof(char *) * (argc - optind));

  for (int index = optind; index < argc; index++) {
    options->paths[options->paths_len] = strdup(argv[index]);
    options->paths_len++;
  }

  return options;
}

void free_options(OPTIONS *options) {
  free(options->command);
  for (int i = 0; i < options->paths_len; i++) {
    free(options->paths[i]);
  }

  if (options->paths_len != 0) {
    free(options->paths);
  }

  free(options);
}

typedef struct watcher {
  int wd;
  char *file_name;
} WATCHER;

int push_watcher(WATCHER **watcher_array, int len, WATCHER *watcher) {
  int new_len = len + 1;

  (*watcher_array) =
      (WATCHER *)realloc(*watcher_array, new_len * sizeof(WATCHER));
  (*watcher_array)[new_len - 1] = *watcher;

  return new_len;
}

void search_watchers_by_wd(const WATCHER *watcher_array, int len, int wd,
                           WATCHER *watcher) {
  for (int i = 0; i < len; i++) {
    WATCHER w = watcher_array[i];

    if (w.wd == wd) {
      (*watcher).file_name = w.file_name;
      (*watcher).wd = w.wd;
      return;
    }
  }
}

void trap(int signal) {
  printf("\ngot %d signal\n", signal);
  execute = false;
}

char *concat_path(char *curr_path, char *new_path) {
  int curr_path_len = strlen(curr_path);
  int new_path_len = strlen(new_path);
  int add_trail = 0;
  if (curr_path[curr_path_len - 1] != '/') {
    add_trail += 1;
  }

  if (new_path[0] == '/') {
    printf("new_path starts with '/'");
    abort();
  }

  char *path = (char *)malloc(sizeof(char) *
                              (curr_path_len + new_path_len + add_trail + 1));
  path[0] = '\0';

  strcat(path, curr_path);
  if (add_trail == 1) {
    strcat(path, "/");
  }
  strcat(path, new_path);

  return path;
}

void walk_files_rec(char *curr_path, WATCHER **watcher_arr,
                    int *watcher_arr_len, int watcher_fd) {
  int err;

  struct stat sbuf;
  err = stat(curr_path, &sbuf);
  if (err != 0) {
    perror("stat");
    abort();
  }

  if (S_ISREG(sbuf.st_mode)) {
    printf("file: %s\n", curr_path);
    // add watcher and return
    int wd = inotify_add_watch(watcher_fd, curr_path, IN_MODIFY);
    if (wd < 0) {
      perror("inotify_add_watch");
      abort();
    }

    WATCHER watcher;
    watcher.wd = wd;
    watcher.file_name = strdup(curr_path);
    (*watcher_arr_len) =
        push_watcher(watcher_arr, (*watcher_arr_len), &watcher);
    return;
  }

  if (S_ISDIR(sbuf.st_mode)) {
    printf("dir: %s\n", curr_path);
    // recursive call on all the files
    DIR *dir = opendir(curr_path);
    if (dir == NULL) {
      perror("could not open directory");
      abort();
    }

    errno = 0;
    struct dirent *dp;
    while ((dp = readdir(dir))) {
      // TODO: write a skip function
      if (strcmp(dp->d_name, "..") == 0 || strcmp(dp->d_name, ".") == 0 ||
          strcmp(dp->d_name, ".git") == 0 ||
          strcmp(dp->d_name, ".cache") == 0) {
        continue;
      }

      // get the fullpath
      char *full_path = concat_path(curr_path, dp->d_name);
      walk_files_rec(full_path, watcher_arr, watcher_arr_len, watcher_fd);
      free(full_path);
    }

    closedir(dir);

    return;
  }

  printf("skipping: %s\n", curr_path);

  return;
}

void walk_files_start(WATCHER **watcher_arr, int *watcher_arr_len,
                      int watcher_fd, OPTIONS *options) {
  for (int i = 0; i < options->paths_len; i++) {
    walk_files_rec(options->paths[i], watcher_arr, watcher_arr_len, watcher_fd);
  }

  printf("watching %d files\n", *watcher_arr_len);
  for (int i = 0; i < *watcher_arr_len; i++) {
    printf("\twatching: %s\n", (*watcher_arr)[i].file_name);
  }

  return;
}

void watch(OPTIONS *options) {
  int err;
  WATCHER *watcher_arr = (WATCHER *)malloc(1 * sizeof(WATCHER));
  int watcher_arr_len = 0;

  // init watcher fd
  int fd = inotify_init();
  if (fd < 0) {
    perror("inotify_init");
    abort();
  }

  walk_files_start(&watcher_arr, &watcher_arr_len, fd, options);

  // go over the paths and see what kind of file they are
  struct pollfd watcher_poll;
  memset(&watcher_poll, 0, sizeof(watcher_poll));

  watcher_poll.fd = fd;
  watcher_poll.events = POLLIN | POLLPRI | POLLERR;

  // poll for reads
  while (execute) {
    if (poll(&watcher_poll, 1, 100) != 1) {
      // not ready
      continue;
    }

    char buf[BUF_LEN];
    int len, i = 0;

    len = read(fd, buf, BUF_LEN);
    if (len < 0) {
      perror("read");
      return;
    }

    if (len == 0) {
      printf("No events\n");
      return;
    }

    printf("got new event\n");

    while (i < len) {
      printf("processing event: %d\n", i);
      struct inotify_event *event;

      event = (struct inotify_event *)&buf[i];

      char *file_name = NULL;
      if (event->len > 0) {
        file_name = event->name;
      } else {
        WATCHER w;
        search_watchers_by_wd(watcher_arr, watcher_arr_len, event->wd, &w);
        file_name = w.file_name;
      }

      printf("%s updated!\n", file_name);

      if (options->command != NULL) {
        bool need_free = true;
        char *new_command = strreplace(options->command, "{}", file_name);

        if (new_command == NULL) {
          need_free = false;
          new_command = options->command;
        }

        err = system(new_command);
        if (err != 0) {
          perror("command");
        }

        if (need_free) {
          free(new_command);
        }
      }

      i += EVENT_SIZE + event->len;
    }
  }

  for (int i = 0; i < watcher_arr_len; i++) {
    free(watcher_arr[i].file_name);
  }

  free(watcher_arr);

  err = close(fd);
  if (err != 0) {
    perror("close");
  }
}

int main(int argc, char **argv) {
  OPTIONS *options = parse_options(argc, argv);

  signal(SIGINT, &trap);

  watch(options);

  free_options(options);

  // remove signal handler
  signal(SIGINT, SIG_DFL);

  return 0;
}
