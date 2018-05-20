
#include "structures.h"
#include <pthread.h>

#define QLEN 5
#define BUFSIZE 256
#define TIMEOUT 60
#define MYFREE(x) free(x); x = NULL;

globs GB; // struct containing global data
player *players[MAX_CLIENTS];
group *groups[MAX_GROUPS];
question **questions[MAX_GROUPS];
file *files[MAX_GROUPS];
fd_set afds;
fd_set rfds;
int nfds;

pthread_mutex_t mtx_numclients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mtx_numplayers[MAX_GROUPS];
pthread_mutex_t mtx_adminwait[MAX_GROUPS];
pthread_cond_t cond_adminwait[MAX_GROUPS];

int status[MAX_CLIENTS] = {0}; // 0 - empty, 1 - connected, 2 - player, 3 - admin

int passivesock(char *service, char *protocol, int qlen, int *rport);
int move_user(int sock, fd_set *src, fd_set *dest, int *snfdsp, int *dnfdsp);
ssize_t sock_send(int sock, char *buf, size_t size, fd_set *set, int *nfdsp);
ssize_t sock_get(int sock, char *buf, size_t size, fd_set *set, int *nfdsp);
int map_msg(char *msg);
void msg_norm(char* msg);
void *group_t(void *vargs);

int main(int argc, char *argv[])
{
    /* initialization */
    GB.nclients = 0;
    GB.ngroups = 0;

    for (int i = 0; i < MAX_GROUPS; i++)
    {
        pthread_mutex_init(&mtx_numplayers[i], NULL);
        pthread_mutex_init(&mtx_adminwait[i], NULL);
        pthread_cond_init(&cond_adminwait[i], NULL);
    }

    /* socket stuff */
    char *service;
    struct sockaddr_in fsin;
    int alen;
    int msock; // master socket for accepting
    int ssock; // accepted socket
    int rport = 0;
    ssize_t cc;

    /* I/O stuff */
    char buf[MAX_CLIENTS][BUFSIZE]; // ONE BUFFER FOR CLIENT
    char buf_tok[MAX_CLIENTS][BUFSIZE]; // ONE SUCH BUFFER FOR CLIENT

    /* multiplexing */
    int fd;
    char *tok;
    char *toks[4];
    struct timeval tv = {1, 0};

    size_t expected_size;
    size_t header_size;
    int group_index[MAX_CLIENTS];
    pthread_t group_thread[MAX_GROUPS];

    /* check arguments */
    switch (argc)
    {
        case 1:
            // No args? let the OS choose a port and tell the user
            rport = 1;
            break;
        case 2:
            // User provides a port? then use it
            service = argv[1];
            break;
        default:
            fprintf(stderr, "usage: server [port]\n");
            exit(-1);
    }

    msock = passivesock(service, "tcp", QLEN, &rport);

    //	Tell the user the selected port
    if (rport)
    {
        printf("server: port %d\n", rport);
        fflush(stdout);
    }

    // Set the max file descriptor being monitored
    nfds = msock + 1;

    FD_ZERO(&afds);
    FD_SET(msock, &afds);


    for (;;) // jump here after answer in GROUP_THREAD
    {
        // Reset the file descriptors you are interested in
        memcpy((char *) &rfds, (char *) &afds, sizeof(rfds));

        tv.tv_sec = 1;
        // Only waiting for sockets who are ready to read
        //  - this also includes the close event
        if (select(nfds, &rfds, NULL, NULL, &tv) < 0)
        {
            fprintf(stderr, "server select (main): %s\n", strerror(errno));
            exit(-1);
        }

        // Since we've reached here it means one or more of our sockets has something
        // that is ready to read

        // The main socket is ready - it means a new client has arrived
        if (FD_ISSET(msock, &rfds))
        {
            // we can call accept with no fear of blocking
            alen = sizeof(fsin);
            ssock = accept(msock, (struct sockaddr *) &fsin, &alen);

            if (ssock < 0)
            {
                fprintf(stderr, "accept: %s\n", strerror(errno));
                exit(-1);
            }

            /* start listening to this guy */
            move_user(ssock, NULL, &afds, NULL, &nfds);
            status[ssock] = 1; // this sock is connected and valid now

            GB.nclients++;

            printf("someone has arrived (sock %d - online: %d).\n", ssock, GB.nclients);
            fflush(stdout);

            sprintf(buf[ssock], "OPENGROUPS");
            for (int i = 0; i < MAX_GROUPS; i++)
            {
                if (groups[i] != NULL)
                {
                    char *name = groups[i]->name;
                    char *topic = groups[i]->topic;
                    int size = groups[i]->max_size;
                    int current_size = groups[i]->current_size;
                    sprintf(buf[ssock] + strlen(buf[ssock]), "|%s|%s|%d|%d", name, topic, size, current_size);
                }
            }
            sprintf(buf[ssock] + strlen(buf[ssock]), "\r\n");
            sock_send(ssock, buf[ssock], strlen(buf[ssock]), &afds, &nfds);
        }

        /*	Handle the participants requests  */
        for (fd = 0; fd < nfds; fd++)
        {
            // check every socket to see if it's in the ready set
            if (fd != msock && FD_ISSET(fd, &rfds))
            {
                // read without blocking because data is there
                if ((cc = sock_get(fd, buf[fd], BUFSIZE, &afds, &nfds)) > 0)
                {
                    buf[fd][cc] = '\0';

                    strcpy(buf_tok[fd], buf[fd]);

                    printf("%d: tokenizing...\n", fd);
                    fflush(stdout);
                    tok = strtok(buf_tok[fd], "|");
                    for (int i = 0; tok != NULL && i < 4; i++)
                    {
                        if (i > 0) printf("|");
                        fflush(stdout);
                        msg_norm(tok);
                        toks[i] = tok;
                        tok = strtok(NULL, "|");
                        printf("%s", toks[i]);
                        fflush(stdout);
                    }
                    printf("\n");
                    fflush(stdout);

                    switch (map_msg(toks[0]))
                    {
                        case 1: // GETOPENGROUPS
                            sprintf(buf[fd], "OPENGROUPS");
                            for (int i = 0; i < MAX_GROUPS; i++)
                            {
                                if (groups[i] != NULL)
                                {
                                    //printf("group at %d is there\n", i);
                                    fflush(stdout);
                                    char *name = groups[i]->name;
                                    char *topic = groups[i]->topic;
                                    int size = groups[i]->max_size;
                                    int current_size = groups[i]->current_size;
                                    sprintf(buf[fd] + strlen(buf[fd]), "|%s|%s|%d|%d", name, topic, size, current_size);
                                }
                            }
                            sprintf(buf[fd] + strlen(buf[fd]), "\r\n");
                            sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                            break;

                        case 2: // GROUP
                            printf("Test 1\n");
                            fflush(stdout);

                            if (GB.ngroups >= MAX_GROUPS)
                            {
                                strcpy(buf[fd], "BAD|too many groups\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }
                            else if (status[fd] == 2)
                            {
                                strcpy(buf[fd], "BAD|you have already created a group\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }

                            printf("Test 2\n");
                            fflush(stdout);

                            status[fd] = 3; // promote to admin

                            char *gtopic = toks[1];
                            char *gname = toks[2];
                            int max_size = (int) strtol(toks[3], NULL, 10);

                            group_index[fd] = group_find_empty_spot(groups);

                            groups[group_index[fd]] = group_create(gtopic, gname, max_size);
                            groups[group_index[fd]]->creator_fd = fd;
                            files[group_index[fd]] = malloc(sizeof(file));
                            files[group_index[fd]]->text = malloc(128 * MAX_STRING * sizeof(char));

                            strcpy(buf[fd], "SENDQUIZ\r\n");
                            sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);

                            printf("Test 3\n");
                            fflush(stdout);

                            GB.ngroups++;

                            printf("Test 4\n");
                            fflush(stdout);

                            pthread_create(&group_thread[group_index[fd]], NULL, group_t, (void *) group_index[fd]);
                            pthread_detach(group_thread[group_index[fd]]);

                            printf("Test 5 (thread created)\n");
                            fflush(stdout);

                            break;

                        case 3: // QUIZ
                            if (status[fd] != 3)
                            {
                                strcpy(buf[fd], "BAD|you are not an admin\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }
                            expected_size = (size_t) strtol(toks[1], NULL, 10);
                            header_size = strlen(toks[0]) + strlen(toks[1]) + 2;
                            size_t rem_size = header_size + expected_size - cc;
                            strcpy(files[group_index[fd]]->text, buf[fd]);

                            while (rem_size > 0)
                            {
                                // read until the end
                                if((cc = sock_get(fd, buf[fd], BUFSIZE, &afds, &nfds)) <= 0)
                                {
                                    pthread_cancel(group_thread[group_index[fd]]);
                                }
                                buf[fd][cc] = '\0';
                                rem_size -= cc;
                                strcat(files[group_index[fd]]->text, buf[fd]);
                            }

                            files[group_index[fd]]->file = fmemopen(files[group_index[fd]]->text, strlen(
                                    files[group_index[fd]]->text), "r");
                            questions[group_index[fd]] = parse_questions(files[group_index[fd]]->file, &groups[group_index[fd]]);
                            file_destroy(files[group_index[fd]]); // no longer needed
                            files[group_index[fd]] = NULL;
                            print_questions(questions[group_index[fd]], groups[group_index[fd]]->num_questions);

                            printf("group created at index %d: %s, %s, %d\n", group_index[fd],
                                   groups[group_index[fd]]->name, groups[group_index[fd]]->topic,
                                   groups[group_index[fd]]->max_size);

                            strcpy(buf[fd], "OK\r\n");
                            if (sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds) <= 0)
                            {
                                pthread_cancel(group_thread[group_index[fd]]);
                            }

                            move_user(fd, &afds, &groups[group_index[fd]]->afds, &nfds, &groups[group_index[fd]]->nfds);
                            pthread_cond_broadcast(&cond_adminwait[group_index[fd]]);
                            break;

                        case 4: // CANCEL
                            if (status[fd] != 3)
                            {
                                strcpy(buf[fd], "BAD|you are not an admin\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }
                            // we can assume that toks[1] is there
                            // we don't really need it, though, since one person can admin one group at a time
                            pthread_cancel(group_thread[group_index[fd]]);

                            status[fd] = 1;

                            strcpy(buf[fd], "OK\r\n");
                            sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                            break;

                        case 5: // JOIN
                            if (status[fd] == 3)
                            {
                                strcpy(buf[fd], "BAD|you cannot join a group\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }
                            // move this socket to the group's FD set
                            int join_index = -1;
                            char* join_name = toks[1];
                            char* join_username = toks[2];
                            for (int i = 0; i < MAX_GROUPS; i++)
                            {
                                if (groups[i] != NULL)
                                {
                                    if (strcmp(groups[i]->name, join_name) == 0)
                                    {
                                        join_index = i;
                                        break;
                                    }
                                }
                            }
                            if (join_index < 0)
                            {
                                strcpy(buf[fd], "NOGROUP\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }
                            if (groups[join_index]->current_size == groups[join_index]->max_size
                                || groups[join_index]->is_playing || !groups[join_index]->admin_joined)
                            {
                                strcpy(buf[fd], "FULL\r\n");
                                sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                                break;
                            }
                            status[fd] = 2; // promote to player
                            players[fd] = player_create(join_username);
                            players[fd]->group = groups[join_index];
                            lock(&mtx_numplayers[join_index]);
                            groups[join_index]->current_size++;
                            unlock(&mtx_numplayers[join_index]);
                            strcpy(buf[fd], "OK\r\n");
                            sock_send(fd, buf[fd], strlen(buf[fd]), &afds, &nfds);
                            move_user(fd, &afds, &groups[join_index]->afds, &nfds, &groups[join_index]->nfds);
                            break;
                    }
                }
            }
        }
    }
}

void group_cleanup(void *vargs)
{
    int group_index = (int) vargs;
    char buf[BUFSIZE];
    group* this = groups[group_index];
    question** this_questions = questions[group_index];

    pthread_cond_broadcast(&cond_adminwait[group_index]);
    unlock(&mtx_adminwait[group_index]);

    for (int fd = 0; fd < this->nfds; fd++)
    {
        // move everyone back to the main hub
        if (FD_ISSET(fd, &this->afds))
        {
            status[fd] = 1; // demote this socket
            player_destroy(players[fd]);
            players[fd] = NULL;
            move_user(fd, &this->afds, &afds, &this->nfds, &nfds);
            sprintf(buf, "ENDGROUP|%s\r\n", this->name);
            sock_send(fd, buf, strlen(buf), &afds, &nfds);
        }
    }

    // destroy the questions if they exist
    int num_questions = this->num_questions;
    for (int i = 0; i < num_questions; i++)
    {
        question_destroy(this_questions[i]);
    }
    questions[group_index] = NULL;

    printf("cleanup for group %s complete.\n", this->name);
    fflush(stdout);

    // burn the group
    group_destroy(this);
    groups[group_index] = NULL;

    return;
}

void *group_t(void *vargs)
{
    int group_index = (int) vargs;
    pthread_cleanup_push(group_cleanup, vargs);

        group* this = groups[group_index];

        FD_ZERO(&this->afds);

        lock(&mtx_adminwait[group_index]);
        while (!FD_ISSET(this->creator_fd, &this->afds))
        {
            pthread_cond_wait(&cond_adminwait[group_index], &mtx_adminwait[group_index]);
        }
        unlock(&mtx_adminwait[group_index]);

        this->admin_joined = 1;
        question **this_questions = questions[group_index];

        struct timeval tv_game = {0, 0};

        int fd, sel_val;
        char buf[BUFSIZE];
        char buf_tok[BUFSIZE];
        ssize_t cc;
        char* tok;
        char* toks[5];

        this->is_playing = 0;
        time_t start_t, end_t;
        int last_time[MAX_CLIENTS][2]; // [0] - time of last activity, [1] - time of answer

        int num_questions = this->num_questions;
        int current_question = 0;

        while (1)
        {
            this->rfds = this->afds;

            if (this->current_size == this->max_size && !this->is_playing)
            {
                // enter game mode
                this->is_playing = 1;
            }

            if (this->is_playing && !this_questions[current_question]->sent)
            {
                // normal, minute-long timeout
                tv_game.tv_sec = TIMEOUT; // 60 sec

                // send the current question
                this_questions[current_question]->sent = 1;
                char *current_question_text = print_question(this_questions, current_question+1);

                sprintf(buf, "QUES|%d|%s", (int) strlen(current_question_text), current_question_text);
                for (int fd = 0; fd < this->nfds; fd++)
                {
                    if (FD_ISSET(fd, &this->afds))
                    {
                        // broadcast to all
                        sock_send(fd, buf, strlen(buf), &this->afds, &this->nfds);
                    }
                }
            }
            else
            {
                // short timeout for updating
                tv_game.tv_sec = 1; // 1 sec
            }

            time(&start_t);

            sel_val = select(this->nfds, &this->rfds, NULL, NULL, &tv_game);

            if (sel_val < 0)
            {
                fprintf(stderr, "server select (group %s) %s\n", this->name, strerror(errno));
                exit(-1);
            }

            time(&end_t);

            /*	Handle the participants requests  */
            for (fd = 0; fd < this->nfds; fd++)
            {
                if (FD_ISSET(fd, &this->afds))
                {
                    // check every socket to see if it's in the ready set
                    if (FD_ISSET(fd, &this->rfds))
                    {
                        printf("%d: in rfds...\n", fd);
                        fflush(stdout);

                        last_time[fd][0] = (int) time(NULL);

                        // read without blocking because data is there
                        if ((cc = sock_get(fd, buf, BUFSIZE, &afds, &nfds)) > 0)
                        {
                            buf[cc] = '\0';

                            strcpy(buf_tok, buf);

                            printf("%d: tokenizing...\n", fd);
                            fflush(stdout);
                            tok = strtok(buf_tok, "|");
                            for (int i = 0; tok != NULL && i < 4; i++)
                            {
                                if (i > 0) printf("|");
                                fflush(stdout);
                                msg_norm(tok);
                                toks[i] = tok;
                                tok = strtok(NULL, "|");
                                printf("%s", toks[i]);
                                fflush(stdout);
                            }
                            printf("\n");
                            fflush(stdout);

                            switch (map_msg(toks[0]))
                            {
                                case 1: // GETOPENGROUPS
                                    sprintf(buf, "OPENGROUPS");
                                    for (int i = 0; i < MAX_GROUPS; i++)
                                    {
                                        if (groups[i] != NULL)
                                        {
                                            //printf("group at %d is there\n", i);
                                            fflush(stdout);
                                            char *name = groups[i]->name;
                                            char *topic = groups[i]->topic;
                                            int size = groups[i]->max_size;
                                            int current_size = groups[i]->current_size;
                                            sprintf(buf + strlen(buf), "|%s|%s|%d|%d", name, topic, size, current_size);
                                        }
                                    }
                                    sprintf(buf + strlen(buf), "\r\n");
                                    sock_send(fd, buf, strlen(buf), &this->afds, &this->nfds);
                                    break;

                                case 4: // CANCEL (for admin only)
                                    if (status[fd] != 3)
                                    {
                                        strcpy(buf, "BAD|you are not an admin\r\n");
                                        sock_send(fd, buf, strlen(buf), &afds, &nfds);
                                        break;
                                    }
                                    // we can assume that toks[1] is there
                                    // we don't really need it, though, since one person can admin one group at a time

                                    strcpy(buf, "OK\r\n");
                                    sock_send(fd, buf, strlen(buf), &afds, &nfds);

                                    // i'm so sorry, dear program.
                                    goto cleanup;

                                case 6: // LEAVE
                                    if (status[fd] == 3)
                                    {
                                        // make it so the group couldn't be accidentally canceled by someone
                                        this->creator_fd = -1;
                                    }

                                    status[fd] = 1; // demote this user
                                    this->current_size--;
                                    if (this->is_playing)
                                    {
                                        // reduce max_size, too
                                        this->max_size--;
                                    }
                                    player_destroy(players[fd]);
                                    players[fd] = NULL;
                                    move_user(fd, &this->afds, &afds, &this->nfds, &nfds);

                                    strcpy(buf, "OK\r\n");
                                    sock_send(fd, buf, strlen(buf), &afds, &nfds);
                                    break;

                                case 7: // ANS (only for players)
                                    if (status[fd] != 2)
                                    {
                                        strcpy(buf, "BAD|you are not a player\r\n");
                                        sock_send(fd, buf, strlen(buf), &afds, &nfds);
                                        break;
                                    }
                                    char *answer = toks[1];

                                    if (strcmp(this_questions[current_question]->correct, answer) == 0)
                                    {
                                        // correct answer
                                        if (this_questions[current_question]->winner == NULL)
                                        {
                                            // the first to answer correctly
                                            this_questions[current_question]->winner = players[fd];
                                            printf("%s answered: %s (first correct!)\n", players[fd]->name, answer);
                                            fflush(stdout);
                                            players[fd]->score += 2;
                                        }
                                        else
                                        {
                                            printf("%s answered: %s (correct!)\n", players[fd]->name, answer);
                                            fflush(stdout);
                                            players[fd]->score += 1;
                                        }
                                    }
                                    else if (strcmp("NOANS", answer) == 0)
                                    {
                                        // no answer provided
                                        printf("%s has not provided an answer\n", players[fd]->name);
                                        fflush(stdout);
                                        break;
                                    }
                                    else
                                    {
                                        // incorrect answer
                                        printf("%s answered: %s (incorrect!)\n", players[fd]->name, answer);
                                        fflush(stdout);
                                        players[fd]->score -= 1;
                                    }

                                    this_questions[current_question]->answered++;

                                    printf("(%d/%d answered)\n", this_questions[current_question]->answered,
                                           this->max_size);
                                    fflush(stdout);

                                    break;
                                default:
                                    strcpy(buf, "BAD|wrong command\r\n");
                                    sock_send(fd, buf, strlen(buf), &afds, &nfds);
                                    break;
                            }
                        }
                    }
                    else
                    {
                        // for sockets that didn't do anything
                        last_time[fd][1] = (int) time(NULL);

                        if (last_time[fd][0] != 0)
                        {
                            int time_diff = last_time[fd][1] - last_time[fd][0];
                            printf("sock %d: last access = %d s ago\n", fd, time_diff);
                            fflush(stdout);
                            if (time_diff >= TIMEOUT && this->is_playing && status[fd] != 3)
                            {
                                // kick out the user
                                status[fd] = 1; // demote this user

                                this->current_size--;
                                this->max_size--;

                                player_destroy(players[fd]);
                                players[fd] = NULL;
                                move_user(fd, &this->afds, &afds, &this->nfds, &nfds);

                                sprintf(buf, "ENDGROUP|%s\r\n", this->name);
                                sock_send(fd, buf, strlen(buf), &afds, &nfds);
                            }
                        }
                    }
                }
            }

            if (this_questions[current_question]->answered == this->max_size)
            {
                if (this_questions[current_question]->winner != NULL)
                {
                    printf("there is a winner\n");
                    fflush(stdout);
                    for (fd = 0; fd < this->nfds; fd++)
                    {
                        if (FD_ISSET(fd, &this->afds))
                        {
                            sprintf(buf, "WIN|%s\r\n", this_questions[current_question]->winner->name);
                            sock_send(fd, buf, strlen(buf), &afds, &nfds);
                            printf("sent: %s", buf);
                            fflush(stdout);
                        }
                    }
                }
                else
                {
                    printf("there is no winner\n");
                    fflush(stdout);
                    for (fd = 0; fd < this->nfds; fd++)
                    {
                        if (FD_ISSET(fd, &this->afds))
                        {
                            sprintf(buf, "WIN|\r\n");
                            sock_send(fd, buf, strlen(buf), &afds, &nfds);
                            printf("sent: %s", buf);
                            fflush(stdout);
                        }
                    }
                }

                if (current_question == num_questions - 1)
                {
                    // questions ended

                    for (fd = 0; fd < this->nfds; fd++)
                    {
                        if (FD_ISSET(fd, &this->afds))
                        {
                            strcpy(buf, "RESULT");

                            for (int i = 0; i < this->nfds; i++)
                            {
                                if (players[i] != NULL && FD_ISSET(i, &this->afds) && status[i] == 2)
                                {
                                    sprintf(&buf[strlen(buf)], "|%s|%d", players[i]->name, players[i]->score);
                                }
                            }

                            sprintf(&buf[strlen(buf)], "\r\n");
                            sock_send(fd, buf, strlen(buf), &afds, &nfds);
                            printf("sent: %s", buf);
                            fflush(stdout);
                        }
                    }
                    goto cleanup; // everyone has answered - game has ended - kick everyone
                }

                current_question++;
            }

        }
        // my heart is crying, but i have to do this.
        // welcome back from the prehistoric times...
        // the terrifying GO-TO.
        cleanup:

        // execute the cleanup routine, and exit.
    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}

/* move user between sets */

int move_user(int sock, fd_set* src, fd_set* dest, int* snfdsp, int* dnfdsp)
{
    if (src == NULL && dest == NULL) return 0;

    if (src != NULL)
    {
        FD_CLR(sock, src);
        if (*snfdsp == sock + 1) *snfdsp -= 1;
    }

    if (dest != NULL)
    {
        FD_SET(sock, dest);
        if (sock + 1 > *dnfdsp) *dnfdsp = sock + 1;
    }

    return 1;
}

/* writing in a loop
 * credits to https://stackoverflow.com/a/24260280/8707157 */

ssize_t sock_send(int sock, char *buf, size_t size, fd_set* set, int *nfdsp)
{
    ssize_t cc = write(sock, buf, size);

    if (cc <= 0)
    {
        // connection closed
        fflush(stdout);
        close(sock);
        FD_CLR(sock, set);
        if (*nfdsp == sock + 1) *nfdsp -= 1;
        status[sock] = 0;
        lock(&mtx_numclients);
        GB.nclients--;
        printf("(sock %d) has left - online: %d\n", sock, GB.nclients);
        fflush(stdout);
        unlock(&mtx_numclients);
        return 0;
    }

    return cc;
}

/* reading in a loop
 * credits to https://stackoverflow.com/a/20149925/8707157 */

ssize_t sock_get(int sock, char *buf, size_t size, fd_set* set, int* nfdsp)
{
    ssize_t cc = read(sock, buf, size);

    if (cc <= 0)
    {
        // closed connection
        FD_CLR(sock, set);
        if (*nfdsp == sock + 1) *nfdsp -= 1;
        status[sock] = 0;
        lock(&mtx_numclients);
        GB.nclients--;
        printf("(sock %d) has left - online: %d\n", sock, GB.nclients);
        fflush(stdout);
        unlock(&mtx_numclients);
        close(sock);
        return 0;
    }

    return cc;
}

int map_msg(char* msg)
{
    // hub commands
    if (!strcmp(msg, "GETOPENGROUPS")) return 1;
    if (!strcmp(msg, "GROUP")) return 2;
    if (!strcmp(msg, "QUIZ")) return 3;
    if (!strcmp(msg, "CANCEL")) return 4;
    if (!strcmp(msg, "JOIN")) return 5;

    // group commands
    if (!strcmp(msg, "LEAVE")) return 6;
    if (!strcmp(msg, "ANS")) return 7;
    if (!strcmp(msg, "NOANS")) return 8;

    return 0;
}

void msg_norm(char* msg)
{
    if (msg[strlen(msg) - 1] == '\n' || msg[strlen(msg) - 1] == '\r') msg[strlen(msg) - 1] = '\0';
    if (msg[strlen(msg) - 1] == '\n' || msg[strlen(msg) - 1] == '\r') msg[strlen(msg) - 1] = '\0';
}
