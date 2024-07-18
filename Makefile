
USER=$(shell whoami)

DEFINES=-DUSER=\"$(USER)\"

all: main.c
	gcc `pkg-config --cflags --libs libnotify` $(DEFINES) main.c -o valentine-job-manager

vjm-status: status.c
	gcc $(DEFINES) status.c config.c -o vjm-status

# install directive for systemd
install: all vjm-status
	systemctl --user stop valentine-job-manager

	cp ./valentine-job-manager          ~/bin/valentine-job-manager #if you change this, remember to change the ExecStart field in the service file as well
	cp ./vjm-status                     ~/bin/vjm-status
	cp ./valentine-job-manager.service  ~/.config/systemd/user/valentine-job-manager.service

	systemctl --user enable valentine-job-manager
	systemctl --user start  valentine-job-manager
