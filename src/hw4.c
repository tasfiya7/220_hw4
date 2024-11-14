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
#define ERR_INVALID_INIT "E 201"
#define ERR_SHAPE_OUT_OF_RANGE "E 300"
#define ERR_ROTATION_OUT_OF_RANGE "E 301"
#define ERR_SHIP_OVERLAP "E 303"
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
    setup_server();
    accept_connections();

    // Main game loop to handle packets and alternate turns
    int current_turn = 1; // Start with Player 1
    char buffer[BUFFER_SIZE] = {0};
    while (1) {
        int conn_fd = (current_turn == 1) ? conn1_fd : conn2_fd;
        memset(buffer, 0, BUFFER_SIZE);
        int nbytes = read(conn_fd, buffer, BUFFER_SIZE);
        if (nbytes <= 0) {
            perror("[Server] read failed");
            break;
        }
        parse_packet(conn_fd, buffer, current_turn);

        // Alternate turns after a valid shoot or forfeit packet
        if (buffer[0] == PACKET_SHOOT || buffer[0] == PACKET_FORFEIT) {
            current_turn = (current_turn == 1) ? 2 : 1;
        }
    }

    // Clean up
    for (int i = 0; i < game_board.height; i++) {
        free(game_board.cells[i]);
        free(player1_state.guesses[i]);
        free(player2_state.guesses[i]);
    }
    free(game_board.cells);
    free(player1_state.guesses);
    free(player2_state.guesses);

    close(conn1_fd);
    close(conn2_fd);
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
        case PACKET_BEGIN:
            handle_begin(conn_fd, buffer, player);
            break;
        case PACKET_INIT:
            handle_init(conn_fd, buffer, player);
            break;
        case PACKET_SHOOT:
            handle_shoot(conn_fd, buffer, player);
            break;
        case PACKET_QUERY:
            handle_query(conn_fd, player);
            break;
        case PACKET_FORFEIT:
            handle_forfeit(conn_fd, player);
            break;
        default:
            send_response(conn_fd, "E 100");
            break;
    }
}

void handle_begin(int conn_fd, char *packet, int player) {
    if (player == 1) {
        int width, height;
        
        // Check for correct format and values in Player 1's Begin packet
        if (sscanf(packet, "B %d %d", &width, &height) != 2 || width < MIN_BOARD_SIZE || height < MIN_BOARD_SIZE) {
            send_response(conn_fd, "E 200"); // Send error if parameters are invalid or out of range
            return;
        }
        
        // Initialize the game board and player states if parameters are valid
        initialize_game_board(width, height);
        initialize_player_state(&player1_state);
        initialize_player_state(&player2_state);
        send_response(conn_fd, "A"); // Send acknowledgment for a valid Begin packet
    } else if (player == 2) {
        // Player 2 should only send "B" with no additional parameters
        if (strcmp(packet, "B") != 0) {
            send_response(conn_fd, "E 200"); // Send error if Player 2's Begin packet is invalid
        } else {
            send_response(conn_fd, "A"); // Send acknowledgment for a valid Player 2 Begin packet
        }
    }
}

void handle_init(int conn_fd, char *packet, int player) {
    // Parse and validate the piece initialization packet
    int type, rotation, col, row;
    if (sscanf(packet + 2, "%d %d %d %d", &type, &rotation, &col, &row) != 4) {
        send_response(conn_fd, ERR_INVALID_INIT);
        return;
    }

    // Ensure the piece fits within bounds and does not overlap with other pieces.
    if (col < 0 || col >= game_board.width || row < 0 || row >= game_board.height) {
        send_response(conn_fd, ERR_SHAPE_OUT_OF_RANGE);
        return;
    }

    // Place the piece and mark cells on the board
    if (game_board.cells[row][col] == 1) {
        send_response(conn_fd, ERR_SHIP_OVERLAP);
        return;
    } else {
        game_board.cells[row][col] = 1; // Marking part of the ship
    }
    send_response(conn_fd, "A"); // Acknowledge successful placement
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
    // Generate query response showing history of hits and misses
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
