#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

// convert urls to string to filter urls
void filter_url(unsigned char *a, char *x)
{
    char rfc3986[256] = {0};
    int i = 0;
    for (; i < 256; i++) {
        rfc3986[i] = isalnum(i)||i == '~'||i == '-'||i == '.'||i == '_'
        ? i : 0;
    }
    
    char *tb = rfc3986;
    
    for (; *a; a++) {
        if (tb[*a]) sprintf(x, "%c", tb[*a]);
        else        sprintf(x, "%%%02X", *a);
        while (*++x);
    }
}

void parse_url(char *url_name, char *host_name, int *port, char *path)
{
    char nurl[1024], nhost_name[1024], portstr[16];
    char* tok_str_ptr = NULL;
    int offset = 1;
    
    // copy the original url to work on
    strcpy(nurl, url_name);
    char *ppath = &(nurl[0]);
    
    // check if the address starts with http://
    // e.g. somehost.com/index.php vs. http://somehost.com/index.php
    if(NULL != strstr(ppath, "http://"))
    {
        ppath = &(nurl[6]);
        offset += 6;
    }
    
    // finding the host_name
    tok_str_ptr = strtok(ppath, "/");
    sprintf(nhost_name, "%s", tok_str_ptr);
    
    // check if the host_name also comes with a port no or not
    if(NULL != strstr(nhost_name, ":"))
    {
        tok_str_ptr = strtok(nhost_name, ":");
        sprintf(host_name, "%s", tok_str_ptr);
        tok_str_ptr = strtok(NULL, ":");
        sprintf(portstr, "%s", tok_str_ptr);
        *port = atoi(portstr);
    } else {
        sprintf(host_name, "%s", nhost_name);
    }
    
    // the rest of the url gives us the path
    ppath = &(url_name[strlen(host_name) + offset]);
    sprintf(path, "%s", ppath);
    if(strcmp(path, "") == 0)
    {
        sprintf(path, "/");
    }
}

#define DEBUG

// list of black-listed websites
// each url in which any of these words occure, will be blocked
char *url_blacklist[] = { "www.facebook.com", "www.youtube.com", "www.hulu.com","www.virus.com" };
int url_blacklist_len = 4;


int main(int argc, char **argv)
{
    pid_t chpid;
    struct sockaddr_in addr_in, cli_addr, serv_addr;
    struct hostent *hostent;
    int sockfd, newsockfd;
    int clilen = sizeof(cli_addr);
    struct stat st = {0};
    
    // the first argument shows the port-no to listen on
    if(argc != 2)
    {
        printf("Using:\n\t%s <port>\n", argv[0]);
        return -1;
    }
    
    printf("starting...\n");
    
    
    bzero((char*)&serv_addr, sizeof(serv_addr));
    bzero((char*)&cli_addr, sizeof(cli_addr));
	   
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[1]));
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    
    // creating the listening socket for our proxy server
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sockfd < 0)
    {
        perror("failed to initialize socket");
    }
    
    // binding our socket to the given port
    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("failed to bind socket");
    }
    
    // start listening - w/ backlog = 50
    listen(sockfd, 50);
    
