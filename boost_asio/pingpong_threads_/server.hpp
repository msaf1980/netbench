#ifndef _SERVER_HPP
#define _SERVER_HPP

#include "asio.hpp"

struct service_runner
{
public:
    service_runner(size_t thread_count) : thread_count_(thread_count), ios_(), ios_work_(asio::make_work_guard(ios_))
    {
        for (size_t i = 0; i < thread_count; ++i)
        {
            thread_group_.add_thread(new boost::thread(boost::bind(&asio::io_service::run, &ios_)));
        }
    }

    template <class F>
    void enqueue(F f)
    {
        ios_.post(f);
    }

    void run() { ios_.run(); }

    void stop()
    {
        ios_work_.reset();
        thread_group_.join_all();
    }

    ~server_thread_group() { stop(); }

private:
    asio::io_context ios_;
    asio::executor_work_guard<asio::io_context::executor_type> ios_work_;
    size_t thread_count_;
    boost::thread_group thread_group_;
}

#endif /* _SERVER_HPP */
