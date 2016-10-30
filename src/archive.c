/*
 * archive.c, SJ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <syslog.h>
#include <openssl/blowfish.h>
#include <openssl/evp.h>
#include <zlib.h>
#include <assert.h>
#include <piler.h>

#ifdef HAVE_ZSTD
   #include <zstd.h>
#endif



void zerr(int ret){

   fputs("zpipe: ", stderr);

   switch (ret) {
      case Z_ERRNO:
                    fputs("I/O error\n", stderr);
                    break;

      case Z_STREAM_ERROR:
                    fputs("invalid compression level\n", stderr);
                    break;


      case Z_DATA_ERROR:
                    fputs("invalid or incomplete deflate data\n", stderr);
                    break;

      case Z_MEM_ERROR:
                    fputs("out of memory\n", stderr);
                    break;

      case Z_VERSION_ERROR:
                    fputs("zlib version mismatch!\n", stderr);
                    break;

   }
}


int inf(unsigned char *in, int len, int mode, char **buffer, FILE *dest){
   int ret, pos=0;
   unsigned have;
   z_stream strm;
   char *new_ptr;
   unsigned char out[REALLYBIGBUFSIZE];

   /* allocate inflate state */

   strm.zalloc = Z_NULL;
   strm.zfree = Z_NULL;
   strm.opaque = Z_NULL;
   strm.avail_in = 0;
   strm.next_in = Z_NULL;
   ret = inflateInit(&strm);


   if(ret != Z_OK) return ret;

   strm.avail_in = len;
   strm.next_in = in;

   if(mode == WRITE_TO_BUFFER){
      *buffer = malloc(REALLYBIGBUFSIZE);
      if(!*buffer) return Z_MEM_ERROR;
      memset(*buffer, 0, REALLYBIGBUFSIZE);
   }


   do {
      strm.avail_out = REALLYBIGBUFSIZE;
      strm.next_out = out;
      ret = inflate(&strm, Z_NO_FLUSH);

      assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
      switch (ret) {
         case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
         case Z_DATA_ERROR:
         case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
      }

      have = REALLYBIGBUFSIZE - strm.avail_out;

      /*
       * write the uncompressed result either to stdout
       * or to the buffer
       */

      if(mode == WRITE_TO_STDOUT){
         if(fwrite(out, 1, have, dest) != have){
            (void)inflateEnd(&strm);
            return Z_ERRNO;
         }
      }
      else {
         memcpy(*buffer+pos, out, have);
         pos += have;
         new_ptr = realloc(*buffer, pos+REALLYBIGBUFSIZE);
         if(!new_ptr){
            (void)inflateEnd(&strm);
            return Z_MEM_ERROR;
         }
         *buffer = new_ptr;
         memset(*buffer+pos, 0, REALLYBIGBUFSIZE);
      }


   } while (strm.avail_out == 0);


   (void)inflateEnd(&strm);

   return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


#ifdef HAVE_ZSTD
int zstd_inf(unsigned char *in, int len, int mode, char **buffer, FILE *dest){
   size_t dSize;
   unsigned long long const rSize = ZSTD_getDecompressedSize(in, len);

   if(rSize == 0)
      return Z_DATA_ERROR;

   if(mode == WRITE_TO_STDOUT){
      void* const rBuff = malloc((size_t)rSize);
      if(!rBuff) return Z_MEM_ERROR;

      dSize = ZSTD_decompress(rBuff, rSize, in, len);

      if(dSize != rSize){
         printf("error decoding buffer: %s\n", ZSTD_getErrorName(dSize));
         return Z_DATA_ERROR;
      }

      fwrite(rBuff, 1, dSize, dest);

      free(rBuff);
   }
   else {
      *buffer = malloc((size_t)rSize);
      if(!*buffer) return Z_MEM_ERROR;
      memset(*buffer, 0, rSize);

      dSize = ZSTD_decompress(*buffer, rSize, in, len);
      if(dSize != rSize){
         printf("error decoding buffer: %s\n", ZSTD_getErrorName(dSize));
         return Z_DATA_ERROR;
      }
   }

   return Z_OK;
}
#endif


