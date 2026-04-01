#include <stdio.h>
#include <syscall.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: touch <filename>\n");
        return 1;
    }
    // Create an empty file (size 0)
    if (create(argv[1], 0)) {
        // Silent success, just like real Linux!
    } else {
        printf("Error: Could not create %s\n", argv[1]);
    }
    return 0;
}