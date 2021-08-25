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
    stats(size_t block_size, int timeout)
        : mutex_()
        , total_bytes_written_(0)
        , total_bytes_read_(0)
        , total_reads_(0)
        , total_writes_(0)
        , block_size_(block_size)
        , session_count_(0)
        , total_errors_(0)
        , timeout_(timeout)
    {
    }

    void add(size_t bytes_written, size_t bytes_read, size_t writes, size_t reads)
    {
        asio::detail::mutex::scoped_lock lock(mutex_);
        total_bytes_written_ += bytes_written;
        total_bytes_read_ += bytes_read;
        total_writes_ += writes;
        total_reads_ += reads;
        ++session_count_;
    }

    void print()
    {
        asio::detail::mutex::scoped_lock lock(mutex_);

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
            "%20zd %8zd %6zd %18.3f %18.3f %10ld %12.3f %18zd %18zd\n",
            block_size_,
            session_count_,
            total_errors_,
            (double)total_bytes_read_ / total_reads_,
            (double)total_bytes_read_ / (timeout_ * 1024 * 1024),
            total_reads_ / timeout_,
            (double)100000 * timeout_ / total_reads_,
            total_bytes_read_,
            total_reads_);
    }

private:
    asio::detail::mutex mutex_;
    size_t total_bytes_written_;
    size_t total_bytes_read_;
    size_t total_writes_;
    size_t total_reads_;
    size_t block_size_;
    size_t session_count_;
    size_t total_errors_;
    int timeout_;
};

class client_session
{
public:
    client_session(asio::io_service & io_service, size_t block_size, stats & s)
        : io_service_(io_service)
        , socket_(io_service_)
        , block_size_(block_size)
        , data_(new char[block_size])
        // , read_data_length_(0)
        // , write_data_(new char[block_size])
        , writes_(0)
        , reads_(0)
        , bytes_written_(0)
        , bytes_read_(0)
        , stats_(s)
    {
        for (size_t i = 0; i < block_size_; ++i)
        {
            data_[i] = static_cast<char>(i % 128);
        }
    }

    ~client_session() { delete[] data_; }

    void start(asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
        socket_.async_connect(endpoint, boost::bind(&client_session::handle_connect, this, asio::placeholders::error, ++endpoint_iterator));
    }

    void stop()
    {
        stats_.add(bytes_written_, bytes_read_, writes_, reads_);

        bytes_written_ = 0;
        bytes_read_ = 0;
        writes_ = 0;
        reads_ = 0;

        close_socket();
    }

private:
    void handle_connect(const std::error_code & err, asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        if (!err)
        {
            asio::error_code set_option_err;
            asio::ip::tcp::no_delay no_delay(true);
            socket_.set_option(no_delay, set_option_err);
            if (!set_option_err)
            {
                do_write(block_size_);
            }
        }
        else if (endpoint_iterator != asio::ip::tcp::resolver::iterator())
        {
            socket_.close();
            asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
            socket_.async_connect(
                endpoint, boost::bind(&client_session::handle_connect, this, asio::placeholders::error, ++endpoint_iterator));
        }
    }

    void close_socket() { socket_.close(); }

    void do_read()
    {
        socket_.async_read_some(asio::buffer(data_, block_size_), [this](asio::error_code ec, std::size_t length) {
            if (!ec)
            {
                bytes_read_ += length;
                ++reads_;
                do_write(length);
            }
        });
        // make_custom_alloc_handler(read_allocator_, [this, self](std::error_code ec, std::size_t length) {
        //     if (!ec)
        //     {
        //         do_write(length);
        //     }
        // }));
    }

