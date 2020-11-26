#include <iostream>

#include <ami/ami.h>
#include <ami/action.h>
#include <ami/ast.h>

#include <ami/khash.h>

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <ami.hpp>

namespace py = pybind11;

void Ami::foreach_action(ami_action_t *amiaction, void *userdata)
{
  ami_field_action_t *field_action;
  khint_t k;
  
  Ami *pami = (Ami *)userdata;
  Action *action = new Action();
  action->set_name(ami_action_get_name(amiaction));
  action->set_exec(ami_action_get_exec(amiaction));
  action->sleep_cursor = amiaction->sleep_cursor;
  action->repeat_index = amiaction->repeat_index;
  
  // ami_action_debug(pami->_ami, amiaction);
  
  if (pami->_ami->global_variables) {
    for (k = 0; k < kh_end(pami->_ami->global_variables); ++k)
      if (kh_exist(pami->_ami->global_variables, k)) {
	char *key = (char *)kh_key(pami->_ami->global_variables, k);
	char *value = (char *)kh_value(pami->_ami->global_variables, k);
	action->variables[key] = ami_get_variable(pami->_ami, value);
      }
  }
  
  if (pami->_ami->repeat_variables) {
    for (k = 0; k < kh_end(pami->_ami->repeat_variables); ++k)
      if (kh_exist(pami->_ami->repeat_variables, k)) {
	char *key = (char *)kh_key(pami->_ami->repeat_variables, k);
	char *value = (char *)kh_value(pami->_ami->repeat_variables, k);
	action->variables[key] = ami_get_variable(pami->_ami, value);
      }
  }
  if (pami->_ami->local_variables) {
    for (k = 0; k < kh_end(pami->_ami->local_variables); ++k)
      if (kh_exist(pami->_ami->local_variables, k)) {
	char *key = (char *)kh_key(pami->_ami->local_variables, k);
	char *value = (char *)kh_value(pami->_ami->local_variables, k);
	action->variables[key] = ami_get_variable(pami->_ami, value);
      }
  }

  for (field_action=amiaction->field_actions; field_action; field_action=field_action->next) {
    if (field_action->left) {
      action->field_actions[field_action->field][field_action->action][field_action->left] = field_action->right;
    } else {
      action->field_actions[field_action->field][field_action->action][field_action->right] = "";
    }
  }
  
  pami->actions.push_back(action);  
}

Ami::Ami(void) {
  _ami = ami_new();
}

Ami::~Ami(void) {
  ami_close(_ami);
}

std::vector<Action*> Ami::GetActions(void) {
  return actions;
}

int Ami::Parse(std::string file) {
  int ret;

  file_path = file;
  
  ami_set_action_callback(_ami, foreach_action, this);

  ret = ami_parse_file(_ami, (char *)file.c_str());
  if (ret) {
    if (_ami->error) {
      std::cerr << "Error: " << ami_error_to_string(_ami) << std::endl;
      return 1;
    }
  }  

  ami_ast_walk_actions(_ami);
  
  // ami_debug(_ami);
  return 0;
}

void Ami::Debug(void) {
  ami_debug(_ami);
}

std::vector<std::string> Ami::GetReferences(void) {
  std::vector<std::string> references;
  ami_t *ami = _ami;
  
  size_t n_array = kv_size(ami->references);
  
  if (n_array > 0) {
    for (size_t i = 0; i < n_array; i++) {
      references.push_back(kv_A(ami->references, i));
    }
  }
  
  return references;
}

std::vector<std::string> Ami::GetTags(void) {
  std::vector<std::string> tags;
  ami_t *ami = _ami;
  size_t n_array = kv_size(ami->tags);
  
  if (n_array > 0) {
    for (size_t i = 0; i < n_array; i++) {
      tags.push_back(kv_A(ami->tags, i));
    }
  }
  
  return tags;
}

PYBIND11_MODULE(pyami, m) {
    m.doc() = "AMI Language for Python";
    py::class_<Ami>(m, "Ami")
      .def(py::init<>())
      .def("GetActions", &Ami::GetActions)
      .def("Parse", &Ami::Parse)
      .def("GetFilePath", &Ami::GetFilePath)
      .def("GetReferences", &Ami::GetReferences)
      .def("GetTags", &Ami::GetTags)
      .def("GetSleepCursor", &Ami::GetSleepCursor)
      .def("Debug", &Ami::Debug);    
    py::class_<Action>(m, "Action")
      .def(py::init<>())
      .def("Variables", &Action::get_variables)
      .def("FieldActions", &Action::get_field_actions)
      .def("Name", &Action::get_name)
      .def("Exec", &Action::get_exec)
      .def("GetRepeatIndex", &Action::GetRepeatIndex)
      .def("GetSleepCursor", &Action::GetSleepCursor);

}
