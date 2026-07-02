/*
 * GtkInterface.cpp
 */

#include "GtkInterface.hpp"
#include "Cifrado.hpp"
#include "GestorGit.hpp"
#include "RepoConfig.hpp"

#include <cstring>
#include <filesystem>
#include <gtk/gtk.h>
#include <openssl/crypto.h>
#include <string>
#include <thread>

namespace fs = std::filesystem;

// ===========================================================================
// Widgets globales
// ===========================================================================
static GtkWidget *entry_clave = nullptr;
static GtkWidget *entry_directorio = nullptr;
static GtkWidget *entry_url = nullptr;
static GtkWidget *entry_rama = nullptr;
static GtkWidget *entry_mensaje = nullptr;
static GtkWidget *entry_token = nullptr;
static GtkWidget *check_guardar = nullptr;
static GtkWidget *label_estado = nullptr;
static GtkWidget *progress_bar = nullptr;
static GtkTextBuffer *buffer_log = nullptr;
static GtkWidget *text_view_log = nullptr;

static GtkWidget *btn_subir = nullptr;
static GtkWidget *btn_descargar = nullptr;
static GtkWidget *btn_sincronizar = nullptr;

// ===========================================================================
// Declaraciones adelantadas de todos los callbacks estáticos
// ===========================================================================
//static gboolean idle_restaurar_exito(gpointer data);
//static gboolean idle_restaurar_error(gpointer data);
//static gboolean idle_error_con_mensaje(gpointer data);
//static gboolean idle_actualizar_config_ui(gpointer data);
//static void     on_select_folder_finish(GObject *src, GAsyncResult *res, gpointer user_data);
//static void     on_select_folder(GtkButton *btn, gpointer data);
//static void     on_clave_activate(GtkEntry *entry, gpointer user_data);
//static void     run_git_upload(GtkWidget *boton, gpointer user_data);
//static void     run_git_download(GtkWidget *boton, gpointer user_data);
//static void     run_git_sync(GtkWidget *boton, gpointer user_data);

// ===========================================================================
// Estructuras para comunicación asíncrona (idle callbacks)
// ===========================================================================

//// Datos para idle_actualizar_config_ui
//struct DatosConfigUI {
//  std::string url;
//  std::string rama;
//  std::string token;
//};
//
//// Datos para idle_error_con_mensaje
//struct DatosMensaje {
//  std::string mensaje;
//};

// ===========================================================================
// Función de limpieza segura de secretos en memoria
// ===========================================================================
void Intefaz::borrarSecreto(std::string &s) {
  if (!s.empty()) {
    OPENSSL_cleanse(&s[0], s.size());
    s.clear();
    s.shrink_to_fit();
  }
}

// ===========================================================================
// Logging
// ===========================================================================
void Intefaz::append_log(GtkTextBuffer *buffer, const char *mensaje) {
  if (!buffer || !mensaje) {
    return;
  }
  GtkTextIter iter;
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_buffer_insert(buffer, &iter, mensaje, -1);

  if (text_view_log) {
    gtk_text_buffer_get_end_iter(buffer, &iter);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view_log), mark, 0.0, FALSE, 0, 0);
    gtk_text_buffer_delete_mark(buffer, mark);
  }
}

struct DatosIdleLog {
  GtkTextBuffer *buf;
  char *msg;
};

gboolean Intefaz::idle_append_log(gpointer data) {
  DatosIdleLog *d = static_cast<DatosIdleLog *>(data);
  if (d && d->buf && d->msg) {
    append_log(d->buf, d->msg);
  }
  if (d) {
    if (d->msg) {
      g_free(d->msg);
    }
    delete d;
  }
  return G_SOURCE_REMOVE;
}

void Intefaz::append_log_async(const std::string &mensaje) {
  DatosIdleLog *d = new DatosIdleLog();
  d->buf = buffer_log;
  d->msg = g_strdup(mensaje.c_str());
  g_idle_add(idle_append_log, d);
}

// ===========================================================================
// Helpers UI
// ===========================================================================
void Intefaz::set_buttons_sensitive(gboolean sensitive) {
  if (btn_subir) {
    gtk_widget_set_sensitive(btn_subir, sensitive);
  }
  if (btn_descargar) {
    gtk_widget_set_sensitive(btn_descargar, sensitive);
  }
  if (btn_sincronizar) {
    gtk_widget_set_sensitive(btn_sincronizar, sensitive);
  }
}

// ===========================================================================
// Callbacks de idle: restaurar UI tras operaciones en hilos
// ===========================================================================

// Restaurar botones y poner progreso a 1.0 (operación completada)
gboolean Intefaz::idle_restaurar_exito(gpointer data) {
  (void)data;
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
  set_buttons_sensitive(TRUE);
  return G_SOURCE_REMOVE;
}

// Restaurar botones y poner progreso a 0.0 (error / excepción)
gboolean Intefaz::idle_restaurar_error(gpointer data) {
  (void)data;
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  set_buttons_sensitive(TRUE);
  return G_SOURCE_REMOVE;
}

