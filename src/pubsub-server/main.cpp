#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <latch>
#include <list>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/json.hpp>
#include <hiredis/async.h>

#include <example/messages.h>

using namespace boost::asio;

class redis_handler
{

  public:
    redis_handler(pub_sub_logic *p_logic) : m_logic(p_logic), m_redis_ctx(nullptr)
    {
        m_logic->redis_handler(this);
    }

    ~redis_handler()
    {
        if (m_redis_ctx != nullptr) {
            redisAsyncFree(m_redis_ctx);
        }
    }

    void connect(const std::string &address, const std::string &port)
    {
        m_redis_ctx = redisAsyncConnect(address.c_str(), std::atoi(port.c_str()));
        std::cout << "Async connect\n";
        std::latch barrier{1};
        if (m_redis_ctx->err) {
            std::cerr << "Error: " << m_redis_ctx->errstr << "\n";
            exit(1);
        }
        m_redis_ctx->data = &barrier;
        redisAsyncSetConnectCallback(m_redis_ctx, [](const redisAsyncContext *c, int status) {
            std::cout << "Counting down\n";
            ((std::latch *)c->data)->count_down();
        });
        // Wait until connection completes
        barrier.wait();
        m_redis_ctx->data = nullptr;
        std::cout << "Connection completed\n";
        // redisAsyncSetDisconnectCallback(m_redis_ctx, appOnDisconnect);
    }

    // void getMessages(uint32_t count) {
    //     reply = redisCommand(m_redis, "LRANGE mylist 0 -1");
    //     if (reply->type == REDIS_REPLY_ARRAY) {
    //         for (j = 0; j < reply->elements; j++) {
    //             printf("%u) %s\n", j, reply->element[j]->str);
    //         }
    //     }
    //     freeReplyObject(reply);
    // }

    // void saveMessage(const std::string &value) {
    //     std::string cmd = "LPUSH chatroom " + value;
    //     auto reply = redisCommand(m_redis, cmd.c_str());

    // }

  private:
    pub_sub_logic *m_logic;
    redisAsyncContext *m_redis_ctx;
};

struct tcp_connection
{
    ip::tcp::socket socket;

    tcp_connection(io_service &io_service) : socket(io_service) {}
};

using con_handle_t = std::list<tcp_connection>::iterator;

class tcp_handler
{
    pub_sub_logic *m_logic;
    io_service m_ioservice;
    ip::tcp::acceptor m_acceptor;
    std::list<tcp_connection> m_conn;

  public:
    tcp_handler(pub_sub_logic *p_logic) : m_logic(p_logic), m_ioservice(), m_acceptor(m_ioservice), m_conn() {}

    void run() { m_ioservice.run(); }

    void stop() { m_ioservice.stop(); }

    void listen(const char *p_address, const char *p_port)
    {
        auto endpoint = ip::tcp::endpoint(ip::tcp::v4(), std::atoi(p_port));
        m_acceptor.open(endpoint.protocol());
        m_acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
        m_acceptor.bind(endpoint);
        m_acceptor.listen();
        start_accept();
    }

    void start_accept()
    {
        auto con_handle = m_conn.emplace(m_conn.begin(), m_ioservice);
        auto handler = boost::bind(&tcp_handler::handle_accept, this, con_handle, placeholders::error);
        m_acceptor.async_accept(con_handle->socket, handler);
    }

    void handle_accept(con_handle_t p_con_handle, boost::system::error_code const &p_err)
    {
        if (!p_err) {
            m_logic->new_tcp_connection(p_con_handle);
        } else {
            std::cerr << "We had an error: " << p_err.message() << "\n";
            m_conn.erase(p_con_handle);
        }
        start_accept();
    }

