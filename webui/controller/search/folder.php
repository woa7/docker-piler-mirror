<?php  


class ControllerSearchFolder extends Controller {

   protected function index() {

      $this->id = "folder";
      $this->template = "search/folder.tpl";

      $session = Registry::get('session');
      $request = Registry::get('request');
      $db = Registry::get('db');

      $this->load->model('folder/folder');

      $this->data['folders'] = $session->get("folders");

      $this->render();
   }


}

?>
