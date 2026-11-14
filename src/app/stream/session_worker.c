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
#include "adb_devices.h"
#include "ss4s.h"
#include "sps_parser.h"
#include <microdns/microdns.h>

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

static bool wifi_ip_from_usb(const char *adb_opts, char *ip, size_t ip_size) {
    char command[512], line[256], candidate[INET_ADDRSTRLEN];
    struct in_addr parsed;
    snprintf(command, sizeof(command), "%s -d shell ip route", adb_opts);
    FILE *output = popen(command, "r");
    if (output) {
        while (fgets(line, sizeof(line), output)) {
            const char *source = strstr(line, "src ");
            if (source && strstr(line, "wlan") &&
                sscanf(source + 4, "%15[0-9.]", candidate) == 1 &&
                inet_pton(AF_INET, candidate, &parsed) == 1) {
                snprintf(ip, ip_size, "%s", candidate);
                pclose(output);
                return true;
            }
        }
        pclose(output);
    }
    snprintf(command, sizeof(command), "%s -d shell getprop dhcp.wlan0.ipaddress", adb_opts);
    output = popen(command, "r");
    if (output) {
        bool found = fgets(line, sizeof(line), output) &&
                     sscanf(line, "%15[0-9.]", candidate) == 1 &&
                     inet_pton(AF_INET, candidate, &parsed) == 1;
        pclose(output);
        if (found) {
            snprintf(ip, ip_size, "%s", candidate);
            return true;
        }
    }
    return false;
}

static bool is_ipv4_endpoint(const char *endpoint) {
    char address[INET_ADDRSTRLEN];
    struct in_addr parsed;
    const char *colon = strchr(endpoint, ':');
    if (!colon || strchr(colon + 1, ':') || (size_t)(colon - endpoint) >= sizeof(address)) return false;
    memcpy(address, endpoint, (size_t)(colon - endpoint));
    address[colon - endpoint] = '\0';
    char *end;
    unsigned long port = strtoul(colon + 1, &end, 10);
    return inet_pton(AF_INET, address, &parsed) == 1 &&
           colon[1] && *end == '\0' && port > 0 && port <= 65535;
}

typedef struct {
    session_t *session;
    struct in_addr pairing_ip;
    Uint32 started;
    char endpoint[64];
} adb_qr_discovery_t;

static bool adb_qr_discovery_stop(void *data) {
    adb_qr_discovery_t *discovery = data;
    return discovery->endpoint[0] || discovery->session->interrupted ||
           SDL_GetTicks() - discovery->started >= 30000;
}

static void adb_qr_discovery_received(void *data, int status, const struct rr_entry *entries) {
    adb_qr_discovery_t *discovery = data;
    if (status < 0) return;

    for (const struct rr_entry *entry = entries; entry; entry = entry->next) {
        if (entry->type == RR_SRV && entry->ttl && entry->data.SRV.port &&
            strstr(entry->name, "._adb-tls-connect._tcp.local")) {
            for (const struct rr_entry *address = entries; address; address = address->next) {
                if (address->type != RR_A ||
                    strcmp(address->name, entry->data.SRV.target) ||
                    address->data.A.addr.s_addr != discovery->pairing_ip.s_addr) continue;
                snprintf(discovery->endpoint, sizeof(discovery->endpoint), "%s:%u",
                         address->data.A.addr_str, entry->data.SRV.port);
                return;
            }
        }
    }
}

static bool adb_qr_discover_connect_endpoint(session_t *session, const char *pair_endpoint,
                                             char *endpoint, size_t endpoint_size) {
    char ip[INET_ADDRSTRLEN], extra;
    unsigned port;
    struct sockaddr_in pairing_address = {.sin_family = AF_INET};
    if (sscanf(pair_endpoint, "%15[0-9.]:%u%c", ip, &port, &extra) != 2 ||
        !port || port > 65535 ||
        inet_pton(AF_INET, ip, &pairing_address.sin_addr) != 1) return false;

    adb_qr_discovery_t discovery = {
        .session = session,
        .pairing_ip = pairing_address.sin_addr,
        .started = SDL_GetTicks(),
    };
    struct mdns_ctx *ctx = NULL;
    const char *services[] = {"_adb-tls-connect._tcp.local"};
    int result = mdns_init(&ctx, NULL, MDNS_PORT);
    if (result < 0) return false;
    result = mdns_listen(ctx, services, 1, RR_PTR, 2, adb_qr_discovery_stop,
                         adb_qr_discovery_received, &discovery);
    mdns_destroy(ctx);
    if (result < 0 || !discovery.endpoint[0] || session->interrupted) return false;
    snprintf(endpoint, endpoint_size, "%s", discovery.endpoint);
    return true;
}

