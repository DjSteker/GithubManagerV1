/*
 * GtkInterface.cpp
 *  Created on: 30 jun 2026
 *  Author: DjSteker
 */

#include "GtkInterface.hpp"
#include "GestorGit.hpp"
#include "Cifrado.hpp"
#include "RepoConfig.hpp"

#include <gtk/gtk.h>
#include <thread>
#include <string>
#include <filesystem>
#include <cstring>
#include <openssl/crypto.h> // OPENSSL_cleanse

namespace fs = std::filesystem;

// Widgets globales (definidos aquí, únicos en este translation unit)
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
static GtkWidget *text_view_log = nullptr; // referencia al GtkTextView (GTK4)

// Estructuras auxiliares para comunicación con el hilo principal
struct LogData {
  GtkWidget *label;
  gchar *texto;
};

// append_log: uso directo desde hilo principal para insertar texto en el buffer
static void append_log(GtkTextBuffer *buffer, const char *mensaje) {
  if (!buffer || !mensaje) return;
  GtkTextIter iter;
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_buffer_insert(buffer, &iter, mensaje, -1);

  // Auto-scroll si tenemos el text_view
  if (text_view_log) {
    gtk_text_buffer_get_end_iter(buffer, &iter);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view_log), mark, 0.0, FALSE, 0, 0);
    gtk_text_buffer_delete_mark(buffer, mark);
  }
}

// append_log_async: segura para usar desde hilos de trabajo (usa g_idle_add)
struct IdleLogData {
  GtkTextBuffer *buf;
  char *msg;
};
static gboolean idle_append_log(gpointer data) {
  auto *d = static_cast<IdleLogData *>(data);
  if (d && d->buf && d->msg) {
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter(d->buf, &iter);
    gtk_text_buffer_insert(d->buf, &iter, d->msg, -1);

    if (text_view_log) {
      gtk_text_buffer_get_end_iter(d->buf, &iter);
      GtkTextMark *mark = gtk_text_buffer_create_mark(d->buf, NULL, &iter, FALSE);
      gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view_log), mark, 0.0, FALSE, 0, 0);
      gtk_text_buffer_delete_mark(d->buf, mark);
    }
    g_free(d->msg);
  }
  delete d;
  return G_SOURCE_REMOVE;
}
static void append_log_async(const std::string &mensaje) {
  IdleLogData *d = new IdleLogData();
  d->buf = buffer_log;
  d->msg = g_strdup(mensaje.c_str());
  g_idle_add(idle_append_log, d);
}

// Actualiza label de estado desde idle
static gboolean actualizar_ui_idle(gpointer data) {
  auto params = static_cast<LogData *>(data);
  if (params->label && params->texto) {
    gtk_label_set_text(GTK_LABEL(params->label), params->texto);
  }
  g_free(params->texto);
  delete params;
  return G_SOURCE_REMOVE;
}

