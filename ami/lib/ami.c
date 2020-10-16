#include <stdio.h>
#include <stdlib.h>
#define _GNU_SOURCE
#include <search.h>
#include <errno.h>

#include <parse.h>

#include <ami/ami.h>
#include <ami/csvread.h>
#include <ami/tree.h>

#include <ami/kvec.h>
#include <ami/khash.h>

ami_ast_t *ami_ast_new(void)
{
  ami_ast_t *ast;
  ast = (ami_ast_t *)malloc(sizeof(ami_ast_t));
  if (!ast) {
    fprintf(stderr, "Cannot allocate ami_ast_t!\n");
    return NULL;
  }

  ast->parsing_function = 0;
  ast->action_block_id = 0;
  ast->repeat_block_id = 0;
  ast->opened_sections = 0;
  ast->repeat = 0;
  ast->in_action = 0;
  ast->in_repeat = 0;
  ast->current_variable_value = NULL;
  ast->current_field_value = NULL;
  kv_init(ast->func_arguments);
  ast->repeat_index_as = NULL;
  kv_init(ast->repeat_flow);
  ast->current_flow = NULL;
  ast->static_var = 0;
  ast->var_value_from_function = 0;

  kv_init(ast->replace_key);
  kv_init(ast->replace_val);

  ast->action_exec = NULL;
  ast->action_replace_field = NULL;

  return ast;
}

ami_t *ami_new(void)
{
  ami_t *ami;
  int retval;

  /* char *field;   */
  /* field = ami_csvread_get_field_at_line("targets.csv", 3, "target", 1); */
  
  ami = (ami_t *)malloc(sizeof(ami_t));
  if (!ami) {
    fprintf(stderr, "Cannot allocate ami_t!\n");
    return NULL;
  }

  ami->tree = ami_tree_new();
  ami->current_tree = NULL;
  ami->current_leaves = NULL;
  ami->current_line = 1;
  ami->debug = 0;
  ami->version = 0;
  ami->revision = 0;
  ami->author = NULL;
  ami->shortdesc = NULL;
  ami->tag = NULL;  

  ami->printmessagecb = NULL;
  ami->sleepcb = NULL;
  ami->action_cb = NULL;
  
  kv_init(ami->references);
  
  ami->_ast = ami_ast_new();
  if (!ami->_ast) return NULL;  

  ami->global_variables = kh_init(strhash);
  ami->repeat_variables = kh_init(strhash);
  ami->local_variables = kh_init(strhash);
  kv_init(ami->actions);

  ami->root_node = NULL;
  ami->current_node = NULL;

  kv_init(ami->values_stack);  
  
  return ami;
}

void ami_ast_tree_debug(ami_t *ami)
{
  ami_tree_debug(ami->tree);
}

int ami_nast_repeat_flow_reset(ami_t *ami) {
  size_t n_array = kv_size(ami->_ast->repeat_flow);
  if (n_array > 0) {
    for (size_t i = 0; i < n_array; i++) {
      ami_flow_t *flow = kv_A(ami->_ast->repeat_flow, i);
      ami_flow_close(flow);
      	kv_pop(ami->_ast->repeat_flow);
    }
  }
}

int ami_set_global_variable(ami_t *ami, char *key, char *val) {
  int absent;
  khint_t k;
  
  if (!ami) return 1;
  if (!ami->global_variables) return 1;  

  k = kh_put(strhash, ami->global_variables, key, &absent);
  if (absent) {
    kh_key(ami->global_variables, k) = strdup(key);
    kh_value(ami->global_variables, k) = strdup(val);
  } else {
    free(kh_value(ami->global_variables, k));
    kh_value(ami->global_variables, k) = strdup(val);
  }
  
  return 0;
}

int ami_set_local_variable(ami_t *ami, char *key, char *val) {
  int absent;
  khint_t k;
  
  if (!ami) return 1;
  if (!ami->local_variables) return 1;  

  k = kh_put(strhash, ami->local_variables, key, &absent);
  if (absent) {
    kh_key(ami->local_variables, k) = strdup(key);
    kh_value(ami->local_variables, k) = strdup(val);
  } else {
    free(kh_value(ami->local_variables, k));
    kh_value(ami->local_variables, k) = strdup(val);
  }
  
  return 0;
}

