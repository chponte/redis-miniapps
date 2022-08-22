#include <cstdlib>
#include <iostream>
#include <regex>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

#include <example/messages.h>

class PubSubClient
{
    boost::asio::io_service m_ioservice;
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::posix::stream_descriptor m_input;
    std::string m_username;
    boost::asio::streambuf m_line_buffer;

  public:
    PubSubClient(std::string username) : m_ioservice(), m_socket(m_ioservice),
                                         m_input(m_ioservice, ::dup(STDIN_FILENO)), m_username(username),
                                         m_line_buffer(example::maxMessageLength) {}

    void connect(char *address, uint16_t port)
    {
        boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(address), port);
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
        m_socket.send(boost::asio::buffer(&ub, sizeof(example::UsernameBlock)));
    }

    void do_async_read_line()
    {
        auto handler = boost::bind(&PubSubClient::handle_read_line,
                                   this,
                                   boost::asio::placeholders::error,
                                   boost::asio::placeholders::bytes_transferred);
        boost::asio::async_read_until(m_input, m_line_buffer, '\n', handler);
    }

    void handle_read_line(boost::system::error_code const &err, size_t bytes_read)
    {
        auto buffer = std::make_shared<example::MessageBlock>();
        if (!err) {
            m_line_buffer.sgetn(buffer->message, bytes_read);
            buffer->message[bytes_read - 1] = '\0'; // replace newline with end of string
            std::cout << "Read message: " << buffer->message << std::endl;
            do_async_send_message(buffer);
            do_async_read_line();
        } else if (err == boost::asio::error::not_found) {
            std::cerr << "Warning: message too long, it will be split in chunks" << std::endl;
            m_line_buffer.sgetn(buffer->message, example::maxMessageLength);
            buffer->message[example::maxMessageLength] = '\0';
            std::cout << "Read max message: " << buffer->message << std::endl;
            do_async_send_message(buffer);
            do_async_read_line();
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
                                   boost::asio::placeholders::error);
        boost::asio::async_write(m_socket, boost::asio::buffer(mb.get(), sizeof(example::MessageBlock)), handler);
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
    }

    void handle_write(std::shared_ptr<example::UsernameBlock> msg_buffer,
                      boost::system::error_code const &err)
    {
        if (!err) {
            std::cout << "Finished sending message\n";
        } else {
            std::cerr << "We had an error: " << err.message() << std::endl;
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