// Función que ejecuta la tarea git en segundo plano.
// NOTA: todas las interacciones con GTK deben hacerse desde el hilo principal (usar g_idle_add / append_log_async).
void run_git_task(GtkWidget *boton, gpointer user_data) {
  gtk_widget_set_sensitive(boton, FALSE);
  if (progress_bar) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "--- Iniciando operación ---\n");

  // Tomar copias locales (modificables) de los valores de los widgets.
  // Usamos copias locales para poder limpiarlas (OPENSSL_cleanse) antes de que el hilo termine.
  std::string clave = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string url_repo = gtk_editable_get_text(GTK_EDITABLE(entry_url));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string mensaje = gtk_editable_get_text(GTK_EDITABLE(entry_mensaje));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));
  bool guardar = gtk_check_button_get_active(GTK_CHECK_BUTTON(check_guardar));

  // Lanzar hilo de trabajo
  std::thread t([=]() mutable {
    ResultadoOperacionGit resultado;
    try {
      // Guardar configuración cifrada (si corresponde)
      if (guardar && !clave.empty() && !dir_path.empty()) {
        ConfigRepo cfg;
        cfg.url = url_repo;
        cfg.directorio = dir_path;
        cfg.rama = rama.empty() ? "main" : rama;

        // Encriptar el token con la clave (Cifrado::encriptar devuelve std::string)
        std::string token_cifrado;
        try {
          token_cifrado = Cifrado::encriptar(token, clave);
        } catch (const std::exception &e) {
          append_log_async(std::string("Error cifrando token: ") + e.what() + "\n");
        }

        cfg.tokenEncriptado = token_cifrado;
        if (GestorConfig::guardar(cfg)) {
          append_log_async("✓ Configuración guardada en XML cifrado\n");
        } else {
          append_log_async("⚠ No se pudo guardar la configuración en XML\n");
        }

        // limpiar token_cifrado en memoria (no necesario si local, pero por higiene)
        if (!token_cifrado.empty()) {
          OPENSSL_cleanse(&token_cifrado[0], token_cifrado.size());
          token_cifrado.clear();
        }
      }

      bool esRepoNuevo = !fs::exists(fs::path(dir_path) / ".git");

      if (esRepoNuevo) {
        append_log_async("Repositorio nuevo detectado...\n");
        if (!url_repo.empty()) {
          // Si el directorio existe y no está vacío, evitar git clone directo
          if (fs::exists(dir_path)) {
            bool vacio = (fs::directory_iterator(dir_path) == fs::directory_iterator());
            if (!vacio) {
              append_log_async("Directorio existente y no vacío: inicializando repo local y añadiendo remote origin...\n");
              resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, url_repo);
            } else {
              append_log_async("Clonando repositorio...\n");
              resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);
              if (resultado.exito) {
                append_log_async("Clonación OK. Subiendo (si procede)...\n");
                resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
              }
            }
          } else {
            append_log_async("Clonando repositorio en directorio nuevo...\n");
            resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);
            if (resultado.exito) {
              append_log_async("Clonación OK. Subiendo (si procede)...\n");
              resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
            }
          }
        } else {
          append_log_async("Inicializando repo local (sin remote)...\n");
          resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
        }
      } else {
        append_log_async("Repo existente. Subiendo cambios...\n");
        resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token, "");
      }

      std::string msg_final = std::string("Resultado: ") + (resultado.exito ? "ÉXITO" : "FALLIDO") + "\n";
      msg_final += "Mensaje: " + resultado.mensaje + "\n\nSalida:\n" + resultado.salidaCompleta + "\n";

      // Actualiza label de estado y log en hilo principal
      LogData *ui = new LogData{ label_estado, g_strdup(msg_final.c_str()) };
      g_idle_add(actualizar_ui_idle, ui);
      append_log_async(msg_final);

      // Restaurar UI: progreso y botón sensible
      g_idle_add([](gpointer d) -> gboolean {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        gtk_widget_set_sensitive(GTK_WIDGET(d), TRUE);
        return G_SOURCE_REMOVE;
      }, boton);

    } catch (const std::exception &e) {
      std::string err = "Excepción: ";
      err += e.what();
      err += "\n";
      LogData *ui = new LogData{ label_estado, g_strdup(err.c_str()) };
      g_idle_add(actualizar_ui_idle, ui);
      append_log_async(err);
      g_idle_add([](gpointer d) -> gboolean {
        gtk_widget_set_sensitive(GTK_WIDGET(d), TRUE);
        return G_SOURCE_REMOVE;
      }, boton);
    }

    // LIMPIEZA DE DATOS SENSIBLES EN MEMORIA (antes de terminar el hilo)
    if (!token.empty()) {
      OPENSSL_cleanse(&token[0], token.size());
      token.clear();
    }
    if (!clave.empty()) {
      OPENSSL_cleanse(&clave[0], clave.size());
      clave.clear();
    }
    if (!mensaje.empty()) {
      OPENSSL_cleanse(&mensaje[0], mensaje.size());
      mensaje.clear();
    }
    if (!url_repo.empty()) {
      // url_repo no es tan sensible pero se limpia por higiene
      OPENSSL_cleanse(&url_repo[0], url_repo.size());
      url_repo.clear();
    }
    if (!dir_path.empty()) {
      OPENSSL_cleanse(&dir_path[0], dir_path.size());
      dir_path.clear();
    }
  });

  t.detach();
}

// Selector de carpeta (usa GtkFileDialog en GTK4). Mantenerlo en hilo principal (es UI).
void on_select_folder(GtkButton *btn, gpointer data) {
  GtkWindow *win = GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(btn)));
  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_select_folder(
    dialog, win, NULL,
    [](GObject *src, GAsyncResult *res, gpointer udata) {
      GFile *file = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(src), res, NULL);
      if (file) {
        gchar *path = g_file_get_path(file);
        gtk_editable_set_text(GTK_EDITABLE(entry_directorio), path);

        // Intentar cargar configuración existente para esa carpeta
        ConfigRepo cfg;
        if (GestorConfig::cargar(path, cfg)) {
          gtk_editable_set_text(GTK_EDITABLE(entry_url), cfg.url.c_str());
          gtk_editable_set_text(GTK_EDITABLE(entry_rama), cfg.rama.c_str());

          // Si hay token en el XML y hay clave en la UI, intentar desencriptar
          std::string clave_local = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
          if (!cfg.tokenEncriptado.empty() && !clave_local.empty()) {
            std::string token_plain;
            try {
              token_plain = Cifrado::desencriptar(cfg.tokenEncriptado, clave_local);
            } catch (...) {
              token_plain.clear();
            }
            if (!token_plain.empty()) {
              gtk_editable_set_text(GTK_EDITABLE(entry_token), token_plain.c_str());
              append_log(buffer_log, "✓ Configuración cargada desde XML\n");
            }
            // limpiar copia local de la clave y del token_plain
            if (!token_plain.empty()) {
              OPENSSL_cleanse(&token_plain[0], token_plain.size());
              token_plain.clear();
            }
          }
          if (!clave_local.empty()) {
            OPENSSL_cleanse(&clave_local[0], clave_local.size());
            clave_local.clear();
          }
        }
        g_free(path);
        g_object_unref(file);
      }
    },
    NULL);
}

