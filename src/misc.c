/*
 * misc.c, SJ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <ctype.h>
#include "misc.h"
#include "smtpcodes.h"
#include "errmsg.h"
#include "config.h"
#include "tai.h"


/*
 * fatal function for quitting
 */

void __fatal(char *s){
   fprintf(stderr, "%s\n", s);
   exit(1);
}

/*
 * calculate the difference betwwen two timevals in [usec]
 */

long tvdiff(struct timeval a, struct timeval b){
   double res;

   res = (a.tv_sec * 1000000 + a.tv_usec) - (b.tv_sec * 1000000 + b.tv_usec);
   return (long) res;
}


/*
 * search something in a buffer
 */

int searchStringInBuffer(char *s, int len1, char *what, int len2){
   int i, k, r;

   for(i=0; i<len1; i++){
      r = 0;

      for(k=0; k<len2; k++){
         if(*(s+i+k) == *(what+k))
            r++;
      }

      if(r == len2)
         return i;
   }

   return 0;
}


/*
 * count a character in buffer
 */

int countCharacterInBuffer(char *p, char c){
   int i=0;

   for(; *p; p++){
      if(*p == c)
         i++;
   }

   return i;
}


void replaceCharacterInBuffer(char *p, char from, char to){
   int i, k=0;

   for(i=0; i<strlen(p); i++){
      if(p[i] == from){
         if(to > 0){
            p[k] = to;
            k++;
         }
      }
      else {
         p[k] = p[i];
         k++;
      }

   }

   p[k] = '\0';
}


/*
 * split a string by a character as delimiter
 */

char *split(char *row, int ch, char *s, int size){
   char *r;
   int len;

   if(row == NULL)
      return NULL;

   r = strchr(row, ch);
   if(r == NULL){
      len = strlen(row);
      if(len > size)
         len = size;
   }
   else {
      len = strlen(row) - strlen(r);
      if(len > size)
         len = size;

      r++;
   }

   if(s != NULL){
      strncpy(s, row, len);
      s[len] = '\0';
   }

   return r;
}


/*
 * split a string by a string as delimiter
 */

char *split_str(char *row, char *what, char *s, int size){
   char *r;
   int len;

   memset(s, 0, size);

   if(row == NULL)
      return NULL;

   r = strstr(row, what);
   if(r == NULL){
      len = strlen(row);
      if(len > size)
         len = size;
   }
   else {
      len = strlen(row) - strlen(r);
      if(len > size)
         len = size;

      r += strlen(what);
   }

   if(s != NULL){
      strncpy(s, row, len);
      s[len] = '\0';
   }

   return r;
}


/*
 * trim trailing CR-LF
 */

void trimBuffer(char *s){
   char *p;

   p = strrchr(s, '\n');
   if(p) *p = '\0';

   p = strrchr(s, '\r');
   if(p) *p = '\0';

}


/*
 * extract email
 */

int extractEmail(char *rawmail, char *email){
   char *p;

   p = strchr(rawmail, '<');
   if(p){
      snprintf(email, SMALLBUFSIZE-1, "%s", p+1);
      p = strchr(email, '>');
      if(p){
         *p = '\0';
         return 1;
      }
   }

   return 0;
}


/*
 * create an ID
 */

void create_id(char *id){
   int i;
   unsigned char buf[RND_STR_LEN/2];

   memset(id, 0, SMALLBUFSIZE);

   get_random_bytes(buf, RND_STR_LEN/2);

   for(i=0; i < RND_STR_LEN/2; i++){
      sprintf(id, "%02x", buf[i]);
      id += 2;
   }

}


/*
 * reading from pool
 */

int get_random_bytes(unsigned char *buf, int len){
   int fd, ret=0;
   struct taia now;
   char nowpack[TAIA_PACK];

   /* the first 12 bytes are the taia timestamp */

   taia_now(&now);
   taia_pack(nowpack, &now);

   memcpy(buf, nowpack, 12);

   fd = open(RANDOM_POOL, O_RDONLY);
   if(fd == -1) return ret;

   if(readFromEntropyPool(fd, buf+12, len-12) != len-12){
      syslog(LOG_PRIORITY, "%s: %s", ERR_CANNOT_READ_FROM_POOL, RANDOM_POOL);
   }
   
   close(fd);
   return ret;
}


