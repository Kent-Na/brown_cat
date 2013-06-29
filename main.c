#include <string.h>
#include <stdio.h>
#include <strings.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "rcp_buffer.h"

static const uint16_t input_port = 80;
static const uint32_t buffer_size=4096;// and maximum http header size

#define max_connection_count  1024

struct output_info{
	const char* domain_name;
	uint16_t output_port;
};

static const char* log_file_path = "/var/log/brown_cat.log";
//process owner name
static const char* user_name = "czel";
static const char* target_field_name = "Host";

//Host name and related internal port number.
//Last ently must be NULL host name and used as default.
static struct output_info target_table[] = {
	{"rcp.tuna-cat.com", 4001},
	{"rcp.tuna-cat.com:80", 4001},
	{NULL, 8080}//default
};

#define CAT_IS_RECEIVING_HEADER 0
#define CAT_IS_RECEIVED_HEADER 1
#define CAT_WANT_DIE 2

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

	//precessing http header line, does not contain end
	uint8_t* line_begin;
	//uint8_t* line_end;

	struct event_info ex_event;	
	struct event_info in_event;	

	struct cat_info* next;
	int ex_fd;
	int in_fd;

	int state;

	uint8_t in_w_ex_r_shutdowned;
	uint8_t ex_w_in_r_shutdowned;
	int id;
};

static struct cat_info info_buffer[max_connection_count];

struct cat_info* free_info;

static int cat_id = 0;

static int epfd = -1;
//static int logfd = -1;
FILE* log_file = NULL;
/*
void print_log(const char* str){
	const char nl = '\n';
	write(logfd, str, strlen(str));
	write(logfd, &nl, 1);
	printf(str);
	printf("\n");
}
*/

void print_log(const char* format, ...){
	va_list args;
	va_start(args, format);
	vfprintf(log_file, format, args);
	va_end(args);

	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	fflush(log_file);
}

///
//prototypes
void setup_new_connection(int epfd);
void setup_in_fd(struct cat_info* info, struct output_info* o_info);
void in_action(struct cat_info* info, struct epoll_event* ev);
void ex_action(struct cat_info* info, struct epoll_event* ev);

void reset_cat(struct cat_info* info){
	info->ex_fd = -1;
	info->in_fd = -1;
	info->state = 0;
	info->in_w_ex_r_shutdowned = 0;
	info->ex_w_in_r_shutdowned = 0;
	info->in_event.target = info;
	info->in_event.action= in_action;
	info->ex_event.target = info;
	info->ex_event.action= ex_action;
	rcp_buffer_consumed_all(&info->ex_to_in_buffer);
	rcp_buffer_consumed_all(&info->in_to_ex_buffer);
	rcp_buffer_cleanup(&info->ex_to_in_buffer);
	rcp_buffer_cleanup(&info->in_to_ex_buffer);
	info->state = CAT_IS_RECEIVING_HEADER;
	info->line_begin = rcp_buffer_data(&info->ex_to_in_buffer);
	info->id = cat_id++;
}

void free_cat(struct cat_info* info){
	struct cat_info* old_free = free_info;
	free_info = info;
	info->next = old_free;
	//printf("i'm dead \n");
	print_log("(%i)cat gone\n", info->id);
}


int main(int argc, char **argv){

	//setup ep
	epfd = epoll_create(1);

	//setup logfile
	//logfd = open(
		//log_file_path, 
		//O_WRONLY | O_APPEND | O_CREAT | O_SYNC,
		//S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	log_file = fopen(log_file_path, "a");

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
		struct passwd* pw = getpwnam(user_name);
		setuid(pw->pw_uid);
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

	print_log("start server\n");

	//main loop
	while (1){
		const int max_events_count = 32;
		struct epoll_event events[max_events_count];
		int nfds = epoll_wait(epfd, events, max_events_count, -1);
		if (nfds<0){
			//error
			if (errno == EINTR)
				continue; //It makes debuger to attach this process.
			print_log("epoll fail\n");
			return 0;
		}
		for (int i = 0; i<nfds; i++){
			struct epoll_event *event = events+i;
			struct event_info *info = event->data.ptr;
			if (info == NULL)
				setup_new_connection(listen_fd);
			else
				info->action(info->target, event);
		}
	}
	return 0;
}

