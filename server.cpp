#include <iostream>
#include <fstream>
#include <map>
#include <thread>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
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
    int status() const;
    bool sendResponse(int conn_fd);
    std::string toString() const;
    static HttpRequest *parse(std::string msg);
    static HttpMethod toMethod(std::string method);
};

class HttpResponse
{
public:
    HttpResponse() : version(""), status_code(503), content_type("") {}
    HttpResponse(HttpRequest *request);
    ~HttpResponse();
    std::string version;
    int status_code;
    std::string content_type;
    std::string content;
    int contentLength() const;
    std::string toString(bool debug = false);
    static const std::map<int, std::string> REASON_PHRASES;
    static std::string toReasonPhrase(int status_code);
    static std::string toMessage(int status_code);
    static const std::map<std::string, std::string> CONTENT_TYPES;
    static std::string toContentType(std::string name);
    static std::string currentDateTime();
    static std::string htmlTemplateOf(int status_code);
    static std::string htmlTemplateOf(std::string directory_path);

private:
    std::ifstream ifs;
};

bool startsWith(std::string base, std::string compare);
bool endsWith(std::string base, std::string compare);
bool replaceAll(std::string &base, std::string old_value, std::string new_value);
bool exists(std::string path);
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
    bool result = request->sendResponse(conn_fd);

    if (!result)
    {
        std::cerr << "Error sending HTTP response!" << std::endl;
    }

    close(conn_fd);
    delete request;
    request = nullptr;
}

// Generate HttpRequest object with request message
HttpRequest *parse_request(int conn_fd)
{
    int buffer_size = 0;
    char buf[MAXLINE] = {0};
    std::string msg{""};
    HttpRequest *request = nullptr;

    // receive request message
    do
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
    } while (buffer_size > MAXLINE);

    // parse request message
    request = HttpRequest::parse(msg);
    int status = request->status();
    if (status >= 400)
    {
        std::cerr << "Error parsing HTTP request:\n"
                  << msg << std::endl;
    }

    return request;
}

// Check if base string starts with compare string
bool startsWith(std::string base, std::string compare)
{
    if (compare.length() <= 0 || base.length() < compare.length())
    {
        return false;
    }
    return base.compare(0, compare.length(), compare) == 0;
}

// Check if base string ends with compare string
bool endsWith(std::string base, std::string compare)
{
    if (compare.length() <= 0 || base.length() < compare.length())
    {
        return false;
    }
    return base.compare(base.length() - compare.length(), std::string::npos, compare) == 0;
}

// Replace substring in base string with specified string
bool replaceAll(std::string &base, std::string old_value, std::string new_value)
{
    int pos = base.find(old_value);

    if (pos == std::string::npos)
    {
        return false;
    }

    do
    {
        base.replace(pos, old_value.length(), new_value);
        pos = base.find(old_value);
    } while (pos != std::string::npos);

    return true;
}

// Check if a path exists in the filesystem
bool exists(std::string path)
{
    struct stat info;
    if (stat(path.c_str(), &info) != 0)
    {
        return false;
    }

    // true for both file or directory
    return true;
}

