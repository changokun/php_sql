<?php

/*
handlers are objects that handle a specific type of sql, with a specific set of php functions, eg mssql with mssql_*() or mssql with sqlsrv_*().
each handler will have one or more connections that it uses to different dbs that it handles,

todo implement caching
*/
final class db {
	protected static $data_sources; // array of data_source objects
	protected static $initted = false;
	protected static $config;
	private static $handlers = array(); // handlers handle syntax types - mysql vs mssql. each handler has it's own set of connections

	private function __construct() {
	}

	/**
	* returns true on success, false otherwise. don't run it twice, ha ha. actually, don't run it. let the class do that.
	* this doesn't actually make any db connections. it loads config data.
	*/
	public static function init($options = array()) {
		if(self::$initted) throw new Exception('double init on sql. oh dear.');

		if(file_exists(__DIR__ . '/sql_config.ini')) {
			if (false === self::$config = parse_ini_file('sql_config.ini', true)) throw new Exception('sql_config.ini is unparsable. there is a sample one waiting for you.');
		}

		foreach($options as $key => $value) self::$config[$key] = $value;

		if(m::help('config')) m::dump(self::$config, __CLASS__ . ' loading config data');
		if(empty(self::$config['data_sources_DEVELOPMENT']) and empty(self::$config['data_sources_PRODUCTION'])) throw new Exception('no data sources configured.');

		if(defined('SITE_IS_DEV') and SITE_IS_DEV) {
			$temp = self::$config['data_sources_DEVELOPMENT'];
		} else {
			$temp = self::$config['data_sources_PRODUCTION'];
		}


		// create actual data_source objects
		foreach($temp as $data) {
			if(is_object($data) and $data instanceof data_source) {
				self::$data_sources[$data->name] = $data;
			} else {
				// $data['putative_schema'] is optional, if you know your db is not needing it
				$data['putative_schema'] = empty($data['putative_schema']) ? NULL : $data['putative_schema'];
				self::$data_sources[$data['name']] = new data_source($data['name'], $data['putative_schema'], $data['type'], $data['host'], $data['username'], $data['pass'], $data['port']);
			}
		}
		unset(self::$config['data_sources_DEVELOPMENT'], self::$config['data_sources_PRODUCTION']);

		// aliases
		if(isset(self::$config['data_source_aliases'])) {
			foreach(self::$config['data_source_aliases'] as $alias => $target) {
				if( ! isset(self::$data_sources[$target])) throw new Exception('i do not like your alias configuration.');
				if(isset(self::$data_sources[$alias])) throw new Exception('i do not like your alias configuration at all.');
				self::$data_sources[$alias] = self::$data_sources[$target];
			}
		}

		//m::death(self::$data_sources);

		self::$initted = true;
		return true;
	}

	public static function get_handler_by_name($data_source_name) {
		if( ! self::$initted) self::init();
		if(isset(self::$data_sources[$data_source_name])) return self::get_handler(self::$data_sources[$data_source_name]);
		$exception = new Exception('unknown datasource: ' . $data_source_name);
		$exception->debug_data = debug_backtrace();
		throw $exception;
	}

	/**
	* internal use only. it gets you the object that will handle a particular syntax, such as mysql or tsql.
	*
	* @param stdClass $data_source - straight from config, please.
	* @return sql_*
	*/
	public static function get_handler(data_source $data_source) {
		if(isset(self::$handlers[$data_source->name])) return self::$handlers[$data_source->name];

		// whelp,.... better go make one.
		if(stripos($data_source->type, 'mssql') !== false) {

			// mssql t-sql type stuff
			if(self::$config['db_force_mssql']) {
				if( ! extension_loaded('mssql')) throw new Exception('configured to use mssql, but that is not available.');
				self::$handlers[$data_source->name] = new sql_mssql($data_source);
			} elseif(self::$config['db_force_sqlsrv']) {
				if( ! extension_loaded('sqlsrv')) throw new Exception('configured to use sqlsrv, but that is not available.');
				self::$handlers[$data_source->name] = new sql_sqlsrv($data_source);
			} else {
				if(extension_loaded('sqlsrv')) {
					self::$handlers[$data_source->name] = new sql_sqlsrv($data_source);
				} elseif(extension_loaded('mssql')) {
					self::$handlers[$data_source->name] = new sql_mssql($data_source);
				} else {
					throw new Exception("cannot handle {$data_source->type}");
				}
			}

		} else { // defaulting to mysqli - mysql is depped.
			if(extension_loaded('mysqli')) {
				self::$handlers[$data_source->name] = new sql_mysqli($data_source);
			} else {
				throw new Exception("cannot handle {$data_source->type}");
			}
		}

		return self::$handlers[$data_source->name];

	}

