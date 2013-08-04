<?php

/**
*currently, this is a class you can use when you need to build a complex query and maybe you don't know what db it runs in or whatnot.
* it is not typed-bound-coupled to any specific database syntax, unless you write your own query.
* the idea is you set the skip and the limit, and when it comes time to run this, the handler will write the actual query that gets used, with limit 0, 10 or offset 0 fetch 10 records only depending on the db. see?
* this can be extended by some shortcut classes, like update_query
*
* this is not completely finalized.
*/
class query {

	public $clauses = array();
	public $action = 'pre-written'; // action is used to determine what type of query to build, *if we are going to build it* so only set it if you are also adding clauses etc.
	public $executed_action;
	public $joined_table_names;
	public $raw_result;
	public $skip = 0;
	public $limit = 666; // arbitrary & high & recognizable
	public $note = array();
	public $query = '';
	public $result;
	public $rows_matched;
	public $changed;
	public $executed;
	public $success = false;
	public $error_messages = array();
	public $return_format = 'array of associative arrays';
	public $data_source_name = '';
	public $cache_time_span = 3600; // not acually implemented yet
	public $stored_procedure_params = array(); // currently used only for sql server stored procedure params. see those classes for details.
	public $stored_procedure_name = ''; // only for sql Server stuff.
	public $num_rows; // having no default value prevents passed in options from setting it.
	public $data = array(); // for inserts and updates
	public $data_table_name = ''; // for inserts and updates

	function __construct($options = array()) {

		// bring options into properties - by using isset, you can only change vars declared above that are initialized to something.
		foreach($options as $key => $value) if(isset($this->$key)) $this->$key = $value;

		// make sure we gots db
		if(empty($this->data_source_name)) throw new Exception('no data_source');

		// stored procedure assurances
		if( ! empty($this->stored_procedure_name)) $this->action = 'stored_procedure';

		// while we are still developing this - some things are limited - if you are writing your own query, you can do anything you want.
		if( ! in_array($this->action, array('select', 'insert', 'update', 'replace', 'delete', 'pre-written', 'stored_procedure'))) throw new Exception($this->action . ' is an unsupported query type for this class.');

		// allow some clauses to be passed by constructor args:
		// todo finish out all teh clause names
		foreach(array('select', 'from', 'where', 'order_by') as $clause_name) if(isset($options[$clause_name])) $this->clauses[$clause_name] = $options[$clause_name];
	}

	public function set_empty_result() {
		switch($this->return_format) {
			case 'single integer':
			case 'single integer regardless':
			case 'single associative array':
			case 'single string':
				$this->result = NULL;
			break;
			case 'array of integers':
			case 'array of strings':
			case 'array of associative arrays':
			default:
				$this->result = array();
			break;
		}
	}

	public function set_results($data = array()) {
		// detect conditions that warrant errors.
		if(strpos($this->return_format, 'single') !== false and strpos($this->return_format, 'regardless') === false) {
			// if we got more than one row, that is an error
			if(count($data) > 1) trigger_error('more than one row returned when expecting one or zero rows - ' . $this->query, E_USER_WARNING);
			if(strpos($this->return_format, 'string') !== false or strpos($this->return_format, 'integer') !== false) {
				// if we got more that one field, that is an error
				if(count($data[0]) > 1) trigger_error('more than one field returned when expecting only one field - ' . $this->query, E_USER_WARNING);
			}
		}

		// format them.
		switch($this->return_format) {
			case 'single associative array':
				$this->result = $data[0];
			break;
			case 'single string':
			case 'single string regardless':
				$this->result = array_shift($data[0]);
			break;
			case 'single integer': // return an int, which may be zero. of course, false on error.
			case 'single integer regardless': // same as above
				$this->result = (float) array_shift($data[0]);
			break;
			case 'array of integers':
				$this->result = array();
				foreach($data as $row) foreach($row as $value) $this->result[] = (float) $value;
			break;
			case 'array of strings':
				$this->result = array();
				foreach($data as $row) foreach($row as $value) $this->result[] = $value;
			break;
			case 'array of associative arrays':
			default:
				$this->result = $data;
			break;
		}
	}

