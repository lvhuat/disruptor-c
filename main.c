

#include "disruptor.h"
#include <stdio.h>

struct command
{
    DATA_VOLATILE long long data;
    DATA_VOLATILE long long flag1;
    DATA_VOLATILE long long flag2;
    DATA_VOLATILE long long flag3;
    PADDING_BYTE(32)
};


#define RING_LEN 64 * 1024
#define RESERVES 16
struct command ring[RING_LEN] = {};
const long long buffer_mask = RING_LEN - 1;
const long long dur_nano = 1000000000L;
const long long dur_milli = 1000000L;

struct data_consumer1_1
{
    long long num;
    long long padding[7];
};

void example_consume1_1(void *args, sequeue_t lower, sequeue_t upper)
{
    struct data_consumer1_1 *data = (struct data_consumer1_1 *)args;
    while (lower <= upper)
    {

        data->num = lower;
        if (ring[buffer_mask & lower].data != lower)
        {
            printf("example_consume1_1:Data race!!!");
            exit(-1);
        }
        // ensure deal after consumer2
        if (ring[buffer_mask & lower].flag3 != 0)
        {
            printf("example_consume1_1:Data race on flag3,%lld,%lld", lower, ring[buffer_mask & lower].flag3);
            exit(-1);
        }
        ring[buffer_mask & lower].flag1 = 1;

        lower++;
    }
}

struct data_consumer1_2
{
    long long num;
    long long padding[7];
};

void example_consume1_2(void *args, sequeue_t lower, sequeue_t upper)
{
    struct data_consumer1_2 *data = (struct data_consumer1_2 *)args;
    while (lower <= upper)
    {

        data->num = lower;
        if (ring[buffer_mask & lower].data != lower)
        {
            printf("example_consume1_2:Data race!!!");
            exit(-1);
        }

        // ensure deal after consumer2
        if (ring[buffer_mask & lower].flag3 != 0)
        {
            printf("example_consume1_2:Data race on flag3,%lld,%lld", lower, ring[buffer_mask & lower].flag3);
            exit(-1);
        }

        ring[buffer_mask & lower].flag2 = 2;

        lower++;
    }
}

struct data_consumer2
{
    long long num;
    long long padding[7];
};

void example_consume2(void *args, sequeue_t lower, sequeue_t upper)
{
    struct data_consumer2 *data = (struct data_consumer2 *)args;
    while (lower <= upper)
    {

        data->num = lower;
        if (ring[buffer_mask & lower].data != lower)
        {
            printf("Data race!!!");
            exit(-1);
        }

        // ensure deal after consumer1_1
        if (ring[buffer_mask & lower].flag1 != 1)
        {
            printf("Data race on flag1,%lld,%lld", lower, ring[buffer_mask & lower].flag1);
            exit(-1);
        }
        
        // ensure deal after consumer1_2
        if (ring[buffer_mask & lower].flag2 != 2)
        {
            printf("Data race on flag2,%lld,%lld", lower, ring[buffer_mask & lower].flag2);
            exit(-1);
        }
        ring[buffer_mask & lower].flag3 = 3;

        lower++;
    }
}

const long long Iterations = 10000000000;

void publish(struct disruptor *d)
{
    size_t next_count = RESERVES;
    sequeue_t max_sequeued = -1;
    while (max_sequeued < Iterations)
    {
        max_sequeued = writer_reserve(d->writer, next_count);
        for (sequeue_t sequeued = max_sequeued - next_count + 1; sequeued <= max_sequeued; sequeued++)
        {
            ring[buffer_mask & sequeued].data = sequeued; // Set event data.
            ring[buffer_mask & sequeued].flag1 = 0;       // Set event data.
            ring[buffer_mask & sequeued].flag2 = 0;       // Set event data.
            ring[buffer_mask & sequeued].flag3 = 0;       // Set event data.
        }

        writer_commit(d->writer, max_sequeued - next_count + 1, max_sequeued);
    }
}

void print_struct_size()
{
    printf("CPU bits            %2ld Bits\n", 8 * sizeof(void *));
    printf("sizeof(reader)      %2ld Bytes\n", sizeof(struct reader));
    printf("sizeof(cursor)      %2ld Bytes\n", sizeof(struct cursor));
    printf("sizeof(disruptor)   %2ld Bytes\n", sizeof(struct disruptor));
    printf("sizeof(writer)      %2ld Bytes\n", sizeof(struct writer));
    printf("sizeof(command)     %2ld Bytes\n", sizeof(struct command));
}

const char *aligned(void *p)
{
    if ((unsigned long long)(p) % 8 == 0)
    {
        return "ok";
    }
    return "no";
}

void print_disruptor_memory(struct disruptor *d)
{
    printf("disruptor  %p aligned=%s\n", d, aligned(d));
    printf("readers    %p aligned=%s\n", d->readers, aligned(d->readers));
    printf("barriers   %p aligned=%s\n", d->barriers, aligned(d->barriers));
    printf("cursors    %p aligned=%s\n", d->cursors, aligned(d->cursors));
    printf("ringbuffer %p aligned=%s\n", ring, aligned(ring));
    printf("writer     %p aligned=%s\n", d->writer, aligned(d->writer));
}

// In X-system shell, build with command-line `gcc main.c -lpthread -O3`
/*
    |            |--------c1_1------>|              |
    |writer ---- |                   | ---- c2 ---->|
    |            |--------c1_2------>|              |
    
*/
int main()
{
    print_struct_size();

    struct disruptor_options dopt = {};
    disruptor_cfg_ringbuffer(&dopt, ring, sizeof(ring) / sizeof(ring[0]));

    struct consumer g1[2] = {};
    struct data_consumer1_1 *arg1_1 = (struct data_consumer1_1 *)malloc(sizeof(struct data_consumer1_1));
    g1[0].f = example_consume1_1;
    g1[0].arg = arg1_1;

    struct data_consumer1_2 *arg1_2 = (struct data_consumer1_2 *)malloc(sizeof(struct data_consumer1_2));
    g1[1].f = example_consume1_2;
    g1[1].arg = arg1_2;
    disruptor_cfg_consumer_grp(&dopt, g1, 2);

    struct consumer g2[1] = {};
    struct data_consumer2 data2 = {};
    g2[0].f = example_consume2;
    g2[0].arg = &data2;
    disruptor_cfg_consumer_grp(&dopt, g2, 1);

    struct disruptor d = {};
    disruptor_build(&d, &dopt);
    print_disruptor_memory(&d);
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
