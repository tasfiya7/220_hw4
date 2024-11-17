#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT1 2201
#define PORT2 2202
#define BUFFER_SIZE 1024

// Define the phases of the game
typedef enum {
    PHASE_BEGIN,
    PHASE_INITIALIZE,
    PHASE_GAMEPLAY
} PlayerPhase;

// shape_offsets[type][rotation][cell][row/col]
int shape_offsets[7][4][4][2] = {
    // Type 1: Square piece
    { {{0, 0}, {0, 1}, {1, 0}, {1, 1}},  // Rotation 1
      {{0, 0}, {0, 1}, {1, 0}, {1, 1}},  // Rotation 2
      {{0, 0}, {0, 1}, {1, 0}, {1, 1}},  // Rotation 3
      {{0, 0}, {0, 1}, {1, 0}, {1, 1}}   // Rotation 4
    },
    // Type 2: Line piece
    { {{0, 0}, {0, 1}, {0, 2}, {0, 3}},  // Rotation 1 (horizontal)
      {{0, 0}, {1, 0}, {2, 0}, {3, 0}},  // Rotation 2 (vertical)
      {{0, 0}, {0, 1}, {0, 2}, {0, 3}},  // Rotation 3
      {{0, 0}, {1, 0}, {2, 0}, {3, 0}}   // Rotation 4
    },
    // Type 3: T piece
    { {{0, 0}, {0, 1}, {0, 2}, {1, 1}},  // Rotation 1
      {{0, 1}, {1, 0}, {1, 1}, {2, 1}},  // Rotation 2
      {{1, 0}, {1, 1}, {1, 2}, {0, 1}},  // Rotation 3
      {{0, 0}, {1, 0}, {2, 0}, {1, 1}}   // Rotation 4
    },
    // Type 4: L piece
    { {{0, 0}, {1, 0}, {2, 0}, {2, 1}},  // Rotation 1
      {{0, 0}, {0, 1}, {0, 2}, {1, 0}},  // Rotation 2
      {{0, 0}, {1, 0}, {2, 0}, {0, -1}}, // Rotation 3
      {{0, 2}, {1, 0}, {1, 1}, {1, 2}}   // Rotation 4
    },
    // Type 5: J piece
    { {{0, 1}, {1, 1}, {2, 1}, {2, 0}},  // Rotation 1
      {{0, 0}, {0, 1}, {0, 2}, {1, 2}},  // Rotation 2
      {{0, 1}, {1, 1}, {2, 1}, {0, 2}},  // Rotation 3
      {{0, 0}, {1, 0}, {1, 1}, {1, 2}}   // Rotation 4
    },
    // Type 6: Z piece
    { {{0, 0}, {0, 1}, {1, 1}, {1, 2}},  // Rotation 1
      {{0, 1}, {1, 0}, {1, 1}, {2, 0}},  // Rotation 2
      {{0, 0}, {0, 1}, {1, 1}, {1, 2}},  // Rotation 3
      {{0, 1}, {1, 0}, {1, 1}, {2, 0}}   // Rotation 4
    },
    // Type 7: S piece
    { {{0, 1}, {0, 2}, {1, 0}, {1, 1}},  // Rotation 1
      {{0, 0}, {1, 0}, {1, 1}, {2, 1}},  // Rotation 2
      {{0, 1}, {0, 2}, {1, 0}, {1, 1}},  // Rotation 3
      {{0, 0}, {1, 0}, {1, 1}, {2, 1}}   // Rotation 4
    }
};


// Struct for the game board
typedef struct {
    int width;
    int height;
    char **grid; // 2D grid: 'E' for empty, 'S' for ship, 'H' for hit, 'M' for miss
} GameBoard;

typedef struct {
    int type;           // Piece type (1-7)
    int rotation;       // Rotation (1-4)
    int column;         // Column position of the reference cell (black circle)
    int row;            // Row position of the reference cell
} TetrisPiece;


// Struct for player state
typedef struct {
    int is_ready;            // 1 if the player is ready
    int ships_remaining;     // Number of ships left
    TetrisPiece pieces[5];   // Array of 5 pieces
    char **hits;             // 2D array to track hits/misses
} PlayerState;


// Initialize the game board
GameBoard *initialize_board(int width, int height) {
    GameBoard *board = malloc(sizeof(GameBoard));
    board->width = width;
    board->height = height;

    board->grid = malloc(height * sizeof(char *));
    for (int i = 0; i < height; i++) {
        board->grid[i] = malloc(width * sizeof(char));
        memset(board->grid[i], 'E', width); // Fill with 'E' for empty
    }

    return board;
}

