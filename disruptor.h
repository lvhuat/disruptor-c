#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sched.h>

#define DATA_VOLATILE volatile
#define PADDING_BYTE(x) char padding[x];

void nano_sleep(int sec, int nsec)
{
    struct timespec req, rem;
    req.tv_sec = sec;
    req.tv_nsec = nsec;
    nanosleep(&req, &rem);
}

void yield()
{
    sched_yield();
}

#define sequeue_t long long

struct cursor
{
    DATA_VOLATILE sequeue_t seq;
    PADDING_BYTE(56)
};

#define INIT_CURSOR_VALUE -1

sequeue_t cursor_reset(struct cursor *c)
{
    c->seq = INIT_CURSOR_VALUE;
}

sequeue_t cursor_reset_many(struct cursor *c, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        c[i].seq = INIT_CURSOR_VALUE;
    }
}

sequeue_t cursor_load(struct cursor *c)
{
    return c->seq;
}

sequeue_t cursor_set(struct cursor *c, sequeue_t n)
{
    return c->seq = n;
}

struct barrier
{
    struct cursor *cursor;
    size_t count;
};

sequeue_t barrier_read(struct barrier *barrier, sequeue_t id)
{
    if (barrier->count == 1)
    {
        return cursor_load(barrier->cursor);
    }

    sequeue_t min = cursor_load(barrier->cursor);
    for (size_t i = 1; i < barrier->count; i++)
    {
        sequeue_t seq = cursor_load(&barrier->cursor[i]);
        if (min > cursor_load(&barrier->cursor[i]))
        {
            min = seq;
        }
    }

    return min;
}

struct consumer;
typedef void (*consumer_func_t)(void *, sequeue_t, sequeue_t);

struct consumer
{
    void *arg;
    consumer_func_t f;
};

struct reader
{
    struct cursor *cursor;
    struct barrier *barrier;
    void *arg;
    consumer_func_t f;
    int running;
    pthread_t thread;

    PADDING_BYTE(12);
};

void *reader_recv_loop(void *p)
{
    struct reader *r = (struct reader *)p;
    sequeue_t prev = cursor_load(r->cursor);
    sequeue_t lower = prev + 1;
    sequeue_t upper = barrier_read(r->barrier, lower);
    long long wait_counter = 0;
    while (1)
    {
        if (lower <= upper)
        {
            (*r->f)(r->arg, lower, upper);
            cursor_set(r->cursor, upper);
            prev = upper;
            lower = prev + 1;
        }
        else if (!r->running)
        {
            break;
        }

        // Wait strategy
        // Use SleepingWaitStrategy:https://bit.ly/2GhQ3ZS
        wait_counter = 200;
        while ((lower > (upper = barrier_read(r->barrier, 0))) && r->running)
        {
            if (wait_counter > 100)
            {
                --wait_counter;
            }
            else if (wait_counter > 0)
            {
                --wait_counter;
                yield();
            }

            nano_sleep(0L, 1L); // -1
        }
    }

    return NULL;
}

void reader_start(struct reader *r)
{
    r->running = 1;
    int ret = pthread_create(&r->thread, NULL, reader_recv_loop, r);
    assert(ret == 0);
}

void reader_stop(struct reader *r)
{
    r->running = 0;
    int ret = pthread_join(r->thread, NULL);
    assert(ret == 0);
}

struct writer
{
    size_t buffer_len;
    struct barrier *barrier;
    sequeue_t prev;
    struct cursor *cursor;
    PADDING_BYTE(32);
};

const int SpinMask = 1024 * 16 - 1;

sequeue_t writer_reserve(struct writer *writer, size_t count)
{
    sequeue_t nextSeq = writer->prev + count;
    sequeue_t gate = barrier_read(writer->barrier, 0);
    for (size_t spin = 0; nextSeq > gate + (sequeue_t)writer->buffer_len; spin++)
    {
        if (spin & SpinMask == 0)
        {
            nano_sleep(0L, 1L);
        }

        gate = barrier_read(writer->barrier, 0);
    }

    writer->prev = nextSeq;
    return writer->prev;
}

