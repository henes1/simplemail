/**
 * coroutines.c - simple coroutines for SimpleMail.
 * Copyright (C) 2015  Sebastian Bauer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file coroutines.c
 */

#include "coroutines.h"

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "lists.h"

/*****************************************************************************/

#define MAX(a,b) ((a)>(b)?(a):(b))

/*****************************************************************************/

/**
 * A simple coroutine.
 */
struct coroutine
{
	/** Embedded node structure for adding it to lists */
	struct node node;

	/** The context parameter of the coroutine */
	struct coroutine_basic_context *context;

	/** The actual entry of the coroutine */
	coroutine_entry_t entry;
};

/**
 * A simple list of elements of type coroutine_t.
 */
struct coroutines_list
{
	struct list list;
};

/**
 * A simple scheduler for coroutines.
 */
struct coroutine_scheduler
{
	/** Contains all ready coroutines. Elements are of type coroutine_t */
	struct coroutines_list coroutines_ready_list;

	/** Contains all waiting coroutines. Elements are of type coroutine_t */
	struct coroutines_list waiting_coroutines_list;

	/** Contains all finished coroutines. Elements are of type coroutine_t */
	struct coroutines_list finished_coroutines_list;

	/**
	 * Function that is invoked to wait or poll for a next event
	 *
	 * @return if there were events that potentially were blocked
	 */
	int (*wait_for_event)(coroutine_scheduler_t sched, int poll, void *udata);

	/** User data passed to wait_for_event() */
	void *wait_for_event_udata;
};

struct coroutine_scheduler_fd_data
{
	/** Highest number of descriptors to wait for */
	int nfds;

	/** Set of read fds to wait for */
	fd_set readfds;

	/** Set of write fds to wait for */
	fd_set writefds;
};

/*****************************************************************************/

/**
 * Initializes a coroutines list.
 *
 * @param list the list to initialize.
 */
static void coroutines_list_init(struct coroutines_list *list)
{
	list_init(&list->list);
}

/**
 * Returns the first element of a coroutines list.
 *
 * @param list the coroutines list.
 * @return the first element or NULL if the list is empty
 */
static coroutine_t coroutines_list_first(struct coroutines_list *list)
{
	return (coroutine_t)list_first(&list->list);
}

/**
 * Returns the next coroutine within the same list.
 *
 * @param c the coroutine whose successor shall be determined
 * @return the successor of c or NULL if c was the last element
 */
static coroutine_t coroutines_next(coroutine_t c)
{
	return (coroutine_t)node_next(&c->node);
}

/*****************************************************************************/

/**
 * Prepare the set of fds for the given scheduler.
 *
 * @param scheduler
 */
static void coroutine_schedule_prepare_fds(coroutine_scheduler_t scheduler, struct coroutine_scheduler_fd_data *data)
{
	coroutine_t cor, cor_next;

	fd_set *readfds = &data->readfds;
	fd_set *writefds = &data->writefds;

	FD_ZERO(readfds);
	FD_ZERO(writefds);
	data->nfds = -1;

	cor = coroutines_list_first(&scheduler->waiting_coroutines_list);
	for (;cor;cor = cor_next)
	{
		cor_next =  coroutines_next(cor);

		if (cor->context->socket_fd >= 0)
		{
			data->nfds = MAX(cor->context->socket_fd, data->nfds);

			if (cor->context->write_mode)
			{
				FD_SET(cor->context->socket_fd, writefds);
			} else
			{
				FD_SET(cor->context->socket_fd, readfds);
			}
		}
	}
}

/**
 * Standard wait for event function using select().
 *
 * @param poll
 * @param udata
 */
static int coroutine_wait_for_fd_event(coroutine_scheduler_t sched, int poll, void *udata)
{
	struct coroutine_scheduler_fd_data *data = (struct coroutine_scheduler_fd_data *)udata;

	struct timeval zero_timeout = {0};

	coroutine_schedule_prepare_fds(sched, data);

	if (data->nfds >= 0)
	{
		select(data->nfds+1, &data->readfds, &data->writefds, NULL, poll?&zero_timeout:NULL);
		return 1;
	}
	return 0;
}

/*****************************************************************************/

