#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PLAYER1_PORT 2201
#define PLAYER2_PORT 2202
#define BUFFER_SIZE 1024
#define MIN_BOARD_SIZE 10
#define MAX_SHIPS 5

// Error Codes
#define ERR_INVALID_BEGIN "E 200"
#define ERR_INVALID_PACKET_TYPE "E 100"
#define ERR_EXPECTED_BEGIN "E 102"
#define ERR_SHOOT_OUT_OF_BOUNDS "E 400"

// Packet Types
#define PACKET_BEGIN 'B'
#define PACKET_INIT 'I'
#define PACKET_SHOOT 'S'
#define PACKET_QUERY 'Q'
#define PACKET_FORFEIT 'F'

// Structures for game state
typedef struct {
    int width;
    int height;
    int **cells; // 2D array for the board cells (0 = empty, 1 = ship part, 2 = hit, 3 = miss)
} GameBoard;

typedef struct {
    int ships_remaining;
    int hits;
    int **guesses; // Track guesses (0 = no guess, 1 = miss, 2 = hit)
} PlayerState;

// Global Variables
GameBoard game_board;
PlayerState player1_state, player2_state;
int game_initialized = 0; // Tracks if the game has been successfully initialized

int player1_fd, player2_fd, conn1_fd, conn2_fd;

// Function Prototypes
void setup_server();
void accept_connections();
void parse_packet(int conn_fd, char *buffer, int player);
void handle_begin(int conn_fd, char *packet, int player);
void handle_init(int conn_fd, char *packet, int player);
void handle_shoot(int conn_fd, char *packet, int player);
void handle_query(int conn_fd, int player);
void handle_forfeit(int conn_fd, int player);
void send_response(int conn_fd, const char *response);

void initialize_game_board(int width, int height) {
    game_board.width = width;
    game_board.height = height;
    game_board.cells = (int **)malloc(height * sizeof(int *));
    for (int i = 0; i < height; i++) {
        game_board.cells[i] = (int *)calloc(width, sizeof(int));
    }
}

void initialize_player_state(PlayerState *player) {
    player->ships_remaining = MAX_SHIPS; // Assume 5 ships per player
    player->hits = 0;
    player->guesses = (int **)malloc(game_board.height * sizeof(int *));
    for (int i = 0; i < game_board.height; i++) {
        player->guesses[i] = (int *)calloc(game_board.width, sizeof(int));
    }
}