// Escribir mensaje de error en el log y restaurar la UI
gboolean Intefaz::idle_error_con_mensaje(gpointer data) {
  DatosMensaje *d = static_cast<DatosMensaje *>(data);
  if (d) {
    append_log(buffer_log, d->mensaje.c_str());
    delete d;
  }
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  set_buttons_sensitive(TRUE);
  return G_SOURCE_REMOVE;
}

// Actualizar los campos URL, rama y token en la UI tras cargar el XML
gboolean Intefaz::idle_actualizar_config_ui(gpointer data) {
  DatosConfigUI *d = static_cast<DatosConfigUI *>(data);
  if (!d) {
    return G_SOURCE_REMOVE;
  }

  gtk_editable_set_text(GTK_EDITABLE(entry_url), d->url.c_str());
  gtk_editable_set_text(GTK_EDITABLE(entry_rama), d->rama.c_str());

  if (!d->token.empty()) {
    gtk_editable_set_text(GTK_EDITABLE(entry_token), d->token.c_str());
    append_log(buffer_log, "✓ Configuración cargada completa\n");
    append_log(buffer_log, "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    append_log(buffer_log, "🔐 IMPORTANTE: usa Personal Access Token (PAT)\n");
    append_log(buffer_log, "   Genera en: https://github.com/settings/tokens\n");
    append_log(buffer_log, "   Scope recomendado: repo (full repository access)\n");
    append_log(buffer_log, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");
  }

  // Limpiar token de la memoria del struct tras enviarlo al widget.
  borrarSecreto(d->token);
  delete d;
  return G_SOURCE_REMOVE;
}

// ===========================================================================
// Cargar configuración desde XML (ejecutado en hilo secundario)
// ===========================================================================
void Intefaz::cargar_configuracion_desde_xml(const std::string &directorio_path) {
  std::string clave_local =
    gtk_editable_get_text(GTK_EDITABLE(entry_clave));

  std::thread t_cargar([directorio_path, clave_local]() mutable {
    ConfigRepo cfg;
    std::string token_plain;
    std::string url_final;
    std::string rama_final;

    try {
      if (GestorConfig::cargar(directorio_path, cfg)) {
        url_final = cfg.url;
        rama_final = cfg.rama;
        append_log_async("✓ Configuración cargada desde XML\n");

        if (!cfg.tokenEncriptado.empty() && !clave_local.empty()) {
          try {
            token_plain = Cifrado::desencriptar(cfg.tokenEncriptado, clave_local);
            if (!token_plain.empty()) {
              append_log_async("  → Token desencriptado ✓\n");
            } else {
              append_log_async("  ⚠ Token vacío tras descifrar (¿clave incorrecta?)\n");
            }
          } catch (const std::exception &e) {
            append_log_async("  ⚠ Error desencriptando: " + std::string(e.what()) + "\n");
          }
        } else if (!cfg.tokenEncriptado.empty() && clave_local.empty()) {
          append_log_async("  ℹ Token cifrado encontrado; introduce la clave maestra\n");
        }

        // Pasar los datos al hilo principal a través de un struct.
        DatosConfigUI *datos = new DatosConfigUI();
        datos->url = url_final;
        datos->rama = rama_final;
        datos->token = token_plain;
        g_idle_add(idle_actualizar_config_ui, datos);

      } else {
        append_log_async("ℹ No hay configuración guardada para este directorio\n");
        append_log_async("💡 Activa 'Guardar credenciales' antes del primer SUBIR\n\n");
      }

    } catch (const std::exception &e) {
      DatosMensaje *dm = new DatosMensaje();
      dm->mensaje = "⚠ Error cargando configuración: " + std::string(e.what()) + "\n";
      g_idle_add(idle_error_con_mensaje, dm);
    }

    borrarSecreto(clave_local);
    borrarSecreto(token_plain);
    borrarSecreto(url_final);
    borrarSecreto(rama_final);
  });

  t_cargar.detach();
}

// ===========================================================================
// Selector de carpeta
// ===========================================================================

// Callback asíncrono (se llama en el hilo principal por GTK).
void Intefaz::on_select_folder_finish(GObject *src, GAsyncResult *res, gpointer user_data) {
  (void)user_data;
  GError *err = nullptr;
  GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, &err);

  if (file) {
    gchar *path_raw = g_file_get_path(file);
    g_object_unref(file);

    if (path_raw) {
      std::string path_str(path_raw);
      g_free(path_raw);

      if (!path_str.empty()) {
        gtk_editable_set_text(GTK_EDITABLE(entry_directorio), path_str.c_str());
        append_log(buffer_log, ("\n📂 Directorio: " + path_str + "\n").c_str());
        append_log(buffer_log, "   Buscando configuración previa...\n");
        cargar_configuracion_desde_xml(path_str);
      }
    }
  }

  if (err) {
    g_clear_error(&err);
  }
}

void Intefaz::on_select_folder(GtkButton *btn, gpointer data) {
  (void)data;
  GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(btn));
  GtkWindow *win = GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : nullptr;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_select_folder(dialog, win, nullptr,
                                on_select_folder_finish, nullptr);
}

