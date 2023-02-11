#define _GNU_SOURCE //so I can get the fucking errno enum name

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <libnotify/notify.h>

// This is where the log files will be stored
// 'USER' is whoever ran the Makefile
char *vjm_path = "/home/" USER "/.config/valentine-job-manager/";

struct run_info {
  const char *job_name;
  const struct timespec *scheduled_for;
  const struct timespec *start_time;
  struct timespec *next_run;
};

struct job {
  char *name;
  void (*func) (struct run_info *info);
  struct timespec next_run;
};

/* struct schedule_item; */
struct schedule_item {
  struct schedule_item *next;
  struct timespec       run_time;
  int    job_index;
};
const struct schedule_item SCHEDULE_ITEM_INIT = {NULL, {0, 0}, -1};

struct schedule_item *schedule_head = NULL;

struct command_status {
  bool execed;
  bool exited;
  bool signaled;
  bool core_dumped;
  bool stopped;
  int  exec_errno;
  int  exit_code;
  int  terminating_signal;
  int  stop_signal;
  struct timespec job_finished_at;
};
const struct command_status COMMAND_STATUS_INIT = {false, false, false, false, false, 0, 0, 0, 0};


char *create_status_file_path(const char *job_name, const struct timespec *job_start, char *suffix) {
  char time_stamp[2000];
  strftime(time_stamp, sizeof time_stamp, "%Y-%m-%d_%H:%M:%S.%%02ld_UTC%z_", localtime(&job_start->tv_sec));

  char *file_name;
  asprintf(&file_name, time_stamp, job_start->tv_nsec);
  
  char *file_path;
  asprintf(&file_path, "%s%s/status/%s%s", vjm_path, job_name, file_name, suffix);
  free(file_name);
  return file_path;
}

#define LOG_NOTHING            1
#define LOG_STDOUT             2
#define LOG_STDERR             4
#define LOG_STDOUT_WITH_STDERR 8   

int exec_command(struct run_info *info, char *command_list[], int log_options, struct command_status *status) {
  int p[2];
  int pipe_ret = pipe(p);
  if (pipe_ret != 0) {
    // the system has to be in a pretty pathological state to fail creating a pipe, not sure if it is worth fully handling
    syslog(LOG_CRIT, "Could not create pipe between parent and child for job %s. %s", info->job_name, strerror(errno));
  }
  
  if (fork() == 0) {
    // child
    close(p[0]);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    
    /* static char *a_envp[] = { \"PATH=/bin:/usr/bin:/sbin\", NULL }; */
    if (!(log_options & LOG_NOTHING)) {
      char *status_dir_path;
      asprintf(&status_dir_path, "%s%s/status/", vjm_path, info->job_name);      
      int ret = mkdir(status_dir_path, 0755);
      if (ret == -1 && errno != EEXIST) {
	syslog(LOG_ERR, "Could not make status directory for job `%s'. %s\n", info->job_name, strerror(errno));

	NotifyNotification *notif;
	notif = notify_notification_new(info->job_name, "Could not create status directory!", NULL);
	notify_notification_set_timeout(notif, NOTIFY_EXPIRES_NEVER);
	notify_notification_show(notif, NULL);
	
	return(-2);
      }

      if (log_options & LOG_STDOUT_WITH_STDERR) {
	char *file_path = create_status_file_path(info->job_name, info->start_time, "stdout_and_stderr.txt");
	int fd = open(file_path, O_CREAT | O_WRONLY, 0666);
	dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
	
      } else {
 	if (log_options & LOG_STDOUT) {	  
	  char *file_path = create_status_file_path(info->job_name, info->start_time, "stdout.txt");
	  int fd = open(file_path, O_CREAT | O_WRONLY, 0666);
	  dup2(fd, STDOUT_FILENO);
	  
	}
	if (log_options & LOG_STDERR) {
	  char *file_path = create_status_file_path(info->job_name, info->start_time, "stderr.txt");
	  int fd = open(file_path, O_CREAT | O_WRONLY, 0666);
	  dup2(fd, STDERR_FILENO);
	}
      }
    }
    
    if (execvp(command_list[0], command_list) == -1) {
      syslog(LOG_ALERT, "Job %s could not run command. exec() returned -1, errno: %s", info->job_name, strerror(errno));
      write(p[1], &errno, sizeof(errno));
      exit(errno);
    }
    
  } else {
    // parent
    close(p[1]);
    int e   = 0;
    int ret = read(p[0], &e, sizeof(int));
    if (ret != 0) {
      status->exec_errno = e;
    } else {
      status->execed = true;
    }
    /* status->execed = true; */
    int statval;
    wait(&statval);
    if (status) {
      if(WIFEXITED(statval)) {            
	status->exited    = true;
	status->exit_code = WEXITSTATUS(statval);
      } else if(WIFSIGNALED(statval)) {
	status->signaled           = true;
	status->terminating_signal = WTERMSIG(statval);
      }
      if(WCOREDUMP(statval)) {            
	status->core_dumped = true;
      }
      if(WIFSTOPPED(statval)) {
	status->stopped     = true;
	status->stop_signal = WSTOPSIG(statval);
      }
      timespec_get(&status->job_finished_at, TIME_UTC);
    }
  }
  return 0;
}

