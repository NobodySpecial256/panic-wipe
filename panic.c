#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/reboot.h>
#include <unistd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <stdbool.h>
#include <string.h>

/* Decide whether we want to lock the screen before or after elevating to root */
const bool LOCK_BEFORE_ELEVATE = true; // `false` locks only after gaining root. `true` locks before gaining root

/*
	construct_cmd: constructs a shell command by appending `v` to `cmd_base`

	Warning: This function does not escape special characters, therefore it is not safe to run on untrusted inputs.
*/
char *construct_cmd(const char *cmd_base, const char *v){
	const size_t sv = strlen(v);
	const size_t scmd_base = strlen(cmd_base);
	const size_t scmd = scmd_base + sv + 2;

	char *cmd = calloc(scmd, sizeof(char));
	sprintf(cmd, "%s %s", cmd_base, v);

	return cmd;
}
char *construct_blkdiscard_secure(const char *v){
	return construct_cmd("blkdiscard -sfv", v);
}
char *construct_blkdiscard_insecure(const char *v){
	return construct_cmd("blkdiscard -fv", v);
}
char *construct_erase_luks(const char *v){
	/* `cryptsetup`'s `-q` suppresses confirmation questions, which is what we want for our non-interactive panic script */
	return construct_cmd("cryptsetup erase -q", v);
}
char *construct_erase_opal(const char *v){
	/* `cryptsetup`'s `-q` suppresses confirmation questions, which is what we want for our non-interactive panic script */
	return construct_cmd("cryptsetup erase --hw-opal-factory-reset", v);
}
void erase_luks(const char *v, mode_t mode){
	char *cmd = construct_erase_luks(v);
	system(cmd);
	free(cmd);
}
void erase_opal(const char *v, mode_t mode){
	char *cmd = construct_erase_luks(v);
	system(cmd);
	free(cmd);
}
void blkdiscard_secure(const char *v, mode_t mode){
	char *cmd = construct_blkdiscard_secure(v);
	system(cmd);
	free(cmd);
}
void blkdiscard_insecure(const char *v, mode_t mode){
	char *cmd = construct_blkdiscard_insecure(v);
	system(cmd);
	free(cmd);
}

int write_message_to_block_device(const char *msg, const char *v, mode_t mode){
	/* Only work with block devices */
	if(!S_ISBLK(mode)) return 1;

	FILE *fp = fopen(v, "w");
	if(fp == NULL)
		return 2;

	printf("write_erase_notice: %s\n", v);
	
	fwrite(msg, strlen(msg) + 1, sizeof(char), fp);
	fclose(fp);

	return 0;
}
void write_erase_notice(const char *v, mode_t mode){
	write_message_to_block_device("Panic handler: Device wiped [ data rendered permananetly irrecoverable through secure erasure ]\n", v, mode);
}

void wipe_disk(const char *v, mode_t mode){
	
	/* Secure discard is optimal for data erasure, since the spec mandates that it must zero all copies as well */
	blkdiscard_secure(v, mode);
	
	/* Many drives don't support secure discard. Normal discard isn't ideal, but it's sometimes the best we have available */
	blkdiscard_insecure(v, mode);
	
	/* Inform any potential attacker that there's nothing to gain from forced key disclosure (eg. legal threats or torture) */
	write_erase_notice(v, mode);
}

int foreach(const char *dir, void(*f)(const char*, mode_t), const int recurse){
	struct dirent *dp;
	DIR *dfd;
	const size_t sd = strlen(dir);

	if((dfd = opendir(dir)) == NULL)
		return -1;

	/* Loop through all files in `dir` */
	while((dp = readdir(dfd)) != NULL){
		struct stat stbuf;
		const size_t sf = strlen(dp->d_name);
		char *filename_qfd = calloc(sd + sf + 2, sizeof(char));
		sprintf(filename_qfd, "%s/%s", dir, dp->d_name);

		if(stat(filename_qfd, &stbuf) == -1)
			return -2;

		if((stbuf.st_mode & S_IFMT) == S_IFDIR){
			if(recurse){
				if(strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")){ // Don't recurse into `.` and `..`
					if(recurse != -1)
						foreach(filename_qfd, f, recurse - 1); // Recurse until `recurse == 0`
					else
						foreach(filename_qfd, f, -1); // Recurse infinitely
				}
			} else
				continue;
		} else
			/* Run whatever function was passed as `f` */
			f(filename_qfd, stbuf.st_mode);

		free(filename_qfd);
	}
	return 0;
}

