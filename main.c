

#include "disruptor.h"

struct command ring[64 * 1000] = {};
const long long buffer_mask = 64 * 1000 - 1;

struct data_consume1_1
{
    long long num;
    char padding[8];
};

void example_consume1_1(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume1_1 *data = (struct data_consume1_1 *)consumer->arg;
    data->num += ring[lower&buffer_mask].data;
}

struct data_consume1_2
{
    long long num;
    char padding[8];
};

void example_consume1_2(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume1_2 *data = (struct data_consume1_2 *)consumer->arg;
    data->num += ring[lower&buffer_mask].data;
}

struct data_consume2
{
    long long num;
    char padding[8];
};

void example_consume2(struct consumer *consumer, sequeue_t lower, sequeue_t upper)
{
    struct data_consume2 *data = (struct data_consume2 *)consumer->arg;
    data->num += ring[lower&buffer_mask].data;
}

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
    c2.f = example_consume2;
    disruptor_cfg_consumer_grp(&dopt, &c2, 1);

    struct disruptor d;
    disruptor_build(&d, &dopt);
    disruptor_start(&d);

    sequeue_t seq = writer_reserve(&d.writer, 16);
    for (sequeue_t lower = seq-16+1; lower< seq; lower++)
    {
        writer_commit(&d.writer, seq - 16 + 1, seq);
        ring[buffer_mask&lower].data = lower;
    }
    disruptor_stop(&d);
}
