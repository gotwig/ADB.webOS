#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/stream/session_worker.c"

cat << 'WORKER' > tmp_worker2.c
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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

static pid_t adb_server_pid = 0;
static int tcp_sock = -1;

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
                        ret = ss4s_nalu_cb(session, buf, bufSize - startCodeLen);
                        
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

    if (ret == 0 && bufSize > 0) {
        ret = ss4s_nalu_cb(session, buf, bufSize);
    }

    free(buf);
    return ret;
}

int session_worker(session_t *session) {
    app_t *app = session->app;
    session_set_state(session, STREAMING_CONNECTING);
    bus_pushevent(USER_STREAM_CONNECTING, NULL, NULL);
    streaming_error(session, 0, "");
    session->player = NULL;

    commons_log_info("Scrcpy", "Starting scrcpy adb commands...");

#ifdef TARGET_WEBOS
    const char *adb = "/media/developer/apps/usr/palm/applications/com.limelight.webos/adb";
    // We must ensure the adb server is running as root or correct user, and HOME is set so it can find/write keys
    setenv("HOME", "/media/developer/apps/usr/palm/applications/com.limelight.webos", 1);
#else
    const char *adb = "adb";
#endif

    char cmd[512];
    
    // Ensure ADB server is started
    snprintf(cmd, sizeof(cmd), "%s start-server", adb);
    system(cmd);
    
    // Step 1: Push server
    snprintf(cmd, sizeof(cmd), "%s -d push /media/developer/apps/usr/palm/applications/com.limelight.webos/scrcpy-server.jar /data/local/tmp/scrcpy-server.jar 2>&1 | tee /tmp/adb_push.log", adb);
    if (system(cmd) != 0) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to push scrcpy-server.jar. Check /tmp/adb_push.log via SSH.");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    // Step 2: Forward port
    snprintf(cmd, sizeof(cmd), "%s -d forward tcp:27183 localabstract:scrcpy 2>&1 | tee /tmp/adb_forward.log", adb);
    if (system(cmd) != 0) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to forward ADB port. Check /tmp/adb_forward.log via SSH.");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    // Step 3: Launch server in background
    pid_t pid = fork();
    if (pid == 0) {
        execlp(adb, adb, "-d", "shell", "CLASSPATH=/data/local/tmp/scrcpy-server.jar", "app_process", "/", "com.genymobile.scrcpy.Server", "3.1", "tunnel_forward=true", "audio=false", "control=false", "max_size=1280", "raw_stream=true", "video_bit_rate=3000000", "max_fps=30", "i_frame_interval=2", NULL);
        exit(1);
    } else if (pid > 0) {
        adb_server_pid = pid;
    }

    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(27183);
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

    commons_log_info("Scrcpy", "Connecting to 127.0.0.1:27183...");
    
    // Step 4: Wait for server to be ready before connecting
    int retries = 20; 
    SDL_Delay(1000);  

    while (retries > 0 && !session->interrupted) {
        if (connect(tcp_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == 0) {
            break;
        }
        SDL_Delay(500);
        retries--;
    }

    if (retries == 0 || session->interrupted) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to connect to scrcpy TCP socket (timed out)");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    session->player = SS4S_PlayerOpen();
    SS4S_PlayerSetWaitAudioVideoReady(session->player, false);
    SS4S_PlayerSetViewportSize(session->player, app->ui.width, app->ui.height);
    SS4S_PlayerSetUserdata(session->player, app);

    SS4S_VideoInfo info = {
        .codec = SS4S_VIDEO_H264,
        .width = 1280,
        .height = 720,
        .frameRateNumerator = 30,
        .frameRateDenominator = 1
    };

    if (SS4S_PlayerVideoOpen(session->player, &info) != SS4S_VIDEO_OPEN_OK) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to open SS4S video decoder");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    session_set_state(session, STREAMING_STREAMING);
    bus_pushevent(USER_STREAM_OPEN, NULL, NULL);

    FILE *f = fdopen(tcp_sock, "rb");
    if (f) {
        nalu_read_socket(f, session);
        fclose(f);
        tcp_sock = -1;
    }

    session_set_state(session, STREAMING_DISCONNECTING);

thread_cleanup:
    if (tcp_sock != -1) {
        close(tcp_sock);
        tcp_sock = -1;
    }
    if (adb_server_pid > 0) {
        kill(adb_server_pid, SIGTERM);
        adb_server_pid = 0;
        snprintf(cmd, sizeof(cmd), "%s -d forward --remove tcp:27183", adb);
        system(cmd);
    }
    if (session->player != NULL) {
        SS4S_PlayerVideoClose(session->player);
        SS4S_PlayerClose(session->player);
        session->player = NULL;
    }

    bus_pushevent(USER_STREAM_FINISHED, NULL, NULL);
    app_bus_post(app, (bus_actionfunc) app_session_destroy, app);
    return 0;
}
WORKER

mv tmp_worker2.c "$FILE"
