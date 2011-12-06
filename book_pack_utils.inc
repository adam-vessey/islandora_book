<?php

function get_page_pids($book_pid) {
  $query = <<<XML
select \$object \$title from <#ri>
	where (\$object <dc:title> \$title
		and \$object <fedora-rels-ext:isMemberOf> <info:fedora/$book_pid>
		and \$object <fedora-model:state> <info:fedora/fedora-system:def/model#Active>)
	order by \$title

XML;

  module_load_include('inc', 'fedora_repository', 'CollectionClass');
  $collection = new CollectionClass($book_pid);
  $contents = $collection->getRelatedItems($book_pid, $query);
  $results_xml = simplexml_load_string($contents);
  $resultsarray = array();
  $book_pids = array();
  foreach ($results_xml->results->result as $result) {
    $book_pids[] = str_replace('info:fedora/', '', $result->object->attributes()->uri);
  }
  return $book_pids;
}

function refresh_ocr($tiff, &$item) {
  $label = $item->objectProfile->objLabel;
  $datastreams = $item->get_datastreams_list_as_array();
  $executable = variable_get('islandora_ocr_path', '/usr/bin/tesseract');
  $returnValue = "";
  $output = "";
  $success = FALSE;
  exec("$executable $tiff $tiff -l eng", $output, $returnValue);
  if ($returnValue == '0') {
    if (array_key_exists('OCR', $datastreams)) {
      $item->purge_datastream('OCR');
    }
    $item->add_datastream_from_file("$tiff.txt", 'OCR', "Scanned Text", 'text/plain', 'M');
    file_delete("$tiff.txt");
    $success = TRUE;
  }

  return $success;
}

function refresh_images($tiff, &$item) {
  $label = $item->objectProfile->objLabel;
  $datastreams = $item->get_datastreams_list_as_array();
  $returnValue = "";
  $success = 0;
  exec('kdu_compress -i "' . $tiff . '" -o "' . $tiff . '.jp2' . '" -rate 0.5 Clayers=1 Clevels=7 "Cprecincts={256,256},{256,256},{256,256},{128,128},{128,128},{64,64},{64,64},{32,32},{16,16}" "Corder=RPCL" "ORGgen_plt=yes" "ORGtparts=R" "Cblk={32,32}" Cuse_sop=yes', $output, $returnValue);
  if ($returnValue == '0') {
    if (array_key_exists('JP2', $datastreams)) {
      $item->purge_datastream('JP2');
    }
    $item->add_datastream_from_file("$tiff.jp2", 'JP2', "Compressed JP2", 'image/jp2', 'M');
    file_delete("$tiff.jp2");
    $success++;
  }

  exec("convert {$tiff}[0] -colorspace RGB -thumbnail 120x $tiff-tn.jpg", $output, $returnValue);

  if ($returnValue == '0') {
    if (array_key_exists('TN', $datastreams)) {
      $item->purge_datastream('TN');
    }
    $item->add_datastream_from_file("$tiff-tn.jpg", 'TN', 'Thumbnail', 'image/jpeg', 'M');
    file_delete("$tiff-tn.jpg");
    $success++;
  }
  file_delete("$tiff-tn.jpg");
  exec("convert  -resize 800 \"{$tiff}\"[0]  \"$tiff-med.jpg\"", $output, $returnValue);
  if ($returnValue == '0') {
    if (array_key_exists('JPEG', $datastreams)) {
      $item->purge_datastream('JPEG');
    }
    $item->add_datastream_from_file("$tiff-med.jpg", 'JPEG', "JPEG image", 'image/jpeg', 'M');
    file_delete("$tiff-med.jpg");
    $success++;
  }
  return $success;
}

function save_datastream_as_tempfile($pid, $dsid) {
  $tmp_filename = preg_replace("/:|-/", '_', $pid) . '.tif';
  $item = new Fedora_Item($pid);
  $url = variable_get('fedora_base_url', 'http://localhost:8080/fedora') . "/objects/$pid/datastreams/$dsid/content";
  $file_contents = do_curl($url);
  $file_path = file_directory_path() . "/" . $tmp_filename;
  file_put_contents($file_path, $file_contents);
  return $file_path;
}

function datastream_display_builder($object_pid, &$fedoraItem) {
  global $fedoraUser, $fedoraPass, $base_url, $user;
  module_load_include('inc', 'fedora_repository', 'ConnectionHelper');
  module_load_include('inc', 'fedora_repository', 'ObjectHelper');
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  module_load_include('inc', 'fedora_repository', 'ContentModel');
  $object_helper = new ObjectHelper();
  $path = drupal_get_path('module', 'fedora_repository');
  $dataStreamBody = '';
  $fedoraItem = new Fedora_Item($object_pid);

  if (user_access(ObjectHelper :: $VIEW_DETAILED_CONTENT_LIST)) {
    $current_datastreams = 'All Datastreams';
    //$metaDataText='Description';
    $mainStreamLabel = NULL;
    $object = $fedoraItem->get_datastreams_list_as_SimpleXML();
    if (!isset($object)) {
      drupal_set_message(t("No datastreams available"));
      return ' ';
    }
    $hasOBJStream = NULL;
    $hasTNStream = FALSE;
    $dataStreamBody = "<br /><table>\n";

    $cmDatastreams = array();
    if (variable_get('fedora_object_restrict_datastreams', FALSE) == TRUE && ($cm = ContentModel::loadFromObject($object_pid)) !== FALSE) {
      $cmDatastreams = $cm->listDatastreams();
    }

    $dataStreamBody .= $object_helper->get_parent_objects_asHTML($object_pid);
    $dataStreamBody .= '<tr><th colspan="4"><h3>' . t("!text", array('!text' => $current_datastreams)) . '</h3></th></tr>';
    foreach ($object as $datastream) {
      foreach ($datastream as $datastreamValue) {
        if (variable_get('fedora_object_restrict_datastreams', FALSE) == FALSE || ((isset($user) && in_array('administrator', $user->roles)) || in_array($datastreamValue->ID, $cmDatastreams))) {
          if ($datastreamValue->ID == 'OBJ') {
            $hasOBJStream = '1';
            $mainStreamLabel = $datastreamValue->label;
            $mainStreamLabel = str_replace("_", " ", $mainStreamLabel);
          }
          if ($datastreamValue->ID == 'TN') {
            $hasTNStream = TRUE;
          }
          //create the links to each datastream
          $dataStreamBody .= $object_helper->create_link_for_ds($object_pid, $datastreamValue); //"<tr><td><b>$key :</b></td><td>$value</td></tr>\n";
        }
      }
    }
    $dataStreamBody .= "</table>\n";
    return $dataStreamBody;



    //if they have access let them add a datastream
    if (user_access(ObjectHelper :: $ADD_FEDORA_STREAMS)) {
      $allow = TRUE;
      if (module_exists('fedora_fesl')) {
        $allow = fedora_fesl_check_roles($object_pid, 'write');
      }
      if ($allow) {
        $dataStreamBody .= drupal_get_form('add_stream_form', $object_pid);
      }
    }
    $fieldset = array(
      '#title' => t("!text", array('!text' => $availableDataStreamsText)),
      '#collapsible' => TRUE,
      '#collapsed' => TRUE,
      '#value' => $dataStreamBody
    );
    $dataStreamBody = '<div class = "fedora_detailed_list">' . theme('fieldset', $fieldset) . '</div>';

    return $dataStreamBody;
  }
  return '';
}

function get_add_datastream_data($object_pid) {
  if (user_access(ObjectHelper :: $ADD_FEDORA_STREAMS)) {
    $allow = TRUE;
    if (module_exists('fedora_fesl')) {
      $allow = fedora_fesl_check_roles($object_pid, 'write');
    }
    if ($allow) {
      $dataStreamBody = drupal_get_form('add_stream_form', $object_pid);
    }
  }
  return $dataStreamBody;
}

function get_collection_from_pid($pid) {

  module_load_include('inc', 'fedora_repository', 'api/fedora_utils');
  $query_string = 'select  $parent  from <#ri>
		where ($object <fedora-rels-ext:isMemberOf> $parent
    or $object <fedora-rels-ext:isMemberOfCollection> $parent)
		and $object <dc:identifier> \'' . $pid . '\'
		order by $object';

  $query_string = htmlentities(urlencode($query_string));
  $url = variable_get('fedora_repository_url', 'http://localhost:8080/fedora/risearch');
  $url .= '?type=tuples&flush=true&format=csv&limit=13000&lang=itql&stream=on&query=' . $query_string;
  $content = do_curl($url, TRUE);
  $results = explode("\n", $content);
  $object_pids = preg_replace('/^info:fedora\/|"parent"| /', '', $results);
  $object_pids = array_values(array_filter($object_pids));
  return $object_pids;
}

function get_collection_relationships($pid) {
  module_load_include('inc', 'fedora_repository', 'api/fedora_item');
  $current_relationships = array();
  $item = new fedora_item($pid);
  $collections = $item->get_relationships('isMemberOfCollection');
  foreach ($collections as $collection) {
    $collection_item = new fedora_item($collection['object']);
    $current_relationships[$collection['object']] = $collection_item->objectProfile->objLabel . ' (' . $collection['object'] .  ')';
  }
  return $current_relationships;
}