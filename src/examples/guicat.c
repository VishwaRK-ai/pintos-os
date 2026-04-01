#include <stdio.h>
#include <syscall.h>

int main(int argc, char *argv[]) {
    if (argc != 2) return 1;
    
    int fd = open(argv[1]);
    if (fd < 0) return 1;

    // start the teact tag with the filename and a pipe symbol '|'
    printf("[READ]%s|", argv[1]); 
    
    char buffer[1024];
    int bytes;
    while ((bytes = read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }
    
    // Close the tag so React knows the file is done!
    printf("[ENDREAD]\n");
    close(fd);
    return 0;
}