	public function get_final_where_clause() {
		// moo=oink => moo=oink
		// array('x=y', 'moo=oink'); => x=y and moo=oink (array of strings)
		// array('x=%s' => 'y', 'moo=%d' => 99); => x='y' and moo = 99 (array of arrays will trigger token/sprintf behavior)
		// the supplied where clause should not contain the word 'where' unless it is in some subquery.
		// the supplied where clause can be:
		// a string, which will be slapped on
		// or
		// an array of strings, which will be joined together with ' AND ' and then slapped on

		// all of those strings are parsed for tokens, if the tokens have values, those will be pulled from where ????????
		if(empty($this->clauses['where'])) {
			// todo - warn? throw?
			return ' /* no where clause - ay caramba */ ';
		} elseif(is_string($this->clauses['where'])) {
			return $this->clauses['where'];
		} elseif(is_array($this->clauses['where'])) {
			if(count($this->clauses['where'])) {
				// want to support clause = array('x = 5', 'y = %s' => array('moco));
				$fragments = array();
				foreach($this->clauses['where'] as $key => $value) {
					if(is_numeric($key)) {
						// that means a simple string is waiting in the value
						$fragments[] = $value;
					} else {
						// that means the clause has tokens. the clause is the key, the tokens are in the $value
						if( ! isset($handler)) $handler = db::get_handler_by_name($this->data_source_name);
						$fragments[] = $handler->replace_tokens($key, $value);
					}
				}
				return implode(' AND ', $fragments);
			}
		} else {
			throw new Exception('bad where clause: ' . $this->clauses['where']);
		}
	}

	/**
	* @returns the clause. it may change teh query's from clause.
	*
	*/
	public function get_final_select_clause() {
		if(empty($this->clauses['select'])) throw new Exception($this->action . ' query needs a select clause.');
		if( ! isset($this->joined_table_names)) $this->joined_table_names = array(); // if there are joins, i'm storing them here. as a list of table_name(possibly aliased)

		if(is_scalar($this->clauses['select'])) {
			return $this->clauses['select'];
		} elseif(is_array($this->clauses['select'])) {
			$fragments = array(); // about to be populated, will work like above.
			foreach($this->clauses['select'] as $fragment) {
				if(is_scalar($fragment)) {
					$fragments[] = $fragment;
				} elseif(is_array($fragment) and isset($fragment['join_clause']) and isset($fragment['select']) and isset($fragment['join_table_name'])) {
					// joiny times.
					// do we have a new join?
					if( ! in_array($fragment['join_table_name'], $this->joined_table_names)) {
						$this->joined_table_names[] = $fragment['join_table_name'];
						$this->clauses['from'] .= ' ' . $fragment['join_clause'];
						// the 'full join' is not universal. let it go, but issue a warning.
						if(stripos($fragment['join_clause'], ' full ')) throw new Exception('full joins are not supported in mysql. todo: detect needed syntax, allow full on tsql, but warn. throw on mysql', E_USER_WARNING);
					}
					//regardless of joining to a new table or not, the select goes in.
					$fragments[] = $fragment['select'];
				} else {
					throw new Exception('i do not know how to handle this select fragment');
				}
			}
			return implode(', ', $fragments);
		} else {
			throw new Exception('i do not know what to do with this select clause data.');
		}
	}


	/** loads our two sql syntax handlers, and dumps the query they would write.
	* @returns void
	* in order for this to work, we need an actual data source. that stinks. todo
	*/
	public function write_query_for_debug() {
		if( ! defined('SITE_IS_DEV') or ! SITE_IS_DEV) {
			trigger_error('debugging on live!!! how dare you', E_USER_WARNING);
			return;
		}

		foreach(array('sql_mysqli', 'sql_mssql') as $sql_type) {
			$handler = new $sql_type(new data_source('test_dummy', 'test', 'test', 'test', 'test', 'test'));
			m::dump($handler->write_query(clone $this), 'the query written for ' . $sql_type, array('collapse' => false, 'relevant_backtrace_depth' => 1, 'founder_verb' => 'debug query written on '));
		}

	}

	/**
	* does things that allow the query to be run again, usually with modifications. doesn't change things like skip, clauses, etc. just makes it so that you can run it again (success/error messages).
	*
	*/
	public function reset() {
		$this->query_previous = $this->query; // just for fun.
		$this->query = '';
		$this->note = 'this query has been ' . __FUNCTION__ . '.';
		$this->result = NULL;
		$this->raw_result = NULL;
		$this->num_rows = NULL;
		$this->rows_matched = NULL;
		$this->changed = NULL;
		$this->executed = false;
		$this->success = false;
		$this->error_messages = array();
	}
}