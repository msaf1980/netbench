//
// client.cpp
// ~~~~~~~~~~
//
// Copyright (c) 2003-2008 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "pch.h" // precompiled header, add other headers after

#include <algorithm>
#include <iostream>
#include <list>
#include <string>
#include "asio.hpp"
#include "handler_allocator.hpp"
#include <boost/bind.hpp>
#include <boost/mem_fn.hpp>

#ifndef WIN32
#    include <sys/resource.h>
#endif

#include "server.hpp"

class stats
{
public:
    stats(int timeout) : mutex_(), total_bytes_written_(0), total_bytes_read_(0), timeout_(timeout) { }

    void add(size_t bytes_written, size_t bytes_read)
    {
        asio::detail::mutex::scoped_lock lock(mutex_);
        total_bytes_written_ += bytes_written;
        total_bytes_read_ += bytes_read;
    }

    void print()
    {
        asio::detail::mutex::scoped_lock lock(mutex_);
        std::cout << total_bytes_written_ << " total bytes written\n";
        std::cout << total_bytes_read_ << " total bytes read\n";
        std::cout << static_cast<double>(total_bytes_read_) / (timeout_ * 1024 * 1024) << " MiB/s throughput\n";
    }

private:
    asio::detail::mutex mutex_;
    size_t total_bytes_written_;
    size_t total_bytes_read_;
    int timeout_;
};

class session
{
public:
    session(asio::io_service & ios, size_t block_size, stats & s)
        : strand_(ios)
        , socket_(ios)
        , block_size_(block_size)
        , read_data_(new char[block_size])
        , read_data_length_(0)
        , write_data_(new char[block_size])
        , unwritten_count_(0)
        , bytes_written_(0)
        , bytes_read_(0)
        , stats_(s)
    {
        for (size_t i = 0; i < block_size_; ++i)
            write_data_[i] = static_cast<char>(i % 128);
    }

    ~session()
    {
        stats_.add(bytes_written_, bytes_read_);

        delete[] read_data_;
        delete[] write_data_;
    }

    void start(asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
        socket_.async_connect(
            endpoint, strand_.wrap(boost::bind(&session::handle_connect, this, asio::placeholders::error, ++endpoint_iterator)));
    }

    void stop() { strand_.post(boost::bind(&session::close_socket, this)); }

private:
    void handle_connect(const asio::error_code & err, asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            asio::error_code set_option_err;
            asio::ip::tcp::no_delay no_delay(true);
            socket_.set_option(no_delay, set_option_err);
            if (!set_option_err)
            {
                ++unwritten_count_;
                async_write(
                    socket_,
                    asio::buffer(write_data_, block_size_),
                    strand_.wrap(make_custom_alloc_handler(
                        write_allocator_,
                        boost::bind(&session::handle_write, this, asio::placeholders::error, asio::placeholders::bytes_transferred))));
                socket_.async_read_some(
                    asio::buffer(read_data_, block_size_),
                    strand_.wrap(make_custom_alloc_handler(
                        read_allocator_,
                        boost::bind(&session::handle_read, this, asio::placeholders::error, asio::placeholders::bytes_transferred))));
            }
        }
        else if (endpoint_iterator != asio::ip::tcp::resolver::iterator())
        {
            socket_.close();
            asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(
                endpoint, strand_.wrap(boost::bind(&session::handle_connect, this, asio::placeholders::error, ++endpoint_iterator)));
        }
    }

    void handle_read(const asio::error_code & err, size_t length)
    {
        if (!err)
        {
            bytes_read_ += length;

            read_data_length_ = length;
            ++unwritten_count_;
            if (unwritten_count_ == 1)
            {
                std::swap(read_data_, write_data_);
                async_write(
                    socket_,
                    asio::buffer(write_data_, read_data_length_),
                    strand_.wrap(make_custom_alloc_handler(
                        write_allocator_,
                        boost::bind(&session::handle_write, this, asio::placeholders::error, asio::placeholders::bytes_transferred))));
                socket_.async_read_some(
                    asio::buffer(read_data_, block_size_),
                    strand_.wrap(make_custom_alloc_handler(
                        read_allocator_,
                        boost::bind(&session::handle_read, this, asio::placeholders::error, asio::placeholders::bytes_transferred))));
            }
        }
    }

    void handle_write(const asio::error_code & err, size_t length)
    {
        if (!err && length > 0)
        {
            bytes_written_ += length;

            --unwritten_count_;
            if (unwritten_count_ == 1)
            {
                std::swap(read_data_, write_data_);
                async_write(
                    socket_,
                    asio::buffer(write_data_, read_data_length_),
                    strand_.wrap(make_custom_alloc_handler(
                        write_allocator_,
                        boost::bind(&session::handle_write, this, asio::placeholders::error, asio::placeholders::bytes_transferred))));
                socket_.async_read_some(
                    asio::buffer(read_data_, block_size_),
                    strand_.wrap(make_custom_alloc_handler(
                        read_allocator_,
                        boost::bind(&session::handle_read, this, asio::placeholders::error, asio::placeholders::bytes_transferred))));
            }
        }
    }

    void close_socket() { socket_.close(); }

