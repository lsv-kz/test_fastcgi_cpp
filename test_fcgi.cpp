#include <cstdio>
#include <cstring>
#include <errno.h>

#include <mutex>
#include <thread>
#include <condition_variable>

#include <unistd.h>
#include <sys/socket.h>

#include "fcgi_server.h"

int create_server_socket(const char *host, int port);
//======================================================================
using namespace std;

const int fcgi_port = 9002;
const char *fcgi_ip = "127.0.0.1";

const int max_thr = 5;
//======================================================================
std::mutex mtx_thr;
std::condition_variable cond_exit_thr;
int count_thr = 0;
//----------------------------------------------------------------------
int start_thr(void)
{
unique_lock<mutex> lk(mtx_thr);
    ++count_thr;
    while (count_thr >= max_thr)
    {
        cond_exit_thr.wait(lk);
    }
    return count_thr;
}
//----------------------------------------------------------------------
void close_thr(void)
{
mtx_thr.lock();
    --count_thr;
mtx_thr.unlock();
    cond_exit_thr.notify_one();
}
//======================================================================
String get_time()
{
    struct tm t;
    char s[40];
    time_t now = time(NULL);

    gmtime_r(&now, &t);

    strftime(s, sizeof(s), "%a, %d %b %Y %H:%M:%S GMT", &t);
    return s;
}
//======================================================================
void send_html(FCGI_server & Fcgi, const char *s)
{
    Fcgi << "<!DOCTYPE HTML>\n"
            "<html>\n"
            " <head>\n"
            "  <meta charset=\"UTF-8\">\n"
            "  <title>File Upload</title>\n"
            "  <style>\n"
            "    body {\n"
            "     margin-left:100px; margin-right:50px;\n"
            "     background-color: rgb(182,145,66);\n"
            "    }\n"
            "  </style>\n"
            " </head>\n"
            " <body>\n"
            "  <h3>" << s << "</h3>\n"
            "  <hr>\n"
            "  " << get_time().str() << "\n"
            " </body>\n"
            "</html>";
}
//======================================================================
int fcgi_out(int count_conn, FCGI_server &Fcgi)
{
    //************************* FCGI_STDIN *****************************
    const int size_buf = 4095;
    char buf[size_buf + 1] = "-";
    
    for ( ; ; )
    {
        int ret = Fcgi.read_from_client(buf, size_buf);
        if (ret < 0)
        {
            fprintf(stderr, "<%s:%d> Error read from fcgi_client\n", __func__, __LINE__);
            Fcgi << "Content-Type: text/html; charset=utf-8\r\n\r\n";
            send_html(Fcgi, "Error read from fcgi_client");
            return 0;
        }
        else if (ret == 0)
        {
            break;
        }
        
        buf[ret] = 0;
    }
    //******************************************************************
    Fcgi << "Content-Type: text/plain; charset=utf-8\r\n\r\n";
    Fcgi << "count = " << count_conn << "\n";

    Fcgi << buf << "\n\n";
    
    for ( int i = 0, n = Fcgi.len_param(); i < n; ++i)
    {
        Fcgi << Fcgi.param(i) << "\n";
        if (Fcgi.error())
        {
            printf("<%s:%d> Error\n", __func__, __LINE__);
            return -1;
        }
    }

    return 0;
}
//======================================================================
void response(int fcgi_sock, int count_conn)
{
    FCGI_server Fcgi(fcgi_sock);
    
    if (Fcgi.error())
    {
        printf("<%s:%d>  Error crteate object Fcgi\n", __func__, __LINE__);
        return;
    }
    
    fcgi_out(count_conn, Fcgi);
    Fcgi << "";
}

//======================================================================
void response_(int sock, int count_conn)
{
    response(sock, count_conn);
    shutdown(sock, SHUT_RDWR);
    close(sock);
    close_thr();
}
//======================================================================
int main(int argc, char *argv[])
{
    unsigned int count_conn = 0;
    printf(" ------ %s:%d, pid: %d ------\n", fcgi_ip, fcgi_port, getpid());
    int fcgi_sock = create_server_socket(fcgi_ip, fcgi_port);
    for ( ; ; )
    {
//      printf(" ---------------- wait connect %d ----------------\n", count_conn);
        int clientSock = accept(fcgi_sock, NULL, NULL);
        if (clientSock == -1)
        {
            if (errno == EINTR)
            {
                printf("<%s:%d>  Error accept(): EINTR\n", __func__, __LINE__);
                continue;
            }
            else if (errno == EMFILE)
            {
                printf("<%s:%d>  Error accept(): EMFILE\n", __func__, __LINE__);
                continue;
            }
            else if (errno == EAGAIN)
            {
                printf("<%s:%d>  Error accept(): EAGAIN\n", __func__, __LINE__);
                continue;
            }
            else
            {
                printf("<%s:%d>  Error accept(): %s\n", __func__, __LINE__, strerror(errno));
                break;
            }
        }
        
        ++count_conn;

        thread thr;
        try
        {
            thr = thread(response_, clientSock, count_conn);
            thr.detach();
        }
        catch (...)
        {
            printf("<%s:%d> Error create thread: %s\n", __func__, __LINE__, strerror(errno));
            exit(1);
        }
        
        start_thr();
    }
    
    return 0;
}
