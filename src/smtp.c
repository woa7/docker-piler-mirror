#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <piler.h>
#include "smtp.h"


void process_smtp_command(struct smtp_session *session, char *buf, struct config *cfg){
   char response[SMALLBUFSIZE];

   if(session->cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "got on fd=%d: *%s*", session->net.socket, buf);

   if(strncasecmp(buf, SMTP_CMD_HELO, strlen(SMTP_CMD_HELO)) == 0){
      process_command_helo(session, response, sizeof(response));
      return;
   }

   if(strncasecmp(buf, SMTP_CMD_EHLO, strlen(SMTP_CMD_EHLO)) == 0 ||
         strncasecmp(buf, LMTP_CMD_LHLO, strlen(LMTP_CMD_LHLO)) == 0){
      process_command_ehlo_lhlo(session, response, sizeof(response));
      return;
   }

   if(strncasecmp(buf, SMTP_CMD_MAIL_FROM, strlen(SMTP_CMD_MAIL_FROM)) == 0){
      process_command_mail_from(session, buf);
      return;
   }

   if(strncasecmp(buf, SMTP_CMD_RCPT_TO, strlen(SMTP_CMD_RCPT_TO)) == 0){
      process_command_rcpt_to(session, buf);
      return;
   }

   if(strncasecmp(buf, SMTP_CMD_DATA, strlen(SMTP_CMD_DATA)) == 0){
      process_command_data(session, cfg);
      return;
   }

   /* Support only BDAT xxxx LAST command */
   if(session->cfg->enable_chunking == 1 && strncasecmp(buf, SMTP_CMD_BDAT, strlen(SMTP_CMD_BDAT)) == 0 && strcasestr(buf, "LAST")){
      get_bdat_size_to_read(session, buf);
      return;
   }

   if(strncasecmp(buf, SMTP_CMD_QUIT, strlen(SMTP_CMD_QUIT)) == 0){
      process_command_quit(session, response, sizeof(response));
      return;
   }

   if(strncasecmp(buf, SMTP_CMD_RESET, strlen(SMTP_CMD_RESET)) == 0){
      process_command_reset(session);
      return;
   }

   if(session->cfg->tls_enable == 1 && strncasecmp(buf, SMTP_CMD_STARTTLS, strlen(SMTP_CMD_STARTTLS)) == 0 && session->net.use_ssl == 0){
      process_command_starttls(session);
      return;
   }

   send_smtp_response(session, SMTP_RESP_502_ERR);
}


void process_data(struct smtp_session *session, char *buf, int buflen){
   if(session->last_data_char == '\n' && strcmp(buf, ".\r\n") == 0){
      process_command_period(session);
   }
   else {
      // write line to file
      if(write(session->fd, buf, buflen) != -1){
         session->tot_len += buflen;
      }
      else syslog(LOG_PRIORITY, "ERROR (line: %d) process_data(): failed to write %d bytes", __LINE__, buflen);
   }

   session->last_data_char = buf[buflen-1];
}


void wait_for_ssl_accept(struct smtp_session *session){
   int rc;

   if(session->cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "waiting for ssl handshake");

   rc = SSL_accept(session->net.ssl);

   // Since we use non-blocking IO, SSL_accept() is likely to return with -1
   // "In this case a call to SSL_get_error() with the return value of SSL_accept()
   // will yield SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE."
   //
   // In this case we may proceed.

   if(rc == 1 || SSL_get_error(session->net.ssl, rc) == SSL_ERROR_WANT_READ){
      session->net.use_ssl = 1;
   }

   if(session->cfg->verbosity >= _LOG_DEBUG || session->net.use_ssl == 0){
      char ssl_error[SMALLBUFSIZE];

      ERR_error_string_n(ERR_get_error(), ssl_error, SMALLBUFSIZE);
      syslog(LOG_PRIORITY, "SSL_accept() result, rc=%d, errorcode: %d, error text: %s",
             rc, SSL_get_error(session->net.ssl, rc), ssl_error);
   }
}


void send_smtp_response(struct smtp_session *session, char *buf){
   write1(&(session->net), buf, strlen(buf));
   if(session->cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "sent on fd=%d: %s", session->net.socket, buf);
}


void process_command_helo(struct smtp_session *session, char *buf, int buflen){
   if(session->protocol_state == SMTP_STATE_INIT) session->protocol_state = SMTP_STATE_HELO;

   snprintf(buf, buflen-1, "250 %s\r\n", session->cfg->hostid);
   send_smtp_response(session, buf);
}


void process_command_ehlo_lhlo(struct smtp_session *session, char *buf, int buflen){
   char extensions[SMALLBUFSIZE];

   memset(extensions, 0, sizeof(extensions));

   if(session->protocol_state == SMTP_STATE_INIT) session->protocol_state = SMTP_STATE_HELO;

   // if tls is not started, but it's enabled in the config
   if(session->net.use_ssl == 0 && session->cfg->tls_enable == 1) snprintf(extensions, sizeof(extensions)-1, "%s", SMTP_EXTENSION_STARTTLS);
   if(session->cfg->enable_chunking == 1) strncat(extensions, SMTP_EXTENSION_CHUNKING, sizeof(extensions)-strlen(extensions)-2);

   snprintf(buf, buflen-1, SMTP_RESP_250_EXTENSIONS, session->cfg->hostid, extensions);

   send_smtp_response(session, buf);
}


