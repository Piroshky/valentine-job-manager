## Valentine Job Manager
An edit-it-yourself cron alternative that makes it easy to reschedule jobs and save their output.

Instead of using the terse, inscrutable language you don't know (cron DSL), use the terse, inscrutable language you do (C).

Runs as a daemon, comes with a command line based dashboard.

## Overview
The "jobs" it manages are really just C functions. The manager calls your job functions,
they do whatever you want, your function tells the manager when it wants to run again, then the manager goes to sleep until the next job is scheduled.

There are some functions provided to help with scheduling, executing commands, and saving command output and exit status. Since you're writing regular C rather than some crummy configuration file or domain specific language it's trivial to do things like rerun the job five minutes if the command fails, or sending desktop notifications.

This manager is aimed at running short, simple jobs. There is not job parallelization, so
if a job is scheduled to start while another job is running the former will not run until
the latter is finished. I aimed to make the manager as simple as possible so it would be
easy for others to modify to their needs. The manager has worked well for me, but of
course I can make no guarantees about its reliability.

## Dashboard
The dashboard program ```vjm-status``` is a convenient way to see the most recent exit
status of your jobs, what output they produced, and when they will run next.

## How it works
Each job function returns void and takes a pointer to a ```run_info``` struct as an argument. This struct
contains the time the job was scheduled to run, what time it is actually being ran, and a
field that the job should fill which tells the manager the next time it wants to run.

Each job has a directory in the path stored in ```vjm_path```
(default:~/.config/valentine-job-manager/). In the job directory there is a file called
'next-run', which the manager uses when it starts up to know when to run the job next. The
first line in this file is a Unix timestamp, and the second line is the timestamp in human
readable format. Only the first line is read by the manager. If the next-run file doesn't
exist on startup then the manager will schedule the job to run immediately.

Jobs that have a timestamp earlier than the current time will also run immediately, except
when the timestamp is 0. In that case the job will not run. The Unix Epoch is used as a
null value.

Jobs are ran one at a time according to the job queue, which is a linked list ordered by
timestamp.

When the manager is done running all ready jobs, and the job queue is not empty, it will
go to sleep until it's time to run the next job. If the job queue is empty, then the
manager will exit.

## Writing your own functions
The job function signature is: return void, and take a pointer to a ```run_info``` struct.
This struct contains: the job's name as a string, the time it was scheduled to run, the time that it is actually being called, and the time the manager should run the job next. This last field is what your function is expected to fill out.
There is a provied ```example_job()``` which I recommend using as a template.

For the manager to know about your job, it has to be placed in the jobs array (Ctrl+f jobs[]) using the JOB macro.

Your job does not need to run any command, it can be whatever code you want. But since you
probably do want to run commands, the functions ```exec_command()``` and
```write_command_status_file()``` are given to help you easily execute and record the output/exit status of the
commands. There are also the ```run_daily()```, ```run_weekly()```, and ```run_in()``` functions which help you create
the timestamp for when you want the job to run next. The next two sections describe these functions further.

### Scheduling helper functions: 'run' & '_run'
The 'run' functions will use the timestamps in the ```run_info``` struct to calculate the next
run time, and fill out the appropriate struct in ```run_info``` for you. They calculate the 
next run time relative to the ```start_time``` field in ```run_info```. For example if you call
```run_daily(12, 0, 0)``` at 11AM then it will set the job to run at 12PM that day. If you
called it at any time after 12PM then it would schedule it to 12PM the following day.
These functions will only change the ```next_run``` field if it's 0, or if it's later than the
timestamp it has calculated (in other words it will only change ```next_run``` to be sooner). This means it doesn't matter which order you call them in, i.e.:
```
run_daily(9, 0, 0);
run_daily(18, 0, 0);
```
is equivalent to:
```
run_daily(18, 0, 0);
run_daily(9, 0, 0);
```
Both of these will result in the job running daily at 9AM and 6PM.

The 'run' functions are really wrappers to the same named functions prefixed with an
underscore. The '_run' functions let you specify what time to base the timestamp
generation on. Lets say you want your job to run everyday at 1PM, and also wanted it to
work through jobs that didn't happen because the computer was off. Then you could call
```_run_daily(&info->scheduled_for, &info->next_run, 13, 0, 0);```

If this job ran on the 1st of the month, and then you left your computer off for a week
then turned it on the morniing of the 8th the manager would read the next-run file and see that the job should run on the 2nd at
1PM, that that time is earlier than the current time and run the job. The job would then
schedule itself to run on the 3rd at 1PM, the manager would see that this is earlier than
the current time, etc, and this would repeat until the job schedules itself to run on the
8th at 1PM, which is now in the future and the job would now be up to date.  

### Using the command helper functions
```exec_command()``` takes the run_info struct, a string array for the actual command arguments,
a flag argument for how you want the command output logged, and a pointer to a
command_status struct it will fill out with the command's exit information.

the logging flag works as follows:
- ```LOG_NOTHING``` will overpower any other flag, no output will be saved
- ```LOG_STDOUT```  save stdout in its own log file
- ```LOG_STDERR```  save std in its own log file
- ```LOG_STDOUT_WITH_STDERR``` will over power LOG_STDOUT and LOG_STDERR, putting both stdout and
stderr in the same log file

Log files are stored in subdirectory of the job's directory called status.

'write_command_status_file' writes out information about the job and the command it ran in
a human readable format, which is stored in a subdirectory of the job's directory called
status.

### Notification helper functions
There are a couple of wrapper functions to libnotify:
```no_expire_notification()``` and ```default_expire_notification()``` take a string and respectively will create a
notification that doesn't expire (you have to click close on the popup) or one that does.
The 'summary' will be the function name it is called from and the 'body' will be your
string.

There is also ```easy_notification``` which additionally allows you to specify the 'sumary' field, and set
the timeout time in milliseconds.

## Installation and Use

### With systemd
The way I use VJM is as a user service managed by systemd. This means VJM will start up
whenever I login. To do this you'll need to put the unit file
([valentine-job-manager.service](valentine-job-manager.service)) in an appropriate path, and enable it enable it.

The make file has a build target, 'install', which you can use to install VJM easily.
It will put the unit file in ~/.config/systemd/user/, and copy the executable to ~/bin/

If you want to install the executable somewhere else you'll need to change the make file
(or place it manually each time you update the program), and change the ```ExecStart``` line in
the unit file (you might be interested in the SPECIFIERS section of the systemd.unit manpage).

The provided unit file is very simple, to learn about what other options you can specify,
see the man pages for systemd.unit and systemd.service

I also recommend the [archwiki page on systemd user units](https://wiki.archlinux.org/title/systemd/User).

You can check to see that VJM is working by calling ```$ journalctl -f -n 20 --user-unit valentine-job-manager.service```
which will show you the 20 most recent log entries from VJM, and print new entries as they
are made.

### Without systemd
If you don't use systemd and want VJM to run as a daemon then you'll have to write the
init script or service file, or whatever your init system calls it, yourself. All the
logging in VJM is done with syslog, so there shouldn't be anything else you need to do to
bring it over from systemd.