// ===========================================================================
// Enter en campo Clave Maestra → recargar configuración
// ===========================================================================
void Intefaz::on_clave_activate(GtkEntry *entry, gpointer user_data) {
  (void)entry;
  (void)user_data;
  const char *dir_actual = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  if (dir_actual && strlen(dir_actual) > 0) {
    append_log(buffer_log, "\n🔄 Recargando configuración con nueva clave...\n");
    cargar_configuracion_desde_xml(std::string(dir_actual));
  } else {
    append_log(buffer_log, "\nℹ Selecciona primero un directorio\n");
  }
}

// ===========================================================================
// RUN GIT UPLOAD (SUBIR: commit + push)
// ===========================================================================
void Intefaz::run_git_upload(GtkWidget *boton, gpointer user_data) {
  (void)boton;
  (void)user_data;

  set_buttons_sensitive(FALSE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "\n━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━\n");
  append_log(buffer_log, "--- Iniciando operación SUBIR ---\n");

  std::string clave = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string url_repo = gtk_editable_get_text(GTK_EDITABLE(entry_url));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string mensaje = gtk_editable_get_text(GTK_EDITABLE(entry_mensaje));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));
  bool guardar = gtk_check_button_get_active(GTK_CHECK_BUTTON(check_guardar));

  std::thread t([=]() mutable {
    try {
      // Validar directorio
      if (dir_path.empty()) {
        DatosMensaje *dm = new DatosMensaje();
        dm->mensaje = "❌ ERROR: Directorio local vacío\n";
        g_idle_add(idle_error_con_mensaje, dm);
        borrarSecreto(clave);
        borrarSecreto(token);
        return;
      }

      // Guardar configuración cifrada si el usuario lo marcó
      if (guardar && !clave.empty()) {
        ConfigRepo cfg;
        cfg.url = url_repo;
        cfg.directorio = dir_path;
        cfg.rama = rama.empty() ? "main" : rama;
        try {
          cfg.tokenEncriptado = Cifrado::encriptar(token, clave);
          if (GestorConfig::guardar(cfg)) {
            append_log_async("✓ Configuración guardada en XML cifrado\n");
          } else {
            append_log_async("⚠ No se pudo guardar la configuración\n");
          }
        } catch (const std::exception &e) {
          append_log_async("✗ Error cifrando token: " + std::string(e.what()) + "\n");
        }
      }

      // Validar URL
      if (!url_repo.empty() && !GestorGit::validarUrlRepositorio(url_repo)) {
        DatosMensaje *dm = new DatosMensaje();
        dm->mensaje = "❌ URL inválida: no usar credenciales embebidas\n";
        dm->mensaje += "   Formato correcto: https://github.com/user/repo.git\n";
        g_idle_add(idle_error_con_mensaje, dm);
        borrarSecreto(clave);
        borrarSecreto(token);
        return;
      }

      // Decidir operación según si el directorio ya es un repo
      ResultadoOperacionGit resultado;
      bool esRepoNuevo = !fs::exists(fs::path(dir_path) / ".git");

      if (esRepoNuevo) {
        append_log_async("📦 Directorio sin .git detectado\n");

        if (!url_repo.empty()) {
          // Directorio existente no vacío → init + push
          if (fs::exists(dir_path)) {
            bool vacio = (fs::directory_iterator(dir_path) == fs::directory_iterator());
            if (!vacio) {
              append_log_async("   → Inicializando repo existente y subiendo...\n");
              resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, url_repo);
            } else {
              // Directorio vacío → clonar directamente
              append_log_async("   → Directorio vacío, clonando repositorio...\n");
              resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);
            }
          } else {
            append_log_async("   → Creando directorio e iniciando repo...\n");
            resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, url_repo);
          }
        } else {
          append_log_async("   → Sin URL remota: inicialización local\n");
          resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
        }
      } else {
        append_log_async("🔄 Repositorio existente: subiendo cambios...\n");
        resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, url_repo);
      }

      // Mostrar resultado
      std::string estadoStr = resultado.exito ? "✅ ÉXITO" : "❌ FALLIDO";
      std::string msg_final =
        "\n──────────────────────────────────────────────\n"
        "Resultado: "
        + estadoStr + "\n"
                      "Mensaje:   "
        + resultado.mensaje + "\n\n";
      if (!resultado.salidaCompleta.empty()) {
        msg_final += "Salida:\n" + resultado.salidaCompleta;
      }
      msg_final += "──────────────────────────────────────────────\n\n";
      append_log_async(msg_final);

      g_idle_add(idle_restaurar_exito, nullptr);

    } catch (const std::exception &e) {
      DatosMensaje *dm = new DatosMensaje();
      dm->mensaje = "💥 Excepción: " + std::string(e.what()) + "\n";
      g_idle_add(idle_error_con_mensaje, dm);
    }

    borrarSecreto(clave);
    borrarSecreto(token);
    borrarSecreto(mensaje);
    borrarSecreto(url_repo);
    borrarSecreto(dir_path);
    borrarSecreto(rama);
  });

  t.detach();
}