accepting:
    newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
    
    if((chpid = fork()) == 0)
    {
        struct sockaddr_in host_addr;
        int i, n;			// loop indices
        int rsockfd;		// remote socket file descriptor
        int cfd;			// cache-file file descriptor
        
        int port = 80;		// default http port - can be overridden
        char type[256];		// type of request - e.g. GET/POST/HEAD
        char url_name[4096];		// url in request - e.g. facebook.com
        char proto[256];	// protocol ver in request - e.g. HTTP/1.0
        
        char datetime[256];	// the date-time when we last cached a url
        
        // break-down of a given url by parse_url function
        char url_host[256], url_path[256];
        
        char filter_urld[4096];	// encoded url, used for cahce filenames
        char filepath[256]; 	// used for cache file paths
        
        char *dateptr;		// used to find the date-time in http response
        char buffer[4096];	// buffer used for send/receive
        int response_code;	// http response code - e.g. 200, 304, 301
        
        bzero((char*)buffer, 4096);			// let's play it safe!
        
        // recieving the http request
        recv(newsockfd, buffer, 4096, 0);
        
        // we only care about the first line in request
        sscanf(buffer, "%s %s %s", type, url_name, proto);
        
        // adjusting the url -- some cleanup!
        if(url_name[0] == '/')
        {
            strcpy(buffer, &url_name[1]);
            strcpy(url_name, buffer);
        }
        
#ifdef DEBUG
        printf("-> %s %s %s\n", type, url_name, proto);
#endif
        
        // make sure the request is a valid request and we can process it!
        // we only accept GET requests
        // also some browsers send non-http requests -- this filters them out!
        if((strncmp(type , "GET", 3) != 0) || ((strncmp(proto, "HTTP/1.1", 8) != 0) && (strncmp(proto, "HTTP/1.0", 8) != 0)))
        {
#ifdef DEBUG
            printf("\t-> bad request format - we only accept HTTP GETs\n");
#endif
            // invalid request -- send the following line back to browser
            sprintf(buffer,"400 : BAD REQUEST\nONLY GET REQUESTS ARE ALLOWED");
            send(newsockfd, buffer, strlen(buffer), 0);
            
            // sometimes goto makes the code more readable!
            goto end;
        }
        
        // OK! now we are sure that we have a valid GET request
        
        // let's break down the url to know the host and path
        parse_url(url_name, url_host, &port, url_path);
        
        // encoding the url for later use
        filter_url(url_name, filter_urld);
        
#ifdef DEBUG
        printf("\t-> url_host: %s\n", url_host);
        printf("\t-> port: %d\n", port);
        printf("\t-> url_path: %s\n", url_path);
        printf("\t-> filter_urld: %s\n", filter_urld);
#endif
        
        // BLACK LIST CHECK
        // check if the given url is black-listed or not
        // for each entry in black-list
        for(i = 0; i < url_blacklist_len; i++)
        {
            // if url contains the black-listed word
            if(NULL != strstr(url_name, url_blacklist[i]))
            {
#ifdef DEBUG
                printf("\t-> url in blacklist: %s\n", url_blacklist[i]);
#endif
                // sorry! -- tell the browser that this url is forbidden
                sprintf(buffer,"400 : BAD REQUEST\nURL FOUND IN BLACKLIST\n%s", url_blacklist[i]);
                send(newsockfd, buffer, strlen(buffer), 0);
                
                // again, goto for clarity!
                goto end;
            }
        }
        
        // So, we know that the url is premissible, what else?
        // we need to find the ip for the host
        if((hostent = gethostbyname(url_host)) == NULL)
        {
            fprintf(stderr, "failed to resolve %s: %s\n", url_host, strerror(errno));
            goto end;
        }
        
        bzero((char*)&host_addr, sizeof(host_addr));
        host_addr.sin_port = htons(port);
        host_addr.sin_family = AF_INET;
        bcopy((char*)hostent->h_addr, (char*)&host_addr.sin_addr.s_addr, hostent->h_length);
        
        // create a socket to connect to the remote host
        rsockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        
        if(rsockfd < 0)
        {
            perror("failed to create remote socket");
            goto end;
        }
        
        // try connecting to the remote host
        if(connect(rsockfd, (struct sockaddr*)&host_addr, sizeof(struct sockaddr)) < 0)
        {
            perror("failed to connect to remote server");
            goto end;
        }
        
#ifdef DEBUG
        printf("\t-> connected to host: %s w/ ip: %s\n", url_host, inet_ntoa(host_addr.sin_addr));
#endif
        
            goto request;
        }
        
    
        // find the first occurunce of "Date:" in response -- NULL if none.
        dateptr = strstr(buffer, "Date:");
        if(NULL != dateptr)
        {
            // response has a Date field, like Date: Fri, 18 Apr 2014 02:57:20 GMT
            
            bzero((char*)datetime, 256);
            // skip 6 characters, namely "Date: "
            // and copy 29 characters, like "Fri, 18 Apr 2014 02:57:20 GMT"
            strncpy(datetime, &dateptr[6], 29);
            
            // send CONDITIONAL GET
            // If-Modified-Since the date that we cached it
            sprintf(buffer,"GET %s HTTP/1.0\r\nHost: %s\r\nIf-Modified-Since: %s\r\nConnection: close\r\n\r\n", url_path, url_host, datetime);
#ifdef DEBUG
            printf("\t-> conditional GET...\n");
            printf("\t-> If-Modified-Since: %s\n", datetime);
#endif
        } else {
            // generally all http responses have Date filed, but just in case!
            sprintf(buffer,"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", url_path, url_host);
#ifdef DEBUG
            printf("\t-> the response had no date field\n");
#endif
        }
        
    request:
        // send the request to remote host
        n = send(rsockfd, buffer, strlen(buffer), 0);
        
        if(n < 0)
        {
            perror("failed to write to remote socket");
            goto end;
        }
        
        cfd = -1;
        do
        {
            bzero((char*)buffer, 4096);
            
            // recieve from remote host
            n = recv(rsockfd, buffer, 4096, 0);
            // if we have read anything - otherwise END-OF-FILE
            if(n > 0)
            {
                // if this is the first time we are here
                // meaning: we are reading the http response header
                if(cfd == -1)
                {
                    float ver;
                    // read the first line to discover the response code
                    // we only care about these two!
                    sscanf(buffer, "HTTP/%f %d", &ver, &response_code);
                    
#ifdef DEBUG
                    printf("\t-> response_code: %d\n", response_code);
#endif
                    // if it is not 304 -- anything other than sub-CASE32
                    if(response_code != 304)
                    {
                        // create the cache-file to save the content
                        sprintf(filepath, "./cache/%s", filter_urld);
                        if((cfd = open(filepath, O_RDWR|O_TRUNC|O_CREAT, S_IRWXU)) < 0)
                        {
                            perror("failed to create cache file");
                            goto end;
                        }
#ifdef DEBUG
                        printf("\t-> from remote host...\n");
#endif
                    } else {
                        // if it is 304 -- sub-CASE32
                        // we don't need to read the content
                        // our content is already up-to-date
#ifdef DEBUG
                        printf("\t-> not modified\n");
                        printf("\t-> from local cache...\n");
#endif
                        // send the response to the browser from local cache
                        goto from_cache;
                    }
                }
                
                
                
              
            }
        } while(n > 0);
        close(cfd);
        
        // up to here we only cached the response, now we need to send it
        // back to the requesting browser
        
    from_cache:
        
        
    end:
#ifdef DEBUG
        printf("\t-> exiting...\n");
#endif
        // closing sockets!
        close(rsockfd);
        close(newsockfd);
        close(sockfd);
        return 0;
    } else {
        // closing socket
        close(newsockfd);
        
        // loop...
        goto accepting;
    }
    
    close(sockfd);
    return 0;
    // :D -- You'll never get here! :(
}
