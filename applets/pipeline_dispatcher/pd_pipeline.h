#ifndef PD_PIPELINE_H
#define PD_PIPELINE_H

#include "pd_config.h"
#include "pd_exit.h"
#include "pd_spawn.h"

#define PD_CHILD_COUNT 3

/**
 * @brief Pipeline state holding inter-process communication resources and child info.
 */
typedef struct {
    int pipe1[2];                                 /* Pipe between stream_merge and log_parse */
    int pipe2[2];                                 /* Pipe between log_parse and clip_store */
    pd_child_t children[PD_CHILD_COUNT];          /* Tracked child processes */
} pd_pipeline_t;

/**
 * @brief Initializes the pipeline state to safe default values.
 * 
 * @param pipeline The pipeline structure to initialize.
 */
void pd_pipeline_init(pd_pipeline_t *pipeline);

/**
 * @brief Constructs and runs the applet pipeline.
 * 
 * Creates the necessary pipes and spawns stream_merge, log_parse, and clip_store
 * in sequence. Waits for all processes to complete.
 * 
 * @param cfg The dispatcher configuration including paths and options.
 * @param pipeline The pipeline state to populate and use.
 * @return PD_EXIT_OK on success, or an appropriate error code on failure.
 */
pd_exit_code_t pd_pipeline_run(const pd_config_t *cfg, pd_pipeline_t *pipeline);

/**
 * @brief Cleans up pipeline resources by closing pipes and killing children.
 * 
 * Safe to call even if the pipeline was only partially constructed.
 * 
 * @param pipeline The pipeline structure to clean up.
 */
void pd_pipeline_cleanup(pd_pipeline_t *pipeline);

#endif