int ami_set_repeat_variable(ami_t *ami, char *key, char *val) {
  int absent;
  khint_t k;
  
  if (!ami) return 1;
  if (!ami->repeat_variables) return 1;  

  k = kh_put(strhash, ami->repeat_variables, key, &absent);
  if (absent) {
    kh_key(ami->repeat_variables, k) = strdup(key);
    kh_value(ami->repeat_variables, k) = strdup(val);
  } else {
    free(kh_value(ami->repeat_variables, k));
    kh_value(ami->repeat_variables, k) = strdup(val);
  }
  
  return 0;
}

const char *ami_get_global_variable(ami_t *ami, char *key)
{
  khint_t k;
  
  k = kh_get(strhash, ami->global_variables, key);
  int is_missing = (k == kh_end(ami->global_variables));
  if (is_missing) return NULL;
  const char *val = kh_value(ami->global_variables, k);
  return val;
}

const char *ami_get_local_variable(ami_t *ami, char *key)
{
  khint_t k;
  
  k = kh_get(strhash, ami->local_variables, key);
  int is_missing = (k == kh_end(ami->local_variables));
  if (is_missing) return NULL;
  const char *val = kh_value(ami->local_variables, k);
  return val;
}

const char *ami_get_repeat_variable(ami_t *ami, char *key)
{
  khint_t k;
  
  k = kh_get(strhash, ami->repeat_variables, key);
  int is_missing = (k == kh_end(ami->repeat_variables));
  if (is_missing) return NULL;
  const char *val = kh_value(ami->repeat_variables, k);
  return val;
}

void ami_debug(ami_t *ami)
{
  khint_t k;

  if (!ami) {
    fprintf(stderr, "Ami is null. Cannot debug!\n");
    return;
  }
  
  printf("version:%d\n", ami->version);
  printf("revision:%d\n", ami->revision);  
  if (ami->author) {
    printf("author:%s\n", ami->author);
  }
  if (ami->shortdesc) {
    printf("shortdesc:%s\n", ami->shortdesc);
  }
  if (ami->tag) {
    printf("tag:%s\n", ami->tag);
  }

  size_t n_array = kv_size(ami->references);
  if (n_array > 0) {
    for (size_t i = 0; i < n_array; i++) {
      printf("\tref %d: %s\n", i, kv_A(ami->references, i));
    }
  }

  if (ami->global_variables) {
    printf("final global variables (they can change in the flow):\n");
    for (k = 0; k < kh_end(ami->global_variables); ++k)
      if (kh_exist(ami->global_variables, k)) {
	printf("\t%s => %s\n", (char *)kh_key(ami->global_variables, k), (char *)kh_value(ami->global_variables, k));
    }
  }

  
}

void ami_erase_global_variables(ami_t *ami)
{
  khiter_t k;
  
  for (k = 0; k < kh_end(ami->global_variables); ++k) {
    if (kh_exist(ami->global_variables, k)) {
      free((char *)kh_key(ami->global_variables, k));
      free((char *)kh_value(ami->global_variables, k));
      kh_del(strhash, ami->global_variables, k);
    }
  }
  
}

void ami_erase_local_variables(ami_t *ami)
{
  khiter_t k;
  
  for (k = 0; k < kh_end(ami->local_variables); ++k) {
    if (kh_exist(ami->local_variables, k)) {
      free((char *)kh_key(ami->local_variables, k));
      free((char *)kh_value(ami->local_variables, k));
      kh_del(strhash, ami->local_variables, k);
    }
  }
  
}

void ami_erase_repeat_variables(ami_t *ami)
{
  khiter_t k;
  
  for (k = 0; k < kh_end(ami->repeat_variables); ++k) {
    if (kh_exist(ami->repeat_variables, k)) {
      free((char *)kh_key(ami->repeat_variables, k));
      free((char *)kh_value(ami->repeat_variables, k));
      kh_del(strhash, ami->repeat_variables, k);
    }
  }
  
}


