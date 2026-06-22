#define _POSIX_C_SOURCE 199309L
#include "session_worker.h"
#include "session_priv.h"
#include "app.h"
#include "util/bus.h"
#include "logging.h"
#include "errors.h"
#include "util/user_event.h"
#include "input/input_gamepad.h"
#include "app_session.h"
#include "ss4s.h"
#include "sps_parser.h"

#ifdef TARGET_WEBOS
#include "lunasynccall.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>

static pid_t adb_server_pid = 0;
static pid_t adb_daemon_pid = 0;
static int tcp_sock = -1;

static void scrcpy_log(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    commons_log_info("Scrcpy", "%s", buf);
    fprintf(stderr, "[Scrcpy] %s\n", buf);
    fflush(stderr);

    FILE *f = fopen("/tmp/adb/scrcpy.log", "a");
    if (f) {
        fprintf(f, "%s\n", buf);
        fclose(f);
    }
}

typedef struct {
    session_t *session;
    int socket;
} audio_thread_data_t;

static int audio_reader_thread(void *ptr) {
    audio_thread_data_t *data = (audio_thread_data_t *)ptr;
    session_t *session = data->session;
    int sock = data->socket;
    scrcpy_log("Audio reader thread started.");

    unsigned char *recv_buf = malloc(16 * 1024); // 16KB read buffer
    if (!recv_buf) {
        scrcpy_log("Audio reader thread: Failed to allocate read buffer!");
        free(data);
        return -1;
    }

    #define AUDIO_FEED_SIZE 3840 // 960 stereo samples @ 48KHz = exactly 20.0ms, perfectly aligned to samplesPerFrame
    unsigned char *feed_buf = malloc(AUDIO_FEED_SIZE);
    if (!feed_buf) {
        scrcpy_log("Audio reader thread: Failed to allocate feed buffer!");
        free(recv_buf);
        free(data);
        return -1;
    }

    size_t feed_buf_len = 0;
    struct timespec next_feed_time;
    bool has_started_feed = false;

    while (!session->interrupted) {
        ssize_t bytes_read = recv(sock, recv_buf, 16 * 1024, 0);
        if (bytes_read <= 0) {
            scrcpy_log("Audio socket closed or error encountered (bytes_read=%zd)", bytes_read);
            break;
        }

        size_t offset = 0;
        while (offset < (size_t)bytes_read) {
            size_t to_copy = AUDIO_FEED_SIZE - feed_buf_len;
            if (to_copy > (size_t)bytes_read - offset) {
                to_copy = (size_t)bytes_read - offset;
            }

            memcpy(feed_buf + feed_buf_len, recv_buf + offset, to_copy);
            feed_buf_len += to_copy;
            offset += to_copy;

            if (feed_buf_len == AUDIO_FEED_SIZE) {
                struct timespec now;
                clock_gettime(CLOCK_MONOTONIC, &now);

                if (!has_started_feed) {
                    // Start absolute sync timeline anchored to the first received frame
                    next_feed_time = now;
                    has_started_feed = true;
                } else {
                    // Accumulate target feed times exactly 20ms apart (20,000,000 ns)
                    next_feed_time.tv_nsec += 20000000;
                    if (next_feed_time.tv_nsec >= 1000000000) {
                        next_feed_time.tv_sec += 1;
                        next_feed_time.tv_nsec -= 1000000000;
                    }

                    long long diff_ns = (next_feed_time.tv_sec - now.tv_sec) * 1000000000LL + 
                                       (next_feed_time.tv_nsec - now.tv_nsec);

                    if (diff_ns > 0) {
                        // Packet arrived early (burst). Pace it strictly by sleeping for the difference
                        struct timespec delay = {
                            .tv_sec = diff_ns / 1000000000LL,
                            .tv_nsec = diff_ns % 1000000000LL
                        };
                        nanosleep(&delay, NULL);
                    } else if (diff_ns < -100000000LL) {
                        // live streaming catchup protection (more than 100ms lag)
                        next_feed_time = now;
                    }
                }

                SS4S_AudioFeedResult res = SS4S_PlayerAudioFeed(session->player, feed_buf, AUDIO_FEED_SIZE);
                if (res != SS4S_AUDIO_FEED_OK) {
                    static int warn_count = 0;
                    if (warn_count < 5) {
                        scrcpy_log("Warning: SS4S_PlayerAudioFeed returned %d", res);
                        warn_count++;
                    }
                }
                feed_buf_len = 0;
            }
        }
    }

    scrcpy_log("Audio reader thread exiting...");
    free(feed_buf);
    free(recv_buf);
    free(data);
    return 0;
}