static bool adb_connect_endpoint(const char *adb_opts, const char *endpoint) {
    if (!is_ipv4_endpoint(endpoint)) return false;
    char command[512], line[256];
    snprintf(command, sizeof(command), "%s connect %s > /tmp/adb_connect.txt 2>&1", adb_opts, endpoint);
    int result = system(command);
    bool connected = false;
    FILE *log = fopen("/tmp/adb_connect.txt", "r");
    if (log) {
        while (fgets(line, sizeof(line), log)) {
            if (strstr(line, "connected to") || strstr(line, "already connected")) connected = true;
            scrcpy_log("ADB connect: %s", line);
        }
        fclose(log);
    }
    return result == 0 && connected;
}

static bool adb_value(const char *adb_opts, const char *transport_opts, const char *query,
                      char *value, size_t size) {
    char command[768];
    snprintf(command, sizeof(command), "%s %s %s 2>/dev/null", adb_opts, transport_opts, query);
    FILE *output = popen(command, "r");
    if (!output) return false;
    bool read = fgets(value, size, output) != NULL;
    int result = pclose(output);
    if (!read || result != 0) { value[0] = 0; return false; }
    value[strcspn(value, "\r\n")] = 0;
    for (char *ch = value; *ch; ++ch) if ((unsigned char) *ch < 32) *ch = ' ';
    return value[0] && strcmp(value, "null") && strcmp(value, "unknown");
}

static bool usb_value(const char *adb_opts, const char *query, char *value, size_t size) {
    return adb_value(adb_opts, "-d", query, value, size);
}

static void remember_usb_phone(session_t *session, const char *adb_opts) {
    app_t *app = session->app;
    adb_device_t device = {0};
    char ip[INET_ADDRSTRLEN];
    if (!usb_value(adb_opts, "get-serialno", device.serial, sizeof(device.serial))) return;
    if (!usb_value(adb_opts, "shell settings get global device_name", device.name, sizeof(device.name)) &&
        !usb_value(adb_opts, "shell getprop ro.product.model", device.name, sizeof(device.name))) {
        snprintf(device.name, sizeof(device.name), "android device");
    }
    if (!wifi_ip_from_usb(adb_opts, ip, sizeof(ip))) {
        scrcpy_log("Phone has no Wi-Fi address; continuing cable casting without wireless setup.");
        return;
    }
    /* Avoid restarting adbd again when wireless access is already enabled. */
    char port[32];
    bool enabled = usb_value(adb_opts, "shell getprop service.adb.tcp.port", port, sizeof(port)) && !strcmp(port, "5555");
    if (!enabled) {
        char command[768];
        snprintf(command, sizeof(command), "%s -d tcpip 5555 > /tmp/adb_tcpip.txt 2>&1", adb_opts);
        if (system(command) != 0) {
            scrcpy_log("Could not enable future wireless access; continuing USB casting.");
            return;
        }
        /* adbd restarts to enable its TCP listener. Resume specifically over USB. */
        enabled = false;
        SDL_Delay(1000);
        for (int attempt = 0; attempt < 10 && !session->interrupted; ++attempt) {
            char state[32];
            if (usb_value(adb_opts, "get-state", state, sizeof(state)) && !strcmp(state, "device")) {
                enabled = true;
                break;
            }
            SDL_Delay(500);
        }
    }
    if (!enabled || session->interrupted) return;
    snprintf(device.endpoint, sizeof(device.endpoint), "%s:5555", ip);
    if (app->settings.conf_persistent && adb_devices_remember(app->settings.conf_dir, &device) == 0) {
        scrcpy_log("Remembered %s for future wireless casting; current casting remains USB.", device.name);
    }
}

