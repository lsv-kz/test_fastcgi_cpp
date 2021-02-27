#include <cstdio>
#include <cstring>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
//#include <netinet/in.h>
//#include <netdb.h>

//======================================================================
int create_server_socket(const char *host, int port)
{
    int sockfd, sock_opt = 1;
    struct sockaddr_in server_sockaddr;
    
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sockfd == -1)
    {
        fprintf(stderr, "   Error socket(): %s\n", strerror(errno));
        return -1;
    }
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt, sizeof(sock_opt)))
    {
        perror("setsockopt (SO_REUSEADDR)");
        close(sockfd);
        return -1;
    }

    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&sock_opt, sizeof(sock_opt)))
    {
        printf("<%s:%d> setsockopt: unable to set TCP_NODELAY: %s\n", __func__, __LINE__, strerror(errno));
        close(sockfd);
        return -1;
    }  
//----------------------------------------------------------------------
    memset(&server_sockaddr, 0, sizeof server_sockaddr);
    server_sockaddr.sin_family = PF_INET;
    server_sockaddr.sin_port = htons(port);

/*  if (inet_pton(PF_INET, addr, &(server_sockaddr.sin_addr)) < 1)
    {
        print_err("   Error inet_pton(%s): %s\n", addr, strerror(errno));
        close(sockfd);
        return -1;
    }*/
    server_sockaddr.sin_addr.s_addr = inet_addr(host);
//  server_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd, (struct sockaddr *) &server_sockaddr, sizeof (server_sockaddr)) == -1)
    {
        int err = errno;
        printf("<%s:%d> Error bind(): %s\n", __func__, __LINE__, strerror(err));
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 128) == -1)
    {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    return sockfd;
}
