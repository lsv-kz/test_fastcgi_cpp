#ifndef FCGI_SERVER_
#define FCGI_SERVER_

#include <cstdio>
#include <poll.h>

#include "String.h"
#include "Array.h"

#define FCGI_VERSION_1           1
#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)


const int TimeoutCGI = 10;
const int SIZE_BUF_OUT = 4096;

typedef struct {
    unsigned char type;
    int len;
    int paddingLen;
} fcgi_header;

class FCGI_server
{
    int err = 0;
    char buf[SIZE_BUF_OUT];
    const char *str_zero = "\0\0\0\0\0\0\0\0";
    int offset = 8, all_send = 0;
    int fcgi_sock;
    
    void fcgi_set_header(char *p, int type, int len)
    {
        if (err) return;
        unsigned char padding = 8 - (len % 8);
        padding = (padding == 8) ? 0 : padding;

        *p++ = FCGI_VERSION_1;
        *p++ = (unsigned char)type;
        *p++ = (unsigned char) ((1 >> 8) & 0xff);
        *p++ = (unsigned char) ((1) & 0xff);
    
        *p++ = (unsigned char) ((len >> 8) & 0xff);
        *p++ = (unsigned char) ((len) & 0xff);
    
        *p++ = padding;
        *p = 0;
        offset += padding;
        if (padding) memcpy(buf + 8 + len, str_zero, padding);
    }
    
    void fcgi_send()
    {
        int write_bytes = 0, ret = 0;
        struct pollfd fdwr;
        char *p = buf;
        
        fdwr.fd = fcgi_sock;
		fdwr.events = POLLOUT;

        while (offset > 0)
		{
			ret = poll(&fdwr, 1, TimeoutCGI * 1000);
			if (ret == -1)
			{
				if (errno == EINTR)
					continue;
				break;
			}
			else if (!ret)
			{
				ret = -408;
				break;
			}
			
			if (fdwr.revents != POLLOUT)
			{
				ret = -1;
				break;
			}
			
			ret = write(fcgi_sock, p, offset);
			if (ret == -1)
			{
				if ((errno == EINTR) || (errno == EAGAIN))
					continue;
				break;
			}
			
			write_bytes += ret;
			offset -= ret;
			p += ret;
		}
		
        if (ret <= 0)
            err = 1;
        else
            all_send += write_bytes;
        offset = 8;
    }
    
    int fcgi_read(char *b, int len)
    {
		int read_bytes = 0, ret;
		struct pollfd fdrd;
		char *p;
		
		fdrd.fd = fcgi_sock;
		fdrd.events = POLLIN;
		p = b;
		
		while (len > 0)
		{
			ret = poll(&fdrd, 1, TimeoutCGI * 1000);
			if (ret == -1)
			{
				if (errno == EINTR)
					continue;
				return -1;
			}
			else if (!ret)
			{
				return -1;
			}
			if (fdrd.revents & POLLIN)
			{
				ret = read(fcgi_sock, p, len);
				if (ret == -1)
				{
					return -1;
				}
				else if (ret == 0)
				{
					return -1;
				}
				else
				{
					p += ret;
					len -= ret;
					read_bytes += ret;
				}
			}
			else
			{
				return -1;
			}
		}
		return read_bytes;
	}
    
    int fcgi_read_header(fcgi_header *header)
    {
        int n;
        char buf[8];
    
        n = fcgi_read(buf, 8);
        if (n <= 0)
            return n;

        header->type = (unsigned char)buf[1];
        header->paddingLen = (unsigned char)buf[6];
        header->len = ((unsigned char)buf[4]<<8) | (unsigned char)buf[5];
    
        return n;
    }
    
    void fcgi_get_begin()
	{
		fcgi_header header;
		int ret = fcgi_read_header(&header);
		if (ret != 8)
		{
			err = 1;
			return;
		}
    
		if (header.len && ((header.len + header.paddingLen) < 16))
		{
			char buf[16];
			ret = fcgi_read(buf, header.len + header.paddingLen);
			if (ret != (header.len + header.paddingLen))
				err = 1;
		}
		else
			err = 1;
	}
    
    FCGI_server() {}
public:
    FCGI_server(int s)
    {
        fcgi_sock = s;
        offset = 8;
        err = 0;
        fcgi_get_begin();
    }
    
