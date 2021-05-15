#include <iostream>
#include <fstream>
#include <map>
#include <thread>
#include <time.h>
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
    int status();
    bool sendResponse(int conn_fd);
    std::string toString();
    static HttpRequest *parse(std::string msg);
    static HttpMethod toMethod(std::string method);
};

class HttpResponse
{
public:
    HttpResponse() : version(""), status_code(503), content_type(""), content_length(-1) {}
    HttpResponse(HttpRequest *request);
    ~HttpResponse();
    std::string version;
    int status_code;
    std::string content_type;
    int content_length;
    std::string toString();
    static const std::map<int, std::string> REASON_PHRASES;
    static std::string toReasonPhrase(int status_code);
    static const std::map<std::string, std::string> CONTENT_TYPES;
    static std::string toContentType(std::string name);
    static std::string currentDateTime();

private:
    std::ifstream ifs;
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
    request->sendResponse(conn_fd);

    // TODO: keep connection if not explicitly closed
    close(conn_fd);
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
        int status = request->status();
        if (status == 0 || (status >= 200 && status < 400))
        {
            break;
        }
        // ? respond with 400
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

HttpResponse::HttpResponse(HttpRequest *request)
    : version(request->version),
      status_code(500),
      content_type(""),
      content_length(-1)
{
    int status = request->status();
    if (status >= 400)
    {
        this->status_code = status;
        return;
    }

    int pos = request->url.find_last_of("/") + 1;
    if (pos == std::string::npos || pos + 1 >= request->url.length())
    {
        this->status_code = 404;
        std::cerr << "Unknown request object with url " << request->url << std::endl;
        return;
    }

    std::string name = request->url.substr(pos + 1);
    std::string contentType = HttpResponse::toContentType(name);
    if (startsWith(contentType, "Error"))
    {
        this->status_code = 415;
        std::cerr << "Unknown file type with name " << name << std::endl;
        return;
    }

    if (endsWith(contentType, "directory"))
    {
        this->status_code = 403;
        std::cerr << "Missing file extension with name " << name << std::endl;
        return;
    }

    this->content_type = contentType;

    auto flags = std::ifstream::in;
    if (!startsWith(this->content_type, "text/"))
    {
        flags |= std::ifstream::binary;
    }

    this->ifs.open("." + request->url, flags);
    if (!this->ifs.is_open() || !this->ifs.good())
    {
        this->status_code = 404;
        std::cerr << "Reading file failed with path " << request->url << std::endl;
        return;
    }

    this->ifs.seekg(0, this->ifs.end);
    this->content_length = this->ifs.tellg();
    this->ifs.seekg(0, this->ifs.beg);

    if (this->content_length < 0)
    {
        this->status_code = 404;
        std::cerr << "Reading file size failed with path " << request->url << std::endl;
        return;
    }
}

HttpResponse::~HttpResponse()
{
    if (this->ifs.is_open())
        this->ifs.close();
}

std::string HttpResponse::toString()
{
    std::string response{""};
    response += (this->version + SP);
    response += (std::to_string(this->status_code) + SP);
    response += (HttpResponse::toReasonPhrase(this->status_code) + CRLF);
    response += ("Date: " + HttpResponse::currentDateTime() + CRLF);
    // TODO: continue
    return response;
}

std::string HttpResponse::toContentType(std::string name)
{
    int pos = name.find_last_of(".");
    if (pos == std::string::npos || pos + 1 >= name.length())
    {
        return "text/directory";
    }

    std::string extension = name.substr(pos + 1);
    std::map<std::string, std::string>::const_iterator it = HttpResponse::CONTENT_TYPES.find(extension);
    if (it != HttpResponse::CONTENT_TYPES.cend())
    {
        return it->second;
    }

    return "Error: Unknown content type";
}

std::string HttpResponse::toReasonPhrase(int status_code)
{
    std::map<int, std::string>::const_iterator it = HttpResponse::REASON_PHRASES.find(status_code);
    if (it != HttpResponse::REASON_PHRASES.cend())
    {
        return it->second;
    }
    return "Error: Unknown status code";
}

std::string HttpResponse::currentDateTime()
{
    time_t localTime;
    time(&localTime);
    tm *gmtTime = gmtime(&localTime);
    char buf[80];
    strftime(buf, 80, "%a, %d %b %Y %X GMT", gmtTime);
    return std::string{buf};
}

int HttpRequest::status()
{
    if (this->method == HttpMethod::UNDEFINED)
        return 405;

    if (!startsWith(this->url, "/"))
        return 404;

    if (!startsWith(this->version, "HTTP/"))
        return 505;

    return 0; // good request
}

bool HttpRequest::sendResponse(int conn_fd)
{
    HttpResponse *response = new HttpResponse(this);
    std::string msg = response->toString();

    int result = write(conn_fd, msg.c_str(), msg.length());

    if (result > 0)
        return true;

    return false;
}

std::string HttpRequest::toString()
{
    std::string value{""};
    value += "HttpRequest {";
    value += ("\n\tstatus: " + this->status());
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
    while (endsWith(request->url, "/"))
    {
        request->url.erase(request->url.length() - 1);
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

const std::map<std::string, std::string> HttpResponse::CONTENT_TYPES = {
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

const std::map<int, std::string> HttpResponse::REASON_PHRASES = {
    {100, "Continue"},
    {101, "Switching Protocols"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {300, "Multiple Choices"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {307, "Temporary Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Time-out"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Request Entity Too Large"},
    {414, "Request-URI Too Large"},
    {415, "Unsupported Media Type"},
    {416, "Requested range not satisfiable"},
    {417, "Expectation Failed"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Time-out"},
    {505, "HTTP Version not supported"},
};