private:
    asio::io_service::strand strand_;
    asio::ip::tcp::socket socket_;
    size_t block_size_;
    char * read_data_;
    size_t read_data_length_;
    char * write_data_;
    int unwritten_count_;
    size_t bytes_written_;
    size_t bytes_read_;
    stats & stats_;
    handler_allocator read_allocator_;
    handler_allocator write_allocator_;
};

class client
{
public:
    client(
        asio::io_service & ios,
        const asio::ip::tcp::resolver::iterator endpoint_iterator,
        size_t block_size,
        size_t session_count,
        int timeout)
        : io_service_(ios), stop_timer_(ios), sessions_(), stats_(timeout)
    {
        stop_timer_.expires_from_now(boost::posix_time::seconds(timeout));
        stop_timer_.async_wait(boost::bind(&client::handle_timeout, this));

        for (size_t i = 0; i < session_count; ++i)
        {
            session * new_session = new session(io_service_, block_size, stats_);
            new_session->start(endpoint_iterator);
            sessions_.push_back(new_session);
        }
    }

    ~client()
    {
        while (!sessions_.empty())
        {
            delete sessions_.front();
            sessions_.pop_front();
        }

        stats_.print();
    }

    void handle_timeout() { std::for_each(sessions_.begin(), sessions_.end(), boost::mem_fn(&session::stop)); }

private:
    asio::io_service & io_service_;
    asio::deadline_timer stop_timer_;
    std::list<session *> sessions_;
    stats stats_;
};

void server_thread(asio::io_service & ios, unsigned short port, size_t block_size, size_t thread_count);

int main(int argc, char * argv[])
{
    try
    {
        int c;
        std::string host;
        std::string port_str = "9876";
        int port;
        int block_size = 16384;
        int session_count = 0;
        int seconds = 60;
        int thread_count = 1;
        int client_start = 0;
        int server_start = 1;

#ifndef WIN32
        struct rlimit rl;
#endif
        while ((c = getopt(argc, argv, ":a:b:n:d:t:h")) != -1)
        {
            switch (c)
            {
                case 'a':
                    host = optarg;
                    server_start = 0;
                    break;
                case 'p':
                    port_str = optarg;
                    break;
                case 'b':
                    block_size = atoi(optarg);
                    break;
                case 'n':
                    session_count = atoi(optarg);
                    break;
                case 't':
                    thread_count = atoi(optarg);
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

        port = std::atoi(port_str.c_str());
        if (port <= 0 || port > USHRT_MAX)
        {
            fprintf(stderr, "Illegal port");
            exit(1);
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
            if (thread_count <= 0)
            {
                fprintf(stderr, "Invalid threads_count\n");
                return 1;
            }
        }
        else if (server_start == 0)
        {
            fprintf(stderr, "Invalid options, nothing started\n");
            return 1;
        }

        signal(SIGPIPE, SIG_IGN);

#ifndef WIN32
        rl.rlim_cur = rl.rlim_max = session_count * 2 + 50;
        if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
        {
            perror("setrlimit");
            exit(1);
        }
#endif

        service_runner * server = nullptr;
        if (server_start)
        {
            server = new service_runner(thread_count);
            if (client_start)
            {
                thread_server = new asio::thread(
                    server_start,
                    std::ref(ios_server),
                    static_cast<unsigned short>(port),
                    static_cast<size_t>(block_size),
                    static_cast<size_t>(thread_count));
                sleep(1);
            }
            else
            {
                server_thread(
                    std::ref(ios_server),
                    static_cast<unsigned short>(port),
                    static_cast<size_t>(block_size),
                    static_cast<size_t>(thread_count));
                exit(0);
            }
        }

        if (client_start)
        {
            asio::io_service ios;

            asio::ip::tcp::resolver r(ios);
            asio::ip::tcp::resolver::iterator iter = r.resolve(asio::ip::tcp::resolver::query(host, port_str));

            client cl(ios, iter, block_size, session_count, seconds);

            std::list<asio::thread *> threads;
            while (--thread_count > 0)
            {
                asio::thread * new_thread = new asio::thread(boost::bind(&asio::io_service::run, &ios));
                threads.push_back(new_thread);
            }

            ios.run();

            while (!threads.empty())
            {
                threads.front()->join();
                delete threads.front();
                threads.pop_front();
            }
        }

        if (server_start)
        {
            if (thread_server)
            {
                ios_server.stop();
                thread_server->join();
                delete thread_server;
            }
        }
    }
    catch (std::exception & e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