/*
	Multiple shutdown types allow for customizing how the system resists cold boot attacks while allowing the drive time to TRIM discarded blocks.
	Different shutdown types have different security properties, and are therefore useful in different scenarios.
	
	Resisting cold boot attacks is an important security property, because a cold boot attack could potentially reveal the encryption key to an attacker after the LUKS
	headers have been wiped, thereby allowing an attacker the ability to read any data which is not immediately TRIMed, as well as anything that was in RAM at the time of
	shutdown.
*/
enum SHUTDOWN_TYPE {
	SHUTDOWN_TYPE_POWEROFF,
	SHUTDOWN_TYPE_DELAY_POWEROFF,
	SHUTDOWN_TYPE_REBOOT,
	SHUTDOWN_TYPE_NONE
};

enum ERASE_MODE {
	ERASE_MODE_DISCARD,
	ERASE_MODE_OPAL,
	ERASE_MODE_NONE
};

char ** construct_args(char *cmd, int argc, char **argv){
	char **n_argv = calloc(argc + 2, sizeof(char*));
	n_argv[0] = cmd;
	for(int a=0; a<argc; ++a)
		n_argv[a+1] = argv[a];
	n_argv[argc+1] = NULL;
	return n_argv;
}

/* Lock the screen to prevent interruption */
void lock_screen(){
	/* Qubes uses XFCE as dom0's default desktop environment */
	system("xflock4");
	
	// We can add other lock commands if we want to...
}

