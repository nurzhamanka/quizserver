//
// Created by Nurzhan Sakenov on 4/12/18.
//

#include "structures.h"

group *group_create(char *topic, char *name, int max_size)
{
    group *g = malloc(sizeof(group));

    if (g == NULL) {
        fprintf(stderr, "group_create: malloc error\n");
        return NULL;
    }

    g->name = malloc(MAX_NAME * sizeof(char));
    g->topic = malloc(MAX_NAME * sizeof(char));
    g->max_size = max_size;
    g->current_size = 0;
    g->num_questions = 0;
    g->nfds = 0;
    g->admin_joined = 0;

    strcpy(g->name, name);
    strcpy(g->topic, topic);

    return g;
}

void group_destroy(group *g) {
    if (g == NULL) return;
    free(g->name);
    free(g->topic);
    g = NULL;
}

int group_find_empty_spot(group **g) {
    for (int j = 0; j < MAX_GROUPS; j++) {
        if (g[j] == NULL) return j;
    }

    return -1;
}


void question_destroy(question *q) {
    // free all pointers
    if (q == NULL) return;

    for (int i = 0; i < q->qnum; i++) {
        free(q->answers[i]);
    }

    free(q->answers);
    free(q->correct);
    free(q->prompt);
    free(q);
}

question **parse_questions(FILE *fp, group **g) {
    question **questions = malloc(MAX_QUESTIONS * sizeof(question *));

    if (questions == NULL) {
        fprintf(stderr, "parse_questions: malloc error\n");
        return NULL;
    }

    rewind(fp);

    char buffer[2048];
    char *toks[2];
    long pos;
    int qnum = 0;
    int correct_index;

    for (int i = 0; i < MAX_QUESTIONS; i++) {

        qnum = 0;

        fgets(buffer, MAX_STRING + 1, fp);

        if (feof(fp)) {
            printf("END OF FILE.\n");
            break;
        }

        printf("parsing question %d...\n", i + 1);

        questions[i] = malloc(sizeof(question));
        if (questions[i] == NULL) {
            fprintf(stderr, "parse_questions: malloc error\n");
            return NULL;
        }

        questions[i]->prompt = malloc(MAX_STRING * sizeof(char));
        questions[i]->correct = malloc(MAX_STRING * sizeof(char));
        questions[i]->winner = NULL;
        questions[i]->answered = 0;
        questions[i]->sent = 0;

        toks[0] = strtok(buffer, " "); // question id
        toks[1] = strtok(NULL, "\0"); // question prompt
        strcpy(questions[i]->prompt, toks[1]); // save the prompt

        printf("%d: %s", i + 1, questions[i]->prompt);

        // count the number of answers
        pos = ftell(fp);
        while (1) {
            fgets(buffer, MAX_STRING + 1, fp);
            if (strcmp(buffer, "\n") != 0) {
                qnum++;
            } else {
                // newline encountered
                break;
            }
        }
        fseek(fp, pos, SEEK_SET); // return to the answers

        questions[i]->answers = malloc(qnum * sizeof(char *));
        for (int j = 0; j < qnum; j++) {
            questions[i]->answers[j] = malloc(MAX_STRING * sizeof(char));
        }

        for (int k = 0; k < qnum; k++) {
            fgets(buffer, MAX_STRING + 1, fp);
            strcpy(questions[i]->answers[k], buffer);
            printf("%c: %s", k + 65, questions[i]->answers[k]);
        }

        fgets(buffer, MAX_STRING + 1, fp); // skip newline

        fgets(buffer, MAX_STRING + 1, fp);
        buffer[strlen(buffer) - 1] = '\0';
        strcpy(questions[i]->correct, buffer);
        printf("correct answer: %s\n", questions[i]->correct);

        fgets(buffer, MAX_STRING + 1, fp); // skip newline

        (*g)->num_questions = i + 1; // global number of questions
        questions[i]->qnum = qnum;

        if (feof(fp)) {
            printf("END OF FILE.\n");
            break;
        }

        printf("\n");
    }

    return questions;
}

void print_questions(question **qs, int num_questions) {
    if (qs == NULL) return;
    int num_qs = num_questions;

    for (int i = 0; i < num_qs; i++) {
        printf("%d ", i + 1);
        printf("%s", qs[i]->prompt);
        for (int j = 0; j < qs[i]->qnum; j++) {
            printf("%s", qs[i]->answers[j]);
        }
        printf("\n");
    }
}

char *print_question(question **qs, int num) {
    char *question_text = malloc(MAX_STRING * sizeof(char));

    if (qs == NULL) return NULL;
    if (qs[num - 1] == NULL) return NULL;

    sprintf(question_text, "question %d:\n", num);
    sprintf(&question_text[strlen(question_text)], "%s", qs[num - 1]->prompt);
    for (int j = 0; j < qs[num - 1]->qnum; j++) {
        sprintf(&question_text[strlen(question_text)], "%s", qs[num - 1]->answers[j]);
    }
    question_text[strlen(question_text) - 1] = '\0';

    return question_text;
}


player *player_create(char *name) {
    player *p = malloc(sizeof(player));

    if (p == NULL) {
        fprintf(stderr, "player_create: malloc error\n");
        return NULL;
    }

    p->name = malloc(MAX_NAME * sizeof(char));
    strcpy(p->name, name);
    p->score = 0;

    return p;
}

void player_destroy(player *p) {
    if (p == NULL) return;
    free(p->name);
    free(p);
    p = NULL;
}

void file_destroy(file *f)
{
    if (f == NULL) return;
    free(f->text);
    f = NULL;
}