int main() {
    int server_fd1, server_fd2, conn1_fd, conn2_fd;
    struct sockaddr_in address1, address2;
    int opt = 1;
    int addrlen1 = sizeof(address1), addrlen2 = sizeof(address2);

    // Create and bind sockets for Player 1 and Player 2
    if ((server_fd1 = socket(AF_INET, SOCK_STREAM, 0)) == 0 ||
        (server_fd2 = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }
    setsockopt(server_fd1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd2, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PLAYER1_PORT);
    bind(server_fd1, (struct sockaddr *)&address1, sizeof(address1));
    listen(server_fd1, 3);

    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = INADDR_ANY;
    address2.sin_port = htons(PLAYER2_PORT);
    bind(server_fd2, (struct sockaddr *)&address2, sizeof(address2));
    listen(server_fd2, 3);

    printf("Server ready on ports %d and %d.\n", PLAYER1_PORT, PLAYER2_PORT);

    // Accept connections for Player 1 and Player 2
    if ((conn1_fd = accept(server_fd1, (struct sockaddr *)&address1, (socklen_t*)&addrlen1)) < 0 ||
        (conn2_fd = accept(server_fd2, (struct sockaddr *)&address2, (socklen_t*)&addrlen2)) < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }

    printf("Player 1 and Player 2 connected.\n");

    // Main game loop to handle packets for each player
    int current_turn = 1;
    char buffer[BUFFER_SIZE] = {0};
    
    while (1) {
        // Set the active connection based on the current turn
        int conn_fd = (current_turn == 1) ? conn1_fd : conn2_fd;

        // Clear buffer and read incoming packet
        memset(buffer, 0, BUFFER_SIZE);
        int nbytes = read(conn_fd, buffer, BUFFER_SIZE);

        // Check for read error or client disconnect
        if (nbytes <= 0) {
            perror("[Server] Read failed or client disconnected.");
            break;
        }

        printf("[Server] Received packet from Player %d: %s\n", current_turn, buffer);
        
        // Parse and handle each packet
        parse_packet(conn_fd, buffer, current_turn);

        // Move to next player for the next packet processing
        current_turn = (current_turn == 1) ? 2 : 1;
    }

    close(conn1_fd);
    close(conn2_fd);
    close(server_fd1);
    close(server_fd2);
    return 0;
}


void setup_server() {
    struct sockaddr_in address1, address2;
    int opt = 1;
    int addrlen = sizeof(address1);

    // Player 1 socket
    if ((player1_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    setsockopt(player1_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address1.sin_family = AF_INET;
    address1.sin_addr.s_addr = INADDR_ANY;
    address1.sin_port = htons(PLAYER1_PORT);
    bind(player1_fd, (struct sockaddr *)&address1, sizeof(address1));
    listen(player1_fd, 3);

    // Player 2 socket
    if ((player2_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    setsockopt(player2_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    address2.sin_family = AF_INET;
    address2.sin_addr.s_addr = INADDR_ANY;
    address2.sin_port = htons(PLAYER2_PORT);
    bind(player2_fd, (struct sockaddr *)&address2, sizeof(address2));
    listen(player2_fd, 3);

    printf("Server ready on ports %d and %d.\n", PLAYER1_PORT, PLAYER2_PORT);
}

void accept_connections() {
    struct sockaddr_in address1, address2;
    int addrlen1 = sizeof(address1), addrlen2 = sizeof(address2);

    if ((conn1_fd = accept(player1_fd, (struct sockaddr *)&address1, (socklen_t*)&addrlen1)) < 0) {
        perror("accept failed for Player 1");
        exit(EXIT_FAILURE);
    }
    printf("Player 1 connected.\n");

    if ((conn2_fd = accept(player2_fd, (struct sockaddr *)&address2, (socklen_t*)&addrlen2)) < 0) {
        perror("accept failed for Player 2");
        exit(EXIT_FAILURE);
    }
    printf("Player 2 connected.\n");
}

void parse_packet(int conn_fd, char *buffer, int player) {
    char packet_type = buffer[0];

    switch (packet_type) {
        case 'B':
            handle_begin(conn_fd, buffer, player);
            break;
        case 'S':
            break;
        case 'Q':
            if (!game_initialized) {
                send_response(conn_fd, ERR_EXPECTED_BEGIN); // Error if game isn't initialized yet
            }
            // Additional handling for "Shoot" and "Query" here if initialized
            break;
        case 'F':
            handle_forfeit(conn_fd, player); // Allow "Forfeit" without checking initialization
            break;
        default:
            send_response(conn_fd, ERR_INVALID_PACKET_TYPE); // Unknown packet type
            break;
    }
}


void handle_begin(int conn_fd, char *packet, int player) {
    if (player == 1) {
        int width, height;

        // Strict format and range validation for Player 1's "Begin" packet
        if (sscanf(packet, "B %d %d", &width, &height) != 2 || width < MIN_BOARD_SIZE || height < MIN_BOARD_SIZE) {
            send_response(conn_fd, ERR_INVALID_BEGIN); // Invalid format or out of range
            return;
        }

        // Initialize game state here (skipping actual initialization for brevity)
        game_initialized = 1; // Mark game as initialized
        send_response(conn_fd, "A"); // Send acknowledgment
    } else if (player == 2) {
        // Player 2 should only send "B" without any parameters
        if (strcmp(packet, "B") != 0) {
            send_response(conn_fd, ERR_INVALID_BEGIN); // Invalid format for Player 2
        } else {
            send_response(conn_fd, "A"); // Acknowledge valid "Begin" packet for Player 2
        }
    }
}



void handle_init(int conn_fd, char *packet, int player) {
    int type, rotation, col, row;
    if (sscanf(packet + 2, "%d %d %d %d", &type, &rotation, &col, &row) != 4) {
        send_response(conn_fd, ERR_INVALID_BEGIN);
        return;
    }
    if (col < 0 || col >= game_board.width || row < 0 || row >= game_board.height) {
        send_response(conn_fd, ERR_SHOOT_OUT_OF_BOUNDS);
        return;
    }
    if (game_board.cells[row][col] == 1) {
        send_response(conn_fd, ERR_INVALID_BEGIN);
        return;
    } else {
        game_board.cells[row][col] = 1;
    }
    send_response(conn_fd, "A");
}

void handle_shoot(int conn_fd, char *packet, int player) {
    int row, col;
    PlayerState *opponent_state = (player == 1) ? &player2_state : &player1_state;
    if (sscanf(packet + 2, "%d %d", &row, &col) != 2 || row >= game_board.height || col >= game_board.width) {
        send_response(conn_fd, ERR_SHOOT_OUT_OF_BOUNDS);
        return;
    }
    if (game_board.cells[row][col] == 1) {
        game_board.cells[row][col] = 2;
        opponent_state->hits++;
        if (opponent_state->hits == MAX_SHIPS) {
            send_response(conn_fd, "R 0 H");
            send_response(conn1_fd, "H 1");
            send_response(conn2_fd, "H 0");
            close(conn1_fd);
            close(conn2_fd);
            exit(0);
        } else {
            send_response(conn_fd, "R <remaining ships> H");
        }
    } else {
        game_board.cells[row][col] = 3;
        send_response(conn_fd, "R <remaining ships> M");
    }
}

void handle_query(int conn_fd, int player) {
    char response[BUFFER_SIZE];
    snprintf(response, BUFFER_SIZE, "G %d M 0 0 H 1 1", player == 1 ? player2_state.ships_remaining : player1_state.ships_remaining);
    send_response(conn_fd, response);
}

void handle_forfeit(int conn_fd, int player) {
    send_response(conn1_fd, (player == 1) ? "H 0" : "H 1");
    send_response(conn2_fd, (player == 2) ? "H 0" : "H 1");
    close(conn1_fd);
    close(conn2_fd);
    exit(0);
}

void send_response(int conn_fd, const char *response) {
    send(conn_fd, response, strlen(response), 0);
}
