#ifndef CACHE_H
#define CACHE_H

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Cache node strcut */
typedef struct cnode {
	char *host;
	char *path;	
	char *payload;
	struct cnode *prev;
	struct cnode *next;
	size_t size;
	int port;
} cnode_t;

extern cnode_t *tail;
extern cnode_t *head;
extern int cache_count;
extern volatile size_t cache_load;
extern volatile int readcnt;            // Initially 0
extern sem_t mutex, w;         // Both initially 1;

/* Cache function prototypes */
int cmp(cnode_t *this, char *host, int port, char *path);
void init_cache();
void delete_cache(cnode_t *node);
void enqueue_cache(cnode_t *node);
void dequeue_cache();
cnode_t * new(char *host, int port, char *path, char *payload, size_t size);
cnode_t * match_cache(char *host, int port, char *path);
int check_cache();
void Check_cache();


#endif