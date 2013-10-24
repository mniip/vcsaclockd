#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <utmp.h>

#ifndef MAXVCSA
#define MAXVCSA 6
#endif

typedef struct {
	unsigned char yres, xres, ypos, xpos;
} vcsainfo;

static time_to_die = 0;
static pthread_mutex_t* mutexes;

void vcsa_seek(int vcsa, vcsainfo info, int x, int y)
{
	lseek(vcsa, sizeof(vcsainfo) + (info.xres*y+x) * 2 + 1, SEEK_SET);
}

void blit_digit(int vcsa, vcsainfo info, int x, int y, int d)
{
	static char map[11][7] = {
		{0,14,10,10,10,14,0},
		{0,8 ,8 ,8 ,8 ,8 ,0},
		{0,14,8 ,14,2 ,14,0},
		{0,14,8 ,14,8 ,14,0},
		{0,10,10,14,8 ,8 ,0},
		{0,14,2 ,14,8 ,14,0},
		{0,14,2 ,14,10,14,0},
		{0,14,8 ,8 ,8 ,8 ,0},
		{0,14,10,14,10,14,0},
		{0,14,10,14,8 ,14,0},
		{0,0 ,4 ,0 ,4 ,0 ,0}
	};
	int i,j;
	for(i = 0; i < 5; i++)
		for(j = 0; j < 7; j++)
		{
			vcsa_seek(vcsa, info, x+i, y+j);
			if((map[d][j]>>i)&1)
				write(vcsa, "\x40", 1);
			else
				write(vcsa, "\x07", 1);
		}
}

void blit_clock(int vcsa, vcsainfo info, int h, int m, int s)
{
	blit_digit(vcsa, info, info.xres-33, 0, h/10);
	blit_digit(vcsa, info, info.xres-29, 0, h%10);
	blit_digit(vcsa, info, info.xres-25, 0, 10);
	blit_digit(vcsa, info, info.xres-21, 0, m/10);
	blit_digit(vcsa, info, info.xres-17, 0, m%10);
	blit_digit(vcsa, info, info.xres-13, 0, 10);
	blit_digit(vcsa, info, info.xres-9 , 0, s/10);
	blit_digit(vcsa, info, info.xres-5 , 0, s%10);
}

void* thread_vcsa(void* m)
{
	int s;
	int id = (intptr_t)m;
	vcsainfo info;
	int vcsa;
	char filename[20];
	sprintf(filename, "/dev/vcsa%d", id);
	if(-1 == (vcsa = open(filename, O_RDWR)))
	{
		printf("[%d]open %s: %s\n", id, filename, strerror(errno));
		return NULL;
	}
	while(!time_to_die)
	{
		time_t epoch_time;
		struct tm *tstruct;
		if(s = pthread_mutex_lock(mutexes+id))
		{
			printf("[%d]pthread_mutex_lock: %s\n", id, strerror(s));
			return NULL;
		}
		if(s = pthread_mutex_unlock(mutexes+id))
		{
			printf("[%d]pthread_mutex_unlock: %s\n", id, strerror(s));
			return NULL;
		}
		lseek(vcsa, 0, SEEK_SET);
		read(vcsa, &info, sizeof(info));
		epoch_time = time(NULL);
		tstruct = localtime(&epoch_time);
		blit_clock(vcsa, info, tstruct->tm_hour, tstruct->tm_min, tstruct->tm_sec);
		usleep(1000000);
	}
	close(vcsa);
}

int main(int argc, char* argv[])
{
	int s,i;
	pthread_t threads[MAXVCSA+1];
	char states[MAXVCSA+1];
	int utmp;
	if(-1 == (utmp = open("/var/run/utmp", O_RDONLY)))
	{
		printf("open /var/run/utmp: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	mutexes = calloc(MAXVCSA+1, sizeof(pthread_mutex_t));
	for(i = 1; i < MAXVCSA+1; i++)
	{
		if(s = pthread_mutex_init(mutexes+i, NULL))
		{
			printf("pthread_mutex_init: %s\n", strerror(s));
			exit(EXIT_FAILURE);
		}
		if(s = pthread_create(threads+i, NULL, &thread_vcsa, (void*)(intptr_t)i))
		{
			printf("pthread_create: %s\n", strerror(s));
			exit(EXIT_FAILURE);
		}
		states[i]=0;
	}
	while(!time_to_die)
	{
		struct utmp record;
		while(sizeof(struct utmp) == (s = read(utmp, &record, sizeof(struct utmp))))
		{
			if(record.ut_id[0] == 'c')
			{
				int ttyn=record.ut_id[1]-'0';
				if((ttyn > 0) && (ttyn < MAXVCSA+1))
					if(states[ttyn] && (record.ut_type != USER_PROCESS))
					{
						states[ttyn] = 0;
						printf("Logout on %d, unlocking clock\n", ttyn);
						if(s = pthread_mutex_unlock(mutexes+ttyn))
						{
							printf("pthread_mutex_unlock: %s\n", strerror(s));
							return;
						}
					}
					else if(!states[ttyn] && (record.ut_type == USER_PROCESS))
					{
						states[ttyn] = 1;
						printf("Login on %d, locking clock\n", ttyn);
						if(s = pthread_mutex_lock(mutexes+ttyn))
						{
							printf("pthread_mutex_lock: %s\n", strerror(s));
							return;
						}
					}
			}
		}
		if(s)
		{
			printf("Incomplete record in utmp, ignoring\n");
		}
		lseek(utmp, 0, SEEK_SET);
		usleep(100000);
	}
	for(i = 1; i < MAXVCSA+1; i++)
	{
		if(s = pthread_join(threads[i], NULL))
		{
			printf("pthread_join: %s\n", strerror(s));
			exit(EXIT_FAILURE);
		}
	}
	exit(EXIT_SUCCESS);
}
