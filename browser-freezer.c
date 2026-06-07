#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

#define SLEEP_LIT 60            // delay between checks when screen active
#define SLEEP_BLANK 5           // react more quickly on wakeup

#define SCRIPT "browser-freezer-signal"
#define LOCK_FILE "/tmp/browser-freezer.lock"

// Function to check and acquire an exclusive lock on a file descriptor
int ensure_single_instance(void) {
    // Open the lock file (auto-create, read/write permissions)
    int fd = open(LOCK_FILE, O_RDWR | O_CREAT, 0600);
    if (fd < 0) {
        fprintf(stderr, "Error: Could not open lock file %s\n", LOCK_FILE);
        exit(1);
    }

    // Set up the lock structure
    struct flock fl;
    memset(&fl, 0, sizeof(fl));
    fl.l_type = F_WRLCK;    // Request an exclusive Write Lock
    fl.l_whence = SEEK_SET; // Start from the beginning of the file
    fl.l_start = 0;
    fl.l_len = 0;           // Lock the entire file length

    // Attempt to lock the file descriptor natively via a kernel syscall
    if (fcntl(fd, F_SETLK, &fl) < 0) {
        if (errno == EACCES || errno == EAGAIN) {
            fprintf(stderr, "Error: browser_freezer is already running.\n");
            close(fd);
            return -1; // Lock failed, process should exit
        }
        fprintf(stderr, "Unexpected lock error encountered.\n");
        exit(1);
    }

    // Keep the file descriptor open for the duration of the process lifecycle.
    // The Linux kernel automatically releases this lock on process termination.
    return fd;
}

// We use external scripts to send the actual signal, as we need flexibility
// but not performance for this
void signal_browser(int sig) {
    char command[256];
    snprintf(command, sizeof(command), "%s %d", SCRIPT, sig);
    int result = system(command);
    if (result == -1) {
        perror("system() failed");
    }
}

// DPMS Extension Power Levels
//     0     DPMSModeOn               In use
//     1     DPMSModeStandby          Blanked, low power
//     2     DPMSModeSuspend          Blanked, lower power
//     3     DPMSModeOff              Shut off, awaiting activity

int main() {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "Error: Cannot open X display, aborting.\n");
        return 1;
    }

    int dummy;
    if (!DPMSQueryExtension(dpy, &dummy, &dummy)) {
        fprintf(stderr, "DPMS Extension not supported by X server\n");
        XCloseDisplay(dpy);
        return 1;
    }

    // Enforce single instance execution pattern before touching X11
    int lock_fd = ensure_single_instance();
    if (lock_fd < 0) {
        return 1; // Exit immediately, another daemon process is running
    }

    int was_blanked = 0;
    int sleep_time = SLEEP_LIT;
    CARD16 power_level;
    BOOL state;

    DPMSInfo(dpy, &power_level, &state);
    // Just emit a warning, but keep waiting for extension to be enabled
    if (!state) {
        fprintf(stderr, "Warning: DPMS Extension not enabled, browser-freezer inactive.\n");
    }
    // Init: Enforce initial state
    if (!state || power_level == DPMSModeOn) { // active, force unfreeze
        signal_browser(SIGCONT);
        was_blanked = 0;
    } else { // Screen is blank, force freeze
        signal_browser(SIGSTOP); // Native Kernel Syscall
        was_blanked = 1;
    }
    
    while (1) {
        // Query the hardware state directly from Xlib shared memory
        DPMSInfo(dpy, &power_level, &state);

        if (!state || power_level == DPMSModeOn) { // Screen is active 
            if (was_blanked) {               // State changed, Wake up!
                signal_browser(SIGCONT); // Native Kernel Syscall
                was_blanked = 0;
                sleep_time = SLEEP_LIT;
            }
        } else { // Screen is blank
            if (!was_blanked) {               // State changed, Freeze!
                signal_browser(SIGSTOP); // Native Kernel Syscall
                was_blanked = 1;
                sleep_time = SLEEP_BLANK;
            }
        }
        sleep(sleep_time);
    }

    XCloseDisplay(dpy);
    return 0;
}
