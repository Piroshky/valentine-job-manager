#include "config.h"

/* config_param vjm_status_params[] = { */
/*   {"default_status_filter", STRING, "all", false}, */
/*   {"show_status_color", BOOL, "false", false} */
/* }; */

/* bool eval_config(config *conf) { */
/*   for (int i = 0; i < conf->num_params; ++i) { */
/*     config_param *p = &(conf->parameters[i]); */

/*     if (strcmp(p->name, "default_status_filter") == 0) { */
      
/*       if (strcmp(p->value, "all")) { */
/* 	display_all = true;	 */
/*       } else if (strcmp(p->value, "fail")) { */
/* 	display_all = false; */
/* 	display_only = FAIL; */
/*       } else if (strcmp(p->value, "pass")) { */
/* 	display_all = false; */
/* 	display_only = PASS; */
/*       } else if (strcmp(p->value, "unknown")) { */
/* 	display_all = false; */
/* 	display_only = UNKNOWN; */
/*       } else { */
/* 	fprintf(stderr, "unrecognized value %s, for parameter %s\n", p->value, p->name); */
/* 	return false; */
/* 	// todo return err? */
/*       } */
      
/*     } else if (strcmp(p->name, "show_status_color") == 0) { */
      
/*     } else { */
/*       fprintf(stderr, "Unhandled parameter %s\n", p->name); */
/*     }     */
/*   } */
  
/*   return true; */
/* } */

/* config vjm_status_conf = { */
/*   sizeof(vjm_status_params) / sizeof(config_param), */
/*   vjm_status_params */
/* }; */

void print_config(config *conf) {
  for (int i = 0; i < conf->num_params; ++i) {
    config_param *p = &(conf->parameters[i]);
    printf("%s = %s %s\n", p->name, p->value, p->manually_set ? "" : "(default)");
  }
}

config_param *find_config_param(config *conf, char *param) {
  int param_len = strlen(param);
  for (int i = 0; i < conf->num_params; ++i) {
    if (strncmp(conf->parameters[i].name, param, param_len) == 0) {
      return &(conf->parameters[i]);
    }
  }
  return NULL;
}

char *read_config_line(char *line, config *conf) {
  char param_name[CONFIG_LINE_SIZE];
  char value[CONFIG_LINE_SIZE];

  // comment
  if (sscanf(line, " %[#]", param_name) == 1) {return NULL;}

  // blank line
  if (sscanf(line, " %s", param_name) == EOF) {return NULL;}

  // paramater/value pair
  if (sscanf(line, "%s %s", param_name, value) == 2) {
    config_param *param = find_config_param(conf, param_name);
    if (param == NULL) {
      char *error_message = malloc(sizeof(char) * 2 * CONFIG_LINE_SIZE);
      snprintf(error_message, 2 * CONFIG_LINE_SIZE, "Parameter '%s' not recognized\n", param_name);
      return error_message;
    }
    if (param->manually_set) {
      char *error_message = malloc(sizeof(char) * 2 * CONFIG_LINE_SIZE);
      snprintf(error_message, 2 * CONFIG_LINE_SIZE, "Parameter '%s' set a second time\n", param_name);
      return error_message;
    }
    param->manually_set = true;

    int name_len = strlen(param_name) + 1;
    param->name = malloc(sizeof(char) * name_len);
    if (param->name == NULL) {
      return "malloc error";
    }
    strncpy(param->name, param_name, name_len);

    int value_len = strlen(value) + 1;
    param->value = malloc(sizeof(char) * value_len);
    if (param->value == NULL) {
      return "malloc error";
    }
    strncpy(param->value, value, value_len);
    
    
    return NULL;
  }

  return "Could not parse line";
}

/* void main() { */

/*   const char *homedir; */
/*   if ((homedir = getenv("HOME")) == NULL) { */
/*     homedir = getpwuid(getuid())->pw_dir; */
/*   } */

/*   char *vjm_status_config_subpath = "/.config/vjm-status/config"; */
/*   int full_path_len = strlen(homedir) + strlen(vjm_status_config_subpath) + 1; */
/*   char *config_full_path = malloc(sizeof(char) * full_path_len);   */
/*   if (config_full_path == NULL) { */
/*     // todo */
/*     printf("malloc failed\n"); */
/*     return; */
/*   } */
/*   int ret = snprintf(config_full_path, full_path_len, "%s%s", homedir, vjm_status_config_subpath); */
/*   if (ret != full_path_len - 1) { */
/*     // todo */
/*     printf("snprintf failed: %d\n", ret); */
/*     return;  */
/*   } */
  
/*   FILE *fp = fopen(config_full_path, "r"); */
/*   if (fp == NULL) { */
/*     // no config file */
/*     // todo remove, debug */
/*     printf("Could not open config file :%s", config_full_path); */
/*     return; */
/*   } */

/*   /\* printf("Opened %s\n", config_full_path); *\/ */

/*   int line_num = 1; */
/*   char config_line[CONFIG_LINE_SIZE]; */
/*   while(fgets((char *) &config_line, CONFIG_LINE_SIZE, fp)) { */
/*     char *err_msg = read_config_line(config_line, &vjm_status_conf); */
/*     if (err_msg != NULL) { */
/*       printf("~/.config/vjm-status/config:%d:0: %s\n", line_num, err_msg); */
/*       printf("line:%s\n", config_line); */
/*       return; */
/*     } */
/*     line_num += 1; */
/*   } */

/*   printf("Read config file\n"); */
/*   print_config(&vjm_status_conf); */
   
/* } */
