<?php


class ControllerMessageRestore extends Controller {

   public function index(){

      $this->id = "content";
      $this->template = "message/restore.tpl";
      $this->layout = "common/layout-empty";

      $session = Registry::get('session');
      $request = Registry::get('request');
      $db = Registry::get('db');

      $this->load->model('search/search');
      $this->load->model('search/message');
      $this->load->model('audit/audit');
      $this->load->model('user/user');
      $this->load->model('mail/mail');

      $this->document->title = $this->data['text_message'];

      $this->data['id'] = @$this->request->get['id'];

      $rcpt = array();

      if(RESTORE_OVER_IMAP == 1) {
         require_once 'Zend/Mail/Protocol/Imap.php';
         require_once 'Zend/Mail/Storage/Imap.php';
      }


      if(Registry::get('auditor_user') == 1) {
         $this->data['id'] = @$this->request->post['id'];
         $this->request->post['rcpt'] = urldecode($this->request->post['rcpt']);
         $rcpt = preg_split("/\s/", $this->request->post['rcpt']);
      }


      if(!$this->model_audit_audit->can_restore()) {
         die("cannot restore at the moment");
      }

      if(!verify_piler_id($this->data['id'])) {
         AUDIT(ACTION_UNKNOWN, '', '', $this->data['id'], 'unknown piler id: ' . $this->data['id']);
         die("invalid id: " . $this->data['id']);
      }

      if(!$this->model_search_search->check_your_permission_by_id($this->data['id'])) {
         AUDIT(ACTION_UNAUTHORIZED_VIEW_MESSAGE, '', '', $this->data['id'], '');
         die("no permission for " . $this->data['id']);
      }

      AUDIT(ACTION_RESTORE_MESSAGE, '', '', $this->data['id'], '');


      $this->data['username'] = Registry::get('username');


      /* send the email to all the recipients of the original email if we are admin or auditor users */

      if(Registry::get('auditor_user') == 0) {
         array_push($rcpt, $session->get("email"));
      }

      $this->data['data'] = $this->data['text_failed_to_restore'];

      if(count($rcpt) > 0) {
         $this->data['meta'] = $this->model_search_message->get_metadata_by_id($this->data['id']);
         $this->data['piler_id'] = $this->data['meta']['piler_id'];

         $msg = $this->model_search_message->get_raw_message($this->data['piler_id']);

         $this->model_search_message->remove_journal($msg);

         if(RESTORE_OVER_IMAP == 1) {
            if($this->model_mail_mail->connect_imap()) {

               $imap_folder = IMAP_RESTORE_FOLDER_INBOX;

               $emails = $session->get("emails");
               if(in_array($this->data['meta']['from'], $emails)) {
                  $imap_folder = IMAP_RESTORE_FOLDER_SENT;
               }

               $x = $this->imap->append($imap_folder,  $msg);
               syslog(LOG_INFO, "imap append " . $this->data['id'] . "/" . $this->data['piler_id'] . " to " . $imap_folder . ", rc=$x");
               $this->model_mail_mail->disconnect_imap();
            }
            else {
               $x = 0;
            }
         }
         else {

            if(RESTORE_EMAILS_AS_ATTACHMENT == 1) {
               $msg = $this->model_mail_mail->message_as_rfc822_attachment($this->data['id'], $msg, $rcpt[0]);
               $x = $this->model_mail_mail->send_smtp_email(SMARTHOST, SMARTHOST_PORT, SMTP_DOMAIN, SMTP_FROMADDR, $rcpt, $msg);
            }
            else {
               $x = $this->model_mail_mail->send_smtp_email(SMARTHOST, SMARTHOST_PORT, SMTP_DOMAIN, SMTP_FROMADDR, $rcpt, 
                  "Received: by piler" . EOL . PILER_HEADER_FIELD . $this->data['id'] . EOL . $msg );
            }

         }

         if($x == 1) { $this->data['data'] = $this->data['text_restored']; }
      }

      $this->render();
   }


}

?>
