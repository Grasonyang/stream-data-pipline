#include "pd_pipeline.h"

#include "libpipeline.h"
#include "pd_signal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define READ_END 0
#define WRITE_END 1

void pd_pipeline_init(pd_pipeline_t *pipeline) {
    if (pipeline == NULL) {
        return;
    }
    pipeline->pipe1[0] = -1;
    pipeline->pipe1[1] = -1;
    pipeline->pipe2[0] = -1;
    pipeline->pipe2[1] = -1;
    pipeline->children[0] = (pd_child_t){ .name = "stream_merge", .pid = -1 };
    pipeline->children[1] = (pd_child_t){ .name = "log_parse", .pid = -1 };
    pipeline->children[2] = (pd_child_t){ .name = "clip_store", .pid = -1 };
}

/** Close all pipe file descriptors held by the parent. */
static void close_pipe_fds(pd_pipeline_t *pipeline) {
    int *fds[] = {&pipeline->pipe1[0], &pipeline->pipe1[1], &pipeline->pipe2[0], &pipeline->pipe2[1]};
    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); ++i) {
        if (*fds[i] >= 0) {
            close(*fds[i]);
            *fds[i] = -1;
        }
    }
}

void pd_pipeline_cleanup(pd_pipeline_t *pipeline) {
    if (pipeline == NULL) {
        return;
    }
    close_pipe_fds(pipeline);
    pd_kill_children(pipeline->children, PD_CHILD_COUNT);
}

pd_exit_code_t pd_pipeline_run(const pd_config_t *cfg, pd_pipeline_t *pipeline) {
    if (cfg == NULL || pipeline == NULL) {
        return PD_EXIT_SETUP_ERROR;
    }

    pd_pipeline_init(pipeline);
    
    /* Create first pipe: stream_merge -> log_parse */
    if (pipe(pipeline->pipe1) < 0) {
        LOG_ERROR("pipe1 failed: %s", strerror(errno));
        return PD_EXIT_SETUP_ERROR;
    }
    
    /* Create second pipe: log_parse -> clip_store */
    if (pipe(pipeline->pipe2) < 0) {
        LOG_ERROR("pipe2 failed: %s", strerror(errno));
        close_pipe_fds(pipeline);
        return PD_EXIT_SETUP_ERROR;
    }

    char *merge_argv[] = {
        (char *)cfg->bin_stream_merge,
        "--clip-secs", (char *)cfg->clip_secs,
        "--idle-secs", (char *)cfg->idle_secs,
        (char *)cfg->session_id,
        (char *)cfg->src_dir,
        NULL
    };
    char *parse_argv[] = {
        (char *)cfg->bin_log_parse,
        "--filter", (char *)cfg->filter,
        NULL
    };
    char *store_argv[] = {
        (char *)cfg->bin_clip_store,
        "--db", (char *)cfg->db_path,
        "--ttl", (char *)cfg->ttl,
        NULL
    };
    int all_pipes[] = {pipeline->pipe1[0], pipeline->pipe1[1], pipeline->pipe2[0], pipeline->pipe2[1]};

    /* Spawn stream_merge reading from original STDIN and writing to pipe1 */
    pipeline->children[0].path = cfg->bin_stream_merge;
    pipeline->children[0].pid = pd_spawn_child("stream_merge", cfg->bin_stream_merge, merge_argv, STDIN_FILENO, pipeline->pipe1[WRITE_END], all_pipes, 4);
    if (pipeline->children[0].pid < 0) {
        pd_pipeline_cleanup(pipeline);
        return PD_EXIT_SETUP_ERROR;
    }

    /* Spawn log_parse reading from pipe1 and writing to pipe2 */
    pipeline->children[1].path = cfg->bin_log_parse;
    pipeline->children[1].pid = pd_spawn_child("log_parse", cfg->bin_log_parse, parse_argv, pipeline->pipe1[READ_END], pipeline->pipe2[WRITE_END], all_pipes, 4);
    if (pipeline->children[1].pid < 0) {
        pd_pipeline_cleanup(pipeline);
        return PD_EXIT_SETUP_ERROR;
    }

    /* Spawn clip_store reading from pipe2 and writing to original STDOUT */
    pipeline->children[2].path = cfg->bin_clip_store;
    pipeline->children[2].pid = pd_spawn_child("clip_store", cfg->bin_clip_store, store_argv, pipeline->pipe2[READ_END], STDOUT_FILENO, all_pipes, 4);
    if (pipeline->children[2].pid < 0) {
        pd_pipeline_cleanup(pipeline);
        return PD_EXIT_SETUP_ERROR;
    }

    close_pipe_fds(pipeline);
    if (pd_signal_interrupted()) {
        pd_pipeline_cleanup(pipeline);
        return PD_EXIT_INTERRUPTED;
    }
    return pd_wait_children(pipeline->children, PD_CHILD_COUNT);
}
