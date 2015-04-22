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

// list of black-listed words for content filtering
// any content in which any of these words occure, will be blocked
char *word_blacklist[] = {"Alcoholic","Amateur","Analphabet","Anarchist","Ape","Arse","Arselicker","Ass","Ass master","Ass-kisser","Ass-nugget","Ass-wipe","Asshole","Baby","Backwoodsman","Balls","Bandit","Barbar","Bastard","Bastard","Beavis","Beginner","Biest","Bitch","Blubber gut","Bogeyman","Booby","Boozer","Bozo","Brain-fart","Brainless","Brainy","Brontosaurus","Brownie","Bugger","Bugger", "silly","Bulloks","Bum","Bum-fucker","Butt","Buttfucker","Butthead","Callboy","Callgirl","Camel","Cannibal","Cave man","Chaavanist","Chaot","Chauvi","Cheater","Chicken","Children fucker","Clit","Clown","Cock","Cock master","Cock up","Cockboy","Cockfucker","Cockroach","Coky","Con merchant","Con-man","Country bumpkin","Cow","Creep","Creep","Cretin","Criminal","Cunt","Cunt sucker","Daywalker","Deathlord","Derr brain","Desperado","Devil","Dickhead","Dinosaur","Disguesting packet","Diz brain","Do-Do","Dog","Dog, dirty","Dogshit","Donkey","Drakula","Dreamer","Drinker","Drunkard","Dufus","Dulles","Dumbo","Dummy","Dumpy","Egoist","Eunuch","Exhibitionist","Fake","Fanny","Farmer","Fart","Fart, shitty","Fatso","Fellow","Fibber","Fish","Fixer","Flake","Flash Harry","Freak","Frog","Fuck","Fuck face","Fuck head","Fuck noggin","Fucker","Head, fat","Hell dog","Hillbilly","Hooligan","Horse fucker","Idiot","Ignoramus","Jack-ass","Jerk","Joker","Junkey","Killer","Lard face","Latchkey child","Learner","Liar","Looser","Lucky","Lumpy","Luzifer","Macho","Macker","Man, old","Minx","Missing link","Monkey","Monster","Motherfucker","Mucky pub","Mutant","Neanderthal","Nerfhearder","Nobody","Nurd","Nuts, numb","Oddball","Oger","Oil dick","Old fart","Orang-Uthan","Original","Outlaw","Pack","Pain in the ass","Pavian","Pencil dick","Pervert","Pig","Piggy-wiggy","Pirate","Pornofreak","Prick","Prolet","Queer","Querulant","Rat","Rat-fink","Reject","Retard","Riff-Raff","Ripper","Roboter","Rowdy","Rufian","Sack","Sadist","Saprophyt","Satan","Scarab","Schfincter","Shark","Shit eater","Shithead","Simulant","Skunk","Skuz bag","Slave","Sleeze","Sleeze bag","Slimer","Slimy bastard","Small pricked","Snail","Snake","Snob","Snot","Son of a bitch ","Square","Stinker","Stripper","Stunk","Swindler","Swine","Teletubby","Thief","Toilett cleaner","Tussi","Typ","Unlike","Vampir","Vandale","Varmit","Wallflower","Wanker","Wanker, bloody","Weeze Bag","Whore","Wierdo","Wino","Witch","Womanizer","Woody allen","Worm","Xena","Xenophebe","Xenophobe","XXX Watcher","Yak","Yeti","Zit face"};
int word_blacklist_len = 237;

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
	
	// checking if the cache directory exists
	if (stat("./cache/", &st) == -1) {
		mkdir("./cache/", 0700);
	}
   
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
		
		bzero((char*)buffer, 4096);
		
		// recieving the http request
		recv(newsockfd, buffer, 4096, 0);
		
		sscanf(buffer, "%s %s %s", type, url_name, proto);
		
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
			
			goto end;
		}
		
		parse_url(url_name, url_host, &port, url_path);
		
		filter_url(url_name, filter_urld);
		
