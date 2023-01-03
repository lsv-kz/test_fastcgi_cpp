#ifndef FCGI_SERVER_
#define FCGI_SERVER_

#include <cstdio>
#include <poll.h>

#include <string>
#include <vector>

#define FCGI_RESPONDER  1

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
#define FCGI_MAXTYPE            (FCGI_UNKNOWN_TYPE)
#define requestId               1

const int FCGI_SIZE_BUF = 8192;// FCGI_SIZE_BUF > 15

//======================================================================
typedef struct {
    unsigned char type;
    int len;
    int paddingLen;
} fcgi_header;
//======================================================================
class FCGI_server
{
    int err = 0;
    char fcgi_buf[FCGI_SIZE_BUF];
    const char *str_zero = "\0\0\0\0\0\0\0\0";
    int offset_out = 8, all_send = 0;
    int fcgi_sock;
    int TimeoutCGI;

    fcgi_header header = {0, 0, 0};

    std::vector <std::string> Param;
    //------------------------------------------------------------------
    void fcgi_set_header(char *p, int type, int len)
    {
        if (err)
            return;

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

        if (padding)
        {
            memcpy(fcgi_buf + 8 + len, str_zero, padding);
            offset_out += padding;
        }
    }
    //------------------------------------------------------------------
    void fcgi_send()
    {
        if (err)
            return;

        int write_bytes = 0, ret = 0;
        struct pollfd fdwr;
        char *p = fcgi_buf;

        fdwr.fd = fcgi_sock;
        fdwr.events = POLLOUT;

        while (offset_out > 0)
        {
            ret = poll(&fdwr, 1, TimeoutCGI * 1000);
            if (ret == -1)
            {
                if (errno == EINTR)
                    continue;
                err = errno;
                break;
            }
            else if (!ret)
            {
                err = ETIMEDOUT;
                break;
            }

            if (fdwr.revents != POLLOUT)
            {
                err = ECONNRESET;
                ret = -1;
                break;
            }

            ret = write(fcgi_sock, p, offset_out);
            if (ret == -1)
            {
                if ((errno == EINTR) || (errno == EAGAIN))
                    continue;
                err = errno;
                break;
            }

            write_bytes += ret;
            offset_out -= ret;
            p += ret;
        }

        if (ret > 0)
            all_send += write_bytes;
        offset_out = 8;
    }
    //------------------------------------------------------------------
    void fcgi_get_begin()
    {
        if (err)
            return;

        fcgi_header header;
        int ret = fcgi_read_header(&header);
        if (ret != 8)
        {
            err = 1;
            return;
        }

        if (header.type != FCGI_BEGIN_REQUEST)
        {
            err = 1;
            return;
        }

        if (header.len == 8)
        {
            char buf[8];
            ret = fcgi_read(buf, 8);
            if (ret != 8)
                err = 1;
        }
        else
            err = 1;
    }
    //------------------------------------------------------------------
    void get_par()
    {
        if (err)
            return;

        int ret = 0;
        fcgi_header hd;
        Param.reserve(20);
        for ( ; ; )
        {
            if ((ret = fcgi_read_header(&hd)) <= 0)
            {
                err = 1;
                break;
            }

            if (hd.len == 0)
                break;

            if (hd.type != FCGI_PARAMS)
            {
                err = 1;
                break;
            }

            ret = fcgi_get_param(hd);
            if (ret < 0)
            {
                err = 1;
                break;
            }
        }
    }
    //------------------------------------------------------------------
    int fcgi_read_header(fcgi_header *header);
    int fcgi_read(char *buf, int len);
    //------------------------------------------------------------------
    FCGI_server() {}
public://===============================================================
    FCGI_server(int sock, int timeout = 10)
    {
        fcgi_sock = sock;
        TimeoutCGI = timeout;
        offset_out = 8;
        err = 0;
        fcgi_get_begin();
        get_par();
    }
    //------------------------------------------------------------------
    FCGI_server & operator << (const char *s) // *** FCGI_STDOUT ***
    {
        if (err)
            return *this;

        if (!s)
        {
            err = EINVAL;
            return *this;
        }

        int n = 0, len = strlen(s);
        int alignment = FCGI_SIZE_BUF % 8;

        if (len == 0)
        {
            fcgi_set_header(fcgi_buf, FCGI_STDOUT, offset_out - 8);
            fcgi_send();

            fcgi_set_header(fcgi_buf, FCGI_STDOUT, 0);
            fcgi_send();

            fcgi_set_header(fcgi_buf, FCGI_END_REQUEST, 8);
            memcpy(fcgi_buf + offset_out, str_zero, 8);
            offset_out += 8;
            fcgi_send();
            return *this;
        }

        while (FCGI_SIZE_BUF <= (offset_out + len + alignment))  //
        {
            int l = FCGI_SIZE_BUF - (offset_out + alignment);    //
            if (l <= 0)
            {
                fprintf(stderr, "<%s:%d> Error: (len = %d) <= 0\n", __func__, __LINE__, l);
                err = 1;
                return *this;
            }

            memcpy(fcgi_buf + offset_out, s + n, l);
            offset_out += l;
            len -= l;
            n += l;
            fcgi_set_header(fcgi_buf, FCGI_STDOUT, offset_out - 8);
            fcgi_send();
            if (err)
            {
                return *this;
            }
        }

        memcpy(fcgi_buf + offset_out, s + n, len);
        offset_out += len;
        fcgi_buf[offset_out] = 0;
        return *this;
    }
    //------------------------------------------------------------------
    FCGI_server & operator << (const long long ll) // *** FCGI_STDOUT ***
    {
        if (err)
            return *this;

        char buf[21];
        snprintf(buf, sizeof(buf), "%lld", ll);
        *this << buf;
        return *this;
    }
    //------------------------------------------------------------------
    int fcgi_out_buf(const char * buf, int len) // *** FCGI_STDOUT ***
    {
        if (err)
            return err;

        if (!buf)
        {
            err = EINVAL;
            return err;
        }

        int n = 0;
        int alignment = FCGI_SIZE_BUF % 8;

        while (FCGI_SIZE_BUF <= (offset_out + len + alignment))  //
        {
            int l = FCGI_SIZE_BUF - (offset_out + alignment);    //
            if (l <= 0)
            {
                fprintf(stderr, "<%s:%d> Error: (len = %d) <= 0\n", __func__, __LINE__, l);
                err = 1;
                return err;
            }

            memcpy(fcgi_buf + offset_out, buf + n, l);
            offset_out += l;
            len -= l;
            n += l;
            fcgi_set_header(fcgi_buf, FCGI_STDOUT, offset_out - 8);
            fcgi_send();
            if (err)
            {
                return err;
            }
        }

        memcpy(fcgi_buf + offset_out, buf + n, len);
        offset_out += len;
        fcgi_buf[offset_out] = 0;
        return 0;
    }
    //------------------------------------------------------------------
    int error() const { return err; }
    int send_bytes() { return all_send; }
    int len_param() { return  Param.size(); }
    const char *param(int n)
    {
        if (n >= (int)Param.size())
        {
            err = EINVAL;
            return "";
        }
        return Param[n].c_str();
    }