// ===========================================================================
// RUN GIT DOWNLOAD (DESCARGAR: clone si no hay .git, pull si ya existe)
// ===========================================================================
void Intefaz::run_git_download(GtkWidget *boton, gpointer user_data) {
  (void)boton;
  (void)user_data;

  set_buttons_sensitive(FALSE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "\n━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━\n");
  append_log(buffer_log, "--- Iniciando operación DESCARGAR ---\n");

  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string url_repo = gtk_editable_get_text(GTK_EDITABLE(entry_url));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));

  std::thread t([=]() mutable {
    try {
      if (dir_path.empty()) {
        DatosMensaje *dm = new DatosMensaje();
        dm->mensaje = "❌ ERROR: Directorio local vacío\n";
        g_idle_add(idle_error_con_mensaje, dm);
        borrarSecreto(token);
        return;
      }

      ResultadoOperacionGit resultado;
      bool repoExiste = fs::exists(fs::path(dir_path) / ".git");

      if (repoExiste) {
        // Repo ya inicializado → pull
        append_log_async("📥 Repositorio existente: ejecutando pull...\n");
        std::string ramaEfectiva = rama.empty() ? "main" : rama;
        resultado = GestorGit::bajarCambios(dir_path, ramaEfectiva, token);

      } else if (!url_repo.empty()) {
        // No hay .git pero hay URL → clonar
        if (!GestorGit::validarUrlRepositorio(url_repo)) {
          DatosMensaje *dm = new DatosMensaje();
          dm->mensaje = "❌ URL inválida: no usar credenciales embebidas\n";
          g_idle_add(idle_error_con_mensaje, dm);
          borrarSecreto(token);
          return;
        }
        append_log_async("📥 No hay repositorio local; clonando desde URL...\n");
        resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);

      } else {
        // No hay .git ni URL → error
        DatosMensaje *dm = new DatosMensaje();
        dm->mensaje = "❌ El directorio no tiene repositorio git (.git ausente)\n";
        dm->mensaje += "   Para clonar uno nuevo introduce la URL remota\n";
        g_idle_add(idle_error_con_mensaje, dm);
        borrarSecreto(token);
        return;
      }

      std::string estadoStr = resultado.exito ? "✅ ÉXITO" : "❌ FALLIDO";
      std::string msg_final =
        "\n──────────────────────────────────────────────\n"
        "Resultado: "
        + estadoStr + "\n"
                      "Mensaje:   "
        + resultado.mensaje + "\n\n"
                              "Salida:\n"
        + resultado.salidaCompleta + "──────────────────────────────────────────────\n\n";
      append_log_async(msg_final);

      g_idle_add(idle_restaurar_exito, nullptr);

    } catch (const std::exception &e) {
      DatosMensaje *dm = new DatosMensaje();
      dm->mensaje = "💥 Excepción: " + std::string(e.what()) + "\n";
      g_idle_add(idle_error_con_mensaje, dm);
    }

    borrarSecreto(token);
    borrarSecreto(dir_path);
    borrarSecreto(url_repo);
    borrarSecreto(rama);
  });

  t.detach();
}

// ===========================================================================
// RUN GIT SYNC (SINCRONIZAR: pull + push)
// ===========================================================================
void Intefaz::run_git_sync(GtkWidget *boton, gpointer user_data) {
  (void)boton;
  (void)user_data;

  set_buttons_sensitive(FALSE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "\n━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━═━\n");
  append_log(buffer_log, "--- Iniciando SINCRONIZACIÓN ---\n");

  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string url_repo = gtk_editable_get_text(GTK_EDITABLE(entry_url));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string mensaje = gtk_editable_get_text(GTK_EDITABLE(entry_mensaje));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));

  std::thread t([=]() mutable {
    try {
      if (dir_path.empty()) {
        DatosMensaje *dm = new DatosMensaje();
        dm->mensaje = "❌ ERROR: Directorio vacío\n";
        g_idle_add(idle_error_con_mensaje, dm);
        borrarSecreto(token);
        return;
      }

      if (!fs::exists(fs::path(dir_path) / ".git")) {
        DatosMensaje *dm = new DatosMensaje();
        dm->mensaje = "❌ No es un repositorio git (.git ausente)\n";
        dm->mensaje += "   Para sincronizar primero descarga (clona) el repo\n";
        g_idle_add(idle_error_con_mensaje, dm);
        borrarSecreto(token);
        return;
      }

      std::string ramaEfectiva = rama.empty() ? "main" : rama;

      // Paso 1: Pull
      append_log_async("📥 Paso 1/2: pull...\n");
      ResultadoOperacionGit pullResult = GestorGit::bajarCambios(dir_path, ramaEfectiva, token);
      if (pullResult.exito) {
        append_log_async("✅ Pull completado\n\n");
      } else {
        append_log_async("⚠ Pull: " + pullResult.mensaje + "\n\n");
      }

      // Paso 2: Push
      append_log_async("📤 Paso 2/2: commit + push...\n");
      ResultadoOperacionGit pushResult =
        GestorGit::subirCambios(dir_path, ramaEfectiva, mensaje, token, url_repo);
      if (pushResult.exito) {
        append_log_async("✅ Push completado\n\n");
      } else {
        append_log_async("⚠ Push: " + pushResult.mensaje + "\n\n");
      }

      bool ambosExito = pullResult.exito && pushResult.exito;
      std::string estadoStr = ambosExito ? std::string("✅ TODO OK") : std::string("⚠ PARCIAL");
      std::string pullIcon = pullResult.exito ? std::string("✅") : std::string("❌");
      std::string pushIcon = pushResult.exito ? std::string("✅") : std::string("❌");

      std::string msg_final =
        "\n═══════════════════════════════════════════\n"
        "SINCRONIZACIÓN: "
        + estadoStr + "\n"
                      "Pull: "
        + pullIcon + "  |  Push: " + pushIcon + "\n\n"
                                                "--- DETALLE PULL ---\n"
        + pullResult.salidaCompleta + "\n"
                                      "--- DETALLE PUSH ---\n"
        + pushResult.salidaCompleta + "\n"
                                      "═══════════════════════════════════════════\n\n";
      append_log_async(msg_final);

      g_idle_add(idle_restaurar_exito, nullptr);

    } catch (const std::exception &e) {
      DatosMensaje *dm = new DatosMensaje();
      dm->mensaje = "💥 Excepción: " + std::string(e.what()) + "\n";
      g_idle_add(idle_error_con_mensaje, dm);
    }

    borrarSecreto(token);
    borrarSecreto(dir_path);
    borrarSecreto(url_repo);
    borrarSecreto(rama);
    borrarSecreto(mensaje);
  });

  t.detach();
}

