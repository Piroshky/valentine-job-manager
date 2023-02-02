// Commandline Executable that will tell you the status of the most recent jobs.
// Slightly easier than digging through the directories.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>

char *vjm_path = "/home/" USER "/.config/valentine-job-manager/";

int directories_only(const struct dirent *e) {
  if (e->d_type != DT_DIR) {
    return 0;
  }
  if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
    return 0;
  }
  return 1;
}

int status_files_only(const struct dirent *e) {
  if (e->d_type != DT_REG) {
    return 0;
  }
  int flen = strlen(e->d_name);
  if (flen < 11) {
    return 0;
  }
  if (strcmp(&(e->d_name[flen - 11]), "_status.txt") != 0) {
    return 0;
  }
  return 1;
}

// Check if s2 is a prefix of s1
int prefix(char *s1, char *s2) {
  return strncmp(s1, s2, strlen(s2)) == 0;
}

int main (void) {

  struct dirent **namelist;
  int n;

  // get the job folders
  n = scandir(vjm_path, &namelist, directories_only, alphasort);
  if (n == -1) {
    perror("scandir");
    return -1;
  }

  int longest_job_name = 0;
  for (int i = 0; i < n; ++i) {
    int l = strlen(namelist[i]->d_name);
    if (l > longest_job_name) { longest_job_name = l; }    
  }

  // get the path of the most recent status file
  char *file_status_dir = malloc(sizeof(char) * (strlen(vjm_path) + longest_job_name + 10));

  for (int j = 0; j < n; ++j) {
    sprintf(file_status_dir, "%s%s/status/", vjm_path, namelist[j]->d_name);
    struct dirent **file_statuses;
    int m;
    m = scandir(file_status_dir, &file_statuses, status_files_only, alphasort);
    for (int i = 0; i < m-1; ++i) {
      free(file_statuses[i]);
    }
  
    char pathname[1024];
    FILE *fp;
    sprintf(pathname, "%s%s", file_status_dir,file_statuses[m-1]->d_name);
    free(file_statuses[m-1]);
    free(file_statuses);

    bool exit_code_p = false;
    int exit_code;
    fp = fopen(pathname, "r");
    if (fp == NULL) {
      fprintf(stderr, "Couldn't open file\n");
    } else {
  
      char *line = NULL;
      size_t len = 0;
      ssize_t read;
      while (read = getline(&line, &len, fp) != -1) {
	if (prefix(line, "Exit Code:")) {
	  exit_code_p = true;
	  int ix = 0;
	  while ( *(line + ix) != ':') {++ix;}
	  ++ix;
	  while ( *(line + ix) == ' ') {++ix;}
	  sscanf(line+ix, "%d", &exit_code);
	  break;
	}
	free(line);
	line = NULL;
	len = 0;
      }
    }

    char *pass_fail;
    if (exit_code_p) {
      pass_fail = (exit_code == 0) ? "PASS" : "FAIL";
    } else {
      pass_fail = "????";
    }
  
    printf("[%s] %-*s  |\n", pass_fail, longest_job_name, namelist[j]->d_name);
    free(namelist[j]);
   
  }
  free(namelist);

  return 0;
}
