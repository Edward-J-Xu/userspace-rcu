// SPDX-FileCopyrightText: 2010 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
// SPDX-FileCopyrightText: 2010 Paolo Bonzini <pbonzini@redhat.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * Userspace RCU library - example RCU-based lock-free queue
 */

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include <urcu/arch.h>
#include <urcu/assert.h>
#include <urcu/tls-compat.h>
#include "thread-id.h"

/* hardcoded number of CPUs */
#define NR_CPUS 16384

#ifndef DYNAMIC_LINK_TEST
#define _LGPL_SOURCE
#endif
#include <urcu.h>
#include <urcu/cds.h>

static volatile int test_go, test_stop;

static unsigned long rduration;

static unsigned long duration;

/* read-side C.S. duration, in loops */
static unsigned long wdelay;

static inline void loop_sleep(unsigned long loops)
{
	while (loops-- != 0)
		caa_cpu_relax();
}

static int verbose_mode;

#define printf_verbose(fmt, args...)		\
	do {					\
		if (verbose_mode)		\
			printf(fmt, args);	\
	} while (0)

static unsigned int cpu_affinities[NR_CPUS];
static unsigned int next_aff = 0;
static int use_affinity = 0;

pthread_mutex_t affinity_mutex = PTHREAD_MUTEX_INITIALIZER;

static void set_affinity(void)
{
#ifdef HAVE_SCHED_SETAFFINITY
	cpu_set_t mask;
	int cpu, ret;
#endif /* HAVE_SCHED_SETAFFINITY */

	if (!use_affinity)
		return;

#ifdef HAVE_SCHED_SETAFFINITY
	ret = pthread_mutex_lock(&affinity_mutex);
	if (ret) {
		perror("Error in pthread mutex lock");
		exit(-1);
	}
	cpu = cpu_affinities[next_aff++];
	ret = pthread_mutex_unlock(&affinity_mutex);
	if (ret) {
		perror("Error in pthread mutex unlock");
		exit(-1);
	}

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	sched_setaffinity(0, sizeof(mask), &mask);
#endif /* HAVE_SCHED_SETAFFINITY */
}

/*
 * returns 0 if test should end.
 */
static int test_duration_dequeue(void)
{
	return !test_stop;
}

static int test_duration_enqueue(void)
{
	return !test_stop;
}

static DEFINE_URCU_TLS(unsigned long long, nr_dequeues);
static DEFINE_URCU_TLS(unsigned long long, nr_enqueues);

static DEFINE_URCU_TLS(unsigned long long, nr_successful_dequeues);
static DEFINE_URCU_TLS(unsigned long long, nr_successful_enqueues);

static unsigned int nr_enqueuers;
static unsigned int nr_dequeuers;

struct test {
	struct cds_lfq_node_rcu list;
	struct rcu_head rcu;
};

static struct cds_lfq_queue_rcu q;

static
void *thr_enqueuer(void *_count)
{
	unsigned long long *count = _count;

	printf_verbose("thread_begin %s, tid %lu\n",
			"enqueuer", urcu_get_thread_id());

	set_affinity();

	rcu_register_thread();

	while (!test_go)
	{
	}
	cmm_smp_mb();

	for (;;) {
		struct test *node = malloc(sizeof(*node));
		if (!node)
			goto fail;
		cds_lfq_node_init_rcu(&node->list);
		rcu_read_lock();
		cds_lfq_enqueue_rcu(&q, &node->list);
		rcu_read_unlock();
		URCU_TLS(nr_successful_enqueues)++;

		if (caa_unlikely(wdelay))
			loop_sleep(wdelay);
fail:
		URCU_TLS(nr_enqueues)++;
		if (caa_unlikely(!test_duration_enqueue()))
			break;
	}

	rcu_unregister_thread();

	count[0] = URCU_TLS(nr_enqueues);
	count[1] = URCU_TLS(nr_successful_enqueues);
	printf_verbose("enqueuer thread_end, tid %lu, "
			"enqueues %llu successful_enqueues %llu\n",
			urcu_get_thread_id(),
			URCU_TLS(nr_enqueues),
			URCU_TLS(nr_successful_enqueues));
	return ((void*)1);

}

