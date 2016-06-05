all: server client

server: ftp_server.c ftp_server.h lib serverlib
	cc -g ftp_server.c lib serverlib -o server

client: ftp_client.c ftp_client.h lib
	cc -g ftp_client.c lib -o client

serverlib: ftp_server_lib.c ftp_server.h ftp_lib.h
	cc -g -c ftp_server_lib.c -o serverlib
lib: ftp_lib.c ftp_lib.h
	cc -g -c ftp_lib.c -o lib

clean:
	rm lib client server