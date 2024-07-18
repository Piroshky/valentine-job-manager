#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#define CONFIG_LINE_SIZE 256

typedef enum {
  STRING,
  BOOL
} Config_type;

typedef struct {
  char        *name;
  Config_type  type;
  char        *value;
  bool         manually_set;
} config_param;

typedef struct {
  int num_params;
  config_param *parameters;
} config;

void print_config(config *conf);
config_param *find_config_param(config *conf, char *param);
char *read_config_line(char *line, config *conf);