int retrieve_file_from_archive(char *filename, int mode, char **buffer, FILE *dest, struct __config *cfg){
   int rc=0, n, olen, tlen, len, fd=-1;
   unsigned char *s=NULL, *addr=NULL, inbuf[REALLYBIGBUFSIZE];
   struct stat st;
   EVP_CIPHER_CTX ctx;
#ifdef HAVE_ZSTD
   unsigned long magic;
#endif

   if(filename == NULL) return 1;


   fd = open(filename, O_RDONLY);
   if(fd == -1){
      syslog(LOG_PRIORITY, "%s: cannot open()", filename);
      return 1;
   }


   if(fstat(fd, &st)){
      syslog(LOG_PRIORITY, "%s: cannot fstat()", filename);
      close(fd);
      return 1;
   }


   if(cfg->encrypt_messages == 1){
      EVP_CIPHER_CTX_init(&ctx);
      EVP_DecryptInit_ex(&ctx, EVP_bf_cbc(), NULL, cfg->key, cfg->iv);

      len = st.st_size+EVP_MAX_BLOCK_LENGTH;

      s = malloc(len);

      if(!s){
         printf("malloc()\n");
         goto CLEANUP;
      }

      tlen = 0;

      while((n = read(fd, inbuf, sizeof(inbuf)))){

         if(!EVP_DecryptUpdate(&ctx, s+tlen, &olen, inbuf, n)){
            syslog(LOG_PRIORITY, "%s: EVP_DecryptUpdate()", filename);
            goto CLEANUP;
         }

         tlen += olen;
      }


      if(EVP_DecryptFinal(&ctx, s + tlen, &olen) != 1){
         syslog(LOG_PRIORITY, "%s: EVP_DecryptFinal()", filename);
         goto CLEANUP;
      }


      tlen += olen;

      if(st.st_size > 4){
      #ifdef HAVE_ZSTD
         memcpy(&magic, addr, 4);

         if(magic == ZSTD_DETECTED_MAGICNUMBER){
            rc = zstd_inf(s, tlen, mode, buffer, dest);
         }
         else {
      #endif
            rc = inf(s, tlen, mode, buffer, dest);
      #ifdef HAVE_ZSTD
         }
      #endif
      }
   }
   else {
      addr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

      if(st.st_size > 4){
      #ifdef HAVE_ZSTD
         memcpy(&magic, addr, 4);

         if(magic == ZSTD_DETECTED_MAGICNUMBER){
            rc = zstd_inf(addr, st.st_size, mode, buffer, dest);
         }
         else {
      #endif
            rc = inf(addr, st.st_size, mode, buffer, dest);
      #ifdef HAVE_ZSTD
         }
      #endif
      }

      munmap(addr, st.st_size);
   }


   if(rc != Z_OK) zerr(rc);


CLEANUP:
   if(fd != -1) close(fd);
   if(s) free(s);
   if(cfg->encrypt_messages == 1) EVP_CIPHER_CTX_cleanup(&ctx);

   return 0;
}


