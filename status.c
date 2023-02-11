// Commandline Executable that will tell you the status of the most recent jobs.
// Slightly easier than digging through the directories.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <termios.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <limits.h>
#include <poll.h>

#include <ctype.h>

#define BUF_LEN sizeof(struct inotify_event) + NAME_MAX + 1


char *vjm_path = "/home/" USER "/.config/valentine-job-manager/";

enum exit_status {
  PASS, FAIL, UNKNOWN
};
const char *exit_status_strings[] = {"PASS", "FAIL", "????"};

const char *status_files_strings[] = {"stdout_and_stderr", "stdout", "stderr"};

struct ui_line {
  enum exit_status exit_status;
  char *job_name;
  char *next_run;
  char *status_file_prefix;
  int  num_status_files;
  bool status_files[3];
};

//// UI State
struct termios original_termios;

int num_jobs = 0;
struct ui_line *job_lines = NULL; // array of all the structs containing job information

bool display_all = true; // these variables are used to control job filtering
enum exit_status display_only = FAIL;
int num_pass = 0;

int displayed_lines  = 0;
int displayed_jobs   = 0;

int selected_line    = 0; // cursor information
int selected_file    = 0;
int selected_column  = 0;

int longest_job_name = 0;

void print_help() {
  int plen = strlen(vjm_path) + 12;
  plen += (plen % 2) ? 1 : 0;
  for (int i = 0; i < (plen-6)/2; ++ i) {
    printf("=");
  }
  printf(" HELP ");
  for (int i = 0; i < (plen-6)/2; ++ i) {
    printf("=");
  }  
  printf("\n"
	 "State:\n"
	 "  vjm_path: %s\n\n"
	 "Controls:\n"
	 "  toggle help            h\n"
	 "  move cursor            arrow keys\n"
	 "  open status file       enter\n"
	 "  reload job statuses    r\n"
	 "  filter by exit status  f fail\n"
	 "                         p pass\n"
	 "                         ? unknown\n"
	 "                         a all\n",
	 vjm_path);
  for (int i = 0; i < plen; ++ i) {
    printf("=");
  }
  printf("\n");
  displayed_lines += 14;
}

