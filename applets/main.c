#include <stdio.h>
#include <string.h>

extern int pipeline_dispatcher_main(int argc, char **argv);
extern int stream_merge_main(int argc, char **argv);
extern int log_parse_main(int argc, char **argv);
extern int clip_store_main(int argc, char **argv);

int main(int argc, char **argv) {
    if (argc == 0 || argv[0] == NULL) {
        return 1;
    }
    
    char *name = strrchr(argv[0], '/');
    if (name) {
        name++;
    } else {
        name = argv[0];
    }

    /* If called directly as the single binary (e.g. "box" or "busybox"), use argv[1] */
    if (strcmp(name, "box") == 0 || strcmp(name, "busybox") == 0) {
        if (argc < 2) {
            printf("Usage: box <applet> [args...]\n");
            printf("Applets: pipeline_dispatcher, stream_merge, log_parse, clip_store\n");
            return 1;
        }
        name = argv[1];
        argv++;
        argc--;
    }

    if (strcmp(name, "pipeline_dispatcher") == 0) {
        return pipeline_dispatcher_main(argc, argv);
    } else if (strcmp(name, "stream_merge") == 0) {
        return stream_merge_main(argc, argv);
    } else if (strcmp(name, "log_parse") == 0) {
        return log_parse_main(argc, argv);
    } else if (strcmp(name, "clip_store") == 0) {
        return clip_store_main(argc, argv);
    } else {
        fprintf(stderr, "box: applet not found: %s\n", name);
        return 127;
    }
}