coroutine_scheduler_t coroutine_scheduler_new_custom(int (*wait_for_event)(coroutine_scheduler_t sched, int poll, void *udata), void *udata)
{
	coroutine_scheduler_t scheduler;

	if (!(scheduler = (coroutine_scheduler_t)malloc(sizeof(*scheduler))))
		return NULL;

	coroutines_list_init(&scheduler->coroutines_ready_list);
	coroutines_list_init(&scheduler->waiting_coroutines_list);
	coroutines_list_init(&scheduler->finished_coroutines_list);

	scheduler->wait_for_event = wait_for_event;
	scheduler->wait_for_event_udata = udata;

	return scheduler;
}

/*****************************************************************************/

coroutine_scheduler_t coroutine_scheduler_new(void)
{
	struct coroutine_scheduler_fd_data *data;
	coroutine_scheduler_t sched;

	if (!(data = malloc(sizeof(*data))))
		return NULL;

	if (!(sched = coroutine_scheduler_new_custom(coroutine_wait_for_fd_event, data)))
	{
		free(data);
		return NULL;
	}
	return sched;
}

/*****************************************************************************/

void coroutine_scheduler_dispose(coroutine_scheduler_t scheduler)
{
	free(scheduler);
}

/*****************************************************************************/

void coroutine_await_socket(struct coroutine_basic_context *context, int socket_fd, int write)
{
	context->socket_fd = socket_fd;
	context->write_mode = write;
	context->is_now_ready = coroutine_is_fd_now_ready;
}

/*****************************************************************************/

coroutine_t coroutine_add(coroutine_scheduler_t scheduler, coroutine_entry_t entry, struct coroutine_basic_context *context)
{
	coroutine_t coroutine;

	if (!(coroutine = malloc(sizeof(*coroutine))))
		return NULL;
	coroutine->entry = entry;
	coroutine->context = context;
	list_insert_tail(&scheduler->coroutines_ready_list.list, &coroutine->node);
	return coroutine;
}

/*****************************************************************************/

void coroutine_schedule_ready(coroutine_scheduler_t scheduler)
{
	coroutine_t cor = coroutines_list_first(&scheduler->coroutines_ready_list);
	coroutine_t cor_next;

	/* Execute all non-waiting coroutines */
	for (;cor;cor = cor_next)
	{
		coroutine_return_t cor_ret;

		cor_next =  coroutines_next(cor);
		cor_ret = cor->entry(cor->context);
		switch (cor_ret)
		{
			case	COROUTINE_DONE:
					node_remove(&cor->node);
					list_insert_tail(&scheduler->finished_coroutines_list.list, &cor->node);
					break;

			case	COROUTINE_YIELD:
					/* Nothing special to do here */
					break;

			case	COROUTINE_WAIT:
					node_remove(&cor->node);
					list_insert_tail(&scheduler->waiting_coroutines_list.list,&cor->node);
					break;
		}
	}
}

/**
 * Returns whether there are any unfinished coroutines.
 *
 * @param scheduler
 * @return
 */
static int coroutine_has_unfinished_coroutines(coroutine_scheduler_t scheduler)
{
	return coroutines_list_first(&scheduler->coroutines_ready_list)
			|| coroutines_list_first(&scheduler->waiting_coroutines_list);
}

/*****************************************************************************/

int coroutine_is_fd_now_ready(coroutine_scheduler_t scheduler, coroutine_t cor)
{
	struct coroutine_scheduler_fd_data *data = (struct coroutine_scheduler_fd_data *)scheduler->wait_for_event_udata;

	if (cor->context->write_mode)
	{
		if (FD_ISSET(cor->context->socket_fd, &data->writefds))
			return 1;
	} else
	{
		if (FD_ISSET(cor->context->socket_fd, &data->readfds))
			return 1;
	}
	return 0;
}

/*****************************************************************************/