	public static function get_data_sources() {
		if( ! self::$initted) self::init(); // we key off of this var to know if we have been initted.
		return self::$data_sources;
	}

	/**
	* returns data formatted to your specifications, or false on error.
	* this and simple_lookup are really the only public-facing functions.
	* accepts only query objects
	* @param query $query
	*/
	static function q(query $query) {
		if( ! self::$initted) self::init(); // we key off of this var to know if we have been initted.

		// get the handler, which handles a particular syntax.
		$handler = self::get_handler_by_name($query->data_source_name); // this will eventually make a db connection

		// easy peasy
		if($result = $handler->run($query)) {
			// success - convert result to final format - set $query->result
			$handler->groom_result($query);
			if(m::help('sql', 9)) m::dump($query, 'ran this query');
			return $query->result;
		} else {
			// the handler will have already triggered an error. we don't do that here. just return false. the code surrounding db::q() should handle a bad result.
			return false;
		}
	}

/**
* quick lookups everything is required
*
* returns result on success and found records. if no records are found, default_value is returned (if it was set), otherwise it will return false and false on error. reports its own errors.
*
* the original goal was not only for ease of finding single values, but also generating queries that were identical so that they could be cached. that last part is no longer the goal as caching is now an option for this method. this makes the where clause much easier to think about. if you are replacing usage of an old mf_lookup, and you have a complex where, please feel free to accomodate it here with some code changes. BEWARE using anything with quote marks in it. it's okay around values, but not in them. keep this simple. todo - it is also unsecure.
*
* @param mixed $field_name select fieldname
* @param mixed $table_name from tablename
* @param mixed $key_field_name where key field name
* @param mixed $key_value = key value
* @param mixed $database_name
*/
public static function simple_lookup($field_name, $table_name, $where_clause, $database_name, $default_value = NULL, $cache_lifespan = 0) {
// if this query becomes complex, have the sql objs build it. but for now we will piece it together
// note, we cannot use mysql_escape stuff here, so we may need to get to secure building. todo
// for now:
m::death(func_get_args());
foreach(array('field_name', 'table_name', 'database_name') as $arg_name) {
if( ! is_string($$arg_name) or ! strlen($$arg_name)) {
// todo add sql safety
trigger_error('bad args for lookup SEND_EMAIL', E_USER_WARNING);
return false;
}
}

// let's start with the where_clause
if(is_array($where_clause) and count($where_clause)) {
// we'll set up where x=y and p=q
$bits = array();
foreach($where_clause as $key => $value) $bits[] = " $key = '$value' ";
$where_clause = ' where ' . implode(' and ', $bits);
} elseif(is_string($where_clause) and strlen($where_clause)) {
// make sure it has the where keyword and replace double quotes with single - i think tsql needs single quotes....
if(stripos($where_clause, 'where') === false) $where_clause = ' where ' . $where_clause;
$where_clause = str_replace('"', "'", $where_clause);
} else {
// todo add sql safety
trigger_error('bad args (where clause) for lookup SEND_EMAIL', E_USER_WARNING);
return false;
}

$query = "select $field_name as lookup_target from $table_name $where_clause";

if(m::help('sql', 4)) decho(__FUNCTION__ . '() query: ' . $query);

m::death($query);


}

}