int retrieve_email_from_archive(struct session_data *sdata, struct __data *data, FILE *dest, struct __config *cfg){
   int i, attachments;
   char *buffer=NULL, *saved_buffer, *p, filename[SMALLBUFSIZE], pointer[SMALLBUFSIZE];
   struct ptr_array ptr_arr[MAX_ATTACHMENTS];
#ifdef HAVE_SUPPORT_FOR_COMPAT_STORAGE_LAYOUT
   struct stat st;
#endif

   if(strlen(sdata->ttmpfile) != RND_STR_LEN){
      printf("invalid piler-id: %s\n", sdata->ttmpfile);
      return 1;
   }

   attachments = query_attachments(sdata, data, &ptr_arr[0]);

   if(attachments == -1){
      printf("problem querying the attachment of %s\n", sdata->ttmpfile);
      return 1;
   }

   snprintf(filename, sizeof(filename)-1, "%s/%02x/%c%c%c/%c%c/%c%c/%s.m", cfg->queuedir, cfg->server_id, *(sdata->ttmpfile+8), *(sdata->ttmpfile+9), *(sdata->ttmpfile+10), *(sdata->ttmpfile+RND_STR_LEN-4), *(sdata->ttmpfile+RND_STR_LEN-3), *(sdata->ttmpfile+RND_STR_LEN-2), *(sdata->ttmpfile+RND_STR_LEN-1), sdata->ttmpfile);
#ifdef HAVE_SUPPORT_FOR_COMPAT_STORAGE_LAYOUT
   if(stat(filename, &st)){
      snprintf(filename, sizeof(filename)-1, "%s/%02x/%c%c/%c%c/%c%c/%s.m", cfg->queuedir, cfg->server_id, *(sdata->ttmpfile+RND_STR_LEN-6), *(sdata->ttmpfile+RND_STR_LEN-5), *(sdata->ttmpfile+RND_STR_LEN-4), *(sdata->ttmpfile+RND_STR_LEN-3), *(sdata->ttmpfile+RND_STR_LEN-2), *(sdata->ttmpfile+RND_STR_LEN-1), sdata->ttmpfile);
   }
#endif

   if(attachments == 0){
      retrieve_file_from_archive(filename, WRITE_TO_STDOUT, &buffer, dest, cfg);
   }
   else {
      retrieve_file_from_archive(filename, WRITE_TO_BUFFER, &buffer, dest, cfg);

      if(buffer){
         saved_buffer = buffer;

         for(i=1; i<=attachments; i++){
            snprintf(pointer, sizeof(pointer)-1, "ATTACHMENT_POINTER_%s.a%d_XXX_PILER", sdata->ttmpfile, i);

            p = strstr(buffer, pointer);
            if(p){
               *p = '\0';
               fwrite(buffer, 1, p - buffer, dest);
               buffer = p + strlen(pointer);

               if(strlen(ptr_arr[i].piler_id) == RND_STR_LEN){
                  snprintf(filename, sizeof(filename)-1, "%s/%02x/%c%c%c/%c%c/%c%c/%s.a%d", cfg->queuedir, cfg->server_id, ptr_arr[i].piler_id[8], ptr_arr[i].piler_id[9], ptr_arr[i].piler_id[10], ptr_arr[i].piler_id[RND_STR_LEN-4], ptr_arr[i].piler_id[RND_STR_LEN-3], ptr_arr[i].piler_id[RND_STR_LEN-2], ptr_arr[i].piler_id[RND_STR_LEN-1], ptr_arr[i].piler_id, ptr_arr[i].attachment_id);

               #ifdef HAVE_SUPPORT_FOR_COMPAT_STORAGE_LAYOUT
                  if(stat(filename, &st)){
                     snprintf(filename, sizeof(filename)-1, "%s/%02x/%c%c/%c%c/%c%c/%s.a%d", cfg->queuedir, cfg->server_id, ptr_arr[i].piler_id[RND_STR_LEN-6], ptr_arr[i].piler_id[RND_STR_LEN-5], ptr_arr[i].piler_id[RND_STR_LEN-4], ptr_arr[i].piler_id[RND_STR_LEN-3], ptr_arr[i].piler_id[RND_STR_LEN-2], ptr_arr[i].piler_id[RND_STR_LEN-1], ptr_arr[i].piler_id, ptr_arr[i].attachment_id);
                  }
               #endif

                  retrieve_file_from_archive(filename, WRITE_TO_STDOUT, NULL, dest, cfg);
               }
            }

         }

         if(buffer){
            fwrite(buffer, 1, strlen(buffer), dest);
         }

         buffer = saved_buffer;
         free(buffer);
      }
   }

   return 0;
}


