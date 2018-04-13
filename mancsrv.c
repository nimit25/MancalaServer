#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAXNAME 80  /* maximum permitted name size, not including \0 */
#define NPITS 6  /* number of pits on a side, not including the end pit */
#define NPEBBLES 4 /* initial number of pebbles per pit */
#define MAXMESSAGE (MAXNAME + 50) /* initial number of pebbles per pit */

int port = 55248;
int listenfd;

int turn = 1;
int prompt_given = 0;
int num_of_players = 0;

struct player {
    int fd;
    char name[MAXNAME+1];
    int pits[NPITS+1];// pits[0..NPITS-1] are the regular pits
    // pits[NPITS] is the end pit
    //other stuff undoubtedly needed here
    char partial_turn[MAXMESSAGE + 1];
    int turn_length;
    struct player *prev;
    int name_length;
    int player_num; // the turn this player gets
    int username_set; // 0 for set and 1 for not
    char partial_name[MAXNAME + 1];
    struct player *next;
};
struct player *playerlist = NULL;


extern void parseargs(int argc, char **argv);
extern void makelistener();
extern int compute_average_pebbles();
extern int game_is_over();  /* boolean */
extern void broadcast(char *s);  /* you need to write this one */

/*
 * finds the player whose turn is the first one from the playerlist and assigns it to player1.
 */
void find_first_player(struct player **player1){
    struct player *curr = playerlist;
    while (curr != NULL){
        if (curr->next == NULL){
            *player1 = curr;
            break;
        }
        curr = curr->next;
    }
}
/*
 * Returns void and increments the number of pebbles in each of the player1's pits starting from start_pebble.
 * if can_drop_in_end is 0, then you drop a pebble in the end pit otherwise you don't
 */
void pit_allocater(struct player *player1, int *num_pebbles, int start_pebble, int can_drop_in_end){
    for (int i = start_pebble; i < NPITS && *num_pebbles != 0; i++){
        player1->pits[i]++;
        *num_pebbles = *num_pebbles - 1;
    }
    if (can_drop_in_end == 0 && *num_pebbles != 0){
        player1->pits[NPITS]++;
        *num_pebbles = *num_pebbles - 1;
    }

}


/*
 * Distributes the num_pebbles according to the rules to Mancala game to different players till num_pebbles becomes 0.
 */
void pebbles_distributer(int *num_pebbles){
    struct player *curr = NULL;
    find_first_player(&curr);
    if (*num_pebbles > 0) {
        while (curr != NULL) {
            if (curr->username_set == 0) {
                pit_allocater(curr, num_pebbles, 0, 0);
            }
            if (*num_pebbles == 0) {
                break;
            }
            curr = curr->prev;
        }
    }
    if (*num_pebbles > 0){
        pebbles_distributer(num_pebbles);
    }
}


/*
 * Broadcasts the message message to every player except player1.
 */

void broadcast_execpt(struct player *player1, char *message){
    struct player *curr = playerlist;
    while (curr != NULL && curr->username_set == 0){
        if (curr != player1){
            write(curr->fd, message, strlen(message) + 1);
        }
        curr = curr->next;
    }

}

/*
 * Disconnects the player player1 from the game.
 */

int disconnect_a_player(struct player *player1){
    if (turn == player1->player_num){
        turn++;
    } else if (turn > player1->player_num){
        turn--;
    }
    struct player *curr = playerlist;
    while (curr!= NULL){
        if (curr == player1){
            if ( curr->prev) {
                curr->prev->next = curr->next;
                if (curr->next) {
                    curr->next->prev = curr->prev;
                }
            } else {
                playerlist = curr->next;
                if (playerlist != NULL) {
                    playerlist->prev = NULL;
                }
            }
            int client_fd = player1->fd;
            close(player1->fd);
            num_of_players--;
            char msg[MAXMESSAGE +1];
            if (player1->username_set == 0) {
                sprintf(msg, "Player %s disconnected\n", player1->name);
                printf("%s", msg);
            } else {
                printf("A Player, whose name was not initialized, disconnected\n");
            }
            return client_fd;
        } else {
            if (curr->player_num > player1->player_num){ // applicable only if the player joined after player1
                curr->player_num--; // reduce the "rank" of the player so that the turn alligns
            }
        }
        curr = curr->next;
    }
    return -1;
}

/*
 * Broadcasts the game board to every player. Returns void.
 */

void state_of_the_game(){
    char buf[1024];
    buf[0] = '\0';
    struct player *curr = playerlist;
    while (curr != NULL) {
        if (curr->username_set == 0) {

            strcat(buf, curr->name);
            strcat(buf, ": ");
            char buf2[32];
            for (int i = 0; i < NPITS; i++) {
                sprintf(buf2, "[%d]%d ", i, curr->pits[i]);
                buf2[5] = '\0';
                strcat(buf, buf2);
            }
            sprintf(buf2, " [end pit]%d\r\n", curr->pits[NPITS]);
            strcat(buf, buf2);

        }
        curr = curr->next;
    }
    broadcast(buf);
}


/*
 * Accepts connection from a player and adds the player to the playerlist.
 */

