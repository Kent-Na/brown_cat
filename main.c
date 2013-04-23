#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "rcp_buffer.h"

const uint16_t input_port = 8080;
const uint32_t buffer_size=4096;

#define max_connection_count  1024

#define CAT_IS_RECEIVING_HEADER 1
#define CAT_IS_RECEIVED_HEADER 2

struct cat_info;

struct event_info{
	struct cat_info* target;
	void (*action)(struct cat_info* cat, struct epoll_event* ev);
};

struct cat_info{
	//uint8_t input_buffer[buffer_size];
	//uint8_t output_buffer[buffer_size];
	struct rcp_buffer ex_to_in_buffer;
	struct rcp_buffer in_to_ex_buffer;

	struct event_info ex_event;	
	struct event_info in_event;	

	struct cat_info* next;
	int ex_fd;
	int in_fd;

	int state;

	uint8_t ex_fd_read_shutdowned;
	uint8_t in_fd_read_shutdowned;
};


struct output_info{
	const char* domain_name;
	uint16_t output_port;
};


//Host name and related internal port number.
//Last ently must be NULL host name and used as default.
struct output_info target_table[] = {
	{"www.tuna-cat.com", 8080},
	{"lab.tuna-cat.com", 8080},
	{"rcp.tuna-cat.com", 4001},
	{NULL, 80}//default
};

static struct cat_info info_buffer[max_connection_count];

struct cat_info* free_info;
//struct info_buffer* active_info;

///
//prototypes
void setup_new_connection(int epfd, int listen_fd);
void setup_in_fd(struct cat_info* info, int epfd);
void in_action(struct cat_info* info, struct epoll_event* ev);
void ex_action(struct cat_info* info, struct epoll_event* ev);

void reset_cat(struct cat_info* info){
	info->ex_fd = -1;
	info->in_fd = -1;
	info->state = 0;
	info->ex_fd_read_shutdowned = 0;
	info->in_fd_read_shutdowned = 0;
	info->in_event.target = info;
	info->in_event.action= in_action;
	info->ex_event.target = info;
	info->ex_event.action= ex_action;
	rcp_buffer_consumed_all(&info->ex_to_in_buffer);
	rcp_buffer_consumed_all(&info->in_to_ex_buffer);
}

int main(int argc, char **argv){

	//setup ep
	int epfd = epoll_create(1);

	//setup info buffer
	for (int i=0; i<max_connection_count; i++){
		struct cat_info *info = info_buffer+i;
		rcp_buffer_init(&info->ex_to_in_buffer, buffer_size);
		rcp_buffer_init(&info->in_to_ex_buffer, buffer_size);
		reset_cat(info);

		if (i != max_connection_count-1)
			info->next = info+1;
		else
			info->next = NULL;
	}
	free_info = info_buffer;

	//setup listner
	struct sockaddr_in s_addr;
	bzero(&s_addr, sizeof s_addr);
	s_addr.sin_family = AF_INET;
	s_addr.sin_addr.s_addr = INADDR_ANY;
	s_addr.sin_port = htons(input_port);
	int listen_fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);

	{
		int val = 1;
		int err = setsockopt(listen_fd, SOL_SOCKET, 
						SO_REUSEADDR, &val, sizeof val);
		if (err) return 0;
	}
	{
		int err = bind(listen_fd, (struct sockaddr*)&s_addr, 
						sizeof s_addr);
		if (err) return 0;
	}
	{
		int err = listen(listen_fd, 16);
		if (err) return 0;
	}

	{
		struct epoll_event ev;
		ev.events = EPOLLIN|EPOLLOUT|EPOLLPRI|
					EPOLLRDHUP|EPOLLERR|EPOLLHUP;
		ev.data.ptr = NULL;
		int err = epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
		if (err) return 0;
	}

	//main loop
	while (1){
		const int max_events_count = 16;
		struct epoll_event events[max_events_count];
		int nfds = epoll_wait(epfd, events, max_events_count, -1);
		if (nfds<0){
			//error
			if (errno == EINTR)
				continue;
			return 0;
		}
		for (int i = 0; i<nfds; i++){
			struct epoll_event *event = events+i;
			struct event_info *info = event->data.ptr;
			if (info == NULL)
				setup_new_connection(epfd, listen_fd);
			else
				info->action(info->target, event);
		}
	}
	return 0;
}

void setup_new_connection(int epfd, int listen_fd){
	struct sockaddr c_addr;
	socklen_t addr_len = sizeof c_addr;
	struct cat_info* info = free_info;
	if (info == NULL){
		//process_error here
		return;
	}
	free_info = info->next;
	int fd = accept4(listen_fd, &c_addr, &addr_len, SOCK_NONBLOCK);
	struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT|EPOLLPRI|
				EPOLLRDHUP|EPOLLERR|EPOLLHUP|EPOLLET;
	ev.data.ptr = &info->ex_event;
	int err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	if (err) return;

	info->ex_fd = fd;
	setup_in_fd(info, epfd);
}


