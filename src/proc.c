#include "../headers/proc.h"

job_t* job_list = NULL;
int last_id = 0;

process* global_process_list[MAX_GLOBAL_PROCESSES];
int global_process_counter = 0;

pid_t get_pgid_of_process(pid_t pid) {
    for (int i = 0; i < global_process_counter; ++i)
        if (global_process_list[i]->pid == pid)
            return global_process_list[i]->pgid;

}

job_t* add_job(pid_t pgid, const char* command_line, struct pipeline* p) {
    job_t* new_job = (job_t*)malloc(sizeof(job_t));
    
    if (new_job == NULL) {
        perror("allocation failed");
        return NULL;
    }
    new_job->pgid = pgid;
    new_job->command_line = strdup(command_line);
    new_job->status = JOB_RUNNING;

    new_job->process_counter = 0;

    new_job->next = job_list;
    job_list = new_job;
    new_job->job_id = ++last_id;

    return new_job;
}

job_t* find_job_by_id(int job_id) {
    job_t* current = job_list;
    while (current) {
        if (current->job_id == job_id)
            return current;
        current = current->next;
    }
    return NULL;
}

job_t* find_job_by_pgid(pid_t pgid) {
    job_t* current = job_list;
    while (current) {
        if (current->pgid == pgid)
            return current;
        current = current->next;
    }
    return NULL;
}

void add_process_to_job(pid_t pgid, pid_t pid) {
    job_t* job = find_job_by_pgid(pgid);
    process* proc = (process*)malloc(sizeof(process));

    if (proc == NULL) {
        perror("allocation failed");
        return;
    }

    global_process_list[global_process_counter++] = proc;

    proc->pid = pid;
    proc->pgid = pgid;
    proc->status = JOB_RUNNING;
    //setpgid(pid, pgid);

    job->process_list[job->process_counter++] = proc;
}

void remove_job(int job_id) {
    job_t* current = job_list, *prev = NULL;
    while (current) {
        if (current->job_id == job_id) {
            if (prev)
                prev->next = current->next;
            else
                job_list = current->next;

            for (int i = 0; i < current->process_counter; ++i)
                free(current->process_list[i]);
            free(current->command_line);
            free(current);
            return;
        }
        prev = current;
        current = current->next;
    }
}

void update_job_status(job_t* job, pid_t proc_pid, int check_status) {

    for (int i = 0; i < job->process_counter; ++i) {
        if (job->process_list[i]->pid == proc_pid)
            job->process_list[i]->status = check_status;

        if (job->process_list[i]->status != check_status)
            return;
    }
    job->status = check_status;
}

void update_all_processes_in_job(job_t* job, int new_status) {

    for (int i = 0; i < job->process_counter; ++i) {
        job->process_list[i]->status = new_status;
    }
    job->status = new_status;
}

bool job_is_stopped(job_t* job) {
    if (job == NULL) {
        return false;
    }
    
    // A job is considered stopped if ANY process is stopped
    // and no processes are still running
    bool has_stopped = false;
    bool has_running = false;
    
    for (int i = 0; i < job->process_counter; ++i) {
        if (job->process_list[i]->status == JOB_STOPPED)
            has_stopped = true;
        if (job->process_list[i]->status == JOB_RUNNING)
            has_running = true;
    }
    
    return has_stopped && !has_running;
}


void print_jobs() {
    job_t* current = job_list;
    while (current) {
        const char* status_str = (current->status == JOB_RUNNING) ? "Running" :
                                 (current->status == JOB_STOPPED) ? "Stopped" :
                                 (current->status == JOB_DONE) ? "Done" : "Unknown";

        printf("[%d] PGID: %d  %s  (%s)\n",
               current->job_id, current->pgid, status_str, current->command_line);
        current = current->next;
    }
}

job_t* get_most_recent_job(void) {
    job_t* most_recent = NULL;
    int max_id = -1;
    
    for (job_t* job = job_list; job != NULL; job = job->next) {
        if (job->status != JOB_DONE && job->job_id > max_id) {
            max_id = job->job_id;
            most_recent = job;
        }
    }
    
    return most_recent;
}

job_t* get_most_recent_stopped_job() {
    job_t* most_recent = NULL;
    int max_id = -1;
    
    for (job_t* job = job_list; job != NULL; job = job->next) {
        if ((job->status == JOB_STOPPED || job_is_stopped(job)) && job->job_id > max_id) {
            max_id = job->job_id;
            most_recent = job;
        }
    }
    
    return most_recent;
}

int count_processes_in_job(job_t* job) {
    int count = 0;
    for (int i = 0; i < job->process_counter; ++i) {
        if (job->process_list[i]->status != JOB_DONE) {
            count++;
        }
    }
    return count;
}

int all_processes_done(job_t* job) {
    
    for (int i = 0; i < job->process_counter; ++i) {
        if (job->process_list[i]->status != JOB_DONE) {
            return 0; 
        }
    }
    return 1; 
}