int accept_connection(int fd) {

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }
    num_of_players++;

    char welcome[] = "Welcome to Mancala. What is your name?\r\n";
    write(client_fd, welcome, strlen(welcome));
    struct player *player1 = malloc(sizeof(struct player));
    player1->fd = client_fd;
    player1->username_set = 1;
    player1->prev = NULL;
    player1->player_num = num_of_players;
    int num_pebbles = compute_average_pebbles();
    for ( int i = 0; i < NPITS; i++) {
        player1->pits[i] = num_pebbles;
    }
    if ( playerlist == NULL){
        player1->next = playerlist;
        playerlist = player1;
    } else {
        player1->next = playerlist;
        playerlist->prev = player1;
        playerlist = player1;
    }
    printf("Player %d has joined the game\n", num_of_players);
    return client_fd;
}

/*
 * Reads from the player depending on whether it's the player's turn or not. Hence, writes and prints the appropriate
 * message to the players and server, respectively.
 */
int read_from(struct player *player1){
    if (player1->username_set == 1) {
        char buf[MAXNAME + 1] = {'\0'};
        // How many bytes currently in buffer?
        int room = sizeof(buf);  // How many bytes remaining in buffer?
        char *after = malloc(sizeof(char *));
        int nbytes = 0;
        nbytes = read(player1->fd, after, room);
        player1->name_length += nbytes;
        if (player1->name_length > MAXNAME){
            return disconnect_a_player(player1);
        }
        strcat(player1->partial_name, after);
        for (int i = 0; i < strlen(player1->partial_name); i++) {
            if (player1->partial_name[i] == '\n' || player1->partial_name[i] == '\r') {
                player1->partial_name[i] = '\0';
                strcpy(player1->name, player1->partial_name);
                if (strcmp(player1->name, "") == 0){
                    return disconnect_a_player(player1);
                }
                struct player *curr = playerlist;
                while (curr != NULL) {
                    if (strcasecmp(player1->name, curr->name) == 0) {// Ask on piazza about case sensitivity
                        if (player1 != curr) {
                            return disconnect_a_player(player1);
                        }
                    }
                    curr = curr->next;
                }
                player1->username_set = 0;
                free(after);
                char msg[MAXMESSAGE +1];
                sprintf(msg, "Player %s has joined the game\r\n", player1->name);
                broadcast(msg);
                sprintf(msg, "Player %s has joined the game\n", player1->name);
                printf("%s", msg);
                state_of_the_game();
                break;
            }
        }
        if (nbytes == 0){
            return disconnect_a_player(player1);
        }
    }
    else if (player1->player_num == turn && player1->username_set == 0) {
        char buf[MAXMESSAGE + 1] = {'\0'}; // make a partial turn in the struct
        // How many bytes currently in buffer?
        int room = sizeof(buf);  // How many bytes remaining in buffer?
        char after[100];
        int nbytes;// use select here and mark the thing according and then when its called again do the thing
        // have have to use select here look it up to use it
        nbytes = read(player1->fd, after, room);  // change this to get the struct //
        player1->turn_length+= nbytes;
        if (player1->turn_length > MAXMESSAGE + 1){
            char msg[] = "Please enter a valid turn\r\n";
            write(player1->fd,msg, sizeof(msg) );
            return 0;
        }

        strcat(player1->partial_turn, after);
        for (int i = 0; i < strlen(player1->partial_turn); i++) {
            if (player1->partial_turn[i] == '\n' || player1->partial_turn[i] == '\r') {
                player1->partial_turn[i] = '\0';
                int move_pit = (int) strtol(player1->partial_turn, NULL, 10);
                if ( move_pit > NPITS || move_pit < 0){ // error checking for the number provided by the user
                    char msg[] = "Invalid move. Please enter your move again.\r\n";
                    write(player1->fd, msg, sizeof(msg));
                    player1->partial_turn[0] = '\0';
                    return 0;
                } else if (player1->pits[move_pit] == 0){
                    char msg[] = "Invalid move. Please enter your move again.\r\n";
                    write(player1->fd, msg, sizeof(msg));
                    player1->partial_turn[0] = '\0';
                    return 0;
                }
                player1->partial_turn[0] = '\0';
                printf("The move is %d\n", move_pit);
                int num_pebbles = player1->pits[move_pit];
                player1->pits[move_pit] = 0;
                pit_allocater(player1, &num_pebbles, move_pit + 1, 1); //  because we need to check for the turn
                if (num_pebbles > 0) {
                    player1->pits[NPITS]++;
                    num_pebbles--;
                    if (num_pebbles == 0) { // player gets another turn
                        turn--;
                    } else {
                        struct player *curr = NULL;
                        find_first_player(&curr);// get the first turn
                        while (curr) {
                            if (curr->player_num > player1->player_num && curr->username_set == 0) { // distribute only if the player's turn is after player1's turn
                                pit_allocater(curr, &num_pebbles, 0, 0);
                            }
                            if (num_pebbles == 0) {
                                break;
                            }
                            curr = curr->prev;
                        }
                        if (num_pebbles != 0){ // if its still not 0, then distribute to the players from the start and loop over
                            pebbles_distributer(&num_pebbles);
                        }
                    }
                }
                if (num_pebbles != 0 ){
                    printf("should not reach here\n");
                }
                char msg[MAXMESSAGE + 1];
                sprintf(msg, "Player %s moved pebbles from pit %d\r\n", player1->name, move_pit);
                broadcast(msg);
                sprintf(msg, "Player %s moved pebbles from pit %d\n", player1->name, move_pit);
                printf("%s",msg);
                state_of_the_game();
                prompt_given = 0;
                turn = turn + 1;
                break;
            }
        }
        if (nbytes == 0){
            prompt_given = 0;// if the player enters an empty string (remains)
            return disconnect_a_player(player1);
        }


    }
    else if (player1->player_num != turn && player1->username_set != 1) {
        char msg[] = "It's not your move\r\n";
        char buf[MAXMESSAGE + 1] = {'\0'};
        int inbuf = 0;
        int room = sizeof(buf);
        char after[100];
        int nbytes;
        nbytes = read(player1->fd, after, room);
        inbuf += nbytes;
        if ( inbuf == 0){ // if the player disconnects (will remain here)
            return disconnect_a_player(player1);
        }
        write(player1->fd,msg, sizeof(msg) );
    }
    return 0;
}



