#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define SLEEP_LIT 60
#define SLEEP_BLANK 5

#define SCRIPT "browser-freezer-signal"
#define LOCK_FILE "/tmp/browser-freezer-wayland.lock"

// Alas, dues to the braindamaed Wayland architecture, there is no way to
// allow any client to freely poll the global hardware power state.
// Wayland’s strict security model completely shatters this.
// Wayland deliberately isolates clients, meaning a standard background
// C program cannot ask the compositor "is the screen blank?" without specific,
// privileged protocols.
// And because Wayland compositors (Mutter, KWin, wlroots) are heavily
// fragmented, there is no single standard way to communicate with them.
//
// To achieve the exact same minimal-overhead freezing functionality in
// Wayland, we must bypass the display server entirely and ask the system's
// session manager instead. The universal standard for this across modern
// distributions (like Debian/Ubuntu) is systemd-logind via D-Bus, but this
// means a fork :-(
//
// PS: Use X11 !

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

// Helper to query systemd-logind for the session's Idle/Blank state
int is_session_idle() {
    FILE *fp;
    char path[1035];
    int is_idle = 0;

    // Use busctl to query the IdleHint of the current user session
    // This works purely in the background on almost all Wayland compositors
    fp = popen("busctl --user get-property org.freedesktop.login1 /org/freedesktop/login1/session/auto org.freedesktop.login1.Session IdleHint 2>/dev/null", "r");
    
    if (fp == NULL) {
        fprintf(stderr, "Failed to run busctl\n" );
        return 0;
    }

    if (fgets(path, sizeof(path)-1, fp) != NULL) {
        // busctl returns "b true" if idle/blanked, "b false" if active
        if (strstr(path, "true") != NULL) {
            is_idle = 1;
        }
    }
    pclose(fp);
    return is_idle;
}

int main() {
    if (getenv("WAYLAND_DISPLAY") == NULL) {
        fprintf(stderr, "Error: No Wayland display, aborting.\n");
        return 1;
    }
    int lock_fd = ensure_single_instance();
    if (lock_fd < 0) {
        return 1; 
    }

    int was_blanked = 0;
    int sleep_time = SLEEP_LIT;

    // Init: Enforce initial state
    if (is_session_idle()) {
        signal_browser(SIGSTOP);
        was_blanked = 1;
    } else {
        signal_browser(SIGCONT);
        was_blanked = 0;
    }
    
    while (1) {
        int currently_idle = is_session_idle();

        if (!currently_idle) { // Screen is active 
            if (was_blanked) { // State changed, Wake up!
                signal_browser(SIGCONT); 
                was_blanked = 0;
                sleep_time = SLEEP_LIT;
            }
        } else { // Screen is blank / Idle
            if (!was_blanked) { // State changed, Freeze!
                signal_browser(SIGSTOP); 
                was_blanked = 1;
                sleep_time = SLEEP_BLANK;
            }
        }
        sleep(sleep_time);
    }

    return 0;
}