    FCGI_server & operator << (const char *s)
    {
        if (err) return *this;
        if (!s)
        {
            err = 1;
            return *this;
        }
        
        int n = 0, len = strlen(s);
        if (len == 0)
        {
            fcgi_set_header(buf, FCGI_STDOUT, offset - 8);
            if (SIZE_BUF_OUT < (offset + 24))
            {
                fcgi_send();
                fcgi_set_header(buf, FCGI_STDOUT, 0);
                fcgi_set_header(buf + offset, FCGI_END_REQUEST, 8);
                offset += 8;
                memcpy(buf + offset, str_zero, 8);
                offset += 8;
            }
            else
            {
                if (offset > 8)
                {
                    fcgi_set_header(buf + offset, FCGI_STDOUT, 0);
                    offset += 8;
                }
                fcgi_set_header(buf + offset, FCGI_END_REQUEST, 8);
                offset += 8;
                memcpy(buf + offset, str_zero, 8);
                offset += 8;
            }
            
            fcgi_send();
            return *this;
        }
        
        while (SIZE_BUF_OUT < (offset + len))
        {
            int l = SIZE_BUF_OUT - offset;
            memcpy(buf + offset, s + n, l);
            offset += l;
            len -= l;
            n += l;
            fcgi_set_header(buf, FCGI_STDOUT, offset - 8);
            fcgi_send();
            if (err)
            {
                return *this;
            }
        }
        
        memcpy(buf + offset, s + n, len);
        offset += len;
        if (SIZE_BUF_OUT == (offset + 6))
        {
            fcgi_set_header(buf, FCGI_STDOUT, offset - 8);
            fcgi_send();
        }
        return *this;
    }
    
    FCGI_server & operator << (const long long ll)
    {
        if (err) return *this;
        char buf[21];
        snprintf(buf, sizeof(buf), "%lld", ll);
        *this << buf;
        return *this;
    }
    
    int fcgi_get_param(Array <String> &Param);
    
    int error() const { return err; }
    int send_bytes() { return all_send; }
};
//----------------------------------------------------------------------
int FCGI_server::fcgi_get_param(Array <String> &Param)
{
    fcgi_header header;
    int num_par = 0;
    int ret = fcgi_read_header(&header);
    if (ret != 8)
    {
        err = 1;
        return -1;
    }
    
    if (header.type != FCGI_PARAMS)
    {
        err = 1;
        return -1;
    }

    const int size = 1024;
    char buf[size], *p = buf;
    int rd;
    int n = 0;
    String s;
    while (header.len > 0)
    {
        for ( int i = 0; (i < n) && (p > buf); i++)
            buf[i] = *(p + i);

        if (header.len < (size - n))
            rd = header.len;
        else
            rd = (size - n);
        p = buf;
        int n_ = fcgi_read(p + n, rd);
        if ((n_ != rd) || (n_ == 0))
        {
            err = 1;
            return ret;
        }

        n += n_;
        header.len -= n_;
        while (((n > 8) || !header.len) && (n > 0))
        {
            s = "";
            int len_par, len_val, len;
            if ((unsigned char)*p < 128)
            {
                len_par = (unsigned char)(*p++);
                len = 1;
            }
            else
            {
                len_par = ((unsigned char)(*p++) & 0x7f) << 24;
                len_par += ((unsigned char)(*p++) << 16);
                len_par += ((unsigned char)(*p++) << 8);
                len_par += (unsigned char)(*p++);
                len = 4;
            }

            if ((unsigned char)*p < 128)
            {
                len_val = (unsigned char)(*p++);
                len++;
            }
            else
            {
                len_val = ((unsigned char)(*p++) & 0x7f) << 24;
                len_val += ((unsigned char)(*p++) << 16);
                len_val += ((unsigned char)(*p++) << 8);
                len_val += (unsigned char)(*p++);
                len += 4; 
            }
            n -= len;

            while (len_par > n)
            {
                s.append(p, n);
                len_par -= n;
                
                p = buf;
                if (header.len < size)
                    rd = header.len;
                else
                    rd = size;
                n = fcgi_read(buf, rd);
                if ((n != rd) || (n == 0))
                {
                    err = 1;
                    return ret;
                }

                header.len -= n;
            }

            s.append(p, len_par);
            s << "=";
            n -= len_par;
            p += len_par;
            
            while (len_val > n)
            {
                s.append(p, n);
                len_val -= n;
                
                p = buf;
                if (header.len < size)
                    rd = header.len;
                else
                    rd = size;
                n = fcgi_read(buf, rd);
                if ((n != rd) || (n == 0))
                {
					err = 1;
                    return ret;
                }

                header.len -= n;
            }
            
            s.append(p, len_val);
            Param << s;
            n -= len_val;
            p += len_val;
            num_par++;
        }
    }
    
    if (header.paddingLen)
    {
        n = fcgi_read(buf, header.paddingLen);
        if (n <= 0)
        {
			err = 1;
			return -1;
		}
    }
    return num_par;
}

#endif