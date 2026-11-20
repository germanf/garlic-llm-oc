#include "task_queue.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define JOURNAL_MAGIC 0x47545131u /* GTQ1 */
#define JOURNAL_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    GarlicTask task;
    uint32_t checksum;
} JournalRecord;

static uint32_t checksum_bytes(const unsigned char *p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static uint64_t task_now(void) {
    time_t t = time(NULL);
    return t < 0 ? 0 : (uint64_t)t;
}

static size_t bounded_len(const char *s, size_t cap) {
    size_t n = 0;
    while (n < cap && s[n]) n++;
    return n;
}

static int record_valid(const JournalRecord *r) {
    return r->magic == JOURNAL_MAGIC && r->version == JOURNAL_VERSION &&
           r->task.id != 0 && r->task.state >= TASK_QUEUED && r->task.state <= TASK_CANCEL_REQUESTED &&
           bounded_len(r->task.prompt, sizeof(r->task.prompt)) < sizeof(r->task.prompt) &&
           bounded_len(r->task.result, sizeof(r->task.result)) < sizeof(r->task.result) &&
           bounded_len(r->task.error, sizeof(r->task.error)) < sizeof(r->task.error) &&
           r->checksum == checksum_bytes((const unsigned char *)r, offsetof(JournalRecord, checksum));
}

static int write_all(int fd, const void *buf, size_t n) {
    const unsigned char *p = (const unsigned char *)buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

static int read_all_at(int fd, void *buf, size_t n, off_t off) {
    unsigned char *p = (unsigned char *)buf;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pread(fd, p + done, n - done, off + (off_t)done);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) return -1;
        if (r == 0) break;
        done += (size_t)r;
    }
    return (int)done;
}

/* Roll back a failed append so later successful writes cannot be hidden behind
 * an incomplete record. The queue is single-owner, so the captured end offset
 * cannot be concurrently changed by another writer. */
static int persist(TaskQueue *q, const GarlicTask *task) {
    JournalRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = JOURNAL_MAGIC;
    r.version = JOURNAL_VERSION;
    r.task = *task;
    r.checksum = checksum_bytes((const unsigned char *)&r, offsetof(JournalRecord, checksum));

    int fd = open(q->journal_path, O_WRONLY | O_APPEND);
    if (fd < 0) return -1;
    off_t start = lseek(fd, 0, SEEK_END);
    if (start < 0) { int saved = errno; close(fd); errno = saved; return -1; }
    int ok = write_all(fd, &r, sizeof(r)) == 0 && fsync(fd) == 0;
    int saved = errno;
    if (!ok) {
        (void)ftruncate(fd, start);
        (void)fsync(fd);
    }
    close(fd);
    errno = saved;
    if (!ok) return -1;
    if (q->journal_records != UINT32_MAX) q->journal_records++;
    return 0;
}

static int task_index(const TaskQueue *q, uint32_t id) {
    for (size_t i = 0; i < q->count; i++) if (q->tasks[i].id == id) return (int)i;
    return -1;
}

static int replace_task(TaskQueue *q, const GarlicTask *task) {
    int i = task_index(q, task->id);
    if (i < 0) {
        if (q->count >= TASK_QUEUE_CAPACITY) return -1;
        i = (int)q->count++;
    }
    q->tasks[i] = *task;
    if (task->id >= q->next_id) q->next_id = task->id == UINT32_MAX ? 0 : task->id + 1;
    return 0;
}

static int active_state(uint32_t state) {
    return state == TASK_RUNNING || state == TASK_CANCEL_REQUESTED;
}

int task_queue_init(TaskQueue *q, const char *path) {
    if (!q || !path || !*path || strlen(path) >= sizeof(q->journal_path)) return -1;
    memset(q, 0, sizeof(*q));
    strcpy(q->journal_path, path);
    q->next_id = 1;

    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return -1; }
    off_t off = 0;
    while (off < st.st_size) {
        JournalRecord r;
        int n = read_all_at(fd, &r, sizeof(r), off);
        if (n < 0) { close(fd); return -1; }
        if ((size_t)n != sizeof(r) || !record_valid(&r)) {
            /* A damaged final record is a torn append. Corruption in the
             * middle is not silently discarded, since that could lose later
             * acknowledged task updates. */
            if (st.st_size - off > (off_t)sizeof(r) || ftruncate(fd, off) != 0 || fsync(fd) != 0) {
                close(fd);
                return -1;
            }
            break;
        }
        if (replace_task(q, &r.task) != 0) { close(fd); return -1; }
        if (q->journal_records != UINT32_MAX) q->journal_records++;
        off += (off_t)sizeof(r);
    }
    close(fd);

    /* A process may have exited after persisting RUNNING but before persisting
     * a result. Requeue for at-least-once execution. A durable cancellation
     * request, on the other hand, must not restart the canceled work. */
    q->ready = 1;
    for (size_t i = 0; i < q->count; i++) {
        GarlicTask t = q->tasks[i];
        if (t.state == TASK_RUNNING) t.state = TASK_QUEUED;
        else if (t.state == TASK_CANCEL_REQUESTED) t.state = TASK_CANCELED;
        else continue;
        t.updated_at = task_now();
        if (persist(q, &t) != 0) { q->ready = 0; return -1; }
        q->tasks[i] = t;
    }
    return 0;
}