sequeue_t writer_commit(struct writer *writer, sequeue_t lower, sequeue_t upper)
{
    cursor_set(writer->cursor, upper);
}

struct consumer_group
{
    size_t consumer_len;
    struct consumer *consumers;
};

struct disruptor_options
{
    size_t ring_len;
    struct command *ring; // ring buffer;
    size_t consumer_group_len;
    struct consumer_group *consumer_groups;
};

struct disruptor
{
    size_t cursor_len;
    struct cursor *cursors;
    struct reader *readers;
    struct barrier *barriers;
    struct consumer *consumers;
    struct writer *writer;
    PADDING_BYTE(12);
};

void disruptor_build(struct disruptor *d, const struct disruptor_options *dopt)
{
    int consumer_len = 0;
    for (size_t group_it = 0; group_it < dopt->consumer_group_len; group_it++)
    {
        consumer_len += dopt->consumer_groups[group_it].consumer_len;
    }
    d->cursor_len = consumer_len + 1;

    // the cursors[0] is used for the writer and the barrier of the first group.
    d->cursors = (struct cursor *)calloc(sizeof(struct cursor), consumer_len + 1);
    cursor_reset_many(d->cursors, consumer_len + 1);
    //d->consumers = (struct consumer *)calloc(sizeof(struct consumer), consumer_len);
    d->readers = (struct reader *)calloc(sizeof(struct reader), consumer_len);
    d->barriers = (struct barrier *)calloc(sizeof(struct barrier), dopt->consumer_group_len + 1);

    d->barriers[0].cursor = d->cursors;
    d->barriers[0].count = 1;

    size_t cursor_index = 1;
    struct barrier *barrier = d->barriers;

    for (size_t group_it = 0; group_it < dopt->consumer_group_len; group_it++)
    {
        struct consumer_group *group = dopt->consumer_groups + group_it;
        sequeue_t temp_cursor_index = cursor_index;
        for (size_t consumer_it = 0; consumer_it < group->consumer_len; consumer_it++)
        {
            struct cursor *cursor = d->cursors + cursor_index;
            struct reader *reader = d->readers + cursor_index - 1;
            reader->f = group->consumers[consumer_it].f;
            reader->arg = group->consumers[consumer_it].arg;
            reader->cursor = cursor;
            reader->barrier = barrier;
            cursor_index++;
        }

        barrier = d->barriers + group_it + 1;
        barrier->cursor = d->cursors + temp_cursor_index;
        barrier->count = group->consumer_len;
    }

    d->writer = (struct writer *)calloc(sizeof(struct writer), 1);
    struct writer *writer = d->writer;
    writer->barrier = barrier;
    writer->cursor = d->cursors; // the first one
    writer->buffer_len = dopt->ring_len;
}

void disruptor_cfg_consumer_grp(struct disruptor_options *d, struct consumer *consumers, size_t len)
{
    struct consumer_group *newg = malloc(sizeof(struct consumer_group) * (d->consumer_group_len + 1));
    if (d->consumer_group_len > 0)
    {
        memcpy(newg, d->consumer_groups, sizeof(struct consumer_group) * d->consumer_group_len);
        free(d->consumer_groups);
    }

    struct consumer_group *newgroup = newg + d->consumer_group_len;
    newgroup->consumers = malloc(sizeof(struct consumer) * len);
    memcpy(newgroup->consumers, consumers, sizeof(struct consumer) * len);
    newgroup->consumer_len = len;

    d->consumer_groups = newg;
    d->consumer_group_len++;
}

void disruptor_cfg_ringbuffer(struct disruptor_options *d, struct command *ring, size_t len)
{
    assert(len > 0);
    assert(ring != NULL);
    assert((len & (len - 1)) == 0);

    d->ring = ring;
    d->ring_len = len;
}

void disruptor_start(struct disruptor *d)
{
    for (size_t i = 0; i < d->cursor_len - 1; i++)
    {
        reader_start(&d->readers[i]);
    }
}

void disruptor_stop(struct disruptor *d)
{
    for (size_t i = 0; i < d->cursor_len - 1; i++)
    {
        reader_stop(&d->readers[i]);
    }
}