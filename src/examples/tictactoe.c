#include <stdio.h>
#include <syscall.h>
#include <string.h>

char board[9] = {'0','0','0','0','0','0','0','0','0'}; 
// '0' = empty, 'X' = player, 'O' = computer

/* Emits the special tracking tag for React webos scraper */
void print_state(void) {
    printf("[TTT_STATE] ");
    for(int i = 0; i < 9; i++) {
        printf("%c", board[i]);
    }
    printf(" [ENDTTT_STATE]\n");
}

int check_win(char player) {
    int wins[8][3] = {
        {0,1,2}, {3,4,5}, {6,7,8}, // rows
        {0,3,6}, {1,4,7}, {2,5,8}, // cols
        {0,4,8}, {2,4,6}           // diagonals
    };
    for(int i = 0; i < 8; i++) {
        if(board[wins[i][0]] == player && 
           board[wins[i][1]] == player && 
           board[wins[i][2]] == player) {
            return 1;
        }
    }
    return 0;
}

void computer_move(void) {
    // Basic AI: just pick the first empty slot.
    for(int i = 0; i < 9; i++) {
        if(board[i] == '0') {
            board[i] = 'O';
            break;
        }
    }
}

int main (int argc, char **argv) {
    printf("Starting Tic Tac Toe Backend...\n");
    print_state(); // Initial blank state

    char input;
    while(1) {
        // Blocks and reads 1 byte from STDIN (fd = 0)
        int bytes_read = read(0, &input, 1);
        if (bytes_read == 1) {
            // Valid inputs map to cells 0-8 for grid places 1-9
            if (input >= '1' && input <= '9') {
                int idx = input - '1';
                
                if (board[idx] == '0') {
                    board[idx] = 'X'; // Player's move
                    
                    if(check_win('X')) {
                        print_state(); // final state before exiting
                        printf("Player X Wins!\n");
                        break;
                    }

                    // Count empty slots BEFORE computer's turn
                    int empty = 0;
                    for(int i = 0; i < 9; i++) if(board[i] == '0') empty++;
                    
                    if(empty == 0) {
                        print_state();
                        printf("Draw!\n");
                        break;
                    }

                    // Computer's turn
                    computer_move();
                    if(check_win('O')) {
                        print_state();
                        printf("Computer Wins!\n");
                        break;
                    }
                    
                    // Final draw check
                    empty = 0;
                    for(int i = 0; i < 9; i++) if(board[i] == '0') empty++;
                    if(empty == 0) {
                        print_state();
                        printf("Draw!\n");
                        break;
                    }

                    // Print updated state back to serial
                    print_state();
                }
            } else if (input == 'q') {
                printf("Quitting game...\n");
                break; 
            }
        }
    }
    return EXIT_SUCCESS;
}