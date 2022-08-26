#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <list>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <hiredis/hiredis.h>

#include <example/messages.h>

using namespace boost::asio;

redisContext *setupContext(char *address, char *port)
{
    redisContext *c = redisConnect(address, std::atoi(port));
    if (c == nullptr || c->err) {
        if (c) {
            std::printf("Error: %s\n", c->errstr);
            // handle error
        } else {
            std::printf("Can't allocate redis context\n");
        }
        exit(1);
    }
    return c;
}

void getMessages() {}

void saveMessage(const std::string &msg) {}

struct Connection
{
    boost::asio::ip::tcp::socket socket;
    std::string username;

    Connection(boost::asio::io_service &io_service)
        : socket(io_service)
    {
        username = "";
    }
};

class Server
{
    io_service m_ioservice;
    ip::tcp::acceptor m_acceptor;
    std::list<Connection> m_pending_con;
    std::list<Connection> m_accepted_con;
    using con_handle_t = std::list<Connection>::iterator;

  public:
    Server() : m_ioservice(), m_acceptor(m_ioservice), m_pending_con(), m_accepted_con() {}

    void run() { m_ioservice.run(); }

    void stop() { m_ioservice.stop(); }

    void listen(uint16_t port)
    {
        auto endpoint = ip::tcp::endpoint(ip::tcp::v4(), port);
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen();
        start_accept();
    }

    void start_accept()
    {
        auto con_handle = m_pending_con.emplace(m_pending_con.begin(), m_ioservice);
        auto handler = boost::bind(&Server::handle_accept, this, con_handle, placeholders::error);
        m_acceptor.async_accept(con_handle->socket, handler);
    }

    void handle_accept(con_handle_t con_handle, boost::system::error_code const &err)
    {
        if (!err) {
            do_async_read_username(con_handle);
        } else {
            std::cerr << "We had an error: " << err.message() << "\n";
            m_pending_con.erase(con_handle);
        }
        start_accept();
    }

    void do_async_read_username(con_handle_t con_handle)
    {
        auto ub = std::make_shared<example::UsernameBlock>();
        auto handler = boost::bind(&Server::handle_read_username,
                                   this,
                                   con_handle,
                                   ub,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_read(con_handle->socket,
                   buffer(ub.get(), sizeof(example::UsernameBlock)),
                   transfer_exactly(sizeof(example::UsernameBlock)),
                   handler);
    }

    void handle_read_username(con_handle_t con_handle, std::shared_ptr<example::UsernameBlock> ub,
                              boost::system::error_code const &err, size_t bytes_transfered)
    {
        if (bytes_transfered == sizeof(example::UsernameBlock)) {
            con_handle->username = std::string(ub->username);
            std::cout << "User " << con_handle->username << " joined\n";
            con_handle_t new_handle = m_accepted_con.insert(m_accepted_con.begin(), std::move(*con_handle));
            m_pending_con.erase(con_handle);
            do_async_read_message(new_handle);
        } else {
            if (!err) {
                // async_read returned less than the requested size
                std::cerr << "Boost returned incomplete UsernameBlock, closing connection\n";
            } else if (err == error::eof) {
                std::cout << "Connection aborted prematurely\n";
            } else {
                std::cerr << "We had an error: " << err.message() << "\n";
            }
            m_pending_con.erase(con_handle);
        }
    }

    void do_async_read_message(con_handle_t con_handle)
    {
        auto mb = std::make_shared<example::MessageBlock>();
        auto handler = boost::bind(&Server::handle_read_message,
                                   this,
                                   con_handle,
                                   mb,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_read(con_handle->socket,
                   buffer(mb.get(), sizeof(example::MessageBlock)),
                   transfer_exactly(sizeof(example::MessageBlock)),
                   handler);
    }

    void handle_read_message(con_handle_t con_handle, std::shared_ptr<example::MessageBlock> mb,
                             boost::system::error_code const &err, size_t bytes_transfered)
    {
        if (bytes_transfered == sizeof(example::MessageBlock)) {
            strcpy(mb->username, con_handle->username.c_str());
            std::cout << "User " << mb->username << " sent message: " << mb->message << "\n";
            for (auto it = m_accepted_con.begin(); it != m_accepted_con.end(); ++it) {
                if (it == con_handle) {
                    continue;
                }
                do_async_send_message(it, mb);
            }
            do_async_read_message(con_handle);
        } else {
            if (!err) {
                // async_read returned less than the requested size
                std::cerr << "Boost returned incomplete MessageBlock, closing connection\n";
            }
            if (err == error::eof) {
                std::cout << con_handle->username << " disconnected\n";
            } else {
                std::cerr << "We had an error: " << err.message() << "\n";
            }
            m_accepted_con.erase(con_handle);
        }
    }

    void do_async_send_message(con_handle_t con_handle, std::shared_ptr<example::MessageBlock> mb)
    {
        auto ub = std::make_shared<example::UsernameBlock>();
        auto handler = boost::bind(&Server::handle_send_message,
                                   this,
                                   con_handle,
                                   mb,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_write(con_handle->socket, buffer(mb.get(), sizeof(example::MessageBlock)), handler);
    }

    void handle_send_message(con_handle_t con_handle, std::shared_ptr<example::MessageBlock> mb,
                             boost::system::error_code const &err, size_t bytes_transfered)
    {
        if (err) {
            std::cerr << "We had an error: " << err.message() << "\n";
            con_handle->socket.close();
            m_accepted_con.erase(con_handle);
        } else if (bytes_transfered != sizeof(example::MessageBlock)) {
            std::cerr << "Boost sent incomplete MessageBlock, closing connection\n";
        }
    }
};

/*
Arguments: ip, port, username

*/

int main(int argc, char **argv)
{
    if (argc < 5) {
        std::printf("Arguments: LISTEN_IP LISTEN_PORT REDIS_IP REDIS_PORT\n");
        exit(1);
    }

    // redisContext *c = setupContext(argv[1], argv[2]);

    auto srv = Server();
    srv.listen(std::atoi(argv[2]));

    // std::signal(SIGINT, [&srv](int signal) { srv.stop(); });

    srv.run();

    // redisFree(c);

    // boost::asio::streambuf buf;

    // redisReply *reply;

    // getMessages();

    // while (!terminate) {
    //     std::string msg;
    //     std::getline(std::cin, msg);
    //     saveMessage(msg);
    //     std::cout << msg << "\n";
    // }

    return 0;
}
