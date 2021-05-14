#include <iostream>
#include <map>
#include <thread>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Global Constants
const int SERVER_PORT = 12345;
const int LISTENNQ = 5;
const int MAXLINE = 8192;

const std::string SP = " ";
const std::string CRLF = "\r\n";

const std::map<std::string, std::string> CONTENT_TYPES = {
    {"bmp", "image/bmp"},
    {"css", "text/css"},
    {"csv", "text/csv"},
    {"doc", "application/msword"},
    {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {"gz", "application/gzip"},
    {"gif", "image/gif"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {"ico", "image/vnd.microsoft.icon"},
    {"jpeg", "image/jpeg"},
    {"jpg", "image/jpeg"},
    {"js", "text/javascript"},
    {"json", "application/json"},
    {"mp3", "audio/mpeg"},
    {"mp4", "video/mp4"},
    {"mpeg", "video/mpeg"},
    {"png", "image/png"},
    {"pdf", "application/pdf"},
    {"php", "application/x-httpd-php"},
    {"ppt", "application/vnd.ms-powerpoint"},
    {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {"rar", "application/vnd.rar"},
    {"sh", "application/x-sh"},
    {"svg", "image/svg+xml"},
    {"tar", "application/x-tar"},
    {"txt", "text/plain"},
    {"wav", "audio/wav"},
    {"weba", "audio/webm"},
    {"webm", "audio/webm"},
    {"webp", "image/webp"},
    {"xhtml", "application/xhtml+xml"},
    {"xls", "application/vnd.ms-excel"},
    {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {"zip", "application/zip"},
    {"7z", "application/x-7z-compressed"},
};

enum class HttpMethod
{
    UNDEFINED = -1,
    GET,
    POST,
};

class HttpRequest
{
public:
    HttpRequest() : method(HttpMethod::UNDEFINED), url(""), version("") {}
    HttpMethod method;
    std::string url;
    std::string version;
    bool isBad();
    std::string toString();
    static HttpRequest *parse(std::string msg);
    static HttpMethod toMethod(std::string method);
};

class HttpResponse
{
public:
    std::string version;
    int status_code;
    std::string reason_phrase;
    std::string content_type;
};

bool startsWith(std::string, std::string);
bool endsWith(std::string, std::string);
HttpRequest *parse_request(int conn_fd);
void request_handler(int conn_fd);

int main()
{
    int server_fd, conn_fd;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
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
        t.detach();
    }

    return 0;
}

void request_handler(int conn_fd)
{
    HttpRequest *request = parse_request(conn_fd);
}

HttpRequest *parse_request(int conn_fd)
{
    int buffer_size = 0;
    char buf[MAXLINE] = {0};
    std::string msg{""};
    HttpRequest *request = nullptr;

    while (true)
    {
        buffer_size = recv(conn_fd, buf, MAXLINE - 1, 0);

        if (buffer_size <= 0)
        {
            std::cerr << "Recv failed!" << std::endl;
            continue;
        }

        if (buf[buffer_size] != '\0')
        {
            buf[buffer_size] = '\0';
        }
        msg += buf;

        request = HttpRequest::parse(msg);
        if (!request->isBad())
        {
            break;
        }
        delete request;
        request = nullptr;
    }

    return request;
}

bool startsWith(std::string base, std::string compare)
{
    if (compare.length() <= 0 || base.length() < compare.length())
    {
        return false;
    }
    return base.compare(0, compare.length(), compare) == 0;
}

bool endsWith(std::string base, std::string compare)
{
    if (compare.length() <= 0 || base.length() < compare.length())
    {
        return false;
    }
    return base.compare(base.length() - compare.length(), std::string::npos, compare) == 0;
}

bool HttpRequest::isBad()
{
    if (this->method == HttpMethod::UNDEFINED)
        return true;

    if (!startsWith(this->url, "/"))
        return true;

    if (!startsWith(this->version, "HTTP/"))
        return true;

    return false;
}

std::string HttpRequest::toString()
{
    std::string value{""};
    value += "HttpRequest {";
    value += ("\n\tisBad: " + this->isBad() ? "true" : "false");
    value += "\n\tmethod: ";
    switch (this->method)
    {
    case HttpMethod::GET:
        value += "GET";
        break;
    case HttpMethod::POST:
        value += "POST";
        break;
    case HttpMethod::UNDEFINED:
    default:
        value += "UNDEFINED";
    }
    value += ("\n\turl: " + this->url);
    value += ("\n\tversion: " + this->version);
    value += "\n}\n";
    return value;
}

HttpRequest *HttpRequest::parse(std::string msg)
{
    HttpRequest *request = new HttpRequest();

    // parse
    int start_pos = 0;
    int end_pos = msg.find(SP, start_pos);
    if (start_pos >= msg.length() || end_pos == std::string::npos)
    {
        return request;
    }

    request->method = toMethod(msg.substr(start_pos, end_pos - start_pos));

    start_pos = end_pos + 1;
    end_pos = msg.find(SP, start_pos);
    if (start_pos >= msg.length() || end_pos == std::string::npos)
    {
        return request;
    }

    request->url = msg.substr(start_pos, end_pos - start_pos);
    if (request->url == "/")
    {
        request->url = "/index.html";
    }

    start_pos = end_pos + 1;
    end_pos = msg.find(CRLF, start_pos);
    if (start_pos >= msg.length() || end_pos == std::string::npos)
    {
        return request;
    }

    request->version = msg.substr(start_pos, end_pos - start_pos);

    return request;
}

HttpMethod HttpRequest::toMethod(std::string method)
{
    if (method == "GET" || method == "get")
        return HttpMethod::GET;

    if (method == "POST" || method == "post")
        return HttpMethod::POST;

    return HttpMethod::UNDEFINED;
}
