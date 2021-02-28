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

const int fcgi_port = 9004;
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
    int d = count_thr;
    while (count_thr >= max_thr)
    {
        cond_exit_thr.wait(lk);
    }
    return d;//count_thr;
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
int fcgi_out(int count_conn, FCGI_server &Fcgi, Array <String> & Param)
{
    Fcgi << "Content-Type: text/plain; charset=utf-8\r\n\r\n";
    Fcgi << "count = " << count_conn << "\n";
    
    fcgi_header hd;
    for ( ; ; )
    {
		int ret = Fcgi.fcgi_read_header(&hd);
		if (ret <= 0)
		{
			printf("<%s:%d>  Error fcgi_read_header()\n", __func__, __LINE__);
			return -1;
		}
	//	printf("<%s:%d>  type=%d, len=%d, padd=%d\n", __func__, __LINE__, hd.type, hd.len, hd.paddingLen);
		if (hd.len == 0)
			break;
		
		if (hd.type != FCGI_STDIN)
		{
			printf("<%s:%d>  Error type != FCGI_STDIN\n", __func__, __LINE__);
			return -1;
		}
		
		const int size_buf = 255;
		char buf[size_buf + 1];
		while (hd.len > 0)
		{
			int rd = (hd.len > size_buf) ? size_buf : hd.len;
			ret = Fcgi.fcgi_read(buf, rd);
			if (ret <= 0)
			{
				printf("<%s:%d>  Error fcgi_read()\n", __func__, __LINE__);
				return -1;
			}
			
			buf[ret] = 0;
			Fcgi << buf;
			hd.len -= ret;
		}
		if ((hd.paddingLen > 0) && (hd.paddingLen < 8))
			Fcgi.fcgi_read(buf, hd.paddingLen);
	}
	
	Fcgi << "\n\n";
    
    for ( int i = 0, n = Param.len(); i < n; ++i)
    {
        Fcgi << Param.get(i)->str() << "\n";
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
    
    Array <String> Param(20);
    int ret = 0;
    fcgi_header hd;
    for ( ; ; )
    {
		if ((ret = Fcgi.fcgi_read_header(&hd)) <= 0)
			break;
//printf("<%s:%d>  type=%d, len=%d, padd=%d\n", __func__, __LINE__, hd.type, hd.len, hd.paddingLen);
		if (hd.len == 0)
			break;
		
		if (hd.type != FCGI_PARAMS)
		{
			ret = -1;
			break;
		}
        ret = Fcgi.fcgi_get_param(hd, Param);
        if (ret <= 0) break;
    }

    if (ret < 0)
    {
        printf("<%s:%d>  Error fcgi_get_param()\n", __func__, __LINE__);
        return;
    }
/*
    for ( int i = 0, n = Param.len(); i < n; ++i)
    {
        cout << "[" << Param.get(i)->str() << "]\n";
    }
*/
    fcgi_out(count_conn, Fcgi, Param);
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
    printf("<%s:%d> ------ %s:%d ------\n", __func__, __LINE__, fcgi_ip, fcgi_port);
    int fcgi_sock = create_server_socket(fcgi_ip, fcgi_port);
    for ( ; ; )
    {
//		printf(" ---------------- wait connect %d ----------------\n", count_conn);
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
        
       /* int ret = */start_thr();
  //      printf("<%s:%d> num thr: %d\n", __func__, __LINE__, ret);
    }
    
    return 0;
}
