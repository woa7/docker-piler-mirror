<?php


class Piler_Mime_Decode {
   const HEADER_FIELDS = ['from', 'to', 'cc', 'subject', 'date'];


   public static function parseMessage($message, &$result) {

      self::splitMessage($message, $headers, $body);

      $boundary = self::getBoundary($headers);

      // No boundary defined

      if($boundary == '') {
         if($headers['content-type']['type'] == "message/rfc822") {
            self::parseMessage($body, $result);
         }
         else {
            $result[] = array(
               'headers' => $headers,
               'body' => $body
            );
         }

         return;
      }

      $parts = self::splitMime($body, $boundary);

      for($i=0; $i<count($parts); $i++) {

         self::splitMessage($parts[$i], $headers, $body);

         $boundary = self::getBoundary($headers);
         if($boundary) {
            self::parseMessage($parts[$i], $result);
         }
         else {
            if(in_array($headers['content-type']['type'], ["text/plain", "text/html"])) {
               $result[] = array('headers' => $headers, 'body' => $body);
            }
            else if($headers['content-type']['type'] == "message/rfc822") {
               self::parseMessage($body, $result);
            }
         }
      }
   }


   public static function splitMime($body, $boundary) {
      $start = 0;
      $res = array();

      $body = self::remove_LF($body);

      // Extract the mime parts excluding the boundary itself

      $p = strpos($body, '--' . $boundary . "\n", $start);
      if($p === false) {
         // no parts found!
         return array();
      }

      // Position after first boundary line

      $start = $p + 3 + strlen($boundary);

      while(($p = strpos($body, '--' . $boundary . "\n", $start)) !== false) {
         $res[] = substr($body, $start, $p-$start);
         $start = $p + 3 + strlen($boundary);
      }

      // No more parts, find end boundary

      $p = strpos($body, '--' . $boundary . '--', $start);
      if($p === false) {
         return array();
      }

      // The remaining part also needs to be parsed:
      $res[] = substr($body, $start, $p - $start);

      return $res;
   }   


   public static function splitMessage($message, &$headers, &$body, $EOL = "\n") {
      self::splitMessageRaw($message, $headers, $journal, $body);
      $headers = self::splitHeaders($headers);
   }


   public static function splitMessageRaw($message, &$headers, &$journal, &$body, $EOL = "\n") {
      $headers = [];
      $body = '';

      $message = self::remove_LF($message);

      // Find an empty line between headers and body, otherwise we got a header-only message

      if(strpos($message, $EOL . $EOL)) {
         list($headers, $body) = explode($EOL . $EOL, $message, 2);

         // Check if the header is actually a journal header
         $headers_array = self::splitHeaders($headers);

         if(isset($headers_array['x-ms-journal-report']) && isset($headers_array['content-type']['boundary'])) {
            $boundary = $headers_array['content-type']['boundary'];
            $parts = self::splitMime($body, $boundary);

            if(count($parts) >= 2) {
               self::splitMessageRaw($parts[0], $s, $j, $journal);

               $i = strpos($parts[1], "\n");
               $msg = substr($parts[1], $i);

               $i = 0;
               while(ctype_space($msg[$i])) { $i++; }
               if($i > 0) { $msg = substr($msg, $i); }

               self::splitMessageRaw($msg, $headers, $j, $body);
            }
         }
      }
      else {
         $headers = $message;
      }
   }


   public static function removeJournal(&$message, $EOL = '\n') {
      $has_journal = 0;

      $s = self::remove_LF($message);
      if(strpos($s, $EOL . $EOL)) {
         list($headers, $body) = explode($EOL . $EOL, $s, 2);
         if(strstr($headers, "\nX-MS-Journal-Report:")) {
            return $has_journal;
         }
      }

      $p = strstr($message, "\nX-MS-Journal-Report:");
      if($p) {
         $q = stristr($p, "message/rfc822");
         if($q) {
            $has_journal = 1;
            $i = strlen("message/rfc822");
            while(ctype_space($q[$i])) { $i++; }
            if($i > 0) { $message = substr($q, $i); }
         }
      }

      return $has_journal;
   }