void coroutine_schedule(coroutine_scheduler_t scheduler)
{
	while (coroutine_has_unfinished_coroutines(scheduler))
	{
		int polling;

		coroutine_t cor, cor_next;

		coroutine_schedule_ready(scheduler);

		cor = coroutines_list_first(&scheduler->waiting_coroutines_list);
		for (;cor;cor = cor_next)
		{
			coroutine_t f;

			cor_next =  coroutines_next(cor);

			/* Check if we are waiting for another coroutine to be finished
			 * FIXME: This needs only be done once when the coroutine that we
			 * are waiting for is done */
			f = coroutines_list_first(&scheduler->finished_coroutines_list);
			while (f)
			{
				if ((f = cor->context->other))
				{
					/* Move from waiting to ready queue */
					node_remove(&cor->node);
					list_insert_tail(&scheduler->coroutines_ready_list.list, &cor->node);
					break;
				}
				f = coroutines_next(f);
			}
		}

		polling = !!list_first(&scheduler->coroutines_ready_list.list);

		scheduler->wait_for_event(scheduler, polling, scheduler->wait_for_event_udata);

		cor = coroutines_list_first(&scheduler->waiting_coroutines_list);
		for (;cor;cor = cor_next)
		{
			cor_next =  coroutines_next(cor);

			if (!cor->context->is_now_ready)
				continue;

			if (!cor->context->is_now_ready(scheduler, cor))
				continue;

			node_remove(&cor->node);
			list_insert_tail(&scheduler->coroutines_ready_list.list, &cor->node);
		}
	}

}

/*---------------------------------------------------------------------------*/



/*****************************************************************************/

#ifdef TEST

#include <assert.h>
#include <stdio.h>

struct count_context
{
	struct coroutine_basic_context basic_context;

	int count;
};

static coroutine_return_t count(struct coroutine_basic_context *arg)
{
	struct count_context *c = (struct count_context *)arg;

	COROUTINE_BEGIN(c);

	for (c->count=0; c->count < 10; c->count++)
	{
		printf("count = %d\n", c->count);
		COROUTINE_YIELD(c);
	}

	COROUTINE_END(c);
}

struct example_context
{
	struct coroutine_basic_context basic_context;

	int fd;
	struct sockaddr_in local_addr;
	socklen_t local_addrlen;
	struct sockaddr_in remote_addr;
	socklen_t remote_addrlen;

	struct count_context count_context;
};

static coroutine_return_t coroutines_test(struct coroutine_basic_context *arg)
{
	struct example_context *c = (struct example_context *)arg;

	COROUTINE_BEGIN(c);

	c->local_addr.sin_family = AF_INET;
	c->local_addr.sin_port = htons(1234);
	c->local_addr.sin_addr.s_addr = INADDR_ANY;
	c->local_addrlen = sizeof(c->local_addr);
	c->remote_addrlen = sizeof(c->remote_addr);

	c->fd = socket(AF_INET, SOCK_STREAM, 0);
	assert(c->fd != 0);

	int rc = bind(c->fd, (struct sockaddr*)&c->local_addr, c->local_addrlen);
	assert(rc == 0);

	rc = listen(c->fd, 50);
	assert(rc == 0);

	rc = fcntl(c->fd, F_SETFL, O_NONBLOCK);
	assert(rc == 0);

	COROUTINE_AWAIT_SOCKET(c, c->fd, 0);

	printf("back again\n");
	rc = accept(c->fd, (struct sockaddr*)&c->remote_addr, &c->remote_addrlen);
	printf("%d\n",rc);

	if (rc >= 0)
	{
		close(rc);
	}

	/* Now invoking the other coroutine */
	c->count_context.basic_context.socket_fd = -1;
	c->count_context.basic_context.scheduler = c->basic_context.scheduler;

	coroutine_t other = coroutine_add(c->basic_context.scheduler, count, &c->count_context.basic_context);
	assert(other != NULL);

	COROUTINE_AWAIT_OTHER(c, other);

	printf("now ending main coroutine\n");
	COROUTINE_END(c);
}


int main(int argc, char **argv)
{
	coroutine_scheduler_t sched;
	coroutine_t example;

	struct example_context example_context = {0};

	if (!(sched = coroutine_scheduler_new()))
	{
		fprintf(stderr, "Couldn't allocate scheduler!\n");
		return 1;
	}


	example_context.basic_context.socket_fd = -1;
	/* TODO: It is not required to set the scheduler as this is part of coroutine_add() */
	example_context.basic_context.scheduler = sched;

	example = coroutine_add(sched, coroutines_test, &example_context.basic_context);
	assert(example != 0);

	coroutine_schedule(sched);

	if (example_context.fd > 0)
		close(example_context.fd);
	return 0;
}


#endif