int main(int argc, char **argv){	
	/*
		If we're not root, use `sudo` to become root automatically.
		Since `sudo` is passwordless on Qubes, we can lock the screen before elevating to root.
		
		Note: On systems which require a password to elevate to root, this script cannot be bound to a keybind. This can make it harder to trigger a panic in situations
		where only a couple seconds are available to initiate data destruction.
	*/
	if(geteuid() != 0){
		if(LOCK_BEFORE_ELEVATE) lock_screen();

		char **n_argv = construct_args("sudo", argc, argv);
		int ret = execvp("sudo", n_argv);
		free(n_argv);
		return ret;
	}
	
	lock_screen();
	
	enum SHUTDOWN_TYPE shutdown_type = SHUTDOWN_TYPE_POWEROFF;
	unsigned int shutdown_delay = 0;
	
	enum ERASE_MODE erase_mode = ERASE_MODE_DISCARD;
	
	bool do_trim = true;
	bool do_notify = true;
	
	// Parse arguments
	for(int a=1; a<argc; ++a){
		if(!strcmp(argv[a], "--delay")){
			/* Delayed shutdown
				This gives the drive more time to TRIM discarded blocks, but creates a wider window of opportunity for an attacker to perform a cold boot attack
				against the system before it powers off.
				*/
			shutdown_type = SHUTDOWN_TYPE_DELAY_POWEROFF;
			shutdown_delay = atoi(argv[++a]);
		} else if(!strcmp(argv[a], "--immediate")){
			/* Immediate poweroff
				This gets the system into a powered-off state quicker, but gives the drive less time to TRIM discarded blocks. This can be useful if the attacker
				prepared for a delayed poweroff and you're worried about cold boot attacks during the time between when the panic handler is triggered and when
				the system loses power.
				*/
			shutdown_type = SHUTDOWN_TYPE_POWEROFF;
		} else if(!strcmp(argv[a], "--reboot")){
			/* Reboot
				This has the benefits of an immediate poweroff, but relies on the BIOS to zero RAM when the system gains power. If the BIOS does not zero RAM, a
				cold boot attack can potentially be performed after the system is rebooted.
				*/
			shutdown_type = SHUTDOWN_TYPE_REBOOT;
		} else if(!strcmp(argv[a], "--erase=OPAL")){
			erase_mode = ERASE_MODE_OPAL;
		} else if(!strcmp(argv[a], "--erase=DISCARD")){
			erase_mode = ERASE_MODE_DISCARD;
		} else if(!strcmp(argv[a], "--dbg=dry-run")){
			/* Don't destroy any data, and don't poweroff when we're done. This should only be used for debugging purposes.
				*/
			shutdown_type = SHUTDOWN_TYPE_NONE;
			erase_mode = ERASE_MODE_NONE;
		} else if(!strcmp(argv[a], "--dbg=no-erase")){
			/* Don't destroy any data. This should only be used for debugging purposes.
				*/
			erase_mode = ERASE_MODE_NONE;
		} else if(!strcmp(argv[a], "--dbg=keep-alive")){
			/* Don't shutdown the system when we're done. This should only be used for debugging purposes.
				*/
			shutdown_type = SHUTDOWN_TYPE_NONE;
		} else if(!strcmp(argv[a], "--dbg=skip-trim")){
			/* Skip the TRIM step. This should only be used for debugging purposes, or on systems where sensitive data is kept exclusively on HDDs.
				*/
			do_trim = false;
		} else if(!strcmp(argv[a], "--dbg=skip-notify")){
			/* Skip writing the wipe notification to disk. This should only be used for debugging purposes.
				*/
			do_notify = false;
		} else{
			printf("Unsupported argument: %s\n", argv[a]);
			return 1;
		}
	}

	switch(erase_mode){
	case ERASE_MODE_OPAL:
		foreach("/dev", &erase_opal, 1);
		// Intentionally fall through into `ERASE_MODE_DISCARD`
	case ERASE_MODE_DISCARD:
		/*  A recursion depth of 1 should include `/dev/*` and `/dev/mapper/*` */
		/* Begin by erasing LUKS headers (`cryptsetup erase`). We do this in a separate loop so we can `sync()` all disks at once */
		foreach("/dev", &erase_luks, 1);
		sync();

		if(do_trim){
			/* Secure discard is optimal for data erasure, since the spec mandates that it must zero all copies as well */
			foreach("/dev", &blkdiscard_secure, 1);
			
			/* Many drives don't support secure discard. Normal discard isn't ideal, but it's sometimes the best we have available */
			foreach("/dev", &blkdiscard_insecure, 1);
		}
		
		if(do_notify){
			/* Inform any potential attacker that there's nothing to gain from forced key disclosure (eg. legal threats or torture) */
			foreach("/dev", &write_erase_notice, 1);
		}
		sync();

		break;
	case ERASE_MODE_NONE:
		printf("[debug] ERASE_MODE_NONE: Refusing to perform destructive actions.\n");
		sync();
		break;
	default:
		printf("Erase mode invalid: %i\n", erase_mode);
		break;
	}

	switch(shutdown_type){
	case SHUTDOWN_TYPE_POWEROFF:
		/* Poweroff the system immediately */
		reboot(RB_POWER_OFF);
		break;
	case SHUTDOWN_TYPE_DELAY_POWEROFF:
		printf("Waiting %i seconds before poweroff...\n", shutdown_delay);
		while(shutdown_delay)
			shutdown_delay = sleep(shutdown_delay);
		reboot(RB_POWER_OFF);
		break;
	case SHUTDOWN_TYPE_REBOOT:
		reboot(RB_AUTOBOOT);
		break;
	case SHUTDOWN_TYPE_NONE:
		break;
	default:
		printf("Shutdown type invalid: %i\n", shutdown_type);
		break;
	}

	return 0;
}