    void do_write(std::size_t length)
    {
        asio::async_write(socket_, asio::buffer(data_, length), [this](asio::error_code ec, std::size_t written) {
            if (!ec)
            {
                bytes_written_ += written;
                ++writes_;
                do_read();
            }
        });
        //make_custom_alloc_handler(write_allocator_, [this, self](std::error_code ec, std::size_t /*length*/) {
        // [this, self](std::error_code ec, std::size_t /*length*/) {
        //     if (!ec)
        //     {
        //         do_read();
        //     }
        // }));
    }
    asio::io_service & io_service_;
    asio::ip::tcp::socket socket_;
    size_t block_size_;
    char * data_;
    // size_t read_data_length_;
    // char * write_data_;
    // int unwritten_count_;
    size_t errors;
    size_t writes_;
    size_t reads_;
    size_t bytes_written_;
    size_t bytes_read_;
    stats & stats_;
    // handler_allocator read_allocator_;
    // handler_allocator write_allocator_;
};

class client
{
public:
    client(
        asio::io_service & io_service,
        const asio::ip::tcp::resolver::iterator endpoint_iterator,
        size_t block_size,
        size_t client_session_count,
        int timeout)
        : io_service_(io_service), stop_timer_(io_service), client_sessions_(), stats_(block_size, timeout)
    {
        for (size_t i = 0; i < client_session_count; ++i)
        {
            client_session * new_client_session = new client_session(io_service_, block_size, stats_);
            new_client_session->start(endpoint_iterator);
            client_sessions_.push_back(new_client_session);
        }

        stop_timer_.expires_from_now(boost::posix_time::seconds(timeout));
        stop_timer_.async_wait(boost::bind(&client::handle_timeout, this));
    }

    ~client()
    {
        while (!client_sessions_.empty())
        {
            auto session = client_sessions_.front();
            session->stop();
            delete session;
            client_sessions_.pop_front();
        }

        stats_.print();
    }

    void handle_timeout() { std::for_each(client_sessions_.begin(), client_sessions_.end(), boost::mem_fn(&client_session::stop)); }

private:
    asio::io_service & io_service_;
    asio::deadline_timer stop_timer_;
    std::list<client_session *> client_sessions_;
    stats stats_;
};

int main(int argc, char * argv[])
{
    try
    {
        int c;
        std::string host;
        std::string port_str = "9876";
        int port;
        int block_size = 16384;
        int client_session_count = 0;
        int seconds = 60;
        int client_start = 0;
        int server_start = 1;

#ifndef WIN32
        struct rlimit rl;
#endif
        while ((c = getopt(argc, argv, ":a:b:n:d:h")) != -1)
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
                    client_session_count = atoi(optarg);
                    break;
                case 'd':
                    seconds = atoi(optarg);
                    break;
                case 'h': {
                    fprintf(stderr, "Usage: %s -p <port> -b <blocksize> ", argv[0]);
                    fprintf(stderr, "-n <client_sessions> -d <time>\n");
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
        if (client_session_count > 0)
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

        signal(SIGPIPE, SIG_IGN);

#ifndef WIN32
        rl.rlim_cur = rl.rlim_max = client_session_count * 2 + 50;
        if (setrlimit(RLIMIT_NOFILE, &rl) == -1)
        {
            perror("setrlimit");
            exit(1);
        }
#endif

        service_runner * server = nullptr;
        boost::thread * thread_server = nullptr;
        if (server_start)
        {
            server = new service_runner();
            if (client_start)
            {
                thread_server = new boost::thread(server_run, server, static_cast<unsigned short>(port), static_cast<size_t>(block_size));
                sleep(1);
            }
            else
            {
                server_run(server, static_cast<unsigned short>(port), static_cast<size_t>(block_size));

                delete server;
                exit(0);
            }
        }

        if (client_start)
        {
            asio::io_service ios;

            asio::ip::tcp::resolver r(ios);
            asio::ip::tcp::resolver::iterator iter = r.resolve(asio::ip::tcp::resolver::query(host, port_str));

            client cl(ios, iter, block_size, client_session_count, seconds);

            ios.run();
        }

        if (server)
        {
            server->stop();
            thread_server->join();
            delete server;
            delete thread_server;
        }
    }
    catch (std::exception & e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