   public static function splitHeaders($headers) {
      $headers = self::headersToArray($headers);

      // normalize header names
      foreach ($headers as $name => $header) {
         $lower = strtolower($name);
         if($lower == $name) {
            continue;
         }

         unset($headers[$name]);

         if(!isset($headers[$lower])) {
            $headers[$lower] = $header;
            continue;
         }

         if(is_array($headers[$lower])) {
            $headers[$lower][] = $header;
            continue;
         }

         $headers[$lower] = array($headers[$lower], $header);
      }

      // Add some default values, if they are missing

      if(!isset($headers['content-type'])) { $headers['content-type'] = 'text/plain'; }

      // I saw a dumb email (it was a spam, though) having two Date: lines.
      // In this case we take the first date, and discard the rest
      if(isset($headers[self::HEADER_FIELDS[4]]) && is_array($headers[self::HEADER_FIELDS[4]])) {
         $headers[self::HEADER_FIELDS[4]] = $headers[self::HEADER_FIELDS[4]][0];
      }

      for($i=0; $i<count(self::HEADER_FIELDS); $i++) {
         if(!isset($headers[self::HEADER_FIELDS[$i]])) { $headers[self::HEADER_FIELDS[$i]] = ''; }

         $headers[self::HEADER_FIELDS[$i]] = iconv_mime_decode($headers[self::HEADER_FIELDS[$i]], ICONV_MIME_DECODE_CONTINUE_ON_ERROR);
      }

      $headers['content-type'] = self::splitContentType($headers['content-type']);

      $headers['content-type']['type'] = strtolower($headers['content-type']['type']);

      return $headers;
   }


   public static function headersToArray($headers = '') {
      $token = '';
      $last_token = '';
      $result = array();

      $headers = explode("\n", $headers);

      foreach($headers as $h) {

         // Handle cases when there's no whitespace between the header key and value
         // eg. Subject:som

         $h = preg_replace("/^([\S]+):(\S)/", '${1}: ${2}', $h);
         $h = preg_replace("/\s{1,}/", " ", $h);

         $line = preg_split("/\s/", $h);

         // Skip line if it doesn't have a colon (:) and the 1st character is not a whitespace

         if(!ctype_space($h[0]) && !strchr($h, ':')) { continue; }

         if($line) {
            if(substr($line[0], -1) == ':') {
               $token = array_shift($line);
               $token = rtrim($token, ':');

               $last_token = $token;
            }
            else {
               $token = '';
            }

            $line_str = implode(" ", $line);

            if(!isset($result[$last_token])) {
               $result[$last_token] = $line_str;
            }
            else {
               if($token) {
                  $result[$last_token] .= "\n";
               }

               $result[$last_token] .= ' ' . $line_str;
            }
         } 

      }

      while(list($k, $v) = each($result)) {
         if(strchr($v, "\n")) {
            $result[$k] = explode("\n", $v);
         }
      }

      return $result;
   }


   public static function splitContentType($field = '') {
      $split = array();
      $what  = 'type';

      $field = $what . '=' . $field;
      if(!preg_match_all('%([^=\s]+)\s*=\s*("[^"]+"|[^;]+)(;\s*|$)%', $field, $matches)) {
         return $split;
      }

      $split = array();
      foreach ($matches[1] as $key => $name) {
         $name = strtolower($name);
         if($matches[2][$key][0] == '"') {
            $split[$name] = substr($matches[2][$key], 1, -1);
         } else {
            $split[$name] = $matches[2][$key];
         }
      }

      return $split;
   }


   public static function remove_LF($message = '') {
      return str_replace("\r", "", $message);
      //return preg_replace("/\r/", "", $message);
   }


   public static function getBoundary($headers = array()) {
      if(isset($headers['content-type']['boundary'])) {
         return $headers['content-type']['boundary'];
      }

      return '';
   }


   public static function fixMimeBodyPart($headers = array(), $body = '') {

      if(isset($headers['content-transfer-encoding'])) {
         if($headers['content-transfer-encoding'] == 'quoted-printable') {
            $body = quoted_printable_decode($body);
         }

         if($headers['content-transfer-encoding'] == 'base64') {
            $body = base64_decode($body);
         }
      }

      if(isset($headers['content-type']['charset'])) {
         $body = iconv($headers['content-type']['charset'], 'utf-8' . '//IGNORE', $body);
      }

      if(strtolower($headers['content-type']['type']) == 'text/plain') {
         $body = self::escape_lt_gt_symbols($body);
         $body = preg_replace("/\n/", "<br />\n", $body);
         $body = "\n" . self::printNicely($body);
      }

      return $body;
   }


   public static function escape_lt_gt_symbols($s = '') {
      $s = preg_replace("/</", "&lt;", $s);
      $s = preg_replace("/>/", "&gt;", $s);

      return $s;
   }


   public static function printNicely($s = '') {
      $k = 0;
      $nice = "";

      $x = explode(" ", $s);

      for($i=0; $i<count($x); $i++){
         $nice .= $x[$i] . " ";
         $k += strlen($x[$i]);

         if(strstr($x[$i], "\n")){ $k = 0; }

         if($k > 70){ $nice .= "\n"; $k = 0; }
      }

      return $nice;
   }

}