#ifdef DEBUG
		printf("\t-> url_host: %s\n", url_host);
		printf("\t-> port: %d\n", port);
		printf("\t-> url_path: %s\n", url_path);
		printf("\t-> filter_urld: %s\n", filter_urld);
#endif

		// BLACK LIST CHECK
		for(i = 0; i < url_blacklist_len; i++)
		{
			if(NULL != strstr(url_name, url_blacklist[i]))
			{
#ifdef DEBUG
				printf("\t-> url in blacklist: %s\n", url_blacklist[i]);
#endif
				sprintf(buffer,"400 : BAD REQUEST\nURL FOUND IN BLACKLIST\n%s", url_blacklist[i]);
				send(newsockfd, buffer, strlen(buffer), 0);
				
				goto end;
			}
		}
		
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
				
		// CACHING CHECK
		sprintf(filepath, "./cache/%s", filter_urld);
		if (0 != access(filepath, 0)) {
			// we don't have any file by this name
			// meaning: we should request it from the remote host
			sprintf(buffer,"GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", url_path, url_host);
#ifdef DEBUG		
			printf("\t-> first access...\n");
#endif
			// jump into request
			goto request; 
		}
		
		// open the file
		sprintf(filepath, "./cache/%s", filter_urld);
		cfd = open (filepath, O_RDWR);
		bzero((char*)buffer, 4096);
		// reading the first chunk is enough
		read(cfd, buffer, 4096);
		close(cfd);
		
		// find the first occurunce of "Date:" in response -- NULL if none.
		dateptr = strstr(buffer, "Date:");
		if(NULL != dateptr)
		{
			// response has a Date field, like Date: Fri, 18 Apr 2014 02:57:20 GMT
			
			bzero((char*)datetime, 256);
			// skip 6 characters, namely "Date: "
			// and copy 29 characters, like
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

do_cache:
		cfd = -1;
		
		// since the response can be huge, we might need to iterate to
		// read all of it
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
					sscanf(buffer, "HTTP/%f %d", &ver, &response_code);
					
#ifdef DEBUG		
					printf("\t-> response_code: %d\n", response_code);
#endif
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
						
#ifdef DEBUG
						printf("\t-> not modified\n");
						printf("\t-> from local cache...\n");
#endif
						// send the response to the browser from local cache
						goto from_cache;
					}
				}
				
				// for each swear word
				for(i = 0; i < word_blacklist_len; i++)
				{
					// if the swear word occurs in the content
					if(NULL != strstr(buffer, word_blacklist[i]))
					{
#ifdef DEBUG
						printf("\t-> content in blacklist: %s\n", word_blacklist[i]);
#endif
						
						close(cfd);
						
						// remove the cache file
						sprintf(filepath, "./cache/%s", filter_urld);
						remove(filepath);
						
						// tell the browser why we are not providing the content
						sprintf(buffer,"400 : BAD REQUEST\nCONTENT FOUND IN BLACKLIST\n%s", word_blacklist[i]);
						send(newsockfd, buffer, strlen(buffer), 0);		// send to browser
						goto end;
					}
				}
				
				// write to file
				write(cfd, buffer, n);
			}
		} while(n > 0);
		close(cfd);
		
		
from_cache:
		
		// read from cache file
		sprintf(filepath, "./cache/%s", filter_urld);
		if((cfd = open (filepath, O_RDONLY)) < 0)
		{
			perror("failed to open cache file");
			goto end;
		}
		do
		{
			bzero((char*)buffer, 4096);
			n = read(cfd, buffer, 4096);
			if(n > 0)
			{
				// send it to the browser
				send(newsockfd, buffer, n, 0);
			}
		} while(n > 0);
		close(cfd);

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
		
		goto accepting;
	}
	
	close(sockfd);
	return 0;
}