// 'status file write'
#define SFW_PADDING "25" //you cannot stringify macros
#define SFW(description, format_string, ...) fprintf(status_file, "%-"SFW_PADDING"s"format_string, description, ##__VA_ARGS__)

int write_command_status_file(struct run_info *info, char *job_command[], struct command_status *status) {
  char *status_dir;
  asprintf(&status_dir, "%s%s/status/", vjm_path, info->job_name);
  mkdir(status_dir, 0755);
  free(status_dir);
  
  char *file_path = create_status_file_path(info->job_name, info->start_time, "status.txt");  
  
  FILE *status_file = fopen(file_path, "w");
  if (status_file == NULL) {
    syslog(LOG_ERR, "Could not create status file %s, for job %s. errno: %s", file_path, info->job_name, strerror(errno));
    return 1;
  }
  free(file_path);
  
  SFW("Job Name:","%s\n", info->job_name);

  char utc[100];
  strftime(utc, sizeof utc, "(UTC%z)", localtime(&info->start_time->tv_sec)); 
  
  char nr[100];
  strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S", localtime(&info->scheduled_for->tv_sec));
  SFW("Job Scheduled For:", "%s.%09ld %s\n", nr, info->scheduled_for->tv_nsec, utc);

  strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S", localtime(&info->start_time->tv_sec));  
  SFW("Job Ran At:", "%s.%09ld %s\n", nr, info->start_time->tv_nsec, utc);   
  
  fprintf(status_file, "%-"SFW_PADDING"s", "Command:");
  char *jc;
  for(int i = 0; job_command[i]; ++i) {
    jc = job_command[i];
    fprintf(status_file, "%s ", jc);
  }
  fprintf(status_file, "\n");
  
  if(status->execed) {
    strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S", localtime(&status->job_finished_at.tv_sec));
    SFW( "Command Finished At:", "%s.%09ld %s\n", nr, status->job_finished_at.tv_nsec, utc);

    long seconds  = status->job_finished_at.tv_sec  - info->start_time->tv_sec;
    long nseconds = status->job_finished_at.tv_nsec - info->start_time->tv_nsec;
    SFW("Time Elapsed:", "%ld.%09ld seconds\n", seconds, nseconds);


    if (status->exited) {
      SFW("Exit Code:", "%d\n", status->exit_code);
    } else {
      SFW("Exit Code:", "Command exited abnormally");
    }
  } else {
    SFW("Command Did Not Exec:", "(errno: %d) %s\n", status->exec_errno, strerror(status->exec_errno));
  }

  strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S (UTC%z)", localtime(&info->next_run->tv_sec));  
  SFW("Next Run:", "%s\n", nr);
  
  fclose(status_file);
}

void _run_daily(const struct timespec *base_time, struct timespec *run_next, int hour, int minute, int second) {
  struct tm modified_tm_time = *localtime(&base_time->tv_sec);
  modified_tm_time.tm_hour = hour;
  modified_tm_time.tm_min  = minute;
  modified_tm_time.tm_sec  = second;

  time_t next = timelocal(&modified_tm_time);
  if (next <= base_time->tv_sec) {
    modified_tm_time.tm_mday += 1; // the call to timelocal will fix up the struct in the case that tm_mday > days in the month
    next = timelocal(&modified_tm_time);
  }
  
  if(run_next->tv_sec == 0 || next < run_next->tv_sec) {
    run_next->tv_sec = next;
  }
}

void run_daily(struct run_info *info, int hour, int minute, int second) {
  _run_daily(info->start_time, info->next_run, hour, minute, second);
}

void _run_in(const struct timespec *base_time, struct timespec *run_next, int hour, int minute, int second) {  
  struct tm modified_tm_time = *localtime(&base_time->tv_sec);
  modified_tm_time.tm_hour += hour;
  modified_tm_time.tm_min  += minute;
  modified_tm_time.tm_sec  += second;

  time_t next = run_next->tv_sec = timelocal(&modified_tm_time);

  if(run_next->tv_sec == 0 || next < run_next->tv_sec) {
    run_next->tv_sec = next;
  }  
}

