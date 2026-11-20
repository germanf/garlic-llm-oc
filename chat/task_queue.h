#ifndef GARLIC_TASK_QUEUE_H
#define GARLIC_TASK_QUEUE_H

#include <stddef.h>
#include <stdint.h>

/* Durable, single-process queue. All calls must be serialized by its owner
 * (the Garlic main loop); inference is deliberately never claimed in parallel. */
#define TASK_QUEUE_CAPACITY 32
#define TASK_PROMPT_MAX 2048
#define TASK_RESULT_MAX 4096
#define TASK_ERROR_MAX 256
#define TASK_ID_MAX 24

typedef enum {
    TASK_QUEUED = 1,
    TASK_RUNNING = 2,
    TASK_COMPLETED = 3,
    TASK_FAILED = 4,
    TASK_CANCELED = 5,
    TASK_CANCEL_REQUESTED = 6
} TaskState;

typedef struct {
    uint32_t id;
    uint32_t attempts;
    uint32_t state;
    uint64_t created_at;
    uint64_t updated_at;
    char prompt[TASK_PROMPT_MAX];
    char result[TASK_RESULT_MAX];
    char error[TASK_ERROR_MAX];
} GarlicTask;

typedef struct {
    char journal_path[256];
    GarlicTask tasks[TASK_QUEUE_CAPACITY];
    size_t count;
    uint32_t next_id;
    uint32_t journal_records;
    int ready;
} TaskQueue;

/* Replay journal. A torn tail is removed; interrupted RUNNING work is requeued
 * at least once (or marked canceled if cancellation had been requested). */
int task_queue_init(TaskQueue *q, const char *journal_path);
/* Persist and enqueue. Returns 0 or -1; id_out receives task-XXXXXXXX. */
int task_queue_add(TaskQueue *q, const char *prompt, char *id_out, size_t id_cap);
/* Claim FIFO work. Persisted RUNNING before return; 1=claimed, 0=none/another
 * active task, -1=I/O error. Exactly one task may be RUNNING. */
int task_queue_claim(TaskQueue *q, GarlicTask *out);
int task_queue_has_active(const TaskQueue *q);
/* Terminal transitions are durable before becoming visible in memory. */
int task_queue_complete(TaskQueue *q, uint32_t id, const char *result);
int task_queue_fail(TaskQueue *q, uint32_t id, const char *error);
/* Queued tasks cancel immediately; running tasks transition to
 * CANCEL_REQUESTED for the scheduler to stop at a safe generation boundary. */
int task_queue_cancel(TaskQueue *q, uint32_t id);
int task_queue_finish_cancel(TaskQueue *q, uint32_t id);
const GarlicTask *task_queue_get(const TaskQueue *q, uint32_t id);
size_t task_queue_count_state(const TaskQueue *q, TaskState state);

#endif