static int get_nalu_type(const unsigned char *nalu, size_t size) {
    size_t i = 0;
    while (i < size && nalu[i] == 0) {
        i++;
    }
    if (i < size && nalu[i] == 1) {
        i++;
        if (i < size) {
            return nalu[i] & 0x1F;
        }
    }
    return -1;
}

static int ss4s_nalu_cb(void *ctx, const unsigned char *nalu, size_t size) {
    session_t *session = ctx;
    if (session->interrupted) {
        return -1;
    }
    // Feed NALU to player
    SS4S_PlayerVideoFeed(session->player, nalu, size, 0);
    return 0;
}

static int nalu_read_socket(FILE *f, session_t *session) {
    int c, headIdx = 0;
    unsigned char *buf = malloc(2 * 1024 * 1024); // 2MB max NALU
    size_t bufSize = 0;
    int ret = 0;
    bool has_seen_sps = false;
    int current_width = 0;
    int current_height = 0;

    while (ret == 0 && !session->interrupted && (c = fgetc(f)) != EOF) {
        buf[bufSize++] = c;
        if (bufSize >= 2 * 1024 * 1024) {
            // Buffer overflow protection, just flush and reset
            bufSize = 0;
            headIdx = 0;
            continue;
        }

        switch (c) {
            case 0: {
                headIdx++;
                break;
            }
            case 1: {
                if (headIdx >= 2) {
                    int startCodeLen = headIdx + 1; // 3 or 4
                    if (bufSize > startCodeLen) {
                        size_t nalu_size = bufSize - startCodeLen;
                        int nalu_type = get_nalu_type(buf, nalu_size);
                        
                        if (!has_seen_sps) {
                            scrcpy_log("Parsed NALU before SPS: size=%zu, type=%d, startBytes=[%02x %02x %02x %02x %02x]", 
                                       nalu_size, nalu_type, 
                                       nalu_size > 0 ? buf[0] : 0, 
                                       nalu_size > 1 ? buf[1] : 0, 
                                       nalu_size > 2 ? buf[2] : 0, 
                                       nalu_size > 3 ? buf[3] : 0, 
                                       nalu_size > 4 ? buf[4] : 0);

                            if (nalu_type == 7) {
                                has_seen_sps = true;
                                scrcpy_log("SPS sequence header found. Starting feed to NDL decoder.");
                            }
                        } else {
                            static int post_sps_log_count = 0;
                            if (post_sps_log_count < 10) {
                                scrcpy_log("Feeding NALU after SPS (%d/10): size=%zu, type=%d", 
                                           post_sps_log_count + 1, nalu_size, nalu_type);
                                post_sps_log_count++;
                            }
                        }

                        if (nalu_type == 7) {
                            size_t sps_header_idx = 0;
                            while (sps_header_idx < nalu_size && buf[sps_header_idx] == 0) {
                                sps_header_idx++;
                            }
                            if (sps_header_idx < nalu_size && buf[sps_header_idx] == 1) {
                                sps_header_idx++;
                                if (sps_header_idx < nalu_size) {
                                    sps_dimension_t dimension;
                                    if (sps_parse_dimension_h264(&buf[sps_header_idx], &dimension)) {
                                        scrcpy_log("Parsed SPS: %dx%d", dimension.width, dimension.height);
                                        if (dimension.width != current_width || dimension.height != current_height) {
                                            current_width = dimension.width;
                                            current_height = dimension.height;
                                            scrcpy_log("Resolution changed to %dx%d. Updating player viewport/display.", current_width, current_height);
                                            SS4S_PlayerVideoSizeChanged(session->player, current_width, current_height);
                                        }
                                    } else {
                                        scrcpy_log("Failed to parse SPS dimensions");
                                    }
                                }
                            }
                        }

                        if (has_seen_sps) {
                            ret = ss4s_nalu_cb(session, buf, nalu_size);
                        }
                        
                        // Keep the start code for the next NALU
                        for (int i = 0; i < startCodeLen; i++) {
                            buf[i] = (i == startCodeLen - 1) ? 1 : 0;
                        }
                        bufSize = startCodeLen;
                    }
                }
                headIdx = 0;
                break;
            }
            default: {
                headIdx = 0;
                break;
            }
        }
    }

    if (ret == 0 && bufSize > 0 && has_seen_sps) {
        scrcpy_log("EOF reached. Feeding trailing buffer size=%zu", bufSize);
        ret = ss4s_nalu_cb(session, buf, bufSize);
    } else {
        scrcpy_log("Socket read loop exited. ret=%d, bufSize=%zu, has_seen_sps=%d", ret, bufSize, has_seen_sps);
    }

    free(buf);
    return ret;
}