static
void free_node_cb(struct rcu_head *head)
{
	struct test *node =
		caa_container_of(head, struct test, rcu);
	free(node);
}

static
void *thr_dequeuer(void *_count)
{
	unsigned long long *count = _count;

	printf_verbose("thread_begin %s, tid %lu\n",
			"dequeuer", urcu_get_thread_id());

	set_affinity();

	rcu_register_thread();

	while (!test_go)
	{
	}
	cmm_smp_mb();

	for (;;) {
		struct cds_lfq_node_rcu *qnode;

		rcu_read_lock();
		qnode = cds_lfq_dequeue_rcu(&q);
		rcu_read_unlock();

		if (qnode) {
			struct test *node;

			node = caa_container_of(qnode, struct test, list);
			call_rcu(&node->rcu, free_node_cb);
			URCU_TLS(nr_successful_dequeues)++;
		}

		URCU_TLS(nr_dequeues)++;
		if (caa_unlikely(!test_duration_dequeue()))
			break;
		if (caa_unlikely(rduration))
			loop_sleep(rduration);
	}

	rcu_unregister_thread();
	printf_verbose("dequeuer thread_end, tid %lu, "
			"dequeues %llu, successful_dequeues %llu\n",
			urcu_get_thread_id(),
			URCU_TLS(nr_dequeues),
			URCU_TLS(nr_successful_dequeues));
	count[0] = URCU_TLS(nr_dequeues);
	count[1] = URCU_TLS(nr_successful_dequeues);
	return ((void*)2);
}

static
void test_end(unsigned long long *nr_dequeues_l)
{
	struct cds_lfq_node_rcu *snode;

	do {
		snode = cds_lfq_dequeue_rcu(&q);
		if (snode) {
			struct test *node;

			node = caa_container_of(snode, struct test, list);
			free(node);	/* no more concurrent access */
			(*nr_dequeues_l)++;
		}
	} while (snode);
}

static
void show_usage(char **argv)
{
	printf("Usage : %s nr_dequeuers nr_enqueuers duration (s) <OPTIONS>\n",
		argv[0]);
	printf("OPTIONS:\n");
	printf("	[-d delay] (enqueuer period (in loops))\n");
	printf("	[-c duration] (dequeuer period (in loops))\n");
	printf("	[-v] (verbose output)\n");
	printf("	[-a cpu#] [-a cpu#]... (affinity)\n");
	printf("\n");
}

