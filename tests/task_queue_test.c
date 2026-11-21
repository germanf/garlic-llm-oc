#include "task_queue.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static const char *journal_path(void) {
    static char path[128];
    snprintf(path, sizeof(path), "/tmp/garlic_task_queue_test_%ld.journal", (long)getpid());
    return path;
}

static void reset_journal(void) { unlink(journal_path()); }

static void test_add_claim_and_single_active(void) {
    reset_journal();
    TaskQueue q;
    GarlicTask t;
    char first[TASK_ID_MAX], second[TASK_ID_MAX];
    CHECK(task_queue_init(&q, journal_path()) == 0);
    CHECK(task_queue_add(&q, "summarize the project", first, sizeof(first)) == 0);
    CHECK(task_queue_add(&q, "review the plan", second, sizeof(second)) == 0);
    CHECK(!strcmp(first, "task-00000001"));
    CHECK(!strcmp(second, "task-00000002"));
    CHECK(task_queue_count_state(&q, TASK_QUEUED) == 2);
    CHECK(task_queue_claim(&q, &t) == 1 && t.id == 1 && t.attempts == 1 && t.state == TASK_RUNNING);
    CHECK(task_queue_has_active(&q));
    CHECK(task_queue_claim(&q, &t) == 0); /* never run two model jobs together */
    CHECK(task_queue_complete(&q, 1, "summary artifact") == 0);
    CHECK(!task_queue_has_active(&q));
    CHECK(task_queue_claim(&q, &t) == 1 && t.id == 2);
    CHECK(task_queue_fail(&q, 2, "mock failure") == 0);
    reset_journal();
}

static void test_restart_and_interrupted_work(void) {
    reset_journal();
    TaskQueue q, recovered;
    GarlicTask t;
    char id[TASK_ID_MAX];
    CHECK(task_queue_init(&q, journal_path()) == 0);
    CHECK(task_queue_add(&q, "recover me", id, sizeof(id)) == 0);
    CHECK(task_queue_claim(&q, &t) == 1 && t.id == 1);
    /* Simulate process exit: init replays RUNNING and durably requeues it. */
    CHECK(task_queue_init(&recovered, journal_path()) == 0);
    const GarlicTask *got = task_queue_get(&recovered, 1);
    CHECK(got && got->state == TASK_QUEUED && got->attempts == 1);
    CHECK(task_queue_claim(&recovered, &t) == 1 && t.attempts == 2);
    CHECK(task_queue_complete(&recovered, 1, "done") == 0);

    TaskQueue final;
    CHECK(task_queue_init(&final, journal_path()) == 0);
    got = task_queue_get(&final, 1);
    CHECK(got && got->state == TASK_COMPLETED && !strcmp(got->result, "done"));
    CHECK(task_queue_add(&final, "after restart", id, sizeof(id)) == 0);
    CHECK(!strcmp(id, "task-00000002"));
    reset_journal();
}

static void test_cancel_transitions_and_recovery(void) {
    reset_journal();
    TaskQueue q, recovered;
    GarlicTask t;
    char id[TASK_ID_MAX];
    CHECK(task_queue_init(&q, journal_path()) == 0);
    CHECK(task_queue_add(&q, "queued cancel", id, sizeof(id)) == 0);
    CHECK(task_queue_cancel(&q, 1) == 0);
    CHECK(task_queue_get(&q, 1)->state == TASK_CANCELED);
    CHECK(task_queue_add(&q, "running cancel", id, sizeof(id)) == 0);
    CHECK(task_queue_claim(&q, &t) == 1 && t.id == 2);
    CHECK(task_queue_cancel(&q, 2) == 0);
    CHECK(task_queue_get(&q, 2)->state == TASK_CANCEL_REQUESTED);
    CHECK(task_queue_has_active(&q));
    CHECK(task_queue_claim(&q, &t) == 0);
    CHECK(task_queue_finish_cancel(&q, 2) == 0);
    CHECK(task_queue_get(&q, 2)->state == TASK_CANCELED);

    CHECK(task_queue_add(&q, "cancel before restart", id, sizeof(id)) == 0);
    CHECK(task_queue_claim(&q, &t) == 1 && t.id == 3);
    CHECK(task_queue_cancel(&q, 3) == 0);
    CHECK(task_queue_init(&recovered, journal_path()) == 0);
    CHECK(task_queue_get(&recovered, 3)->state == TASK_CANCELED);
    CHECK(task_queue_count_state(&recovered, TASK_RUNNING) == 0);
    CHECK(task_queue_count_state(&recovered, TASK_CANCEL_REQUESTED) == 0);
    reset_journal();
}

static void test_torn_and_corrupt_tail(void) {
    reset_journal();
    TaskQueue q, recovered;
    char id[TASK_ID_MAX];
    CHECK(task_queue_init(&q, journal_path()) == 0);
    CHECK(task_queue_add(&q, "durable task", id, sizeof(id)) == 0);

    int fd = open(journal_path(), O_WRONLY | O_APPEND);
    CHECK(fd >= 0);
    if (fd >= 0) { CHECK(write(fd, "torn", 4) == 4); close(fd); }
    CHECK(task_queue_init(&recovered, journal_path()) == 0);
    CHECK(task_queue_get(&recovered, 1) != NULL);
    CHECK(task_queue_add(&recovered, "append after repair", id, sizeof(id)) == 0);
    TaskQueue replay;
    CHECK(task_queue_init(&replay, journal_path()) == 0);
    CHECK(task_queue_get(&replay, 2) != NULL);

    /* A complete but invalid final record is also discarded. */
    fd = open(journal_path(), O_WRONLY | O_APPEND);
    CHECK(fd >= 0);
    if (fd >= 0) {
        unsigned char junk[64];
        memset(junk, 0xa5, sizeof(junk));
        CHECK(write(fd, junk, sizeof(junk)) == (ssize_t)sizeof(junk));
        close(fd);
    }
    TaskQueue repaired;
    CHECK(task_queue_init(&repaired, journal_path()) == 0);
    CHECK(task_queue_get(&repaired, 1) != NULL && task_queue_get(&repaired, 2) != NULL);
    reset_journal();
}

static void test_validation_and_terminal_states(void) {
    reset_journal();
    TaskQueue q;
    char id[TASK_ID_MAX];
    CHECK(task_queue_init(&q, journal_path()) == 0);
    CHECK(task_queue_add(&q, "", id, sizeof(id)) == -1);
    CHECK(task_queue_add(&q, "x", id, 2) == -1);
    CHECK(task_queue_add(&q, "ok", id, sizeof(id)) == 0);
    CHECK(task_queue_complete(&q, 1, "not running") == -1);
    CHECK(task_queue_fail(&q, 1, "not running") == -1);
    CHECK(task_queue_cancel(&q, 999) == -1);
    CHECK(task_queue_count_state(&q, TASK_QUEUED) == 1);
    reset_journal();
}

int main(void) {
    test_add_claim_and_single_active();
    test_restart_and_interrupted_work();
    test_cancel_transitions_and_recovery();
    test_torn_and_corrupt_tail();
    test_validation_and_terminal_states();
    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
