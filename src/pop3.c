/*
 * pop3.c, SJ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <fcntl.h>
#include <ctype.h>
#include <syslog.h>
#include <unistd.h>
#include <limits.h>
#include <piler.h>


void update_import_job_stat(struct session_data *sdata, struct __data *data);


int is_last_complete_pop3_packet(char *s, int len){

   if(*(s+len-5) == '\r' && *(s+len-4) == '\n' && *(s+len-3) == '.' && *(s+len-2) == '\r' && *(s+len-1) == '\n'){
      return 1;
   }

   return 0;
}


int connect_to_pop3_server(int sd, char *username, char *password, struct __data *data, int use_ssl){
   int n;
   char buf[MAXBUFSIZE];
   X509* server_cert;
   char *str;


   if(use_ssl == 1){

      SSL_library_init();
      SSL_load_error_strings();

   #if OPENSSL_VERSION_NUMBER < 0x10100000L
      data->ctx = SSL_CTX_new(TLSv1_client_method());
   #else
      data->ctx = SSL_CTX_new(TLS_client_method());
   #endif
      CHK_NULL(data->ctx, "internal SSL error");

      data->ssl = SSL_new(data->ctx);
      CHK_NULL(data->ssl, "internal ssl error");

      SSL_set_fd(data->ssl, sd);
      n = SSL_connect(data->ssl);
      CHK_SSL(n, "internal ssl error");

      printf("Cipher: %s\n", SSL_get_cipher(data->ssl));

      server_cert = SSL_get_peer_certificate(data->ssl);
      CHK_NULL(server_cert, "server cert error");

      str = X509_NAME_oneline(X509_get_subject_name(server_cert), 0, 0);
      CHK_NULL(str, "error in server cert");
      OPENSSL_free(str);

      str = X509_NAME_oneline(X509_get_issuer_name(server_cert), 0, 0);
      CHK_NULL(str, "error in server cert");
      OPENSSL_free(str);

      X509_free(server_cert);
   }


   recvtimeoutssl(sd, buf, sizeof(buf), data->import->timeout, use_ssl, data->ssl);


   snprintf(buf, sizeof(buf)-1, "USER %s\r\n", username);

   write1(sd, buf, strlen(buf), use_ssl, data->ssl);
   recvtimeoutssl(sd, buf, sizeof(buf), data->import->timeout, use_ssl, data->ssl);


   snprintf(buf, sizeof(buf)-1, "PASS %s\r\n", password);

   write1(sd, buf, strlen(buf), use_ssl, data->ssl);
   recvtimeoutssl(sd, buf, sizeof(buf), data->import->timeout, use_ssl, data->ssl);

   if(strncmp(buf, "+OK", 3) == 0) return OK;

   printf("error: %s", buf);

   return ERR;
}


int process_pop3_emails(int sd, struct session_data *sdata, struct __data *data, int use_ssl, int dryrun, struct __config *cfg){
   int i=0, rc=ERR, n, pos, readlen, fd, lastpos, nreads;
   char *p, buf[MAXBUFSIZE], filename[SMALLBUFSIZE];
   char aggrbuf[3*MAXBUFSIZE];

   data->import->processed_messages = 0;
   data->import->total_messages = 0;

   snprintf(buf, sizeof(buf)-1, "STAT\r\n");
   write1(sd, buf, strlen(buf), use_ssl, data->ssl);

   recvtimeoutssl(sd, buf, sizeof(buf), data->import->timeout, use_ssl, data->ssl);

   if(strncmp(buf, "+OK ", 4) == 0){
      p = strchr(&buf[4], ' ');
      if(p){
         *p = '\0';
         data->import->total_messages = atoi(&buf[4]);
      }
   }
   else return ERR;


   if(data->quiet == 0) printf("found %d messages\n", data->import->total_messages);

   if(data->import->total_messages <= 0) return OK;

   for(i=data->import->start_position; i<=data->import->total_messages; i++){
      data->import->processed_messages++;
      if(data->quiet == 0){ printf("processed: %7d [%3d%%]\r", data->import->processed_messages, 100*i/data->import->total_messages); fflush(stdout); }


      snprintf(buf, sizeof(buf)-1, "RETR %d\r\n", i);

      snprintf(filename, sizeof(filename)-1, "pop3-tmp-%d-%d.txt", getpid(), i);
      unlink(filename);

      fd = open(filename, O_CREAT|O_EXCL|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
      if(fd == -1){
         printf("cannot open: %s\n", filename);
         return rc;
      }

      write1(sd, buf, strlen(buf), use_ssl, data->ssl);

      readlen = 0;
      pos = 0;
      nreads = 0;

      memset(aggrbuf, 0, sizeof(aggrbuf));
      lastpos = 0;


      while((n = recvtimeoutssl(sd, buf, sizeof(buf), data->import->timeout, use_ssl, data->ssl)) > 0){
         nreads++;
         readlen += n;

         if(nreads == 1){

            if(strncmp(buf, "+OK", 3) == 0){
               p = strchr(&buf[3], '\n');
               if(p){
                  *p = '\0';
                  pos = strlen(buf)+1;
                  *p = '\n';
               }
            }
            else { printf("error: %s", buf); return ERR; }

         }

         if(lastpos + 1 + n < sizeof(aggrbuf)){

            if(nreads == 1){
               memcpy(aggrbuf+lastpos, buf+pos, n-pos);
               lastpos += n-pos;
            }
            else {
               memcpy(aggrbuf+lastpos, buf, n);
               lastpos += n;
            }
         }
         else {
            write(fd, aggrbuf, sizeof(buf));

            memmove(aggrbuf, aggrbuf+sizeof(buf), lastpos-sizeof(buf));
            lastpos -= sizeof(buf);

            memcpy(aggrbuf+lastpos, buf, n);
            lastpos += n;
         }

         if(is_last_complete_pop3_packet(aggrbuf, lastpos) == 1){
            write(fd, aggrbuf, lastpos-3);
            break;
         }

      } 

      close(fd);

      if(dryrun == 0) rc = import_message(filename, sdata, data, cfg);
      else rc = OK;

      if(dryrun == 0 && rc == OK && data->import->remove_after_import == 1){
         snprintf(buf, sizeof(buf)-1, "DELE %d\r\n", i);
         write1(sd, buf, strlen(buf), use_ssl, data->ssl);
         recvtimeoutssl(sd, buf, sizeof(buf), data->import->timeout, use_ssl, data->ssl);
      }

      if(i % 100 == 0){
         time(&(data->import->updated));
         update_import_job_stat(sdata, data);
      }

      if(data->import->download_only == 0) unlink(filename);


      /* whether to quit after processing a batch of messages */

      if(data->import->batch_processing_limit > 0 && data->import->processed_messages >= data->import->batch_processing_limit){
         break;
      }
   }


   snprintf(buf, sizeof(buf)-1, "QUIT\r\n");
   write1(sd, buf, strlen(buf), use_ssl, data->ssl);

   if(data->quiet == 0) printf("\n");

   time(&(data->import->finished));
   data->import->status = 2;
   update_import_job_stat(sdata, data);


   return OK;
}