    void do_async_read_username(con_handle_t p_con_handle)
    {
        auto ub = std::make_shared<example::UsernameBlock>();
        auto handler = boost::bind(&tcp_handler::handle_read_username,
                                   this,
                                   p_con_handle,
                                   ub,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_read(p_con_handle->socket,
                   buffer(ub.get(), sizeof(example::UsernameBlock)),
                   transfer_exactly(sizeof(example::UsernameBlock)),
                   handler);
    }

    void handle_read_username(con_handle_t p_con_handle, std::shared_ptr<example::UsernameBlock> p_ub,
                              const boost::system::error_code &p_err, size_t p_bytes_transfered)
    {
        if (p_bytes_transfered == sizeof(example::UsernameBlock)) {
            m_logic->recv_username(p_con_handle, p_ub);
        } else {
            if (!p_err) {
                // async_read returned less than the requested size
                std::cerr << "Boost returned incomplete UsernameBlock, closing connection\n";
            } else if (p_err == error::eof) {
                std::cout << "Connection aborted prematurely\n";
            } else {
                std::cerr << "We had an error: " << p_err.message() << "\n";
            }
            m_conn.erase(p_con_handle);
        }
    }

    // void send_previous_messages(con_handle_t con_handle, uint32_t count)
    // {

    //     // do_redis_request;
    //     // for (int i = 0; i < size; ++i){
    //     auto mb = std::make_shared<example::MessageBlock>();
    //     // set message content;
    //     auto handler = boost::bind(&PubSubServer::handle_send_message,
    //                                this,
    //                                con_handle,
    //                                mb,
    //                                placeholders::error,
    //                                placeholders::bytes_transferred);
    //     async_write(con_handle->socket, buffer(mb.get(), sizeof(example::MessageBlock)), handler);
    //     // }
    // }

    void do_async_read_message(con_handle_t p_con_handle, std::string p_user)
    {
        auto mb = std::make_shared<example::MessageBlock>();
        auto handler = boost::bind(&tcp_handler::handle_read_message,
                                   this,
                                   p_con_handle,
                                   mb,
                                   p_user,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_read(p_con_handle->socket,
                   buffer(mb.get(), sizeof(example::MessageBlock)),
                   transfer_exactly(sizeof(example::MessageBlock)),
                   handler);
    }

    void handle_read_message(con_handle_t p_con_handle, std::shared_ptr<example::MessageBlock> p_mb, std::string p_user,
                             boost::system::error_code const &p_err, size_t p_bytes_transfered)
    {
        if (p_bytes_transfered == sizeof(example::MessageBlock)) {
            m_logic->recv_message(p_user, p_mb);
        } else {
            if (!p_err) {
                // async_read returned less than the requested size
                std::cerr << "Boost returned incomplete MessageBlock, closing connection\n";
            }
            if (p_err == error::eof) {
                std::cout << p_con_handle->username << " disconnected\n";
            } else {
                std::cerr << "We had an error: " << p_err.message() << "\n";
            }
            m_conn.erase(p_con_handle);
        }
    }

    void do_async_send_message(con_handle_t p_con_handle, std::shared_ptr<example::MessageBlock> p_mb)
    {
        auto handler = boost::bind(&tcp_handler::handle_send_message,
                                   this,
                                   p_con_handle,
                                   p_mb,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_write(p_con_handle->socket, buffer(p_mb.get(), sizeof(example::MessageBlock)), handler);
    }

    void handle_send_message(con_handle_t p_con_handle, std::shared_ptr<example::MessageBlock> p_mb,
                             boost::system::error_code const &p_err, size_t p_bytes_transfered)
    {
        if (p_err) {
            std::cerr << "We had an error: " << p_err.message() << "\n";
            p_con_handle->socket.close();
            m_conn.erase(p_con_handle);
        } else if (p_bytes_transfered != sizeof(example::MessageBlock)) {
            std::cerr << "Boost sent incomplete MessageBlock, closing connection\n";
        }
    }
};

class pub_sub_logic
{
  public:
    pub_sub_logic() : m_redis_handler(nullptr), m_tcp_handler(nullptr) {}

    void set_redis_handler(redis_handler *p_redis_handler)
    {
        m_redis_handler = p_redis_handler;
    }

    void set_tcp_handler(tcp_handler *p_tcp_handler)
    {
        m_tcp_handler = p_tcp_handler;
    }

    void new_tcp_connection(con_handle_t p_conn_handle)
    {
        m_tcp_handler->do_async_read_username(p_conn_handle);
    }

    void recv_username(con_handle_t p_conn_handle, std::shared_ptr<example::UsernameBlock> p_ub)
    {
        m_users[std::string(p_ub->username)] = p_conn_handle;
        m_tcp_handler->do_async_read_message(p_conn_handle);
    }

    void recv_message(std::string p_user, std::shared_ptr<example::MessageBlock> p_mb)
    {
        for (auto it = m_accepted_con.begin(); it != m_accepted_con.end(); ++it) {
            if (it == p_con_handle) {
                continue;
            }
            do_async_send_message(it, p_mb);
        }
        do_async_read_message(p_con_handle);
    }

    void send_message()
    {
    }

  private:
    redis_handler *m_redis_handler;
    tcp_handler *m_tcp_handler;
    std::unordered_map<std::string, con_handle_t> m_users;
};

int main(int argc, char **argv)
{
    if (argc < 5) {
        std::printf("Arguments: LISTEN_IP LISTEN_PORT REDIS_IP REDIS_PORT\n");
        exit(1);
    }

    auto

        auto srv = PubSubServer();
    srv.connect_redis(argv[3], argv[4]);
    srv.listen(argv[1], argv[2]);
    srv.run();

    return 0;
}
