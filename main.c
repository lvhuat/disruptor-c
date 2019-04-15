

#include "disruptor.h"
#include <stdio.h>

struct command ring[64 * 1000] = {};
const long long buffer_mask = 64 * 1000 - 1;
const long long dur_nano = 1000000000L;
const long long dur_milli = 1000000L;

// struct data_consume1_1
// {
//     long long num;
//     char padding[8];
// };

// void example_consume1_1(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
// {
//     struct data_consume1_1 *data = (struct data_consume1_1 *)consumer->arg;
//     for (size_t i = lower; i <= upper; i++)
//     {
//         data->num += ring[lower & buffer_mask].data;
//     }
//     //printf("example_consume1_1:lower=%d upper=%d\n", lower, upper);
// }

// struct data_consume1_2
// {
//     long long num;
//     char padding[8];
// };

// void example_consume1_2(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
// {
//     struct data_consume1_2 *data = (struct data_consume1_2 *)consumer->arg;
//     for (size_t i = lower; i <= upper; i++)
//     {
//         data->num += ring[lower & buffer_mask].data;
//     }
//     //printf("example_consume1_2:lower=%d upper=%d\n", lower, upper);
// }

struct data_consume2
{
    long long num;
    char padding[8];
};

void example_consume2(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume2 *data = (struct data_consume2 *)consumer->arg;
    for (size_t i = lower; i <= upper; i++)
    {
        //printf("%ld\n", i);
        data->num++;
    }

    if (lower % 1000 == 0)
    {
        printf("####%d,%d\n", lower, upper);
    }
}

void publish(struct disruptor *d)
{
    sequeue_t seq = writer_reserve(&d->writer, 16);
    //printf("----%d\n", seq);
    for (sequeue_t lower = seq - 16 + 1; lower < seq; lower++)
    {
        ring[buffer_mask & lower].data = lower; // Set event data.
    }

    writer_commit(&d->writer, seq - 16 + 1, seq);
}

// In X-system shell, build with command-line `gcc main.c -lpthread`
int main()
{
    struct disruptor_options dopt = {};
    disruptor_cfg_ringbuffer(&dopt, ring, sizeof(ring) / sizeof(ring[0]));

    // struct consumer c1[2] = {};
    // struct data_consume1_1 *arg1_1 = (struct data_consume1_1 *)malloc(sizeof(struct data_consume1_1));
    // c1[0].f = example_consume1_1;
    // c1[0].arg = arg1_1;

    // struct data_consume1_2 *arg1_2 = (struct data_consume1_2 *)malloc(sizeof(struct data_consume1_2));
    // c1[1].f = example_consume1_2;
    // c1[1].arg = arg1_2;
    // disruptor_cfg_consumer_grp(&dopt, c1, 2);

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

    for (size_t i = 0; i < 100000000; i++)
    {
        publish(&d);
    }

    disruptor_stop(&d);
    clock_gettime(CLOCK_REALTIME, &finish);

    long long start_nano = start.tv_sec * dur_nano + start.tv_nsec;
    long long finish_nano = finish.tv_sec * dur_nano + finish.tv_nsec;
    printf("data result:%lld\n", data2.num);
    printf("cost millisecond : %lld\n", (finish_nano-start_nano)/dur_milli);
}
