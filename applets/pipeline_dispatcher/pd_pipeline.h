#ifndef PD_PIPELINE_H
#define PD_PIPELINE_H

#include "pd_config.h"
#include "pd_exit.h"
#include "pd_spawn.h"

#define PD_CHILD_COUNT 3

typedef struct {
    int pipe1[2];
    int pipe2[2];
    pd_child_t children[PD_CHILD_COUNT];
} pd_pipeline_t;

void pd_pipeline_init(pd_pipeline_t *pipeline);
pd_exit_code_t pd_pipeline_run(const pd_config_t *cfg, pd_pipeline_t *pipeline);
void pd_pipeline_cleanup(pd_pipeline_t *pipeline);

#endif
