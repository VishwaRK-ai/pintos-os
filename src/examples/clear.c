#include <stdio.h>

int main(void) {
    // A VGA monitor is exactly 25 lines tall, tis pushes all old text off the screen!
    for(int i = 0; i < 25; i++) {
        printf("\n");
    }
    return 0;
}