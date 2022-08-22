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
using ip::tcp;
using std::cout;
using std::endl;

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
    boost::asio::streambuf read_buffer;

    std::string username;

    Connection(boost::asio::io_service &io_service)
        : socket(io_service), read_buffer()
    {
    }

    Connection(boost::asio::io_service &io_service, size_t max_buffer_size)
        : socket(io_service), read_buffer(max_buffer_size)
    {
    }
};

class Server
{
    boost::asio::io_service m_ioservice;
    boost::asio::ip::tcp::acceptor m_acceptor;
    std::list<Connection> m_pending_con;
    std::list<Connection> m_accepted_con;
    using con_handle_t = std::list<Connection>::iterator;

  public:
    Server() : m_ioservice(), m_acceptor(m_ioservice), m_pending_con(), m_accepted_con() {}

    void run() { m_ioservice.run(); }

    void stop() { m_ioservice.stop(); }

    void listen(uint16_t port)
    {
        auto endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port);
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen();
        start_accept();
    }

    void start_accept()
    {
        auto con_handle = m_pending_con.emplace(m_pending_con.begin(), m_ioservice);
        auto handler = boost::bind(&Server::handle_accept, this, con_handle, boost::asio::placeholders::error);
        m_acceptor.async_accept(con_handle->socket, handler);
    }

    void handle_accept(con_handle_t con_handle, boost::system::error_code const &err)
    {
        if (!err) {
            // std::cout << "Connection from: " << con_handle->remote_endpoint().address().to_string() << "\n";
            // std::cout << "Sending message\n";
            // auto buff = std::make_shared<std::string>("Hello World!\r\n\r\n");
            // auto handler = boost::bind(&Server::handle_write, this, con_handle, buff, boost::asio::placeholders::error);
            // boost::asio::async_write(con_handle->socket, boost::asio::buffer(*buff), handler);
            do_async_read_username(con_handle);
        } else {
            std::cerr << "We had an error: " << err.message() << std::endl;
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
                                   boost::asio::placeholders::error,
                                   boost::asio::placeholders::bytes_transferred);
        boost::asio::async_read(con_handle->socket,
                                boost::asio::buffer(ub.get(), sizeof(example::UsernameBlock)),
                                boost::asio::transfer_exactly(sizeof(example::UsernameBlock)),
                                handler);
    }

    void handle_read_username(con_handle_t con_handle, std::shared_ptr<example::UsernameBlock> ub,
                              boost::system::error_code const &err, size_t bytes_transfered)
    {
        if (bytes_transfered == sizeof(example::UsernameBlock)) {
            std::cout << "User " << ub->username << " joined\n";
            con_handle->socket.close();
            m_pending_con.erase(con_handle);
        } else {
            if (!err) {
                // async_read returned less than the requested size
                std::cerr << "Boost returned incomplete UsernameBlock, closing connection\n";
            } else {
                std::cerr << "We had an error: " << err.message() << std::endl;
            }
            m_pending_con.erase(con_handle);
        }
    }

    void do_async_read_username_2(con_handle_t con_handle)
    {
        auto handler = boost::bind(&Server::handle_read_username_2,
                                   this,
                                   con_handle,
                                   boost::asio::placeholders::error,
                                   boost::asio::placeholders::bytes_transferred);
        int missing_bytes = (int)con_handle->username.size() - (int)con_handle->read_buffer.size();
        boost::asio::async_read(con_handle->socket,
                                con_handle->read_buffer,
                                boost::asio::transfer_exactly(std::max(0, missing_bytes)),
                                handler);
    }

    void handle_read_username_2(con_handle_t con_handle, boost::system::error_code const &err, size_t bytes_transfered)
    {
        if (bytes_transfered > 0 && con_handle->read_buffer.size() >= con_handle->username.size()) {
            std::istream is(&con_handle->read_buffer);
            is.read(con_handle->username.data(), con_handle->username.size());
            cout << "User " << con_handle->username << " joined the chat" << endl;

            con_handle->socket.close();
            m_pending_con.erase(con_handle);
        } else {
            if (!err) {
                do_async_read_username_2(con_handle);
            } else {
                std::cerr << "We had an error: " << err.message() << std::endl;
                m_pending_con.erase(con_handle);
            }
        }
    }

    void do_async_read(con_handle_t con_handle)
    {
        auto handler = boost::bind(&Server::handle_read, this, con_handle,
                                   boost::asio::placeholders::error,
                                   boost::asio::placeholders::bytes_transferred);
        boost::asio::async_read_until(con_handle->socket, con_handle->read_buffer, "\n", handler);
    }

    void handle_read(con_handle_t con_handle, boost::system::error_code const &err, size_t bytes_transfered)
    {
        if (bytes_transfered > 0) {
            std::istream is(&con_handle->read_buffer);
            std::string line;
            std::getline(is, line);
        }

        if (!err) {
            do_async_read(con_handle);
        } else {
            std::cerr << "We had an error: " << err.message() << std::endl;
            m_accepted_con.erase(con_handle);
        }
    }

    void handle_write(con_handle_t con_handle,
                      std::shared_ptr<std::string> msg_buffer,
                      boost::system::error_code const &err)
    {
        if (!err) {
            std::cout << "Finished sending message\n";
            if (con_handle->socket.is_open()) {
                // Write completed successfully and connection is open
            }
        } else {
            std::cerr << "We had an error: " << err.message() << std::endl;
            m_accepted_con.erase(con_handle);
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