void Intefaz::update_progress_with_message(const gchar *msg, gdouble fraction) {
  if (label_estado) {
    gtk_label_set_text(GTK_LABEL(label_estado), msg ? msg : "Procesando...");
  }
  if (progress_bar) {
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), fraction);
  }
  g_main_context_iteration(g_main_context_default(), FALSE);
}


void Intefaz::run_git_compiler(GtkWidget *boton, gpointer user_data) {
  (void)boton;
  (void)user_data;

  const gchar *dir_local = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));

  if (!dir_local || strlen(dir_local) == 0) {
    append_log(buffer_log, "⚠️ ERROR: Debes especificar un directorio local primero.\n");
    gtk_label_set_text(GTK_LABEL(label_estado), "Estado: Error - Sin directorio");
    return;
  }

  GError *error = NULL;
  GFile *file = g_file_new_for_path(dir_local);
  GFileInfo *info = g_file_query_info(
    file,
    G_FILE_ATTRIBUTE_STANDARD_TYPE,
    G_FILE_QUERY_INFO_NONE,
    NULL, &error);

  if (!info || g_file_info_get_file_type(info) != G_FILE_TYPE_DIRECTORY) {
    append_log(buffer_log, "⚠️ ERROR: Directorio no válido o no accesible:\n");
    append_log(buffer_log, dir_local);
    append_log(buffer_log, "\n\n");
    gtk_label_set_text(GTK_LABEL(label_estado), "Estado: Error - Directorio inválido");

    if (error && g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      append_log(buffer_log, "💡 Crear directorio manualmente o seleccionar uno existente.\n");
    }

    if (error) g_clear_error(&error);  // ✅ LIMPIAR MEMORY LEAK
    if (info) g_object_unref(info);
    if (file) g_object_unref(file);  // ✅ Liberar también el file
    return;
  }

  g_object_unref(info);
  g_object_unref(file);
  if (error) g_clear_error(&error);  // ✅ Libear error antes de continuar

  append_log(buffer_log, "═══════════════════════════════════════════════════════\n");
  append_log(buffer_log, "📝 COMPILANDO REPOSITORIO GIT\n");
  append_log(buffer_log, "═══════════════════════════════════════════════════════\n");
  append_log(buffer_log, "Directorio: ");
  append_log(buffer_log, dir_local);
  append_log(buffer_log, "\n\n");

  gtk_label_set_text(GTK_LABEL(label_estado), "Estado: Compilando...");
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);

  append_log(buffer_log, "Paso 1/3: Limpieza previa...\n");
  update_progress_with_message("Limpieza...", 0.25);

  gint exit_status_clean = 0;
  gchar *output_clean = NULL;
  gchar *stderr_clean = NULL;
  GError *err_spawn = NULL;

  std::string clean_cmd = std::string("cd \"") + dir_local + "\" && make clean";

  if (!g_spawn_command_line_sync(clean_cmd.c_str(), &output_clean, &stderr_clean,
                                 &exit_status_clean, &err_spawn)) {
    append_log(buffer_log, "⚠️ Advertencia: make clean falló.\n");
    if (stderr_clean) {
      append_log(buffer_log, stderr_clean);
      g_free(stderr_clean);
      stderr_clean = NULL;
    }
    if (err_spawn) {
      append_log(buffer_log, err_spawn->message);
      append_log(buffer_log, "\n");
      g_error_free(err_spawn);
      err_spawn = NULL;
    }
  }

  if (output_clean) {
    g_free(output_clean);
    output_clean = NULL;
  }
  if (stderr_clean) {
    g_free(stderr_clean);
    stderr_clean = NULL;
  }

  update_progress_with_message("Compilando secretos...", 0.30);
  append_log(buffer_log, "\nPaso 2/3: Generando secretos y compilando...\n");
  append_log(buffer_log, "(Los secretos se generan aleatoriamente cada vez)\n\n");

  gint exit_status_build = 0;
  gchar *output_build = NULL;
  gchar *stderr_build = NULL;
  GError *err_build = NULL;

  std::string cmd_build_str = std::string("cd \"") + dir_local + "\" && make -f makelist.txt all";

  if (!g_spawn_command_line_sync(cmd_build_str.c_str(), &output_build, &stderr_build,
                                 &exit_status_build, &err_build)) {
    append_log(buffer_log, "❌ ERROR FATAL al ejecutar make:\n");
    if (stderr_build) {
      append_log(buffer_log, stderr_build);
      g_free(stderr_build);
      stderr_build = NULL;
    }
    if (err_build) {
      append_log(buffer_log, err_build->message);
      g_error_free(err_build);
      err_build = NULL;
    }

    gtk_label_set_text(GTK_LABEL(label_estado), "Status: Fallo la compilacion");
    update_progress_with_message("Fallo!", 1.0);
    return;
  }

  if (output_build && strlen(output_build) > 0) {
    gchar **lines = g_strsplit(output_build, "\n", 0);
    for (gchar **line = lines; *line != NULL; line++) {
      if (strstr(*line, "SECRET_NUMBER") || strstr(*line, "Compilando:") || strstr(*line, "Enlazando:") || strstr(*line, "Completada")) {
        append_log(buffer_log, *line);
        append_log(buffer_log, "\n");
      }
    }
    g_strfreev(lines);
    g_free(output_build);
    output_build = NULL;
  }

  if (stderr_build && strlen(stderr_build) > 0) {
    append_log(buffer_log, "--- Salida STDERR ---\n");
    append_log(buffer_log, stderr_build);
    append_log(buffer_log, "\n");
    g_free(stderr_build);
    stderr_build = NULL;
  }

  update_progress_with_message("Limpiando secretos del disco...", 0.85);

  if (exit_status_build == 0) {
    std::string ruta_ejecutable_str = std::string(dir_local) + "/GithubManager";
    GFile *exec_file = g_file_new_for_path(ruta_ejecutable_str.c_str());

    if (g_file_query_exists(exec_file, NULL)) {
      GFileInfo *file_info = g_file_query_info(
        exec_file,
        G_FILE_ATTRIBUTE_STANDARD_SIZE,
        G_FILE_QUERY_INFO_NONE,
        NULL, NULL);

      if (file_info) {
        guint64 tamano_bytes = g_file_info_get_size(file_info);
        g_autofree gchar *tamano_str = g_format_size(tamano_bytes);

        append_log(buffer_log, "\n");
        append_log(buffer_log, "═══════════════════════════════════════════════════════\n");
        append_log(buffer_log, "✅ COMPILACION COMPLETADA CON ÉXITO\n");
        append_log(buffer_log, "═══════════════════════════════════════════════════════\n");
        append_log(buffer_log, "Ejecutable: ");
        append_log(buffer_log, ruta_ejecutable_str.c_str());
        append_log(buffer_log, "\n");
        append_log(buffer_log, "Tamaño: ");
        append_log(buffer_log, tamano_str);
        append_log(buffer_log, "\n\n");
        append_log(buffer_log, "Nota: Secretos temporales borrados del sistema de archivos.\n");
        append_log(buffer_log, "Permanecen SOLO incrustados en el binario.\n\n");

        g_object_unref(file_info);
      } else {
        append_log(buffer_log, "✅ Compilación completada (no se pudo verificar tamaño)\n\n");
      }

      g_object_unref(exec_file);
      gtk_label_set_text(GTK_LABEL(label_estado), "Estado: Compilado correctamente");
      update_progress_with_message("Completo!", 1.0);

    } else {
      append_log(buffer_log, "⚠️ Ejecutable no encontrado después de compilar.\n");
      append_log(buffer_log, "Path esperado: ");
      append_log(buffer_log, ruta_ejecutable_str.c_str());
      append_log(buffer_log, "\n");

      g_object_unref(exec_file);
      gtk_label_set_text(GTK_LABEL(label_estado), "Advertencia - Sin ejecutable");
      update_progress_with_message("Sin ejec.", 1.0);
    }
  } else {
    append_log(buffer_log, "\n❌ LA COMPILACIÓN FALLÓ CON CÓDIGO DE SALIDA: ");
    gchar status_str[32];
    snprintf(status_str, sizeof(status_str), "%d", exit_status_build);
    append_log(buffer_log, status_str);
    append_log(buffer_log, "\n\n");
    append_log(buffer_log, "Revisa dependencias instaladas:\n");
    append_log(buffer_log, "- GTK4, tinyxml2, openssl\n");
    append_log(buffer_log, "Uso: ./scripts/install-deps.sh\n\n");

    gtk_label_set_text(GTK_LABEL(label_estado), "Estado: Error en compilación");
    update_progress_with_message("Fallo!", 1.0);
  }
}


