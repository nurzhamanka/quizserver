//
// Created by Nurzhan Sakenov on 4/12/18.
//

#ifndef FANCYQUIZ_STRUCTURES_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/select.h>

#define MAX_CLIENTS 1024
#define MAX_STRING 2048
#define MAX_NAME 64
#define MAX_QUESTIONS 1024
#define MAX_GROUPS 32

#define lock(x) pthread_mutex_lock(x)
#define unlock(x) pthread_mutex_unlock(x)

typedef struct _question question;
typedef struct _player player;
typedef struct _group group;
typedef struct _globs globs;
typedef struct _file file;

struct _group
{
    char *name;
    char *topic;
    int max_size;
    int current_size;
    int num_questions;
    int is_playing;
    int creator_fd;
    // group multiplexing
    fd_set rfds;
    fd_set afds;
    int nfds;
    int admin_joined;
};

struct _player
{
    char *name;
    group *group; // current group this player is in
    int score;
};

struct _question
{
    char *prompt; // question prompt
    char **answers; // array of answer strings
    char *correct; // correct answer
    int qnum;
    player *winner; // the fastest correct user
    int answered; // how many clients answered this
    int sent; // has this question been sent?
};

struct _globs
{
    // numbers
    int nclients;
    int ngroups;

    // synchronization
};

struct _file
{
    FILE* file;
    char* text;
};

//
// QUESTION
//

void question_destroy(question *q);

question **parse_questions(FILE *fp, group **g);

void print_questions(question **qs, int num_questions);

char *print_question(question **qs, int num);

//
// PLAYER
//

player *player_create(char *name);

void player_destroy(player *p);

//
// GROUP
//

group *group_create(char *topic, char *name, int max_size);

void group_destroy(group *g);

int group_find_empty_spot(group **g);

void file_destroy(file *f);


#define FANCYQUIZ_STRUCTURES_H

#endif //FANCYQUIZ_STRUCTURES_H