int init_ssl(struct smtp_session *session){
#if OPENSSL_VERSION_NUMBER < 0x10100000L
   session->net.ctx = SSL_CTX_new(TLSv1_server_method());
#else
   session->net.ctx = SSL_CTX_new(TLS_server_method());
#endif

   if(session->net.ctx == NULL){
      syslog(LOG_PRIORITY, "SSL ctx is null");
      return 0;
   }

   if(SSL_CTX_set_cipher_list(session->net.ctx, session->cfg->cipher_list) == 0){
      syslog(LOG_PRIORITY, "failed to set cipher list: '%s'", session->cfg->cipher_list);
      return 0;
   }

   if(SSL_CTX_use_PrivateKey_file(session->net.ctx, session->cfg->pemfile, SSL_FILETYPE_PEM) != 1){
      syslog(LOG_PRIORITY, "cannot load private key from %s", session->cfg->pemfile);
      return 0;
   }

   if(SSL_CTX_use_certificate_file(session->net.ctx, session->cfg->pemfile, SSL_FILETYPE_PEM) != 1){
      syslog(LOG_PRIORITY, "cannot load certificate from %s", session->cfg->pemfile);
      return 0;
   }

   return 1;
}


void process_command_starttls(struct smtp_session *session){
   if(session->cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "starttls request from client");

   if(init_ssl(session) == 1){

      session->net.ssl = SSL_new(session->net.ctx);
      if(session->net.ssl){

         SSL_set_options(session->net.ssl, SSL_OP_NO_SSLv2|SSL_OP_NO_SSLv3);

         if(SSL_set_fd(session->net.ssl, session->net.socket) == 1){
            session->net.starttls = 1;
            send_smtp_response(session, SMTP_RESP_220_READY_TO_START_TLS);
            session->protocol_state = SMTP_STATE_INIT;

            if(session->net.starttls == 1 && session->net.use_ssl == 0)
               wait_for_ssl_accept(session);

            return;
         } syslog(LOG_PRIORITY, "%s: SSL_set_fd() failed", session->ttmpfile);
      } syslog(LOG_PRIORITY, "%s: SSL_new() failed", session->ttmpfile);
   } syslog(LOG_PRIORITY, "SSL ctx is null!");

   send_smtp_response(session, SMTP_RESP_454_ERR_TLS_TEMP_ERROR);
}


void process_command_mail_from(struct smtp_session *session, char *buf){
   if(session->protocol_state != SMTP_STATE_HELO && session->protocol_state != SMTP_STATE_PERIOD && session->protocol_state != SMTP_STATE_BDAT){
      send(session->net.socket, SMTP_RESP_503_ERR, strlen(SMTP_RESP_503_ERR), 0);
   }
   else {
      memset(&(session->ttmpfile[0]), 0, SMALLBUFSIZE);
      make_random_string(&(session->ttmpfile[0]), QUEUE_ID_LEN);
      session->protocol_state = SMTP_STATE_MAIL_FROM;

      extractEmail(buf, session->mailfrom);

      reset_bdat_counters(session);
      session->tot_len = 0;

      send_smtp_response(session, SMTP_RESP_250_OK);
   }
}


void process_command_rcpt_to(struct smtp_session *session, char *buf){

   if(session->protocol_state == SMTP_STATE_MAIL_FROM || session->protocol_state == SMTP_STATE_RCPT_TO){

      // For now, we are not interested in the envelope recipients

      session->protocol_state = SMTP_STATE_RCPT_TO;

      if(session->num_of_rcpt_to < MAX_RCPT_TO){
         extractEmail(buf, session->rcptto[session->num_of_rcpt_to]);
         session->num_of_rcpt_to++;
      }

      send_smtp_response(session, SMTP_RESP_250_OK);
   }
   else {
      send_smtp_response(session, SMTP_RESP_503_ERR);
   }
}


void process_command_data(struct smtp_session *session, struct config *cfg){
   session->tot_len = 0;

   if(session->protocol_state != SMTP_STATE_RCPT_TO){
      send_smtp_response(session, SMTP_RESP_503_ERR);
   }
   else {
      session->fd = open(session->ttmpfile, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP);
      if(session->fd == -1){
         syslog(LOG_PRIORITY, "%s: %s", ERR_OPEN_TMP_FILE, session->ttmpfile);
         send_smtp_response(session, SMTP_RESP_451_ERR);
      }
      else {
         session->protocol_state = SMTP_STATE_DATA;
         send_smtp_response(session, SMTP_RESP_354_DATA_OK);

         if(cfg->process_rcpt_to_addresses == 1) write_envelope_addresses(session, cfg);
      }
   }

}

void process_command_period(struct smtp_session *session){
   char buf[SMALLBUFSIZE];

   session->protocol_state = SMTP_STATE_PERIOD;

   // TODO: add some error handling

   fsync(session->fd);
   close(session->fd);

   session->fd = -1;

   syslog(LOG_PRIORITY, "received: %s, from=%s, size=%d, client=%s, fd=%d", session->ttmpfile, session->mailfrom, session->tot_len, session->remote_host, session->net.socket);

   move_email(session);

   snprintf(buf, sizeof(buf)-1, "250 OK <%s>\r\n", session->ttmpfile);

   session->buflen = 0;
   session->last_data_char = 0;
   memset(session->buf, 0, SMALLBUFSIZE);

   send_smtp_response(session, buf);
}


void process_command_quit(struct smtp_session *session, char *buf, int buflen){
   session->protocol_state = SMTP_STATE_FINISHED;

   snprintf(buf, buflen-1, SMTP_RESP_221_GOODBYE, session->cfg->hostid);

   send_smtp_response(session, buf);
}


void process_command_reset(struct smtp_session *session){
   send_smtp_response(session, SMTP_RESP_250_OK);

   session->tot_len = 0;
   session->fd = -1;
   session->protocol_state = SMTP_STATE_HELO;
   session->last_data_char = 0;

   reset_bdat_counters(session);

   memset(&(session->ttmpfile[0]), 0, SMALLBUFSIZE);
   make_random_string(&(session->ttmpfile[0]), QUEUE_ID_LEN);
}
