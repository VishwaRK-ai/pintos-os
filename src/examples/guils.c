#include <stdio.h>
#include <syscall.h>

int main(void) {
    // Open the current directory (Root)
    int dir_fd = open(".");
    if (dir_fd < 0) return 1;

    char name[20];
    // Read every file/folder and print it with a special React tag
    while (readdir(dir_fd, name)) {
        printf("[GUI]%s\n", name);
    }
    
    close(dir_fd);
    return 0;
}