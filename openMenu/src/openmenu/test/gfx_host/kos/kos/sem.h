#pragma once
typedef struct {
    int count;
} semaphore_t;
int sem_init(semaphore_t* s, int value);
int sem_wait(semaphore_t* s);
int sem_signal(semaphore_t* s);
