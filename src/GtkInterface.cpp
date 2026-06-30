/*
 * GtkInterface.cpp
 *  Created on: 30 jun 2026
 *  Author: usuario001 - modificado para cifrado + XML
 */

#include "GtkInterface.hpp"
#include "GestorGit.hpp"
#include "Cifrado.hpp"
#include "RepoConfig.hpp"

#include <gtk/gtk.h>
#include <thread>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// Widgets globales
static GtkWidget *entry_clave;
static GtkWidget *entry_directorio;
static GtkWidget *entry_url;
static GtkWidget *entry_rama;
static GtkWidget *entry_mensaje;
static GtkWidget *entry_token;
static GtkWidget *check_guardar;
static GtkWidget *label_estado;
static GtkWidget *progress_bar;
static GtkTextBuffer *buffer_log;
static GtkWidget *text_view_log;  // <-- NUEVO para GTK4

struct LogData {
  GtkWidget *label;
  gchar *texto;
};

void append_log(GtkTextBuffer *buffer, const char *mensaje) {
  if (!buffer) return;
  GtkTextIter iter;
  gtk_text_buffer_get_end_iter(buffer, &iter);
  gtk_text_buffer_insert(buffer, &iter, mensaje, -1);

  // Auto-scroll en GTK4 (usamos el text_view global)
  if (text_view_log) {
    gtk_text_buffer_get_end_iter(buffer, &iter);
    GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, &iter, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(text_view_log), mark, 0.0, FALSE, 0, 0);
    gtk_text_buffer_delete_mark(buffer, mark);
  }
}

gboolean actualizar_ui_idle(gpointer data) {
  auto params = static_cast<LogData *>(data);
  if (params->label && params->texto) {
    gtk_label_set_text(GTK_LABEL(params->label), params->texto);
  }
  g_free(params->texto);
  delete params;
  return G_SOURCE_REMOVE;
}

void run_git_task(GtkWidget *boton, gpointer user_data) {
  gtk_widget_set_sensitive(boton, FALSE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 0.0);
  append_log(buffer_log, "--- Iniciando operación ---\n");

  std::string clave = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
  std::string dir_path = gtk_editable_get_text(GTK_EDITABLE(entry_directorio));
  std::string url_repo = gtk_editable_get_text(GTK_EDITABLE(entry_url));
  std::string rama = gtk_editable_get_text(GTK_EDITABLE(entry_rama));
  std::string mensaje = gtk_editable_get_text(GTK_EDITABLE(entry_mensaje));
  std::string token = gtk_editable_get_text(GTK_EDITABLE(entry_token));
  bool guardar = gtk_check_button_get_active(GTK_CHECK_BUTTON(check_guardar));

  std::thread t([=]() {
    ResultadoOperacionGit resultado;
    try {
      // Guardar configuración cifrada
      if (guardar && !clave.empty() && !dir_path.empty()) {
        ConfigRepo cfg;
        cfg.url = url_repo;
        cfg.directorio = dir_path;
        cfg.rama = rama.empty() ? "main" : rama;
        cfg.tokenEncriptado = Cifrado::encriptar(token, clave);
        if (GestorConfig::guardar(cfg)) {
          append_log(buffer_log, "✓ Configuración guardada en XML cifrado\n");
        }
      }

      bool esRepoNuevo = !fs::exists(fs::path(dir_path) / ".git");

      if (esRepoNuevo) {
        append_log(buffer_log, "Repositorio nuevo detectado...\n");
        if (!url_repo.empty()) {
          append_log(buffer_log, "Clonando...\n");
          resultado = GestorGit::clonarRepositorio(url_repo, dir_path, token);
          if (resultado.exito) {
            append_log(buffer_log, "Clonación OK. Subiendo...\n");
            resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token);
          }
        } else {
          append_log(buffer_log, "Inicializando repo local...\n");
          resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token);
        }
      } else {
        append_log(buffer_log, "Repo existente. Subiendo cambios...\n");
        resultado = GestorGit::subirCambios(dir_path, rama, mensaje, token);
      }

      std::string msg_final = std::string("Resultado: ") + (resultado.exito ? "ÉXITO" : "FALLIDO") + "\n";
      msg_final += "Mensaje: " + resultado.mensaje + "\n\nSalida:\n" + resultado.salidaCompleta + "\n";

      LogData *ui = new LogData{ label_estado, g_strdup(msg_final.c_str()) };
      g_idle_add(actualizar_ui_idle, ui);
      append_log(buffer_log, msg_final.c_str());

      g_idle_add([](gpointer d) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), 1.0);
        gtk_widget_set_sensitive(GTK_WIDGET(d), TRUE);
        return G_SOURCE_REMOVE;
      }, boton);

    } catch (const std::exception &e) {
      std::string err = "Excepción: " + std::string(e.what());
      LogData *ui = new LogData{ label_estado, g_strdup(err.c_str()) };
      g_idle_add(actualizar_ui_idle, ui);
      g_idle_add([](gpointer d) {
        gtk_widget_set_sensitive(GTK_WIDGET(d), TRUE);
        return G_SOURCE_REMOVE;
      }, boton);
    }
  });
  t.detach();
}

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

        // Cargar XML si existe
        ConfigRepo cfg;
        if (GestorConfig::cargar(path, cfg)) {
          gtk_editable_set_text(GTK_EDITABLE(entry_url), cfg.url.c_str());
          gtk_editable_set_text(GTK_EDITABLE(entry_rama), cfg.rama.c_str());
          std::string clave = gtk_editable_get_text(GTK_EDITABLE(entry_clave));
          if (!cfg.tokenEncriptado.empty() && !clave.empty()) {
            std::string token = Cifrado::desencriptar(cfg.tokenEncriptado, clave);
            gtk_editable_set_text(GTK_EDITABLE(entry_token), token.c_str());
            append_log(buffer_log, "✓ Configuración cargada desde XML\n");
          }
        }
        g_free(path);
        g_object_unref(file);
      }
    },
    NULL);
}

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

  // URL
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

  // Mensaje
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

  // Progreso
  progress_bar = gtk_progress_bar_new();
  gtk_grid_attach(GTK_GRID(grid), progress_bar, 0, row++, 2, 1);
  label_estado = gtk_label_new("Listo");
  gtk_grid_attach(GTK_GRID(grid), label_estado, 0, row++, 2, 1);

  // Botón
  GtkWidget *btn_accion = gtk_button_new_with_label("⬆ SUBIR / SINCRONIZAR");
  g_signal_connect(btn_accion, "clicked", G_CALLBACK(run_git_task), NULL);
  gtk_widget_set_size_request(btn_accion, -1, 40);
  gtk_grid_attach(GTK_GRID(grid), btn_accion, 0, row++, 2, 1);

  // Log
  GtkWidget *frame_log = gtk_frame_new("Registro");
  GtkWidget *scrolled = gtk_scrolled_window_new();
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scrolled, TRUE);
  text_view_log = gtk_text_view_new();  // <-- guardamos referencia
  buffer_log = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view_log));
  gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view_log), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view_log), GTK_WRAP_WORD_CHAR);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), text_view_log);
  gtk_frame_set_child(GTK_FRAME(frame_log), scrolled);
  gtk_grid_attach(GTK_GRID(grid), frame_log, 0, row, 2, 1);

  gtk_window_set_child(GTK_WINDOW(window), grid);
  gtk_window_present(GTK_WINDOW(window));
}
