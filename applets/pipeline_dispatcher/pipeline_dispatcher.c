/*
 * pipeline_dispatcher.c -- process orchestrator for the applet pipeline.
 *
 * The dispatcher intentionally does not handle UDP/RTP ingress. It validates a
 * file-backed session artifact and supervises stream_merge | log_parse | clip_store.
 */

#include "libpipeline.h"
#include "pd_config.h"
#include "pd_exit.h"
#include "pd_pipeline.h"
#include "pd_signal.h"

#include <string.h>

int pipeline_dispatcher_main(int argc, char *argv[]) {
    stream_logger_set_tag("dispatcher");

    pd_config_t config;
    
    /* Parse command line arguments into configuration */
    int parse_rc = pd_config_parse(argc, argv, &config);
    if (parse_rc > 0) {
        return PD_EXIT_OK; /* --help printed */
    }
    if (parse_rc < 0) {
        return PD_EXIT_BAD_ARGS; /* Invalid usage */
    }
    
    /* Validate parsed configuration options */
    if (pd_config_validate(&config) != 0) {
        return PD_EXIT_SETUP_ERROR;
    }
    
    /* Setup safe signal handling for graceful shutdown */
    if (pd_signal_install() != 0) {
        LOG_ERROR("failed to install signal handlers");
        return PD_EXIT_SETUP_ERROR;
    }

    LOG_INFO("starting pipeline session=%s src=%s db=%s ttl=%s clip=%s idle=%s filter=%s",
             config.session_id, config.src_dir, config.db_path, config.ttl,
             config.clip_secs, config.idle_secs, config.filter);

    pd_pipeline_t pipeline;
    
    /* Run the full pipeline process hierarchy and block until completion */
    pd_exit_code_t rc = pd_pipeline_run(&config, &pipeline);
    if (rc == PD_EXIT_OK) {
        LOG_INFO("pipeline complete session=%s", config.session_id);
    } else if (rc == PD_EXIT_INTERRUPTED) {
        LOG_WARN("pipeline interrupted session=%s", config.session_id);
    } else {
        LOG_ERROR("pipeline failed session=%s rc=%d", config.session_id, rc);
    }
    return rc;
}