static void remember_wireless_phone(session_t *session, const char *adb_opts, const char *endpoint) {
    app_t *app = session->app;
    if (!app->settings.conf_persistent || !app->settings.conf_dir || !app->settings.conf_dir[0]) return;

    adb_device_t device = {0};
    char transport_opts[96];
    snprintf(transport_opts, sizeof(transport_opts), "-s %s", endpoint);
    if (!adb_value(adb_opts, transport_opts, "get-serialno", device.serial, sizeof(device.serial))) {
        scrcpy_log("Could not read the paired phone's ADB serial; wireless pairing will not be remembered.");
        return;
    }
    snprintf(device.endpoint, sizeof(device.endpoint), "%s", endpoint);
    if (!adb_value(adb_opts, transport_opts, "shell settings get global device_name",
                   device.name, sizeof(device.name)) &&
        !adb_value(adb_opts, transport_opts, "shell getprop ro.product.model",
                   device.name, sizeof(device.name))) {
        snprintf(device.name, sizeof(device.name), "android device");
    }
    if (adb_devices_remember(app->settings.conf_dir, &device) != 0) {
        scrcpy_log("Could not remember paired wireless phone %s.", device.name);
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
    scrcpy_connection_t *connection = &session->scrcpy_connection;
    char transport_opts[160] = "-d";

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

    if (session->interrupted) goto thread_cleanup;

    // Pair and connect only after the same ADB server and keys used by scrcpy are ready.
    if (connection->serial[0] || connection->mode == SCRCPY_ADB_QR) {
        size_t code_length = strlen(connection->pair_code);
        if ((connection->serial[0] &&
             strspn(connection->serial, "0123456789.:_-abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ") !=
             strlen(connection->serial)) ||
            (connection->pair_endpoint[0] &&
             (!is_ipv4_endpoint(connection->pair_endpoint) || code_length != 6 ||
              strspn(connection->pair_code, "0123456789") != code_length)) ||
            (connection->mode == SCRCPY_ADB_QR &&
             (!connection->pair_endpoint[0] || code_length != 6 ||
              strspn(connection->pair_code, "0123456789") != code_length))) {
            session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
            streaming_error(session, GS_WRONG_STATE, "Invalid wireless ADB address or pairing code.");
            session_set_state(session, STREAMING_ERROR);
            goto thread_cleanup;
        }
        if (connection->pair_endpoint[0]) {
            scrcpy_log("Pairing wireless ADB device at %s...", connection->pair_endpoint);
            snprintf(cmd, sizeof(cmd), "%s pair %s %s > /tmp/adb_pair.txt 2>&1",
                     adb_opts, connection->pair_endpoint, connection->pair_code);
            if (system(cmd) != 0) {
                session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
                streaming_error(session, GS_WRONG_STATE,
                                "Wireless ADB pairing failed. Check the pairing port and six-digit code on Android.");
                session_set_state(session, STREAMING_ERROR);
                goto thread_cleanup;
            }
        }
        if (connection->mode == SCRCPY_ADB_QR) {
            if (!adb_qr_discover_connect_endpoint(session, connection->pair_endpoint,
                                                  connection->serial, sizeof(connection->serial))) {
                if (session->interrupted) goto thread_cleanup;
                session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
                streaming_error(session, GS_WRONG_STATE,
                                "Paired, but couldn't find the phone's wireless debugging connection. "
                                "Keep Wireless debugging enabled and try again.");
                session_set_state(session, STREAMING_ERROR);
                goto thread_cleanup;
            }
        }
        {
            snprintf(transport_opts, sizeof(transport_opts), "-s %s", connection->serial);
            scrcpy_log("Connecting wireless ADB device at %s...", connection->serial);
            if (!adb_connect_endpoint(adb_opts, connection->serial)) {
                session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
                streaming_error(session, GS_WRONG_STATE,
                                "Wireless ADB connection failed. Check the phone IP address, connection port and Wi-Fi network.");
                session_set_state(session, STREAMING_ERROR);
                goto thread_cleanup;
            }
        }
    }

    // Step 0: Check the selected USB or wireless ADB device.
    scrcpy_log("Checking for connected ADB device...");
    int usb_chk_retries = 15; // Wait up to 15 seconds for a device to be detected
    bool usb_connected = false;
    bool usb_authorized = false;
    
    while (usb_chk_retries > 0 && !session->interrupted) {
        snprintf(cmd, sizeof(cmd), "%s %s get-state > /tmp/adb_state.txt 2>&1", adb_opts, transport_opts);
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
                
                scrcpy_log("ADB device check state: %s", state_line);
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
        if (connection->serial[0]) {
            streaming_error(session, GS_WRONG_STATE, "Wireless ADB device not available. Check the phone's Wireless debugging screen and connection port.");
        } else {
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
        }
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    if (!usb_authorized) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, connection->serial[0] ?
                        "Wireless ADB device is not authorized. Check the phone's Wireless debugging screen." :
                        "USB device connected but UNAUTHORIZED.\n\n"
                        "Please check your mobile phone's screen and accept the 'Allow USB Debugging' authorization prompt (always allow is recommended).");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    /* Prepare future wireless use, but keep this session on the USB transport. */
    if (connection->mode == SCRCPY_ADB_USB && !connection->serial[0]) {
        remember_usb_phone(session, adb_opts);
        if (session->interrupted) goto thread_cleanup;
    } else if (connection->mode == SCRCPY_ADB_QR) {
        remember_wireless_phone(session, adb_opts, connection->serial);
    }

    // Step 1: Push server
    scrcpy_log("Pushing scrcpy-server.jar...");
#ifdef TARGET_WEBOS
    char scrcpy_jar[512];
    snprintf(scrcpy_jar, sizeof(scrcpy_jar), "/media/developer/apps/usr/palm/applications/%s/scrcpy-server.jar", APPID);
#else
    const char *scrcpy_jar = "scrcpy-server.jar";
#endif
    snprintf(cmd, sizeof(cmd), "%s %s push %s /data/local/tmp/scrcpy-server.jar > /tmp/adb_push.log 2>&1", adb_opts, transport_opts, scrcpy_jar);
    int push_ret = system(cmd);
    scrcpy_log("Push returned: %d", push_ret);
    if (push_ret != 0) {
        session_interrupt(session, false, STREAMING_INTERRUPT_ERROR);
        streaming_error(session, GS_WRONG_STATE, "Failed to push scrcpy server to the selected ADB device. See /tmp/adb_push.log.");
        session_set_state(session, STREAMING_ERROR);
        goto thread_cleanup;
    }

    // Step 2: Forward port
    scrcpy_log("Forwarding port...");
    snprintf(cmd, sizeof(cmd), "%s %s forward tcp:27183 localabstract:scrcpy > /tmp/adb_forward.log 2>&1", adb_opts, transport_opts);
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

        const char *args[32];
        int n = 0;
        args[n++] = adb;
        args[n++] = "-H";
        args[n++] = "127.0.0.1";
        args[n++] = "-P";
        args[n++] = "5037";
        if (connection->serial[0]) {
            args[n++] = "-s";
            args[n++] = connection->serial;
        } else {
            args[n++] = "-d";
        }
        args[n++] = "shell";
        args[n++] = "CLASSPATH=/data/local/tmp/scrcpy-server.jar";
        args[n++] = "app_process";
        args[n++] = "/";
        args[n++] = "com.genymobile.scrcpy.Server";
        args[n++] = "3.1";
        args[n++] = "tunnel_forward=true";
        args[n++] = "audio=true";
        args[n++] = "audio_codec=raw";
        args[n++] = "control=false";
        args[n++] = "raw_stream=true";
        args[n++] = "video_bit_rate=40000000";
        args[n++] = "max_fps=60";
        args[n++] = "i_frame_interval=2";
        args[n++] = max_size_arg;
        args[n] = NULL;
        execvp(adb, (char *const *) args);
        _exit(1);
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
            .appName = "ADB.webOS",
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
        snprintf(cmd, sizeof(cmd), "%s %s forward --remove tcp:27183", adb_opts, transport_opts);
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
