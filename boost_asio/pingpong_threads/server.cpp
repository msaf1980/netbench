//
// server.cpp
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
#include "asio.hpp"
#include "handler_allocator.hpp"
#include <boost/bind.hpp>

#include "server.hpp"

class server_session : public std::enable_shared_from_this<server_session>
{
public:
    server_session(asio::ip::tcp::socket socket, size_t block_size)
        : socket_(std::move(socket)), block_size_(block_size), data_(new char[block_size])
    {
    }

    ~server_session() { delete[] data_; }

    asio::ip::tcp::socket & socket() { return socket_; }

    void start()
    {
        auto self(shared_from_this());
        asio::error_code set_option_err;
        asio::ip::tcp::no_delay no_delay(true);
        socket_.set_option(no_delay, set_option_err);

        if (!set_option_err)
        {
            do_read();
        }
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(
            asio::buffer(data_, block_size_),
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec)
                {
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
        auto self(shared_from_this());
        asio::async_write(
            socket_,
            asio::buffer(data_, length),
            [this, self](std::error_code ec, std::size_t /*length*/) {
                if (!ec)
                {
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

    asio::ip::tcp::socket socket_;
    size_t block_size_;
    char * data_;
    // handler_allocator read_allocator_;
    // handler_allocator write_allocator_;
};

class server
{
public:
    server(asio::io_service & ios, const asio::ip::tcp::endpoint & endpoint, size_t block_size)
        : io_context_(ios), acceptor_(ios), block_size_(block_size)
    {
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(1));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        do_accept();
    }


private:
    void do_accept()
    {
        acceptor_.async_accept([this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec)
            {
                std::make_shared<server_session>(std::move(socket), block_size_)->start();
            }

            do_accept();
        });
    }

    asio::io_service & io_context_;
    asio::ip::tcp::acceptor acceptor_;
    size_t block_size_;
};

// private:
//     asio::io_service ios_;
//     asio::executor_work_guard<asio::io_context::executor_type> ios_work_;
//     size_t thread_count_;
//     boost::thread_group thread_group_;
// };

void server_run(service_runner * runner, unsigned short port, size_t block_size)
{
    server s(runner->io_context(), asio::ip::tcp::endpoint(asio::ip::address::from_string("127.0.0.1"), port), block_size);
    runner->run();
}