int session_worker(session_t *session) {
    app_t *app = session->app;
    int audio_tcp_sock = -1;
    SDL_Thread *audio_thread = NULL;
    session_set_state(session, STREAMING_CONNECTING);
    bus_pushevent(USER_STREAM_CONNECTING, NULL, NULL);
    streaming_error(session, 0, "");
    session->player = NULL;

    // Initialize log file
    system("mkdir -p /tmp/adb");
    FILE *log_init = fopen("/tmp/adb/scrcpy.log", "w");
    if (log_init) {
        fprintf(log_init, "--- Scrcpy Session Started ---\n");
        fclose(log_init);
    }

    scrcpy_log("session_worker thread started");

#ifdef TARGET_WEBOS
    char adb[512];
    snprintf(adb, sizeof(adb), "/media/developer/apps/usr/palm/applications/%s/global-syspath/adb", APPID);
#else
    const char *adb = "adb";
#endif

    // Let's configure the writable HOME environment variable inside /home/root so ADB can generate keys
    char adb_home[512];
    snprintf(adb_home, sizeof(adb_home), "/home/root");
    
    // Ensure the directories exist
    system("mkdir -p /home/root/.android");
    
    // If the app has persistent storage configured and writable, check if we have existing keys saved
    char persistent_key[512] = {0};
    char persistent_pub[512] = {0};
    if (app->settings.conf_persistent && app->settings.conf_dir && strlen(app->settings.conf_dir) > 0) {
        snprintf(persistent_key, sizeof(persistent_key), "%s/adbkey", app->settings.conf_dir);
        snprintf(persistent_pub, sizeof(persistent_pub), "%s/adbkey.pub", app->settings.conf_dir);
        
        // If they exist in persistent storage, copy them into the active /home/root/.android directory
        FILE *tf_chk = fopen(persistent_key, "r");
        if (tf_chk) {
            fclose(tf_chk);
            scrcpy_log("Restoring persistent ADB keys of com.adb.webos into /home/root/.android/... ");
            char cp_cmd[512];
            snprintf(cp_cmd, sizeof(cp_cmd), "cp %s /home/root/.android/adbkey && cp %s /home/root/.android/adbkey.pub", persistent_key, persistent_pub);
            system(cp_cmd);
        }
    }

    setenv("HOME", adb_home, 1);

    // Let's specify robust global options for ADB: we enforce direct IPv4 loopback binding and port (-H 127.0.0.1 -P 5037)
    // to bypass slow, blocked, or broken localhost lookup resolutions inside unprivileged containers!
    char adb_opts[256];
    scrcpy_log("Explicit loopback server binding configuration (-H 127.0.0.1 -P 5037) enabled");
    snprintf(adb_opts, sizeof(adb_opts), "%s -H 127.0.0.1 -P 5037", adb);

    // Let's check socket existence on host loopback 5037 directly 
    int test_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in test_addr;
    memset(&test_addr, 0, sizeof(test_addr));
    test_addr.sin_family = AF_INET;
    test_addr.sin_port = htons(5037);
    inet_pton(AF_INET, "127.0.0.1", &test_addr.sin_addr);

    bool adb_server_running = false;
    if (test_sock >= 0) {
        if (connect(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr)) == 0) {
            adb_server_running = true;
        }
        close(test_sock);
    }