void restore_termios() {
  printf("\e[?25h"); // show cursor
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

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

int max(int a, int b) {
  return (a > b) ? a : b;
}
int min(int a, int b) {
  return (a < b) ? a : b;
}

void draw_ui() {
  // Display the UI
  char pass_count_string[256];
  if (display_all) {
    sprintf(pass_count_string, "%d/%d", num_pass, num_jobs);
  } else {
    sprintf(pass_count_string, "%s", exit_status_strings[display_only]);
  }
  int first_column_width = max(4, strlen(pass_count_string));
  printf("%-*s  %-*sNext Run                        Status Files\n", first_column_width, pass_count_string, longest_job_name+1, "Job");
  displayed_lines += 2;

  if (display_all) {
    displayed_jobs = num_jobs;
  } else {
    displayed_jobs = 0;
    for (int i = 0; i < num_jobs; ++i) {
      if (display_only == job_lines[i].exit_status) {
	displayed_jobs += 1;
      }
    }
    if (selected_line > displayed_jobs) {
      selected_line = max(0, displayed_jobs - 1);
    }
  }
 
  for (int i = 0; i < num_jobs; ++i) {
    if (!display_all && display_only != job_lines[i].exit_status) {
      continue;
    }
    
    printf("%*s ", first_column_width, exit_status_strings[job_lines[i].exit_status]);
    if (selected_line == i) { // begin inverse mode
      printf("\e[7m");
    }
    printf(" %-*s", longest_job_name, job_lines[i].job_name);
    if (selected_line == i) { // end inverse mode
      printf("\e[27m");
    }
    
    printf(" %s ", job_lines[i].next_run);
    for (int k = 0; k < 3; ++k) {
      if (job_lines[i].status_files[k]) {
	if (selected_line == i && selected_file == k) {
	  printf("\e[7m");
	}
	printf(" %s ", status_files_strings[k]);
	if (selected_line == i && selected_file == k) {
	  printf("\e[27m");
	}
	printf(" ");
      }
    }
    if (i != num_jobs - 1) {
      printf("\n");
      displayed_lines += 1;
    }

  }
  fflush(stdout);
}

void load_job_statuses() {
  longest_job_name = 0;
  num_pass = 0;
  struct dirent **namelist;

  // get the job folders
  num_jobs = scandir(vjm_path, &namelist, directories_only, alphasort);
  if (num_jobs == -1) {
    perror("scandir");
    exit(-1);
  }

  if (job_lines != NULL) {
    free(job_lines);
  }
  job_lines = malloc(sizeof(struct ui_line) * num_jobs);
  
  for (int i = 0; i < num_jobs; ++i) {
    int l = strlen(namelist[i]->d_name);
    if (l > longest_job_name) { longest_job_name = l; }    
  }
  longest_job_name += 1;

  // Populate the ui_lines
  char *file_status_dir = malloc(sizeof(char) * (strlen(vjm_path) + longest_job_name + 10));
  for (int j = 0; j < num_jobs; ++j) {
    job_lines[j].num_status_files = 0;
    // get the path of the most recent status file
    sprintf(file_status_dir, "%s%s/status/", vjm_path, namelist[j]->d_name);
    struct dirent **file_statuses;
    int m;
    m = scandir(file_status_dir, &file_statuses, status_files_only, alphasort);
    for (int i = 0; i < m-1; ++i) {
      free(file_statuses[i]);
    }
  
    char pathname[1024];
    FILE *fp;
    sprintf(pathname, "%s%s", file_status_dir, file_statuses[m-1]->d_name);
    free(file_statuses[m-1]);
    free(file_statuses);

    bool exit_code_p = false;
    int exit_code;
    char *next_run = malloc(sizeof(char) * 100); //todo
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
	} else if (prefix(line, "Next Run:")) {
	  int ix = 0;
	  while ( *(line + ix) != ':') {++ix;}
	  ++ix;
	  while ( *(line + ix) == ' ') {++ix;}
	  int jx = 0;
	  while (( *(line + ix) != '\n')) {
	    next_run[jx++] = *(line + ix++);
	  }
	  next_run[jx] = 0;

	}
	free(line);
	line = NULL;
	len = 0;
      }
    }

    if (exit_code_p) {
      if (exit_code == 0) {
	num_pass += 1;
      }
      job_lines[j].exit_status = (exit_code == 0) ? PASS : FAIL;
    } else {
      job_lines[j].exit_status = UNKNOWN;
    }

    char *job_name = malloc(sizeof(char) * (strlen(namelist[j]->d_name) + 1));
    strcpy(job_name, namelist[j]->d_name);
    job_lines[j].job_name = job_name;
    job_lines[j].next_run = next_run;

    int pl = strlen(pathname) - 11;
    char modified_path[1024];
    strncpy((char *)&modified_path, pathname, pl);

    job_lines[j].status_file_prefix = malloc(sizeof(char) * (pl + 1));
    strncpy(job_lines[j].status_file_prefix, pathname, pl);
    job_lines[j].status_file_prefix[pl] = 0;
    
    // stdout_and_err
    modified_path[pl] = 0;
    strcpy(&(modified_path[pl]), "_stdout_and_stderr.txt");
    if (access(modified_path, F_OK) == 0) {
      job_lines[j].status_files[0] = true;
      job_lines[j].num_status_files += 1;
    } else {
      job_lines[j].status_files[0] = false;
    }
    
    strcpy(&(modified_path[pl]), "_stdout.txt");
    if (access(modified_path, F_OK) == 0) {
      job_lines[j].status_files[1] = true;
      job_lines[j].num_status_files += 1;
    } else {
      job_lines[j].status_files[1] = false;
    }
    
    strcpy(&(modified_path[pl]), "_stderr.txt");
    if (access(modified_path, F_OK) == 0) {
      job_lines[j].status_files[2] = true;
      job_lines[j].num_status_files += 1;
    } else {
      job_lines[j].status_files[2] = false;
    }

    free(namelist[j]);
   
  }
  free(namelist);  
}