int main(int argc, char **argv) {
    char msg[MAXMESSAGE];

    parseargs(argc, argv);
    makelistener();
    int max_fd = listenfd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(listenfd, &all_fds);


    while (!game_is_over()) {
        // select updates the fd_set it receives, so we always use a copy and retain the original.
        struct player *curr = playerlist;
        while (curr != NULL){
            char buf[32];
            buf[0] = '\0';
            if (curr->player_num == turn && curr->username_set == 0 && prompt_given == 0) {
                write(curr->fd, "Your move?\r\n", 13);
                prompt_given++;
                sprintf(buf, "It's %s's move.\r\n", curr->name);
                broadcast_execpt(curr, buf);
            }
            curr = curr->next;
        }
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(listenfd, &listen_fds)) {
            int client_fd = accept_connection(listenfd);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            if (client_fd != -1) {
                FD_SET(client_fd, &all_fds);
            }
        }
        int client = 0;
        curr = playerlist;
        while (curr != NULL) {
            if (FD_ISSET(curr->fd, &listen_fds)) {
                int client_closed = read_from(curr);
                if (client_closed > 0) {
                    FD_CLR(client_closed, &all_fds);
                    client = client_closed;
                } else {

                }
            }
            if (curr != NULL) {
                struct player *to_be_freed = curr;// if a player disconnects then doing curr = curr->next can give a segmentation fault
                curr = curr->next;
                if (client > 0){
                    free(to_be_freed);
                    client = 0;
                }
            }
        }
        if (turn > num_of_players){ // if turn has reached the last person then reset the turn
            turn = 1;
        }


    }

    broadcast("Game over!\r\n");
    printf("Game over!\n");
    for (struct player *p = playerlist; p; p = p->next) {
        int points = 0;
        for (int i = 0; i <= NPITS; i++) {
            points += p->pits[i];
        }
        printf("%s has %d points\r\n", p->name, points);
        snprintf(msg, MAXMESSAGE, "%s has %d points\r\n", p->name, points);
        broadcast(msg);
    }
    return 0;
}


void parseargs(int argc, char **argv) {
    int c, status = 0;
    while ((c = getopt(argc, argv, "p:")) != EOF) {
        switch (c) {
            case 'p':
                port = strtol(optarg, NULL, 0);
                break;
            default:
                status++;
        }
    }
    if (status || optind != argc) {
        fprintf(stderr, "usage: %s [-p port]\n", argv[0]);
        exit(1);
    }
}


void makelistener() {
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    int on = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                   (const char *) &on, sizeof(on)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    memset(&r, '\0', sizeof(r));
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&r, sizeof(r))) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
}



/* call this BEFORE linking the new player in to the list */
int compute_average_pebbles() {
    struct player *p;
    int i;

    if (playerlist == NULL) {
        return NPEBBLES;
    }

    int nplayers = 0, npebbles = 0;
    for (p = playerlist; p; p = p->next) {
        nplayers++;
        for (i = 0; i < NPITS; i++) {
            npebbles += p->pits[i];
        }
    }
    return ((npebbles - 1) / nplayers / NPITS + 1);  /* round up */
}


int game_is_over() { /* boolean */
    int i;

    if (!playerlist) {
        return 0;  /* we haven't even started yet! */
    }

    for (struct player *p = playerlist; p; p = p->next) {
        int is_all_empty = 1;
        for (i = 0; i < NPITS; i++) {
            if (p->pits[i]) {
                is_all_empty = 0;
            }
        }
        if (is_all_empty) {
            return 1;
        }
    }
    return 0;
}
/*
 * Writes a message s to every player connected to the server.
 */
void broadcast(char *s) {
    struct player *curr = playerlist;
    while (curr != NULL) {
        write(curr->fd, s, strlen(s) + 1);
        curr = curr->next;
    }
}
