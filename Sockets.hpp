#ifndef _SOCKETS_HPP
#define _SOCKETS_HPP

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "LockUtils.hpp"

int CreateListen(int port, const char* ip = NULL, bool re_use_addr = true, bool re_use_port = true)
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if(sockfd < 0) abort();

    int optval = re_use_addr ? 1 : 0;
    MCHECK(::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
               &optval, sizeof(optval)));

    optval = re_use_port ? 1 : 0;
    MCHECK(::setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT,
                         &optval, sizeof(optval)));

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_port = htons(port);

    if(ip)
    {
        local.sin_addr.s_addr = inet_addr(ip);
    }
    else
    {
        local.sin_addr.s_addr = inet_addr(INADDR_ANY);
    }
    

    MCHECK(::bind(sockfd,(struct sockaddr*)&local , sizeof(local)));
    MCHECK(::listen(sockfd, SOMAXCONN));
    return sockfd;
}

int ConnectServer(int port, const char* ip)
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(sockfd < 0) abort();

    struct sockaddr_in remote;
    remote.sin_family = AF_INET;
    remote.sin_port = htons(port);
    if(inet_pton(AF_INET, ip, &remote.sin_addr) != 1) abort();
    
    if(::connect(sockfd, (struct sockaddr*)&remote, sizeof(remote)) < 0)
    {
        return -1;
    }

    return sockfd;
}

#endif