/*
 * Ping-pong benchmark (client)
 */
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int64_t total_bytes_read = 0;
int64_t total_messages_read = 0;

int64_t total_errors = 0;

static void set_tcp_no_delay(evutil_socket_t fd)
{
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

static void timeoutcb(evutil_socket_t fd, short what, void * arg)
{
    struct event_base * base = arg;
    fprintf(stderr, "client shutdown\n");

    event_base_loopexit(base, NULL);
}

static void readcb(struct bufferevent * bev, void * ctx)
{
    /* This callback is invoked when there is data to read on bev. */
    struct evbuffer * input = bufferevent_get_input(bev);
    struct evbuffer * output = bufferevent_get_output(bev);

    ++total_messages_read;
    total_bytes_read += evbuffer_get_length(input);

    /* Copy all the data from the input buffer to the output buffer. */
    evbuffer_add_buffer(output, input);
}

static void eventcb(struct bufferevent * bev, short events, void * ptr)
{
    if (events & BEV_EVENT_CONNECTED)
    {
        evutil_socket_t fd = bufferevent_getfd(bev);
        set_tcp_no_delay(fd);
    }
    else if (events & BEV_EVENT_ERROR)
    {
        total_errors++;
        fprintf(stderr, "NOT Connected\n");
    }
}

extern struct event_base * server_base;

void * server_thread(int * port);

int main(int argc, char ** argv)
{
#ifndef WIN32
    struct rlimit rl;
#endif
    struct event_base * base;
    struct bufferevent ** bevs;
    struct sockaddr_in sin;
    struct event * evtimeout;
    struct timeval timeout;
    int i, c;

    char * host = "127.0.0.1";
    int port = 9876;
    int block_size = 16384;
    int session_count = 0;
    int seconds = 60;
    int client_start = 0;
    int server_start = 1;
    pthread_t server_tid = 0;
    pthread_attr_t attr;

    while ((c = getopt(argc, argv, ":a:b:n:d:h")) != -1)
    {
        switch (c)
        {
            case 'a':
                host = optarg;
                server_start = 0;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'b':
                block_size = atoi(optarg);
                break;
            case 'n':
                session_count = atoi(optarg);
                break;
            case 'd':
                seconds = atoi(optarg);
                break;
            case 'h': {
                fprintf(stderr, "Usage: %s -p <port> -b <blocksize> ", argv[0]);
                fprintf(stderr, "-n <sessions> -d <time>\n");
                fprintf(stderr, "  [-a server] (by default, start internal server)\n");
                exit(1);
            }
            default:
                fprintf(stderr, "Illegal argument \"%c\"\n", c);
                exit(1);
        }
    }

    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Invalid port\n");
        return 1;
    }
    if (session_count > 0)
    {
        client_start = 1;

        if (block_size <= 0)
        {
            fprintf(stderr, "Invalid block_size\n");
            return 1;
        }
        if (seconds <= 0)
        {
            fprintf(stderr, "Invalid durations\n");
            return 1;
        }
    }
    else if (server_start == 0)
    {
        fprintf(stderr, "Invalid options, nothing started\n");
        return 1;
    }

    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    signal(SIGPIPE, SIG_IGN);

#ifndef WIN32
    rl.rlim_cur = rl.rlim_max = session_count * 2 + 50;
    if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
    {
        perror("setrlimit");
        exit(1);
    }
#endif

    if (server_start)
    {
        if (client_start)
        {
            int perr;
            pthread_attr_init(&attr);
            perr = pthread_create(&server_tid, &attr, &server_thread, &port);
            if (perr != 0)
            {
                perror(strerror(perr));
                return 1;
            }
            sleep(1);
        }
        else
        {
            if (server_thread(&port) != NULL)
            {
                return 1;
            }
        }
    }

    if (client_start)
    {
        base = event_base_new();
        if (!base)
        {
            puts("Couldn't open event base");
            return 1;
        }

        char * message = malloc(block_size);
        for (i = 0; i < block_size; ++i)
        {
            message[i] = i % 128;
        }

        evtimeout = evtimer_new(base, timeoutcb, base);
        evtimer_add(evtimeout, &timeout);

        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr(host);
        sin.sin_port = htons(port);

        bevs = malloc(session_count * sizeof(struct bufferevent *));
        for (i = 0; i < session_count; ++i)
        {
            struct bufferevent * bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);

            bufferevent_setcb(bev, readcb, NULL, eventcb, NULL);
            bufferevent_enable(bev, EV_READ | EV_WRITE);
            evbuffer_add(bufferevent_get_output(bev), message, block_size);

            if (bufferevent_socket_connect(bev, (struct sockaddr *)&sin, sizeof(sin)) < 0)
            {
                /* Error starting connection */
                bufferevent_free(bev);
                puts("error connect");
                return -1;
            }
            bevs[i] = bev;
        }

        event_base_dispatch(base);

        for (i = 0; i < session_count; ++i)
        {
            bufferevent_free(bevs[i]);
        }
        free(bevs);
        event_free(evtimeout);
        event_base_free(base);
        free(message);

        printf(
            "%20s %8s %6s %18s %18s %10s %12s %18s %18s\n",
            "Write buffer (bytes)",
            "Clients",
            "Errors",
            "Avg messages size",
            "Throughtput: MiB/s",
            "Msg/s",
            "us/msg",
            "Total read: bytes",
            "messages");
        printf(
            "%20d %8d %6zd %18.3f %18.3f %10ld %12.3f %18zd %18zd\n",
            block_size,
            session_count,
            total_errors,
            (double)total_bytes_read / total_messages_read,
            (double)total_bytes_read / (timeout.tv_sec * 1024 * 1024),
            total_messages_read / seconds,
            (double)100000 * seconds / total_messages_read,
            total_bytes_read,
            total_messages_read);

        if (server_start)
        {
            kill(getpid(), SIGINT);
            pthread_join(server_tid, NULL);
        }
    }

    return 0;
}