void setup_in_fd(struct cat_info* info, int epfd){
	
	struct output_info* o_info = target_table;
	while (o_info->domain_name){
		//compair_name_here
		o_info ++;
	}

	char* ip = "183.181.20.215";

	struct sockaddr_in s_addr;
	bzero(&s_addr, sizeof s_addr);
	s_addr.sin_family = AF_INET;
	s_addr.sin_addr.s_addr = inet_addr(ip);
	s_addr.sin_port = htons(o_info->output_port);

	int fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	int err;
	err = connect(fd, (struct sockaddr*) &s_addr, sizeof s_addr);
	if (err){
		if (errno != EINPROGRESS)
			return;
	}

	info->in_fd = fd;

	struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT|EPOLLPRI|
				EPOLLRDHUP|EPOLLERR|EPOLLHUP|EPOLLET;
	ev.data.ptr = &info->in_event;
	err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	if (err) return;
}

void kill_cat(struct cat_info* info){

	close(info->ex_fd);
	close(info->in_fd);

	rcp_buffer_consumed_all(&info->ex_to_in_buffer);
	rcp_buffer_consumed_all(&info->in_to_ex_buffer);

	struct cat_info* old_free = free_info;
	free_info = info;
	info->next = old_free;
}

ssize_t receive_from(int fd, struct rcp_buffer* buffer){
	ssize_t r_len = recv(
			fd,
			rcp_buffer_space(buffer),
			rcp_buffer_space_size(buffer),
			MSG_NOSIGNAL);

	if (r_len > 0)
		rcp_buffer_supplied(buffer, r_len);

	return r_len;
}

ssize_t send_to(int fd, struct rcp_buffer* buffer){
	ssize_t s_len = send(
			fd,
			rcp_buffer_data(buffer),
			rcp_buffer_data_size(buffer),
			MSG_NOSIGNAL);

	if (s_len > 0)
		rcp_buffer_consumed(buffer, s_len);

	return s_len;
}

void transfer_a_to_b(int fd_a, int fd_b, struct rcp_buffer* buffer){
	while(1){
		if (rcp_buffer_space_size(buffer) == 0)
			rcp_buffer_cleanup(buffer);
		ssize_t r_len;
		if (rcp_buffer_space_size(buffer) != 0)
			r_len = receive_from(fd_a, buffer);
		int r_errno = errno;

		//if nodata in buffer,end it.
		if (rcp_buffer_data_size(buffer) == 0)
			return;

		size_t s_len = send_to(fd_b, buffer);
		int s_errno = errno;

		if (s_len == -1){
			return;
		}
	}
}

void transfer_ex_to_in(struct cat_info* info){
	transfer_a_to_b(info->ex_fd, info->in_fd, &info->ex_to_in_buffer);
}

void transfer_in_to_ex(struct cat_info* info){
	transfer_a_to_b(info->in_fd, info->ex_fd, &info->in_to_ex_buffer);
}

void ex_action(struct cat_info* info, struct epoll_event* ev){
	printf("----ex\n");
	if (ev->events & EPOLLIN){
		printf("ex_read\n");
		transfer_ex_to_in(info);
	}
	if (ev->events & EPOLLOUT){
		printf("ex_write\n");
		transfer_in_to_ex(info);
	}

	if (ev->events & EPOLLRDHUP){
		printf("ex_rd_closed\n");
		if (rcp_buffer_data_size(&info->ex_to_in_buffer) == 0){
			shutdown(info->in_fd, SHUT_WR);
		}
	}

	if (ev->events & EPOLLHUP){
		printf("ex_all_closed\n");
		//printf("in_all_closed\n");
		//kill_cat(info);
		close(info->ex_fd);
	}
}

void in_action(struct cat_info* info, struct epoll_event* ev){
	printf("----in\n");
	if (ev->events & EPOLLIN){
		printf("in_read\n");
		transfer_in_to_ex(info);
	}
	if (ev->events & EPOLLOUT){
		printf("in_write\n");
		transfer_ex_to_in(info);
	}

	if (ev->events & EPOLLRDHUP){
		printf("in_rd_closed\n");
		if (rcp_buffer_data_size(&info->in_to_ex_buffer) == 0){
			shutdown(info->ex_fd, SHUT_WR);
		}
	}

	if (ev->events & EPOLLHUP){
		printf("in_all_closed\n");
		//printf("in_all_closed\n");
		//kill_cat(info);
		close(info->in_fd);
	}
}
