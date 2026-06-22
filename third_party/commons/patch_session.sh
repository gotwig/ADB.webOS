#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/app_session.c"

cat << 'SESSION' > tmp_session.c
#include "app_session.h"
#include "app.h"
#include "stream/session.h"
#include "logging.h"
#include "stream/session_priv.h"
#include "stream/session_worker.h"

int app_session_begin(app_t *app, const uuidstr_t *uuid, const APP_LIST *gs_app) {
    if (app->session != NULL) {
        commons_log_error("App", "Session already exists");
        return -1;
    }
    const pclist_t *node = pcmanager_node(pcmanager, uuid);
    if (node == NULL) {
        commons_log_error("App", "Failed to find node %s", (const char *) uuid);
        return -1;
    }
    app->session = session_create(app, app_configuration, node->server, gs_app);
    return 0;
}

void app_session_destroy(app_t *app) {
    if (app->session == NULL) {
        return;
    }
    session_destroy(app->session);
    app->session = NULL;
}

int app_session_begin_scrcpy(app_t *app) {
    if (app->session != NULL) {
        commons_log_error("App", "Session already exists");
        return -1;
    }
    
    // We MUST initialize session exactly like session_create to avoid UI crashes
    session_t *session = malloc(sizeof(session_t));
    memset(session, 0, sizeof(session_t));
    
    // UI expects config stream to be populated or it crashes on resize/info
    session->config.stream.width = 1280;
    session->config.stream.height = 720;
    session->config.stream.fps = 30;
    
    session->app = app;
    session->display_width = app->ui.width;
    session->display_height = app->ui.height;
    session->audio_cap = app->ss4s.audio_cap;
    session->video_cap = app->ss4s.video_cap;
    
    // UI might check server for info
    session->server = calloc(1, sizeof(SERVER_DATA));
    
    session->mutex = SDL_CreateMutex();
    session->cond = SDL_CreateCond();
    
    session_input_init(&session->input, session, &app->input, &session->config);
    
    app->session = session;
    
    extern int session_worker(session_t *session);
    session->thread = SDL_CreateThread((SDL_ThreadFunction)session_worker, "session", session);
    return 0;
}
SESSION

mv tmp_session.c "$FILE"
