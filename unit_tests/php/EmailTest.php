<?php

use PHPUnit\Framework\TestCase;

define('DIR_BASE', $_ENV['DIR_BASE']);

include_once(DIR_BASE . "system/model.php");
include_once(DIR_BASE . "model/search/search.php");


final class SearchSearchTest extends TestCase {

   public function providerTestFixEmailAddressForSphinx() {
      return [
         ['aaa@aaa.fu', 'aaaXaaaXfu'],
         ['list-507327664@mail.aaa.fu', 'listX507327664XmailXaaaXfu'],
         ['aaa+bbb@aaa.fu', 'aaaXbbbXaaaXfu'],
         ['ahahah_aiai@aaa.fu', 'ahahahXaiaiXaaaXfu'],
         ['aaa|@bbb@ccc.fu', 'aaa|bbbXcccXfu']
      ];
   }


   /**
    * @dataProvider providerTestFixEmailAddressForSphinx
    */

   public function test_get_boundary($input, $expected_result) {
      $result = ModelSearchSearch::fix_email_address_for_sphinx($input);
      $this->assertEquals($result, $expected_result);
   }


}
