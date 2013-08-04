<?php

// todo select @@IDENTITY as id

abstract class sql_SQL_Server extends sql {

	function write_query(query $query) {
		parent::write_query($query); // logging and type checking, sets ->query to empty string.

		// only one type of action is allowed. select, insert, update, delete, etc.
		switch($query->action) {
			case 'select':
				// oh man. if we have a skip, we need an order by, and we use offset/fetch
				// if we do not have a skip but have a limit, we need a top. (no order by required.)
				// so, put the query clauses together. here is the order for mysql
				foreach(array('select', 'from', 'where', 'having', 'order_by', 'note') as $clause_name) { // limit handled in select

					// SELECT or INSERT etc
					if( ! empty($query->clauses[$clause_name])) $query->query .= ' ' . strtoupper(str_replace('_', ' ', $clause_name)) . ' ';

					switch($clause_name) {
						case 'select':
							if(is_numeric($query->limit) and $query->limit > 0) {
								// okay, do we do it by top or offset/fetch? (is this only for selects?)
								if(is_numeric($query->skip) and $query->skip > 0) {
									// offset/fetch - we'll hit it later.
								} else {
									// top
									$query->query .= ' top ' . $query->limit . ' ';
								}
							}

							// a select clause can also carry join information. and in those joins the 'FULL' keyword is not universal, but also pretty uncommon.
							// we'll handle these at the query level.
							$query->query .= $query->get_final_select_clause();
						break;

						case 'where':
							// a where clause is essentially universal. there may be some obscure comparison operators, but by and large, we can use these in the same way every where. so.... we'll just have the query do this part.
							$query->query .= $query->get_final_where_clause();
						break;

						case 'order_by':
							if( ! empty($query->clauses[$clause_name])) {
								if(is_array($query->clauses[$clause_name]) and count($query->clauses[$clause_name])) {
									$temp = array();
									foreach($query->clauses[$clause_name] as $fields => $direction) $temp[] = $fields . ' ' . $direction;
									$query->query .= implode(', ', $temp);
								} else {
									$query->query .= $query->clauses[$clause_name];
								}
							}
							// now add skip/limit offset/fetch - these are a part of the order by clause.
							if(is_numeric($query->limit) and $query->limit > 0) {
								// okay, do we do it by top or offset/fetch? (is this only for selects?)
								if(is_numeric($query->skip) and $query->skip > 0) {
									// offset/fetch
									$query->query .= ' offset ' . $query->skip . ' rows fetch next ' . $query->limit . ' rows only ';
								} else {
									// top - we hit that earlier.
								}
							}
						break;

						case 'from':
							if(empty($query->clauses[$clause_name])) throw new Exception($query->action . ' needs a ' . $clause_name . ' clause.');
							$query->query .= $query->clauses[$clause_name];
						break;

						case 'note':
							if( ! empty($query->note)) $query->query .= ' /* ' . $query->note . ' */ ';
						break;

						default:
							if( ! empty($query->clauses[$clause_name])) $query->query .= strtoupper($clause_name) . ' ' . $query->clauses[$clause_name];
						break;
					}
					if( ! empty($query->clauses[$clause_name . '_note'])) $query->query .= '/* ' . $query->clauses[$clause_name . '_note'] . ' */ ';
				}
			break;

			case 'update':
				$this->write_update_query($query);
			break;

			case 'insert':
				$this->write_insert_query($query);
			break;

			case 'replace':
				// http://stackoverflow.com/questions/234/sql-server-2005-implementation-of-mysql-replace-into
				throw new Exception('replace not developed for sql server queries yet. please get on it.');
			break;

			case 'delete':

				if(empty($query->data_table_name)) throw new Exception('must provide data_table_name as in DELETE from data_table_name.');
				if(empty($query->clauses['where'])) throw new Exception('delete queries must include where clauses.');

				$query->query = "delete from {$query->data_table_name} where " . $query->get_final_where_clause();
				// a where clause is essentially universal. there may be some obscure comparison operators, but by and large, we can use these in the same way every where. so.... we'll just have the query do this part.

			break;

			default:
				throw new Exception($query->action . ' not implemented.');
			break;
		}

		// note also that $query is passed by ref
		return $query->query;
	}

	function run(query $query) {
		parent::run($query); // logging,
		// clean up any stored procedure params
		if( ! empty($query->stored_procedure_params)) $query->stored_procedure_params = $this->groom_stored_procedure_params($query->stored_procedure_params);
		// ... todo ... complete., we're going to do some translations from mysql to mssql.
		$query->query = self::simple_convert_from_mysql($query->query);
	}

	static function simple_convert_from_mysql($query) { //query string.
		// i do simple conversions that can be done with blind str_ireplace().
		$query = str_ireplace('now()', 'getDate()', $query);
		return $query;
	}

	// public function groom_string_data($str) - no usage in sql server stuff. basically, you are on your own, and we already convert quotes, so is that good enough? todo

}