/*
 * read random data from entropy pool
 */

int readFromEntropyPool(int fd, void *_s, size_t n){
   char *s = _s;
   ssize_t res, pos = 0;

   while(n > pos){
      res = read(fd, s + pos, n - pos);
      switch(res){
         case  -1: continue;
         case   0: return res;
         default : pos += res;
      }
   }
   return pos;
}


/*
 * recv() with timeout
 */

int recvtimeout(int s, char *buf, int len, int timeout){
    fd_set fds;
    int n;
    struct timeval tv;

    memset(buf, 0, MAXBUFSIZE);

    FD_ZERO(&fds);
    FD_SET(s, &fds);

    tv.tv_sec = timeout;
    tv.tv_usec = TIMEOUT_USEC;

    n = select(s+1, &fds, NULL, NULL, &tv);
    if (n == 0) return -2; // timeout!
    if (n == -1) return -1; // error

    return recv(s, buf, len, 0);
}


/*
 * check if it's a valid ID
 */

int isValidClapfID(char *p){

   if(strlen(p) != 30 && strlen(p) != 31)
      return 0;

   for(; *p; p++){
      /* 0-9: 0x30-0x39, a-f: 0x61-0x66 */

      if(! ((*p >= 0x30 && *p <= 0x39) || (*p >= 0x61 && *p <= 0x66) || *p == 0x0d) ){
         //printf("%c*\n", *p);
         return 0;
      }
   }

   return 1;
}


/*
 * is it a valid dotted IPv4 address
 */

int isDottedIPv4Address(char *s){
   struct in_addr addr;

   if(inet_aton(s, &addr) == 0) return 0;

   return 1;
}


/*
 * whitelist check
 */

int isEmailAddressOnList(char *list, char *tmpfile, char *email, struct __config *cfg){
   char *p, *q, w[SMALLBUFSIZE];

   if(email == NULL) return 0;

   p = list;

   if(cfg->verbosity >= _LOG_INFO) syslog(LOG_PRIORITY, "%s: list: %s", tmpfile, list);

   do {
      p = split(p, '\n', w, SMALLBUFSIZE-1);

      trimBuffer(w);

      if(strlen(w) > 2){

         if(cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "%s: matching '%s' on '%s'", tmpfile, w, email);

         if(w[strlen(w)-1] == '$'){
            q = email + strlen(email) - strlen(w) + 1;
            if(strncasecmp(q, w, strlen(w)-1) == 0)
               return 1;
         }
         else if(strcasestr(email, w))
            return 1;

      }

   } while(p);

   return 0;
}


void write_pid_file(char *pidfile){
   FILE *f;

   f = fopen(pidfile, "w");
   if(f){
      fprintf(f, "%d", (int)getpid());
      fclose(f);
   }
   else syslog(LOG_PRIORITY, "cannot write pidfile: %s", pidfile);
}


int drop_privileges(struct passwd *pwd){

   if(pwd->pw_uid > 0 && pwd->pw_gid > 0){

      if(getgid() != pwd->pw_gid){
         if(setgid(pwd->pw_gid)) return -1;
      }

      if(getuid() != pwd->pw_uid){
         if(setuid(pwd->pw_uid)) return -1;
      }

   }

   return 0;
}


#ifndef _GNU_SOURCE
char *strcasestr(const char *s, const char *find){
   char c, sc;
   size_t len;

   if((c = *find++) != 0){
      c = tolower((unsigned char)c);
      len = strlen(find);
      do {
         do {
            if((sc = *s++) == 0)
               return (NULL);
         } while((char)tolower((unsigned char)sc) != c);
      } while (strncasecmp(s, find, len) != 0);
      s--;
   }

   return((char*)s);
}
#endif

