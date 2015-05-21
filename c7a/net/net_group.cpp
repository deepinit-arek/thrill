/*******************************************************************************
 * c7a/net/net_group.cpp
 *
 * NetGroup is a collection of NetConnections providing simple MPI-like
 * collectives and point-to-point communication.
 *
 * Part of Project c7a.
 *
 * Copyright (C) 2015 Timo Bingmann <tb@panthema.net>
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#include <c7a/net/net_group.hpp>
#include <c7a/net/net_dispatcher.hpp>

#include <deque>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace c7a {
namespace net {

template <typename Functional, size_t BufferSize = 0>
class NetReadBuffer
{
public:
    //! Construct buffered reader with callback
    NetReadBuffer(lowlevel::Socket& socket, size_t buffer_size = BufferSize,
                  const Functional& functional = Functional())
        : functional_(functional),
          buffer_(buffer_size, 0)
    {
        if (buffer_size == 0)
            functional_(socket, buffer_);
    }

    //! Should be called when the socket is readable
    bool operator () (lowlevel::Socket& s)
    {
        int r = s.recv_one(const_cast<char*>(buffer_.data() + size_),
                           buffer_.size() - size_);

        if (r < 0)
            throw Exception("NetReadBuffer() error in recv", errno);

        size_ += r;

        if (size_ == buffer_.size()) {
            functional_(s, buffer_);
            return false;
        }
        else {
            return true;
        }
    }

private:
    //! total size currently read
    size_t size_ = 0;

    //! functional object to call once data is complete
    Functional functional_;

    //! Receive buffer
    std::string buffer_;
};

NetGroup::NetGroup(ClientId my_rank, size_t group_size)
    : my_rank_(my_rank),
      connections_(group_size)
{ }

void NetGroup::ExecuteLocalMock(
    size_t num_clients,
    const std::function<void(NetGroup*)>& thread_function)
{
    using lowlevel::Socket;

    // construct a group of num_clients
    std::vector<std::unique_ptr<NetGroup> > group(num_clients);

    for (size_t i = 0; i != num_clients; ++i) {
        group[i] = std::unique_ptr<NetGroup>(new NetGroup(i, num_clients));
    }

    // construct a stream socket pair for (i,j) with i < j
    for (size_t i = 0; i != num_clients; ++i) {
        for (size_t j = i + 1; j < num_clients; ++j) {
            LOG << "doing Socket::CreatePair() for i=" << i << " j=" << j;

            std::pair<Socket, Socket> sp = Socket::CreatePair();

            group[i]->connections_[j] = std::move(NetConnection(sp.first));
            group[j]->connections_[i] = std::move(NetConnection(sp.second));
        }
    }

    // create a thread for each NetGroup object and run user program.
    std::vector<std::thread> threads(num_clients);

    for (size_t i = 0; i != num_clients; ++i) {
        threads[i] = std::thread(
            std::bind(thread_function, group[i].get()));
    }

    for (size_t i = 0; i != num_clients; ++i) {
        threads[i].join();
    }
}

} // namespace net
} // namespace c7a

/******************************************************************************/