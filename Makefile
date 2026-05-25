# ws_pipeline_dispatcher — v1 minimal Makefile
#
# Targets:
#   make            — build all binaries into build/
#   make test       — build & run unit tests
#   make smoke      — end-to-end skeleton smoke test
#   make install-man — install man pages to $(MANDIR) (default: /usr/local/share/man/man1)
#   make clean      — remove build artifacts
#
# Layout:
#   lib/     shared code (libpipeline, stream_logger)
#   applets/ one .c per executable (pipeline_dispatcher + 3 stubs)
#   tests/   unit tests
#   build/   build outputs (created at build time)

CC        ?= cc
CFLAGS    ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
CPPFLAGS  ?= -Ilib -Ithird-party/cJSON
LDFLAGS   ?=
LDLIBS    ?=

BUILD_DIR := build
LIB_SRCS  := lib/libpipeline.c lib/dynamic_buffer.c lib/jsonl_codec.c lib/stream_logger.c third-party/cJSON/cJSON.c
LIB_OBJS  := $(BUILD_DIR)/libpipeline.o $(BUILD_DIR)/dynamic_buffer.o $(BUILD_DIR)/jsonl_codec.o $(BUILD_DIR)/stream_logger.o $(BUILD_DIR)/cJSON.o
LOG_PARSE_SRCS := \
    applets/log_parse/log_parse.c \
    applets/log_parse/log_filter_expr.c \
    applets/log_parse/log_output_format.c \
    applets/log_parse/log_regex.c
CLIP_STORE_SRCS := \
    applets/clip_store/clip_store.c \
    applets/clip_store/db_format.c \
    applets/clip_store/db_query.c \
    applets/clip_store/db_compact.c
STREAM_MERGE_SRCS := \
    applets/stream_merge/stream_merge.c \
    applets/stream_merge/sm_fsm.c \
    applets/stream_merge/sm_reader.c \
    applets/stream_merge/sm_events.c

APPLETS   := pipeline_dispatcher stream_merge log_parse clip_store
BINS      := $(addprefix $(BUILD_DIR)/,$(APPLETS))

TEST_BINS    := $(BUILD_DIR)/test_libpipeline $(BUILD_DIR)/test_stream_logger $(BUILD_DIR)/test_lib_strict
TEST_SCRIPTS := tests/test_log_parse.sh tests/test_clip_store.sh tests/test_stream_merge.sh tests/test_pipeline_dispatcher.sh

MAN_DIR   := man
MAN_PAGES := $(MAN_DIR)/stream_merge.1 $(MAN_DIR)/log_parse.1 $(MAN_DIR)/clip_store.1
MANDIR    ?= /usr/local/share/man/man1

.PHONY: all clean test smoke install-man

all: $(BINS)

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/libpipeline.o: lib/libpipeline.c lib/libpipeline.h lib/dynamic_buffer.h lib/jsonl_codec.h lib/stream_logger.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/dynamic_buffer.o: lib/dynamic_buffer.c lib/dynamic_buffer.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/jsonl_codec.o: lib/jsonl_codec.c lib/jsonl_codec.h lib/dynamic_buffer.h third-party/cJSON/cJSON.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/cJSON.o: third-party/cJSON/cJSON.c third-party/cJSON/cJSON.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR)/stream_logger.o: lib/stream_logger.c lib/stream_logger.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@


$(BUILD_DIR)/log_parse: $(LOG_PARSE_SRCS) applets/log_parse/log_parse.h applets/log_parse/log_filter_expr.h applets/log_parse/log_output_format.h applets/log_parse/log_regex.h $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LOG_PARSE_SRCS) $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/clip_store: $(CLIP_STORE_SRCS) applets/clip_store/clip_store.h applets/clip_store/db_format.h applets/clip_store/db_query.h applets/clip_store/db_compact.h $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(CLIP_STORE_SRCS) $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/stream_merge: $(STREAM_MERGE_SRCS) applets/stream_merge/sm_fsm.h applets/stream_merge/sm_reader.h applets/stream_merge/sm_events.h $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iapplets/stream_merge $(STREAM_MERGE_SRCS) $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

# Each applet links against the shared lib objects.
$(BUILD_DIR)/%: applets/%.c $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_libpipeline: tests/test_libpipeline.c $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_stream_logger: tests/test_stream_logger.c $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/test_lib_strict: tests/lib/test_lib_strict.c $(LIB_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST_BINS) $(BINS)
	@for test_bin in $(TEST_BINS); do $$test_bin || exit $$?; done
	@for test_script in $(TEST_SCRIPTS); do \
		LOG_PARSE="$(CURDIR)/$(BUILD_DIR)/log_parse" \
		CLIP_STORE="$(CURDIR)/$(BUILD_DIR)/clip_store" \
		STREAM_MERGE="$(CURDIR)/$(BUILD_DIR)/stream_merge" \
		PIPELINE_DISPATCHER="$(CURDIR)/$(BUILD_DIR)/pipeline_dispatcher" \
		sh $$test_script || exit $$?; \
	done

# End-to-end smoke: run dispatcher with skeleton stubs from build/.
smoke: all
	@mkdir -p /tmp/ws_pipeline_smoke
	@cd $(BUILD_DIR) && ./pipeline_dispatcher \
	    smoke_session /tmp/ws_pipeline_smoke /tmp/ws_pipeline_smoke/clips.db 300

install-man: $(MAN_PAGES)
	install -d $(MANDIR)
	install -m 644 $(MAN_PAGES) $(MANDIR)

clean:
	rm -rf $(BUILD_DIR)
