#ifndef PD_CONFIG_H
#define PD_CONFIG_H

#include <limits.h>
#include <stdio.h>

/**
 * @brief Parsed dispatcher configuration and resolved applet paths.
 */
typedef struct {
    const char *program_name;
    const char *session_id;
    const char *src_dir;
    const char *db_path;
    const char *ttl;
    const char *clip_secs;
    const char *idle_secs;
    const char *filter;
    char bin_stream_merge[PATH_MAX];
    char bin_log_parse[PATH_MAX];
    char bin_clip_store[PATH_MAX];
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
