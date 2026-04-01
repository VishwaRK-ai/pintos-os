#include <stdio.h>
#include <syscall.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: writefile <filename> <text>\n");
        return 1;
    }
    
    /*UPGRADE: If the file already exists
    ,delete it first so we can cleanly overwrite it with the new text!*/
    remove(argv[1]);
    
    //create the file with the exact size of the text
    if (!create(argv[1], strlen(argv[2]))) {
        printf("Error: Could not create %s\n", argv[1]);
        return 1;
    }
    
    //ppen the file to get a File Descriptor
    int fd = open(argv[1]);
    if (fd < 0) {
        printf("Error: Could not open %s\n", argv[1]);
        return 1;
    }
    
    //write the text and close the file
    write(fd, argv[2], strlen(argv[2]));
    close(fd);
    
    printf("Saved %s\n", argv[1]);
    return 0;
}