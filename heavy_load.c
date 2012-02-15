#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

/* 150 threads running 900 sockets each => 135k connections and having each socket 
doing 10000 requests should be enough coming from one program instance */
#define CONNECTIONS 450
#define THREADS 150
#define REQUESTS 10000
#define DURATION 30 

/* TODO: 
- take into account round trip time for one request, serialization speed
- based on those check if usleep values for each socket and for each fork are too
low and increase them if necessary 
- increase max open files to open more sockets at once 
- since this is TCP 3-way handshake is required per socket, take into account when computing RTT 
- check for server response of HTTP 200 OK  */

/* threads[THREADS] is a counter for the number of http requests issued by various forks */
/* each thread would write to its own place, less concurrency */
long *threads = NULL;

int make_socket(char *host, char *port) {
	struct addrinfo hints, *servinfo, *p;
	int sock, r;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if((r=getaddrinfo(host, port, &hints, &servinfo))!=0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
		exit(0);
	}
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			continue;
		}
		if(connect(sock, p->ai_addr, p->ai_addrlen)==-1) {
			close(sock);
			continue;
		}
		break;
	}
	if(p == NULL) {
		if(servinfo)
			freeaddrinfo(servinfo);
		fprintf(stderr, "No connection could be made\n");
		exit(0);
	}
/* otherwise you got a memleak */
	freeaddrinfo(servinfo);
	return sock;
}

void httpload(char *host, char *port, int count) {
	/* initialize array & variables */ 
	int sockets[CONNECTIONS] = { 0 };
	int x, r, y;

    /* http header/s */
    char http_header[31] = "HEAD /test.txt HTTP/1.0\r\n\r\n \0";

	/* ignore SIGPIPE */
	signal(SIGPIPE, SIG_IGN);
	while(1) {
	    for(x = 0; x < CONNECTIONS; x++) {
	        if(sockets[x] == 0) {
                sockets[x] = make_socket(host, port);
                for (y = 0; y < REQUESTS; y++) {
                    r=send(sockets[x], http_header, 31, MSG_NOSIGNAL);
                    threads[count] += 1;
                }
                if(r == -1) {
                    close(sockets[x]);
                    sockets[x] = make_socket(host, port);
                    /* CHANGEME: should another for loop be run here ^^ */
                    /* even if the socket failed, the variable was incremented, need to decrement it if the socket failed */
                    threads[count] -= 10000;
                }
            }
        }
		usleep(10000);
	}
}

int main(int argc, char **argv) {

	/* Setting up shared memory for the 'requests' variable
	since it needs to be accessed by many forks. */
    int shr = shmget(IPC_PRIVATE, sizeof(long)*THREADS, 0600);
    threads = (long *) shmat(shr, NULL, 0);

    /* starttime of this program */
    struct timeval start, end;
    gettimeofday(&start, NULL);

	/* CHANGEME: to be or not to be +1 */
	pid_t offspring[THREADS+1] = { '\0' };
	int x;
        int count;

    /* create a new thread for each httpload() instance */
	for(x = 0; x < THREADS; x++) {
		pid_t pid = fork();
		if(0 == pid) { 							/* child process */
			offspring[x] = getpid();
			httpload(argv[1], argv[2], x);
			usleep(10000);						/* 10 ms delay between spawning processes */
		}
		if(pid < 0) { 							/* failed to fork */
			fprintf(stderr, "Can't fork\n");
			exit(1);
		}
    }

	/* print number of requests every second */
    /* generate load for the DURATION of the test */
    struct timeval now;
    for (count=0; count < DURATION ; count++) {
		int i;
	    long long requests = 0;
    for(i = 0; i < THREADS; i++) {
        gettimeofday(&now, NULL);
        printf("%ld\n", requests);
        printf("program starttime: %ld\n", start.tv_sec);
        printf("thread nowtime: %ld\n", now.tv_sec);

        requests += threads[i];
        printf("requests made total: %lld\naverage requests/second: %lld\n", requests, (long long)requests/(((now.tv_sec) - (start.tv_sec)) +1)); 
        sleep(1); 
		}
	}

    getc(stdin);

	/* kill child pids */
	int pids;

	for(pids = 0; pids < THREADS; pids++) {
		kill(offspring[pids],SIGKILL);
	}
	return 0;
}