#ifdef TARGET_WEBOS
    // On webOS, we want to guarantee the ADB daemon is running with root physical USB privileges.
    // Rather than reusing a potentially unprivileged, stale, or broken user-space (5430) daemon,
    // we always enforce an elevated startup sequence.
    adb_server_running = false;
#endif

    if (!adb_server_running) {
        scrcpy_log("No active ADB server on port 5037 detected. Spawning daemon safely...");
        
        bool spawned_via_hb = false;
#ifdef TARGET_WEBOS
        scrcpy_log("Rooted TV context detected. Spawning elevated ADB daemon via Homebrew Channel, permitting raw USB access...");
        char payload[1024];

        // Execute elevated kill-server first to clear port mapping
        snprintf(payload, sizeof(payload), "{\"command\": \"env HOME=/home/root %s kill-server\"}", adb);
        HLunaServiceCallSync("luna://org.webosbrew.hbchannel.service/exec", payload, true, NULL);
        SDL_Delay(300);

        // Execute start-server as root using the homebrew channel service
        snprintf(payload, sizeof(payload), "{\"command\": \"env HOME=/home/root %s start-server\"}", adb);
        char *hb_out = NULL;
        bool hb_ret = HLunaServiceCallSync("luna://org.webosbrew.hbchannel.service/exec", payload, true, &hb_out);
        if (hb_ret) {
            scrcpy_log("Homebrew Channel service accepted start-server command.");
            if (hb_out) {
                scrcpy_log("Elevated startup response: %s", hb_out);
                if (strstr(hb_out, "error") || strstr(hb_out, "failed") || strstr(hb_out, "not found")) {
                    scrcpy_log("Warning: Elevated response indicates failure. Falling back to local sandbox execution.");
                } else {
                    spawned_via_hb = true;
                }
                free(hb_out);
            } else {
                spawned_via_hb = true;
            }
            SDL_Delay(600); // Give socket a moment to establish listener
        } else {
            scrcpy_log("Homebrew Channel service did not respond (TV might be unrooted).");
        }
#endif

        if (!spawned_via_hb) {
            scrcpy_log("Starting local sandboxed ADB server as fallback...");
            char kill_cmd[512];
            snprintf(kill_cmd, sizeof(kill_cmd), "HOME=/home/root %s kill-server > /dev/null 2>&1", adb_opts);
            system(kill_cmd);
            SDL_Delay(300);

            // We run adb start-server directly using the system() wrapper, specifying HOME.
            char start_cmd[512];
            snprintf(start_cmd, sizeof(start_cmd), "HOME=/home/root %s start-server > /tmp/adb/start_server.log 2>&1", adb);
            scrcpy_log("Executing: %s", start_cmd);
            int start_res = system(start_cmd);
            scrcpy_log("Server start command status result: %d", start_res);
            SDL_Delay(500); // Wait for socket to become active
        }
    } else {
        scrcpy_log("An existing ADB server is already running on port 5037. Reusing it.");
    }

    // Now, if new keys are generated, persistently back them up to Moonlight's writable directory
    if (app->settings.conf_persistent && app->settings.conf_dir && strlen(app->settings.conf_dir) > 0) {
        FILE *gen_chk = fopen("/home/root/.android/adbkey", "r");
        if (gen_chk) {
            fclose(gen_chk);
            FILE *p_chk = fopen(persistent_key, "r");
            if (!p_chk) { // Back up if they are not already stored persistently
                scrcpy_log("Saving newly generated authorized ADB keys to persistent directory: %s", app->settings.conf_dir);
                char backup_cmd[512];
                snprintf(backup_cmd, sizeof(backup_cmd), "cp /home/root/.android/adbkey %s/ && cp /home/root/.android/adbkey.pub %s/", app->settings.conf_dir, app->settings.conf_dir);
                system(backup_cmd);
            } else {
                fclose(p_chk);
            }
        }
    }

    char cmd[512];
    // Dynamic wait for ADB server to be up and responsive
    scrcpy_log("Waiting for ADB server daemon to become responsive...");
    int adb_wait_retries = 10;
    bool adb_ready = false;
    while (adb_wait_retries > 0 && !session->interrupted) {
        snprintf(cmd, sizeof(cmd), "%s devices > /tmp/adb_ping.txt 2>&1", adb_opts);
        system(cmd);
        
        // Open the ping file in all cases to parse output
        FILE *f_ping = fopen("/tmp/adb_ping.txt", "r");
        if (f_ping) {
            char ping_line[256];
            bool found_attached = false;
            while (fgets(ping_line, sizeof(ping_line), f_ping)) {
                // Check if the command output lists "List of devices attached", "daemon started", or "daemon running"
                if (strstr(ping_line, "List of devices") || 
                    strstr(ping_line, "daemon started") || 
                    strstr(ping_line, "daemon running")) {
                    found_attached = true;
                }
            }
            fclose(f_ping);
            if (found_attached) {
                adb_ready = true;
                break;
            }
        }
        SDL_Delay(1000);
        adb_wait_retries--;
    }

    if (!adb_ready && !session->interrupted) {
        scrcpy_log("ADB server did not report ready. Let's try to proceed anyway.");
    } else {
        scrcpy_log("ADB server is verified ready and responding.");
    }

    // Step 0: Robust check for connected USB (wired ADB) device
    scrcpy_log("Checking for connected USB (wired ADB) devices...");
    int usb_chk_retries = 15; // Wait up to 15 seconds for a device to be detected
    bool usb_connected = false;
    bool usb_authorized = false;
    
    while (usb_chk_retries > 0 && !session->interrupted) {
        snprintf(cmd, sizeof(cmd), "%s -d get-state > /tmp/adb_state.txt 2>&1", adb_opts);
        system(cmd);
        
        FILE *sf = fopen("/tmp/adb_state.txt", "r");
        if (sf) {
            char state_line[256] = {0};
            if (fgets(state_line, sizeof(state_line), sf)) {
                // Remove trailing whitespace, newlines, and carriage returns for robust comparison
                size_t len = strlen(state_line);
                while (len > 0 && (state_line[len-1] == '\n' || state_line[len-1] == '\r' || state_line[len-1] == ' ' || state_line[len-1] == '\t')) {
                    state_line[len-1] = '\0';
                    len--;
                }
                
                scrcpy_log("Wired device check state: %s", state_line);
                if (strcmp(state_line, "device") == 0) {
                    usb_connected = true;
                    usb_authorized = true;
                    fclose(sf);
                    break;
                } else if (strstr(state_line, "unauthorized")) {
                    usb_connected = true;
                    usb_authorized = false;
                    // Keep waiting in case the user authorizes it on their screen
                } else if (strstr(state_line, "offline")) {
                    usb_connected = true;
                    usb_authorized = false;
                } else {
                    usb_connected = false;
                    usb_authorized = false;
                }
            }
            fclose(sf);
        }
        
        SDL_Delay(1000);
        usb_chk_retries--;
    }
    
    if (session->interrupted) {
        goto thread_cleanup;
    }

    if (!usb_connected) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, 
                        "No USB device detected.\n\n"
                        "Please verify:\n"
                        "1. Connect Android to TV via USB\n"
                        "2. Enable Developer Options on android:\n"
                        "   Settings -> About -> tap 7x 'Build Number'\n"
                        "3. Developer Options -> [✓] 'USB Debugging'\n"
                        "4. Ensure your android screen is unlocked\n"
                        "5. Accept Android connection prompt\n\n"
                        "Good Luck. Find help on GitHub");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    if (!usb_authorized) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, 
                        "USB device connected but UNAUTHORIZED.\n\n"
                        "Please check your mobile phone's screen and accept the 'Allow USB Debugging' authorization prompt (always allow is recommended).");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }
    
    // Step 1: Push server
    scrcpy_log("Pushing scrcpy-server.jar...");
