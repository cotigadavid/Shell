#pragma once

#include "headers.h"

#define JOB_RUNNING 0
#define JOB_STOPPED 1
#define JOB_DONE    2

typedef struct {
    pid_t pid;
    pid_t pgid;
    int status;
} process;

typedef struct job {
    int job_id;
    pid_t pgid;
    char* command_line;
    int status;
    struct job* next;
    process* process_list[MAX_PROCESSES];
    int process_counter;
} job_t;

extern job_t* job_list;
extern int last_id;

extern process* global_process_list[MAX_GLOBAL_PROCESSES];
extern int global_process_counter;

pid_t get_pgid_of_process(pid_t pid);

struct pipeline;

job_t* add_job(pid_t pgid, const char* command_line, struct pipeline* p);

job_t* find_job_by_id(int job_id);

job_t* find_job_by_pgid(pid_t pgid);

void add_process_to_job(pid_t pgid, pid_t pid);

void remove_job(int job_id);

void update_job_status(job_t* job, pid_t proc_pid, int check_status);

void update_all_processes_in_job(job_t* job, int new_status);

bool job_is_stopped(job_t* job);

void print_jobs();

job_t* get_most_recent_job(void);

job_t* get_most_recent_stopped_job();

int count_processes_in_job(job_t* job);

int all_processes_done(job_t* job);