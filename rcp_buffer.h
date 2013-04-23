
struct rcp_buffer{
	//char buffer[RCP_PROTOCOL_JSON_BUFFER_SIZE];
	//allocated mem info
	void *front;
	void *back;
	//active data info
	void *begin;
	void *end;
};

void rcp_buffer_init(struct rcp_buffer *buffer, size_t len);

void rcp_buffer_deinit(struct rcp_buffer *buffer);

void rcp_buffer_verify(struct rcp_buffer *buffer);

void *rcp_buffer_data(struct rcp_buffer *buffer);

void *rcp_buffer_data_end(struct rcp_buffer *buffer);

size_t rcp_buffer_data_size(struct rcp_buffer *buffer);

void *rcp_buffer_space(struct rcp_buffer *buffer);

size_t rcp_buffer_space_size(struct rcp_buffer *buffer);

void rcp_buffer_supplied(struct rcp_buffer *buffer, size_t len);
void rcp_buffer_supply(struct rcp_buffer *buffer, 
		const void* data, size_t len);

void* rcp_buffer_consume(struct rcp_buffer *buffer, size_t len);
void rcp_buffer_consumed(struct rcp_buffer *buffer, size_t len);
void rcp_buffer_consumed_at(struct rcp_buffer *buffer, void* ptr);
void rcp_buffer_consumed_all(struct rcp_buffer *buffer);

void rcp_buffer_cleanup(struct rcp_buffer *buffer);

