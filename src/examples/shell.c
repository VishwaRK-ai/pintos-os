#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

static void read_line (char line[], size_t);
static bool backspace (char **pos, char line[]);

int
main (void)
{
  printf ("Shell starting...\n");
  for (;;) 
    {
      char command[4096];

      /* Read command. */
      printf ("--> ");
      read_line (command, sizeof command);

      /* Execute command. */
      if (!strcmp (command, "exit"))
        break;
      else if (!memcmp (command, "cd ", 3)) 
        {
          if (!chdir (command + 3))
            printf ("\"%s\": chdir failed\n", command + 3);
        }
      else if (command[0] == '\0') 
        {
          /* Empty command. */
        }
      else
        {
          bool background = false;
          int len = strlen (command);
          int i = len - 1;
          while (i >= 0 && command[i] == ' ') i--;
          if (i >= 0 && command[i] == '&') 
            {
              background = true;
              command[i] = '\0'; /* Remove '&' */
            }

          /* 1. Try running it exactly as typed */
          pid_t pid = exec (command);
          
          /* 2. THE FIX: If it failed, try running it from the root directory! */
          if (pid == PID_ERROR) 
            {
              char root_command[4098];
              /* Prepend a slash so it searches the root directory */
              snprintf(root_command, sizeof root_command, "/%s", command);
              pid = exec(root_command);
            }

          /* 3. Wait for it to finish, or print the error */
          if (pid != PID_ERROR) {
            if (!background) {
              wait (pid);
            }
          } else {
            printf ("exec failed\n");
          }
        }
    }

  printf ("Shell exiting.");
  return EXIT_SUCCESS;
}

static void
read_line (char line[], size_t size) 
{
  char *pos = line;
  for (;;)
    {
      char c;
      read (STDIN_FILENO, &c, 1);

      switch (c) 
        {
        case '\r':
          *pos = '\0';
          putchar ('\n');
          return;

        case '\b':
          backspace (&pos, line);
          break;

        case ('U' - 'A') + 1:
          while (backspace (&pos, line))
            continue;
          break;

        default:
          if (pos < line + size - 1) 
            {
              putchar (c);
              *pos++ = c;
            }
          break;
        }
    }
}

static bool
backspace (char **pos, char line[]) 
{
  if (*pos > line)
    {
      printf ("\b \b");
      (*pos)--;
      return true;
    }
  else
    return false;
}