void ami_close(ami_t *ami)
{
  ami_node_t *n;
  khint_t k;

  if (!ami) return;
  
  kv_destroy(ami->references);
  
  for (k = 0; k < kh_end(ami->global_variables); ++k) {
    if (kh_exist(ami->global_variables, k)) {
      free((char *)kh_key(ami->global_variables, k));
      free((char *)kh_value(ami->global_variables, k));
    }
  }

  for (n = ami->root_node; n; n = n->next) {
    if (n->strval) {
      free(n->strval);
    }
    if (n->right) {
      for (ami_node_t *r = n->right; r; r = r->right) {
	if (r->strval) {
	  free(r->strval);
	}
	free(r);
      }
    }
    free(n);
  }  
  
  free(ami);
}

int ami_validate(ami_t *ami)
{
  if (ami->version == 0) {
    ami->error = NO_VERSION;
    return 1;
  }
  
  return 0;
}

char *ami_error_to_string(ami_t *ami) {
  if (!ami) return "AMI struct not defined!";
  
  switch(ami->error) {
  case NO_VERSION:
    return "Please set 'ami_version 1' in the file.";
    break;
  default:
    return "no error";
    break;
  }
  
  return "huh?";
}


int ami_parse_file(ami_t *ami, char *file)
{
	char *string = "This is a bunch of words";
	yyscan_t scanner;
	int state;
	FILE *fpin;

	if (!ami) {
	  fprintf(stderr, "Cannot parse file %s as library initialization failed.\n", file);
	  return 1;
	}
	
	if (ami_yylex_init(&scanner) != 0) {
		fprintf(stderr, "Error initializing yylex\n");
		return 1;
	}

	fpin = fopen(file, "r");
	if (!fpin) {
	  fprintf(stderr, "Error parsing %s\n", file);
	  return 1;
	}
	state = ami_yyrestart(fpin, scanner);
	
	/* state = ami_yy_scan_string(string, scanner); */
	if (ami_yyparse(scanner, ami) != 0) {
	        fprintf(stderr, "Error with yyparse\n");
		return 1;
	}
	/* ami_yy_delete_buffer(state, scanner); */
	ami_yylex_destroy(scanner);
	
	fflush(fpin);
	fclose(fpin);

	return ami_validate(ami);
}

void ami_set_message_callback(ami_t *ami, print_message_cb message_cb)
{
  ami->printmessagecb = message_cb;
}

void ami_set_sleep_callback(ami_t *ami, sleep_cb sleep_cb)
{
  ami->sleepcb = sleep_cb;
}

int ami_loop(ami_t *ami, foreach_action_cb action_cb, void *user_data)
{

}

void ami_set_action_callback(ami_t *ami, ami_action_cb action_cb, void *userdata)
{
  ami->action_cb = action_cb;
  ami->action_cb_userdata = userdata;
}

void ami_append_item(ami_t *ami, ami_node_type_t type, char *strval, int intval)
{
  /* GOOGLE DRIVE: repeat block_id:0; action block id:1; opened_sections:1 */
  /* if (strval) { */
  /*   if (!strcmp("GoogleDriveDownload", strval)) { */
  /*     printf("GOOGLE DRIVE: repeat block_id:%d; action block id:%d; opened_sections:%d\n", ami->_ast->repeat_block_id, ami->_ast->action_block_id, ami->_ast->opened_sections); */
  /*   } */
  /* } */
  
  if (ami->_ast->repeat_block_id > 0) {
    // All the stuff we repeat come one after another
    ami_node_create_right(&ami->root_node, type, strval, intval);
  } else {
    /* if ((ami->_ast->action_block_id > 0) && (ami->_ast->repeat_block_id <= 0)) { */
    /*   if (ami->_ast->action_block_id == ami->_ast->opened_sections) { */
    /* 	ami_node_create_right(&ami->root_node, type, strval, intval); */
    /* 	return; */
    /*   } */
    /* } */
    ami_node_create(&ami->root_node, type, strval, intval);
  }
}

char *ami_get_variable(ami_t *ami, char *key)
{
  char *retval;
  if (!key) {
    fprintf(stderr, "Key is NULL! No Variable to extract!\n");
    return "";
  }

  if (strlen(key) > 0) {
    if (key[0] != '$') return key; // This is not a variable
  }
  
  retval = ami_get_local_variable(ami, key);
  if (retval) return retval;
  
  retval = ami_get_repeat_variable(ami, key);
  if (retval) return retval;

  return ami_get_global_variable(ami, key);  
}
