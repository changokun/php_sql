<?php
/**
*currently, this is a class you can use when you need to build a complex query (sorry, only simple queries) and maybe you don't know what db it runs in or whatnot.
* it is not needed for ordinary quick queries it is optional - but that may change
* it is not typed-bound-coupled to any specific database syntax, unless you write your own query.
* the idea is you set the skip and the limit, and when it comes time to run this, the handler will write the actual query that gets used, with limit 0, 10 or offset 0 fetch 10 records only depending on the db. see?
* this does not get extended.
*
* this is not completely finalized.
*/
class delete_query extends query {

	function __construct($data_source_name, $data_table_name, $where_clause, $options = array()) {
		parent::__construct(array_merge(array('data_source_name' => $data_source_name, 'action' => 'delete', 'data_table_name' => $data_table_name, 'where' => $where_clause), $options));
	}

}