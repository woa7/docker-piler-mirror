<?php

class ModelMessageAttachment extends Model {


   public function get_attachment_by_id($id = 0) {
      if($id <= 0) { return array(); }

      $query = $this->db->query("SELECT id, piler_id, attachment_id, name, type FROM " . TABLE_ATTACHMENT . " WHERE id=?", array($id));

      if(isset($query->row)) {
         $metaid = $this->model_search_message->get_id_by_piler_id($query->row['piler_id']);

         if($metaid > 0 && $this->model_search_search->check_your_permission_by_id($metaid) == 1) {
            $attachment = $this->get_attachment_content($query->row['piler_id'], $query->row['attachment_id']);

            return array('filename' => fix_evolution_mime_name_crap($query->row['name']), 'piler_id' => $query->row['piler_id'], 'attachment' => $attachment);
         }
      }

      return array();
   }


   public function get_attachment_content($piler_id = '', $attachment_id = '') {
      $data = '';

      if($piler_id == '' || $attachment_id == '' || !preg_match("/^([0-9a-f]+)$/", $piler_id) || !preg_match("/^([0-9m]+)$/", $attachment_id)) { return $data; }

      $cmd = DECRYPT_ATTACHMENT_BINARY . " $piler_id $attachment_id";

      if(LOG_LEVEL >= DEBUG) { syslog(LOG_INFO, "attachment cmd: $cmd"); }

      $handle = popen($cmd, "r");

      while(($buf = fread($handle, DECRYPT_BUFFER_LENGTH))){
         $data .= $buf;
      }
      pclose($handle);

      /* check if it's a base64 encoded stuff */

      $s = substr($data, 0, 4096);
      $s = preg_replace("/(\r|\n)/", "", $s);

      if(!preg_match("/\s/", $s)) {
         return base64_decode(preg_replace("/\s/", "", $data));
      }

      return $data;
   }


   public function write_image_attachments_to_tmp($attachments, $id) {
      $images = [];

      foreach($attachments as $a) {

         if(preg_match("/image/", $a['type'])) {

            $attachment = $this->get_attachment_by_id($a['id']);

            $fp = fopen(DIR_BASE . 'tmp/' . "i." . $a['id'], "w+");
            if($fp) {
               fwrite($fp, $attachment['attachment']);
               fclose($fp);

               $images[] = array('name' => "i." . $a['id']);
            }
         }
      }

      return $images;
   }

}
