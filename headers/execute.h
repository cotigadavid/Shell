#pragma once

#include "headers.h"
#include "pipelines.h"

void duplicate_fd(command* cmd);

void execute_pipeline(pipeline* curr_pipeline);

void execute_single_command(pipeline* curr_pipeline);