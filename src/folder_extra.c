/*
 * folder_extra.c, SJ
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


int get_folder_extra_id(struct session_data *sdata, struct __data *data, char *foldername){
   int id=ERR_FOLDER;

   if(prepare_sql_statement(sdata, &(data->stmt_get_folder_id), SQL_PREPARED_STMT_GET_FOLDER_EXTRA_ID) == ERR) return id;

   p_bind_init(data);
   data->sql[data->pos] = foldername; data->type[data->pos] = TYPE_STRING; data->pos++;
   data->sql[data->pos] = (char *)&(data->import->uid); data->type[data->pos] = TYPE_LONG; data->pos++;

   if(p_exec_query(sdata, data->stmt_get_folder_id, data) == OK){

      p_bind_init(data);
      data->sql[data->pos] = (char *)&id; data->type[data->pos] = TYPE_LONG; data->len[data->pos] = sizeof(unsigned long); data->pos++;

      p_store_results(data->stmt_get_folder_id, data);
      p_fetch_results(data->stmt_get_folder_id);
      p_free_results(data->stmt_get_folder_id);
   }

   close_prepared_statement(data->stmt_get_folder_id);

   return id;
}


int add_new_folder_extra(struct session_data *sdata, struct __data *data, char *foldername){
   int id=ERR_FOLDER;

   if(foldername == NULL) return id;

   if(prepare_sql_statement(sdata, &(data->stmt_insert_into_folder_table), SQL_PREPARED_STMT_INSERT_INTO_FOLDER_EXTRA_TABLE) == ERR) return id;

   p_bind_init(data);
   data->sql[data->pos] = foldername; data->type[data->pos] = TYPE_STRING; data->pos++;
   data->sql[data->pos] = (char *)&(data->import->uid); data->type[data->pos] = TYPE_LONG; data->pos++;

   if(p_exec_query(sdata, data->stmt_insert_into_folder_table, data) == OK){
      id = p_get_insert_id(data->stmt_insert_into_folder_table);
   }

   close_prepared_statement(data->stmt_insert_into_folder_table);

   return id;
}
