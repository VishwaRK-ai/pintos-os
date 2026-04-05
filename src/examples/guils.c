/* guils.c - Atomic GUI List Directory with Absolute Paths! */
#include <stdio.h>
#include <syscall.h>
#include <string.h>
#include <stdbool.h>

int main (int argc, char *argv[]) 
{
  /* If React passes a path, use it. Otherwise, use root "/" */
  char *target_dir = "/";
  if (argc == 2) {
      target_dir = argv[1];
  }

  int dir_fd = open (target_dir);
  if (dir_fd < 0) {
      printf("\n[GUI_START]\n[GUI_END]\n");
      return -1;
  }

  char name[128];
  
  /* ATOMIC BOUNDARY - React waits for this! */
  printf("\n[GUI_START]\n");
  
  while (readdir (dir_fd, name)) 
    {
      /* Build the full absolute path so we can check if it's a folder */
      char fullpath[256];
      strlcpy(fullpath, target_dir, sizeof fullpath);
      if (fullpath[strlen(fullpath) - 1] != '/') {
          strlcat(fullpath, "/", sizeof fullpath);
      }
      strlcat(fullpath, name, sizeof fullpath);

      int item_fd = open (fullpath);
      bool is_folder = isdir (item_fd);
      close (item_fd);

      if (is_folder) {
        printf("<DIR> %s\n", name); 
      } else {
        printf("<FILE> %s\n", name);
      }
    }
    
  /* ATOMIC END BOUNDARY */
  printf("[GUI_END]\n");
  
  close (dir_fd);
  return 0;
}