void run_in(struct run_info *info, int hour, int minute, int second) {
  _run_in(info->start_time, info->next_run, hour, minute, second);
}

#define SUNDAY     1
#define MONDAY     2
#define TUESAY     4
#define WEDNESDAY  8
#define THURSDAY  16
#define FRIDAY    32
#define SATURDAY  64

void _run_weekly(const struct timespec *base_time, struct timespec *run_next, int days, int hour, int minute, int second) {
  if (days == 0) {
    return;
  }
  struct tm modified_tm_time = *localtime(&base_time->tv_sec);
  modified_tm_time.tm_hour = hour;
  modified_tm_time.tm_min  = minute;
  modified_tm_time.tm_sec  = second;
  struct timespec temp = {timelocal(&modified_tm_time), 0};
  time_t next = 0;
  
  const int wday = modified_tm_time.tm_wday;
  int day = 1 << wday;
  for(int i = 0; i < 8; ++i) {
    int day_flag = 1 << ((wday + i) % 7);
    if ((day_flag & days)) {
      if (i == 0 && temp.tv_sec <= base_time->tv_sec) {
	continue;
      } else {
	modified_tm_time.tm_mday += i;
	next = timelocal(&modified_tm_time);
	break;
      }
    }
  }
  if(run_next->tv_sec == 0 || next < run_next->tv_sec) {
    run_next->tv_sec = next;
  }  
}

void run_weekly(struct run_info *info, int days, int hour, int minute, int second) {
  _run_weekly(info->start_time, info->next_run, days, hour, minute, second);
}

#define no_expire_notification(msg) easy_notification(__FUNCTION__, msg, NOTIFY_EXPIRES_NEVER)
#define default_expire_notification(msg) easy_notification(__FUNCTION__, msg, NOTIFY_EXPIRES_DEFAULT)
void easy_notification(const char *summary, char *body, int timeout) {
  NotifyNotification *notif;
  notif = notify_notification_new(summary, body, NULL);
  notify_notification_set_timeout(notif, timeout); // This call is optional, and the last argument can also be set to the desired timeout in milliseconds
  notify_notification_show(notif, NULL);
}

void example_job(struct run_info *info) {
  char *command_and_args[] = {"echo", "example job!", NULL};
  struct command_status status = COMMAND_STATUS_INIT;
  exec_command(info, command_and_args, LOG_STDOUT_WITH_STDERR, &status);

  if (!status.execed) {
    char *msg;
    asprintf(&msg, "Command failed to exec! (errno %d) %s", status.exec_errno, strerror(status.exec_errno));
    no_expire_notification(msg);
    free(msg);
    run_in(info, 0, 5, 0);

  } else if (status.exited && status.exit_code != 0) {
    no_expire_notification("Command failed!");
    run_in(info, 0, 5, 0);

  } else {
    /* run_daily(info, 12, 0, 0); */
    run_in(info, 0, 0, 5);
    /* run_weekly(info, MONDAY | WEDNESDAY | FRIDAY,  15, 30, 0); */

  }
  write_command_status_file(info, command_and_args, &status);
}

#define JOB(job_function) { #job_function, job_function, {0, 0} }

struct job jobs[] = {
  /* JOB(example_job) */
};
const int num_jobs = sizeof(jobs) / sizeof (struct job);

bool timespec_less_than(struct timespec *left, struct timespec *right) {
  if (left->tv_sec == right->tv_sec) {
    return left->tv_nsec < right->tv_nsec;
  } else {
    return left->tv_sec < right->tv_sec;
  }  
}

bool timespec_less_than_or_equal_to(struct timespec *left, struct timespec *right) {
  if (left->tv_sec == right->tv_sec) {
    return left->tv_nsec <= right->tv_nsec;
  } else {
    return left->tv_sec < right->tv_sec;
  }
}

// Adds a job run to the job queue (a linked list pointed to by schedule_head).
// Return values
//   0: success
//   1: invalid job index
//   2: tried to schedule job for Unix epoch (which is used as a null value)

