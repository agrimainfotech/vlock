<?php

require ('Config.php');



class Config 
{

PRIVATE $agrs;
    
    public function createDatabase($args)
    {
       
        mysql_query('CREATE DATABASE '."paulrio_".$args) ;
        
    }
    
    public function createTable($database,$tableName,$values)
    {
	
        mysql_select_db($database);        
        mysql_query("CREATE TABLE IF NOT EXISTS `$tableName` ($values)");
		
    }


    public function insert($database,$table,$data)
    {
    
   
        mysql_select_db($database); 
        $fieldNames = implode('`, `',array_keys($data));
        $fieldValues=implode(',',  array_values($data));

        mysql_query("INSERT INTO `$table` (`$fieldNames`) VALUES ($fieldValues)");
       
      
        
    }
    
      public function read($database,$table,$where)
     {
      mysql_select_db($database);    
      $sth=  mysql_query("SELECT * FROM $table WHERE $where");    
      $row = mysql_fetch_array($sth);

        return $row;
    
     }
        
                public function readall($database,$table,$where)
     {
      mysql_select_db($database);    
      $sth=  mysql_query("SELECT * FROM $table WHERE $where");    
      
return $sth;
     }
        
    public function update($database,$table,$data,$where)
    {
       mysql_select_db($database);

      mysql_query("UPDATE $table SET  $data  WHERE $where");
     
    }
    
   
     public function delete($database,$table,$where)
     { 
        mysql_select_db($database);
        mysql_query("DELETE FROM $table WHERE $where");    
    
     }
    
}

?>