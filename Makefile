CFLAGS = -Wall -g
LDFLAGS = -L. -l:rpc.a -lm

CLIENT = rpc-client
SERVER = rpc-server

SRC = rpc.c linkedlists.c
OBJ = $(SRC:%.c=%.o)

RPC_SYSTEM_A=rpc.a

.PHONY: format all

all: $(CLIENT) $(SERVER) 
#test_client test_server

%.o: %.c
	cc $(CFLAGS) -c -o $@ $<

$(CLIENT): client.c $(RPC_SYSTEM_A)
	cc $(CFLAGS) -o $@ $< $(LDFLAGS)
$(SERVER): server.c $(RPC_SYSTEM_A)
	cc $(CFLAGS) -o $@ $< $(LDFLAGS)

#test_client:
#	$(CC) -o test_client client.a $(RPC_SYSTEM_A) $(LDFLAGS)
#test_server:
#	$(CC) -o test_server server.a $(RPC_SYSTEM_A) $(LDFLAGS)

$(RPC_SYSTEM_A): $(OBJ)
	ar rcs $(RPC_SYSTEM_A) $^

clean:
	rm -rf **/*.o *.o $(CLIENT) $(SERVER) $(RPC_SYSTEM_A) test_client test_server valgrind_server_out.txt valgrind_client_out.txt

format:
	clang-format -style=file -i *.c *.h