#define schedule_job(...) _schedule_job(__FILE__, __LINE__, __VA_ARGS__)
int _schedule_job(char *file, int line, int job_index, struct timespec *run_time) {
  if (job_index >= num_jobs || job_index < 0) {
    syslog(LOG_ERR, "Tried to schedule job with invalid job index (%d) in %s:%d", file, line);
    return 1;
  }
  if (run_time->tv_sec == 0) {
    syslog(LOG_ERR, "Tried to schedule job to run at Unix epoch in %s:%d", file, line);
    return 2;
  }
  
  struct schedule_item *new = malloc(sizeof(struct schedule_item));
  *new = SCHEDULE_ITEM_INIT;
  new->job_index =  job_index;
  new->run_time  = *run_time;

  if (schedule_head == NULL) {
    schedule_head = new;
    return 0;
  }

  struct schedule_item *c = schedule_head; //current
  struct schedule_item *p = NULL;          //previous

  while (c != NULL) {
    if (timespec_less_than_or_equal_to(&new->run_time, &c->run_time)) {
      new->next = c;
      if (p == NULL) {
	schedule_head = new;
	return 0;
      } else {
	p->next = new;
	return 0;
      }    
    }
    p = c;
    c = c->next;
  }

  p->next = new;  
  return 0;
}

void pop_schedule() {
  struct schedule_item *next = schedule_head->next;
  free(schedule_head);
  schedule_head = next;
}

void print_schedule() {
  if (schedule_head == NULL) {
    printf("Job schedule: none scheduled\n");
    return;
  }
  int n = 0;
  struct schedule_item *c = schedule_head;
  while (c != NULL) {
    n += 1;
    c = c->next;
  }  
  printf("Job schedule: %d scheduled\n", n);

  c = schedule_head;
  while (c != NULL) {
    char nr[100];
    strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S (UTC%z)", localtime(&c->run_time.tv_sec));
    
    printf("  %s %s\n", nr, jobs[c->job_index].name);
    c = c->next;
  }
}

// This runs when the program first starts.
// It populates the next_run field before the job functions have ran.
// If the job directory has a file called next-run in it, the first line from that file
// is read and uses that unix timestamp for next_run. If the file does not exist,
// schedule the job to run now.
void populate_next_run_from_job_directory() {
  int num_no_next_run_jobs = 0;
  struct timespec current_time;
  timespec_get(&current_time, TIME_UTC);
  
  for (int i = 0; i < num_jobs; ++i) {
    char *job_path;
    asprintf(&job_path, "%s%s/", vjm_path, jobs[i].name);
    mkdir(job_path, 0755);
    free(job_path);

    char *file_path;
    asprintf(&file_path, "%s%s/next-run", vjm_path, jobs[i].name);
               
    if (access(file_path, F_OK) != 0) {      
      syslog(LOG_INFO, "No next-run file found for job %s, scheduling to run now", jobs[i].name);
      jobs[i].next_run = current_time;
      schedule_job(i, &jobs[i].next_run);
      
      num_no_next_run_jobs += 1;
    } else {      
      FILE *next_run = fopen(file_path, "r");
      if (next_run != NULL) {
	fscanf(next_run, "%ld", &jobs[i].next_run.tv_sec);
	fclose(next_run);
	schedule_job(i, &jobs[i].next_run);
	
      } else {
	syslog(LOG_CRIT, "The next-run file for job: %s exists but cannot be read. The job is set to not run", jobs[i].name);
	jobs[i].next_run.tv_sec = 0;
	jobs[i].next_run.tv_nsec = 0;
      }
    }    
  }
  if (num_no_next_run_jobs == num_jobs) {
    syslog(LOG_NOTICE, "None of the %d jobs had a next-run file. This should be expected if this is the first time the job manager is running", num_jobs);
  }
  
}

