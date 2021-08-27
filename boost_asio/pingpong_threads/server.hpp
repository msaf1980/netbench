#ifndef _SERVER_HPP
#define _SERVER_HPP

#include "asio.hpp"

struct service_runner
{
public:
    service_runner() : io_context_(), io_context_work_(asio::make_work_guard(io_context_)) { }

    ~service_runner() { stop(); }

    asio::io_context & io_context() { return io_context_; }

    template <class F>
    void enqueue(F f)
    {
        io_context_.post(f);
    }

    void run() { io_context_.run(); }

    void stop()
    {
        // io_context_work_.reset();
        io_context_.stop();
    }

private:
    asio::io_context io_context_;
    asio::executor_work_guard<asio::io_context::executor_type> io_context_work_;
};

void server_run(service_runner * runner, unsigned short port, size_t block_size);

#endif /* _SERVER_HPP */