void setup_new_connection(int listen_fd){
	//printf("----yay, new cat\n");
	struct sockaddr c_addr;
	socklen_t addr_len = sizeof c_addr;
	struct cat_info* info = free_info;
	if (info == NULL){
		//process_error here
		print_log("No free cat.\n");
		return;
	}
	free_info = info->next;
	reset_cat(info);
	print_log("(%i)new cat\n", info->id);
	int fd = accept4(listen_fd, &c_addr, &addr_len, SOCK_NONBLOCK);
	struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT|EPOLLPRI|
				EPOLLRDHUP|EPOLLERR|EPOLLHUP|EPOLLET;
	ev.data.ptr = &info->ex_event;
	int err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

	if (err){
		print_log("fail epoll ctl\n");
		return;
	}

	info->ex_fd = fd;
}


void setup_in_fd(struct cat_info* info, struct output_info *o_info){

	char* ip = "127.0.0.1";

	struct sockaddr_in s_addr;
	bzero(&s_addr, sizeof s_addr);
	s_addr.sin_family = AF_INET;
	s_addr.sin_addr.s_addr = inet_addr(ip);
	s_addr.sin_port = htons(o_info->output_port);

	int fd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
	int err;
	err = connect(fd, (struct sockaddr*) &s_addr, sizeof s_addr);
	if (err){
		if (errno != EINPROGRESS){
			print_log("fail connect\n");
			//printf("connect fail\n");
			return;
		}
	}

	info->in_fd = fd;
	info->state = CAT_IS_RECEIVED_HEADER;

	struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT|EPOLLPRI|
				EPOLLRDHUP|EPOLLERR|EPOLLHUP|EPOLLET;
	ev.data.ptr = &info->in_event;
	err = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	if (err){
		print_log("fail epoll ctl\n");
		return;
	}


	//printf("connect to in\n");
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
		if (r_len == -1);
		//int r_errno = errno;

		//if nodata in buffer,end it.
		if (rcp_buffer_data_size(buffer) == 0)
			return;

		size_t s_len = send_to(fd_b, buffer);
		//int s_errno = errno;

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

void process_field_value(
	struct cat_info* info, uint8_t *begin, uint8_t *end){
	//*end must be '\n'
	if (end - begin < 2) return;
	if (end[-1] != '\n') return;
	if (end[-2] != '\r') return;

	uint8_t *itr = begin;
	while (itr != end && (*itr == '\t' || *itr == ' ')) itr ++;

	begin = itr;

	struct output_info* o_info = target_table;
	while (o_info->domain_name){
		uint8_t *tgt = (uint8_t*) o_info->domain_name;
		uint8_t is_match= 0;
		
		for (itr = begin; itr != end; itr++, tgt++){
			if (*tgt == '\0'){
				is_match = 1;
				break;
			}
			if (*itr != *tgt) break;
		}

		if (is_match){
			while (itr != end && (*itr == '\t' || *itr == ' ')) itr ++;
			if (*itr == '\r') break;// OK
		}

		o_info ++;
	}

	setup_in_fd(info, o_info);
}

void process_line(struct cat_info* info, uint8_t *begin, uint8_t *end){
	//*end must be '\r\n'
	if (end - begin < 2) return;
	if (end[-1] != '\n') return;
	if (end[-2] != '\r') return;

	if (end-begin == 2){
		//find default target
		struct output_info* o_info = target_table;
		while (o_info->domain_name) o_info ++;
		setup_in_fd(info, o_info);
		return;
	}

	uint8_t *tgt = (uint8_t*) target_field_name;
	
	for (uint8_t *itr = begin; itr != end; itr++, tgt++){
		if (*tgt == '\0' && *itr == ':'){
			process_field_value(info, itr+1, end);
			return;
		}
		if (*itr != *tgt) return;
	}
}

void precess_header(struct cat_info* info){
	struct rcp_buffer* buffer = &info->ex_to_in_buffer;
	ssize_t r_len;
	if (rcp_buffer_space_size(buffer) == 0){
		print_log("(%i)header too long\n",info -> id);
	}
	r_len = receive_from(info->ex_fd, buffer);

	uint8_t *data_end = 
		rcp_buffer_data(buffer)+rcp_buffer_data_size(buffer);

	if (r_len <= 0)
		return;
	
	for (uint8_t *itr = info->line_begin +1; itr < data_end; itr++){
		if (itr[0] == '\n' && itr[-1] == '\r'){
			//ignore first line
			if (rcp_buffer_data(buffer) != info->line_begin){
				process_line(info, info->line_begin, itr+1);
				if (info->state == CAT_IS_RECEIVED_HEADER)
					return;
			}
			info->line_begin = itr+1;
		}
	}
}

void test_and_kill_cat(struct cat_info* info){
	if (info->ex_w_in_r_shutdowned && info->in_w_ex_r_shutdowned){
		if (info->in_fd != -1){
			close(info->in_fd);
			info->in_fd = -1;
		}
		if (info->ex_fd != -1){
			close(info->ex_fd);
			info->ex_fd = -1;
		}
	}

	if (info->in_fd  == -1 && info->ex_fd == -1){
		free_cat(info);
	}
}

void ex_action(struct cat_info* info, struct epoll_event* ev){
	//printf("----ex%i,%i\n",info->ex_fd, info->in_fd);
	if (ev->events & EPOLLIN){
		//printf("ex_read\n");
		if (info->state == CAT_IS_RECEIVING_HEADER)
			precess_header(info);
		if (info->state == CAT_IS_RECEIVED_HEADER)
			transfer_ex_to_in(info);
	}
	if (ev->events & EPOLLOUT){
		//printf("ex_write\n");
		transfer_in_to_ex(info);
	}

	if (ev->events & EPOLLRDHUP){
		//printf("ex_rd_closed\n");
		if (rcp_buffer_data_size(&info->ex_to_in_buffer) == 0){
			shutdown(info->in_fd, SHUT_WR);
			info->in_w_ex_r_shutdowned = 1;
		}
		if (info->in_fd == -1){
			close(info->ex_fd);
			info->ex_fd = -1;
		}
	}

	if (ev->events & EPOLLHUP){
		//printf("ex_all_closed\n");
		close(info->ex_fd);
		info->ex_fd = -1;
	}

	if (ev->events & EPOLLERR){
		print_log("(%i)sock err\n", info->id);
		//print_log("sock err");
		//printf("ex_err\n");
	}

	test_and_kill_cat(info);
}

void in_action(struct cat_info* info, struct epoll_event* ev){
	//printf("----in%i,%i\n",info->ex_fd, info->in_fd);
	if (ev->events & EPOLLIN){
		//printf("in_read\n");
		transfer_in_to_ex(info);
	}
	if (ev->events & EPOLLOUT){
		//printf("in_write\n");
		if (info->state == CAT_IS_RECEIVING_HEADER)
			precess_header(info);
		if (info->state == CAT_IS_RECEIVED_HEADER)
			transfer_ex_to_in(info);
	}

	if (ev->events & EPOLLRDHUP){
		//printf("in_rd_closed\n");
		if (rcp_buffer_data_size(&info->in_to_ex_buffer) == 0){
			shutdown(info->ex_fd, SHUT_WR);
			info->ex_w_in_r_shutdowned = 1;
		}
	}

	if (ev->events & EPOLLHUP){
		//printf("in_all_closed\n");
		close(info->in_fd);
		info->in_fd = -1;
	}

	if (ev->events & EPOLLERR){
		print_log("(%i)sock err\n", info->id);
		//printf("in_err\n");
	}

	test_and_kill_cat(info);
}