int task_queue_add(TaskQueue *q, const char *prompt, char *id_out, size_t id_cap) {
    if (!q || !q->ready || !prompt || !*prompt ||
        bounded_len(prompt, TASK_PROMPT_MAX) >= TASK_PROMPT_MAX || q->count >= TASK_QUEUE_CAPACITY ||
        !q->next_id || !id_out || id_cap < TASK_ID_MAX) return -1;
    GarlicTask t;
    memset(&t, 0, sizeof(t));
    t.id = q->next_id;
    t.state = TASK_QUEUED;
    t.created_at = t.updated_at = task_now();
    strcpy(t.prompt, prompt);
    if (persist(q, &t) != 0) return -1;
    if (replace_task(q, &t) != 0) return -1;
    snprintf(id_out, id_cap, "task-%08u", t.id);
    return 0;
}

int task_queue_has_active(const TaskQueue *q) {
    if (!q || !q->ready) return 0;
    for (size_t i = 0; i < q->count; i++) if (active_state(q->tasks[i].state)) return 1;
    return 0;
}

int task_queue_claim(TaskQueue *q, GarlicTask *out) {
    if (!q || !q->ready || !out) return -1;
    if (task_queue_has_active(q)) return 0;
    for (size_t i = 0; i < q->count; i++) {
        if (q->tasks[i].state != TASK_QUEUED) continue;
        GarlicTask t = q->tasks[i];
        t.state = TASK_RUNNING;
        t.attempts++;
        t.updated_at = task_now();
        if (persist(q, &t) != 0) return -1;
        q->tasks[i] = t;
        *out = t;
        return 1;
    }
    return 0;
}

static int terminal(TaskQueue *q, uint32_t id, TaskState state, const char *text) {
    if (!q || !q->ready || !text) return -1;
    int i = task_index(q, id);
    if (i < 0 || q->tasks[i].state != TASK_RUNNING) return -1;
    GarlicTask t = q->tasks[i];
    size_t cap = state == TASK_COMPLETED ? sizeof(t.result) : sizeof(t.error);
    if (bounded_len(text, cap) >= cap) return -1;
    if (state == TASK_COMPLETED) strcpy(t.result, text);
    else strcpy(t.error, text);
    t.state = (uint32_t)state;
    t.updated_at = task_now();
    if (persist(q, &t) != 0) return -1;
    q->tasks[i] = t;
    return 0;
}

int task_queue_complete(TaskQueue *q, uint32_t id, const char *result) {
    return terminal(q, id, TASK_COMPLETED, result);
}

int task_queue_fail(TaskQueue *q, uint32_t id, const char *error) {
    return terminal(q, id, TASK_FAILED, error);
}

int task_queue_cancel(TaskQueue *q, uint32_t id) {
    if (!q || !q->ready) return -1;
    int i = task_index(q, id);
    if (i < 0) return -1;
    GarlicTask t = q->tasks[i];
    if (t.state == TASK_QUEUED) t.state = TASK_CANCELED;
    else if (t.state == TASK_RUNNING) t.state = TASK_CANCEL_REQUESTED;
    else if (t.state == TASK_CANCEL_REQUESTED) return 0;
    else return -1;
    t.updated_at = task_now();
    if (persist(q, &t) != 0) return -1;
    q->tasks[i] = t;
    return 0;
}

int task_queue_finish_cancel(TaskQueue *q, uint32_t id) {
    if (!q || !q->ready) return -1;
    int i = task_index(q, id);
    if (i < 0 || (q->tasks[i].state != TASK_RUNNING && q->tasks[i].state != TASK_CANCEL_REQUESTED)) return -1;
    GarlicTask t = q->tasks[i];
    t.state = TASK_CANCELED;
    t.updated_at = task_now();
    if (persist(q, &t) != 0) return -1;
    q->tasks[i] = t;
    return 0;
}

const GarlicTask *task_queue_get(const TaskQueue *q, uint32_t id) {
    if (!q || !q->ready) return NULL;
    int i = task_index(q, id);
    return i < 0 ? NULL : &q->tasks[i];
}

size_t task_queue_count_state(const TaskQueue *q, TaskState state) {
    size_t n = 0;
    if (q && q->ready) for (size_t i = 0; i < q->count; i++) n += q->tasks[i].state == (uint32_t)state;
    return n;
}
