#include "pd_config.h"

#include "libpipeline.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_TTL_SECS "300"
#define DEFAULT_CLIP_SECS "5"
#define DEFAULT_IDLE_SECS "2"
#define DEFAULT_FILTER "type=clip"

/** Resolve a sibling executable located next to the dispatcher binary. */
static int sibling_path(char *out, size_t out_size, const char *argv0, const char *name) {
    char exe_path[PATH_MAX];
    ssize_t nread = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    const char *base_path = argv0;
    if (nread > 0) {
        exe_path[nread] = '\0';
        base_path = exe_path;
    }

    const char *slash = strrchr(base_path, '/');
    if (slash == NULL) {
        int n = snprintf(out, out_size, "./%s", name);
        return n >= 0 && (size_t)n < out_size ? 0 : -1;
    }

    size_t dir_len = (size_t)(slash - base_path);
    if (dir_len + 1 + strlen(name) + 1 > out_size) {
        return -1;
    }
    memcpy(out, base_path, dir_len);
    out[dir_len] = '/';
    strcpy(out + dir_len + 1, name);
    return 0;
}

/** Return 1 only when value is a base-10 integer and satisfies allow_zero. */
static int valid_uint_string(const char *value, int allow_zero) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    char *end = NULL;
    errno = 0;
    unsigned long n = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') {
        return 0;
    }
    return allow_zero || n > 0;
}

void pd_config_print_usage(FILE *stream, const char *prog) {
    fprintf(stream, "Usage: %s [OPTIONS] <session_id> <src_dir> <db_path>\n\n", prog);
    fprintf(stream, "Description:\n");
    fprintf(stream, "  Build and supervise stream_merge | log_parse | clip_store.\n\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "      --ttl <seconds>       clip_store TTL (default: %s)\n", DEFAULT_TTL_SECS);
    fprintf(stream, "      --clip-secs <n>       stream_merge clip window (default: %s)\n", DEFAULT_CLIP_SECS);
    fprintf(stream, "      --idle-secs <n>       stream_merge idle timeout (default: %s)\n", DEFAULT_IDLE_SECS);
    fprintf(stream, "      --filter <expr>       log_parse filter (default: %s)\n", DEFAULT_FILTER);
    fprintf(stream, "  -h, --help                Show this help message and exit\n");
}

int pd_config_parse(int argc, char **argv, pd_config_t *out) {
    if (out == NULL || argv == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->program_name = argv[0];
    out->ttl = DEFAULT_TTL_SECS;
    out->clip_secs = DEFAULT_CLIP_SECS;
    out->idle_secs = DEFAULT_IDLE_SECS;
    out->filter = DEFAULT_FILTER;

    int opt;
    static struct option long_options[] = {
        {"ttl", required_argument, 0, 1000},
        {"clip-secs", required_argument, 0, 1001},
        {"idle-secs", required_argument, 0, 1002},
        {"filter", required_argument, 0, 1003},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    optind = 1;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
            case 1000:
                out->ttl = optarg;
                break;
            case 1001:
                out->clip_secs = optarg;
                break;
            case 1002:
                out->idle_secs = optarg;
                break;
            case 1003:
                out->filter = optarg;
                break;
            case 'h':
                pd_config_print_usage(stdout, argv[0]);
                return 1;
            case '?':
                pd_config_print_usage(stderr, argv[0]);
                return -1;
            default:
                return -1;
        }
    }

    if (optind + 3 != argc) {
        pd_config_print_usage(stderr, argv[0]);
        return -1;
    }

    out->session_id = argv[optind];
    out->src_dir = argv[optind + 1];
    out->db_path = argv[optind + 2];

    if (sibling_path(out->bin_stream_merge, sizeof(out->bin_stream_merge), argv[0], "stream_merge") != 0 ||
        sibling_path(out->bin_log_parse, sizeof(out->bin_log_parse), argv[0], "log_parse") != 0 ||
        sibling_path(out->bin_clip_store, sizeof(out->bin_clip_store), argv[0], "clip_store") != 0) {
        LOG_ERROR("failed to resolve applet paths");
        return -1;
    }
    return 0;
}

int pd_config_validate(const pd_config_t *cfg) {
    if (cfg == NULL || cfg->session_id == NULL || cfg->session_id[0] == '\0' ||
        cfg->src_dir == NULL || cfg->src_dir[0] == '\0' ||
        cfg->db_path == NULL || cfg->db_path[0] == '\0') {
        LOG_ERROR("session_id, src_dir, and db_path are required");
        return -1;
    }

    struct stat st;
    if (stat(cfg->src_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        LOG_ERROR("src_dir is not a directory: %s", cfg->src_dir);
        return -1;
    }
    if (!valid_uint_string(cfg->ttl, 1)) {
        LOG_ERROR("invalid --ttl value: %s", cfg->ttl);
        return -1;
    }
    if (!valid_uint_string(cfg->clip_secs, 0)) {
        LOG_ERROR("invalid --clip-secs value: %s", cfg->clip_secs);
        return -1;
    }
    if (!valid_uint_string(cfg->idle_secs, 0)) {
        LOG_ERROR("invalid --idle-secs value: %s", cfg->idle_secs);
        return -1;
    }
    if (access(cfg->bin_stream_merge, X_OK) != 0 ||
        access(cfg->bin_log_parse, X_OK) != 0 ||
        access(cfg->bin_clip_store, X_OK) != 0) {
        LOG_ERROR("one or more applet binaries are not executable");
        return -1;
    }
    return 0;
}