int main(int argc, char **argv)
{
	int err;
	pthread_t *tid_enqueuer, *tid_dequeuer;
	void *tret;
	unsigned long long *count_enqueuer, *count_dequeuer;
	unsigned long long tot_enqueues = 0, tot_dequeues = 0;
	unsigned long long tot_successful_enqueues = 0,
			   tot_successful_dequeues = 0;
	unsigned long long end_dequeues = 0;
	int i, a;
	unsigned int i_thr;

	if (argc < 4) {
		show_usage(argv);
		return -1;
	}

	err = sscanf(argv[1], "%u", &nr_dequeuers);
	if (err != 1) {
		show_usage(argv);
		return -1;
	}

	err = sscanf(argv[2], "%u", &nr_enqueuers);
	if (err != 1) {
		show_usage(argv);
		return -1;
	}

	err = sscanf(argv[3], "%lu", &duration);
	if (err != 1) {
		show_usage(argv);
		return -1;
	}

	for (i = 4; i < argc; i++) {
		if (argv[i][0] != '-')
			continue;
		switch (argv[i][1]) {
		case 'a':
			if (argc < i + 2) {
				show_usage(argv);
				return -1;
			}
			a = atoi(argv[++i]);
			cpu_affinities[next_aff++] = a;
			use_affinity = 1;
			printf_verbose("Adding CPU %d affinity\n", a);
			break;
		case 'c':
			if (argc < i + 2) {
				show_usage(argv);
				return -1;
			}
			rduration = atol(argv[++i]);
			break;
		case 'd':
			if (argc < i + 2) {
				show_usage(argv);
				return -1;
			}
			wdelay = atol(argv[++i]);
			break;
		case 'v':
			verbose_mode = 1;
			break;
		}
	}

	printf_verbose("running test for %lu seconds, %u enqueuers, "
		       "%u dequeuers.\n",
		       duration, nr_enqueuers, nr_dequeuers);
	printf_verbose("Writer delay : %lu loops.\n", rduration);
	printf_verbose("Reader duration : %lu loops.\n", wdelay);
	printf_verbose("thread %-6s, tid %lu\n",
			"main", urcu_get_thread_id());

	tid_enqueuer = calloc(nr_enqueuers, sizeof(*tid_enqueuer));
	tid_dequeuer = calloc(nr_dequeuers, sizeof(*tid_dequeuer));
	count_enqueuer = calloc(nr_enqueuers, 2 * sizeof(*count_enqueuer));
	count_dequeuer = calloc(nr_dequeuers, 2 * sizeof(*count_dequeuer));
	cds_lfq_init_rcu(&q, call_rcu);
	err = create_all_cpu_call_rcu_data(0);
	if (err) {
		printf("Per-CPU call_rcu() worker threads unavailable. Using default global worker thread.\n");
	}

	next_aff = 0;

	for (i_thr = 0; i_thr < nr_enqueuers; i_thr++) {
		err = pthread_create(&tid_enqueuer[i_thr], NULL, thr_enqueuer,
				     &count_enqueuer[2 * i_thr]);
		if (err != 0)
			exit(1);
	}
	for (i_thr = 0; i_thr < nr_dequeuers; i_thr++) {
		err = pthread_create(&tid_dequeuer[i_thr], NULL, thr_dequeuer,
				     &count_dequeuer[2 * i_thr]);
		if (err != 0)
			exit(1);
	}

	cmm_smp_mb();

	test_go = 1;

	for (i_thr = 0; i_thr < duration; i_thr++) {
		sleep(1);
		if (verbose_mode) {
			fwrite(".", sizeof(char), 1, stdout);
			fflush(stdout);
		}
	}

	test_stop = 1;

	for (i_thr = 0; i_thr < nr_enqueuers; i_thr++) {
		err = pthread_join(tid_enqueuer[i_thr], &tret);
		if (err != 0)
			exit(1);
		tot_enqueues += count_enqueuer[2 * i_thr];
		tot_successful_enqueues += count_enqueuer[2 * i_thr + 1];
	}
	for (i_thr = 0; i_thr < nr_dequeuers; i_thr++) {
		err = pthread_join(tid_dequeuer[i_thr], &tret);
		if (err != 0)
			exit(1);
		tot_dequeues += count_dequeuer[2 * i_thr];
		tot_successful_dequeues += count_dequeuer[2 * i_thr + 1];
	}

	test_end(&end_dequeues);
	err = cds_lfq_destroy_rcu(&q);
	urcu_posix_assert(!err);

	printf_verbose("total number of enqueues : %llu, dequeues %llu\n",
		       tot_enqueues, tot_dequeues);
	printf_verbose("total number of successful enqueues : %llu, "
		       "successful dequeues %llu\n",
		       tot_successful_enqueues, tot_successful_dequeues);
	printf("SUMMARY %-25s testdur %4lu nr_enqueuers %3u wdelay %6lu "
		"nr_dequeuers %3u "
		"rdur %6lu nr_enqueues %12llu nr_dequeues %12llu "
		"successful enqueues %12llu successful dequeues %12llu "
		"end_dequeues %llu nr_ops %12llu\n",
		argv[0], duration, nr_enqueuers, wdelay,
		nr_dequeuers, rduration, tot_enqueues, tot_dequeues,
		tot_successful_enqueues,
		tot_successful_dequeues, end_dequeues,
		tot_enqueues + tot_dequeues);
	if (tot_successful_enqueues != tot_successful_dequeues + end_dequeues)
		printf("WARNING! Discrepancy between nr succ. enqueues %llu vs "
		       "succ. dequeues + end dequeues %llu.\n",
		       tot_successful_enqueues,
		       tot_successful_dequeues + end_dequeues);

	free_all_cpu_call_rcu_data();
	free(count_enqueuer);
	free(count_dequeuer);
	free(tid_enqueuer);
	free(tid_dequeuer);

	return 0;
}