    int fcgi_get_param(fcgi_header & header);
    int read_from_fcgi_client(char *buf, int size);
};
//======================================================================
    int FCGI_server::fcgi_read_header(fcgi_header *header)
    {
        if (err)
            return -1;
        int n;
        char buf[8];

        n = fcgi_read(buf, 8);
        if (n < 8)
            return -1;

        header->type = (unsigned char)buf[1];
        header->paddingLen = (unsigned char)buf[6];
        header->len = ((unsigned char)buf[4]<<8) | (unsigned char)buf[5];

        return n;
    }
//======================================================================
    int FCGI_server::fcgi_read(char *buf, int len)
    {
        if (err)
            return -1;
        int read_bytes = 0, ret;
        struct pollfd fdrd;
        char *p;

        fdrd.fd = fcgi_sock;
        fdrd.events = POLLIN;
        p = buf;

        while (len > 0)
        {
            ret = poll(&fdrd, 1, TimeoutCGI * 1000);
            if (ret == -1)
            {
                if (errno == EINTR)
                    continue;
                err = errno;
                return -1;
            }
            else if (!ret)
            {
                err = ETIMEDOUT;
                return -1;
            }

            if (fdrd.revents & POLLIN)
            {
                ret = read(fcgi_sock, p, len);
                if (ret < 0)
                {
                    err = errno;
                    return -1;
                }
                else if (ret == 0)
                {
                    err = ECONNRESET;
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
                err = ECONNRESET;
                return -1;
            }
        }
        return read_bytes;
    }
//======================================================================
    int FCGI_server::fcgi_get_param(fcgi_header & header)
    {
        if (err)
            return -1;

        int num_par = 0;
        if (header.type != FCGI_PARAMS)
        {
            err = 1;
            return -1;
        }

        const int size = FCGI_SIZE_BUF;
        char *p = fcgi_buf;
        int rd;
        int n = 0;
        std::string s;
        while (header.len > 0)
        {
            for ( int i = 0; (i < n) && (p > fcgi_buf); i++)
                fcgi_buf[i] = *(p + i);

            if (header.len < (size - n))
                rd = header.len;
            else
                rd = (size - n);
            p = fcgi_buf;
            int ret = fcgi_read(p + n, rd);
            if ((ret != rd) || (ret == 0))
            {
                err = 1;
                return -1;
            }

            n += ret;
            header.len -= ret;
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

                    p = fcgi_buf;
                    if (header.len < size)
                        rd = header.len;
                    else
                        rd = size;
                    n = fcgi_read(fcgi_buf, rd);
                    if ((n != rd) || (n == 0))
                    {
                        err = 1;
                        return -1;
                    }

                    header.len -= n;
                }

                s.append(p, len_par);
                s += "=";
                n -= len_par;
                p += len_par;

                while (len_val > n)
                {
                    s.append(p, n);
                    len_val -= n;

                    p = fcgi_buf;
                    if (header.len < size)
                        rd = header.len;
                    else
                        rd = size;
                    n = fcgi_read(fcgi_buf, rd);
                    if ((n != rd) || (n == 0))
                    {
                        err = 1;
                        return -1;
                    }

                    header.len -= n;
                }

                s.append(p, len_val);
                Param.push_back(s);
                n -= len_val;
                p += len_val;
                num_par++;
            }
        }

        if (header.paddingLen)
        {
            n = fcgi_read(fcgi_buf, header.paddingLen);
            if (n <= 0)
            {
                err = 1;
                return -1;
            }
        }
        return num_par;
    }
//======================================================================
    int FCGI_server::read_from_fcgi_client(char *buf, int size)// *** FCGI_STDIN ***
    {
        char padd[256];
        if (err)
            return -1;

        if (header.len == 0)
        {
            if (header.paddingLen > 0)
            {
                if (fcgi_read(padd, header.paddingLen) < 0)
                {
                    err = 1;
                    return -1;
                }
            }

            int n = fcgi_read_header(&header);
            if (n <= 0)
            {
                err = 1;
                return -1;
            }
        }

        if (header.type != FCGI_STDIN)
        {
            fprintf(stderr, "<%s:%d>  Error type != FCGI_STDIN (%d)\n", __func__, __LINE__, header.type);
            return -1;
        }

        if (header.len == 0)
            return 0;

        int rd = (header.len <= size) ? header.len : size;
        int n = fcgi_read(buf, rd);
        if (n <= 0)
        {
            fprintf(stderr, "! Error: fcgi_read FCGI_STDOUT\n");
            return -1;
        }

        header.len -= n;

        return n;
    }

#endif
