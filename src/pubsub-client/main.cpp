#include <cstdlib>
#include <iostream>
#include <regex>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

#include <example/messages.h>

using namespace boost::asio;

class PubSubClient
{
    io_service m_ioservice;
    ip::tcp::socket m_socket;
    posix::stream_descriptor m_input;
    std::string m_username;
    streambuf m_line_buffer;

  public:
    PubSubClient(std::string username) : m_ioservice(), m_socket(m_ioservice),
                                         m_input(m_ioservice, ::dup(STDIN_FILENO)), m_username(username),
                                         m_line_buffer(example::maxMessageLength) {}

    void connect(char *address, uint16_t port)
    {
        ip::tcp::endpoint endpoint(ip::address::from_string(address), port);
        m_socket.connect(endpoint);
        do_async_read_line();
        do_async_recv_message();
    }

    void run()
    {
        send_username();
        m_ioservice.run();
    }

    void stop() { m_ioservice.stop(); }

    void send_username()
    {
        // send username
        auto ub = example::UsernameBlock(m_username);
        m_socket.send(buffer(&ub, sizeof(example::UsernameBlock)));
    }

    void do_async_read_line()
    {
        auto handler = boost::bind(&PubSubClient::handle_read_line,
                                   this,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_read_until(m_input, m_line_buffer, '\n', handler);
    }

    void handle_read_line(boost::system::error_code const &err, size_t bytes_read)
    {
        auto buffer = std::make_shared<example::MessageBlock>();
        if (!err) {
            m_line_buffer.sgetn(buffer->message, bytes_read);
            buffer->message[bytes_read - 1] = '\0'; // replace newline with end of string
            // std::cout << "Read message: " << buffer->message << std::endl;
            do_async_send_message(buffer);
            do_async_read_line();
        } else if (err == error::not_found) {
            std::cerr << "Warning: message too long, it will be split in chunks" << std::endl;
            m_line_buffer.sgetn(buffer->message, example::maxMessageLength);
            buffer->message[example::maxMessageLength] = '\0';
            // std::cout << "Read max message: " << buffer->message << std::endl;
            do_async_send_message(buffer);
            do_async_read_line();
        } else if (err == error::eof) {
            exit(0);
        } else {
            std::cerr << "We had an error: " << err.message() << std::endl;
            exit(1);
        }
    }

    void do_async_send_message(std::shared_ptr<example::MessageBlock> mb)
    {
        auto handler = boost::bind(&PubSubClient::handle_write_message,
                                   this,
                                   mb,
                                   placeholders::error);
        async_write(m_socket, buffer(mb.get(), sizeof(example::MessageBlock)), handler);
    }

    void handle_write_message(std::shared_ptr<example::MessageBlock>, boost::system::error_code const &err)
    {
        if (err) {
            std::cerr << "We had an error: " << err.message() << std::endl;
            exit(1);
        }
    }

    void do_async_recv_message()
    {
        auto mb = std::make_shared<example::MessageBlock>();
        auto handler = boost::bind(&PubSubClient::handle_read_message,
                                   this,
                                   mb,
                                   placeholders::error,
                                   placeholders::bytes_transferred);
        async_read(m_socket,
                   buffer(mb.get(), sizeof(example::MessageBlock)),
                   transfer_exactly(sizeof(example::MessageBlock)),
                   handler);
    }

    void handle_read_message(std::shared_ptr<example::MessageBlock> mb, boost::system::error_code const &err,
                             size_t bytes_transfered)
    {
        if (bytes_transfered == sizeof(example::MessageBlock)) {
            std::cout << mb->username << " : " << mb->message << "\n";
            do_async_recv_message();
        } else {
            if (err) {
                if (err == error::eof) {
                    std::cerr << "Server disconnected\n";
                } else {
                    std::cerr << "We had an error: " << err.message() << std::endl;
                }
            } else {
                std::cerr << "Boost returned incomplete MessageBlock, closing connection\n";
            }
            exit(1);
        }
    }
};

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::printf("Client arguments: IP PORT\n");
        exit(1);
    }

    // read username
    std::cout << "Enter username: ";
    std::string username;
    std::getline(std::cin, username);
    // trim leading and ending spaces
    const std::regex base_regex("\\s*(\\S*)\\s*");
    std::smatch base_match;
    if (std::regex_match(username, base_match, base_regex)) {
        username = base_match[1];
    }
    // check username length
    if (username.size() > example::maxUsernameLength) {
        std::cerr << "Username exceeds max. size: " << example::maxUsernameLength << std::endl;
        return 1;
    }

    // run client
    PubSubClient client(username);
    client.connect(argv[1], std::atoi(argv[2]));
    client.run();

    return 0;
}
