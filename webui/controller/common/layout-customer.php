<?php  

class ControllerCommonLayoutCustomer extends Controller {

      protected function index() {


         $this->data['title'] = $this->document->title;

         $this->template = "common/layout-customer.tpl";


         $this->children = array(
                      "common/menu",
                      "common/footer"
         );

         $this->render();

      }


}


?>
