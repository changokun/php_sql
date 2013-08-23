<?php

// please note: php.ini: mssql.datetimeconvert = Off DOES NOT WORK. PRODUCES GARBAGE. KNOWN BUG - blame freeTDS.
class sql_mssql extends sql_SQL_Server {

	protected function _get_connection() { // called by get_connection. don't break that flow.
		// todo make persistent connections configurable
		// if(self::$connections[$this->data_source->name] = mssql_pconnect($this->data_source->host . ':' . $this->data_source->port, $this->data_source->username, $this->data_source->get_password())) {
		if(self::$connections[$this->data_source->name] = mssql_connect($this->data_source->host . ':' . $this->data_source->port, $this->data_source->username, $this->data_source->get_password())) {
			// is this part nec?
			if( ! mssql_select_db($this->data_source->putative_schema)) throw new Exception('could not select putative schema; ' . $this->data_source->putative_schema . '. ' . mssql_get_last_message());
		} else {
			throw new Exception('could not make db connection. check settings. host: ' . $this->data_source->host . ':' . $this->data_source->port . ' username: ' . $this->data_source->username); // no other information is available.
		}
	}


  function run(query $query) {
    parent::run($query); // mostly for logging and debugging and mssql cleanup
    if($query->action == 'stored_procedure') return $this->execute_stored_procedure($query);

    // todo handle varchars of greater than 255 chars.
    // http://board.phpbuilder.com/showthread.php?10220757-PHP-amp-MS-SQL-text-truncated

    $connection = self::get_connection();

    if($result = mssql_query($query->query, $connection)) {
      $query->raw_result = $result;
      $query->success = true;
      $query->executed = true;
		// todo select @@IDENTITY as id $query->insert_id = $connection->insert_id;
		$query->num_rows = mssql_num_rows($result);
      return $result;
    } else {
      $query->error_messages[] = mssql_get_last_message();
      $this->_trigger_error($query);
      return false;
    }
  }

  function execute_stored_procedure(query $query) {
    //
// oh boy. need the name of the stored procedure
if(empty($query->stored_procedure_name)) trigger_error('no stored procedure name specced.', E_USER_WARNING);
$stmt = mssql_init($query->stored_procedure_name);
foreach($query->stored_procedure_params as $param) {
  m::dump($param);
  mssql_bind($stmt, $param['param_name'], $param['value'], $param['sql_data_type']);

}
// Execute the statement
$result = mssql_execute($stmt);

m::dump($result);

    $data = array();
    while($row = mssql_fetch_assoc($result)) $data[] = $row;

m::dump($data);
// And we can free it like so:
mssql_free_statement($stmt);

    die("<hr>XXX<br>Died on line " . __LINE__ . " of " . __FILE__);
    if($result = mssql_query($query->query)) {
      $query->raw_result = $result;
      $query->success = true;
      $query->executed = true;
      return $result;
    } else {
      $query->error_messages[] = mssql_get_last_message();
      $this->_trigger_error($query);
      return false;
    }
  }

  protected function groom_stored_procedure_params($params = array()) {
    // params are set up with lots of information for the mssql driver. we need to convert some values to mssql constants.
    // they should already be named as they need to be except for one: data type. we are going to translate that to mssql constants and add them to params
    foreach($params as $key => $param) {
      // sql data type is derived from the supplied sqldata type http://php.net/manual/en/mssql.constants.php
      switch($param['sql_data_type']) {
        case 'integer':
          $params[$key]['sql_data_type'] = SQLINT4; // aiming large.
        break;
        default:
          throw new Exception('unsupported sql data type.'); // todo cover them all.
        break;
      }
    }

    return $params;

  }

	public function groom_result(query $query) {
		parent::groom_result($query); // sets -executed_action and makes sure we executed
		// okay, if it was a select, that's easy. if it was an insert, grab the new id. if it was an update.... or a delete... return rows affected?
		// the parent function just set executed action, go by that
		switch($query->executed_action) {
			case 'select':
			case 'show':
				$data = array();
				while($row = mssql_fetch_assoc($query->raw_result)) {
					$data[] = $row;
				}

				if(count($data) === 0) {
					$query->set_empty_result(); // this does things like NULL vs empty array. it is nicer than the same thing all teh time.
				} else {
					$query->set_results($data); // we have some results
				}

			break;

			case 'insert':
				$query->result = self::$connections[$query->database_name]->insert_id;
			break;

			default:
				throw new Exception('Sorry, we need to develop code for your type of query (' . get_class($this) . ' ' . $query->executed_action . ').');
			break;
		}
	}

}
