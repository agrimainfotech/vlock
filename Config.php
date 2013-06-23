<?php
$mysql_host='localhost';
$mysql_user='paulrio_vlock';
$mysql_pass='94bd66ca';
$mysql_db='paulrio_vlock';

if(mysql_connect($mysql_host,$mysql_user,$mysql_pass)){
  
   mysql_select_db($mysql_db);
    }
}
?>