// Initialize player state
PlayerState *initialize_player_state(int width, int height) {
    PlayerState *player = malloc(sizeof(PlayerState));
    player->is_ready = 0;
    player->ships_remaining = 5;

    player->hits = malloc(height * sizeof(char *));
    for (int i = 0; i < height; i++) {
        player->hits[i] = malloc(width * sizeof(char));
        memset(player->hits[i], 'E', width); // Fill with 'E' for empty
    }

    return player;
}

// Free the game board
void free_board(GameBoard *board) {
    for (int i = 0; i < board->height; i++) {
        free(board->grid[i]);
    }
    free(board->grid);
    free(board);
}

// Free the player state
void free_player_state(PlayerState *player, int height) {
    for (int i = 0; i < height; i++) {
        free(player->hits[i]);
    }
    free(player->hits);
    free(player);
}

// Function to send error response
void send_error(int client_fd, int error_code, int player_num) {
    char error_message[BUFFER_SIZE];
    snprintf(error_message, BUFFER_SIZE, "E %d", error_code);
    send(client_fd, error_message, strlen(error_message), 0);
    printf("[Server] Sent to Player %d: E %d.\n", player_num, error_code);
}


// Function to send acknowledgment
void send_acknowledgment(int client_fd, int player_num) {
    send(client_fd, "A", strlen("A"), 0);
    printf("[Server] Sent acknowledgment to Player %d.\n", player_num);
}


// Function to process Forfeit packet
void process_forfeit_packet(int forfeiting_player, int client_fd1, int client_fd2, GameBoard *board, PlayerState *player1, PlayerState *player2) {
    if (forfeiting_player == 1) {
        send(client_fd1, "H 0", strlen("H 0"), 0); // Player 1 loses
        send(client_fd2, "H 1", strlen("H 1"), 0); // Player 2 wins
        printf("[Server] Player 1 forfeited. Player 2 wins.\n");
    } else if (forfeiting_player == 2) {
        send(client_fd1, "H 1", strlen("H 1"), 0); // Player 1 wins
        send(client_fd2, "H 0", strlen("H 0"), 0); // Player 2 loses
        printf("[Server] Player 2 forfeited. Player 1 wins.\n");
    }

    // Clean up resources
    if (board) free_board(board);
    if (player1) free_player_state(player1, board->height);
    if (player2) free_player_state(player2, board->height);

    close(client_fd1);
    close(client_fd2);
    exit(0); // Terminate server
}

int does_piece_fit(GameBoard *board, TetrisPiece piece) {
    int (*offsets)[2] = shape_offsets[piece.type - 1][piece.rotation - 1];

    for (int i = 0; i < 4; i++) {
        int cell_row = piece.row + offsets[i][0];
        int cell_col = piece.column + offsets[i][1];

        if (cell_row < 0 || cell_row >= board->height || 
            cell_col < 0 || cell_col >= board->width) {
            return 0; // Cell is outside the board boundaries
        }
    }

    return 1; // All cells fit within the board
}

// Process Begin packet
int process_begin_packet(char *buffer, int player_num, int *width, int *height, int player_ready[]) {
    if (player_num == 1) {
        // Player 1 should send "B <width> <height>"
        int parsed_count = sscanf(buffer, "B %d %d", width, height);
        
        // Check if exactly two integers were read and if dimensions are valid
        if (parsed_count != 2 || *width < 10 || *height < 10) {
            return 200; // Invalid Begin packet (invalid number of parameters or dimensions)
        }
        
        // Check for extra characters after the two integers
        char extra;
        if (sscanf(buffer + 2 + snprintf(NULL, 0, "%d %d", *width, *height), " %c", &extra) == 1) {
            return 200; // Extra unexpected characters found
        }

        // Mark Player 1 as ready after valid Begin packet
        player_ready[0] = 1; 
    } else if (player_num == 2) {
        // Player 2 should only send "B" without parameters
        if (strcmp(buffer, "B") != 0) {
            return 200; // Invalid Begin packet (unexpected parameters for Player 2)
        }
        
        // Mark Player 2 as ready after valid Begin packet
        player_ready[1] = 1;
    }

    return 0; // Success
}


int is_piece_valid(GameBoard *board, TetrisPiece piece) {
    if (piece.type < 1 || piece.type > 7) {
        return 300; // Invalid piece type
    }
    if (piece.rotation < 1 || piece.rotation > 4) {
        return 301; // Invalid rotation
    }

    if (!does_piece_fit(board, piece)) {
        return 302; // Piece doesn't fit in the game board
    }

    return 0; // Valid piece
}

