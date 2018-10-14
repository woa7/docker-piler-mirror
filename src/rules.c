/*
 * rules.c, SJ
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <piler.h>
#include "rules.h"


void load_rules(struct session_data *sdata, struct node *xhash[], char *table){
   char s[SMALLBUFSIZE];
   struct rule_cond rule_cond;
   struct sql sql;

   memset(rule_cond.domain, 0, SMALLBUFSIZE);
   memset(rule_cond.from, 0, SMALLBUFSIZE);
   memset(rule_cond.to, 0, SMALLBUFSIZE);
   memset(rule_cond.subject, 0, SMALLBUFSIZE);
   memset(rule_cond.body, 0, SMALLBUFSIZE);
   memset(rule_cond._size, 0, SMALLBUFSIZE);
   memset(rule_cond.attachment_name, 0, SMALLBUFSIZE);
   memset(rule_cond.attachment_type, 0, SMALLBUFSIZE);
   memset(rule_cond._attachment_size, 0, SMALLBUFSIZE);

   rule_cond.size = rule_cond.attachment_size = rule_cond.spam = rule_cond.days = rule_cond.folder_id = 0;

   snprintf(s, sizeof(s)-1, "SELECT `domain`, `from`, `to`, `subject`, `body`, `_size`, `size`, `attachment_name`, `attachment_type`, `_attachment_size`, `attachment_size`, `spam`, `days`, `folder_id` FROM `%s`", table);

   if(prepare_sql_statement(sdata, &sql, s) == ERR) return;


   p_bind_init(&sql);

   if(p_exec_stmt(sdata, &sql) == OK){

      p_bind_init(&sql);

      sql.sql[sql.pos] = &rule_cond.domain[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.domain)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond.from[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.from)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond.to[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.to)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond.subject[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.subject)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond.body[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.body)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond._size[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond._size)-2; sql.pos++;
      sql.sql[sql.pos] = (char *)&rule_cond.size; sql.type[sql.pos] = TYPE_LONG; sql.len[sql.pos] = sizeof(rule_cond.size); sql.pos++;
      sql.sql[sql.pos] = &rule_cond.attachment_name[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.attachment_name)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond.attachment_type[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond.attachment_type)-2; sql.pos++;
      sql.sql[sql.pos] = &rule_cond._attachment_size[0]; sql.type[sql.pos] = TYPE_STRING; sql.len[sql.pos] = sizeof(rule_cond._attachment_size)-2; sql.pos++;
      sql.sql[sql.pos] = (char *)&rule_cond.attachment_size; sql.type[sql.pos] = TYPE_LONG; sql.len[sql.pos] = sizeof(rule_cond.attachment_size); sql.pos++;
      sql.sql[sql.pos] = (char *)&rule_cond.spam; sql.type[sql.pos] = TYPE_LONG; sql.len[sql.pos] = sizeof(rule_cond.spam); sql.pos++;
      sql.sql[sql.pos] = (char *)&rule_cond.days; sql.type[sql.pos] = TYPE_LONG; sql.len[sql.pos] = sizeof(rule_cond.days); sql.pos++;
      sql.sql[sql.pos] = (char *)&rule_cond.folder_id; sql.type[sql.pos] = TYPE_LONG; sql.len[sql.pos] = sizeof(rule_cond.folder_id); sql.pos++;


      p_store_results(&sql);

      while(p_fetch_results(&sql) == OK){
         append_rule(xhash, &rule_cond);

         memset(rule_cond.domain, 0, SMALLBUFSIZE);
         memset(rule_cond.from, 0, SMALLBUFSIZE);
         memset(rule_cond.to, 0, SMALLBUFSIZE);
         memset(rule_cond.subject, 0, SMALLBUFSIZE);
         memset(rule_cond.body, 0, SMALLBUFSIZE);
         memset(rule_cond._size, 0, SMALLBUFSIZE);
         memset(rule_cond.attachment_name, 0, SMALLBUFSIZE);
         memset(rule_cond.attachment_type, 0, SMALLBUFSIZE);
         memset(rule_cond._attachment_size, 0, SMALLBUFSIZE);

         rule_cond.size = rule_cond.attachment_size = rule_cond.spam = rule_cond.days = rule_cond.folder_id = 0;
      }

      p_free_results(&sql);

   }

   close_prepared_statement(&sql);
}


int append_rule(struct node *xhash[], struct rule_cond *rule_cond){
   struct node *q, *Q=NULL, *node;
   struct rule *rule;
   int rc=0;

   if((node = malloc(sizeof(struct node))) == NULL) return rc;

   memset(node, 0, sizeof(struct node));
   node->r = NULL;

   rule = create_rule_item(rule_cond);

   if(rule == NULL){
      free(node);
      syslog(LOG_INFO, "could not load rule=%s/%s/%s/%s/%s/%s,%d", rule_cond->domain, rule_cond->from, rule_cond->to, rule_cond->subject, rule_cond->body, rule_cond->_size, rule_cond->size);
      return rc;
   }

   node->str = rule;

   q = xhash[0];

   while(q != NULL){
      Q = q;
      q = q->r;
   }

   if(Q == NULL) xhash[0] = node;
   else {
      Q->r = node;
   }

   rc = 1;

   return rc;
}


struct rule *create_rule_item(struct rule_cond *rule_cond){
   struct rule *h=NULL;
   char empty = '\0';
   int len;

   if(rule_cond == NULL) return NULL;

   if((h = malloc(sizeof(struct rule))) == NULL)
      return NULL;


   h->compiled = 1;

   h->domain = NULL;
   h->domainlen = strlen(rule_cond->domain);

   if(h->domainlen > 2){
      h->domain = malloc(h->domainlen+2);
      if(h->domain) snprintf(h->domain, h->domainlen, "%s", rule_cond->domain);
      else {
         h->compiled = 0;
         syslog(LOG_INFO, "malloc error in create_rule_item() for '%s'", rule_cond->domain);
      }
   }

   h->emptyfrom = h->emptyto = h->emptysubject = h->emptyaname = h->emptyatype = 0;


   if(rule_cond->from == NULL || strlen(rule_cond->from) < 1){ rule_cond->from[0] = empty; h->emptyfrom = 1; }
   if(regcomp(&(h->from), rule_cond->from, REG_ICASE | REG_EXTENDED)) h->compiled = 0;

   if(rule_cond->to == NULL || strlen(rule_cond->to) < 1){ rule_cond->to[0] = empty; h->emptyto = 1; }
   if(regcomp(&(h->to), rule_cond->to, REG_ICASE | REG_EXTENDED)) h->compiled = 0;

   if(rule_cond->subject == NULL || strlen(rule_cond->subject) < 1){ rule_cond->subject[0] = empty; h->emptysubject = 1; }
   if(regcomp(&(h->subject), rule_cond->subject, REG_ICASE | REG_EXTENDED)) h->compiled = 0;

   if(rule_cond->body == NULL || strlen(rule_cond->body) < 1){ rule_cond->body[0] = empty; h->emptybody = 1; }
   if(regcomp(&(h->body), rule_cond->body, REG_ICASE | REG_EXTENDED)) h->compiled = 0;

   h->spam = rule_cond->spam;
   h->days = rule_cond->days;
   h->folder_id = rule_cond->folder_id;

   h->size = rule_cond->size;

   if(rule_cond->_size == NULL) rule_cond->_size[0] = empty;
   snprintf(h->_size, 3, "%s", rule_cond->_size);

   if(rule_cond->attachment_name == NULL || strlen(rule_cond->attachment_name) < 1){ rule_cond->attachment_name[0] = empty; h->emptyaname = 1; }
   if(regcomp(&(h->attachment_name), rule_cond->attachment_name, REG_ICASE | REG_EXTENDED)) h->compiled = 0;

   if(rule_cond->attachment_type == NULL || strlen(rule_cond->attachment_type) < 1){ rule_cond->attachment_type[0] = empty; h->emptyatype = 1; }
   if(regcomp(&(h->attachment_type), rule_cond->attachment_type, REG_ICASE | REG_EXTENDED)) h->compiled = 0;


   h->attachment_size = rule_cond->attachment_size;

   if(rule_cond->_attachment_size == NULL) rule_cond->_attachment_size[0] = empty;
   snprintf(h->_attachment_size, 3, "%s", rule_cond->_attachment_size);

   len = strlen(rule_cond->domain)+8 + strlen(rule_cond->from)+6 + strlen(rule_cond->to)+4 + strlen(rule_cond->subject)+9 + strlen(rule_cond->body)+6 + strlen(rule_cond->_size)+6 + strlen(rule_cond->attachment_name)+10 + strlen(rule_cond->attachment_type)+10 + strlen(rule_cond->_attachment_size)+10 + 9 + 15 + 15;
   h->rulestr = malloc(len);



   if(h->rulestr){
      snprintf(h->rulestr, len-1, "domain=%s,from=%s,to=%s,subject=%s,body=%s,size%s%d,att.name=%s,att.type=%s,att.size%s%d,spam=%d", rule_cond->domain, rule_cond->from, rule_cond->to, rule_cond->subject, rule_cond->body, rule_cond->_size, rule_cond->size, rule_cond->attachment_name, rule_cond->attachment_type, rule_cond->_attachment_size, rule_cond->attachment_size, rule_cond->spam);
      syslog(LOG_INFO, "adding rule: %s", h->rulestr);
   }
   else h->compiled = 0;

   h->r = NULL;

   return h;
}


char *check_againt_ruleset(struct node *xhash[], struct parser_state *state, int size, int spam){
   size_t nmatch=0;
   struct rule *p;
   struct node *q;
   int ismatch;

   q = xhash[0];

   while(q != NULL){

      if(q->str){
         p = q->str;

         if(p){
            ismatch = 0;

            ismatch += check_spam_rule(spam, p->spam);
            ismatch += check_size_rule(size, p->size, p->_size);
            ismatch += check_attachment_rule(state, p);

            if(p->compiled == 1){
               if(p->emptyfrom == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->from), state->b_from, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptyto == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->to), state->b_to, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptysubject == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->subject), state->b_subject, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptybody == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->body), state->b_body, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

            }

            if(ismatch > 0){
               return p->rulestr;
            }

         }
      }

      q = q->r;
   }

   return NULL;
}


time_t query_retain_period(struct data *data, struct parser_state *state, int size, int spam, struct config *cfg){
   size_t nmatch=0;
   struct rule *p;
   struct node *q;
   int ismatch;

   q = data->retention_rules[0];

   while(q != NULL){

      if(q->str){
         p = q->str;

         ismatch = 0;

         if(p->domainlen > 2){
            if(strcasestr(state->b_to_domain, p->domain) || strcasestr(state->b_from_domain, p->domain)){
               state->retention = p->days;
               return (time_t)p->days * (time_t)86400;
            }
         }
         else {

            ismatch += check_spam_rule(spam, p->spam);
            ismatch += check_size_rule(size, p->size, p->_size);
            ismatch += check_attachment_rule(state, p);

            if(p->compiled == 1){
               if(p->emptyfrom == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->from), state->b_from, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptyto == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->to), state->b_to, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptysubject == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->subject), state->b_subject, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptybody == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->body), state->b_body, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

            }

            if(ismatch > 0){
               state->retention = p->days;
               return (time_t)p->days * (time_t)86400;
            }
         }

      }

      q = q->r;
   }


   state->retention = cfg->default_retention_days;

   return (time_t)cfg->default_retention_days * (time_t)86400;
}


int get_folder_id_by_rule(struct data *data, struct parser_state *state, int size, int spam, struct config *cfg){
   size_t nmatch=0;
   struct rule *p;
   struct node *q;
   int ismatch;

   if(cfg->enable_folders == 0) return 0;

   q = data->folder_rules[0];

   while(q != NULL){

      if(q->str){
         p = q->str;

         ismatch = 0;

         if(p->domainlen > 2){
            if(strcasestr(state->b_to_domain, p->domain) || strcasestr(state->b_from_domain, p->domain)){
               return p->folder_id;
            }
         }
         else {

            ismatch += check_spam_rule(spam, p->spam);
            ismatch += check_size_rule(size, p->size, p->_size);
            ismatch += check_attachment_rule(state, p);

            if(p->compiled == 1){
               if(p->emptyfrom == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->from), state->b_from, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptyto == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->to), state->b_to, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptysubject == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->subject), state->b_subject, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

               if(p->emptybody == 1){
                  ismatch += RULE_UNDEF;
               }
               else if(regexec(&(p->body), state->b_body, nmatch, NULL, 0) == 0) ismatch += RULE_MATCH; else ismatch += RULE_NO_MATCH;

            }

            if(ismatch > 0){
               return p->folder_id;
            }
         }

      }

      q = q->r;
   }

   return 0; // default folder_id
}


int check_size_rule(int message_size, int size, char *_size){
   if(size <= 0) return RULE_UNDEF;

   if(strcmp(_size, ">") == 0 && message_size > size) return RULE_MATCH;
   if(strcmp(_size, "<") == 0 && message_size < size) return RULE_MATCH;
   if(strcmp(_size, "=") == 0 && message_size == size) return RULE_MATCH;
   if( (strcmp(_size, "<>") == 0 || strcmp(_size, "!=") == 0) && message_size != size) return RULE_MATCH;

   return RULE_NO_MATCH;
}


int check_spam_rule(int is_spam, int spam){
   if(spam == -1) return RULE_UNDEF;
   if(is_spam == spam) return RULE_MATCH;
   return RULE_NO_MATCH;
}


int check_attachment_rule(struct parser_state *state, struct rule *rule){
   int i;
   size_t nmatch=0;

   if(state->n_attachments == 0) return RULE_UNDEF;

   if(rule->emptyaname == 1 && rule->emptyatype == 1) return RULE_UNDEF;

   for(i=1; i<=state->n_attachments; i++){
      int ismatch = 0;

      if(rule->emptyaname == 0){
         if(regexec(&(rule->attachment_name), state->attachments[i].filename, nmatch, NULL, 0) == 0)
            ismatch += RULE_MATCH;
         else
            ismatch += RULE_NO_MATCH;
      }

      if(rule->emptyatype == 0){
         if(regexec(&(rule->attachment_type), state->attachments[i].type, nmatch, NULL, 0) == 0)
            ismatch += RULE_MATCH;
         else
            ismatch += RULE_NO_MATCH;
      }

      ismatch += check_size_rule(state->attachments[i].size, rule->attachment_size, rule->_attachment_size);

      if(ismatch > 0) return RULE_MATCH;
   }

   return RULE_NO_MATCH;
}


void initrules(struct node *xhash[]){
   xhash[0] = NULL;
}


void clearrules(struct node *xhash[]){
   struct node *q;
   struct rule *rule;

   q = xhash[0];

   while(q != NULL){
      struct node *p = q;
      q = q->r;

      if(p){
         if(p->str){
            rule = (struct rule*)p->str;

            regfree(&(rule->from));
            regfree(&(rule->to));
            regfree(&(rule->subject));
            regfree(&(rule->body));
            regfree(&(rule->attachment_name));
            regfree(&(rule->attachment_type));

            free(rule->rulestr);

            if(rule->domain) free(rule->domain);

            free(rule);
         }
         free(p);
      }
   }

   xhash[0] = NULL;
}

