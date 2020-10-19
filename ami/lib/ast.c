#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

#include <ami/kvec.h>

#include <ami/ami.h>
#include <ami/action.h>
#include <ami/tree.h>
#include <ami/ast.h>
#include <ami/csvread.h>

static void walk_node(ami_t *ami, ami_node_t *node, int repeat_index, int right)
{
  ami_node_t *n;
  ami_action_t *action;
  int index;
  char *stack_str = NULL; // Keeping the last value
  int stack_int = 0;
  char *tmp_str;
  static char *csv_args[4] = { NULL, NULL, NULL, NULL }; // For now, we only have the CSV function
  kvec_t(char *) values_stack;
  static int varpos = 0;  
  ami_field_action_t *field_action;
  
  for (n = node; n; n = right ? n->right : n->next) {
    switch(n->type) {
    case AMI_NT_REFERENCE:
      kv_push(char *, ami->references, n->strval);      
      break;
    case AMI_NT_ACTION:
      ami->in_action = 1;
      action = ami_action_new();
      action->name = n->strval;
      break;
    case AMI_NT_EXEC:
      action->exec = n->strval;      
      break;
    case AMI_NT_ACTIONCLOSE:
      ami->in_action = 0;

      /* ami_field_action_debug(action); */
      
      action = ami_action_copy_variables(ami, action);
      if (ami->action_cb) {
	ami->action_cb(action, ami->action_cb_userdata);
      } else {
	fprintf(stderr,"*** Warning: No Action Callback Set!\n");
      }
      ami_action_close(action);
      /* ami_erase_local_variables(ami); */
      break;
    case AMI_NT_REPEAT:
      ami->in_repeat = 1;
      for (index = 1; index <= n->intval; index++) {
	char *indexvar;
	asprintf(&indexvar, "%d", index);
	ami_set_global_variable(ami, n->strval, indexvar);
	/* printf("We run a repeat action at index:%d\n", index); */
      	walk_node(ami, n->right, index, 1);
      }
      ami_erase_repeat_variables(ami);
      ami->in_repeat = 0;
      index = 0;
      break;
    case AMI_NT_MESSAGE:
      printf("%s\n", n->strval);
      break;
    case AMI_NT_VARVALSTR:
      kv_push(char *, ami->values_stack, strdup(n->strval));      
      break;
    case AMI_NT_VARVALINT:
      asprintf(&tmp_str, "%d", n->intval);
      kv_push(char *, ami->values_stack, strdup(tmp_str));
      free(tmp_str);
      /* kv_a(char *, ami->values_stack, strdup(n->strval)); */
      break;
    case AMI_NT_VARVAR:
      kv_push(char *, ami->values_stack, strdup(n->strval));            
      break;
    case AMI_NT_VARNAME:
      if (ami->in_repeat || ami->in_action) {
	if (!ami->in_action) {
	  /* printf("%s is a local repeat variable with value:%s\n", n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1)); */
	  ami_set_repeat_variable(ami, n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1));
	} else {
	  /* printf("%s is a local action variable with value:%s\n", n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1)); */
	  ami_set_local_variable(ami, n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1));
	}
      } else {
	/* printf("%s is a global variable with value:%s\n", n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1)); */
	  ami_set_global_variable(ami, n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1));
      }
      break;
    case AMI_NT_FIELDFUNC:
      /* printf("Fieldfunc :%s\n", n->strval); // ip */
      if (!strcmp("replace", kv_A(ami->values_stack, kv_size(ami->values_stack)-1))) {
	size_t stacklen = kv_size(ami->values_stack);
	/* printf("the stacklen when we replace:%ld\n", stacklen); */
	/* for (size_t i = 0; i < stacklen; i++) { */
	/*   printf("i:%d;val:%s\n", i, kv_A(ami->values_stack, i)); */
	/* } */

	for (int i = 1; i <= ami->replace_count; i++) {
	  int pos_from = (i*-1)-i; 
	  int pos_to = (i*-1)-(i+1);
	  char *to = kv_A(ami->values_stack, stacklen+pos_to);
	  char *from = kv_A(ami->values_stack, stacklen+pos_from);
	  
	  field_action = ami_field_action_new();
	  field_action->field = n->strval;
	  field_action->action = "replace";
	  field_action->left = ami_get_variable(ami, from);
	  field_action->right = ami_get_variable(ami, to);
	  action->field_actions = ami_field_action_append(action->field_actions, field_action);
	  
	}
	ami->replace_count = 0;
      } else {
	fprintf(stderr, "Unknown function call on field %s: %s\n", n->strval, kv_A(ami->values_stack, kv_size(ami->values_stack)-1));
	exit(1);
      }
      
      break;
    case AMI_NT_FIELDVAR:
      /* printf("Fieldvar :%s\n", n->strval); // FullFilePath */
      field_action = ami_field_action_new();
      field_action->field = n->strval;
      field_action->action = "set";
      char *value = ami_get_variable(ami, kv_A(ami->values_stack, kv_size(ami->values_stack)-1));
      field_action->right = value;
	
      action->field_actions = ami_field_action_append(action->field_actions, field_action);
      break;
    case AMI_NT_REPLACE:
      {
	/* ami_print_all_variables(ami); */
	/* char *replace_with = ami_get_variable(ami, kv_A(ami->values_stack, kv_size(ami->values_stack)-1)); */
	/* printf("replace:%s with:%s\n", n->strval, replace_with); */
	kv_push(char *, ami->values_stack, strdup(n->strval));
	ami->replace_count++;
      }
      break;
    case AMI_NT_FUNCTION:
      if (!strcmp("csv", n->strval)) {
	/* size_t stacklen = kv_size(ami->values_stack); */
	/* for (size_t i = 0; i < stacklen; i++) { */
	/*   printf("i:%d;val:%s\n", i, kv_A(ami->values_stack, i)); */
	/* }	 */
	
	int has_header = (int)strtod(kv_A(ami->values_stack, kv_size(ami->values_stack)-1), NULL);
	char *field = kv_A(ami->values_stack, kv_size(ami->values_stack)-2);
	char *line_val = ami_get_variable(ami, kv_A(ami->values_stack, kv_size(ami->values_stack)-3));
	if (!line_val) {
	  fprintf(stderr, "Cannot get the variable value from %s\n", kv_A(ami->values_stack, kv_size(ami->values_stack)-3));
	  exit(1);
	}
	int line_in_csv = (int)strtod(line_val, NULL);
	char *file = kv_A(ami->values_stack, kv_size(ami->values_stack)-4);

	/* printf("file:%s;line_in_csv:%d;field:%s;has_header:%d\n", file, line_in_csv, field, has_header); */
	char *result = ami_csvread_get_field_at_line(file, line_in_csv, field, has_header);
	if (!result) {
	  fprintf(stderr, "Error reading CSV file %s, field:%s, line:%d\n", file, field, line_in_csv);
	  exit(1);
	} else {
	  kv_push(char *, ami->values_stack, strdup(result));
	}
      } else if (!strcmp("replace", n->strval)) {
	kv_push(char *, ami->values_stack, strdup("replace"));// So we know we have a replace to perform
	/* printf("We are going to REPLACE!\n"); */
      } else {
      	fprintf(stderr, "Unhandled function:%s\n", n->strval);
	kv_push(char *, ami->values_stack, strdup(n->strval));
      }
      varpos = 0;
      break;
    }
  }
}

int ami_ast_walk_actions(ami_t *ami)
{
  
  if (!ami) {
    fprintf(stderr, "Ami is empty, cannot run %s!\n", __FUNCTION__);
    return -1;
  }

  if (!ami->root_node) {
    fprintf(stderr, "Ami root node is empty, cannot run %s!\n", __FUNCTION__);
    return -1;
  }

  walk_node(ami, ami->root_node, 0, 0);

  return 0;
}