int do_pieces_overlap(TetrisPiece existing_piece, TetrisPiece new_piece) {
    int (*existing_offsets)[2] = shape_offsets[existing_piece.type - 1][existing_piece.rotation - 1];
    int (*new_offsets)[2] = shape_offsets[new_piece.type - 1][new_piece.rotation - 1];

    for (int i = 0; i < 4; i++) {
        int existing_row = existing_piece.row + existing_offsets[i][0];
        int existing_col = existing_piece.column + existing_offsets[i][1];

        for (int j = 0; j < 4; j++) {
            int new_row = new_piece.row + new_offsets[j][0];
            int new_col = new_piece.column + new_offsets[j][1];

            if (existing_row == new_row && existing_col == new_col) {
                return 1; // Overlap found
            }
        }
    }

    return 0; // No overlap
}

int is_piece_overlapping(PlayerState *player, TetrisPiece piece) {
    for (int i = 0; i < 5; i++) {
        if (player->pieces[i].type != 0 && do_pieces_overlap(player->pieces[i], piece)) {
            return 1; // Overlap found
        }
    }
    return 0; // No overlap
}




// Process Initialize packet


int process_initialize_packet(GameBoard *board, PlayerState *player, char *packet) {
    int type, rotation, col, row;
    int offset = 2; // Skip the "I " prefix in the packet
    char buffer[BUFFER_SIZE];
    strncpy(buffer, packet + offset, BUFFER_SIZE - offset);
    buffer[BUFFER_SIZE - offset - 1] = '\0'; // Ensure null-termination

    for (int i = 0; i < 5; i++) {
        if (sscanf(buffer, "%d %d %d %d", &type, &rotation, &col, &row) != 4) {
            return 201; // Invalid number of parameters
        }

        TetrisPiece piece = {type, rotation, col, row};

        // Validate the piece
        int validation_error = is_piece_valid(board, piece);
        if (validation_error != 0) {
            return validation_error; // Specific validation error (300, 301, 302)
        }

        if (is_piece_overlapping(player, piece)) {
            return 303; // Pieces overlap
        }

        player->pieces[i] = piece;

        // Advance the buffer to the next set of inputs
        char *remaining = strchr(buffer, ' ');
        if (!remaining) {
            return 201; // Not enough data for another piece
        }

        // Move to the next piece description
        memmove(buffer, remaining + 1, strlen(remaining));
    }

    player->is_ready = 1;
    return 0; // Success
}


int process_shoot_packet(GameBoard *board, PlayerState *target, char *packet, char *response) {
    int row, col;

    if (sscanf(packet + 2, "%d %d", &row, &col) != 2) {
        return 202; // Invalid number of parameters
    }

    if (row < 0 || row >= board->height || col < 0 || col >= board->width) {
        return 400; // Cell not in game board
    }

    if (target->hits[row][col] != 'E') {
        return 401; // Cell already guessed
    }

    if (board->grid[row][col] == 'S') { // Hit
        target->hits[row][col] = 'H';
        board->grid[row][col] = 'H';

        for (int i = 0; i < 5; i++) {
            TetrisPiece piece = target->pieces[i];
            if (piece.type == 0) continue;

            int (*offsets)[2] = shape_offsets[piece.type - 1][piece.rotation - 1];

            int piece_cells = 0, hit_cells = 0;
            for (int j = 0; j < 4; j++) {
                int pr = piece.row + offsets[j][0];
                int pc = piece.column + offsets[j][1];

                if (pr >= 0 && pr < board->height && pc >= 0 && pc < board->width) {
                    piece_cells++;
                    if (target->hits[pr][pc] == 'H') hit_cells++;
                }
            }
            if (piece_cells == hit_cells) {
                target->pieces[i].type = 0; // Mark ship as sunk
                target->ships_remaining--;
            }
        }

        snprintf(response, BUFFER_SIZE, "R %d H", target->ships_remaining);
    } else {
        target->hits[row][col] = 'M';
        board->grid[row][col] = 'M';
        snprintf(response, BUFFER_SIZE, "R %d M", target->ships_remaining);
    }

    return 0; // Success
}

void process_query_packet(PlayerState *player, char *response) {
    char history[BUFFER_SIZE] = "";
    snprintf(response, BUFFER_SIZE, "G %d ", player->ships_remaining);

    for (int r = 0; r < *(player->hits[0]); r++) {
        for (int c = 0; c < *(player->hits[0]); c++) {
            if (player->hits[r][c] != 'E') {
                char cell[16];
                snprintf(cell, sizeof(cell), "%c %d %d ", player->hits[r][c], c, r);
                strcat(history, cell);
            }
        }
    }

    strcat(response, history);
}

