/**
 * metis - A simple file watcher
 *
 * This program is a simple file watcher that can be used to run a command when a file is modified.
 *
 * The program is written in C and uses the inotify API to watch for file changes.
 *
 * The program is designed to be used in a similar way to nodemon, a popular file watcher for Node.js.
 *
 * Usage:
 * - metis -c "echo 'File modified'" test.txt
 * - metis -c "echo 'File modified'" .
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdbool.h>
#include <poll.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 * (EVENT_SIZE + 16))
bool execute = true;

char *strdup(const char *s)
{
  size_t len = strlen(s) + 1;
  char *new = (char *)malloc(len);
  if (new == NULL)
    return NULL;

  strcpy(new, s);
  return new;
}

char *strreplace(char *orig, char *rep, char *with)
{
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
  for (count = 0; (tmp = strstr(ins, rep)); ++count)
  {
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
  while (count--)
  {
    ins = strstr(orig, rep);
    len_front = ins - orig;
    tmp = strncpy(tmp, orig, len_front) + len_front;
    tmp = strcpy(tmp, with) + len_with;
    orig += len_front + len_rep; // move to next "end of rep"
  }
  strcpy(tmp, orig);
  return result;
}

typedef struct options
{
  char *command;
  char **paths;
  int paths_len;
} OPTIONS;

OPTIONS *parse_options(int argc, char **argv)
{
  OPTIONS *options = (OPTIONS *)malloc(sizeof(OPTIONS));
  options->command = NULL;
  options->paths = NULL;
  options->paths_len = 0;

  int c;
  while ((c = getopt(argc, argv, "c:")) != -1)
  {
    switch (c)
    {
    case 'c':
      options->command = strdup(optarg);
      break;
    default:
      abort();
    }
  }

  // initialize paths array
  options->paths = (char **)malloc(sizeof(char *) * (argc - optind));

  for (int index = optind; index < argc; index++)
  {
    options->paths[options->paths_len] = strdup(argv[index]);
    options->paths_len++;
  }

  return options;
}

void free_options(OPTIONS *options)
{
  free(options->command);
  for (int i = 0; i < options->paths_len; i++)
  {
    free(options->paths[i]);
  }

  if (options->paths_len != 0)
  {
    free(options->paths);
  }

  free(options);
}

typedef struct watcher
{
  int wd;
  char *file_name;
} WATCHER;

int push_watcher(WATCHER **watcher_array, int len, WATCHER *watcher)
{
  int new_len = len + 1;
  WATCHER *tmp = (WATCHER *)realloc(*watcher_array, new_len * sizeof(WATCHER));
  if (tmp == NULL)
  {
    printf("failed to reallocate new memory");
    abort();
  }

  *watcher_array = tmp;

  *watcher_array[new_len - 1] = *watcher;

  return new_len;
}

void search_watchers_by_wd(const WATCHER *watcher_array, int len, int wd, WATCHER *watcher)
{
  for (int i = 0; i < len; i++)
  {
    WATCHER w = watcher_array[i];

    if (w.wd == wd)
    {
      (*watcher).file_name = w.file_name;
      (*watcher).wd = w.wd;
      return;
    }
  }
}

void trap(int signal)
{
  printf("\ngot %d signal\n", signal);
  execute = false;
}

void watch(OPTIONS *options)
{
  int err;
  WATCHER *watcher_arr = (WATCHER *)malloc(1 * sizeof(WATCHER));
  int watcher_arr_len = 0;

  // init watcher fd
  int fd = inotify_init();
  if (fd < 0)
  {
    perror("inotify_init");
    abort();
  }

  // go over the paths and see what kind of file they are
  for (int i = 0; i < options->paths_len; i++)
  {
    char *path = options->paths[i];
    struct stat sbuf;
    err = stat(path, &sbuf);
    if (err != 0)
    {
      perror("stat");
      abort();
    }

    if (S_ISREG(sbuf.st_mode))
    {
      // add watcher
      int wd = inotify_add_watch(fd, path, IN_MODIFY);
      if (wd < 0)
      {
        perror("inotify_add_watch");
        abort();
      }

      WATCHER watcher;
      watcher.wd = wd;
      watcher.file_name = strdup(path);

      watcher_arr_len = push_watcher(&watcher_arr, watcher_arr_len, &watcher);
      continue;
    }

    if (S_ISDIR(sbuf.st_mode))
    {
      // walk directory and add watchers
      continue;
    }

    printf("%s is not a file or directory, skipping", path);
  }

  struct pollfd watcher_poll;
  memset(&watcher_poll, 0, sizeof(watcher_poll));

  watcher_poll.fd = fd;
  watcher_poll.events = POLLIN;

  // poll for reads
  while (execute)
  {
    if (poll(&watcher_poll, 1, 100) != 1)
    {
      // not ready
      continue;
    }

    char buf[BUF_LEN];
    int len, i = 0;

    len = read(fd, buf, BUF_LEN);
    if (len < 0)
    {
      perror("read");
      return;
    }

    if (len == 0)
    {
      printf("No events\n");
      return;
    }

    while (i < len)
    {
      struct inotify_event *event;

      event = (struct inotify_event *)&buf[i];

      char *file_name = NULL;
      if (event->len > 0)
      {
        file_name = event->name;
      }
      else
      {
        WATCHER w;
        search_watchers_by_wd(watcher_arr, watcher_arr_len, event->wd, &w);
        file_name = w.file_name;
      }

      printf("%s updated!\n", file_name);

      if (options->command != NULL)
      {
        bool need_free = true;
        char *new_command = strreplace(options->command, "{}", file_name);

        if (new_command == NULL)
        {
          need_free = false;
          new_command = options->command;
        }

        err = system(new_command);
        if (err != 0)
        {
          perror("command");
        }

        if (need_free)
        {
          free(new_command);
        }
      }

      i += EVENT_SIZE + event->len;
    }
  }

  for (int i = 0; i < watcher_arr_len; i++)
  {
    free(watcher_arr[i].file_name);
  }

  free(watcher_arr);

  err = close(fd);
  if (err != 0)
  {
    perror("close");
  }
}

int main(int argc, char **argv)
{
  OPTIONS *options = parse_options(argc, argv);

  signal(SIGINT, &trap);

  watch(options);

  free_options(options);

  // remove signal handler
  signal(SIGINT, SIG_DFL);

  return 0;
}