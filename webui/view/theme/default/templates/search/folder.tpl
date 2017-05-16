<p class="bold"><?php print $text_folders; ?>:</p>

<div id="folders">

<?php while(list($k,$v) = each($folders)) { ?>
   <div>
      <label class="folderlabel"><input type="checkbox" id="folder_<?php print $v; ?>" name="folder_<?php print $v; ?>" style="margin:0;" class="foldercheckbox" /> <?php print $k; ?></label>
   </div>
<?php } ?>

</div>
