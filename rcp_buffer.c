#include <stdlib.h>
#include <string.h>
#include "rcp_buffer.h"

void rcp_buffer_init(struct rcp_buffer *buffer, size_t len)
{
	buffer->front = malloc(len);
	buffer->back = buffer->front + len;
	buffer->begin = buffer->front;
	buffer->end = buffer->front;
}

void rcp_buffer_deinit(struct rcp_buffer *buffer)
{
	free(buffer->front);
}

void rcp_buffer_verify(struct rcp_buffer *buffer)
{
#ifdef RCP_SELF_TEST
	if (buffer->end > buffer->back){
		rcp_error("buffer over run!!!\n");
	}
	if (buffer->begin > buffer->end){
		rcp_error("undefined data used!!!\n");
	}
#endif
}

void *rcp_buffer_data(struct rcp_buffer *buffer)
{
	return buffer->begin;
}

void *rcp_buffer_data_end(struct rcp_buffer *buffer)
{
	return buffer->end;
}

size_t rcp_buffer_data_size(struct rcp_buffer *buffer)
{
	return buffer->end - buffer->begin;
}

void *rcp_buffer_space(struct rcp_buffer *buffer)
{
	return buffer->end;
}

size_t rcp_buffer_space_size(struct rcp_buffer *buffer)
{
	return  buffer->back - buffer->end;
}

void rcp_buffer_supplied(struct rcp_buffer *buffer, size_t len)
{
	buffer->end += len;	
	rcp_buffer_verify(buffer);
}

void rcp_buffer_supply(struct rcp_buffer *buffer, 
		const void* data, size_t len)
{
#ifdef RCP_SELF_TEST
	if (rcp_buffer_space_size(buffer)<len){
		rcp_error("buf not enough space");
		return;
	}
#endif 
	memcpy(buffer->end, data, len);
	rcp_buffer_supplied(buffer, len);
}

void *rcp_buffer_consume(struct rcp_buffer *buffer, size_t len)
{
	void *n = buffer->begin+len;
	void *o = buffer->begin;
	if (n > buffer->end)
		return NULL;
	buffer->begin = n;
	rcp_buffer_verify(buffer);
	return o;
}
void rcp_buffer_consumed(struct rcp_buffer *buffer, size_t len)
{
	buffer->begin += len;
	rcp_buffer_verify(buffer);
}

void rcp_buffer_consumed_at(struct rcp_buffer *buffer, void* ptr)
{
	buffer->begin = ptr;
	rcp_buffer_verify(buffer);
}

void rcp_buffer_consumed_all(struct rcp_buffer *buffer)
{
	buffer->begin = buffer->end;
	rcp_buffer_verify(buffer);
}

void rcp_buffer_cleanup(struct rcp_buffer *buffer)
{
	if (buffer->begin == buffer->end){
		//no remain data
		buffer->begin = buffer->front;
		buffer->end = buffer->front;
		return;
	}

	if (buffer->begin != buffer->front){
		size_t remain = buffer->end - buffer->begin;
		memmove(buffer->front, buffer->begin, remain);
		buffer->begin = buffer->front;
		buffer->end = buffer->front + remain;
	}
}

