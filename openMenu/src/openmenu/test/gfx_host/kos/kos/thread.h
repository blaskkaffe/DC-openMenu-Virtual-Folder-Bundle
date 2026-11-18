#pragma once
typedef struct kthread kthread_t;
#define PRIO_DEFAULT 10
kthread_t* thd_create(int detach, void* (*fn)(void*), void* arg);
int thd_set_prio(kthread_t* t, int prio);
void thd_pass(void);
void thd_sleep(int ms);
