#ifndef PD_CONFIG_H
#define PD_CONFIG_H

#include <limits.h>
#include <stdio.h>

/**
 * @brief Parsed dispatcher configuration and resolved applet paths.
 */
typedef struct {
    const char *program_name;        /* Program name from argv[0] */
    const char *session_id;          /* Target session to dispatch */
    const char *src_dir;             /* Source directory containing log/stream files */
    const char *db_path;             /* Target database for clip_store */
    const char *ttl;                 /* TTL option for clip_store */
    const char *clip_secs;           /* Clip window option for stream_merge */
    const char *idle_secs;           /* Idle timeout option for stream_merge */
    const char *filter;              /* Filter expression for log_parse */
    char bin_stream_merge[PATH_MAX]; /* Resolved absolute path to stream_merge applet */
    char bin_log_parse[PATH_MAX];    /* Resolved absolute path to log_parse applet */
    char bin_clip_store[PATH_MAX];   /* Resolved absolute path to clip_store applet */
} pd_config_t;

/**
 * @brief Parse CLI options into dispatcher configuration.
 *
 * @param argc Argument count from main.
 * @param argv Argument vector from main.
 * @param out Destination config.
 * @return 0 on success, 1 when --help was printed, -1 on invalid usage.
 */
int pd_config_parse(int argc, char **argv, pd_config_t *out);

/**
 * @brief Validate paths and numeric option strings in config.
 *
 * @param cfg Parsed configuration.
 * @return 0 on success, -1 on validation failure.
 */
int pd_config_validate(const pd_config_t *cfg);

/**
 * @brief Print dispatcher command-line usage.
 *
 * @param stream Output stream.
 * @param prog Program name for the usage line.
 */
void pd_config_print_usage(FILE *stream, const char *prog);

#endif