// Construye la interfaz. Esta función se registra como callback "activate" de GtkApplication.
void build_interface(GtkApplication *app) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Gestor Git - GTK4 + XML Cifrado");
  gtk_window_set_default_size(GTK_WINDOW(window), 680, 620);

  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 15);
  gtk_widget_set_margin_start(grid, 20);
  gtk_widget_set_margin_end(grid, 20);
  gtk_widget_set_margin_top(grid, 20);
  gtk_widget_set_margin_bottom(grid, 20);

  int row = 0;

  // Clave maestra
  GtkWidget *lbl_clave = gtk_label_new("<b>Clave Maestra:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_clave), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_clave, 0, row, 1, 1);
  entry_clave = gtk_password_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_clave), "clave para cifrar/descifrar tokens");
  gtk_grid_attach(GTK_GRID(grid), entry_clave, 1, row++, 1, 1);

  // Directorio
  GtkWidget *lbl_dir = gtk_label_new("<b>Directorio Local:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_dir), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_dir, 0, row, 1, 1);
  entry_directorio = gtk_entry_new();
  GtkWidget *btn_sel = gtk_button_new_with_label("📁");
  g_signal_connect(btn_sel, "clicked", G_CALLBACK(on_select_folder), NULL);
  GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_box_append(GTK_BOX(hbox), entry_directorio);
  gtk_box_append(GTK_BOX(hbox), btn_sel);
  gtk_grid_attach(GTK_GRID(grid), hbox, 1, row++, 1, 1);

  // URL remoto
  GtkWidget *lbl_url = gtk_label_new("<b>URL Remoto:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_url), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_url, 0, row, 1, 1);
  entry_url = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), entry_url, 1, row++, 1, 1);

  // Rama
  GtkWidget *lbl_ram = gtk_label_new("<b>Rama:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_ram), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_ram, 0, row, 1, 1);
  entry_rama = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(entry_rama), "main");
  gtk_grid_attach(GTK_GRID(grid), entry_rama, 1, row++, 1, 1);

  // Mensaje commit
  GtkWidget *lbl_msg = gtk_label_new("<b>Mensaje Commit:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_msg), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_msg, 0, row, 1, 1);
  entry_mensaje = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid), entry_mensaje, 1, row++, 1, 1);

  // Token
  GtkWidget *lbl_tok = gtk_label_new("<b>Token GitHub:</b>");
  gtk_label_set_use_markup(GTK_LABEL(lbl_tok), TRUE);
  gtk_grid_attach(GTK_GRID(grid), lbl_tok, 0, row, 1, 1);
  entry_token = gtk_password_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(entry_token), "ghp_xxxxxxxx");
  gtk_grid_attach(GTK_GRID(grid), entry_token, 1, row++, 1, 1);

  check_guardar = gtk_check_button_new_with_label("Guardar credenciales cifradas (1 XML por repo)");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check_guardar), TRUE);
  gtk_grid_attach(GTK_GRID(grid), check_guardar, 1, row++, 1, 1);

  // Progreso y estado
  progress_bar = gtk_progress_bar_new();
  gtk_grid_attach(GTK_GRID(grid), progress_bar, 0, row++, 2, 1);
  label_estado = gtk_label_new("Listo");
  gtk_grid_attach(GTK_GRID(grid), label_estado, 0, row++, 2, 1);

  // Botón principal
  GtkWidget *btn_accion = gtk_button_new_with_label("⬆ SUBIR / SINCRONIZAR");
  g_signal_connect(btn_accion, "clicked", G_CALLBACK(run_git_task), NULL);
  gtk_widget_set_size_request(btn_accion, -1, 40);
  gtk_grid_attach(GTK_GRID(grid), btn_accion, 0, row++, 2, 1);

  // Registro (log)
  GtkWidget *frame_log = gtk_frame_new("Registro");
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scrolled, TRUE);
  text_view_log = gtk_text_view_new();
  buffer_log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_log));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_log), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_log), GTK_WRAP_WORD_CHAR);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), text_view_log);
  gtk_frame_set_child(GTK_FRAME(frame_log), scrolled);
  gtk_grid_attach(GTK_GRID(grid), frame_log, 0, row, 2, 1);

  gtk_window_set_child(GTK_WINDOW(window), grid);
  gtk_window_present(GTK_WINDOW(window));
}