int main (int argc, char **argv) {

  if (argc > 1) {
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0) {
      print_help();
    } else {
      printf("Unrecognized argument: %s\n", argv[1]);
      print_help();
    }

    exit(0);
  }

  char buf[BUF_LEN];
  struct pollfd fds[2];
  nfds_t nfds = 0;

  fds[0].fd = STDIN_FILENO;
  fds[0].events = POLLIN;
  nfds += 1;
  
  int inotify_fd = inotify_init1(IN_NONBLOCK);
  if (inotify_fd != -1) {
    fds[1].fd = inotify_fd;
    fds[1].events = POLLIN;
    inotify_add_watch(inotify_fd, "/home/drew/.config/valentine-job-manager/", IN_ATTRIB);
    nfds += 1;
  }
  
  load_job_statuses();

  // Save termios, restore on exit
  tcgetattr(STDIN_FILENO, &original_termios);
  atexit(restore_termios);
  
  struct termios program_termios = original_termios;
  // Turn off ctrl-s / ctrl-q
  program_termios.c_iflag &= ~(IXON);
  // Enable raw mode, disable canonical mode, disable ctrl-z/c
  // I will manually re-enable ctrl-c, but suspending the program seems to cause termios to reset
  // when it's restored to the foreground, and there's no reason to suspend this program anyways.
  program_termios.c_lflag &= ~(ECHO | ICANON | ISIG);
  
  program_termios.c_cc[VMIN]  = 0;
  //program_termios.c_cc[VTIME] = -1;
  
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &program_termios);

  printf("\e[?25l"); //hide cursor
  
  draw_ui();

  bool update_ui = false;
  bool show_help = false;
  bool line_change = false;
  int  file_change = 0;

  int poll_ret;
  while (1) {

    poll_ret = poll(fds, nfds, -1);

    if (poll_ret <= 0) {
      continue;
    }
    
    if (fds[0].revents & POLLIN) {
      char c = 0;
      read(STDIN_FILENO, &c, 1);
    
      if (c == '\e') {
	read(STDIN_FILENO, &c, 1);
	if (c == '[') {
	  read(STDIN_FILENO, &c, 1);
	  switch(c) {
	  case 'A': { // up
	    update_ui = true;
	    line_change = true;
	    if (displayed_jobs > 0) {
	      selected_line = (num_jobs + selected_line-1) % displayed_jobs;
	    }
	    break; 
	  }
	  case 'B': { // down
	    update_ui = true;
	    line_change = true;
	    if (displayed_jobs > 0) {
	      selected_line = (selected_line+1) % displayed_jobs;
	    }
	    break; 
	  }
	  case 'C': { // right
	    if (job_lines[selected_line].num_status_files <= 1) {
	      break;
	    }	  
	    update_ui = true;
	    file_change = 1;
	    break; 
	  } 
	  case 'D': { // left
	    if (job_lines[selected_line].num_status_files <= 1) {
	      break;
	    }
	    update_ui = true;
	    file_change = -1;
	    break;
	  }
	  }
	}
      } else if (c == 'q' || c == 3) {
	break;
      } else if (c == 'h') {
	show_help = !show_help;
	update_ui = true;
      } else if (c == 'p') {
	display_all = false;
	display_only = PASS;
	update_ui = true;
      } else if (c == 'f') {
	display_all = false;
	display_only = FAIL;
	update_ui = true;
      } else if (c == '?') {
	display_all = false;
	display_only = UNKNOWN;
	update_ui = true;      
      } else if (c == 'a') {
	display_all = true;
	update_ui = true;
      } else if (c == 'r') {
	load_job_statuses();
	update_ui = true;
      } else if (c == 10) {
	int pid = fork();
	if (pid == 0) {
	  int fd = open("/dev/null",O_WRONLY | O_CREAT, 0666);
	  dup2(fd, 1);
	  dup2(fd, 2);
	  char file_path[1024];
	  sprintf(file_path, "%s_%s.txt", job_lines[selected_line].status_file_prefix, status_files_strings[selected_file]);
	  execl("/usr/bin/xdg-open", "xdg-open", file_path, (char *)0);
	  exit(1);
	}
      }
    }

    
    if (nfds == 2 && (fds[1].revents & POLLIN)) {
      read(inotify_fd, buf, sizeof buf); // might not be necessary, but probably clears the fd
      load_job_statuses();
      update_ui = true;
    }

    if (update_ui) {
      
      if (line_change && job_lines[selected_line].num_status_files > 0) { // this isn't guaranteed to do the right thing visualy
	line_change = false;

	int n_cols = job_lines[selected_line].num_status_files;
	if (selected_column > n_cols-1) {
	  selected_column = n_cols-1;
	}
	int col = -1;
	for (int i = 0; i < 3; ++i) {
	  if (job_lines[selected_line].status_files[i]) {
	    col += 1;
	  }
	  if (col == selected_column) {
	    selected_file = i;
	    break;
	  }
	}
	      
      } else if (file_change != 0) {

	int n_cols = job_lines[selected_line].num_status_files;

	selected_column = (n_cols + selected_column + file_change) % n_cols;

	for (int i = 0; i < 3; ++i) {
	  selected_file = (3 + selected_file + file_change) % 3;
	  if (job_lines[selected_line].status_files[selected_file]) {
	    break;
	  }
	}
	
	file_change = 0;
      }

      // erase screen     
      printf("\r");
      for (int i = 0; i < displayed_lines; ++i) {
	printf("\e[2K\e[1A"); // erase entire line, move up
      }
      printf("\e[1B");
     
      displayed_lines = 0;

      if (show_help) {
	print_help();
      }
      
      draw_ui();
      update_ui = false;
    }
    
  }
  return 0;
}