// check each job and run it if the timespec is <= the current time
void process_job(int job_index) {
  struct job *j = &jobs[job_index];
  
  if (j->next_run.tv_sec == 0) {
    syslog(LOG_CRIT, "Job `%s' is being skipped as it is scheduled to run at the Unix epoch",j->name);
    pop_schedule();
    return;
  }
    
  struct timespec current_time;
  timespec_get(&current_time, TIME_UTC);
        
  if (timespec_less_than_or_equal_to(&j->next_run, &current_time)) {
      
    char nr[100];
    strftime(nr, sizeof nr, "%Y-%m-%d_%H:%M:%S", localtime(&j->next_run.tv_sec));

    syslog(LOG_INFO, "Job `%s' started", j->name);
    struct timespec next_run = {0, 0};
    struct run_info info = {j->name, &j->next_run, &current_time, &next_run};
    
    j->func(&info);
    syslog(LOG_INFO, "Job `%s' terminated", j->name);
    j->next_run = next_run;
            
    if (j->next_run.tv_sec == 0) {
      syslog(LOG_WARNING, "Job %s did not schedule itself to run again.",j->name);
      pop_schedule();
      return;
    } else {
      strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S (UTC%z)", localtime(&j->next_run.tv_sec));
      syslog(LOG_INFO, "Job `%s' scheduled to run at %s", j->name, nr);
    }

    schedule_job(job_index, &j->next_run);
      
    // save the next run time to file
    char *job_path;
    asprintf(&job_path, "%s%s/", vjm_path, j->name);
    int ret = mkdir(job_path, 0755);
    if (ret == -1 && errno != EEXIST) {
      syslog(LOG_CRIT, "Could not make job directory %s. %s", job_path, strerror(errno));
      exit(1);
    }
    
    free(job_path);
    char *file_path;
    asprintf(&file_path, "%s/%s/next-run", vjm_path, j->name);
         
    FILE *next_run_file = fopen(file_path, "w");
    free(file_path);
    if (next_run_file != NULL) {
      fprintf(next_run_file, "%ld\n", j->next_run.tv_sec);

      char nr[100];
      strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S (UTC%z)", localtime(&j->next_run.tv_sec));
      fprintf(next_run_file, "%s", nr);
        
      fclose(next_run_file);
    } else {
      syslog(LOG_CRIT, "Could not write next-run file for job %s. errno %s. If the job manager crashes this scheduled date will be lost", j->name, strerror(errno));
    }    
  }
  pop_schedule();
}

#define next_wake schedule_head->run_time
#define next_job  schedule_head->job_index
int main() {
  notify_init("Valentine Job Manager");

  // Append '/' to end of path in case it was forgotten
  int path_len = strlen(vjm_path);
  if (vjm_path[path_len-1] != '/') {    
    char *fixed_path = malloc((sizeof(char)) * (path_len + 2));
    strcpy(fixed_path, vjm_path);
    fixed_path[path_len] = '/';
    fixed_path[path_len+1] = '\0';
    vjm_path = fixed_path;    
  }

  int ret = mkdir(vjm_path, 0755);
  if (ret == -1 && errno != EEXIST) {
    syslog(LOG_CRIT, "Could not make base directory %s. %s", vjm_path, strerror(errno));
    exit(1);
  }
  
  struct timespec current_time;
  
  populate_next_run_from_job_directory();
  
  while(schedule_head) {
    if (timespec_less_than_or_equal_to(&next_wake, &current_time)) {
      process_job(next_job);
    }
    
    if (schedule_head == NULL) {
      syslog(LOG_NOTICE, "No jobs scheduled, exiting");
      exit(1);
    }
    
    // sleep until next job is ready
    timespec_get(&current_time, TIME_UTC);
    
    while (timespec_less_than(&current_time, &next_wake)) {      
      char nr[100];
      strftime(nr, sizeof nr, "%Y-%m-%d %H:%M:%S (UTC%z)", localtime(&next_wake.tv_sec));
      syslog(LOG_INFO, "Next job `%s' scheduled for %s", jobs[next_job].name, nr);
      /* print_schedule(); */
      
      int sleep_return = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &next_wake, NULL);
      switch (sleep_return) {
      case EINTR:
	// put interrupt handling here
	break;

      // as written these cases shouldn't occur, they're a starting point if you do start messing around with how clock_nanosleep is called 
      case EFAULT:
	syslog(LOG_CRIT, "clock_nanosleep() returned EFAULT for job `%s'. Invalid address for `request' or `remain'", jobs[next_job].name);
	exit(1);
	break;
      case EINVAL:
        char *reason = NULL;
	if (next_wake.tv_nsec < 0 || next_wake.tv_nsec > 999999999) {
	  asprintf(&reason, "next_wake.tv_nsec (%d) out of range [0, 999999999]", next_wake.tv_nsec);
	} else if (next_wake.tv_sec < 0) {
	  asprintf(&reason, "next_wake.tv_sec (%d) was negative", next_wake.tv_sec);	  
	} else {
	  asprintf(&reason, "the argument `clockid' was invalid");
	}
	syslog(LOG_CRIT, "clock_nanosleep() returned EINVAL for job `%s' because %s", jobs[next_job].name, reason);
	exit(1);
	break;
      case ENOTSUP:
	syslog(LOG_CRIT, "clock_nanosleep() returned ENOTSUP for job `%s' (kernel does not support sleeping against this clockid)", jobs[next_job].name);
	exit(1);
	break;
      }
      
      timespec_get(&current_time, TIME_UTC);
    }
    utimensat(0, vjm_path, NULL, 0);
  }
  if (schedule_head == NULL) {
    syslog(LOG_NOTICE, "No jobs scheduled, exiting");
    exit(1);
  }
  return 0;
}
