class HTTPConnection
{
private:
    SOCKET socket;
    std::string response;

public:
    HTTPConnection(SOCKET socket);
};