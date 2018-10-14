/*
 * store.c, SJ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <piler.h>
#include <zlib.h>
#include <openssl/blowfish.h>
#include <openssl/evp.h>
#include <errno.h>


int read_key(struct config *cfg){
   int fd, n;

   fd = open(KEYFILE, O_RDONLY);
   if(fd == -1){
      syslog(LOG_PRIORITY, "cannot read keyfile: %s", KEYFILE);
      return -1;
   }

   n = read(fd, cfg->key, KEYLEN);

   close(fd);

   if(n > 5) return 0;

   return 1;
}


int store_file(struct session_data *sdata, char *filename, int len, struct config *cfg){
   int ret=0, rc, fd, n;
   char *addr, *p, *p0, *p1, *p2, s[SMALLBUFSIZE];
   struct stat st;
   Bytef *z=NULL;
   uLongf dstlen;

#if OPENSSL_VERSION_NUMBER < 0x10100000L
   EVP_CIPHER_CTX ctx;
#else
   EVP_CIPHER_CTX *ctx;
#endif
   unsigned char *outbuf=NULL;
   int outlen=0, writelen, tmplen;

   struct timezone tz;
   struct timeval tv1, tv2;


   fd = open(filename, O_RDONLY);
   if(fd == -1){
      syslog(LOG_PRIORITY, "%s: cannot open: %s", sdata->ttmpfile, filename);
      return ret;
   }

   if(len == 0){
      if(fstat(fd, &st)){
         close(fd);
         return ret;
      }

      len = st.st_size;
      if(len == 0){
         close(fd);
         return 1;
      }
   }

   gettimeofday(&tv1, &tz);

   addr = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
   close(fd);

   if(addr == MAP_FAILED) return ret;

   dstlen = compressBound(len);

   z = malloc(dstlen);

   if(z == NULL){
      munmap(addr, len);
      syslog(LOG_PRIORITY, "%s: cannot malloc for z buffer", sdata->ttmpfile);
      return ret;
   }

   rc = compress(z, &dstlen, (const Bytef *)addr, len);
   gettimeofday(&tv2, &tz);
   sdata->__compress += tvdiff(tv2, tv1);

   munmap(addr, len);

   if(rc != Z_OK) goto ENDE;

   if(cfg->encrypt_messages == 1){
      gettimeofday(&tv1, &tz);

   #if OPENSSL_VERSION_NUMBER < 0x10100000L
      EVP_CIPHER_CTX_init(&ctx);
      EVP_EncryptInit_ex(&ctx, EVP_bf_cbc(), NULL, cfg->key, cfg->iv);
   #else
      ctx = EVP_CIPHER_CTX_new();
      if(!ctx) goto ENDE;

      EVP_CIPHER_CTX_init(ctx);
      EVP_EncryptInit_ex(ctx, EVP_bf_cbc(), NULL, cfg->key, cfg->iv);
   #endif

      outbuf = malloc(dstlen + EVP_MAX_BLOCK_LENGTH);
      if(outbuf == NULL) goto ENDE;

   #if OPENSSL_VERSION_NUMBER < 0x10100000L
      if(!EVP_EncryptUpdate(&ctx, outbuf, &outlen, z, dstlen)) goto ENDE;
      if(!EVP_EncryptFinal_ex(&ctx, outbuf + outlen, &tmplen)) goto ENDE;
   #else
      if(!EVP_EncryptUpdate(ctx, outbuf, &outlen, z, dstlen)) goto ENDE;
      if(!EVP_EncryptFinal_ex(ctx, outbuf + outlen, &tmplen)) goto ENDE;
   #endif
      outlen += tmplen;
   #if OPENSSL_VERSION_NUMBER < 0x10100000L
      EVP_CIPHER_CTX_cleanup(&ctx);
   #else
      EVP_CIPHER_CTX_free(ctx);
   #endif

      gettimeofday(&tv2, &tz);
      sdata->__encrypt += tvdiff(tv2, tv1);
   }

   /* create a filename in the store based on piler_id */

   p = strchr(filename, '.');
   if(p) *p = '\0';

   snprintf(s, sizeof(s)-1, "%s/%02x/%c%c%c/%c%c/%c%c/%s", cfg->queuedir, cfg->server_id, filename[8], filename[9], filename[10], filename[RND_STR_LEN-4], filename[RND_STR_LEN-3], filename[RND_STR_LEN-2], filename[RND_STR_LEN-1], filename);

   if(p){
      *p = '.';
      strncat(s, p, sizeof(s)-strlen(s)-1);
   }


   p0 = strrchr(s, '/'); if(!p0) goto ENDE;
   *p0 = '\0';

   if(stat(s, &st)){
      p1 = strrchr(s, '/'); if(!p1) goto ENDE;
      *p1 = '\0';
      p2 = strrchr(s, '/'); if(!p2) goto ENDE;
      *p2 = '\0';

      mkdir(s, 0750);
      *p2 = '/';
      mkdir(s, 0750);
      *p1 = '/';
      rc = mkdir(s, 0770); if(rc == -1) syslog(LOG_PRIORITY, "%s: mkdir %s: error=%s", sdata->ttmpfile, s, strerror(errno));
   }

   *p0 = '/';

   unlink(s);

   fd = open(s, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP);
   if(fd == -1){
      syslog(LOG_PRIORITY, "%s: cannot open: %s", sdata->ttmpfile, s);
      goto ENDE;
   }


   if(cfg->encrypt_messages == 1){
      n = write(fd, outbuf, outlen);
      writelen = outlen;
   }
   else {
      n = write(fd, z, dstlen);
      writelen = dstlen;
   }

   if(n > 0 && n == writelen){
      ret = 1;
      sdata->stored_len += writelen;
      if(cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "%s: stored '%s' %d/%d bytes", sdata->ttmpfile, filename, len, writelen);
   }
   else {
      syslog(LOG_PRIORITY, "%s: cannot write %d bytes (only %d)", sdata->ttmpfile, writelen, n);
   }

   fsync(fd);

   close(fd);

   if(ret == 0){
      unlink(s);
   }