#ifdef TARGET_WEBOS
    char scrcpy_jar[512];
    snprintf(scrcpy_jar, sizeof(scrcpy_jar), "/media/developer/apps/usr/palm/applications/%s/scrcpy-server.jar", APPID);
#else
    const char *scrcpy_jar = "scrcpy-server.jar";
#endif
    snprintf(cmd, sizeof(cmd), "%s -d push %s /data/local/tmp/scrcpy-server.jar 2>&1 | tee /tmp/adb_push.log", adb_opts, scrcpy_jar);
    int push_ret = system(cmd);
    scrcpy_log("Push returned: %d", push_ret);
    if (push_ret != 0) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to push payload. Make sure USB Debugging is ON and your phone AUTHORIZED the TV. (See /tmp/adb_push.log)");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    // Step 2: Forward port
    scrcpy_log("Forwarding port...");
    snprintf(cmd, sizeof(cmd), "%s -d forward tcp:27183 localabstract:scrcpy 2>&1 | tee /tmp/adb_forward.log", adb_opts);
    int forward_ret = system(cmd);
    scrcpy_log("Forward returned: %d", forward_ret);
    if (forward_ret != 0) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to forward ADB port. Check /tmp/adb_forward.log via SSH.");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    // Step 3: Launch server in background
    scrcpy_log("Forking child to launch scrcpy server on device...");
    pid_t pid = fork();
    if (pid == 0) {
        // Determine safe scaling ceiling based on UI context. Defaulting safely to 1920 max edge for standard 1080p/4K TV decoders.
        const char *max_size_arg = "max_size=1920"; 
        if (app->ui.width > 1920 || app->ui.height > 1080) {
            max_size_arg = "max_size=3840"; // Bound up to 4K if panel environment layout explicitly supports it
        }

        scrcpy_log("Child process constraining max encoder target boundary to: %s", max_size_arg);

        // Child cannot log safely to scrcpy_log if it exits or calls execlp
        execlp(adb, adb, "-H", "127.0.0.1", "-P", "5037", "-d", "shell", 
               "CLASSPATH=/data/local/tmp/scrcpy-server.jar", "app_process", "/", 
               "com.genymobile.scrcpy.Server", "3.1", 
               "tunnel_forward=true", 
               "audio=true", 
               "audio_codec=raw", 
               "control=false", 
               "raw_stream=true", 
               "video_bit_rate=40000000", 
               "max_fps=60", 
               "i_frame_interval=2", 
               max_size_arg, 
               NULL);
        exit(1);
    } else if (pid > 0) {
        adb_server_pid = pid;
        scrcpy_log("Parent: scrcpy server process spawned with PID %d", pid);
    }

    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(27183);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    scrcpy_log("Connecting to 127.0.0.1:27183...");
    
    // Step 4: Wait for server to be ready before connecting
    int retries = 20; 
    SDL_Delay(1000);  

    while (retries > 0 && !session->interrupted) {
        scrcpy_log("Connection attempt %d/20...", 21 - retries);
        if (connect(tcp_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            scrcpy_log("Socket connected!");
            break;
        }
        SDL_Delay(500);
        retries--;
    }

    if (retries == 0 || session->interrupted) {
        scrcpy_log("Timed out waiting for socket connection");
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to connect to scrcpy TCP socket (timed out)");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    scrcpy_log("Connecting audio socket to 127.0.0.1:27183...");
    audio_tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (audio_tcp_sock < 0) {
        scrcpy_log("Failed to create audio socket!");
    } else {
        int audio_retries = 10;
        bool audio_connected = false;
        while (audio_retries > 0 && !session->interrupted) {
            if (connect(audio_tcp_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
                scrcpy_log("Audio socket connected successfully!");
                audio_connected = true;
                break;
            }
            SDL_Delay(200);
            audio_retries--;
        }
        if (!audio_connected) {
            scrcpy_log("Failed to connect audio socket within timeout!");
            close(audio_tcp_sock);
            audio_tcp_sock = -1;
        }
    }

    scrcpy_log("Opening SS4S Player...");
    session->player = SS4S_PlayerOpen();
    if (!session->player) {
        scrcpy_log("Failed to open SS4S Player!");
    }
    SS4S_PlayerSetWaitAudioVideoReady(session->player, false);
    SS4S_PlayerSetViewportSize(session->player, app->ui.width, app->ui.height);
    SS4S_PlayerSetUserdata(session->player, app);

    SS4S_VideoInfo info = {
        .codec = SS4S_VIDEO_H264,
        .width = app->ui.width > 0 ? app->ui.width : 1920,
        .height = app->ui.height > 0 ? app->ui.height : 1080,
        .frameRateNumerator = 60,
        .frameRateDenominator = 1
    };

    scrcpy_log("Opening SS4S video decoder...");
    if (SS4S_PlayerVideoOpen(session->player, &info) != SS4S_VIDEO_OPEN_OK) {
        scrcpy_log("Failed to open SS4S video decoder!");
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to open SS4S video decoder");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    if (audio_tcp_sock != -1) {
        SS4S_AudioInfo audio_info = {
            .codec = SS4S_AUDIO_PCM_S16LE,
            .sampleRate = 48000,
            .numOfChannels = 2,
            .samplesPerFrame = 960,
            .appName = "Moonlight",
            .streamName = "Streaming"
        };

        scrcpy_log("Opening SS4S audio decoder...");
        if (SS4S_PlayerAudioOpen(session->player, &audio_info) != SS4S_AUDIO_OPEN_OK) {
            scrcpy_log("Failed to open SS4S audio decoder!");
        } else {
            scrcpy_log("SS4S audio decoder opened successfully!");
            // Spawn the audio reader thread
            audio_thread_data_t *thread_data = malloc(sizeof(audio_thread_data_t));
            if (thread_data) {
                thread_data->session = session;
                thread_data->socket = audio_tcp_sock;
                audio_thread = SDL_CreateThread(audio_reader_thread, "scrcpy_audio", thread_data);
                if (audio_thread == NULL) {
                    scrcpy_log("Failed to spawn audio reader thread!");
                    free(thread_data);
                } else {
                    scrcpy_log("Audio reader thread spawned successfully.");
                }
            } else {
                scrcpy_log("Failed to allocate audio thread data!");
            }
        }
    }

    session_set_state(session, STREAMING_STREAMING);
    bus_pushevent(USER_STREAM_OPEN, NULL, NULL);

    scrcpy_log("Starting socket reader...");
    FILE *f = fdopen(tcp_sock, "rb");
    if (f) {
        nalu_read_socket(f, session);
        fclose(f);
        tcp_sock = -1;
    } else {
        scrcpy_log("fdopen failed on tcp_sock!");
    }

    session_set_state(session, STREAMING_DISCONNECTING);

thread_cleanup:
    scrcpy_log("Cleaning up thread...");
    if (tcp_sock != -1) {
        close(tcp_sock);
        tcp_sock = -1;
    }
    if (audio_tcp_sock != -1) {
        scrcpy_log("Shutting down audio socket to unblock reader...");
        shutdown(audio_tcp_sock, SHUT_RDWR);
    }
    if (audio_thread != NULL) {
        scrcpy_log("Waiting for audio thread to exit...");
        int thread_ret;
        SDL_WaitThread(audio_thread, &thread_ret);
        audio_thread = NULL;
    }
    if (audio_tcp_sock != -1) {
        close(audio_tcp_sock);
        audio_tcp_sock = -1;
    }
    if (adb_server_pid > 0) {
        scrcpy_log("Terminating ADB server child PID %d", adb_server_pid);
        kill(adb_server_pid, SIGTERM);
        adb_server_pid = 0;
        snprintf(cmd, sizeof(cmd), "%s -d forward --remove tcp:27183", adb);
        system(cmd);
    }
    if (adb_daemon_pid > 0) {
        scrcpy_log("Terminating ADB daemon child PID %d", adb_daemon_pid);
        kill(adb_daemon_pid, SIGTERM);
        adb_daemon_pid = 0;
    }
    if (session->player != NULL) {
        SS4S_PlayerAudioClose(session->player);
        SS4S_PlayerVideoClose(session->player);
        SS4S_PlayerClose(session->player);
        session->player = NULL;
    }

    bus_pushevent(USER_STREAM_FINISHED, NULL, NULL);
    app_bus_post(app, (bus_actionfunc) app_session_destroy, app);
    return 0;
}