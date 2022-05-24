#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

/* function prototypes */
void *thread(void *vargp);
int parse_uri(char *uri, char *hostname, char *pathname, int *port);
void ClientServerCommunication(int fd);
void build_requesthdrs(rio_t *rp, char *newreq, char *hostname, char *pathname, int *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);
    cache_init();

    /* local variables */
    char *port = argv[1]; // listening port
    pthread_t tid;
    struct sockaddr_in clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    int listenfd = Open_listenfd(port);
    char client_hostname[MAXLINE];
    char client_port[MAXLINE];

    while (1)
    {
        int *connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // for checking
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);

        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

// thread routine
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(Pthread_self());
    Free(vargp);
    ClientServerCommunication(connfd);
    Close(connfd);

    return NULL;
}

void ClientServerCommunication(int connfd)
{
    int clientfd, port;
    char buf[MAXBUF];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE];
    rio_t rio_client_proxy, rio_proxy_server;

    cnode_t *tmp = Malloc(sizeof(cnode_t));

    Rio_readinitb(&rio_client_proxy, connfd);
    if (!Rio_readlineb(&rio_client_proxy, buf, MAXBUF))
        return;
    sscanf(buf, "%s %s %s", method, uri, version); // Read request line
    if (strcasecmp(method, "GET"))
    { // Check if method exists
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy Server does not implement this method");
        return;
    }
    if (parse_uri(uri, hostname, pathname, &port) == -1)
    { // parsing uri
        clienterror(connfd, uri, "404", "Not Found",
                    "Proxy Server can't find requested page");
        return;
    }

    sprintf(buf, "GET %s HTTP/1.0\r\n", pathname);
    build_requesthdrs(&rio_client_proxy, buf, hostname, pathname, &port); // Read headers

    /* check if match */
    cnode_t *c;
    if ((c = match(hostname, port, pathname))) // if match success
    {
        Rio_writen(connfd, c->payload, strlen(c->payload)); // write to client
        printf("cache hit\n");
    return;
    }

    printf("Cache miss\n");
    /* request information -> cache */
    tmp->host = Malloc(strlen(hostname) + 1);
    strcpy(tmp->host, hostname);
    tmp->path = Malloc(strlen(pathname) + 1);
    strcpy(tmp->path, pathname);    tmp->port = port;

    /* proxy becomes client to requested uri */
    char port_str[10];
    sprintf(port_str, "%d", port);
    if ((clientfd = Open_clientfd(hostname, port_str)) < 0)
    {
        clienterror(connfd, uri, "500", "Server Error",
                    "Proxy Server can't connect to requested server host");
        return;
    }

    /* Enter Client & Server Session (proxy becomes client) */
    Rio_readinitb(&rio_proxy_server, clientfd); // connect proxy with server
    Rio_writen(clientfd, buf, strlen(buf));     // send request to server

    int n;
    char tmp_payload[MAX_OBJECT_SIZE]; // for cache
    while ((n = Rio_readlineb(&rio_proxy_server, buf, MAXBUF)))
    {
        Rio_writen(connfd, buf, n); // write to client
        strcat(tmp_payload, buf);
    }

    /* result information -> cache */
    tmp->payload = Malloc(strlen(tmp_payload) + 1);
    strcpy(tmp->payload, tmp_payload);
    tmp->size = strlen(tmp->host) + strlen(tmp->path) + strlen(tmp->payload);

    P(&mutex);                                             // block ( write to memory )
    while (cache_load + (int)(tmp->size) > MAX_CACHE_SIZE) //check size
    {
        dequeue();
    }
    enqueue(new (tmp->host, tmp->port, tmp->path, tmp->payload, tmp->size)); // save to queue
    V(&mutex);                                                               // block ( write to memory )

    Free(tmp->host);
    Free(tmp->path);
    Free(tmp->payload);
    Free(tmp);
    Free(c);
}

/* build headers */
void build_requesthdrs(rio_t *rp, char *newreq, char *hostname, char *pathname, int *port)
{
    char buf[MAXLINE];

    while (Rio_readlineb(rp, buf, MAXLINE) > 0)
    {
        if (!strcmp(buf, "\r\n"))
            break;
        if (strstr(buf, "Host:") != NULL)
            continue;
        if (strstr(buf, "User-Agent:") != NULL)
            continue;
        if (strstr(buf, "Connection:") != NULL)
            continue;
        if (strstr(buf, "Proxy-Connection:") != NULL)
            continue;

        sprintf(newreq, "%s%s", newreq, buf);
    }
    sprintf(newreq, "%sHost: %s:%d\r\n", newreq, hostname, *port);
    sprintf(newreq, "%s%s%s%s", newreq, user_agent_hdr, conn_hdr, prox_hdr);
    sprintf(newreq, "%s\r\n", newreq);
}

/* parse_uri - URI parser */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0)
    {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if(hostend == NULL) return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')
    {
        *port = atoi(hostend + 1);
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
    {
    return -1;
    }
    else
    {
        strcpy(pathname, pathbegin++);
    }

    return 0;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
