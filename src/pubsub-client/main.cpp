#include <cstdlib>
#include <iostream>
#include <regex>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

#include <example/messages.h>

/*
Arguments: ip, port, username

*/

void handle_write(std::shared_ptr<example::UsernameBlock> msg_buffer,
                  boost::system::error_code const &err)
{
    if (!err) {
        std::cout << "Finished sending message\n";
    } else {
        std::cerr << "We had an error: " << err.message() << std::endl;
    }
}

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

    // connect to server
    boost::asio::io_service ios;
    boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::address::from_string(argv[1]), std::atoi(argv[2]));
    boost::asio::ip::tcp::socket socket(ios);
    socket.connect(endpoint);

    // copy data into buffer
    auto ub = std::make_shared<example::UsernameBlock>(username);
    socket.send(boost::asio::buffer(ub.get(), sizeof(example::UsernameBlock)));
    // auto handler = boost::bind(&handle_write, ub, boost::asio::placeholders::error);

    // boost::asio::async_write(socket, boost::asio::buffer(*ub), handler);

    // std::printf("Started session with username \"%s\"\n\n", username);

    // boost::asio::io_service io_service;
    // boost::asio::ip::tcp::socket socket(io_service);
    // boost::system::error_code error;

    // socket.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string(argv[1]), std::atoi(argv[2])));
    // boost::asio::write(socket, boost::asio::buffer(username), error);

    // if (error){
    //     std::fprintf(stderr, "TCP error while sending username\n");
    //     exit(1);
    // }

    // socket.close();

    // while (!terminate) {
    //     std::string msg;
    //     std::getline(std::cin, msg);
    //     saveMessage(msg);
    //     std::cout << msg << "\n";
    // }

    return 0;
}
