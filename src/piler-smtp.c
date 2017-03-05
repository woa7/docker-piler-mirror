/*
 * piler-smtp.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <locale.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <syslog.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <piler.h>

#define PROGNAME "piler-smtp"

extern char *optarg;
extern int optind;

struct epoll_event event, *events=NULL;
int timeout = 20; // checking for timeout this often [sec]
int num_connections = 0;
int listenerfd = -1;

char *configfile = CONFIG_FILE;
struct __config cfg;
struct passwd *pwd;
struct smtp_session *session, **sessions=NULL;


void p_clean_exit(){
   int i;

   if(listenerfd != -1) close(listenerfd);

   if(sessions){
      for(i=0; i<cfg.max_connections; i++){
         if(sessions[i]) free_smtp_session(sessions[i]);
      }

      free(sessions);
   }

   if(events) free(events);

   syslog(LOG_PRIORITY, "%s has been terminated", PROGNAME);

   //unlink(cfg.pidfile);

   ERR_free_strings();

   exit(1);
}


void fatal(char *s){
   syslog(LOG_PRIORITY, "%s", s);
   p_clean_exit();
}


void check_for_client_timeout(){
   time_t now;
   int i;

   if(num_connections > 0){
      time(&now);

      for(i=0; i<cfg.max_connections; i++){
         if(sessions[i] && now - sessions[i]->lasttime >= cfg.smtp_timeout){
            syslog(LOG_PRIORITY, "client %s timeout", sessions[i]->remote_host);
            tear_down_session(sessions, sessions[i]->slot, &num_connections);
         }
      }
   }

   alarm(timeout);
}


void initialise_configuration(){
   cfg = read_config(configfile);

   if(strlen(cfg.username) > 1){
      pwd = getpwnam(cfg.username);
      if(!pwd) fatal(ERR_NON_EXISTENT_USER);
   }

   if(getuid() == 0 && pwd){
      check_and_create_directories(&cfg, pwd->pw_uid, pwd->pw_gid);
   }

   if(chdir(cfg.workdir)){
      syslog(LOG_PRIORITY, "workdir: *%s*", cfg.workdir);
      fatal(ERR_CHDIR);
   }

   setlocale(LC_MESSAGES, cfg.locale);
   setlocale(LC_CTYPE, cfg.locale);

   syslog(LOG_PRIORITY, "reloaded config: %s", configfile);
}


int main(int argc, char **argv){
   int listenerfd, client_sockfd;
   int i, n, daemonise=0;
   int client_len = sizeof(struct sockaddr_storage);
   ssize_t readlen;
   struct sockaddr_storage client_address;
   char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
   char readbuf[BIGBUFSIZE];
   int efd;

   while((i = getopt(argc, argv, "c:dvVh")) > 0){
      switch(i){

        case 'c' :
                   configfile = optarg;
                   break;

        case 'd' :
                   daemonise = 1;
                   break;

        case 'v' :
        case 'V' :
                   printf("%s build %d\n", VERSION, get_build());
                   return 0;

        case 'h' :
        default  : 
                   __fatal("usage: ...");
      }
   }

   (void) openlog(PROGNAME, LOG_PID, LOG_MAIL);

   initialise_configuration();

   listenerfd = create_and_bind(cfg.listen_addr, cfg.listen_port);
   if(listenerfd == -1){
      exit(1);
   }

   if(make_socket_non_blocking(listenerfd) == -1){
      fatal("make_socket_non_blocking()");
   }

   if(listen(listenerfd, cfg.backlog) == -1){
      fatal("ERROR: listen()");
   }

   if(drop_privileges(pwd)) fatal(ERR_SETUID);

   efd = epoll_create1(0);
   if(efd == -1){
      fatal("ERROR: epoll_create()");
   }

   event.data.fd = listenerfd;
   event.events = EPOLLIN | EPOLLET;
   if(epoll_ctl(efd, EPOLL_CTL_ADD, listenerfd, &event) == -1){
      fatal("ERROR: epoll_ctl() on efd");
   }

   set_signal_handler(SIGINT, p_clean_exit);
   set_signal_handler(SIGTERM, p_clean_exit);
   set_signal_handler(SIGALRM, check_for_client_timeout);
   set_signal_handler(SIGHUP, initialise_configuration);

   alarm(timeout);

   // calloc() initialitizes the allocated memory

   sessions = calloc(cfg.max_connections, sizeof(struct smtp_session));
   events = calloc(cfg.max_connections, sizeof(struct epoll_event));

   if(!sessions || !events) fatal("ERROR: calloc()");

   SSL_library_init();
   SSL_load_error_strings();

   srand(getpid());

   syslog(LOG_PRIORITY, "%s %s, build %d starting", PROGNAME, VERSION, get_build());

#if HAVE_DAEMON == 1
   if(daemonise == 1 && daemon(1, 0) == -1) fatal(ERR_DAEMON);
#endif

   for(;;){
      n = epoll_wait(efd, events, cfg.max_connections, -1);
      for(i=0; i<n; i++){

         if((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN))){
            syslog(LOG_PRIORITY, "ERROR: epoll error");
            close(events[i].data.fd);
            continue;
         }

         // We have 1 or more incoming connections to process

         else if(listenerfd == events[i].data.fd){

            while(1){

               client_sockfd = accept(listenerfd, (struct sockaddr *)&client_address, (socklen_t *)&client_len);
               if(client_sockfd == -1){
                  if((errno == EAGAIN) || (errno == EWOULDBLOCK)){
                     // We have processed all incoming connections
                     break;
                  }
                  else {
                     syslog(LOG_PRIORITY, "ERROR: accept()");
                     break;
                  }
               }

               if(getnameinfo((struct sockaddr *)&client_address, client_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV) == 0){
                  syslog(LOG_PRIORITY, "connected from %s:%s on descriptor %d", hbuf, sbuf, client_sockfd);
               }

               if(make_socket_non_blocking(client_sockfd) == -1){
                  syslog(LOG_PRIORITY, "ERROR: cannot make the socket non blocking");
                  break;
               }

               event.data.fd = client_sockfd;
               event.events = EPOLLIN | EPOLLET;
               if(epoll_ctl(efd, EPOLL_CTL_ADD, client_sockfd, &event) == -1){
                  syslog(LOG_PRIORITY, "ERROR: epoll_ctl() on client_sockfd");
                  break;
               }

               start_new_session(sessions, client_sockfd, &num_connections, &cfg);
            }

            continue;
         }


         // handle data from an existing connection

         else {
            int done = 0;

            session = get_session_by_socket(sessions, cfg.max_connections, events[i].data.fd);
            if(session == NULL){
               syslog(LOG_PRIORITY, "ERROR: cannot find session for this socket: %d", events[i].data.fd);
               close(events[i].data.fd);
               continue;
            }

            time(&(session->lasttime));

            while(1){
               memset(readbuf, 0, sizeof(readbuf));

               if(session->use_ssl == 1)
                  readlen = SSL_read(session->ssl, (char*)&readbuf[0], sizeof(readbuf)-1);
               else
                  readlen = read(events[i].data.fd, (char*)&readbuf[0], sizeof(readbuf)-1);

               if(cfg.verbosity >= _LOG_DEBUG && readlen > 0) syslog(LOG_PRIORITY, "got %ld bytes to read", readlen);

               if(readlen == -1){
                  /* If errno == EAGAIN, that means we have read all data. So go back to the main loop. */
                  if(errno != EAGAIN){
                     syslog(LOG_PRIORITY, "read");
                     done = 1;
                  }
                  break;
               }
               else if(readlen == 0){
                  /* End of file. The remote has closed the connection. */
                  done = 1;
                  break;
               }

               handle_data(session, &readbuf[0], readlen);

               if(session->protocol_state == SMTP_STATE_BDAT && session->bad == 1){
                  done = 1;
                  break;
               }
            }

            /* Don't wait until the remote client closes the connection after he sent the QUIT command */

            if(done || session->protocol_state == SMTP_STATE_FINISHED){
               tear_down_session(sessions, session->slot, &num_connections);
            }
         }


      }
   }

   return 0;
}