HttpResponse::HttpResponse(HttpRequest *request)
    : version(request->version),
      status_code(500),
      content_type(""),
      content("")
{
    // check for bad request
    int status = request->status();
    if (status >= 400)
    {
        this->status_code = status;
        return;
    }

    // parse the requested object
    int pos = request->url.find_last_of("/");
    if (pos == std::string::npos || pos + 1 >= request->url.length())
    {
        this->status_code = 400;
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

    // directory requested
    if (endsWith(contentType, "directory"))
    {
        // this->status_code = 403;
        // std::cerr << "Missing file extension with name " << name << std::endl;

        if (!exists("." + request->url))
        {
            this->status_code = 404;
            std::cerr << "Reading directory failed with path " << request->url << std::endl;
            return;
        }

        if (!exists("." + request->url + "/index.html"))
        {
            // directory listing
            this->status_code = 200;
            this->content_type = "text/html";
            this->content = HttpResponse::htmlTemplateOf("." + request->url);
            return;
        }

        // redirect to index.html inside the requested directory
        request->url += "/index.html";
        contentType = "text/html";
    }

    this->content_type = contentType;

    // read the requested file
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

    // store the file content
    this->content = std::string{(std::istreambuf_iterator<char>(this->ifs)), std::istreambuf_iterator<char>()};

    this->ifs.close();

    if (this->contentLength() < 0)
    {
        this->status_code = 404;
        std::cerr << "Reading file size failed with path " << request->url << std::endl;
        return;
    }

    // read file successful
    this->status_code = 200;
}

HttpResponse::~HttpResponse()
{
    if (this->ifs.is_open())
        this->ifs.close();
}

// Get the length of the stored content
int HttpResponse::contentLength() const
{
    return this->content.length();
}

// Generate the response message or debug message
std::string HttpResponse::toString(bool debug)
{
    // construct the debug message
    if (debug)
    {
        std::string value{""};
        value += "HttpResponse {";
        value += ("\n\tversion: " + this->version);
        value += ("\n\tstatus_code: " + std::to_string(this->status_code));
        value += ("\n\tcontent_type: " + this->content_type);
        value += ("\n\tcontent_length: " + std::to_string(this->contentLength()));
        value += ("\n\tis_file_read: ");
        value += (this->ifs.is_open() && this->ifs.good()) ? "true" : "false";
        value += "\n}\n";
        return value;
    }

    // construct the response message
    std::string response{""};
    response += (this->version + SP);
    response += (std::to_string(this->status_code) + SP);
    response += (HttpResponse::toReasonPhrase(this->status_code) + CRLF);

    response += ("Date: " + HttpResponse::currentDateTime() + CRLF);

    std::string contentType{"text/html"};

    if (this->status_code >= 200 && this->status_code < 400 &&
        this->content_type.length() > 0 && !startsWith(this->content_type, "Error"))
    {
        contentType = this->content_type;
    }

    response += ("Content-Type: " + contentType + CRLF);

    if (this->status_code < 200 || this->status_code >= 400)
    {
        std::string html = HttpResponse::htmlTemplateOf(this->status_code);
        response += ("Content-Length: " + std::to_string(html.length()) + CRLF + CRLF);
        response += html;
        return response;
    }

    response += ("Content-Length: " + std::to_string(this->contentLength()) + CRLF + CRLF);

    response += this->content;

    return response;
}

// Convert filename to content type based on extension
std::string HttpResponse::toContentType(std::string name)
{
    // filename is directory if no extension
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

// Convert status code to reason phrase
std::string HttpResponse::toReasonPhrase(int status_code)
{
    std::map<int, std::string>::const_iterator it = HttpResponse::REASON_PHRASES.find(status_code);
    if (it != HttpResponse::REASON_PHRASES.cend())
    {
        return it->second;
    }
    return "Error: Unknown status code";
}

// Convert status code to user-friendly message
std::string HttpResponse::toMessage(int status_code)
{
    std::string message{""};
    switch (status_code)
    {
    case 400:
        message += "Please check the request format.";
        break;

    case 403:
        message += "Directory listing is not allowed.";
        break;

    case 404:
        message += "The requested file or directory cannot be found.";
        break;

    case 405:
    case 501:
        message += "GET is currently the only supported method.";
        break;

    case 415:
        message += "The requested file format is currently not supported.";
        break;

    case 500:
        message += "The server is experiencing some unknown errors.";
        break;

    case 503:
        message += "The server is currently busy. Please try again later.";
        break;

    case 505:
        message += "The requested HTTP version is not supported. Please consider using HTTP/1.1.";
        break;

    default:
        message += "No message available.";
        break;
    }

    return message;
}

// Get http-date formatted string of the current datetime
std::string HttpResponse::currentDateTime()
{
    time_t localTime;
    time(&localTime);
    tm *gmtTime = gmtime(&localTime);
    char buf[80];
    // example: Wed, 19 Dec 2010 16:00:21 GMT
    strftime(buf, 80, "%a, %d %b %Y %X GMT", gmtTime);
    return std::string{buf};
}

// Generate html template based of status code
std::string HttpResponse::htmlTemplateOf(int status_code)
{
    std::string html{""};
    std::ifstream ifs{"./templates/error.html"};

    if (!ifs.is_open() || !ifs.good())
    {
        // basic error message
        html += ("<h1>" + std::to_string(status_code) + " " + HttpResponse::toReasonPhrase(status_code) + "</h1>");
        return html;
    }

    // read the error page template
    html = std::string{(std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()};
    ifs.close();

    // substitute placeholders with actual values
    bool result = true;
    result = result && replaceAll(html, "{%status_code%}", std::to_string(status_code));
    result = result && replaceAll(html, "{%reason_phrase%}", HttpResponse::toReasonPhrase(status_code));
    result = result && replaceAll(html, "{%message%}", HttpResponse::toMessage(status_code));

    if (!result)
    {
        std::cerr << "Substituting template error.html failed!\nContent:\n"
                  << html << std::endl;
    }

    return html;
}

// Generate html template for directory listing based on the path
std::string HttpResponse::htmlTemplateOf(std::string directory_path)
{
    std::string html{""};
    std::ifstream ifs{"./templates/dirlist.html"};

    if (!ifs.is_open() || !ifs.good())
    {
        html += ("<h1>Missing file template</h1>");
        return html;
    }

    // read the directory listing page template
    html = std::string{(std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()};
    ifs.close();

    std::string list{""};
    struct dirent **entList;
    int count = scandir(directory_path.c_str(), &entList, NULL, alphasort);

    if (count < 0)
    {
        return list;
    }

    // print directory before files
    for (int i = count - 1; i >= 0; --i)
    {
        std::string name{entList[i]->d_name};

        // append / character after directory name
        if (entList[i]->d_type == DT_DIR)
        {
            name += "/";
        }
        else
        {
            continue;
        }

        // ignore current directory and parent entry
        if (name == "./" || name == "../")
        {
            continue;
        }

        list += ("\n<li><a href=\"" + name + "\">" + name + "</a></li>");
    }

    // print files after directory
    for (int i = count - 1; i >= 0; --i)
    {
        std::string name{entList[i]->d_name};

        // append / character after directory name
        if (entList[i]->d_type == DT_DIR)
        {
            free(entList[i]);
            continue;
        }

        free(entList[i]);

        list += ("\n<li><a href=\"" + name + "\">" + name + "</a></li>");
    }

    free(entList);

    if (startsWith(directory_path, "."))
    {
        directory_path.erase(0, 1);
    }

    if (!endsWith(directory_path, "/"))
    {
        directory_path += "/";
    }

    bool result = true;
    result = result && replaceAll(html, "{%path%}", directory_path);
    result = result && replaceAll(html, "{%list%}", list);

    if (!result)
    {
        std::cerr << "Substituting template dirlist.html failed!\nContent:\n"
                  << html << std::endl;
    }

    return html;
}

// Check the status of the request
int HttpRequest::status() const
{
    if (this->method == HttpMethod::UNDEFINED)
        return 501;

    if (!startsWith(this->url, "/"))
        return 400;

    if (!startsWith(this->version, "HTTP/"))
        return 505;

    return 0; // good request
}

// Send a http response based on the request
bool HttpRequest::sendResponse(int conn_fd)
{
    HttpResponse *response = new HttpResponse(this);
    std::string msg = response->toString();

    // log the constructed response
    std::cout << response->toString(true) << std::endl;

    int result = send(conn_fd, msg.c_str(), msg.length(), 0);

    delete response;
    response = nullptr;

    if (result > 0)
        return true;

    return false;
}

// Generate the debug message
std::string HttpRequest::toString() const
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

// Create a HttpRequest object by parsing the request message
HttpRequest *HttpRequest::parse(std::string msg)
{
    HttpRequest *request = new HttpRequest();

    // parse the method
    int start_pos = 0;
    int end_pos = msg.find(SP, start_pos);
    if (start_pos >= msg.length() || end_pos == std::string::npos)
    {
        return request;
    }

    request->method = toMethod(msg.substr(start_pos, end_pos - start_pos));

    // parse the url
    start_pos = end_pos + 1;
    end_pos = msg.find(SP, start_pos);
    if (start_pos >= msg.length() || end_pos == std::string::npos)
    {
        return request;
    }

    request->url = msg.substr(start_pos, end_pos - start_pos);

    // redirect to index.html if root directory is requested
    if (request->url == "/")
    {
        request->url = "/index.html";
    }

    // strip the / characters at the end
    while (endsWith(request->url, "/"))
    {
        request->url.erase(request->url.length() - 1);
    }

    // parse the HTTP version
    start_pos = end_pos + 1;
    end_pos = msg.find(CRLF, start_pos);
    if (start_pos >= msg.length() || end_pos == std::string::npos)
    {
        return request;
    }

    request->version = msg.substr(start_pos, end_pos - start_pos);

    return request;
}

// Find the enum of the given HTTP method
HttpMethod HttpRequest::toMethod(std::string method)
{
    if (method == "GET")
        return HttpMethod::GET;

    // * ignore other methods for now
    return HttpMethod::UNDEFINED;
}

// Define the conversion map between file extensions and content types
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

// Define the conversion map between status codes and reason phrases
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
    {405, "Method Not Allowed"},
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
