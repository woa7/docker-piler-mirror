/*
 * import.c, SJ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <locale.h>
#include <syslog.h>
#include <piler.h>


int import_message(char *filename, struct session_data *sdata, struct __data *data, struct __config *cfg){
   int rc=ERR;
   char *rule;
   struct stat st;
   struct parser_state state;
   struct __counters counters;


   init_session_data(sdata, cfg);

   if(data->import->extra_recipient){
      snprintf(sdata->rcptto[0], SMALLBUFSIZE-1, "%s", data->import->extra_recipient);
      sdata->num_of_rcpt_to = 1;
   }

   if(cfg->verbosity > 1) printf("processing: %s\n", filename);

   if(strcmp(filename, "-") == 0){

      if(read_from_stdin(sdata) == ERR){
         printf("error reading from stdin\n");
         return rc;
      }

      snprintf(sdata->filename, SMALLBUFSIZE-1, "%s", sdata->ttmpfile);

   }
   else {

      if(stat(filename, &st) != 0){
         printf("cannot stat() %s\n", filename);
         return rc;
      }

      if(S_ISREG(st.st_mode) == 0){
         printf("%s is not a file\n", filename);
         return rc;
      }

      snprintf(sdata->filename, SMALLBUFSIZE-1, "%s", filename);

      sdata->tot_len = st.st_size;
   }


   if(sdata->tot_len < cfg->min_message_size){
      printf("%s is too short: %d bytes\n", sdata->filename, sdata->tot_len);
      return rc;
   }

   data->import->total_size += sdata->tot_len;

   
   sdata->delivered = 0;

   sdata->import = 1;

   state = parse_message(sdata, 1, data, cfg);
   post_parse(sdata, &state, cfg);

   rule = check_againt_ruleset(data->archiving_rules, &state, sdata->tot_len, sdata->spam_message);

   if(rule){
      if(data->quiet == 0) printf("discarding %s by archiving policy: %s\n", filename, rule);
      rc = OK;
   }
   else {
      make_digests(sdata, cfg);

      if(sdata->hdr_len < 10){
         printf("%s: invalid message, hdr_len: %d\n", filename, sdata->hdr_len);
         return ERR;
      }

      if(data->import->reimport == 1)
         rc = reimport_message(sdata, &state, data, cfg);
      else
         rc = process_message(sdata, &state, data, cfg);

      /*
       * if pilerimport was invoked with --email (then queried the matching uid!),
       * and this is a duplicate, then add it to the folder_extra table
       */

      if(rc == ERR_EXISTS && data->import->uid > 0){
         store_folder_id(sdata, data, sdata->duplicate_id);
      }

      unlink(state.message_id_hash);
   }

   unlink(sdata->tmpframe);

   if(strcmp(filename, "-") == 0) unlink(sdata->ttmpfile);


   switch(rc) {
      case OK:
                        if(data->import->reimport == 0){
                           bzero(&counters, sizeof(counters));
                           counters.c_rcvd = 1;
                           counters.c_size += sdata->tot_len;
                           counters.c_stored_size = sdata->stored_len;
                           update_counters(sdata, data, &counters, cfg);
                        }

                        break;

      case ERR_EXISTS:
                        rc = OK;

                        bzero(&counters, sizeof(counters));
                        counters.c_duplicate = 1;
                        update_counters(sdata, data, &counters, cfg);

                        if(data->quiet == 0) printf("duplicate: %s (duplicate id: %llu)\n", filename, sdata->duplicate_id);
                        break;

      default:
                        printf("failed to import: %s (id: %s)\n", filename, sdata->ttmpfile);
                        break;
   } 

   return rc;
}


void update_import_job_stat(struct session_data *sdata, struct __data *data){
   char buf[SMALLBUFSIZE];

   snprintf(buf, sizeof(buf)-1, "update import set status=%d, started=%ld, updated=%ld, finished=%ld, total=%d, imported=%d where id=%d", data->import->status, data->import->started, data->import->updated, data->import->finished, data->import->total_messages, data->import->processed_messages, data->import->import_job_id);

   p_query(sdata, buf);
}



