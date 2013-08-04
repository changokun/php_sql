<?php
class data_source {
	public $name;
	public $putative_schema; // in some cases you may need to name a schema when connecting. so we need a valid schema name, but it is not default, not used in any way.
	public $type;
	public $host;
	public $username;
	public $port;
	private $pass;
	public function __construct($name, $putative_schema, $type, $host, $username, $pass, $port = NULL) {
		$this->name = $name;
		$this->putative_schema = $putative_schema;
		$this->type = $type;
		$this->host = $host;
		$this->username = $username;
		$this->pass = $pass;
		if( ! empty($port) and is_numeric($port)) $this->port = (int) $port;
	}

	public function get_password() {
		return $this->pass;
	}
}