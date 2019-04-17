

#include "disruptor.h"
#include <stdio.h>

#define RING_LEN 64 * 1024
struct command ring[RING_LEN] = {};
const long long buffer_mask = RING_LEN - 1;
const long long dur_nano = 1000000000L;
const long long dur_milli = 1000000L;

struct data_consume1_1
{
    long long num;
    PADDING
};

void example_consume1_1(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume1_1 *data = (struct data_consume1_1 *)consumer->arg;
    while (lower <= upper)
    {

        data->num = lower;
        if (ring[buffer_mask & lower].data != lower)
        {
            printf("Data race!!!");
            exit(-1);
        }

        lower++;
    }
}

struct data_consume1_2
{
    long long num;
    PADDING
};

void example_consume1_2(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume1_2 *data = (struct data_consume1_2 *)consumer->arg;
    while (lower <= upper)
    {

        data->num = lower;
        if (ring[buffer_mask & lower].data != lower)
        {
            printf("Data race!!!");
            exit(-1);
        }

        lower++;
    }
}

struct data_consume2
{
    long long num;
    PADDING
};

void example_consume2(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume2 *data = (struct data_consume2 *)consumer->arg;
    while (lower <= upper)
    {

        data->num = lower;
        if (ring[buffer_mask & lower].data != lower)
        {
            printf("Data race!!!");
            exit(-1);
        }

        lower++;
    }
}

const long long Iterations = 10000000000;

void publish(struct disruptor *d)
{
    size_t next_count = 16;
    sequeue_t max_sequeued = -1;
    while (max_sequeued < Iterations)
    {
        max_sequeued = writer_reserve(&d->writer, next_count);
        for (sequeue_t sequeued = max_sequeued - next_count + 1; sequeued <= max_sequeued; sequeued++)
        {
            ring[buffer_mask & sequeued].data = sequeued; // Set event data.
        }
        writer_commit(&d->writer, max_sequeued - next_count + 1, max_sequeued);
    }
}

// In X-system shell, build with command-line `gcc main.c -lpthread -O3`
int main()
{
    struct disruptor_options dopt = {};
    disruptor_cfg_ringbuffer(&dopt, ring, sizeof(ring) / sizeof(ring[0]));

    struct consumer c1[2] = {};
    struct data_consume1_1 *arg1_1 = (struct data_consume1_1 *)malloc(sizeof(struct data_consume1_1));
    c1[0].f = example_consume1_1;
    c1[0].arg = arg1_1;

    struct data_consume1_2 *arg1_2 = (struct data_consume1_2 *)malloc(sizeof(struct data_consume1_2));
    c1[1].f = example_consume1_2;
    c1[1].arg = arg1_2;
    disruptor_cfg_consumer_grp(&dopt, c1, 2);

    struct consumer c2 = {};
    struct data_consume2 data2 = {};
    c2.f = example_consume2;
    c2.arg = &data2;
    disruptor_cfg_consumer_grp(&dopt, &c2, 1);

    struct disruptor d = {};
    disruptor_build(&d, &dopt);
    disruptor_start(&d);
    sleep(1);

    struct timespec start, finish;
    clock_gettime(CLOCK_REALTIME, &start);
    publish(&d);
    disruptor_stop(&d);
    clock_gettime(CLOCK_REALTIME, &finish);

    long long start_nano = start.tv_sec * dur_nano + start.tv_nsec;
    long long finish_nano = finish.tv_sec * dur_nano + finish.tv_nsec;
    printf("data result:%lld\n", data2.num);
    printf("cost millisecond : %lld ms\n", (finish_nano - start_nano) / dur_milli);
}