int main() {
    int server_fd1, server_fd2, client_fd1, client_fd2;
    struct sockaddr_in address1, address2;
    int opt = 1;
    socklen_t addrlen = sizeof(struct sockaddr_in);
    char buffer[BUFFER_SIZE];
    int width = 0, height = 0; // Board dimensions
    int player_ready[2] = {0, 0}; // Track readiness of players

    // Create sockets
    server_fd1 = socket(AF_INET, SOCK_STREAM, 0);
    server_fd2 = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd1 == 0 || server_fd2 == 0) {
        perror("[Server] Socket creation failed");
        exit(EXIT_FAILURE);
    }

    setsockopt(server_fd1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd2, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configure addresses
    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PORT1);

    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = INADDR_ANY;
    address2.sin_port = htons(PORT2);

    // Bind sockets
    if (bind(server_fd1, (struct sockaddr *)&address1, sizeof(address1)) < 0 ||
        bind(server_fd2, (struct sockaddr *)&address2, sizeof(address2)) < 0) {
        perror("[Server] Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for connections
    listen(server_fd1, 3);
    listen(server_fd2, 3);

    printf("[Server] Waiting for Player 1 on port %d\n", PORT1);
    printf("[Server] Waiting for Player 2 on port %d\n", PORT2);

    // Accept connections
    client_fd1 = accept(server_fd1, (struct sockaddr *)&address1, &addrlen);
    client_fd2 = accept(server_fd2, (struct sockaddr *)&address2, &addrlen);

    if (client_fd1 < 0 || client_fd2 < 0) {
        perror("[Server] Accept failed");
        exit(EXIT_FAILURE);
    }


    PlayerPhase player1_phase = PHASE_BEGIN;
    PlayerPhase player2_phase = PHASE_BEGIN;

    printf("[Server] Both players connected. Starting game setup...\n");

    // Initialize game board and player states
    GameBoard *game_board = NULL;
    PlayerState *player1 = NULL;
    PlayerState *player2 = NULL;

    
    // Main gameplay loop
    while (1) {
        for (int i = 0; i < 2; i++) {
            int client_fd = (i == 0) ? client_fd1 : client_fd2;
            PlayerState *opponent = (i == 0) ? player2 : player1;
            PlayerPhase *current_phase = (i == 0) ? &player1_phase : &player2_phase;

            memset(buffer, 0, BUFFER_SIZE);
            read(client_fd, buffer, BUFFER_SIZE);
            printf("[Player %d] Sent: %s\n", i + 1, buffer);

            // Handle Forfeit packet first, regardless of phase
            if (strncmp(buffer, "F", 1) == 0) {
                process_forfeit_packet(i + 1, client_fd1, client_fd2, game_board, player1, player2);
                return 0; // Exit the loop and terminate the server after processing Forfeit
            }

            // Handle other packets based on the current phase
            if (*current_phase == PHASE_BEGIN) {
                if (strncmp(buffer, "B", 1) == 0) {
                    int error = process_begin_packet(buffer, i + 1, &width, &height, player_ready);
                    if (error) {
                        send_error(client_fd, error, i + 1);
                    } else {
                        send_acknowledgment(client_fd, i + 1);
                        if (player_ready[0] && player_ready[1]) {
                            *current_phase = PHASE_INITIALIZE;
                            game_board = initialize_board(width, height);
                            player1 = initialize_player_state(width, height);
                            player2 = initialize_player_state(width, height);
                        }
                    }
                } else {
                    send_error(client_fd, 100, i + 1); // Invalid packet type
                }
            } else if (*current_phase == PHASE_INITIALIZE) {
                if (strncmp(buffer, "I", 1) == 0) {
                    int error = process_initialize_packet(game_board, opponent, buffer);
                    if (error) {
                        send_error(client_fd, error, i + 1);
                    } else {
                        send_acknowledgment(client_fd, i + 1);
                        *current_phase = PHASE_GAMEPLAY;
                    }
                } else {
                    send_error(client_fd, 101, i + 1); // Invalid packet type
                }
            } else if (*current_phase == PHASE_GAMEPLAY) {
                if (strncmp(buffer, "S", 1) == 0) {
                    char response[BUFFER_SIZE];
                    int error = process_shoot_packet(game_board, opponent, buffer, response);
                    if (error) {
                        send_error(client_fd, error, i + 1);
                    } else {
                        send(client_fd, response, strlen(response), 0);
                    }
                } else if (strncmp(buffer, "Q", 1) == 0) {
                    char response[BUFFER_SIZE];
                    process_query_packet(opponent, response);
                    send(client_fd, response, strlen(response), 0);
                } else {
                    send_error(client_fd, 102, i + 1); // Invalid packet type
                }
            }
        }
    }

    // Cleanup resources
    printf("[Server] Cleaning up resources and shutting down...\n");
    if (game_board) free_board(game_board);
    if (player1) free_player_state(player1, height);
    if (player2) free_player_state(player2, height);
    close(client_fd1);
    close(client_fd2);
    close(server_fd1);
    close(server_fd2);

    return 0;
}