ENDE:
   if(outbuf) free(outbuf);
   if(z) free(z);

   return ret;
}


int remove_stored_message_files(struct session_data *sdata, struct parser_state *state, struct config *cfg){
   char s[SMALLBUFSIZE];

   if(state->n_attachments > 0){
      for(int i=1; i<=state->n_attachments; i++){
         snprintf(s, sizeof(s)-1, "%s/%02x/%c%c%c/%c%c/%c%c/%s.a%d", cfg->queuedir, cfg->server_id, sdata->ttmpfile[8], sdata->ttmpfile[9], sdata->ttmpfile[10], sdata->ttmpfile[RND_STR_LEN-4], sdata->ttmpfile[RND_STR_LEN-3], sdata->ttmpfile[RND_STR_LEN-2], sdata->ttmpfile[RND_STR_LEN-1], sdata->ttmpfile, i);

         if(cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "%s: unlinking %s", sdata->ttmpfile, s);

         unlink(s);
      }
   }

   snprintf(s, sizeof(s)-1, "%s/%02x/%c%c%c/%c%c/%c%c/%s.m", cfg->queuedir, cfg->server_id, sdata->ttmpfile[8], sdata->ttmpfile[9], sdata->ttmpfile[10], sdata->ttmpfile[RND_STR_LEN-4], sdata->ttmpfile[RND_STR_LEN-3], sdata->ttmpfile[RND_STR_LEN-2], sdata->ttmpfile[RND_STR_LEN-1], sdata->ttmpfile);

   if(cfg->verbosity >= _LOG_DEBUG) syslog(LOG_PRIORITY, "%s: unlinking %s", sdata->ttmpfile, s);

   unlink(s);

   return 0;
}


