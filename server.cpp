#include <iostream>
#include <thread>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Global Constants
const int SERVER_PORT = 80; // use port 80 for HTTP
const int LISTENNQ = 5;
const int MAXLINE = 100;

void request_handler(int conn_fd);

int main()
{
    int server_fd, conn_fd;
    if (server_fd = socket(AF_INET, SOCK_STREAM, 0) < 0)
    {
        std::cerr << "Socket creation failed!" << std::endl;
        return 0;
    }

    sockaddr_in server_addr, client_addr;
    socklen_t len = sizeof(sockaddr_in);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd, (sockaddr *)&server_addr, sizeof(sockaddr)) < 0)
    {
        std::cerr << "Bind failed!" << std::endl;
        return 0;
    }

    if (listen(server_fd, LISTENNQ) < 0)
    {
        std::cerr << "Listen failed!" << std::endl;
        return 0;
    }

    char ip_str[INET_ADDRSTRLEN] = {0};

    while (true)
    {
        conn_fd = accept(server_fd, (sockaddr *)&client_addr, &len);
        if (conn_fd < 0)
        {
            std::cerr << "Accept failed!" << std::endl;
            return 0;
        }

        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);

        std::cout << "Connection from " << ip_str << ":" << ntohs(client_addr.sin_port) << std::endl;

        std::thread t(request_handler, conn_fd);
    }

    return 0;
}

void request_handler(int conn_fd)
{
}