// ===========================================================================
// setClaveMaestra: establecer la clave maestra desde código externo
// ===========================================================================
void Intefaz::setClaveMaestra(const std::string &clave) {
  if (entry_clave) {
    gtk_editable_set_text(GTK_EDITABLE(entry_clave), clave.c_str());
  }
}

// ===========================================================================
// build_interface: construye la ventana principal
// Firma exacta que GTK espera para la señal "activate":
//   void callback(GtkApplication*, gpointer)
// ===========================================================================
void Intefaz::build_interface(GtkApplication *app, gpointer user_data) {
  (void)user_data;

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Gestor Git V1.0 🔐");
  gtk_window_set_default_size(GTK_WINDOW(window), 720, 750);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 15);
  gtk_widget_set_margin_start(grid, 20);
  gtk_widget_set_margin_end(grid, 20);
  gtk_widget_set_margin_top(grid, 20);
  gtk_widget_set_margin_bottom(grid, 20);

  int row = 0;

  // ====================== CLAVE MAESTRA ======================
  GtkWidget *lbl_clave = gtk_label_new("<b>🔑 Clave Maestra:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_clave), TRUE);
  gtk_widget_set_halign(lbl_clave, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl_clave, 0, row, 1, 1);

  entry_clave = gtk_password_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_clave), "Para cifrar/descifrar tokens guardados");
  gtk_widget_set_tooltip_text(entry_clave, "Presiona [Enter] para recargar la configuración con esta clave");
  gtk_widget_set_hexpand(entry_clave, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entry_clave, 1, row++, 1, 1);

  // ====================== DIRECTORIO LOCAL ====================
  GtkWidget *lbl_dir = gtk_label_new("<b>📁 Directorio Local:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_dir), TRUE);
  gtk_widget_set_halign(lbl_dir, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl_dir, 0, row, 1, 1);

  entry_directorio = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_directorio), "/home/usuario/proyectos/mi-proyecto");
  gtk_widget_set_hexpand(entry_directorio, TRUE);

  GtkWidget *btn_sel = gtk_button_new_with_label("📂 Seleccionar");
  g_signal_connect(btn_sel, "clicked", G_CALLBACK(on_select_folder), NULL);

  GtkWidget *hbox_dir = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(hbox_dir), entry_directorio);
  gtk_box_append(GTK_BOX(hbox_dir), btn_sel);
  gtk_grid_attach(GTK_GRID(grid), hbox_dir, 1, row++, 1, 1);

  // ====================== URL REMOTO ==========================
  GtkWidget *lbl_url = gtk_label_new("<b>🌐 URL Remoto:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_url), TRUE);
  gtk_widget_set_halign(lbl_url, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, row, 1, 1);

  entry_url = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_url), "https://github.com/usuario/repo.git");
  gtk_widget_set_tooltip_text(entry_url, "HTTPS sin credenciales embebidas: https://github.com/user/repo.git");
  gtk_widget_set_hexpand(entry_url, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entry_url, 1, row++, 1, 1);

  // ====================== RAMA =================================
  GtkWidget *lbl_ram = gtk_label_new("<b>🌿 Rama:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_ram), TRUE);
  gtk_widget_set_halign(lbl_ram, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl_ram, 0, row, 1, 1);

  entry_rama = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_rama), "main");
  gtk_widget_set_hexpand(entry_rama, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entry_rama, 1, row++, 1, 1);

  // ====================== MENSAJE COMMIT =======================
  GtkWidget *lbl_msg = gtk_label_new("<b>✏️ Mensaje Commit:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_msg), TRUE);
  gtk_widget_set_halign(lbl_msg, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl_msg, 0, row, 1, 1);

  entry_mensaje = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_mensaje),
                                 "Actualización automática del código");
  gtk_widget_set_hexpand(entry_mensaje, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entry_mensaje, 1, row++, 1, 1);

  // ====================== TOKEN PAT ============================
  GtkWidget *lbl_tok = gtk_label_new("<b>🔐 Token GitHub (PAT):</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_tok), TRUE);
  gtk_widget_set_halign(lbl_tok, GTK_ALIGN_END);
  gtk_grid_attach(GTK_GRID(grid), lbl_tok, 0, row, 1, 1);

  entry_token = gtk_password_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_token), "ghp_xxxxxxxxxx (Personal Access Token)");
  gtk_widget_set_tooltip_text(entry_token, "PAT generado en github.com/settings/tokens\n Scope recomendado: repo");
  gtk_widget_set_hexpand(entry_token, TRUE);
  gtk_grid_attach(GTK_GRID(grid), entry_token, 1, row++, 1, 1);

  // ====================== CHECKBOX GUARDAR =====================
  check_guardar = gtk_check_button_new_with_label(
    "💾 Guardar credenciales cifradas (XML por repo)");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check_guardar), TRUE);
  gtk_widget_set_tooltip_text(check_guardar, "Crea un XML cifrado por directorio para carga automática futura");
  gtk_grid_attach(GTK_GRID(grid), check_guardar, 1, row++, 1, 1);

  // ====================== PROGRESO Y ESTADO ====================
  progress_bar = gtk_progress_bar_new();
  gtk_widget_set_hexpand(progress_bar, TRUE);
  gtk_grid_attach(GTK_GRID(grid), progress_bar, 0, row++, 2, 1);

  label_estado = gtk_label_new("Estado: Listo");
  gtk_widget_set_halign(label_estado, GTK_ALIGN_START);
  gtk_grid_attach(GTK_GRID(grid), label_estado, 0, row++, 2, 1);

  // ====================== BOTONES ACCIÓN =======================
  GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_hexpand(button_box, TRUE);

  btn_descargar = gtk_button_new_with_label("⬇ DESCARGAR");
  gtk_widget_set_size_request(btn_descargar, -1, 45);
  gtk_widget_set_tooltip_text(btn_descargar, "Clone (1ª vez) o pull (repo existente)");
  gtk_widget_set_hexpand(btn_descargar, TRUE);
  g_signal_connect(btn_descargar, "clicked", G_CALLBACK(run_git_download), NULL);
  gtk_box_append(GTK_BOX(button_box), btn_descargar);

  btn_sincronizar = gtk_button_new_with_label("🔄 SINCRONIZAR");
  gtk_widget_set_size_request(btn_sincronizar, -1, 45);
  gtk_widget_set_tooltip_text(btn_sincronizar, "Pull + commit + push");
  gtk_widget_set_hexpand(btn_sincronizar, TRUE);
  g_signal_connect(btn_sincronizar, "clicked", G_CALLBACK(run_git_sync), NULL);
  gtk_box_append(GTK_BOX(button_box), btn_sincronizar);

  btn_subir = gtk_button_new_with_label("⬆ SUBIR");
  gtk_widget_set_size_request(btn_subir, -1, 45);
  gtk_widget_set_tooltip_text(btn_subir, "Commit + push (local → remoto)");
  gtk_widget_set_hexpand(btn_subir, TRUE);
  g_signal_connect(btn_subir, "clicked", G_CALLBACK(run_git_upload), NULL);
  gtk_box_append(GTK_BOX(button_box), btn_subir);

  // ====================== BOTÓN Compila Git =====================
  GtkWidget *btn_CompilaGit = gtk_button_new_with_label(" 📝 Compila .GIT ");
  g_signal_connect(btn_CompilaGit, "clicked", G_CALLBACK(run_git_compiler), NULL);
  gtk_widget_set_size_request(btn_CompilaGit, -1, 50);
  gtk_widget_set_tooltip_text(btn_CompilaGit, "Versión optimizada para muchos archivos\nUsa git add inteligente + progreso");
  gtk_box_append(GTK_BOX(button_box), btn_CompilaGit);


  gtk_grid_attach(GTK_GRID(grid), button_box, 0, row++, 2, 1);

  // ====================== ÁREA DE LOG ==========================
  GtkWidget *frame_log = gtk_frame_new("📋 Registro de Operaciones");
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scrolled, TRUE);
  gtk_widget_set_size_request(scrolled, -1, 200);

  text_view_log = gtk_text_view_new();
  buffer_log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_log));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_log), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_log), GTK_WRAP_WORD_CHAR);
  gtk_widget_set_vexpand(text_view_log, TRUE);

  // Fuente monospace para el log
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(provider,
                                    ".log-mono {"
                                    "  font-family: 'Liberation Mono', 'DejaVu Sans Mono', monospace;"
                                    "  font-size: 10px;"
                                    "}");
  gtk_style_context_add_provider_for_display(
    gdk_display_get_default(),
    GTK_STYLE_PROVIDER(provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_widget_add_css_class(text_view_log, "log-mono");
  g_object_unref(provider);

  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), text_view_log);
  gtk_frame_set_child(GTK_FRAME(frame_log), scrolled);
  gtk_widget_set_vexpand(frame_log, TRUE);
  gtk_grid_attach(GTK_GRID(grid), frame_log, 0, row, 2, 1);

  // ====================== SEÑALES EXTRA ========================
  g_signal_connect(entry_clave, "activate", G_CALLBACK(on_clave_activate), NULL);

  // Mensaje de bienvenida
  append_log(buffer_log,
             "╔══════════════════════════════════════════════════════╗\n"
             "║           GESTOR GIT V1.0 - Interfaz GTK4            ║\n"
             "╚══════════════════════════════════════════════════════╝\n\n"
             "· Selecciona un directorio para empezar\n"
             "· Configura tu Token PAT de GitHub\n"
             "· DESCARGAR: clona (1ª vez) o hace pull (repo existente)\n"
             "· SUBIR:     commit + push de cambios locales\n\n"
             "🔗 PAT en: https://github.com/settings/tokens\n"
             "   Scope recomendado: repo (Full control private repos)\n\n");

  gtk_window_set_child(GTK_WINDOW(window), grid);
  gtk_window_present(GTK_WINDOW(window));
}
