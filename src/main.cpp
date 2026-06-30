/*
 * main.cpp
 *
 *  Created on: 30 jun 2026
 *      Author: DjSteker
 */

//============================================================================
// Name        : GithubManager_V1
// Author      :
// Version     : 0.0
// Copyright   :
// Description : file manager for the repository
//$(shell pkg-config --cflags gtk4 tinyxml2 openssl)
//$(shell pkg-config --libs gtk4 tinyxml2 openssl)
//============================================================================


#include <gtk/gtk.h>
#include "GtkInterface.hpp"

int main(int argc, char **argv) {
    GtkApplication *app = gtk_application_new(
        "com.ejemplo.gestorgit",
        G_APPLICATION_DEFAULT_FLAGS
    );

    // Conectar la señal de activación a nuestra función de construcción de UI
    g_signal_connect(app, "activate", G_CALLBACK(build_interface), NULL);

    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    return status;
}

